// 异常：VMState::ThrowNew/Throw、常用异常辅助、try/handler 解析（FindCatchBlock）
// 语义对照：
//   - art_method.cc:328-365  FindCatchBlock（typed 顺序 -> catch-all）
//   - dex_file_exception_helpers.cc  CatchHandlerIterator（sleb128 size 解析）
//   - dex_file-inl.h:270-275/554-558  GetTryItems/GetCatchHandlerData 布局
#include "core/exception.h"

#include <cstring>
#include <string>
#include <unordered_set>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "dex/leb128.h"

namespace dexvm::runtime {

namespace {

// 内存不足时兜底异常对象（保证 pending 一定非空、可安全解引用）
VObject* AllocBareException(VMState& state) {
  if (state.heap != nullptr) {
    void* mem = state.heap->Alloc(sizeof(VObject));
    if (mem != nullptr) {
      VObject* obj = new (mem) VObject();
      obj->klass = nullptr;
      return obj;
    }
  }
  static VObject fallback{};
  fallback.klass = nullptr;
  return &fallback;
}

// 当前抛点所属 GuestTask（frame_stack 为多任务共享活跃帧集；纤维换出使
// back() 可能属于其它任务——owner 必须取调度器当前任务，不能用 back()->owner，
// 否则诊断栈会挂到别人身上（跨任务误归因实锤））。
void* CurrentThrowOwner(VMState& state) {
  const GuestSchedOps* sched = GuestSched();
  void* owner = sched != nullptr ? sched->current_task() : nullptr;
  if (owner == nullptr && !state.frame_stack.empty() && state.frame_stack.back() != nullptr) {
    owner = state.frame_stack.back()->owner;
  }
  return owner;
}

// 抛出瞬间的 guest 栈快照（fillInStackTrace 语义诊断面；innermost 先）。
// "Lcls;.name(params)ret@dex_pc"；上限 32 帧防诊断自身开销失控。
void CaptureThrowBacktrace(VMState& state) {
  state.last_throw_backtrace.clear();
  if (state.frame_stack.empty()) return;
  // 只取当前任务的帧，逆扫得到 innermost 先；不跟 caller 链（caller 可能跨
  // task 或指向已回收帧，跟链曾 SIGSEGV）。
  void* owner = CurrentThrowOwner(state);
  // 宿主上下文（非 guest task，owner=nullptr）无法区分「合法宿主帧」与
  // 已退役 fiber 残留帧——其 VMethod 可能已随类元数据释放，解引用即 SEGV
  // （fault @ region 末页实证）。诊断栈在此上下文直接放弃，保证 VM 不崩。
  if (owner == nullptr) return;
  for (auto it = state.frame_stack.rbegin(); it != state.frame_stack.rend(); ++it) {
    if (state.last_throw_backtrace.size() >= 32) break;
    const Frame* f = *it;
    if (f == nullptr || f->method == nullptr) continue;
    if (f->owner != owner) continue;
    const VMethod* m = f->method;
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
    s += "@" + std::to_string(f->dex_pc);
    state.last_throw_backtrace.push_back(std::move(s));
  }
}

// 静默异常探针：app 常把 NoSuchMethod/NoClassDef 等吞在 try/catch 里继续跑，
// 首现即丢失归因。按 descriptor+message 去重打一条（含抛点栈），tag=DexVmThrow。
bool ShouldProbeThrow(std::string_view desc) {
  return desc == "Ljava/lang/NoSuchMethodError;" || desc == "Ljava/lang/NoSuchFieldError;" ||
         desc == "Ljava/lang/NoClassDefFoundError;" ||
         desc == "Ljava/lang/NullPointerException;" || desc == "Ljava/lang/ClassCastException;" ||
         desc == "Ljava/lang/IncompatibleClassChangeError;" ||
         desc == "Ljava/lang/IllegalAccessError;" || desc == "Ljava/lang/VerifyError;" ||
         // 网络/TLS 面（OkHttp 把 IOException 吞进 onFailure；无探针则调用失败
         // 只剩 callFailed 事件、根因丢失）
         desc == "Ljava/io/IOException;" || desc == "Ljava/io/InterruptedIOException;" ||
         // 流面细分（gzip 资产/响应体解码失败的根因常在此）
         desc == "Ljava/io/EOFException;" || desc == "Ljava/util/zip/ZipException;" ||
         desc == "Ljava/util/zip/DataFormatException;" ||
         desc == "Ljava/lang/NegativeArraySizeException;" ||
         desc == "Ljava/net/SocketTimeoutException;" ||
         desc == "Ljava/net/SocketException;" || desc == "Ljava/net/ConnectException;" ||
         desc == "Ljava/net/UnknownHostException;" || desc == "Ljava/net/ProtocolException;" ||
         desc == "Ljava/lang/IllegalStateException;" || desc == "Ljava/lang/IllegalArgumentException;" ||
         desc == "Ljavax/net/ssl/SSLException;" ||
         desc == "Ljavax/net/ssl/SSLHandshakeException;" ||
         desc == "Ljavax/net/ssl/SSLPeerUnverifiedException;" ||
         // 服务发现面（ServiceLoader 失败会静默导致整条 provider 链丢失）
         desc == "Ljava/util/ServiceConfigurationError;" ||
         desc == "Ljava/security/NoSuchAlgorithmException;" ||
         desc == "Ljava/security/GeneralSecurityException;";
}

void ProbeThrow(VMState& state, std::string_view desc, std::string_view msg) {
#if defined(__ANDROID__)
  // 抛点指令坐标（method + dex_pc + opcode）：区分 guest athrow 与 VM 指令路径。
  // 按当前任务取 innermost 帧（intrinsic 内抛异常时无新 guest 帧，top 即调用方
  // invoke 指令；跨任务帧不得参与归因）。
  void* owner = CurrentThrowOwner(state);
  const Frame* top = nullptr;
  for (auto it = state.frame_stack.rbegin(); it != state.frame_stack.rend(); ++it) {
    if (*it != nullptr && (*it)->method != nullptr && (*it)->owner == owner) {
      top = *it;
      break;
    }
  }
  if (top == nullptr && !state.frame_stack.empty()) top = state.frame_stack.back();
  const VMethod* top_m = top != nullptr ? top->method : nullptr;
  const std::string top_desc =
      top_m != nullptr && top_m->declaring != nullptr ? top_m->declaring->descriptor : "";
  // 网络上下文（okhttp/okio 帧上抛出的任何异常）一律探针：OkHttp 把请求失败
  // 的根因吞进 onFailure / exchangeDoneDueToException，异常类+消息是唯一线索。
  // TEMP DIAG: 账户存储面（storeAccount 把异常吞进 catch 日志；按抛出时的调用
  // 栈判定上下文——throw 点可能在 Jackson/序列化内部，不只看 top 帧。事后移除）。
  bool acct_ctx =
      top_desc.find("app/accounts/") != std::string::npos ||
      top_desc.find("app/util/rj/") != std::string::npos;
  if (!acct_ctx) {
    for (const auto& f : state.last_throw_backtrace) {
      if (f.find("app/accounts/") != std::string::npos ||
          f.find("app/util/rj/") != std::string::npos ||
          f.find("api/util/t;") != std::string::npos) {
        acct_ctx = true;
        break;
      }
    }
  }
  const bool net_ctx =
      top_desc.find("okhttp3/") != std::string::npos ||
      top_desc.find("okhttp/") != std::string::npos ||
      top_desc.find("okio/") != std::string::npos ||
      // TEMP DIAG: cloudid 账户链（udid/guest 账户创建失败根因；事后移除）
      top_desc.find("cloudid/") != std::string::npos ||
      // TEMP DIAG: Jackson 反序列化根因（UDIDGuestInfo 解析失败面；事后移除）
      top_desc.find("fasterxml/jackson") != std::string::npos ||
      acct_ctx;
  if (!ShouldProbeThrow(desc) && !net_ctx) return;
  static std::unordered_set<std::string> seen;
  // 网络上下文按抛点方法区分（同一异常类+消息可能来自不同失败面）
  std::string key = std::string(desc) + ":" + std::string(msg);
  if (net_ctx && top_m != nullptr) {
    key += "@";
    key += top_desc;
    key += ".";
    key += top_m->name;
  }
  if (!seen.insert(key).second || seen.size() > 600) return;
  __android_log_print(ANDROID_LOG_INFO, "DexVmThrow", "%s: %s", desc.data(),
                      std::string(msg).c_str());
  if (top != nullptr && top_m != nullptr && top_m->code != nullptr &&
      top->dex_pc < top_m->code->insns_size) {
    uint8_t op = static_cast<uint8_t>(top->insns[top->dex_pc] & 0xFF);
    __android_log_print(ANDROID_LOG_INFO, "DexVmThrow", "  top %s.%s pc=%u op=0x%02x",
                        top_m->declaring != nullptr ? top_m->declaring->descriptor.c_str() : "?",
                        top_m->name.c_str(), top->dex_pc, op);
  }
  for (size_t i = 0; i < state.last_throw_backtrace.size() && i < 12; i++) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmThrow", "  at %s",
                        state.last_throw_backtrace[i].c_str());
  }
#else
  (void)state;
  (void)desc;
  (void)msg;
#endif
}

