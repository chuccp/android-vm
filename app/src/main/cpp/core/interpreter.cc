// 解释器：switch 解释循环 + DoCall/StringFactory 重写 + 异常投递。
// 语义对照：
//   - interpreter_switch_impl-inl.h      主循环 / opcode 行为
//   - interpreter_common.cc:1192-1387    DoCallCommon（first_dest_reg/receiver/wide）
//   - interpreter_common.cc:283-315      SetStringInitValueToAllAliases
//   - art_method.cc:328-365              FindCatchBlock（exception.cc）
//
// Frame vreg 物理布局（重要）：
//   regs 是 uint32 槽；每个【字节码 vreg】占 2 个物理槽（64-bit payload）：
//     P(v) = regs[2v] | regs[2v+1] << 32（小端）
//   - 32 位值：写入零扩展，读取取低 32 位（有符号按需解释）
//   - wide（J/D）：完整 64 位存于 P(v)；字节码层面仍"占两 vreg 号"
//     （v+1 号被占用跳过，与 ART 指令布局一致），move-wide 同步复制 P(v+1)
//   - 引用：完整 64 位宿主指针存于 P(v)；null = 0
//   这样 32-bit vreg 槽宽下引用/wide 无损（宿主 64 位指针不可截断）。
//   regs 容量 = 2 * (registers_size + 1)；payload[registers_size] 为返回值槽。
#include "core/interpreter.h"

#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#if defined(__ANDROID__)
#include <android/log.h>
#include <unistd.h>
#endif

// 调用期诊断开关（默认 0）：源码中的 TEMP DIAG 块保留用于具体缺陷复现，
// 但它们在每次 guest 调用/字段读写上做描述符字符串比较（启动期实测可达
// 数秒级纯开销），默认不编译执行。需要归因时以 -DDEXVM_CALL_DIAGNOSTICS=1 构建。
#ifndef DEXVM_CALL_DIAGNOSTICS
#define DEXVM_CALL_DIAGNOSTICS 0
#endif

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/exception.h"
#include "core/intrinsic.h"
#include "dex/dex_file.h"
#include "dex/opcodes.h"

namespace dexvm::runtime {

namespace {

// ---- payload 访问 ----

inline uint64_t GetP(const Frame& f, uint32_t v) {
  return static_cast<uint64_t>(f.regs[2 * v]) |
         (static_cast<uint64_t>(f.regs[2 * v + 1]) << 32);
}

// vreg 最近写点环（CheckRef 槽来源注记；诊断升级用，语义零影响）：
// 每笔 vreg payload 写入登记 (frame, vreg, bits, dex_pc, method)；
// 脏引用上抛时按 (frame, vreg) 倒查最近写点，注记进 InternalError 消息
// ——「该 vreg 最近一次写点」。环定长（thread_local，多 GuestTask 各自独立）。
struct VregWriteRec {
  const Frame* frame = nullptr;
  const VMethod* method = nullptr;
  uint64_t bits = 0;
  uint32_t vreg = 0;
  uint32_t dex_pc = 0;
};
constexpr size_t kVregWriteRing = 64;
thread_local VregWriteRec g_vreg_writes[kVregWriteRing] = {};
thread_local uint32_t g_vreg_write_pos = 0;

// 诊断环默认关闭（DQ 性能面：ART 发布构建同样不带逐指令写点记录）。
// 归因需要时 -DDEXVM_VREG_DIAG=1 重新打开。
#ifndef DEXVM_VREG_DIAG
#define DEXVM_VREG_DIAG 0
#endif

inline void NoteVregWrite(const Frame& f, uint32_t v, uint64_t bits) {
#if DEXVM_VREG_DIAG
  VregWriteRec& rec = g_vreg_writes[g_vreg_write_pos++ & (kVregWriteRing - 1u)];
  rec.frame = &f;
  rec.method = f.method;
  rec.bits = bits;
  rec.vreg = v;
  rec.dex_pc = f.dex_pc;
#else
  (void)f;
  (void)v;
  (void)bits;
#endif
}

// 倒查 (frame, vreg) 最近写点 → 文本（无命中返回 nullptr）
const char* VregSrcNote(const Frame& frame, uint32_t vreg, char* buf, size_t buf_sz);

inline void SetP(Frame& f, uint32_t v, uint64_t bits) {
  f.regs[2 * v] = static_cast<uint32_t>(bits);
  f.regs[2 * v + 1] = static_cast<uint32_t>(bits >> 32);
  NoteVregWrite(f, v, bits);
}

// wide 值写入（头注释布局不变式：P(v+1) 与 P(v) 同步镜像）。
// v+1 越出 vreg 文件（return 槽或更后）时只写 P(v)——return 槽不是 wide 对。
inline void SetWideP(Frame& f, uint32_t v, uint64_t bits) {
  SetP(f, v, bits);
  // regs 容量 = 2*(nregs+1)，vreg 文件 = 2*nregs 槽；v+1 必须仍在 vreg 文件内
  if (2 * (v + 1) + 1 < f.regs.size() - 2) {
    SetP(f, v + 1, bits);
  }
}

inline int32_t GetI(const Frame& f, uint32_t v) { return static_cast<int32_t>(GetP(f, v)); }

inline void SetI(Frame& f, uint32_t v, int32_t x) {
  SetP(f, v, static_cast<uint32_t>(x));  // 零扩展高位
}

inline int64_t GetL(const Frame& f, uint32_t v) { return static_cast<int64_t>(GetP(f, v)); }

inline void SetL(Frame& f, uint32_t v, int64_t x) { SetWideP(f, v, static_cast<uint64_t>(x)); }

inline ObjHeader* GetRef(const Frame& f, uint32_t v) {
  return reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(GetP(f, v)));
}

inline void SetRef(Frame& f, uint32_t v, ObjHeader* o) {
  SetP(f, v, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(o)));
}

inline float GetF(const Frame& f, uint32_t v) {
  uint32_t bits = static_cast<uint32_t>(GetP(f, v));
  float out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

inline void SetF(Frame& f, uint32_t v, float x) {
  uint32_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  SetP(f, v, bits);
}

inline double GetD(const Frame& f, uint32_t v) {
  uint64_t bits = GetP(f, v);
  double out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

inline void SetD(Frame& f, uint32_t v, double x) {
  uint64_t bits;
  std::memcpy(&bits, &x, sizeof(bits));
  SetWideP(f, v, bits);
}

// ---- 脏引用防御（docs 03 章 12；对照 gc.cc IsHeapStart 判别法）----
// 非空引用必须命中堆对象起始地址（Alloc 登记表）。半槽写/悬垂/垃圾位型
// （如 0xb400007a00000000：指针高 32 位 + 低 32 位清零）按 InternalError 语义
// 带位型+来源诊断上抛，不裸解引用 SIGSEGV（ART 对野引用 abort 的可恢复投影）。
// 诊断升级（卫生墙注记；语义不变——仍只做 IsHeapStart 判真，修语义不修防御）：
//   位型   ClassifyPtr 形态（reclaimed = 清扫残骸 = 死对象槽；interior = 半写/错位）
//   来源   槽来源注记：iget/sget 给出字段槽（declaring/name/type/@offset/receiver），
//          vreg 面倒查最近写点环（该 vreg 最近一次写点 pc/method/bits）
const char* PtrShapeName(mem::Heap::PtrShape s) {
  switch (s) {
    case mem::Heap::PtrShape::kNull: return "null";
    case mem::Heap::PtrShape::kHeapStart: return "heap-start";
    case mem::Heap::PtrShape::kHeapInterior: return "heap-interior";
    case mem::Heap::PtrShape::kReclaimed: return "reclaimed-block";
    case mem::Heap::PtrShape::kOutside: return "outside-heap";
  }
  return "?";
}

const char* VregSrcNote(const Frame& frame, uint32_t vreg, char* buf, size_t buf_sz) {
  for (uint32_t i = 0; i < kVregWriteRing; i++) {
    // 倒查：最近写点优先（环位从写点-1 回溯）
    uint32_t idx = (g_vreg_write_pos - 1u - i) & (kVregWriteRing - 1u);
    const VregWriteRec& rec = g_vreg_writes[idx];
    if (rec.frame == &frame && rec.vreg == vreg) {
      std::snprintf(buf, buf_sz, "vreg-src=pc=%u in %s%s wrote 0x%016llx", rec.dex_pc,
                    rec.method != nullptr && rec.method->declaring != nullptr
                        ? rec.method->declaring->descriptor.c_str()
                        : "?",
                    rec.method != nullptr ? rec.method->name.c_str() : "?",
                    static_cast<unsigned long long>(rec.bits));
      return buf;
    }
  }
  return "vreg-src=<no write recorded>";
}

inline bool CheckRef(VMState& state, const Frame& frame, ObjHeader* obj, uint32_t vreg,
                     const char* what, const char* slot_note = nullptr) {
  if (obj == nullptr) return true;
  // 精确起点判别：范围快路径会放过“堆内中部指针”（半写/槽错位的典型形态），
  // 这类脏引用进入虚调用后会以垃圾 klass 做派发缓存写入（野写）——必须保留
  // IsHeapStart 精确判真（-O2 下脏引用曾直接踩栈 canary 实锤）。
  if (state.heap != nullptr && state.heap->IsHeapStart(obj)) return true;
  const void* owner = nullptr;
  size_t owner_off = 0;
  mem::Heap::PtrShape shape = mem::Heap::PtrShape::kOutside;
  if (state.heap != nullptr) shape = state.heap->ClassifyPtr(obj, &owner, &owner_off);
  char src[192];
  if (slot_note == nullptr) slot_note = VregSrcNote(frame, vreg, src, sizeof(src));
  char extra[256];
  if (shape == mem::Heap::PtrShape::kHeapInterior) {
    std::snprintf(extra, sizeof(extra), " [shape=%s owner=0x%016llx +%zu; %s]",
                  PtrShapeName(shape), static_cast<unsigned long long>(
                                            reinterpret_cast<uintptr_t>(owner)),
                  owner_off, slot_note);
  } else {
    std::snprintf(extra, sizeof(extra), " [shape=%s; %s]", PtrShapeName(shape), slot_note);
  }
  char msg[560];
  std::snprintf(msg, sizeof(msg),
                "dirty reference 0x%016llx at v%u pc=%u in %s%s (%s)%s",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(obj)), vreg,
                frame.dex_pc,
                frame.method != nullptr && frame.method->declaring != nullptr
                    ? frame.method->declaring->descriptor.c_str()
                    : "?",
                frame.method != nullptr ? frame.method->name.c_str() : "?", what, extra);
  state.ThrowNew("Ljava/lang/InternalError;", msg);
  return false;
}

// ---- 数值语义（Java 窄化/除法）----

inline int32_t Sgn4(uint32_t x) {
  uint32_t v = x & 0xfu;
  return (v & 0x8u) != 0 ? static_cast<int32_t>(v | 0xfffffff0u) : static_cast<int32_t>(v);
}
inline int32_t Sgn8(uint32_t x) { return static_cast<int32_t>(static_cast<int8_t>(x)); }
inline int32_t Sgn16(uint32_t x) { return static_cast<int32_t>(static_cast<int16_t>(x)); }

bool IntDiv(VMState& state, int32_t a, int32_t b, int32_t* out) {
  if (b == 0) {
    ThrowArithmeticException(state, "divide by zero");
    return false;
  }
  if (a == INT32_MIN && b == -1) {
    *out = INT32_MIN;  // Java：不抛异常
    return true;
  }
  *out = a / b;
  return true;
}

bool IntRem(VMState& state, int32_t a, int32_t b, int32_t* out) {
  if (b == 0) {
    ThrowArithmeticException(state, "divide by zero");
    return false;
  }
  if (a == INT32_MIN && b == -1) {
    *out = 0;
    return true;
  }
  *out = a % b;
  return true;
}

bool LongDiv(VMState& state, int64_t a, int64_t b, int64_t* out) {
  if (b == 0) {
    ThrowArithmeticException(state, "divide by zero");
    return false;
  }
  if (a == INT64_MIN && b == -1) {
    *out = INT64_MIN;
    return true;
  }
  *out = a / b;
  return true;
}

bool LongRem(VMState& state, int64_t a, int64_t b, int64_t* out) {
  if (b == 0) {
    ThrowArithmeticException(state, "divide by zero");
    return false;
  }
  if (a == INT64_MIN && b == -1) {
    *out = 0;
    return true;
  }
  *out = a % b;
  return true;
}

int32_t NarrowFloatToInt(float x) {
  if (std::isnan(x)) return 0;
  if (x >= 2147483647.0f) return INT32_MAX;
  if (x <= -2147483648.0f) return INT32_MIN;
  return static_cast<int32_t>(x);
}

int64_t NarrowFloatToLong(float x) {
  if (std::isnan(x)) return 0;
  if (x >= 9223372036854775808.0) return INT64_MAX;
  if (x <= -9223372036854775808.0) return INT64_MIN;
  return static_cast<int64_t>(x);
}

int32_t NarrowDoubleToInt(double x) {
  if (std::isnan(x)) return 0;
  if (x >= 2147483647.0) return INT32_MAX;
  if (x <= -2147483648.0) return INT32_MIN;
  return static_cast<int32_t>(x);
}

int64_t NarrowDoubleToLong(double x) {
  if (std::isnan(x)) return 0;
  if (x >= 9223372036854775808.0) return INT64_MAX;
  if (x <= -9223372036854775808.0) return INT64_MIN;
  return static_cast<int64_t>(x);
}

// ---- 槽位窄化读写（aget/iget/sget 家族，8 字节槽）----

enum class Narrow : uint8_t { kWord, kWide, kRef, kBool, kByte, kChar, kShort };

uint64_t NarrowRead(Narrow n, uint64_t slot) {
  switch (n) {
    case Narrow::kWide:
    case Narrow::kRef:
      return slot;
    case Narrow::kBool:
      return (slot & 0xffu) != 0 ? 1u : 0u;
    case Narrow::kByte:
      return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(slot)));
    case Narrow::kChar:
      return static_cast<uint32_t>(slot & 0xffffu);
    case Narrow::kShort:
      return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(slot)));
    case Narrow::kWord:
    default:
      return static_cast<uint32_t>(slot);
  }
}

uint64_t NarrowWrite(Narrow n, uint64_t bits) {
  switch (n) {
    case Narrow::kWide:
    case Narrow::kRef:
      return bits;
    case Narrow::kBool:
      return bits & 0xffu;
    case Narrow::kByte:
      return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(bits)));
    case Narrow::kChar:
      return bits & 0xffffu;
    case Narrow::kShort:
      return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(bits)));
    case Narrow::kWord:
    default:
      return static_cast<uint32_t>(bits);
  }
}

uint64_t LoadSlot(const void* p) {
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

void StoreSlot(void* p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }

// ---- 类初始化（<clinit> 失败包装 ExceptionInInitializerError）----

bool EnsureClassInitialized(VMState& state, VClass* klass) {
  if (klass == nullptr) {
    ThrowUnsupportedOperationException(state, "null class");
    return false;
  }
  if (klass->status == ClassStatus::kInitialized) return true;
  // erroneous 语义（docs 03 章 12，Android 行为）：<clinit> 曾失败的类
  // active use 抛 NoClassDefFoundError，【不再重跑 <clinit>】。不收口则
  // InitializeClass 失败路径（状态退回 kResolved）会让失败 <clinit> 反复
  // 重跑——每次重跑把该类的静态字面量解密清单原地重走一遍（"循环不推进"
  // 周期打转的机制来源）。
  if (IsClinitFailed(klass)) {
    state.ThrowNew("Ljava/lang/NoClassDefFoundError;",
                   klass->descriptor + " (erroneous: <clinit> previously failed)");
    return false;
  }
  if (!CheckClassDefinition(state, klass)) return false;
  if (state.linker == nullptr) return true;
  if (state.linker->InitializeClass(klass, &state)) return true;
  // <clinit> 异常：登记 erroneous（此后 active use 短路，不再重跑）
  if (state.HasPendingException()) NoteClinitFailure(klass);
  // docs 03 章 12：<clinit> 异常包装为 ExceptionInInitializerError。
  // 诊断面：内层异常类+消息并入包装消息（ExceptionInInitializerError cause
  // 语义的可观察投影；缺口归因需要根因，不丢内层）。
  ObjHeader* cause = state.pending_exception;
  std::string cause_diag;
  if (cause != nullptr && cause->klass != nullptr) {
    cause_diag = cause->klass->descriptor;
    VObject* cobj = reinterpret_cast<VObject*>(cause);
    for (VClass* k = cobj->klass; k != nullptr; k = k->super_class) {
      bool done = false;
      for (const auto& f : k->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* s = nullptr;
          std::memcpy(&s, cobj->field_data() + f.offset, sizeof(s));
          if (s != nullptr) {
            cause_diag += ": ";
            cause_diag += static_cast<VString*>(s)->ToModifiedUtf8();
          }
          done = true;
          break;
        }
      }
      if (done) break;
    }
  }
#if defined(__ANDROID__)
  // <clinit> 根因诊断：内层异常 + 抛点栈（OkHttp Util/OkHttpClient 等被
  // erroneous 短路后表象为 NoClassDefFound，根因只在此可见）。
  __android_log_print(ANDROID_LOG_INFO, "DexVmClinit", "%s <clinit> failed <- %s",
                      klass->descriptor.c_str(),
                      cause_diag.empty() ? "(no cause)" : cause_diag.c_str());
  for (size_t i = 0; i < state.last_throw_backtrace.size() && i < 16; i++) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmClinit", "  at %s",
                        state.last_throw_backtrace[i].c_str());
  }
#endif
  state.ClearException();
  state.ThrowNew("Ljava/lang/ExceptionInInitializerError;",
                 klass->descriptor + " <clinit> failed" +
                     (cause_diag.empty() ? "" : " <- " + cause_diag));
  return false;
}

// ---- 解析失败诊断（错误消息带未解析成员，对照 ART LinkError 文本语义）----

std::string TypeRefStr(const dex::DexFile* dex, uint32_t type_idx) {
  return dex != nullptr ? std::string(dex->GetTypeDescriptor(type_idx)) : "?";
}

// const-class/check-cast/instance-of/new-array 等类型解析失败统一抛点：
// 消息并入 linker.last_error()（缺具体缺失面时归因悬空——残骸类/壳 loadClass
// 失败的根因坐标）。ResolveType 传真实 state：Java loadClass 回落路径的异常
// 语义/诊断依赖它。
void ThrowTypeResolveError(VMState& state, const dex::DexFile* dex, uint32_t type_idx) {
  std::string desc = TypeRefStr(dex, type_idx);
  std::string why = state.linker != nullptr ? state.linker->last_error() : "";
  state.ThrowNew("Ljava/lang/NoClassDefFoundError;",
                 desc + (why.empty() ? "" : " (define/link failed: " + why + ")"));
}

std::string MethodRefStr(const dex::DexFile* dex, uint32_t method_idx) {
  if (dex == nullptr) return "?";
  const dex::MethodId& mid = dex->GetMethodId(method_idx);
  std::string s(dex->GetTypeDescriptor(mid.class_idx));
  s += ".";
  s += dex->GetString(mid.name_idx);
  s += "(";
  for (uint32_t i = 0; i < dex->GetProtoParamCount(mid.proto_idx); i++) {
    s += dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(mid.proto_idx, i));
  }
  s += ")";
  s += dex->GetTypeDescriptor(dex->GetProto(mid.proto_idx).return_type_idx);
  return s;
}

std::string FieldRefStr(const dex::DexFile* dex, uint32_t field_idx) {
  if (dex == nullptr) return "?";
  const dex::FieldId& fid = dex->GetFieldId(field_idx);
  std::string s(dex->GetTypeDescriptor(fid.class_idx));
  s += ".";
  s += dex->GetString(fid.name_idx);
  s += ":";
  s += dex->GetTypeDescriptor(fid.type_idx);
  return s;
}

// 方法签名文本（declaring 名 + 名称 + proto）
std::string MethodSigStr(const VMethod* m) {
  if (m == nullptr) return "?";
  std::string s;
  if (m->declaring != nullptr) {
    s += m->declaring->descriptor;
    s += ".";
  }
  s += m->name;
  s += "(";
  for (const auto& p : m->param_types) s += p;
  s += ")";
  s += m->return_type;
  return s;
}

// ---- 派发辅助 ----

bool SameSignature(const VMethod& a, const VMethod& b) {
  return a.name == b.name && a.return_type == b.return_type && a.param_types == b.param_types;
}

// invoke-super：从 caller 的父类起向上找同签名实现（对照 FindSuperMethodToCall）
// 无码抽象实现跳过（<init>/<clinit> 骨架除外：空实现可直接执行）
VMethod* FindSuperMethod(VClass* caller_class, VMethod* resolved) {
  for (VClass* k = (caller_class != nullptr) ? caller_class->super_class : nullptr; k != nullptr;
       k = k->super_class) {
    for (auto& m : k->virtual_methods) {
      bool callable = m.HasCode() || m.name == "<init>" || m.name == "<clinit>";
      if (callable && SameSignature(m, *resolved)) return &m;
    }
    for (auto& m : k->direct_methods) {
      bool callable = m.HasCode() || m.name == "<init>" || m.name == "<clinit>";
      if (!m.IsStatic() && callable && SameSignature(m, *resolved)) return &m;
    }
  }
  return resolved;
}

// 接口 default 方法深搜（Java 8 语义）：FindInterfaceImpl 只查类链 + 直接接口，
// 漏掉【超接口】携带的 default 体（如 List 接收者继承 Collection.isEmpty）。
// 命中抽象无码声明时沿接口闭包（含超接口）找带码同签名实现回落派发。
VMethod* FindImplDeep(VClass* k, const VMethod& target) {
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& m : c->virtual_methods) {
      if (m.HasImplementation() && SameSignature(m, target)) return &m;
    }
    for (auto& m : c->direct_methods) {
      if (!m.IsStatic() && m.HasImplementation() && SameSignature(m, target)) return &m;
    }
    for (VClass* iface : c->interfaces) {
      std::vector<VClass*> stack{iface};
      while (!stack.empty()) {
        VClass* cur = stack.back();
        stack.pop_back();
        if (cur == nullptr) continue;
        for (auto& m : cur->virtual_methods) {
          if (m.HasImplementation() && SameSignature(m, target)) return &m;
        }
        for (VClass* pi : cur->interfaces) stack.push_back(pi);
      }
    }
  }
  return nullptr;
}

// ---- invoke 操作数（35c 五寄存器 / 3rc 连续）----

struct CallSite {
  bool is_range = false;
  uint16_t count = 0;   // 参数 word 数（A）
  uint32_t reg[5] = {};  // 35c：按 word 索引
  uint32_t base = 0;    // 3rc：首寄存器
  uint32_t idx = 0;     // method/type idx
};

CallSite DecodeCallSite35c(const uint16_t* ins) {
  CallSite cs;
  cs.is_range = false;
  cs.count = static_cast<uint16_t>(ins[0] >> 12);
  cs.idx = ins[1];
  uint32_t reg_list = ins[2];
  cs.reg[0] = reg_list & 0xfu;
  cs.reg[1] = (reg_list >> 4) & 0xfu;
  cs.reg[2] = (reg_list >> 8) & 0xfu;
  cs.reg[3] = (reg_list >> 12) & 0xfu;
  cs.reg[4] = (ins[0] >> 8) & 0xfu;  // G
  return cs;
}

CallSite DecodeCallSite3rc(const uint16_t* ins) {
  CallSite cs;
  cs.is_range = true;
  cs.count = static_cast<uint16_t>(ins[0] >> 8);
  cs.idx = ins[1];
  cs.base = ins[2];
  return cs;
}

uint32_t WordReg(const CallSite& cs, uint32_t word) {
  return cs.is_range ? cs.base + word : cs.reg[word % 5];
}

enum class InvokeKind { kStatic, kDirect, kSuper, kVirtual, kInterface };

// ---- StringFactory 重写（docs 12 章 22.5）----

// 字节流按 UTF-8 解码（malformed -> U+FFFD，对照 newStringFromUtf8Bytes）
void Utf8ToChars(const std::vector<uint8_t>& bytes, std::u16string* out) {
  size_t i = 0;
  const size_t n = bytes.size();
  while (i < n) {
    uint8_t b = bytes[i];
    if (b < 0x80) {
      out->push_back(static_cast<char16_t>(b));
      i++;
      continue;
    }
    int extra;
    uint32_t cp;
    if ((b & 0xE0) == 0xC0) {
      extra = 1;
      cp = b & 0x1Fu;
    } else if ((b & 0xF0) == 0xE0) {
      extra = 2;
      cp = b & 0x0Fu;
    } else if ((b & 0xF8) == 0xF0) {
      extra = 3;
      cp = b & 0x07u;
    } else {
      out->push_back(0xFFFD);
      i++;
      continue;
    }
    if (i + extra >= n) {  // 截断
      out->push_back(0xFFFD);
      i++;
      continue;
    }
    bool ok = true;
    for (int k = 1; k <= extra; k++) {
      uint8_t c = bytes[i + k];
      if ((c & 0xC0) != 0x80) {
        ok = false;
        break;
      }
      cp = (cp << 6) | (c & 0x3Fu);
    }
    if (!ok) {
      out->push_back(0xFFFD);
      i++;
      continue;
    }
    i += extra + 1;
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out->push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out->push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out->push_back(static_cast<char16_t>(cp));
    }
  }
}

// 字符集名归一（大小写折叠 + 丢弃 -_. 空格；与 builtins_string 同口径）
std::string NormalizeCharsetForName(const uint16_t* chars, int32_t len) {
  std::string out;
  for (int32_t i = 0; i < len; i++) {
    char16_t c = chars[i];
    if (c == u'-' || c == u'_' || c == u'.' || c == u' ') continue;
    if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - u'a' + u'A');
    if (c > 0x7F) continue;  // 非 ASCII 名不进本地快路径
    out.push_back(static_cast<char>(c));
  }
  return out;
}

bool DecodeUtf16Bytes(const std::vector<uint8_t>& bytes, bool little, std::u16string* out) {
  size_t n = bytes.size();
  size_t i = 0;
  while (i + 1 < n) {
    uint16_t unit = little
                        ? static_cast<uint16_t>(bytes[i] | (bytes[i + 1] << 8))
                        : static_cast<uint16_t>((bytes[i] << 8) | bytes[i + 1]);
    out->push_back(static_cast<char16_t>(unit));
    i += 2;
  }
  if (i < n) out->push_back(0xFFFD);
  return true;
}

// 常见字符集本地解码（UTF-8/ASCII/Latin-1/UTF-16 族）；未知返回 false。
bool DecodeByCharsetName(const std::vector<uint8_t>& bytes, const std::string& name,
                         std::u16string* out) {
  if (name == "UTF8" || name == "UNICODE11UTF8") {
    Utf8ToChars(bytes, out);
    return true;
  }
  if (name == "USASCII" || name == "ASCII" || name == "ISO646US" || name == "ANSIX341968") {
    for (uint8_t b : bytes) out->push_back(b < 0x80 ? static_cast<char16_t>(b) : 0xFFFD);
    return true;
  }
  if (name == "ISO88591" || name == "LATIN1" || name == "88591" || name == "CSISOLATIN1") {
    for (uint8_t b : bytes) out->push_back(static_cast<char16_t>(b));
    return true;
  }
  if (name == "UTF16BE" || name == "UNICODEBIGUNMARKED") {
    return DecodeUtf16Bytes(bytes, /*little=*/false, out);
  }
  if (name == "UTF16LE" || name == "UNICODELITTLEUNMARKED") {
    return DecodeUtf16Bytes(bytes, /*little=*/true, out);
  }
  if (name == "UTF16") {
    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
      std::vector<uint8_t> rest(bytes.begin() + 2, bytes.end());
      return DecodeUtf16Bytes(rest, /*little=*/false, out);
    }
    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
      std::vector<uint8_t> rest(bytes.begin() + 2, bytes.end());
      return DecodeUtf16Bytes(rest, /*little=*/true, out);
    }
    return DecodeUtf16Bytes(bytes, /*little=*/false, out);
  }
  return false;
}

// 取 byte[] 实参（可选子区间）；失败时挂 pending exception 并返回 false。
bool StringBytesArg(VMState& state, Frame& frame, uint32_t vreg, int32_t offset, int32_t count,
                    bool whole, std::vector<uint8_t>* out) {
  ObjHeader* src = GetRef(frame, vreg);
  if (src != nullptr && !CheckRef(state, frame, src, vreg, "StringFactory arg")) return false;
  if (src == nullptr) {
    ThrowNullPointerException(state, "data == null");
    return false;
  }
  VArray* arr = static_cast<VArray*>(src);
  if (whole) {
    offset = 0;
    count = arr->length;
  }
  if (offset < 0 || count < 0 || offset > arr->length || count > arr->length - offset) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "offset=" + std::to_string(offset) + ",count=" + std::to_string(count) +
                       ",length=" + std::to_string(arr->length));
    return false;
  }
  out->reserve(static_cast<size_t>(count));
  for (int32_t i = 0; i < count; i++) {
    out->push_back(
        static_cast<uint8_t>(LoadSlot(arr->data() + (offset + i) * 8) & 0xffu));
  }
  return true;
}

// Charset 对象 → 归一字符集名（name() 虚拟调用）；失败返回 false。
bool CharsetObjectName(VMState& state, VObject* charset, std::string* name_out) {
  if (charset == nullptr || charset->klass == nullptr) return false;
  VMethod probe;
  probe.name = "name";
  probe.param_types = {};
  probe.return_type = "Ljava/lang/String;";
  VMethod* m = charset->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return false;
  Value args[1] = {};
  args[0].ref = charset;
  Value r{};
  if (!InvokeMethod(state, m, args, 1, &r) || r.ref == nullptr) return false;
  VString* s = static_cast<VString*>(r.ref);
  *name_out = NormalizeCharsetForName(s->chars(), s->length);
  return true;
}

bool DoStringFactory(VMState& state, Frame& frame, VMethod* resolved, const CallSite& cs) {
  // 丢 this：参数自 word 1 起（对照 DoCallCommon string_init 重写 + vregC 修正）
  std::u16string chars;
  const std::vector<std::string>& pt = resolved->param_types;
  if (pt.empty()) {
    // <init>() -> 空串
  } else if (pt.size() == 1 && pt[0] == "Ljava/lang/String;") {
    ObjHeader* src = GetRef(frame, WordReg(cs, 1));
    if (src != nullptr && !CheckRef(state, frame, src, WordReg(cs, 1), "StringFactory arg")) {
      return false;
    }
    if (src == nullptr) {
      ThrowNullPointerException(state, "toCopy == null");
      return false;
    }
    VString* s = static_cast<VString*>(src);
    chars.assign(s->chars(), s->chars() + s->length);
  } else if (pt.size() == 1 && pt[0] == "[C") {
    ObjHeader* src = GetRef(frame, WordReg(cs, 1));
    if (src != nullptr && !CheckRef(state, frame, src, WordReg(cs, 1), "StringFactory arg")) {
      return false;
    }
    if (src == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    VArray* arr = static_cast<VArray*>(src);
    chars.reserve(static_cast<size_t>(arr->length));
    for (int32_t i = 0; i < arr->length; i++) {
      chars.push_back(static_cast<char16_t>(LoadSlot(arr->data() + i * 8) & 0xffffu));
    }
  } else if (pt.size() == 3 && pt[0] == "[C" && pt[1] == "I" && pt[2] == "I") {
    ObjHeader* src = GetRef(frame, WordReg(cs, 1));
    if (src != nullptr && !CheckRef(state, frame, src, WordReg(cs, 1), "StringFactory arg")) {
      return false;
    }
    if (src == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    VArray* arr = static_cast<VArray*>(src);
    int32_t offset = GetI(frame, WordReg(cs, 2));
    int32_t count = GetI(frame, WordReg(cs, 3));  // word0=this, 1=char[], 2=offset, 3=count
    if (offset < 0 || count < 0 || offset > arr->length || count > arr->length - offset) {
      state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                     "length=" + std::to_string(arr->length) +
                         "; regionStart=" + std::to_string(offset) +
                         "; regionLength=" + std::to_string(count));
      return false;
    }
    for (int32_t i = 0; i < count; i++) {
      chars.push_back(static_cast<char16_t>(
          LoadSlot(arr->data() + (offset + i) * 8) & 0xffffu));
    }
  } else if (pt.size() == 1 && pt[0] == "[B") {
    ObjHeader* src = GetRef(frame, WordReg(cs, 1));
    if (src != nullptr && !CheckRef(state, frame, src, WordReg(cs, 1), "StringFactory arg")) {
      return false;
    }
    if (src == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    VArray* arr = static_cast<VArray*>(src);
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(arr->length));
    for (int32_t i = 0; i < arr->length; i++) {
      bytes.push_back(static_cast<uint8_t>(LoadSlot(arr->data() + i * 8) & 0xffu));
    }
    Utf8ToChars(bytes, &chars);
  } else if (pt.size() == 3 && pt[0] == "[B" && pt[1] == "I" && pt[2] == "I") {
    // String(byte[], offset, count)：默认 UTF-8 解码（libcore newStringFromUtf8Bytes）
    ObjHeader* src = GetRef(frame, WordReg(cs, 1));
    if (src != nullptr && !CheckRef(state, frame, src, WordReg(cs, 1), "StringFactory arg")) {
      return false;
    }
    if (src == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    VArray* arr = static_cast<VArray*>(src);
    int32_t offset = GetI(frame, WordReg(cs, 2));
    int32_t count = GetI(frame, WordReg(cs, 3));
    if (offset < 0 || count < 0 || offset > arr->length || count > arr->length - offset) {
      state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                     "offset=" + std::to_string(offset) + ",count=" + std::to_string(count) +
                         ",length=" + std::to_string(arr->length));
      return false;
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; i++) {
      bytes.push_back(static_cast<uint8_t>(LoadSlot(arr->data() + (offset + i) * 8) & 0xffu));
    }
    Utf8ToChars(bytes, &chars);
  } else if ((pt.size() == 2 || pt.size() == 4) && pt[0] == "[B" &&
             pt[pt.size() - 1] == "Ljava/nio/charset/Charset;") {
    // String(byte[] / byte[],off,len, Charset)：Okio readString / JSON 解码
    std::vector<uint8_t> bytes;
    const bool whole = pt.size() == 2;
    int32_t offset = whole ? 0 : GetI(frame, WordReg(cs, 2));
    int32_t count = whole ? 0 : GetI(frame, WordReg(cs, 3));
    if (!StringBytesArg(state, frame, WordReg(cs, 1), offset, count, whole, &bytes)) return false;
    VObject* charset = reinterpret_cast<VObject*>(GetRef(frame, WordReg(cs, pt.size())));
    if (charset == nullptr) {
      ThrowNullPointerException(state, "charset == null");
      return false;
    }
    std::string name;
    if (!CharsetObjectName(state, charset, &name) || !DecodeByCharsetName(bytes, name, &chars)) {
      // 未知字符集：保守回落 UTF-8（JSON 响应实际字符集恒 UTF-8）
      if (state.HasPendingException()) state.ClearException();
      chars.clear();
      Utf8ToChars(bytes, &chars);
    }
  } else if ((pt.size() == 2 || pt.size() == 4) && pt[0] == "[B" &&
             pt[pt.size() - 1] == "Ljava/lang/String;") {
    // String(byte[] / byte[],off,len, String charsetName)（受检异常语义）
    std::vector<uint8_t> bytes;
    const bool whole = pt.size() == 2;
    int32_t offset = whole ? 0 : GetI(frame, WordReg(cs, 2));
    int32_t count = whole ? 0 : GetI(frame, WordReg(cs, 3));
    if (!StringBytesArg(state, frame, WordReg(cs, 1), offset, count, whole, &bytes)) return false;
    ObjHeader* name_obj = GetRef(frame, WordReg(cs, pt.size()));
    if (name_obj == nullptr) {
      ThrowNullPointerException(state, "charsetName == null");
      return false;
    }
    VString* name_str = static_cast<VString*>(name_obj);
    std::string name = NormalizeCharsetForName(name_str->chars(), name_str->length);
    if (!DecodeByCharsetName(bytes, name, &chars)) {
      std::string raw;
      for (int32_t i = 0; i < name_str->length; i++) {
        char16_t c = name_str->chars()[i];
        raw.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
      }
      state.ThrowNew("Ljava/io/UnsupportedEncodingException;", raw);
      return false;
    }
  } else if (pt.size() == 1 && (pt[0] == "Ljava/lang/StringBuilder;" ||
                                pt[0] == "Ljava/lang/StringBuffer;" ||
                                pt[0] == "Ljava/lang/CharSequence;")) {
    // String(StringBuilder/StringBuffer/CharSequence)：取 toString 内容拷贝
    ObjHeader* src = GetRef(frame, WordReg(cs, 1));
    if (src != nullptr && !CheckRef(state, frame, src, WordReg(cs, 1), "StringFactory arg")) {
      return false;
    }
    if (src == nullptr) {
      ThrowNullPointerException(state, "toCopy == null");
      return false;
    }
    if (src->klass != nullptr && src->klass->descriptor == "Ljava/lang/StringBuilder;") {
      // StringBuilder 内部 char[] value + int count（builtins.cc 布局）
      const uint8_t* base = static_cast<VObject*>(src)->field_data();
      uint64_t slot;
      std::memcpy(&slot, base + 0, sizeof(slot));
      VArray* buf = reinterpret_cast<VArray*>(static_cast<uintptr_t>(slot));
      std::memcpy(&slot, base + 8, sizeof(slot));
      int32_t count = static_cast<int32_t>(slot);
      if (buf != nullptr) {
        for (int32_t i = 0; i < count && i < buf->length; i++) {
          chars.push_back(static_cast<char16_t>(LoadSlot(buf->data() + i * 8) & 0xffffu));
        }
      }
    } else {
      // CharSequence/其他：toString 虚拟调用
      VMethod probe;
      probe.name = "toString";
      probe.param_types = {};
      probe.return_type = "Ljava/lang/String;";
      VMethod* m = src->klass != nullptr ? src->klass->FindInterfaceImpl(&probe) : nullptr;
      if (m == nullptr) {
        ThrowUnsupportedOperationException(state, "String.<init>(CharSequence): no toString");
        return false;
      }
      Value call_args[1] = {};
      call_args[0].ref = src;
      Value r{};
      if (!InvokeMethod(state, m, call_args, 1, &r)) return false;
      VString* s = r.ref != nullptr ? static_cast<VString*>(r.ref) : nullptr;
      if (s != nullptr) chars.assign(s->chars(), s->chars() + s->length);
    }
  } else {
    ThrowUnsupportedOperationException(state, "String.<init> signature not supported");
    return false;
  }

  VString* created = nullptr;
  if (state.linker != nullptr) created = state.linker->AllocString(chars);
  if (created == nullptr) {
    ThrowOOM(state, "StringFactory allocation failed");
    return false;
  }

  // 结果写回 this vreg 及持有同一旧引用的所有 alias vreg
  // （对照 interpreter_common.cc:283-315 SetStringInitValueToAllAliases）
  uint32_t this_vreg = WordReg(cs, 0);
  uint64_t old_bits = GetP(frame, this_vreg);
  uint64_t new_bits = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(created));
  if (old_bits == 0) {
    SetP(frame, this_vreg, new_bits);
  } else {
    for (uint32_t i = 0; i < frame.method->code->registers_size; i++) {
      if (GetP(frame, i) == old_bits) SetP(frame, i, new_bits);
    }
  }
  return true;
}

namespace {
// 启动期慢方法采样：DoCall 入口记录当前 guest 方法（仅 profile 开启时做
// 原子交换）；宿主侧采样线程周期性读取，定位 MainActivity.onCreate 内的热点。
std::atomic<bool> g_method_profile_on{false};
std::atomic<VMethod*> g_current_method{nullptr};

struct MethodProfileGuard {
  VMethod* prev = nullptr;
  explicit MethodProfileGuard(VMethod* m) {
    if (g_method_profile_on.load(std::memory_order_relaxed)) {
      prev = g_current_method.exchange(m, std::memory_order_relaxed);
    }
  }
  ~MethodProfileGuard() {
    if (prev != nullptr) g_current_method.store(prev, std::memory_order_relaxed);
  }
};
}  // namespace

// ---- DoCall（docs 03 章 11.6 / 12 章 22.5）----