// Throwable.detailMessage 字段查找（沿继承链）
VField* FindDetailMessage(VClass* klass) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == "detailMessage") return &f;
    }
  }
  return nullptr;
}

void SetDetailMessage(VMState& state, ObjHeader* exception, std::string_view msg) {
  if (exception == nullptr || msg.empty() || exception->klass == nullptr) return;
  VField* f = FindDetailMessage(exception->klass);
  if (f == nullptr || state.linker == nullptr) return;
  std::u16string chars;
  chars.reserve(msg.size());
  for (char c : msg) chars.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));
  VString* s = state.linker->AllocString(chars);
  if (s == nullptr) return;
  VObject* obj = static_cast<VObject*>(exception);
  ObjHeader** slot =
      reinterpret_cast<ObjHeader**>(obj->field_data() + f->offset);
  *slot = reinterpret_cast<ObjHeader*>(s);
}

// 异常对象 detailMessage → UTF-8（诊断面；非 ASCII 字符以 '?' 粗投影）。
// guest 侧 new IOException(msg) 后 athrow 的路径只有拿到消息才能归因。
std::string ExceptionMessage(ObjHeader* exception) {
  if (exception == nullptr || exception->klass == nullptr) return {};
  VField* f = FindDetailMessage(exception->klass);
  if (f == nullptr) return {};
  VObject* obj = static_cast<VObject*>(exception);
  ObjHeader* s = nullptr;
  std::memcpy(&s, obj->field_data() + f->offset, sizeof(s));
  if (s == nullptr || s->klass == nullptr ||
      s->klass->descriptor != "Ljava/lang/String;") {
    return {};
  }
  VString* str = reinterpret_cast<VString*>(s);
  std::string out;
  out.reserve(static_cast<size_t>(str->length));
  for (int32_t i = 0; i < str->length; i++) {
    char16_t c = str->chars()[i];
    out.push_back(c <= 0x7F ? static_cast<char>(c) : '?');
  }
  return out;
}