bool DoCall(VMState& state, Frame& frame, VMethod* resolved, InvokeKind kind,
            const CallSite& cs, Value* result) {
  // String.<init> -> StringFactory 重写（跳过真实构造）
  bool string_init = resolved->name == "<init>" && resolved->declaring != nullptr &&
                     resolved->declaring->descriptor == "Ljava/lang/String;";
  if (string_init) {
    if (!DoStringFactory(state, frame, resolved, cs)) return false;
    result->l = 0;
    return true;
  }

  VMethod* target = resolved;
  switch (kind) {
    case InvokeKind::kStatic:
    case InvokeKind::kDirect:
      target = resolved;
      break;
    case InvokeKind::kSuper:
      target = FindSuperMethod(frame.method->declaring, resolved);
      break;
    case InvokeKind::kVirtual:
    case InvokeKind::kInterface: {
      ObjHeader* receiver = GetRef(frame, WordReg(cs, 0));
      if (receiver == nullptr) {
        // 诊断面：带上被调方法与调用点（归因缺口必需）
        std::string msg = "receiver == null at " + MethodSigStr(resolved);
        if (frame.method != nullptr) {
          msg += " from ";
          if (frame.method->declaring != nullptr) msg += frame.method->declaring->descriptor;
          msg += "." + frame.method->name + "@" + std::to_string(frame.dex_pc);
        }
        ThrowNullPointerException(state, msg);
        return false;
      }
      if (!CheckRef(state, frame, receiver, WordReg(cs, 0), "invoke receiver")) return false;
      if (receiver->klass == nullptr) {
        // M4.3 防御：类镜像面——primitive/array 镜像（VClass 自身即对象）的
        // klass 槽未挂 class_class 时，虚调用回落到 resolved 声明类的实现
        //（Class.* intrinsic 的 self 即镜像；不改镜像布局语义）。
        target = resolved->declaring != nullptr ? resolved->declaring->FindInterfaceImpl(resolved)
                                                : nullptr;
        if (target == nullptr) {
          state.ThrowNew("Ljava/lang/NoSuchMethodError;", MethodSigStr(resolved));
          return false;
        }
        break;
      }
      // 派发缓存（ART vtable/IMT 等价最小面）：首次线性签名查找后按
      // (接收者类, 已解析方法) 命中，避免每次虚调用重复字符串签名比较。
      {
        VClass* rk = receiver->klass;
        target = nullptr;  // 缓存未命中必须走 FindInterfaceImpl（不能回落 resolved）
        for (const auto& kv : rk->dispatch_cache) {
          if (kv.first == resolved) {
            target = kv.second;
            break;
          }
        }
        if (target == nullptr) {
          target = rk->FindInterfaceImpl(resolved);
          if (target == nullptr) {
            state.ThrowNew("Ljava/lang/NoSuchMethodError;",
                           "No virtual method " + MethodSigStr(resolved) + " in " +
                               rk->descriptor);
            return false;
          }
          if (!target->HasImplementation()) {
            // 命中抽象无码声明：接口 default 深搜回落（Java 8 超接口 default 体，
            // 如 List <- Collection.isEmpty；见 FindImplDeep 注释）
            if (VMethod* impl = FindImplDeep(rk, *resolved)) target = impl;
          }
          if (rk->dispatch_cache.size() >= 64) rk->dispatch_cache.clear();
          rk->dispatch_cache.emplace_back(resolved, target);
        }
      }
      break;
    }
  }
  if (target == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", MethodSigStr(resolved));
    return false;
  }
#if defined(__ANDROID__)
  // 热点调用者归因（仅 profile 窗口，限量）：摘要校验与 LinkedList 遍历的
  // 直接调用链——定位启动期 onCreate 热点的来源（JarVerifier 等）。
  if (g_method_profile_on.load(std::memory_order_relaxed) && target->declaring != nullptr) {
    const std::string& td = target->declaring->descriptor;
    const bool probe =
        (td == "Lsun/security/provider/MD5;" &&
         (target->name == "processBlock" || target->name == "engineUpdate")) ||
        (td == "Ljava/util/LinkedList;" && target->name == "node");
    if (probe) {
      static std::atomic<int> caller_probe_logs{0};
      if (caller_probe_logs.fetch_add(1, std::memory_order_relaxed) < 12) {
        std::string chain;
        Frame* f = &frame;
        for (int i = 0; i < 10 && f != nullptr; i++) {
          if (f->method != nullptr) {
            if (!chain.empty()) chain += " <- ";
            if (f->method->declaring != nullptr) chain += f->method->declaring->descriptor;
            chain += "." + f->method->name;
          }
          f = f->caller;
        }
        __android_log_print(ANDROID_LOG_INFO, "DexVmCaller", "%s.%s <- %s", td.c_str(),
                            target->name.c_str(), chain.c_str());
      }
    }
  }
  // TEMP DIAG：setRecyclerView 调用是否到达解释器 + 目标实现核对。
  if (g_method_profile_on.load(std::memory_order_relaxed) && resolved->name == "setRecyclerView" &&
      resolved->declaring != nullptr &&
      resolved->declaring->descriptor.find("RecyclerView$LayoutManager") != std::string::npos) {
    static std::atomic<int> srv_logs{0};
    if (srv_logs.fetch_add(1, std::memory_order_relaxed) < 12) {
      ObjHeader* recv = GetRef(frame, WordReg(cs, 0));
      VClass* rk = recv != nullptr ? recv->klass : nullptr;
      __android_log_print(
          ANDROID_LOG_INFO, "DexVmRV",
          "call setRecyclerView recv=%p rk=%s target=%s.%s code=%d native=%d", recv,
          rk != nullptr ? rk->descriptor.c_str() : "?",
          target->declaring != nullptr ? target->declaring->descriptor.c_str() : "?",
          target->name.c_str(), target->HasCode() ? 1 : 0,
          target->native_fn != nullptr ? 1 : 0);
    }
  }
#endif
  MethodProfileGuard profile_guard(target);

  // 参数铺陈（对照 DoCallCommon：receiver 首参槽、J/D 占两连续 vreg）。
  // 8 槽内栈缓冲（调用面绝大多数 ≤8 参）——避免每次方法调用一次 std::vector
  // 堆分配（ART 用连续托管栈，这里做最小对齐）。
  const size_t arg_capacity = 1 + resolved->param_types.size();
  Value small_args[8];
  std::vector<Value> heap_args;
  Value* args = small_args;
  if (arg_capacity > 8) {
    heap_args.resize(arg_capacity);
    args = heap_args.data();
  }
  size_t argc = 0;
  uint32_t word = 0;
  if (!resolved->IsStatic()) {
    Value v{};
    v.ref = GetRef(frame, WordReg(cs, word));
    if (v.ref != nullptr && !CheckRef(state, frame, v.ref, WordReg(cs, word), "invoke arg0")) {
      return false;
    }
    args[argc++] = v;
    word += 1;
  }
  for (const auto& p : resolved->param_types) {
    if (word >= cs.count) break;
    uint32_t src = WordReg(cs, word);
    Value v{};
    if (p == "J" || p == "D") {
      v.l = GetL(frame, src);
      word += 2;
    } else if (p[0] == 'L' || p[0] == '[') {
      v.ref = GetRef(frame, src);
      if (v.ref != nullptr && !CheckRef(state, frame, v.ref, src, "invoke arg")) return false;
      word += 1;
    } else {
      v.i = GetI(frame, src);
      word += 1;
    }
    args[argc++] = v;
  }
  return InvokeMethod(state, target, args, argc, result);
}

// ---- invoke-polymorphic：java.lang.invoke marker/accessor 面（最小语义）----
// 背景：d8 min-api 26（dex 038）把 javac 的 signature-polymorphic 调用点
// （VarHandle accessor / MethodHandle.invokeBasic/linkTo*）保留为 invoke-polymorphic
// 真 opcode；min-api 24（dex 037）下 d8 整体替换为 "Instruction is unrepresentable"
// 抛出桩（juc VarHandle 面全灭）——故打包侧 min-api 已升 26。
// 语义分层（docs 14 章 §31.7：JLI 引擎是 Java，C++ 只做 opcode 面派发）：
//   1. VarHandle accessor 形 → d8 脱糖形状 VarHandleGuards.guard_*（按字段读写
//      语义转发到 VarHandle.access——语义核心全在 Java）；
//   2. MethodHandle.invoke/invokeExact/invokeBasic/linkToStatic/Virtual/Special/
//      Interface marker 形 → MemberName 目标基础派发（LambdaForm 引擎后置）。

// 客体字段按名读取（MemberName/MethodHandle 布局契约 = 字段名级；8 字节槽）
ObjHeader* GuestFieldRef(VObject* obj, const char* name) {
  if (obj == nullptr || obj->klass == nullptr) return nullptr;
  for (VClass* k = obj->klass; k != nullptr; k = k->super_class) {
    for (const auto& f : k->instance_fields) {
      if (f.name == name) {
        uint64_t slot = LoadSlot(obj->field_data() + f.offset);
        return reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(slot));
      }
    }
  }
  return nullptr;
}

int32_t GuestFieldInt(VObject* obj, const char* name) {
  if (obj == nullptr || obj->klass == nullptr) return 0;
  for (VClass* k = obj->klass; k != nullptr; k = k->super_class) {
    for (const auto& f : k->instance_fields) {
      if (f.name == name) {
        uint64_t slot = LoadSlot(obj->field_data() + f.offset);
        return static_cast<int32_t>(slot);
      }
    }
  }
  return 0;
}

std::string GuestStringUtf8(ObjHeader* o) {
  if (o == nullptr) return "";
  return static_cast<VString*>(o)->ToModifiedUtf8();
}

// 描述符 "(IJ)Ljava/lang/String;" → params + return
bool SplitMethodDescriptor(const std::string& desc, std::vector<std::string>* params,
                           std::string* ret) {
  if (desc.empty() || desc[0] != '(') return false;
  size_t i = 1;
  while (i < desc.size() && desc[i] != ')') {
    size_t start = i;
    while (i < desc.size() && desc[i] == '[') i++;
    if (i >= desc.size()) return false;
    if (desc[i] == 'L') {
      size_t semi = desc.find(';', i);
      if (semi == std::string::npos) return false;
      i = semi + 1;
    } else {
      i++;
    }
    params->push_back(desc.substr(start, i - start));
  }
  if (i >= desc.size() || desc[i] != ')') return false;
  *ret = desc.substr(i + 1);
  return true;
}

// 按 name+descriptor 找实现（类链优先，接口闭包回落——marker 目标可达 default 体）
VMethod* ResolveBySig(VClass* k, const std::string& name, const std::string& desc) {
  if (k == nullptr) return nullptr;
  std::vector<std::string> params;
  std::string ret;
  if (!SplitMethodDescriptor(desc, &params, &ret)) return nullptr;
  auto match = [&](VMethod& m) {
    return m.name == name && m.param_types == params && m.return_type == ret;
  };
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& m : c->direct_methods) if (match(m)) return &m;
    for (auto& m : c->virtual_methods) if (match(m)) return &m;
  }
  std::vector<VClass*> stack{k};
  while (!stack.empty()) {
    VClass* c = stack.back();
    stack.pop_back();
    for (VClass* iface : c->interfaces) {
      for (auto& m : iface->direct_methods) if (match(m)) return &m;
      for (auto& m : iface->virtual_methods) if (match(m)) return &m;
      stack.push_back(iface);
    }
    if (c != k) stack.push_back(c->super_class);
  }
  return nullptr;
}

// 静态方法按 name+参数形找（guard_*/AccessDescriptor.of 派发面）
VMethod* FindStaticBySig(VClass* k, const std::string& name, const std::vector<std::string>& params,
                         const std::string& ret) {
  if (k == nullptr) return nullptr;
  for (auto& m : k->direct_methods) {
    if (m.IsStatic() && m.name == name && m.param_types == params && m.return_type == ret) return &m;
  }
  for (auto& m : k->virtual_methods) {
    if (m.IsStatic() && m.name == name && m.param_types == params && m.return_type == ret) return &m;
  }
  return nullptr;
}

// basic type 擦除（LambdaForm 形状约定：Z/B/C/S → I；L/[ → L）
char EraseShapeChar(const std::string& desc) {
  if (desc.empty()) return 'V';
  switch (desc[0]) {
    case 'L':
    case '[': return 'L';
    case 'J': return 'J';
    case 'F': return 'F';
    case 'D': return 'D';
    case 'V': return 'V';
    default: return 'I';
  }
}

// 从 word 基址按 target 签名铺 args（对照 DoCall 参数铺陈；receiver 首参槽）
bool MarshalFromWords(VMState& state, Frame& frame, const CallSite& cs, uint32_t word0,
                      VMethod* target, std::vector<Value>* args) {
  uint32_t word = word0;
  if (!target->IsStatic()) {
    if (word >= cs.count) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "polymorphic: no receiver");
      return false;
    }
    Value v{};
    v.ref = GetRef(frame, WordReg(cs, word));
    if (v.ref != nullptr && !CheckRef(state, frame, v.ref, WordReg(cs, word), "poly receiver")) {
      return false;
    }
    args->push_back(v);
    word += 1;
  }
  for (const auto& p : target->param_types) {
    if (word >= cs.count) break;
    uint32_t src = WordReg(cs, word);
    Value v{};
    if (p == "J" || p == "D") {
      v.l = GetL(frame, src);
      word += 2;
    } else if (p[0] == 'L' || p[0] == '[') {
      v.ref = GetRef(frame, src);
      if (v.ref != nullptr && !CheckRef(state, frame, v.ref, src, "poly arg")) return false;
      word += 1;
    } else {
      v.i = GetI(frame, src);
      word += 1;
    }
    args->push_back(v);
  }
  return true;
}

bool DoPolymorphicCall(VMState& state, Frame& frame, const CallSite& cs, uint16_t proto_idx,
                       Value* result) {
  const dex::DexFile* dex = frame.method->dex_file;
  const dex::MethodId& mid = dex->GetMethodId(cs.idx);
  std::string name = dex->GetString(mid.name_idx);
  std::string decl(dex->GetTypeDescriptor(mid.class_idx));

  // 调用点 proto：坐标形 + 返回（不含 handle/MemberName 所在的 receiver 槽）
  const uint32_t np = dex->GetProtoParamCount(proto_idx);
  std::vector<std::string> ptypes;
  ptypes.reserve(np);
  for (uint32_t i = 0; i < np; i++) {
    ptypes.emplace_back(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(proto_idx, i)));
  }
  std::string ret(dex->GetTypeDescriptor(dex->GetProto(proto_idx).return_type_idx));

  // ---- 形状 1：VarHandle accessor → VarHandleGuards.guard_* ----
  if (decl == "Ljava/lang/invoke/VarHandle;") {
    ObjHeader* handle = GetRef(frame, WordReg(cs, 0));
    if (handle == nullptr) {
      state.ThrowNew("Ljava/lang/NullPointerException;", "VarHandle accessor receiver");
      return false;
    }
    if (!CheckRef(state, frame, handle, WordReg(cs, 0), "VarHandle")) return false;
    std::string shape;
    for (const auto& p : ptypes) shape.push_back(EraseShapeChar(p));
    std::string guard_name = "guard_" + shape + "_" + std::string(1, EraseShapeChar(ret));
    if (ret == "Z") guard_name = "guard_" + shape + "_Z";  // Z 返回保留 Z 形
    if (ret == "V") guard_name = "guard_" + shape + "_V";

    VClass* guard_cls = state.linker->FindClass("Ljava/lang/invoke/VarHandleGuards;");
    if (!EnsureClassInitialized(state, guard_cls)) return false;
    // guard 签名：(VarHandle, <coords…>, AccessDescriptor)<ret>
    std::vector<std::string> gparams;
    gparams.emplace_back("Ljava/lang/invoke/VarHandle;");
    for (const auto& p : ptypes) {
      char c = EraseShapeChar(p);
      switch (c) {
        case 'J': gparams.emplace_back("J"); break;
        case 'F': gparams.emplace_back("F"); break;
        case 'D': gparams.emplace_back("D"); break;
        case 'L': gparams.emplace_back("Ljava/lang/Object;"); break;
        default: gparams.emplace_back("I"); break;
      }
    }
    gparams.emplace_back("Ljava/lang/invoke/VarHandle$AccessDescriptor;");
    std::string gret;
    {
      char c = EraseShapeChar(ret);
      switch (c) {
        case 'V': gret = "V"; break;
        case 'J': gret = "J"; break;
        case 'F': gret = "F"; break;
        case 'D': gret = "D"; break;
        case 'L': gret = "Ljava/lang/Object;"; break;
        case 'Z': gret = "Z"; break;
        default: gret = "I"; break;
      }
      if (ret == "Z") gret = "Z";
    }
    VMethod* guard = FindStaticBySig(guard_cls, guard_name, gparams, gret);
    if (guard == nullptr) {
      state.ThrowNew("Ljava/lang/NoSuchMethodError;",
                     "Ljava/lang/invoke/VarHandleGuards;." + guard_name);
      return false;
    }

    // AccessDescriptor.of(opName, returnType)（语义核心在 Java：name→mode 映射）
    VClass* ad_cls = state.linker->FindClass("Ljava/lang/invoke/VarHandle$AccessDescriptor;");
    if (!EnsureClassInitialized(state, ad_cls)) return false;
    VMethod* ad_of = FindStaticBySig(ad_cls, "of",
                                     {"Ljava/lang/String;", "Ljava/lang/Class;"},
                                     "Ljava/lang/invoke/VarHandle$AccessDescriptor;");
    if (ad_of == nullptr) {
      state.ThrowNew("Ljava/lang/NoSuchMethodError;", "VarHandle$AccessDescriptor.of");
      return false;
    }
    Value of_args[2] = {};
    {
      std::u16string chars(name.begin(), name.end());
      VString* s = state.linker != nullptr ? state.linker->AllocString(chars) : nullptr;
      if (s == nullptr) {
        ThrowOOM(state, "AccessDescriptor.of name");
        return false;
      }
      of_args[0].ref = reinterpret_cast<ObjHeader*>(s);
      if (ret != "V") of_args[1].ref = reinterpret_cast<ObjHeader*>(state.linker->FindClass(ret));
    }
    Value ad{};
    if (!InvokeMethod(state, ad_of, of_args, 2, &ad)) return false;

    // 铺陈：handle + coords（按 proto 宽型走 word）+ ad
    std::vector<Value> args;
    args.reserve(gparams.size());
    Value hv{};
    hv.ref = handle;
    args.push_back(hv);
    uint32_t word = 1;
    for (const auto& p : ptypes) {
      if (word >= cs.count) break;
      uint32_t src = WordReg(cs, word);
      Value v{};
      if (p == "J" || p == "D") {
        v.l = GetL(frame, src);
        word += 2;
      } else if (p[0] == 'L' || p[0] == '[') {
        v.ref = GetRef(frame, src);
        if (v.ref != nullptr && !CheckRef(state, frame, v.ref, src, "poly coord")) return false;
        word += 1;
      } else {
        v.i = GetI(frame, src);
        word += 1;
      }
      args.push_back(v);
    }
    args.push_back(ad);
    return InvokeMethod(state, guard, args.data(), args.size(), result);
  }

  // ---- 形状 2：MethodHandle marker → MemberName 目标基础派发 ----
  if (decl == "Ljava/lang/invoke/MethodHandle;" ||
      decl == "Ljava/lang/invoke/MethodHandleNatives;") {
    const bool is_link =
        name == "linkToStatic" || name == "linkToVirtual" || name == "linkToSpecial" ||
        name == "linkToInterface";
    ObjHeader* mn_obj = nullptr;
    uint32_t arg0_word = 0;
    if (is_link) {
      // linkTo*：末参为 MemberName（proto 参数尾）
      uint32_t word = 0;
      uint32_t last_word = 0;
      for (size_t i = 0; i < ptypes.size(); i++) {
        last_word = word;
        word += (ptypes[i] == "J" || ptypes[i] == "D") ? 2 : 1;
      }
      mn_obj = GetRef(frame, WordReg(cs, last_word));
      arg0_word = 0;
    } else {
      // invoke/invokeExact/invokeBasic：word 0 = MethodHandle，其 member 为目标
      ObjHeader* mh = GetRef(frame, WordReg(cs, 0));
      if (mh == nullptr) {
        state.ThrowNew("Ljava/lang/NullPointerException;", "MethodHandle receiver");
        return false;
      }
      if (!CheckRef(state, frame, mh, WordReg(cs, 0), "MethodHandle")) return false;
      mn_obj = GuestFieldRef(reinterpret_cast<VObject*>(mh), "member");
      arg0_word = 1;
    }
    if (mn_obj == nullptr) {
      state.ThrowNew("Ljava/lang/IllegalStateException;",
                     "MethodHandle marker without MemberName: " + name);
      return false;
    }
    VObject* mno = reinterpret_cast<VObject*>(mn_obj);
    VClass* decl_cls = reinterpret_cast<VClass*>(GuestFieldRef(mno, "declaringClass"));
    std::string mname = GuestStringUtf8(GuestFieldRef(mno, "name"));
    std::string mdesc = GuestStringUtf8(GuestFieldRef(mno, "descriptor"));
    int ref_kind = GuestFieldInt(mno, "refKind");
    VMethod* resolved = ResolveBySig(decl_cls, mname, mdesc);
    if (resolved == nullptr) {
      state.ThrowNew("Ljava/lang/NoSuchMethodError;",
                     (decl_cls != nullptr ? decl_cls->descriptor : "?") + "." + mname + mdesc);
      return false;
    }
    VMethod* target = resolved;
    constexpr int kRefInvokeStatic = 6;
    constexpr int kRefInvokeSpecial = 7;
    if (ref_kind != kRefInvokeStatic && ref_kind != kRefInvokeSpecial && !resolved->IsStatic()) {
      // 虚派发（linkToVirtual/Interface、invokeBasic 对虚目标）
      ObjHeader* recv = GetRef(frame, WordReg(cs, arg0_word));
      if (recv == nullptr) {
        state.ThrowNew("Ljava/lang/NullPointerException;", "marker target receiver");
        return false;
      }
      if (!CheckRef(state, frame, recv, WordReg(cs, arg0_word), "marker receiver")) return false;
      if (recv->klass != nullptr) {
        target = recv->klass->FindInterfaceImpl(resolved);
        if (target == nullptr || !target->HasImplementation()) {
          if (VMethod* impl = FindImplDeep(recv->klass, *resolved)) target = impl;
        }
      }
    }
    if (target == nullptr) {
      state.ThrowNew("Ljava/lang/NoSuchMethodError;", MethodSigStr(resolved));
      return false;
    }
    std::vector<Value> args;
    args.reserve(1 + target->param_types.size());
    if (!MarshalFromWords(state, frame, cs, arg0_word, target, &args)) return false;
    return InvokeMethod(state, target, args.data(), args.size(), result);
  }

  ThrowUnsupportedOperationException(state, "invoke-polymorphic on " + decl + "." + name);
  return false;
}

}  // namespace

// 采样控制面定义放在外层匿名命名空间之外，保证外部链接（jni 可引用）。
void SetGuestMethodProfile(bool on) {
  g_method_profile_on.store(on, std::memory_order_relaxed);
  if (!on) g_current_method.store(nullptr, std::memory_order_relaxed);
}

bool GuestMethodProfileEnabled() {
  return g_method_profile_on.load(std::memory_order_relaxed);
}

VMethod* CurrentGuestMethod() {
  return g_current_method.load(std::memory_order_relaxed);
}

// 类定义完整性防御（interpreter.h；M4.3 SIGSEGV 根因防御，见声明注释）
bool CheckClassDefinition(VMState& state, VClass* klass) {
  if (klass == nullptr) return true;
  if (klass->status == ClassStatus::kLoaded ||
      klass->static_storage.size() < klass->static_fields.size()) {
    // 诊断升级：并入 linker.last_error()（super/interface 解析失败的具体
    // 缺失面——残骸类根因坐标，归因不再悬空）。
    std::string why = state.linker != nullptr ? state.linker->last_error() : "";
    state.ThrowNew("Ljava/lang/NoClassDefFoundError;",
                   klass->descriptor + " (definition incomplete: link failed" +
                       (why.empty() ? "" : ": " + why) + ")");
    return false;
  }
  return true;
}

// ---- InvokeMethod / ExecuteFrame ----

// JNI NewObject(String, ...) 承接面（interpreter.h 声明）。
// ART 中 String.<init> 是真实构造器，JNI NewObject 与解释器 invoke 同语义；
// 本 VM 的 String 是 VString 布局、<init> 为无 code 骨架，JNI 直调
// InvokeMethod 不会填充内容（加固 native 以 new String(byte[]) 重建字符串
// 常量时得到空串 → H.d("") 解密失败）。此处按 DoStringFactory 同一套规则
// 从构造器实参（不含 this）生成内容并分配 VString。
bool BuildJniStringCtor(VMState& state, VMethod* ctor, const Value* args, size_t argc,
                        VString** out) {
  if (out == nullptr) return false;
  *out = nullptr;
  if (ctor == nullptr) {
    ThrowNullPointerException(state, "constructor == null");
    return false;
  }
  const std::vector<std::string>& pt = ctor->param_types;
  if (argc < pt.size()) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "String.<init> arity mismatch");
    return false;
  }
  auto ref_at = [&](size_t i) -> ObjHeader* { return args[i].ref; };
  auto read_bytes = [&](ObjHeader* src, int32_t offset, int32_t count, bool whole,
                        std::vector<uint8_t>* bytes) -> bool {
    if (src == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    VArray* arr = static_cast<VArray*>(src);
    if (whole) {
      offset = 0;
      count = arr->length;
    }
    if (offset < 0 || count < 0 || offset > arr->length ||
        count > arr->length - offset) {
      state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                     "offset=" + std::to_string(offset) +
                         ",count=" + std::to_string(count) +
                         ",length=" + std::to_string(arr->length));
      return false;
    }
    bytes->reserve(static_cast<size_t>(count));
    for (int32_t i = 0; i < count; i++) {
      bytes->push_back(static_cast<uint8_t>(LoadSlot(arr->data() + (offset + i) * 8) & 0xffu));
    }
    return true;
  };
  std::u16string chars;
  if (pt.empty()) {
    // <init>() -> 空串
  } else if (pt.size() == 1 && pt[0] == "Ljava/lang/String;") {
    ObjHeader* src = ref_at(0);
    if (src == nullptr) {
      ThrowNullPointerException(state, "toCopy == null");
      return false;
    }
    VString* s = static_cast<VString*>(src);
    chars.assign(s->chars(), s->chars() + s->length);
  } else if (pt.size() == 1 && pt[0] == "[C") {
    VArray* arr = static_cast<VArray*>(ref_at(0));
    if (arr == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    chars.reserve(static_cast<size_t>(arr->length));
    for (int32_t i = 0; i < arr->length; i++) {
      chars.push_back(static_cast<char16_t>(LoadSlot(arr->data() + i * 8) & 0xffffu));
    }
  } else if (pt.size() == 3 && pt[0] == "[C" && pt[1] == "I" && pt[2] == "I") {
    VArray* arr = static_cast<VArray*>(ref_at(0));
    if (arr == nullptr) {
      ThrowNullPointerException(state, "data == null");
      return false;
    }
    int32_t offset = args[1].i;
    int32_t count = args[2].i;
    if (offset < 0 || count < 0 || offset > arr->length ||
        count > arr->length - offset) {
      state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                     "length=" + std::to_string(arr->length) +
                         "; regionStart=" + std::to_string(offset) +
                         "; regionLength=" + std::to_string(count));
      return false;
    }
    for (int32_t i = 0; i < count; i++) {
      chars.push_back(
          static_cast<char16_t>(LoadSlot(arr->data() + (offset + i) * 8) & 0xffffu));
    }
  } else if (pt.size() == 1 && pt[0] == "[B") {
    std::vector<uint8_t> bytes;
    if (!read_bytes(ref_at(0), 0, 0, /*whole=*/true, &bytes)) return false;
    Utf8ToChars(bytes, &chars);
  } else if (pt.size() == 3 && pt[0] == "[B" && pt[1] == "I" && pt[2] == "I") {
    std::vector<uint8_t> bytes;
    if (!read_bytes(ref_at(0), args[1].i, args[2].i, /*whole=*/false, &bytes)) return false;
    Utf8ToChars(bytes, &chars);
  } else if ((pt.size() == 2 || pt.size() == 4) && pt[0] == "[B" &&
             pt[pt.size() - 1] == "Ljava/nio/charset/Charset;") {
    std::vector<uint8_t> bytes;
    const bool whole = pt.size() == 2;
    if (!read_bytes(ref_at(0), whole ? 0 : args[1].i, whole ? 0 : args[2].i, whole,
                    &bytes)) {
      return false;
    }
    ObjHeader* charset = ref_at(pt.size() - 1);
    if (charset == nullptr) {
      ThrowNullPointerException(state, "charset == null");
      return false;
    }
    std::string name;
    if (!CharsetObjectName(state, reinterpret_cast<VObject*>(charset), &name) ||
        !DecodeByCharsetName(bytes, name, &chars)) {
      if (state.HasPendingException()) state.ClearException();
      chars.clear();
      Utf8ToChars(bytes, &chars);
    }
  } else if ((pt.size() == 2 || pt.size() == 4) && pt[0] == "[B" &&
             pt[pt.size() - 1] == "Ljava/lang/String;") {
    std::vector<uint8_t> bytes;
    const bool whole = pt.size() == 2;
    if (!read_bytes(ref_at(0), whole ? 0 : args[1].i, whole ? 0 : args[2].i, whole,
                    &bytes)) {
      return false;
    }
    ObjHeader* name_obj = ref_at(pt.size() - 1);
    if (name_obj == nullptr) {
      ThrowNullPointerException(state, "charsetName == null");
      return false;
    }
    VString* name_str = static_cast<VString*>(name_obj);
    std::string name = NormalizeCharsetForName(name_str->chars(), name_str->length);
    if (!DecodeByCharsetName(bytes, name, &chars)) {
      std::string raw;
      for (int32_t i = 0; i < name_str->length; i++) {
        char16_t c = name_str->chars()[i];
        raw.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
      }
      state.ThrowNew("Ljava/io/UnsupportedEncodingException;", raw);
      return false;
    }
  } else if (pt.size() == 1 && (pt[0] == "Ljava/lang/StringBuilder;" ||
                                pt[0] == "Ljava/lang/StringBuffer;" ||
                                pt[0] == "Ljava/lang/CharSequence;")) {
    ObjHeader* src = ref_at(0);
    if (src == nullptr) {
      ThrowNullPointerException(state, "toCopy == null");
      return false;
    }
    if (src->klass != nullptr && src->klass->descriptor == "Ljava/lang/StringBuilder;") {
      const uint8_t* base = reinterpret_cast<VObject*>(src)->field_data();
      uint64_t slot;
      std::memcpy(&slot, base + 0, sizeof(slot));
      VArray* buf = reinterpret_cast<VArray*>(static_cast<uintptr_t>(slot));
      std::memcpy(&slot, base + 8, sizeof(slot));
      int32_t count = static_cast<int32_t>(slot);
      if (buf != nullptr) {
        for (int32_t i = 0; i < count && i < buf->length; i++) {
          chars.push_back(static_cast<char16_t>(LoadSlot(buf->data() + i * 8) & 0xffffu));
        }
      }
    } else {
      VMethod probe;
      probe.name = "toString";
      probe.param_types = {};
      probe.return_type = "Ljava/lang/String;";
      VMethod* m = src->klass != nullptr ? src->klass->FindInterfaceImpl(&probe) : nullptr;
      if (m == nullptr) {
        ThrowUnsupportedOperationException(state, "String.<init>(CharSequence): no toString");
        return false;
      }
      Value call_args[1] = {};
      call_args[0].ref = src;
      Value r{};
      if (!InvokeMethod(state, m, call_args, 1, &r)) return false;
      if (r.ref != nullptr) {
        VString* s = static_cast<VString*>(r.ref);
        chars.assign(s->chars(), s->chars() + s->length);
      }
    }
  } else {
    ThrowUnsupportedOperationException(state, "String.<init> signature not supported");
    return false;
  }
  VString* created = state.linker != nullptr ? state.linker->AllocString(chars) : nullptr;
  if (created == nullptr) {
    ThrowOOM(state, "String.<init> allocation failed");
    return false;
  }
  *out = created;
  return true;
}

// 解释器 C++ 栈护栏（StackOverflowError 预检）：JNI->guest->JNI 递归解释帧
// 深撞 fiber guard page = SIGSEGV/SIGBUS（vm-crash 位型；2MB/4MB fiber 都有
// 深链打穿面）。按当前 guest task 的 fiber 栈边界动态判定
// （GuestSchedOps::current_stack_bounds）：剩余栈 < kStackGuardSlack 时抛
// java.lang.StackOverflowError（可 catch 的 Java 语义，不崩）。未装协程接线 /
// 非 guest task 上下文 = 护栏停用（既有宿主单流测试行为不变）。
constexpr uintptr_t kStackGuardSlack = 512u * 1024u;

bool StackGuardTripped(VMState& state) {
  const GuestSchedOps* ops = GuestSched();
  if (ops == nullptr || ops->current_stack_bounds == nullptr) return false;
  uintptr_t lo = 0;
  if (!ops->current_stack_bounds(&lo, nullptr) || lo == 0) return false;
  uintptr_t sp = reinterpret_cast<uintptr_t>(__builtin_frame_address(0));
  if (sp <= lo || sp - lo < kStackGuardSlack) {
    state.ThrowNew("Ljava/lang/StackOverflowError;",
                   "deep recursion (interpreter C++ stack guard)");
    return true;
  }
  return false;
}

namespace {
std::vector<std::string> g_guest_trace_patterns;
std::atomic<bool> g_guest_trace_on{false};

bool GuestTraceMatches(const std::string& descriptor) {
  for (const auto& p : g_guest_trace_patterns) {
    if (descriptor.find(p) != std::string::npos) return true;
  }
  return false;
}

#if defined(__ANDROID__)
// TEMP DIAG（Jackson creator 属性冲突归因；事后移除）。
// AnnotatedParameter(d0/l)：field l=owner(d0/m)、n=index(I)；
// owner(d0/d ctor / d0/i method)：field m=Constructor/Method 镜像；
// 镜像首 8 字节 = art 指针（builtins_reflect.cc kMirrorArt）。
std::string DiagReadString(ObjHeader* o, const char* field) {
  if (o == nullptr || o->klass == nullptr) return "?";
  uint8_t* data = reinterpret_cast<VObject*>(o)->field_data();
  for (auto& f : o->klass->instance_fields) {
    if (f.name != field) continue;
    ObjHeader* s = nullptr;
    std::memcpy(&s, data + f.offset, sizeof(s));
    if (s == nullptr || s->klass == nullptr ||
        s->klass->descriptor != "Ljava/lang/String;") {
      return "?";
    }
    return reinterpret_cast<VString*>(s)->ToModifiedUtf8();
  }
  return "?";
}

// String 对象 → ModifiedUTF8（诊断面；参数直接取值，非字段）。
std::string DiagStringOf(ObjHeader* s) {
  if (s == nullptr || s->klass == nullptr ||
      s->klass->descriptor != "Ljava/lang/String;") {
    return "";
  }
  return reinterpret_cast<VString*>(s)->ToModifiedUtf8();
}

// PropertyName(d0/u)：field l=simpleName(String)、m=namespace。
std::string DiagPropName(ObjHeader* o, const char* field) {
  if (o == nullptr || o->klass == nullptr) return "?";
  uint8_t* data = reinterpret_cast<VObject*>(o)->field_data();
  for (auto& f : o->klass->instance_fields) {
    if (f.name != field) continue;
    ObjHeader* pn = nullptr;
    std::memcpy(&pn, data + f.offset, sizeof(pn));
    return DiagReadString(pn, "l");
  }
  return "?";
}

bool DiagAnnotatedParam(ObjHeader* ph, ObjHeader** owner, int32_t* idx) {
  *owner = nullptr;
  *idx = -1;
  if (ph == nullptr || ph->klass == nullptr) return false;
  uint8_t* data = reinterpret_cast<VObject*>(ph)->field_data();
  for (auto& f : ph->klass->instance_fields) {
    if (f.name == "l") {
      std::memcpy(owner, data + f.offset, sizeof(*owner));
    } else if (f.name == "n") {
      std::memcpy(idx, data + f.offset, sizeof(*idx));
    }
  }
  return *owner != nullptr;
}

VMethod* DiagOwnerMember(ObjHeader* owner) {
  if (owner == nullptr || owner->klass == nullptr) return nullptr;
  uint8_t* odata = reinterpret_cast<VObject*>(owner)->field_data();
  for (auto& f : owner->klass->instance_fields) {
    if (f.name != "m") continue;
    uint64_t mirror = 0;
    std::memcpy(&mirror, odata + f.offset, sizeof(mirror));
    if (mirror == 0) return nullptr;
    uint64_t art = 0;
    std::memcpy(&art,
                reinterpret_cast<VObject*>(static_cast<uintptr_t>(mirror))->field_data(),
                sizeof(art));
    return reinterpret_cast<VMethod*>(static_cast<uintptr_t>(art));
  }
  return nullptr;
}

std::string DiagMemberSig(ObjHeader* owner) {
  VMethod* vm = DiagOwnerMember(owner);
  if (vm == nullptr || vm->declaring == nullptr) return "?";
  return vm->declaring->descriptor + "." + vm->name;
}
#endif
}  // namespace

void SetGuestMethodTrace(std::string patterns) {
  g_guest_trace_patterns.clear();
  size_t start = 0;
  while (start <= patterns.size()) {
    size_t comma = patterns.find(',', start);
    std::string part = patterns.substr(
        start, comma == std::string::npos ? std::string::npos : comma - start);
    if (!part.empty()) g_guest_trace_patterns.push_back(std::move(part));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  g_guest_trace_on.store(!g_guest_trace_patterns.empty(), std::memory_order_relaxed);
}

bool InvokeMethod(VMState& state, VMethod* method, const Value* args, size_t arg_count,
                  Value* result) {
  Value local_result{};
  if (result == nullptr) result = &local_result;
  result->l = 0;
  if (method == nullptr) {
    ThrowUnsupportedOperationException(state, "invoke null method");
    return false;
  }
  if (StackGuardTripped(state)) return false;
  // 深递归硬护栏：fiber 边界判定在 JNI 调用密集路径上可能晚于宿主 Java
  // 线程栈上限（ART 直接 abort）。frame_stack 为多 GuestTask 共享，必须按
  // owner 统计"本任务"深度（全局深度会把并发任务的活跃帧叠加，误报 SOE——
  // 实测 re2j 正则编译链被误杀）。仅在高水位时做 O(n) 统计，常态零开销。
  if (state.frame_stack.size() > 400) {
    void* owner = state.frame_stack.back() != nullptr ? state.frame_stack.back()->owner
                                                      : nullptr;
    size_t task_depth = 0;
    for (Frame* fr : state.frame_stack) {
      if (fr != nullptr && fr->owner == owner) task_depth++;
    }
    if (task_depth > 2500) {
#if defined(__ANDROID__)
      static std::atomic<int> soe_logs{0};
      if (soe_logs.fetch_add(1, std::memory_order_relaxed) < 4) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmStack",
                            "SOE owner=%p task_depth=%zu total=%zu", owner, task_depth,
                            state.frame_stack.size());
        int printed = 0;
        for (size_t i = state.frame_stack.size(); i > 0 && printed < 24; i--) {
          Frame* fr = state.frame_stack[i - 1];
          if (fr == nullptr || fr->owner != owner || fr->method == nullptr) continue;
          __android_log_print(
              ANDROID_LOG_INFO, "DexVmStack", "  soe#%d %s.%s@%u", printed,
              fr->method->declaring != nullptr ? fr->method->declaring->descriptor.c_str()
                                               : "?",
              fr->method->name.c_str(), fr->dex_pc);
          printed++;
        }
      }
#endif
      state.ThrowNew("Ljava/lang/StackOverflowError;", "guest task frame depth > 2500");
      return false;
    }
  }
#if defined(__ANDROID__)
  // 深递归归因：栈深每 +100 打一次最近 12 帧（重复方法链 = 自递归根因）。
  if (state.frame_stack.size() >= 200 && state.frame_stack.size() % 100 == 0) {
    static std::atomic<int> deep_logs{0};
    if (deep_logs.fetch_add(1, std::memory_order_relaxed) < 8) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmStack", "depth=%zu",
                          state.frame_stack.size());
      const size_t total = state.frame_stack.size();
      for (size_t i = 0; i < 12 && i < total; i++) {
        Frame* f = state.frame_stack[total - 1 - i];
        if (f == nullptr || f->method == nullptr) continue;
        __android_log_print(
            ANDROID_LOG_INFO, "DexVmStack", "  #%zu %s.%s@%u", i,
            f->method->declaring != nullptr ? f->method->declaring->descriptor.c_str() : "?",
            f->method->name.c_str(), f->dex_pc);
      }
    }
  }
  // TEMP DIAG（RecyclerView measure NPE）：绑定/测量两侧实例核对
  // （InvokeMethod 是 CallVirtual 顶层入口，DoCall 只覆盖 guest→guest）。
  if (g_method_profile_on.load(std::memory_order_relaxed) && method->declaring != nullptr &&
      arg_count >= 1) {
    const std::string& td = method->declaring->descriptor;
    auto ref_field = [](VObject* o, const char* name) -> ObjHeader* {
      if (o == nullptr || o->klass == nullptr) return nullptr;
      for (VClass* k = o->klass; k != nullptr; k = k->super_class) {
        for (auto& f : k->instance_fields) {
          if (f.name == name) {
            ObjHeader* r = nullptr;
            std::memcpy(&r, o->field_data() + f.offset, sizeof(r));
            return r;
          }
        }
      }
      return nullptr;
    };
    if (method->name == "setLayoutManager" &&
        td == "Landroidx/recyclerview/widget/RecyclerView;") {
      static std::atomic<int> slm_logs{0};
      if (slm_logs.fetch_add(1, std::memory_order_relaxed) < 20) {
        VObject* lm =
            arg_count >= 2 ? reinterpret_cast<VObject*>(args[1].ref) : nullptr;
        int32_t pre_off = -1;
        ObjHeader* pre = nullptr;
        if (lm != nullptr && lm->klass != nullptr) {
          for (VClass* k = lm->klass; k != nullptr && pre_off < 0; k = k->super_class) {
            for (auto& f : k->instance_fields) {
              if (f.name == "mRecyclerView") {
                pre_off = static_cast<int32_t>(f.offset);
                std::memcpy(&pre, lm->field_data() + f.offset, sizeof(pre));
                break;
              }
            }
          }
        }
        __android_log_print(ANDROID_LOG_INFO, "DexVmRV",
                            "invoke setLayoutManager rv=%p lm=%p pre.mRecyclerView=%p off=%d",
                            static_cast<void*>(reinterpret_cast<VObject*>(args[0].ref)),
                            static_cast<void*>(lm), static_cast<void*>(pre), pre_off);
      }
    }
    if (method->name == "setRecyclerView" &&
        td == "Landroidx/recyclerview/widget/RecyclerView$LayoutManager;") {
      static std::atomic<int> bind_logs{0};
      if (bind_logs.fetch_add(1, std::memory_order_relaxed) < 10) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmRV",
                            "invoke setRecyclerView self=%p arg=%p",
                            static_cast<void*>(reinterpret_cast<VObject*>(args[0].ref)),
                            arg_count >= 2 ? static_cast<void*>(reinterpret_cast<VObject*>(
                                                 args[1].ref))
                                           : nullptr);
      }
    }
    if (method->name == "onMeasure" &&
        td == "Landroidx/recyclerview/widget/RecyclerView;") {
      static std::atomic<int> measure_logs{0};
      if (measure_logs.fetch_add(1, std::memory_order_relaxed) < 50) {
        VObject* self = reinterpret_cast<VObject*>(args[0].ref);
        ObjHeader* layout = ref_field(self, "mLayout");
        ObjHeader* back = layout != nullptr
                              ? ref_field(reinterpret_cast<VObject*>(layout), "mRecyclerView")
                              : nullptr;
        int32_t mrv_off = -1;
        if (layout != nullptr && layout->klass != nullptr) {
          for (VClass* k = layout->klass; k != nullptr && mrv_off < 0; k = k->super_class) {
            for (auto& f : k->instance_fields) {
              if (f.name == "mRecyclerView") {
                mrv_off = static_cast<int32_t>(f.offset);
                break;
              }
            }
          }
        }
        __android_log_print(ANDROID_LOG_INFO, "DexVmRV",
                            "invoke onMeasure rv=%p mLayout=%p layout.mRecyclerView=%p off=%d",
                            static_cast<void*>(self), static_cast<void*>(layout),
                            static_cast<void*>(back), mrv_off);
      }
    }
  }