// try_item 布局：start_addr u4 + insn_count u2 + handler_off u2（8 字节）
struct TryItemRaw {
  uint32_t start_addr;
  uint16_t insn_count;
  uint16_t handler_off;
};

TryItemRaw ReadTryItem(const uint8_t* p) {
  TryItemRaw t;
  std::memcpy(&t, p, sizeof(t));
  return t;
}

const uint8_t* Align4(const uint8_t* p) {
  uintptr_t a = reinterpret_cast<uintptr_t>(p);
  return reinterpret_cast<const uint8_t*>((a + 3u) & ~static_cast<uintptr_t>(3u));
}

}  // namespace

// ---- VMState ----

void VMState::Throw(ObjHeader* exception) {
  CaptureThrowBacktrace(*this);
  if (exception != nullptr && exception->klass != nullptr) {
    ProbeThrow(*this, exception->klass->descriptor, ExceptionMessage(exception));
  }
  pending_exception = exception;
}

void VMState::ThrowNew(std::string_view descriptor, std::string_view msg) {
  CaptureThrowBacktrace(*this);
  ProbeThrow(*this, descriptor, msg);
  ObjHeader* ex = nullptr;
  if (linker != nullptr) {
    VClass* klass = linker->FindClass(descriptor);
    if (klass != nullptr) {
      ex = reinterpret_cast<ObjHeader*>(linker->AllocObject(klass));
      SetDetailMessage(*this, ex, msg);
    }
  }
  if (ex == nullptr) {
    // 类缺失/分配失败：挂裸对象兜底（保证 pending 非空且可安全解引用）
    ex = reinterpret_cast<ObjHeader*>(AllocBareException(*this));
    SetDetailMessage(*this, ex, msg);
  }
  pending_exception = ex;
}

// ---- 常用异常辅助 ----

void ThrowNullPointerException(VMState& state, std::string_view msg) {
  state.ThrowNew("Ljava/lang/NullPointerException;", msg);
}

void ThrowIllegalArgumentException(VMState& state, std::string_view msg) {
  state.ThrowNew("Ljava/lang/IllegalArgumentException;", msg);
}

void ThrowArrayIndexOutOfBoundsException(VMState& state, int32_t index, int32_t length) {
  std::string msg = "index=" + std::to_string(index) + ", length=" + std::to_string(length);
  state.ThrowNew("Ljava/lang/ArrayIndexOutOfBoundsException;", msg);
}