#endif
#if defined(__ANDROID__) && DEXVM_CALL_DIAGNOSTICS
  // TEMP DIAG（Jackson creator 参数归因；事后移除）：POJOPropertiesCollector.
  // _addCreatorParam(Map, AnnotatedParameter) 每次调用打印参数所属成员。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/fasterxml/jackson/databind/d0/z;" &&
      method->name == "a" && arg_count >= 3 && args[2].ref != nullptr) {
    ObjHeader* ph = args[2].ref;
    ObjHeader* owner = nullptr;
    int32_t idx = -1;
    DiagAnnotatedParam(ph, &owner, &idx);
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "addCreatorParam param=%p idx=%d owner=%p ownerCls=%s member=%s",
                        static_cast<void*>(ph), idx, static_cast<void*>(owner),
                        (owner != nullptr && owner->klass != nullptr)
                            ? owner->klass->descriptor.c_str()
                            : "?",
                        DiagMemberSig(owner).c_str());
  }
  // POJOPropertyBuilder.addCtor(AnnotatedParameter, PropertyName, Z,Z,Z)：
  // 打印落入的属性键（builder 自身 o/p 两个 PropertyName）、参数成员。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/fasterxml/jackson/databind/d0/a0;" &&
      method->name == "c0" && arg_count >= 3 && args[1].ref != nullptr) {
    ObjHeader* ph = args[1].ref;
    ObjHeader* owner = nullptr;
    int32_t idx = -1;
    DiagAnnotatedParam(ph, &owner, &idx);
    ObjHeader* builder = args[0].ref;
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "addCtor builder=%p name=%s internal=%s param=%p idx=%d owner=%p member=%s",
                        static_cast<void*>(builder), DiagPropName(builder, "p").c_str(),
                        DiagPropName(builder, "o").c_str(), static_cast<void*>(ph), idx,
                        static_cast<void*>(owner), DiagMemberSig(owner).c_str());
  }
  // ud.i(context, Token, People, proto)：账户落地入口快照（事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/app/util/ud;" &&
      method->name == "i" && arg_count >= 3) {
    ObjHeader* token = args[1].ref;
    ObjHeader* people = args[2].ref;
    int64_t token_uid = 0;
    int64_t people_uid = 0;
    auto read_long = [](ObjHeader* o, const char* name) -> int64_t {
      if (o == nullptr || o->klass == nullptr) return -1;
      uint8_t* data = reinterpret_cast<VObject*>(o)->field_data();
      for (auto& f : o->klass->instance_fields) {
        if (f.name == name) {
          int64_t v = 0;
          std::memcpy(&v, data + f.offset, sizeof(v));
          return v;
        }
      }
      return -1;
    };
    token_uid = read_long(token, "userId");
    people_uid = read_long(people, "uid");
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "ud.i ctx=%p token=%p people=%p tokenUid=%lld tokenHash=%s "
                        "peopleUid=%lld peopleId=%s",
                        static_cast<void*>(args[0].ref), static_cast<void*>(token),
                        static_cast<void*>(people), static_cast<long long>(token_uid),
                        DiagReadString(token, "uid").c_str(),
                        static_cast<long long>(people_uid),
                        DiagReadString(people, "id").c_str());
  }
  // AccountManager.addAccount(Account) 入口（事后移除）：确认落地调用与账户身份。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/app/accounts/AccountManager;" &&
      method->name == "addAccount" && arg_count >= 2 && args[1].ref != nullptr) {
    ObjHeader* acc = args[1].ref;
    ObjHeader* token = nullptr;
    if (acc->klass != nullptr) {
      uint8_t* data = reinterpret_cast<VObject*>(acc)->field_data();
      for (auto& f : acc->klass->instance_fields) {
        if (f.name == "mToken") std::memcpy(&token, data + f.offset, sizeof(token));
      }
    }
    int64_t uid = -1;
    if (token != nullptr && token->klass != nullptr) {
      uint8_t* data = reinterpret_cast<VObject*>(token)->field_data();
      for (auto& f : token->klass->instance_fields) {
        if (f.name == "userId") std::memcpy(&uid, data + f.offset, sizeof(uid));
      }
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "addAccount acc=%p token=%p userId=%lld tokenHash=%s",
                        static_cast<void*>(acc), static_cast<void*>(token),
                        static_cast<long long>(uid), DiagReadString(token, "uid").c_str());
  }
  // GuestUtils.createToken(GuestResponse)：guest 账户创建入参快照（hashId 空值
  // 归因；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/app/util/GuestUtils;" &&
      method->name == "createToken" && arg_count >= 1 && args[0].ref != nullptr) {
    ObjHeader* g = args[0].ref;
    int64_t uid = 0;
    if (g->klass != nullptr) {
      uint8_t* data = reinterpret_cast<VObject*>(g)->field_data();
      for (auto& f : g->klass->instance_fields) {
        if (f.name == "uid") std::memcpy(&uid, data + f.offset, sizeof(uid));
      }
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "createToken guest=%p hashId=%s accessToken=%s tokenType=%s uid=%lld",
                        static_cast<void*>(g), DiagReadString(g, "hashId").c_str(),
                        DiagReadString(g, "accessToken").c_str(),
                        DiagReadString(g, "tokenType").c_str(),
                        static_cast<long long>(uid));
  }
  // TEMP DIAG（appcloud2 za 头归因；事后移除）：记录 Request.Builder 写头调用
  // 中名含 "za" 的（大小写不敏感），以及 x-app-key 圈定 appcloud 请求窗口。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lokhttp3/Request$Builder;" &&
      (method->name == "header" || method->name == "addHeader") && arg_count >= 3 &&
      args[1].ref != nullptr) {
    std::string hname = DiagStringOf(args[1].ref);
    std::string lname;
    lname.reserve(hname.size());
    for (char c : hname) lname.push_back(static_cast<char>(::tolower(c)));
    if (lname.find("za") != std::string::npos || lname == "x-app-key") {
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet",
                          "reqHeader %s=%s (builder=%p count=%zu)", hname.c_str(),
                          DiagStringOf(args[2].ref).c_str(),
                          static_cast<void*>(args[0].ref), arg_count);
    }
  }
  // TEMP DIAG：buildAppInfo 入口（app/n0/a.b/c）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/app/n0/a;" &&
      (method->name == "b" || method->name == "c")) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "buildAppInfo enter %s.%s",
                        method->declaring->descriptor.c_str(), method->name.c_str());
  }
  // TEMP DIAG（BuilderDecorator 注册/查询名比对；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Ljava/util/HashSet;" &&
      (method->name == "add" || method->name == "contains") && arg_count >= 2 &&
      args[1].ref != nullptr && args[1].ref->klass != nullptr &&
      args[1].ref->klass->descriptor == "Ljava/lang/String;") {
    std::string v = DiagStringOf(args[1].ref);
    if (v.find("Decorator") != std::string::npos) {
      int hsize = -1;
      ObjHeader* self = args[0].ref;
      if (self != nullptr && self->klass != nullptr) {
        uint8_t* data = reinterpret_cast<VObject*>(self)->field_data();
        for (auto& f : self->klass->instance_fields) {
          if (f.name == "size") std::memcpy(&hsize, data + f.offset, sizeof(hsize));
        }
      }
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "HashSet.%s %s self=%p size=%d",
                          method->name.c_str(), v.c_str(), static_cast<void*>(self), hsize);
    }
  }
  // TEMP DIAG（FileDownloader 连接面入口追踪；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor.find("filedownloader") != std::string::npos &&
      (method->declaring->descriptor.find("Service") != std::string::npos ||
       method->declaring->descriptor.find("Connection") != std::string::npos ||
       method->declaring->descriptor.find("DownloadTask") != std::string::npos)) {
    static std::atomic<int> fdl_hits{0};
    if (fdl_hits.fetch_add(1) < 800) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmService", "fdl %s.%s",
                          method->declaring->descriptor.c_str(), method->name.c_str());
    }
  }
  // TEMP DIAG（bindService 派发面归因；事后移除）。
  if (method->name == "bindService" && arg_count >= 1) {
    VClass* self_cls = (args[0].ref != nullptr) ? args[0].ref->klass : nullptr;
    __android_log_print(ANDROID_LOG_INFO, "DexVmService",
                        "bindService call decl=%s selfCls=%s native=%d code=%d",
                        method->declaring != nullptr
                            ? method->declaring->descriptor.c_str()
                            : "?",
                        self_cls != nullptr ? self_cls->descriptor.c_str() : "null",
                        method->jni_native_fn != nullptr ? 1 : 0,
                        method->HasCode() ? 1 : 0);
  }
  // TEMP DIAG（模块注册面；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/module/b1;" &&
      (method->name == "c" || method->name == "e" || method->name == "f" ||
       method->name == "d" || method->name == "h")) {
    std::string cls = "?";
    int z = -1;
    if (method->name == "c") {
      cls = "register";
    } else if (arg_count >= 2 && args[1].ref != nullptr) {
      VClass* k = reinterpret_cast<VClass*>(args[1].ref);
      cls = k->descriptor;
    }
    if (method->name != "c" && method->name != "h" && method->name != "d" &&
        arg_count >= 3) {
      z = args[2].i;
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "b1.%s enter cls=%s z=%d",
                        method->name.c_str(), cls.c_str(), z);
  }
  // TEMP DIAG（BuilderDecorator 应用面归因；事后移除）。
  if (method->declaring != nullptr && method->name == "decorate" &&
      method->declaring->descriptor.find("Decorator;") != std::string::npos) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "decorate enter %s",
                        method->declaring->descriptor.c_str());
  }
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/app/util/netplugable/g;" ) {
    static std::atomic<int> g_hits{0};
    if (g_hits.fetch_add(1) < 4) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "netplugable/g.%s",
                          method->name.c_str());
    }
  }
  // TEMP DIAG（NetworkMonitorDecorator Sets.of 重复元素归因；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Ljava8/util/y;" && method->name == "b" &&
      arg_count >= 4) {
    auto cls_name = [](ObjHeader* o) -> std::string {
      return (o != nullptr && o->klass != nullptr) ? o->klass->descriptor : "null";
    };
    __android_log_print(
        ANDROID_LOG_INFO, "DexVmNet",
        "Sets.of(%s:%s | %s:%s | %s:%s | %s:%s)", cls_name(args[0].ref).c_str(),
        DiagStringOf(args[0].ref).c_str(), cls_name(args[1].ref).c_str(),
        DiagStringOf(args[1].ref).c_str(), cls_name(args[2].ref).c_str(),
        DiagStringOf(args[2].ref).c_str(), cls_name(args[3].ref).c_str(),
        DiagStringOf(args[3].ref).c_str());
    // 原始 String 内容（length + 前 8 字符 hex），判定是否转换面丢字。
    auto raw_str = [](ObjHeader* o) -> std::string {
      if (o == nullptr || o->klass == nullptr ||
          o->klass->descriptor != "Ljava/lang/String;") {
        return "?";
      }
      VString* s = reinterpret_cast<VString*>(o);
      char buf[128];
      int n = std::snprintf(buf, sizeof(buf), "len=%d [", s->length);
      for (int i = 0; i < s->length && i < 8 && n < (int)sizeof(buf) - 8; i++) {
        n += std::snprintf(buf + n, sizeof(buf) - n, "%04x ", s->chars()[i]);
      }
      std::snprintf(buf + n, sizeof(buf) - n, "]");
      return buf;
    };
    __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "setsRaw %s | %s | %s | %s",
                        raw_str(args[0].ref).c_str(), raw_str(args[1].ref).c_str(),
                        raw_str(args[2].ref).c_str(), raw_str(args[3].ref).c_str());
    // 若调用方是 NetworkMonitorDecorator.<init>，将其 dex 文件落盘供离线反汇编。
    {
      const Frame* hidden = nullptr;
      for (auto it = state.frame_stack.rbegin(); it != state.frame_stack.rend(); ++it) {
        const Frame* f = *it;
        if (f != nullptr && f->method != nullptr && f->method->declaring != nullptr &&
            f->method->declaring->descriptor ==
                "Lcom/zhihu/android/api/util/NetworkMonitorDecorator;" &&
            f->method->name == "<init>") {
          hidden = f;
          break;
        }
      }
      static bool dumped = false;
      if (hidden != nullptr && !dumped && hidden->method->dex_file != nullptr) {
        dumped = true;
        const dex::DexFile* df = hidden->method->dex_file;
        std::string path = "/data/data/com.dexvm.host/files/hidden_monitor.dex";
        if (FILE* fp = std::fopen(path.c_str(), "wb")) {
          std::fwrite(df->data(), 1, df->size(), fp);
          std::fclose(fp);
          __android_log_print(ANDROID_LOG_INFO, "DexVmNet",
                              "dumped hidden dex %zu bytes to %s", df->size(),
                              path.c_str());
        }
      }
    }
    // 栈回溯（定位四个空串来源的隐藏方法）。
    int levels = 0;
    for (auto it = state.frame_stack.rbegin();
         it != state.frame_stack.rend() && levels < 10; ++it, ++levels) {
      const Frame* f = *it;
      if (f == nullptr || f->method == nullptr) continue;
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "  setsAt %s.%s@%u",
                          f->method->declaring != nullptr
                              ? f->method->declaring->descriptor.c_str()
                              : "?",
                          f->method->name.c_str(), f->dex_pc);
    }
  }
  // TEMP DIAG（appcloud2 za 头归因；事后移除）：签名路径（appcloudsdk/b0.*）
  // 首现时主动调用一次 app/n0/a.b()，把 x-app-za 的真实值抓出来。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/appcloudsdk/b0;") {
    static std::atomic<bool> probed{false};
    if (!probed.exchange(true) && state.linker != nullptr) {
      VClass* k = state.linker->FindClass("Lcom/zhihu/android/app/n0/a;");
      VMethod* bm = nullptr;
      if (k != nullptr) {
        for (auto& m : k->direct_methods) {
          if (m.name == "b" && m.return_type == "Ljava/lang/String;" &&
              m.param_types.empty()) {
            bm = &m;
            break;
          }
        }
      }
      ObjHeader* saved = state.pending_exception;
      state.pending_exception = nullptr;
      Value r{};
      bool ok = bm != nullptr && InvokeMethod(state, bm, nullptr, 0, &r);
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet",
                          "probe n0/a.b ok=%d pending=%d value=%s", ok ? 1 : 0,
                          state.pending_exception != nullptr ? 1 : 0,
                          ok && state.pending_exception == nullptr
                              ? DiagStringOf(r.ref).c_str()
                              : "?");
      state.pending_exception = saved;
    }
  }
  // POJOPropertyBuilder.addAll(POJOPropertyBuilder)：侦测自合并（同一 builder
  // 被 map 双重登记）——会把 ctor 链复制成同名双节点。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/fasterxml/jackson/databind/d0/a0;" &&
      method->name == "b0" && arg_count >= 2 && args[1].ref != nullptr) {
    ObjHeader* self = args[0].ref;
    ObjHeader* other = args[1].ref;
    const Frame* caller = state.frame_stack.empty() ? nullptr : state.frame_stack.back();
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "addAll self=%p other=%p selfSame=%d name=%s/%s internal=%s/%s caller=%s",
                        static_cast<void*>(self), static_cast<void*>(other),
                        self == other ? 1 : 0, DiagPropName(self, "o").c_str(),
                        DiagPropName(other, "o").c_str(), DiagPropName(self, "p").c_str(),
                        DiagPropName(other, "p").c_str(),
                        (caller != nullptr && caller->method != nullptr &&
                         caller->method->declaring != nullptr)
                            ? (caller->method->declaring->descriptor + "." +
                               caller->method->name)
                                  .c_str()
                            : "?");
  }
#endif
  if (g_guest_trace_on.load(std::memory_order_relaxed) && method->declaring != nullptr &&
      GuestTraceMatches(method->declaring->descriptor)) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DexVmTrace", "%s.%s",
                        method->declaring->descriptor.c_str(), method->name.c_str());
#endif
  }
  // JNI native 派发（M2 汇合点）：RegisterNatives 的方法走 jni trampoline 钩子。
  if (method->jni_native_fn != nullptr) {
    NativeCallHook hook = GetNativeCallHook();
    if (hook == nullptr) {
      ThrowUnsupportedOperationException(state, "JNI native hook not installed");
      return false;
    }
#if defined(__ANDROID__) && DEXVM_CALL_DIAGNOSTICS
    // TEMP DIAG（H.d 解密结果归因；事后移除）。
    const bool is_hd = method->declaring != nullptr &&
                       method->declaring->descriptor == "Lcom/secneo/apkwrapper/H;" &&
                       method->name == "d";
    std::string hd_in;
    if (is_hd && arg_count >= 1) hd_in = DiagStringOf(args[0].ref);
    bool hd_ok = hook(&state, method, args, arg_count, result);
    if (is_hd) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmJniStr", "H.d(%s) -> ok=%d \"%s\"",
                          hd_in.c_str(), hd_ok ? 1 : 0,
                          (hd_ok && result->ref != nullptr)
                              ? DiagStringOf(result->ref).c_str()
                              : "?");
    }
    return hd_ok;
#else
    return hook(&state, method, args, arg_count, result);
#endif
  }
  // Intrinsic 派发（M2，core/intrinsic.h 约定）：native_fn 非空走 IntrinsicFn 直调。
  // self 语义：实例方法 = args[0].ref（receiver），static = nullptr；args/n 不含 self。
  if (method->native_fn != nullptr) {
    IntrinsicFn fn = AsIntrinsic(method->native_fn);
    VObject* self = nullptr;
    const Value* params = args;
    size_t n = arg_count;
    if (!method->IsStatic()) {
      if (arg_count == 0 || args[0].ref == nullptr) {
        ThrowNullPointerException(state, "intrinsic receiver == null");
        return false;
      }
      self = reinterpret_cast<VObject*>(args[0].ref);
      params = args + 1;
      n = arg_count - 1;
    }
    bool ok = true;
    Value r = fn(state, self, params, n, &ok);
    if (!ok) return false;
    // 返回值卫生（根因防御纵深；对照 vm_types.h NSDMI 注释）：
    //   窄型（Z B C S I F V）按 32 位语义清高半槽；J/D 全宽由 intrinsic 承诺；
    //   引用型过 IsHeapStart 校验——脏引用按 InternalError 诊断（带来源），
    //   不让联合体尾半槽垃圾流进 move-result-object/check-cast 解引用面。
    {
      char rt = method->return_type.empty() ? 'V' : method->return_type[0];
      switch (rt) {
        case 'Z':
        case 'B':
        case 'C':
        case 'S':
        case 'I':
        case 'F':
          r.l = static_cast<int64_t>(static_cast<uint32_t>(r.l));
          break;
        case 'V':
          r.l = 0;
          break;
        case 'J':
        case 'D':
          break;
        default: {  // L / [
          if (r.ref != nullptr && state.heap != nullptr && !state.heap->IsHeapStart(r.ref)) {
            const void* owner = nullptr;
            size_t owner_off = 0;
            mem::Heap::PtrShape shape = state.heap->ClassifyPtr(r.ref, &owner, &owner_off);
            char dmsg[320];
            if (shape == mem::Heap::PtrShape::kHeapInterior) {
              std::snprintf(dmsg, sizeof(dmsg),
                            "dirty intrinsic result 0x%016llx from %s [shape=%s owner=0x%016llx +%zu]",
                            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r.ref)),
                            MethodSigStr(method).c_str(), PtrShapeName(shape),
                            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(owner)),
                            owner_off);
            } else {
              std::snprintf(dmsg, sizeof(dmsg),
                            "dirty intrinsic result 0x%016llx from %s [shape=%s]",
                            static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r.ref)),
                            MethodSigStr(method).c_str(), PtrShapeName(shape));
            }
            state.ThrowNew("Ljava/lang/InternalError;", dmsg);
            return false;
          }
          break;
        }
      }
    }
    *result = r;
    return true;
  }
  if (!method->HasCode()) {
    // 骨架方法：<init>/<clinit> 无码 = 空实现（builtin 构造器）
    if (method->name == "<init>" || method->name == "<clinit>") return true;
    // ART 语义（interpreter_common.cc / art_method.cc）：
    //   abstract 无实现 -> AbstractMethodError；
    //   native 未注册实现 -> UnsatisfiedLinkError("No implementation found for: ...")；
    //   其余无码骨架（VM 内建面缺口）-> UnsupportedOperationException 诊断。
    constexpr uint32_t kAccAbstract = 0x400;
    constexpr uint32_t kAccNative = 0x100;
    if ((method->access_flags & kAccAbstract) != 0) {
      state.ThrowNew("Ljava/lang/AbstractMethodError;", MethodSigStr(method));
      return false;
    }
    if ((method->access_flags & kAccNative) != 0) {
      // JNI 导出符号回落（art_jni_dlsym_lookup 等价）：未 RegisterNatives
      // 绑定的 native 经 trampoline 尝试 Java_* 导出符号解析+派发（未命中由
      // trampoline 抛 UnsatisfiedLinkError，诊断保真）；JNI 模块未装（hook
      // 缺位，宿主纯核心测试）维持直接抛。
      NativeCallHook hook = GetNativeCallHook();
      if (hook != nullptr) return hook(&state, method, args, arg_count, result);
      state.ThrowNew("Ljava/lang/UnsatisfiedLinkError;",
                     "No implementation found for: " + MethodSigStr(method));
      return false;
    }
    ThrowUnsupportedOperationException(state, "no code: " + MethodSigStr(method));
    return false;
  }
  const dex::CodeItem* code = method->code;

  Frame frame;
  frame.method = method;
  frame.insns = code->insns;
  frame.dex_pc = 0;
  frame.caller = state.frame_stack.empty() ? nullptr : state.frame_stack.back();
  {
    const GuestSchedOps* ops = GuestSched();
    frame.owner = (ops != nullptr && ops->current_task != nullptr) ? ops->current_task()
                                                                  : nullptr;
  }
  // +1 个 payload 为返回值槽
  frame.regs.assign(2u * (code->registers_size + 1u), 0u);

  // 铺 ins 区：first_dest_reg = registers_size - inputs_size
  uint32_t ins_words = code->ins_size < code->registers_size ? code->ins_size : code->registers_size;
  uint32_t dest = code->registers_size - ins_words;
  size_t ai = 0;
  if (!method->IsStatic()) {
    // receiver 首参槽
    if (ai < arg_count && dest < code->registers_size) SetRef(frame, dest, args[ai].ref);
    ai += 1;
    dest += 1;
  }
  for (const auto& p : method->param_types) {
    if (ai >= arg_count || dest >= code->registers_size + 1) break;
    if (p == "J" || p == "D") {
      SetL(frame, dest, args[ai].l);  // wide 一个 Value 装 64 位，落 P(v)（占两 vreg 号）
      dest += 2;
    } else if (p[0] == 'L' || p[0] == '[') {
      SetRef(frame, dest, args[ai].ref);
      dest += 1;
    } else {
      SetI(frame, dest, args[ai].i);
      dest += 1;
    }
    ai += 1;
  }

  if (!ExecuteFrame(state, frame)) {
#if defined(__ANDROID__)
    // TEMP DIAG：RecyclerView LM 子类 setRecyclerView 覆写执行失败归因。
    if (method->name == "setRecyclerView" && method->declaring != nullptr &&
        method->declaring->descriptor.find("LayoutManager") != std::string::npos) {
      ObjHeader* ex = state.pending_exception;
      std::string desc = ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor : "?";
      std::string msg;
      if (ex != nullptr) {
        for (VClass* k = ex->klass; k != nullptr && msg.empty(); k = k->super_class) {
          for (auto& f : k->instance_fields) {
            if (f.name == "detailMessage") {
              ObjHeader* m = nullptr;
              std::memcpy(&m, reinterpret_cast<VObject*>(ex)->field_data() + f.offset, sizeof(m));
              if (m != nullptr) msg = static_cast<VString*>(m)->ToModifiedUtf8();
              break;
            }
          }
        }
      }
      __android_log_print(ANDROID_LOG_INFO, "DexVmRV", "setRecyclerView FAILED in %s: %s: %s",
                          method->declaring->descriptor.c_str(), desc.c_str(), msg.c_str());
    }
#endif
    return false;
  }
  result->l = static_cast<int64_t>(GetP(frame, code->registers_size));
#if defined(__ANDROID__)
  // AOSP 不变量修复：RecyclerView.setLayoutManager 成功后，LayoutManager 必须
  // 绑定回该 RecyclerView（mRecyclerView + mChildHelper）。实测 app 子类
  // CatchLinearLayoutManager 覆写 setRecyclerView 时未调 super，导致
  // LayoutManager.onMeasure 默认实现 NPE，整窗 measure 中止（首帧 bounds 全 0）。
  if (method->name == "setLayoutManager" && method->declaring != nullptr &&
      method->declaring->descriptor == "Landroidx/recyclerview/widget/RecyclerView;") {
    auto find_field = [](VClass* cls, const char* name) -> VField* {
      for (VClass* k = cls; k != nullptr; k = k->super_class) {
        for (auto& f : k->instance_fields) {
          if (f.name == name) return &f;
        }
      }
      return nullptr;
    };
    auto read_ref = [](VObject* o, VField* f) -> ObjHeader* {
      ObjHeader* r = nullptr;
      if (o != nullptr && f != nullptr) std::memcpy(&r, o->field_data() + f->offset, sizeof(r));
      return r;
    };
    VObject* rv = reinterpret_cast<VObject*>(args[0].ref);
    ObjHeader* lm_h = rv != nullptr ? read_ref(rv, find_field(rv->klass, "mLayout")) : nullptr;
    if (rv != nullptr && lm_h != nullptr) {
      VObject* lm = reinterpret_cast<VObject*>(lm_h);
      VField* back_f = find_field(lm->klass, "mRecyclerView");
      if (back_f != nullptr && read_ref(lm, back_f) == nullptr) {
        std::memcpy(lm->field_data() + back_f->offset, &rv, sizeof(rv));
        VField* src_ch = find_field(rv->klass, "mChildHelper");
        VField* dst_ch = find_field(lm->klass, "mChildHelper");
        if (src_ch != nullptr && dst_ch != nullptr) {
          ObjHeader* helper = read_ref(rv, src_ch);
          if (helper != nullptr) {
            std::memcpy(lm->field_data() + dst_ch->offset, &helper, sizeof(helper));
          }
        }
        static std::atomic<int> bind_fixups{0};
        if (bind_fixups.fetch_add(1, std::memory_order_relaxed) < 10) {
          __android_log_print(ANDROID_LOG_INFO, "DexVmRV",
                              "rebind LayoutManager %s -> RecyclerView %p",
                              lm->klass != nullptr ? lm->klass->descriptor.c_str() : "?", rv);
        }
      }
    }
  }
#endif
#if defined(__ANDROID__) && DEXVM_CALL_DIAGNOSTICS
  // TEMP DIAG（ServiceManifest 查询路径归因；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/module/b1;" &&
      (method->name == "h" || method->name == "d" || method->name == "f")) {
    ObjHeader* list = result->ref;
    int size = -1;
    if (list != nullptr && list->klass != nullptr) {
      VClass* lc = list->klass;
      for (auto& m : lc->virtual_methods) {
        if (m.name == "size" && m.param_types.empty() && m.return_type == "I") {
          ObjHeader* saved = state.pending_exception;
          state.pending_exception = nullptr;
          Value args1[1];
          args1[0].ref = list;
          Value r{};
          if (InvokeMethod(state, const_cast<VMethod*>(&m), args1, 1, &r) &&
              state.pending_exception == nullptr) {
            size = r.i;
          } else {
            state.pending_exception = saved;
          }
          state.pending_exception = saved;
          break;
        }
      }
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "b1.%s -> %s size=%d",
                        method->name.c_str(), list == nullptr ? "null" : "list", size);
  }
  // TEMP DIAG（URLConnection 下载面归因；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor.find("BridgedHttpURLConnection") != std::string::npos) {
    if (method->name == "read") {
      static std::atomic<int> rd_hits{0};
      if (result->i <= 0 && rd_hits.fetch_add(1) < 30) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmUrl",
                            "%s.%s -> %d params=%zu", method->declaring->descriptor.c_str(),
                            method->name.c_str(), result->i, method->param_types.size());
      }
    } else {
      static std::atomic<int> entry_hits{0};
      if (entry_hits.fetch_add(1) < 40) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmUrl", "%s.%s ret=%s",
                            method->declaring->descriptor.c_str(), method->name.c_str(),
                            method->return_type.c_str());
      }
    }
  }
  // TEMP DIAG（HashSet.contains 结果核对；事后移除）。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Ljava/util/HashSet;" &&
      method->name == "contains" && arg_count >= 2 && args[1].ref != nullptr) {
    std::string v = DiagStringOf(args[1].ref);
    if (v.find("Decorator") != std::string::npos) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "HashSet.contains(%s) => %d",
                          v.c_str(), result->i);
    }
  }
  // TEMP DIAG（appcloud2 za 头归因；事后移除）：buildAppInfo 返回值快照。
  if (method->declaring != nullptr &&
      method->declaring->descriptor == "Lcom/zhihu/android/app/n0/a;" &&
      method->return_type == "Ljava/lang/String;") {
    __android_log_print(ANDROID_LOG_INFO, "DexVmNet", "buildAppInfo %s -> %s",
                        method->name.c_str(), DiagStringOf(result->ref).c_str());
  }
#endif
  return true;
}