void ThrowClassCastException(VMState& state, std::string_view from_desc, std::string_view to_desc) {
  std::string msg = std::string(from_desc) + " cannot be cast to " + std::string(to_desc);
  state.ThrowNew("Ljava/lang/ClassCastException;", msg);
}

void ThrowOutOfMemoryError(VMState& state, std::string_view msg) {
  state.ThrowNew("Ljava/lang/OutOfMemoryError;", msg);
}

void ThrowArithmeticException(VMState& state, std::string_view msg) {
  state.ThrowNew("Ljava/lang/ArithmeticException;", msg);
}

void ThrowUnsupportedOperationException(VMState& state, std::string_view msg) {
  state.ThrowNew("Ljava/lang/UnsupportedOperationException;", msg);
}

// ---- FindCatchBlock ----

bool FindCatchBlock(VMState& state, const Frame& frame, uint32_t dex_pc, uint32_t* handler_pc) {
  ObjHeader* exception = state.pending_exception;
  const VMethod* method = frame.method;
  const dex::CodeItem* code = (method != nullptr) ? method->code : nullptr;
  if (code == nullptr || exception == nullptr || code->tries_size == 0) return false;

  // try_item 数组位置（对照 GetTryItems = RoundUp(insns_end, 4)）：
  // dex_file 非空时优先用 tries_off（dex_file.cc：文件绝对偏移）；
  // 无 dex 的合成 CodeItem 回退 canonical 布局（insns 末尾 4 对齐）。
  const uint8_t* try_base = nullptr;
  if (method->dex_file != nullptr && code->tries_off != 0) {
    try_base = method->dex_file->data() + code->tries_off;
  } else {
    try_base = Align4(reinterpret_cast<const uint8_t*>(code->insns) +
                      static_cast<size_t>(code->insns_size) * 2u);
  }
  // encoded_catch_handler_list 起点 = try_item 数组末尾 4 对齐
  const uint8_t* handlers_base =
      Align4(try_base + static_cast<size_t>(code->tries_size) * 8u);

  // 找覆盖 dex_pc 的 try_item（tries 按 start_addr 升序，重叠时取最内层=最后一个命中）
  bool found = false;
  TryItemRaw try_item{};
  for (uint32_t i = 0; i < code->tries_size; i++) {
    TryItemRaw t = ReadTryItem(try_base + static_cast<size_t>(i) * 8u);
    if (t.start_addr <= dex_pc && dex_pc < t.start_addr + t.insn_count) {
      try_item = t;
      found = true;
    }
  }
  if (!found) return false;

  // encoded_catch_handler 解析（dex_file_exception_helpers.cc Init/Next）：
  //   sleb128 size（负 = 存在 catch-all，size 取反为 typed 数）
  //   (uleb128 type_idx, uleb128 addr) * typed 数
  //   可选 uleb128 catch_all_addr
  const uint8_t* p = handlers_base + try_item.handler_off;
  const uint8_t* end = p + 65536;  // 防御性上界（handler 体积极小）
  int32_t size = 0;
  if (!dex::ReadSleb128(&p, end, &size)) return false;
  bool catch_all = size <= 0;
  uint32_t typed_count = catch_all ? static_cast<uint32_t>(-size) : static_cast<uint32_t>(size);

  VClass* exception_klass = exception->klass;
  for (uint32_t i = 0; i < typed_count; i++) {
    uint32_t type_idx = 0;
    uint32_t addr = 0;
    if (!dex::ReadUleb128(&p, end, &type_idx) || !dex::ReadUleb128(&p, end, &addr)) return false;
    VClass* catch_klass = nullptr;
    if (method->dex_file != nullptr && state.linker != nullptr) {
      catch_klass = state.linker->ResolveType(method->dex_file, type_idx);
    }
    if (catch_klass == nullptr) {
      // 对照 FindCatchBlock：未解析的异常类跳过（解析期异常忽略、原异常保留）
      state.pending_exception = exception;
      continue;
    }
    if (exception_klass != nullptr && catch_klass->IsAssignableFrom(exception_klass)) {
      state.pending_exception = exception;
      *handler_pc = addr;
      return true;
    }
    state.pending_exception = exception;
  }
  if (catch_all) {
    uint32_t addr = 0;
    if (!dex::ReadUleb128(&p, end, &addr)) return false;
    state.pending_exception = exception;
    *handler_pc = addr;
    return true;
  }
  state.pending_exception = exception;
  return false;
}

}  // namespace dexvm::runtime