bool ExecuteFrame(VMState& state, Frame& frame) {
  const VMethod* method = frame.method;
  const dex::CodeItem* code = method->code;
  const uint32_t nregs = code->registers_size;
  if (frame.regs.size() < 2u * (nregs + 1u)) frame.regs.resize(2u * (nregs + 1u), 0u);
  const uint16_t* insns = code->insns;
  const uint32_t insns_size = code->insns_size;
  const uint32_t return_slot = nregs;

  Value pending_result{};  // invoke/filled-new-array 结果，move-result* 读取
  ObjHeader* delivered = nullptr;  // handler 入口待 move-exception 取走的异常
  bool have_delivered = false;

  state.frame_stack.push_back(&frame);
  // M4.4：frame_stack 为多 GuestTask 共享的活跃帧集（GC root / 抛点栈快照）。
  // fiber 换出使各 task 的 push/pop 跨 task 交错，pop_back 会误摘他人帧
  // （悬挂指针 -> CaptureThrowBacktrace 解引用 SIGSEGV）：完成时按身份摘除
  // 自己的帧（frame 地址在其 ExecuteFrame 生存期内唯一）。
  struct PopGuard {
    std::vector<Frame*>* stack;
    Frame* self;
    ~PopGuard() {
      for (auto it = stack->rbegin(); it != stack->rend(); ++it) {
        if (*it == self) {
          stack->erase(std::next(it).base());
          return;
        }
      }
    }
  } guard{&state.frame_stack, &frame};

  for (;;) {
    // 异常停步：当前 frame 查 handler；未命中 unwind 递交 caller
    if (state.HasPendingException()) {
      // 裸兜底异常升级（docs 12 章 22.10）：堆耗尽归因的裸对象换预分配真
      // OutOfMemoryError——catch(OutOfMemoryError) 可命中（不可改抛点兜底）。
      UpgradeBareOomIfExhausted(state);
      uint32_t handler_pc = 0;
      if (FindCatchBlock(state, frame, frame.dex_pc, &handler_pc)) {
        ObjHeader* ex = state.pending_exception;
        state.ClearException();
        frame.dex_pc = handler_pc;
        if (handler_pc < insns_size && (insns[handler_pc] & 0xffu) == dex::kMoveException) {
          delivered = ex;
          have_delivered = true;
        }
        // 否则 has_no_move_exception：异常丢弃（对照 FindCatchBlock 语义）
        continue;
      }
      return false;
    }
    if (frame.dex_pc >= insns_size) return true;  // 防御：非法落出

    const uint32_t pc = frame.dex_pc;
    const uint16_t* ins = insns + pc;
    const uint32_t u0 = ins[0];
    const uint8_t op = static_cast<uint8_t>(u0 & 0xffu);
    const uint32_t next_pc = pc + dex::InsnWidth(ins);
    const uint32_t aa = (u0 >> 8) & 0xffu;
    const uint32_t a4 = (u0 >> 8) & 0xfu;
    const uint32_t b4 = (u0 >> 12) & 0xfu;

    switch (op) {
      // ---- 0x00 nop ----
      case dex::kNop:
        break;

      // ---- move 族 ----
      case dex::kMove:
        SetP(frame, a4, GetP(frame, b4));
        break;
      case dex::kMoveFrom16:
        SetP(frame, aa, GetP(frame, ins[1]));
        break;
      case dex::kMove16:
        SetP(frame, ins[1], GetP(frame, ins[2]));
        break;
      case dex::kMoveWide:
        // wide 值整体落 P(a4)，镜像 P(a4+1) 同步（不再拷源镜像：源镜像可能
        // 是未同步的陈旧半槽——脏 vreg 传播源之一）
        SetWideP(frame, a4, GetP(frame, b4));
        break;
      case dex::kMoveWideFrom16:
        SetWideP(frame, aa, GetP(frame, ins[1]));
        break;
      case dex::kMoveWide16:
        SetWideP(frame, ins[1], GetP(frame, ins[2]));
        break;
      case dex::kMoveObject:
        SetRef(frame, a4, GetRef(frame, b4));
        break;
      case dex::kMoveObjectFrom16:
        SetRef(frame, aa, GetRef(frame, ins[1]));
        break;
      case dex::kMoveObject16:
        SetRef(frame, ins[1], GetRef(frame, ins[2]));
        break;

      // ---- move-result / move-exception ----
      case dex::kMoveResult:
        SetP(frame, aa, static_cast<uint32_t>(pending_result.l));
        break;
      case dex::kMoveResultWide:
        SetWideP(frame, aa, static_cast<uint64_t>(pending_result.l));
        break;
      case dex::kMoveResultObject:
        SetP(frame, aa, static_cast<uint64_t>(pending_result.l));
        break;
      case dex::kMoveException: {
        if (have_delivered) {
          SetRef(frame, aa, delivered);
          delivered = nullptr;
          have_delivered = false;
        } else if (state.HasPendingException()) {
          SetRef(frame, aa, state.pending_exception);
          state.ClearException();
        } else {
          SetRef(frame, aa, nullptr);
        }
        break;
      }

      // ---- return 族 ----
      case dex::kReturnVoid:
        SetP(frame, return_slot, 0);
        return true;
      case dex::kReturn:
        SetP(frame, return_slot, static_cast<uint32_t>(GetI(frame, aa)));
        return true;
      case dex::kReturnWide:
        SetP(frame, return_slot, GetP(frame, aa));
        return true;
      case dex::kReturnObject:
        SetP(frame, return_slot, GetP(frame, aa));
        return true;

      // ---- const 族 ----
      case dex::kConst4:
        SetI(frame, a4, Sgn4(b4));
        break;
      case dex::kConst16:
        SetI(frame, aa, Sgn16(ins[1]));
        break;
      case dex::kConst:
        SetI(frame, aa, static_cast<int32_t>(ins[1] | (ins[2] << 16)));
        break;
      case dex::kConstHigh16:
        SetI(frame, aa, static_cast<int32_t>(ins[1] << 16));
        break;
      case dex::kConstWide16:
        SetL(frame, aa, Sgn16(ins[1]));
        break;
      case dex::kConstWide32:
        SetL(frame, aa, static_cast<int64_t>(static_cast<int32_t>(ins[1] | (ins[2] << 16))));
        break;
      case dex::kConstWide:
        SetWideP(frame, aa, static_cast<uint64_t>(ins[1]) | (static_cast<uint64_t>(ins[2]) << 16) |
                                (static_cast<uint64_t>(ins[3]) << 32) |
                                (static_cast<uint64_t>(ins[4]) << 48));
        break;
      case dex::kConstWideHigh16:
        SetL(frame, aa, static_cast<int64_t>(static_cast<uint64_t>(ins[1]) << 48));
        break;
      case dex::kConstString: {
        VString* s = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          s = state.linker->ResolveString(method->dex_file, ins[1]);
        }
        if (s == nullptr) {
          ThrowOOM(state, "const-string resolve failed");
          continue;
        }
        SetRef(frame, aa, reinterpret_cast<ObjHeader*>(s));
        break;
      }
      case dex::kConstStringJumbo: {
        uint32_t idx = ins[1] | (ins[2] << 16);
        VString* s = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          s = state.linker->ResolveString(method->dex_file, idx);
        }
        if (s == nullptr) {
          ThrowOOM(state, "const-string/jumbo resolve failed");
          continue;
        }
        SetRef(frame, aa, reinterpret_cast<ObjHeader*>(s));
        break;
      }
      case dex::kConstClass: {
        VClass* k = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          k = state.linker->ResolveType(method->dex_file, ins[1], &state);
        }
        if (k == nullptr) {
          ThrowTypeResolveError(state, method->dex_file, ins[1]);
          continue;
        }
        SetRef(frame, aa, reinterpret_cast<ObjHeader*>(k));
        break;
      }

      // ---- monitor（M4.4：重入计数 + owner/等待队列侧表；争用协程挂起）----
      case dex::kMonitorEnter: {
        ObjHeader* obj = GetRef(frame, aa);
        if (obj == nullptr) {
          ThrowNullPointerException(state, "monitor-enter on null");
          continue;
        }
        if (!CheckRef(state, frame, obj, aa, "monitor-enter")) continue;
        MonitorEnterImpl(state, obj);
        if (state.HasPendingException()) continue;
        break;
      }
      case dex::kMonitorExit: {
        ObjHeader* obj = GetRef(frame, aa);
        if (obj == nullptr) {
          ThrowNullPointerException(state, "monitor-exit on null");
          continue;
        }
        if (!CheckRef(state, frame, obj, aa, "monitor-exit")) continue;
        if (!MonitorExitImpl(state, obj)) continue;
        break;
      }

      // ---- 类型检查 ----
      case dex::kCheckCast: {
        ObjHeader* obj = GetRef(frame, aa);
        if (obj == nullptr) break;  // null 通过
        if (!CheckRef(state, frame, obj, aa, "check-cast")) continue;  // 脏引用防御
        VClass* target = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          target = state.linker->ResolveType(method->dex_file, ins[1], &state);
        }
        if (target == nullptr) {
          ThrowTypeResolveError(state, method->dex_file, ins[1]);
          continue;
        }
        // 镜像面（M4.3 约定：klass 槽未挂 class_class 的类镜像——VClass 自身即
        // 对象）：check-cast 对镜像按 java.lang.Class 实例判定（对照 DoCall 虚调用
        // 回落面）。dex 定义类的镜像 klass 槽未由 DefineClass 填充（存量缺口，
        // class_linker 侧后置），java.lang.invoke 家族消费面（Class 参数 check-cast）
        // 在此兜底。
        VClass* obj_klass = obj->klass;
        bool mirror = false;
        if (obj_klass == nullptr && state.linker != nullptr) {
          VClass* class_class = state.linker->FindClass("Ljava/lang/Class;");
          mirror = class_class != nullptr &&
                   (target == class_class || target->descriptor == "Ljava/lang/Object;");
          obj_klass = class_class;
        }
        if (!mirror && (obj_klass == nullptr || !target->IsAssignableFrom(obj_klass))) {
          std::string from = obj_klass != nullptr ? obj_klass->descriptor : "null";
          ThrowClassCastException(state, from, target->descriptor);
          continue;
        }
        break;
      }
      case dex::kInstanceOf: {
        ObjHeader* obj = GetRef(frame, b4);
        if (obj != nullptr && !CheckRef(state, frame, obj, b4, "instance-of")) continue;
        VClass* target = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          target = state.linker->ResolveType(method->dex_file, ins[1], &state);
        }
        if (target == nullptr) {
          ThrowTypeResolveError(state, method->dex_file, ins[1]);
          continue;
        }
        bool inst = false;
        if (obj != nullptr) {
          VClass* obj_klass = obj->klass;
          if (obj_klass == nullptr && state.linker != nullptr) {
            VClass* class_class = state.linker->FindClass("Ljava/lang/Class;");
            inst = class_class != nullptr &&
                   (target == class_class || target->descriptor == "Ljava/lang/Object;");
          } else {
            inst = obj_klass != nullptr && target->IsAssignableFrom(obj_klass);
          }
        }
        SetI(frame, a4, inst ? 1 : 0);
        break;
      }
      case dex::kArrayLength: {
        ObjHeader* obj = GetRef(frame, b4);
        if (obj == nullptr) {
          ThrowNullPointerException(state, "array-length on null");
          continue;
        }
        if (!CheckRef(state, frame, obj, b4, "array-length")) continue;
        SetI(frame, a4, static_cast<VArray*>(obj)->length);
        break;
      }

      // ---- 分配 ----
      case dex::kNewInstance: {
        VClass* k = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          k = state.linker->ResolveType(method->dex_file, ins[1], &state);
        }
        if (k == nullptr) {
          ThrowTypeResolveError(state, method->dex_file, ins[1]);
          continue;
        }
        if (k->is_array || k->is_primitive || k->IsInterface() || k->IsAbstract()) {
          state.ThrowNew("Ljava/lang/InstantiationError;", k->descriptor);
          continue;
        }
        if (!EnsureClassInitialized(state, k)) continue;
        VObject* obj = state.linker != nullptr ? state.linker->AllocObject(k) : nullptr;
        if (obj == nullptr) {
          ThrowOOM(state, "new-instance allocation failed");
          continue;
        }
        SetRef(frame, aa, reinterpret_cast<ObjHeader*>(obj));
        break;
      }
      case dex::kNewArray: {
        int32_t length = GetI(frame, b4);
        if (length < 0) {
          state.ThrowNew("Ljava/lang/NegativeArraySizeException;", std::to_string(length));
          continue;
        }
        VClass* k = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          k = state.linker->ResolveType(method->dex_file, ins[1], &state);
        }
        if (k == nullptr) {
          ThrowTypeResolveError(state, method->dex_file, ins[1]);
          continue;
        }
        VArray* arr = state.linker != nullptr ? state.linker->AllocArray(k, length) : nullptr;
        if (arr == nullptr) {
          ThrowOOM(state, "new-array allocation failed");
          continue;
        }
        SetRef(frame, a4, reinterpret_cast<ObjHeader*>(arr));
        break;
      }
      case dex::kFilledNewArray:
      case dex::kFilledNewArrayRange: {
        CallSite cs = (op == dex::kFilledNewArray) ? DecodeCallSite35c(ins) : DecodeCallSite3rc(ins);
        int32_t length = cs.count;
        VClass* k = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          k = state.linker->ResolveType(method->dex_file, cs.idx, &state);
        }
        if (k == nullptr) {
          ThrowTypeResolveError(state, method->dex_file, cs.idx);
          continue;
        }
        VArray* arr = state.linker != nullptr ? state.linker->AllocArray(k, length) : nullptr;
        if (arr == nullptr) {
          ThrowOOM(state, "filled-new-array allocation failed");
          continue;
        }
        VClass* comp = k->component_type;
        bool ref_comp = comp != nullptr && (comp->descriptor[0] == 'L' || comp->descriptor[0] == '[');
        for (int32_t i = 0; i < length; i++) {
          uint32_t src = WordReg(cs, static_cast<uint32_t>(i));
          if (ref_comp) {
            StoreSlot(arr->data() + i * 8,
                      static_cast<uint64_t>(reinterpret_cast<uintptr_t>(GetRef(frame, src))));
          } else {
            StoreSlot(arr->data() + i * 8, NarrowWrite(Narrow::kWord, GetP(frame, src)));
          }
        }
        pending_result.l = static_cast<int64_t>(reinterpret_cast<uintptr_t>(arr));
        break;
      }
      case dex::kFillArrayData: {
        ObjHeader* obj = GetRef(frame, aa);
        if (obj == nullptr) {
          ThrowNullPointerException(state, "fill-array-data on null");
          continue;
        }
        VArray* arr = static_cast<VArray*>(obj);
        int32_t payload_delta = static_cast<int32_t>(ins[1] | (ins[2] << 16));
        const uint16_t* payload = ins + payload_delta;
        if (payload_delta < 0 || pc + payload_delta >= insns_size ||
            payload[0] != dex::kArrayDataPayload) {
          ThrowIllegalArgumentException(state, "bad fill-array-data payload");
          continue;
        }
        uint32_t elem_width = payload[1];
        uint32_t size = static_cast<uint32_t>(payload[2]) | (static_cast<uint32_t>(payload[3]) << 16);
        if (static_cast<uint32_t>(arr->length) != size) {
          ThrowIllegalArgumentException(state, "fill-array-data length mismatch");
          continue;
        }
        const uint8_t* data = reinterpret_cast<const uint8_t*>(payload + 4);
        VClass* comp = arr->klass != nullptr ? arr->klass->component_type : nullptr;
        for (uint32_t i = 0; i < size; i++) {
          uint64_t raw = 0;
          for (uint32_t b = 0; b < elem_width && b < 8; b++) {
            raw |= static_cast<uint64_t>(data[i * elem_width + b]) << (8 * b);
          }
          uint64_t slot;
          if (comp == nullptr || comp->descriptor[0] == 'L' || comp->descriptor[0] == '[') {
            slot = 0;  // 引用数组不允许 fill-array-data；置 null
          } else {
            switch (comp->primitive_type) {
              case 'Z':
              case 'B':
                slot = NarrowWrite(Narrow::kByte, raw);
                break;
              case 'S':
                slot = NarrowWrite(Narrow::kShort, raw);
                break;
              case 'C':
                slot = NarrowWrite(Narrow::kChar, raw);
                break;
              case 'J':
              case 'D':
                slot = raw;
                break;
              default:  // I / F
                slot = NarrowWrite(Narrow::kWord, raw);
                break;
            }
          }
          StoreSlot(arr->data() + i * 8, slot);
        }
        break;
      }

      // ---- throw / goto / switch ----
      case dex::kThrow: {
        ObjHeader* ex = GetRef(frame, aa);
        if (ex == nullptr) {
          ThrowNullPointerException(state, "throw null");
          continue;
        }
        if (!CheckRef(state, frame, ex, aa, "throw")) continue;
        state.Throw(ex);
        continue;  // 不前进：catch 按 throw 指令 pc 查找
      }
      case dex::kGoto:
        frame.dex_pc = pc + Sgn8(u0 >> 8);
        if (frame.dex_pc < pc) GuestYieldPoint();  // 回边：多 GuestTask 并发推进
        continue;
      case dex::kGoto16:
        frame.dex_pc = pc + Sgn16(ins[1]);
        if (frame.dex_pc < pc) GuestYieldPoint();
        continue;
      case dex::kGoto32:
        frame.dex_pc = pc + static_cast<int32_t>(ins[1] | (ins[2] << 16));
        if (frame.dex_pc < pc) GuestYieldPoint();
        continue;
      case dex::kPackedSwitch: {
        int32_t delta = static_cast<int32_t>(ins[1] | (ins[2] << 16));
        const uint16_t* payload = ins + delta;
        if (delta < 0 || pc + delta >= insns_size || payload[0] != dex::kPackedSwitchPayload) {
          ThrowIllegalArgumentException(state, "bad packed-switch payload");
          continue;
        }
        uint32_t size = payload[1];
        int32_t first_key = static_cast<int32_t>(payload[2] | (payload[3] << 16));
        int32_t v = GetI(frame, aa);
        if (static_cast<uint32_t>(v - first_key) < size) {
          const uint16_t* target_slot = payload + 4 + 2 * (v - first_key);
          int32_t rel = static_cast<int32_t>(target_slot[0] | (target_slot[1] << 16));
          frame.dex_pc = pc + rel;
          if (frame.dex_pc < pc) GuestYieldPoint();
          continue;
        }
        break;  // 未命中走顺序下一条
      }
      case dex::kSparseSwitch: {
        int32_t delta = static_cast<int32_t>(ins[1] | (ins[2] << 16));
        const uint16_t* payload = ins + delta;
        if (delta < 0 || pc + delta >= insns_size || payload[0] != dex::kSparseSwitchPayload) {
          ThrowIllegalArgumentException(state, "bad sparse-switch payload");
          continue;
        }
        uint32_t size = payload[1];
        int32_t v = GetI(frame, aa);
        bool hit = false;
        for (uint32_t i = 0; i < size && !hit; i++) {
          const uint16_t* key_slot = payload + 2 + 2 * i;
          int32_t key = static_cast<int32_t>(key_slot[0] | (key_slot[1] << 16));
          if (key == v) {
            const uint16_t* target_slot = payload + 2 + 2 * size + 2 * i;
            int32_t rel = static_cast<int32_t>(target_slot[0] | (target_slot[1] << 16));
            frame.dex_pc = pc + rel;
            if (frame.dex_pc < pc) GuestYieldPoint();
            hit = true;
          }
        }
        if (hit) continue;
        break;  // 未命中走顺序下一条
      }

      // ---- cmp（23x：BB = ins[1]&0xff，CC = ins[1]>>8）----
      case dex::kCmplFloat: {
        float x = GetF(frame, ins[1] & 0xffu), y = GetF(frame, ins[1] >> 8);
        int32_t r = (std::isnan(x) || std::isnan(y)) ? -1 : (x < y ? -1 : (x == y ? 0 : 1));
        SetI(frame, aa, r);
        break;
      }
      case dex::kCmpgFloat: {
        float x = GetF(frame, ins[1] & 0xffu), y = GetF(frame, ins[1] >> 8);
        int32_t r = (std::isnan(x) || std::isnan(y)) ? 1 : (x < y ? -1 : (x == y ? 0 : 1));
        SetI(frame, aa, r);
        break;
      }
      case dex::kCmplDouble: {
        double x = GetD(frame, ins[1] & 0xffu), y = GetD(frame, ins[1] >> 8);
        int32_t r = (std::isnan(x) || std::isnan(y)) ? -1 : (x < y ? -1 : (x == y ? 0 : 1));
        SetI(frame, aa, r);
        break;
      }
      case dex::kCmpgDouble: {
        double x = GetD(frame, ins[1] & 0xffu), y = GetD(frame, ins[1] >> 8);
        int32_t r = (std::isnan(x) || std::isnan(y)) ? 1 : (x < y ? -1 : (x == y ? 0 : 1));
        SetI(frame, aa, r);
        break;
      }
      case dex::kCmpLong: {
        int64_t x = GetL(frame, ins[1] & 0xffu), y = GetL(frame, ins[1] >> 8);
        SetI(frame, aa, x < y ? -1 : (x == y ? 0 : 1));
        break;
      }

      // ---- if ----
      case dex::kIfEq:
      case dex::kIfNe:
      case dex::kIfLt:
      case dex::kIfGe:
      case dex::kIfGt:
      case dex::kIfLe: {
        int32_t x = GetI(frame, a4), y = GetI(frame, b4);
        bool taken = false;
        switch (op) {
          case dex::kIfEq: taken = x == y; break;
          case dex::kIfNe: taken = x != y; break;
          case dex::kIfLt: taken = x < y; break;
          case dex::kIfGe: taken = x >= y; break;
          case dex::kIfGt: taken = x > y; break;
          default: taken = x <= y; break;
        }
        if (taken) {
          frame.dex_pc = pc + Sgn16(ins[1]);
          if (frame.dex_pc < pc) GuestYieldPoint();  // 回边：多 GuestTask 并发推进
          continue;
        }
        break;
      }
      case dex::kIfEqz:
      case dex::kIfNez:
      case dex::kIfLtz:
      case dex::kIfGez:
      case dex::kIfGtz:
      case dex::kIfLez: {
        int32_t x = GetI(frame, aa);
        bool taken = false;
        switch (op) {
          case dex::kIfEqz: taken = x == 0; break;
          case dex::kIfNez: taken = x != 0; break;
          case dex::kIfLtz: taken = x < 0; break;
          case dex::kIfGez: taken = x >= 0; break;
          case dex::kIfGtz: taken = x > 0; break;
          default: taken = x <= 0; break;
        }
        if (taken) {
          frame.dex_pc = pc + Sgn16(ins[1]);
          if (frame.dex_pc < pc) GuestYieldPoint();  // 回边：多 GuestTask 并发推进
          continue;
        }
        break;
      }

      // ---- aget / aput ----
      case dex::kAget:
      case dex::kAgetWide:
      case dex::kAgetObject:
      case dex::kAgetBoolean:
      case dex::kAgetByte:
      case dex::kAgetChar:
      case dex::kAgetShort: {
        uint32_t src1 = ins[1] >> 8;   // CC
        uint32_t src2 = ins[1] & 0xffu;  // BB -> wait: 23x: BB = ins[1]&0xff, CC = ins[1]>>8
        ObjHeader* obj = GetRef(frame, src2);
        if (obj == nullptr) {
          ThrowNullPointerException(state, "aget on null array");
          continue;
        }
        if (!CheckRef(state, frame, obj, src2, "aget")) continue;
        VArray* arr = static_cast<VArray*>(obj);
        int32_t idx = GetI(frame, src1);
        if (idx < 0 || idx >= arr->length) {
          ThrowArrayIndexOutOfBoundsException(state, idx, arr->length);
          continue;
        }
        uint64_t slot = LoadSlot(arr->data() + static_cast<size_t>(idx) * 8);
        Narrow n = Narrow::kWord;
        switch (op) {
          case dex::kAgetWide: n = Narrow::kWide; break;
          case dex::kAgetObject: n = Narrow::kRef; break;
          case dex::kAgetBoolean: n = Narrow::kBool; break;
          case dex::kAgetByte: n = Narrow::kByte; break;
          case dex::kAgetChar: n = Narrow::kChar; break;
          case dex::kAgetShort: n = Narrow::kShort; break;
          default: break;
        }
        uint64_t elem = NarrowRead(n, slot);
        if (n == Narrow::kRef) {
          ObjHeader* o = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(elem));
          if (!CheckRef(state, frame, o, aa, "aget-object")) continue;
        }
        SetP(frame, aa, elem);
        break;
      }
      case dex::kAput:
      case dex::kAputWide:
      case dex::kAputObject:
      case dex::kAputBoolean:
      case dex::kAputByte:
      case dex::kAputChar:
      case dex::kAputShort: {
        uint32_t src1 = ins[1] >> 8;
        uint32_t src2 = ins[1] & 0xffu;
        ObjHeader* obj = GetRef(frame, src2);
        if (obj == nullptr) {
          ThrowNullPointerException(state, "aput on null array");
          continue;
        }
        if (!CheckRef(state, frame, obj, src2, "aput")) continue;
        VArray* arr = static_cast<VArray*>(obj);
        int32_t idx = GetI(frame, src1);
        if (idx < 0 || idx >= arr->length) {
          ThrowArrayIndexOutOfBoundsException(state, idx, arr->length);
          continue;
        }
        if (op == dex::kAputObject) {
          ObjHeader* val = GetRef(frame, aa);
          if (val != nullptr) {
            if (!CheckRef(state, frame, val, aa, "aput-object value")) continue;
            VClass* comp = arr->klass != nullptr ? arr->klass->component_type : nullptr;
            if (comp != nullptr && val->klass != nullptr && !comp->IsAssignableFrom(val->klass)) {
              state.ThrowNew("Ljava/lang/ArrayStoreException;",
                             (val->klass->descriptor + " into " + arr->klass->descriptor));
              continue;
            }
          }
        }
        Narrow n = Narrow::kWord;
        switch (op) {
          case dex::kAputWide: n = Narrow::kWide; break;
          case dex::kAputObject: n = Narrow::kRef; break;
          case dex::kAputBoolean: n = Narrow::kBool; break;
          case dex::kAputByte: n = Narrow::kByte; break;
          case dex::kAputChar: n = Narrow::kChar; break;
          case dex::kAputShort: n = Narrow::kShort; break;
          default: break;
        }
        StoreSlot(arr->data() + static_cast<size_t>(idx) * 8, NarrowWrite(n, GetP(frame, aa)));
        break;
      }

      // ---- iget / iput ----
      case dex::kIget:
      case dex::kIgetWide:
      case dex::kIgetObject:
      case dex::kIgetBoolean:
      case dex::kIgetByte:
      case dex::kIgetChar:
      case dex::kIgetShort:
      case dex::kIput:
      case dex::kIputWide:
      case dex::kIputObject:
      case dex::kIputBoolean:
      case dex::kIputByte:
      case dex::kIputChar:
      case dex::kIputShort: {
        bool is_get = op >= dex::kIget && op <= dex::kIgetShort;
        VField* f = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          f = state.linker->ResolveField(method->dex_file, ins[1]);
        }
        if (f == nullptr) {
          state.ThrowNew("Ljava/lang/NoSuchFieldError;", FieldRefStr(method->dex_file, ins[1]));
          continue;
        }
        ObjHeader* obj = GetRef(frame, b4);
        if (obj == nullptr) {
          // 字段坐标随消息上抛（诊断面：null receiver 的 igets 直接指到字段，
          // 构造器/回调停点无须再反查 dex_pc）。
          std::string npe = is_get ? "iget on null: " : "iput on null: ";
          if (f->declaring != nullptr) npe += f->declaring->descriptor;
          npe += "->" + f->name + " " + f->type_descriptor;
          ThrowNullPointerException(state, npe);
          continue;
        }
        if (!CheckRef(state, frame, obj, b4, is_get ? "iget" : "iput")) continue;
        uint8_t* slot_ptr = static_cast<VObject*>(obj)->field_data() + f->offset;
        Narrow n = Narrow::kWord;
        uint8_t kind = is_get ? static_cast<uint8_t>(op - dex::kIget)
                              : static_cast<uint8_t>(op - dex::kIput);
        switch (kind) {
          case 1: n = Narrow::kWide; break;
          case 2: n = Narrow::kRef; break;
          case 3: n = Narrow::kBool; break;
          case 4: n = Narrow::kByte; break;
          case 5: n = Narrow::kChar; break;
          case 6: n = Narrow::kShort; break;
          default: break;
        }
        if (is_get) {
          uint64_t bits = NarrowRead(n, LoadSlot(slot_ptr));
          if (n == Narrow::kRef) {
            // 字段引用读出即校验（脏槽位型诊断直接指向字段）。
            // 槽来源注记：declaring/name/type/@offset + receiver（死对象槽的
            // 直接证据面——reclaimed 形态 + 字段坐标即根因坐标）。
            ObjHeader* o = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(bits));
#if defined(__ANDROID__) && DEXVM_CALL_DIAGNOSTICS
            // TEMP DIAG: Jackson 反序列化注解内省器空值归因（事后移除）。
            if (f->declaring != nullptr &&
                ((f->name == "c" &&
                  (f->declaring->descriptor == "Lcom/fasterxml/jackson/databind/d0/c;" ||
                   f->declaring->descriptor == "Lcom/fasterxml/jackson/databind/d0/t;")) ||
                 (f->name == "l" &&
                  (f->declaring->descriptor == "Lcom/fasterxml/jackson/databind/b0/a;" ||
                   f->declaring->descriptor == "Lcom/fasterxml/jackson/databind/b0/h;")))) {
              __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "iget %s.%s -> 0x%llx",
                                  f->declaring->descriptor.c_str(), f->name.c_str(),
                                  static_cast<unsigned long long>(bits));
            }
#endif
            char note[192];
            std::snprintf(note, sizeof(note),
                          "slot=%s->%s:%s @%u recv=0x%016llx",
                          f->declaring != nullptr ? f->declaring->descriptor.c_str() : "?",
                          f->name.c_str(), f->type_descriptor.c_str(), f->offset,
                          static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(obj)));
            if (!CheckRef(state, frame, o, a4, "iget-object", note)) continue;
          }
          SetP(frame, a4, bits);
        } else {
#if defined(__ANDROID__)
          // TEMP DIAG（RecyclerView measure 断点归因）：LayoutManager.mRecyclerView
          // 的绑定轨迹（setLayoutManager 是否写入/是否被置 null）。
          if (n == Narrow::kRef && f->declaring != nullptr && f->name == "mLayout" &&
              f->declaring->descriptor == "Landroidx/recyclerview/widget/RecyclerView;") {
            static std::atomic<int> rv_layout_logs{0};
            if (rv_layout_logs.fetch_add(1, std::memory_order_relaxed) < 20) {
              __android_log_print(ANDROID_LOG_INFO, "DexVmRV",
                                  "iput mLayout rv=%p val=0x%llx from %s.%s",
                                  static_cast<void*>(reinterpret_cast<VObject*>(obj)),
                                  static_cast<unsigned long long>(GetP(frame, a4)),
                                  method->declaring != nullptr
                                      ? method->declaring->descriptor.c_str()
                                      : "?",
                                  method->name.c_str());
            }
          }
          if (n == Narrow::kRef && f->declaring != nullptr && f->name == "mRecyclerView" &&
              f->declaring->descriptor.find("RecyclerView$LayoutManager") != std::string::npos) {
            static std::atomic<int> rv_bind_logs{0};
            if (rv_bind_logs.fetch_add(1, std::memory_order_relaxed) < 20) {
              __android_log_print(ANDROID_LOG_INFO, "DexVmRV",
                                  "iput mRecyclerView recv=%p val=0x%llx off=%u from %s.%s",
                                  static_cast<void*>(reinterpret_cast<VObject*>(obj)),
                                  static_cast<unsigned long long>(GetP(frame, a4)), f->offset,
                                  method->declaring != nullptr
                                      ? method->declaring->descriptor.c_str()
                                      : "?",
                                  method->name.c_str());
            }
          }
#endif
          StoreSlot(slot_ptr, NarrowWrite(n, GetP(frame, a4)));
        }
        break;
      }

      // ---- sget / sput ----
      case dex::kSget:
      case dex::kSgetWide:
      case dex::kSgetObject:
      case dex::kSgetBoolean:
      case dex::kSgetByte:
      case dex::kSgetChar:
      case dex::kSgetShort:
      case dex::kSput:
      case dex::kSputWide:
      case dex::kSputObject:
      case dex::kSputBoolean:
      case dex::kSputByte:
      case dex::kSputChar:
      case dex::kSputShort: {
        bool is_get = op >= dex::kSget && op <= dex::kSgetShort;
        VField* f = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          f = state.linker->ResolveField(method->dex_file, ins[1]);
        }
        if (f == nullptr) {
          state.ThrowNew("Ljava/lang/NoSuchFieldError;", FieldRefStr(method->dex_file, ins[1]));
          continue;
        }
        const bool dbg_sget = f->declaring != nullptr &&
                              f->declaring->descriptor.find("US_ASCII") != std::string::npos;
#if defined(__ANDROID__)
        if (dbg_sget) {
          std::string fs;
          const GuestSchedOps* gs = GuestSched();
          void* mtask = gs != nullptr && gs->current_task != nullptr ? gs->current_task() : nullptr;
          for (size_t fi = 0; fi < state.frame_stack.size() && fi < 10; fi++) {
            Frame* fr = state.frame_stack[state.frame_stack.size() - 1 - fi];
            if (fr == nullptr || fr->method == nullptr) continue;
            if (mtask != nullptr && fr->owner != mtask) continue;
            fs += " <- ";
            if (fr->method->declaring != nullptr) fs += fr->method->declaring->descriptor;
            fs += "." + fr->method->name + "@" + std::to_string(fr->dex_pc);
          }
          __android_log_print(ANDROID_LOG_INFO, "DexVmSgetDbg",
                              "SGET %s.%s tid=%ld status=%d slot=%u storage=%zu frames:%s",
                              f->declaring->descriptor.c_str(), f->name.c_str(),
                              static_cast<long>(gettid()),
                              static_cast<int>(f->declaring->status), f->offset,
                              f->declaring->static_storage.size(), fs.c_str());
        }
#else
        (void)dbg_sget;
#endif
        if (!EnsureClassInitialized(state, f->declaring)) continue;
        VClass* decl = f->declaring;
        if (decl == nullptr || f->offset >= decl->static_storage.size()) {
          state.ThrowNew("Ljava/lang/NoSuchFieldError;",
                         FieldRefStr(method->dex_file, ins[1]) + " storage missing");
          continue;
        }
        Value& slot = decl->static_storage[f->offset];
        Narrow n = Narrow::kWord;
        uint8_t kind = is_get ? static_cast<uint8_t>(op - dex::kSget)
                              : static_cast<uint8_t>(op - dex::kSput);
        switch (kind) {
          case 1: n = Narrow::kWide; break;
          case 2: n = Narrow::kRef; break;
          case 3: n = Narrow::kBool; break;
          case 4: n = Narrow::kByte; break;
          case 5: n = Narrow::kChar; break;
          case 6: n = Narrow::kShort; break;
          default: break;
        }
        if (is_get) {
          uint64_t bits = NarrowRead(n, static_cast<uint64_t>(slot.l));
#if defined(__ANDROID__) && DEXVM_CALL_DIAGNOSTICS
          // TEMP DIAG: ObjectMapper 静态默认值（clinit 执行面；事后移除）。
          if (n == Narrow::kRef && f->declaring != nullptr &&
              f->declaring->descriptor == "Lcom/fasterxml/jackson/databind/ObjectMapper;" &&
              (f->name == "DEFAULT_ANNOTATION_INTROSPECTOR" || f->name == "DEFAULT_BASE")) {
            __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                                "sget ObjectMapper.%s -> 0x%llx (status=%d)",
                                f->name.c_str(), static_cast<unsigned long long>(bits),
                                static_cast<int>(f->declaring->status));
          }
#endif
          if (n == Narrow::kRef) {
            // 静态引用槽读出即校验（脏槽位型诊断直接指向字段）
            ObjHeader* o = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(bits));
            char note[192];
            std::snprintf(note, sizeof(note),
                          "static-slot=%s->%s:%s #%u",
                          decl != nullptr ? decl->descriptor.c_str() : "?", f->name.c_str(),
                          f->type_descriptor.c_str(), f->offset);
            if (!CheckRef(state, frame, o, aa, "sget-object", note)) continue;
          }
          SetP(frame, aa, bits);
        } else {
          slot.l = static_cast<int64_t>(NarrowWrite(n, GetP(frame, aa)));
        }
        break;
      }

      // ---- invoke ----
      case dex::kInvokeVirtual:
      case dex::kInvokeSuper:
      case dex::kInvokeDirect:
      case dex::kInvokeStatic:
      case dex::kInvokeInterface:
      case dex::kInvokeVirtualRange:
      case dex::kInvokeSuperRange:
      case dex::kInvokeDirectRange:
      case dex::kInvokeStaticRange:
      case dex::kInvokeInterfaceRange: {
        bool is_range = op >= dex::kInvokeVirtualRange;
        CallSite cs = is_range ? DecodeCallSite3rc(ins) : DecodeCallSite35c(ins);
        InvokeKind kind;
        if (op == dex::kInvokeVirtual || op == dex::kInvokeVirtualRange) {
          kind = InvokeKind::kVirtual;
        } else if (op == dex::kInvokeSuper || op == dex::kInvokeSuperRange) {
          kind = InvokeKind::kSuper;
        } else if (op == dex::kInvokeDirect || op == dex::kInvokeDirectRange) {
          kind = InvokeKind::kDirect;
        } else if (op == dex::kInvokeStatic || op == dex::kInvokeStaticRange) {
          kind = InvokeKind::kStatic;
        } else {
          kind = InvokeKind::kInterface;
        }
        VMethod* resolved = nullptr;
        if (state.linker != nullptr && method->dex_file != nullptr) {
          resolved = state.linker->ResolveMethod(method->dex_file, cs.idx);
        }
        if (resolved == nullptr && state.linker != nullptr && method->dex_file != nullptr) {
          // marker 形回落：d8 把 MethodHandle.invokeBasic/linkTo* 调用点脱糖为
          // 【精确 proto method id 的常规 invoke】（非 invoke-polymorphic opcode；
          // VarHandle accessor 才走 opcode 形）。method id 的 proto 即调用点 proto，
          // 按名回落到 DoPolymorphicCall（java.lang.invoke 三类上的未解析调用）。
          const dex::MethodId& rmid = method->dex_file->GetMethodId(cs.idx);
          std::string rdecl(method->dex_file->GetTypeDescriptor(rmid.class_idx));
          if (rdecl == "Ljava/lang/invoke/MethodHandle;" ||
              rdecl == "Ljava/lang/invoke/MethodHandleNatives;" ||
              rdecl == "Ljava/lang/invoke/VarHandle;") {
            Value call_result{};
            if (!DoPolymorphicCall(state, frame, cs, rmid.proto_idx, &call_result)) continue;
            pending_result = call_result;
            break;
          }
        }
        if (resolved == nullptr) {
          // ART 诊断语义（art_method.cc / interpreter_common）：区分【声明类解析/定义失败】
          // 与【类在但方法缺失】——前者 NoClassDefFoundError（类级失败），后者
          // NoSuchMethodError。此前一律报 NoSuchMethodError 会把类定义/链接失败误诊为方法缺失
          // （如 Math.<clinit> 依赖缺失导致类无法初始化的上游、DefineClass 中途失败留残骸）。
          if (state.linker != nullptr && method->dex_file != nullptr) {
            const dex::MethodId& rmid = method->dex_file->GetMethodId(cs.idx);
            VClass* rdecl = state.linker->ResolveType(method->dex_file, rmid.class_idx);
            if (rdecl == nullptr) {
              state.ThrowNew("Ljava/lang/NoClassDefFoundError;",
                             TypeRefStr(method->dex_file, rmid.class_idx) +
                                 " (define/link failed: " + state.linker->last_error() + ")");
            } else {
              state.ThrowNew("Ljava/lang/NoSuchMethodError;",
                             MethodRefStr(method->dex_file, cs.idx));
            }
          } else {
            state.ThrowNew("Ljava/lang/NoSuchMethodError;", MethodRefStr(method->dex_file, cs.idx));
          }
          continue;
        }
        if (kind == InvokeKind::kStatic && !EnsureClassInitialized(state, resolved->declaring)) {
          continue;
        }
        Value call_result{};
        if (!DoCall(state, frame, resolved, kind, cs, &call_result)) {
          // 异常驱动控制流：DoCall 失败必须挂 pending exception（否则本 pc 无前进
          // 无限重试——intrinsic 裸 *ok=false 挂死根因防御）。无异常按内部错误兜底。
          if (!state.HasPendingException()) {
            state.ThrowNew("Ljava/lang/InternalError;",
                           "call failed without exception: " + MethodSigStr(resolved));
          }
          continue;  // dex_pc 不前进
        }
        pending_result = call_result;
        break;
      }

      // ---- invoke-polymorphic：VarHandle accessor → guard_* 形 / MethodHandle
      //      marker → MemberName 目标派发（最小语义；见 DoPolymorphicCall 注释）----
      case dex::kInvokePolymorphic:
      case dex::kInvokePolymorphicRange: {
        bool is_range = op == dex::kInvokePolymorphicRange;
        CallSite cs = is_range ? DecodeCallSite3rc(ins) : DecodeCallSite35c(ins);
        Value call_result{};
        if (!DoPolymorphicCall(state, frame, cs, ins[3], &call_result)) continue;
        pending_result = call_result;
        break;
      }

      // ---- M2：invoke-custom / const-method-handle / const-method-type ----
      case dex::kInvokeCustom:
      case dex::kInvokeCustomRange:
      case dex::kConstMethodHandle:
      case dex::kConstMethodType:
        ThrowUnsupportedOperationException(state, "M2 opcode not supported");
        continue;

      // ---- unop / 转换 ----
      case dex::kNegInt:
        SetI(frame, a4, static_cast<int32_t>(0u - static_cast<uint32_t>(GetI(frame, b4))));
        break;
      case dex::kNotInt:
        SetI(frame, a4, ~GetI(frame, b4));
        break;
      case dex::kNegLong:
        SetL(frame, a4, static_cast<int64_t>(0ull - static_cast<uint64_t>(GetL(frame, b4))));
        break;
      case dex::kNotLong:
        SetL(frame, a4, ~GetL(frame, b4));
        break;
      case dex::kNegFloat:
        SetF(frame, a4, -GetF(frame, b4));
        break;
      case dex::kNegDouble:
        SetD(frame, a4, -GetD(frame, b4));
        break;
      case dex::kIntToLong:
        SetL(frame, a4, GetI(frame, b4));
        break;
      case dex::kIntToFloat:
        SetF(frame, a4, static_cast<float>(GetI(frame, b4)));
        break;
      case dex::kIntToDouble:
        SetD(frame, a4, static_cast<double>(GetI(frame, b4)));
        break;
      case dex::kLongToInt:
        SetI(frame, a4, static_cast<int32_t>(GetL(frame, b4)));
        break;
      case dex::kLongToFloat:
        SetF(frame, a4, static_cast<float>(GetL(frame, b4)));
        break;
      case dex::kLongToDouble:
        SetD(frame, a4, static_cast<double>(GetL(frame, b4)));
        break;
      case dex::kFloatToInt:
        SetI(frame, a4, NarrowFloatToInt(GetF(frame, b4)));
        break;
      case dex::kFloatToLong:
        SetL(frame, a4, NarrowFloatToLong(GetF(frame, b4)));
        break;
      case dex::kFloatToDouble:
        SetD(frame, a4, static_cast<double>(GetF(frame, b4)));
        break;
      case dex::kDoubleToInt:
        SetI(frame, a4, NarrowDoubleToInt(GetD(frame, b4)));
        break;
      case dex::kDoubleToLong:
        SetL(frame, a4, NarrowDoubleToLong(GetD(frame, b4)));
        break;
      case dex::kDoubleToFloat:
        SetF(frame, a4, static_cast<float>(GetD(frame, b4)));
        break;
      case dex::kIntToByte:
        SetI(frame, a4, Sgn8(GetI(frame, b4)));
        break;
      case dex::kIntToChar:
        SetI(frame, a4, GetI(frame, b4) & 0xffff);
        break;
      case dex::kIntToShort:
        SetI(frame, a4, Sgn16(GetI(frame, b4)));
        break;

      // ---- binop（23x）----
      case dex::kAddInt:
      case dex::kSubInt:
      case dex::kMulInt:
      case dex::kDivInt:
      case dex::kRemInt:
      case dex::kAndInt:
      case dex::kOrInt:
      case dex::kXorInt:
      case dex::kShlInt:
      case dex::kShrInt:
      case dex::kUshrInt: {
        uint32_t bb = ins[1] & 0xffu, cc = ins[1] >> 8;
        int32_t x = GetI(frame, bb), y = GetI(frame, cc), r = 0;
        switch (op) {
          case dex::kAddInt: r = static_cast<int32_t>(static_cast<uint32_t>(x) + static_cast<uint32_t>(y)); break;
          case dex::kSubInt: r = static_cast<int32_t>(static_cast<uint32_t>(x) - static_cast<uint32_t>(y)); break;
          case dex::kMulInt: r = static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y)); break;
          case dex::kDivInt: if (!IntDiv(state, x, y, &r)) continue; break;
          case dex::kRemInt: if (!IntRem(state, x, y, &r)) continue; break;
          case dex::kAndInt: r = x & y; break;
          case dex::kOrInt: r = x | y; break;
          case dex::kXorInt: r = x ^ y; break;
          case dex::kShlInt: r = static_cast<int32_t>(static_cast<uint32_t>(x) << (y & 31)); break;
          case dex::kShrInt: r = x >> (y & 31); break;
          default: r = static_cast<int32_t>(static_cast<uint32_t>(x) >> (y & 31)); break;
        }
        SetI(frame, aa, r);
        break;
      }
      case dex::kAddLong:
      case dex::kSubLong:
      case dex::kMulLong:
      case dex::kDivLong:
      case dex::kRemLong:
      case dex::kAndLong:
      case dex::kOrLong:
      case dex::kXorLong:
      case dex::kShlLong:
      case dex::kShrLong:
      case dex::kUshrLong: {
        uint32_t bb = ins[1] & 0xffu, cc = ins[1] >> 8;
        int64_t x = GetL(frame, bb), y = GetL(frame, cc), r = 0;
        switch (op) {
          case dex::kAddLong: r = static_cast<int64_t>(static_cast<uint64_t>(x) + static_cast<uint64_t>(y)); break;
          case dex::kSubLong: r = static_cast<int64_t>(static_cast<uint64_t>(x) - static_cast<uint64_t>(y)); break;
          case dex::kMulLong: r = static_cast<int64_t>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y)); break;
          case dex::kDivLong: if (!LongDiv(state, x, y, &r)) continue; break;
          case dex::kRemLong: if (!LongRem(state, x, y, &r)) continue; break;
          case dex::kAndLong: r = x & y; break;
          case dex::kOrLong: r = x | y; break;
          case dex::kXorLong: r = x ^ y; break;
          case dex::kShlLong: r = static_cast<int64_t>(static_cast<uint64_t>(x) << (y & 63)); break;
          case dex::kShrLong: r = x >> (y & 63); break;
          default: r = static_cast<int64_t>(static_cast<uint64_t>(x) >> (y & 63)); break;
        }
        SetL(frame, aa, r);
        break;
      }
      case dex::kAddFloat:
      case dex::kSubFloat:
      case dex::kMulFloat:
      case dex::kDivFloat:
      case dex::kRemFloat: {
        uint32_t bb = ins[1] & 0xffu, cc = ins[1] >> 8;
        float x = GetF(frame, bb), y = GetF(frame, cc), r = 0;
        switch (op) {
          case dex::kAddFloat: r = x + y; break;
          case dex::kSubFloat: r = x - y; break;
          case dex::kMulFloat: r = x * y; break;
          case dex::kDivFloat: r = x / y; break;
          default: r = std::fmod(x, y); break;
        }
        SetF(frame, aa, r);
        break;
      }
      case dex::kAddDouble:
      case dex::kSubDouble:
      case dex::kMulDouble:
      case dex::kDivDouble:
      case dex::kRemDouble: {
        uint32_t bb = ins[1] & 0xffu, cc = ins[1] >> 8;
        double x = GetD(frame, bb), y = GetD(frame, cc), r = 0;
        switch (op) {
          case dex::kAddDouble: r = x + y; break;
          case dex::kSubDouble: r = x - y; break;
          case dex::kMulDouble: r = x * y; break;
          case dex::kDivDouble: r = x / y; break;
          default: r = std::fmod(x, y); break;
        }
        SetD(frame, aa, r);
        break;
      }

      // ---- binop/2addr（12x：A=dest/src1，B=src2）----
      case dex::kAddInt2addr:
      case dex::kSubInt2addr:
      case dex::kMulInt2addr:
      case dex::kDivInt2addr:
      case dex::kRemInt2addr:
      case dex::kAndInt2addr:
      case dex::kOrInt2addr:
      case dex::kXorInt2addr:
      case dex::kShlInt2addr:
      case dex::kShrInt2addr:
      case dex::kUshrInt2addr: {
        int32_t x = GetI(frame, a4), y = GetI(frame, b4), r = 0;
        switch (op) {
          case dex::kAddInt2addr: r = static_cast<int32_t>(static_cast<uint32_t>(x) + static_cast<uint32_t>(y)); break;
          case dex::kSubInt2addr: r = static_cast<int32_t>(static_cast<uint32_t>(x) - static_cast<uint32_t>(y)); break;
          case dex::kMulInt2addr: r = static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y)); break;
          case dex::kDivInt2addr: if (!IntDiv(state, x, y, &r)) continue; break;
          case dex::kRemInt2addr: if (!IntRem(state, x, y, &r)) continue; break;
          case dex::kAndInt2addr: r = x & y; break;
          case dex::kOrInt2addr: r = x | y; break;
          case dex::kXorInt2addr: r = x ^ y; break;
          case dex::kShlInt2addr: r = static_cast<int32_t>(static_cast<uint32_t>(x) << (y & 31)); break;
          case dex::kShrInt2addr: r = x >> (y & 31); break;
          default: r = static_cast<int32_t>(static_cast<uint32_t>(x) >> (y & 31)); break;
        }
        SetI(frame, a4, r);
        break;
      }
      case dex::kAddLong2addr:
      case dex::kSubLong2addr:
      case dex::kMulLong2addr:
      case dex::kDivLong2addr:
      case dex::kRemLong2addr:
      case dex::kAndLong2addr:
      case dex::kOrLong2addr:
      case dex::kXorLong2addr:
      case dex::kShlLong2addr:
      case dex::kShrLong2addr:
      case dex::kUshrLong2addr: {
        int64_t x = GetL(frame, a4), y = GetL(frame, b4), r = 0;
        switch (op) {
          case dex::kAddLong2addr: r = static_cast<int64_t>(static_cast<uint64_t>(x) + static_cast<uint64_t>(y)); break;
          case dex::kSubLong2addr: r = static_cast<int64_t>(static_cast<uint64_t>(x) - static_cast<uint64_t>(y)); break;
          case dex::kMulLong2addr: r = static_cast<int64_t>(static_cast<uint64_t>(x) * static_cast<uint64_t>(y)); break;
          case dex::kDivLong2addr: if (!LongDiv(state, x, y, &r)) continue; break;
          case dex::kRemLong2addr: if (!LongRem(state, x, y, &r)) continue; break;
          case dex::kAndLong2addr: r = x & y; break;
          case dex::kOrLong2addr: r = x | y; break;
          case dex::kXorLong2addr: r = x ^ y; break;
          case dex::kShlLong2addr: r = static_cast<int64_t>(static_cast<uint64_t>(x) << (y & 63)); break;
          case dex::kShrLong2addr: r = x >> (y & 63); break;
          default: r = static_cast<int64_t>(static_cast<uint64_t>(x) >> (y & 63)); break;
        }
        SetL(frame, a4, r);
        break;
      }
      case dex::kAddFloat2addr:
      case dex::kSubFloat2addr:
      case dex::kMulFloat2addr:
      case dex::kDivFloat2addr:
      case dex::kRemFloat2addr: {
        float x = GetF(frame, a4), y = GetF(frame, b4), r = 0;
        switch (op) {
          case dex::kAddFloat2addr: r = x + y; break;
          case dex::kSubFloat2addr: r = x - y; break;
          case dex::kMulFloat2addr: r = x * y; break;
          case dex::kDivFloat2addr: r = x / y; break;
          default: r = std::fmod(x, y); break;
        }
        SetF(frame, a4, r);
        break;
      }
      case dex::kAddDouble2addr:
      case dex::kSubDouble2addr:
      case dex::kMulDouble2addr:
      case dex::kDivDouble2addr:
      case dex::kRemDouble2addr: {
        double x = GetD(frame, a4), y = GetD(frame, b4), r = 0;
        switch (op) {
          case dex::kAddDouble2addr: r = x + y; break;
          case dex::kSubDouble2addr: r = x - y; break;
          case dex::kMulDouble2addr: r = x * y; break;
          case dex::kDivDouble2addr: r = x / y; break;
          default: r = std::fmod(x, y); break;
        }
        SetD(frame, a4, r);
        break;
      }

      // ---- binop/lit16（22s）----
      case dex::kAddIntLit16:
      case dex::kRsubInt:
      case dex::kMulIntLit16:
      case dex::kDivIntLit16:
      case dex::kRemIntLit16:
      case dex::kAndIntLit16:
      case dex::kOrIntLit16:
      case dex::kXorIntLit16: {
        int32_t x = GetI(frame, b4), y = Sgn16(ins[1]), r = 0;
        switch (op) {
          case dex::kAddIntLit16: r = static_cast<int32_t>(static_cast<uint32_t>(x) + static_cast<uint32_t>(y)); break;
          case dex::kRsubInt: r = static_cast<int32_t>(static_cast<uint32_t>(y) - static_cast<uint32_t>(x)); break;
          case dex::kMulIntLit16: r = static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y)); break;
          case dex::kDivIntLit16: if (!IntDiv(state, x, y, &r)) continue; break;
          case dex::kRemIntLit16: if (!IntRem(state, x, y, &r)) continue; break;
          case dex::kAndIntLit16: r = x & y; break;
          case dex::kOrIntLit16: r = x | y; break;
          default: r = x ^ y; break;
        }
        SetI(frame, a4, r);
        break;
      }

      // ---- binop/lit8（22b）----
      case dex::kAddIntLit8:
      case dex::kRsubIntLit8:
      case dex::kMulIntLit8:
      case dex::kDivIntLit8:
      case dex::kRemIntLit8:
      case dex::kAndIntLit8:
      case dex::kOrIntLit8:
      case dex::kXorIntLit8:
      case dex::kShlIntLit8:
      case dex::kShrIntLit8:
      case dex::kUshrIntLit8: {
        int32_t x = GetI(frame, ins[1] & 0xffu), y = Sgn8(ins[1] >> 8), r = 0;
        switch (op) {
          case dex::kAddIntLit8: r = static_cast<int32_t>(static_cast<uint32_t>(x) + static_cast<uint32_t>(y)); break;
          case dex::kRsubIntLit8: r = static_cast<int32_t>(static_cast<uint32_t>(y) - static_cast<uint32_t>(x)); break;
          case dex::kMulIntLit8: r = static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y)); break;
          case dex::kDivIntLit8: if (!IntDiv(state, x, y, &r)) continue; break;
          case dex::kRemIntLit8: if (!IntRem(state, x, y, &r)) continue; break;
          case dex::kAndIntLit8: r = x & y; break;
          case dex::kOrIntLit8: r = x | y; break;
          case dex::kXorIntLit8: r = x ^ y; break;
          case dex::kShlIntLit8: r = static_cast<int32_t>(static_cast<uint32_t>(x) << (y & 31)); break;
          case dex::kShrIntLit8: r = x >> (y & 31); break;
          default: r = static_cast<int32_t>(static_cast<uint32_t>(x) >> (y & 31)); break;
        }
        SetI(frame, aa, r);
        break;
      }

      default:
        // 未使用/非法 opcode（0x3E-0x43/0x73/0x79-0x7A/0xE3-0xF9 等）
        ThrowUnsupportedOperationException(state, "illegal opcode");
        continue;
    }
    frame.dex_pc = next_pc;
  }
}

}  // namespace dexvm::runtime
