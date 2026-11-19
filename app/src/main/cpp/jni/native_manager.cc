// Guest Native Library 管理 + native 登记/调用实现
//（docs 07 章 16.x；对照 java_vm_ext.cc:952-1170、jni_internal.cc:2701-2830）
#include "jni/native_manager.h"

#include <dlfcn.h>

#include <deque>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <type_traits>
#include <utility>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "core/class_linker.h"
#include "core/exception.h"
#include "core/interpreter.h"
#include "jni/jni_env.h"

#ifndef DEXVM_CALL_DIAGNOSTICS
#define DEXVM_CALL_DIAGNOSTICS 0
#endif

namespace dexvm::jni {

// ---- 调用期诊断（TEMP DIAG；默认构建空实现）----
// 只对加固 native（com/bangcle/andjni）开启，避免 PAG/MMKV 等高频 native 把
// 预算耗尽；budget 上限兜底 logcat 体量。
#if defined(__ANDROID__) && DEXVM_CALL_DIAGNOSTICS
namespace {
thread_local int g_native_diag_depth = 0;
thread_local std::string g_native_diag_owner;
thread_local std::vector<std::string> g_native_any_stack;
thread_local std::deque<std::string> g_native_ring;
}  // namespace

bool NativeDiagBegin(runtime::VMethod* m) {
  if (m != nullptr && m->declaring != nullptr) {
    g_native_any_stack.push_back(m->declaring->descriptor + "." + m->name);
  }
  if (m == nullptr || m->declaring == nullptr) return false;
  if (m->declaring->descriptor.rfind("Lcom/bangcle/andjni/", 0) != 0) return false;
  ++g_native_diag_depth;
  if (g_native_diag_depth == 1) {
    g_native_diag_owner = m->declaring->descriptor + "." + m->name;
    static std::atomic<int> begin_logs{0};
    if (begin_logs.fetch_add(1) < 16) {
      Dl_info di{};
      const char* fname = "?";
      unsigned long long foff = 0;
      if (m->jni_native_fn != nullptr && dladdr(m->jni_native_fn, &di) != 0 &&
          di.dli_fbase != nullptr) {
        if (di.dli_fname != nullptr) fname = di.dli_fname;
        foff = static_cast<unsigned long long>(
            reinterpret_cast<uintptr_t>(m->jni_native_fn) -
            reinterpret_cast<uintptr_t>(di.dli_fbase));
      }
      __android_log_print(ANDROID_LOG_INFO, "DexVmNativeJni",
                          "enter %s.%s fn=%p %s+0x%llx", m->declaring->descriptor.c_str(),
                          m->name.c_str(), m->jni_native_fn, fname, foff);
    }
  }
  return true;
}

void NativeDiagEnd(bool entered) {
  if (!g_native_any_stack.empty()) g_native_any_stack.pop_back();
  if (!entered) return;
  if (g_native_diag_depth > 0) {
    --g_native_diag_depth;
    if (g_native_diag_depth == 0) g_native_diag_owner.clear();
  }
}

bool NativeDiagActive() { return g_native_diag_depth > 0; }

int NativeDiagDepth() { return g_native_diag_depth; }

void NativeDiagRecord(const std::string& entry) {
  if (g_native_ring.size() >= 600) g_native_ring.pop_front();
  g_native_ring.push_back(entry);
}

void NativeDiagDumpRing(const char* why) {
  static std::atomic<int> dumps{0};
  if (dumps.fetch_add(1) >= 4) return;
  __android_log_print(ANDROID_LOG_INFO, "DexVmNativeJni", "ring dump (%s) entries=%zu", why,
                      g_native_ring.size());
  size_t i = 0;
  for (const auto& e : g_native_ring) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmRing", "%03zu %s", i++, e.c_str());
  }
}

std::string NativeDiagCurrentNative() {
  return g_native_any_stack.empty() ? std::string() : g_native_any_stack.back();
}

void NativeDiagLog(const char* fn, const std::string& detail) {
  if (!NativeDiagActive()) return;
  NativeDiagRecord(std::string(fn) + " " + detail);
}
#else
bool NativeDiagBegin(runtime::VMethod*) { return false; }
void NativeDiagEnd(bool) {}
std::string NativeDiagCurrentNative() { return std::string(); }
int NativeDiagDepth() { return 0; }
void NativeDiagRecord(const std::string&) {}
void NativeDiagDumpRing(const char*) {}
bool NativeDiagActive() { return false; }
void NativeDiagLog(const char*, const std::string&) {}
#endif

namespace {

using dexvm::runtime::ClassStatus;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;

inline JNIEnvExt* Ext(JNIEnv* env) { return static_cast<JNIEnvExt*>(env); }

std::string MethodSignature(const VMethod& m) {
  std::string s = "(";
  for (const auto& p : m.param_types) s += p;
  s += ")";
  s += m.return_type;
  return s;
}

// dex ACC_NATIVE（0x100）
inline bool IsNativeMethod(const VMethod& m) { return (m.access_flags & 0x100u) != 0; }

// 单类内查找（name+sig 精确匹配；static/instance 不区分——Java 禁止同签名异 static）
VMethod* FindMatch(VClass* klass, const std::string& name, const std::string& sig,
                   bool native_only) {
  if (klass == nullptr) return nullptr;
  for (std::vector<VMethod>* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name != name || MethodSignature(m) != sig) continue;
      if (native_only && !IsNativeMethod(m)) continue;
      return &m;
    }
  }
  return nullptr;
}

// JNI_OnLoad/OnUnload 版本校验（java_vm_ext.cc IsSupportedJniVersion）
bool IsSupportedJniVersion(jint version) {
  return version == JNI_VERSION_1_1 || version == JNI_VERSION_1_2 || version == JNI_VERSION_1_4 ||
         version == JNI_VERSION_1_6 || version == JNI_VERSION_1_8;
}

// ---- 形状调用（native trampoline ABI 面，docs 16.6）----
// 参数按类重排：GPR 类（int/long/引用）按声明序在前、FPR 类（float/double）按声明序
// 在后。SysV x86_64 与 AAPCS64 均按类分配寄存器，重排后每个参数恰好落入被调方
//（定型原型）期望的寄存器；env/self 固占前两个 GPR。
// 限制：GPR 形参 ≤6（env/self 另计，寄存器区 x0..x7），超出部分按 AAPCS64
// 放栈（aarch64 asm 桩支持 8 个栈槽）；FPR 参数 ≤8（v0..v7，超出仍后置）。
// 返回值按类取寄存器：int/long/引用 rax/x0、float s0/xmm0 低 32 位、double d0/xmm0。
#if defined(__aarch64__)
// ABI 硬墙（jni/native_shield.S）：不可信 packer native 实测返回时改写调用者
// 被调用者保存寄存器（-O2 帧指针/TLS 缓存 → stack canary 误报）。aarch64 上
// native 一律经汇编桩调用：寄存器契约由桩保存/恢复，clobber_mask 记录违规项
//（bit0..9 = x19..x28，bit10 = x29），供 trampoline 诊断行归因。
struct DexvmJniShieldFrame {
  void* fn;               // 0x00
  uint64_t gpr[8];        // 0x08 x0..x7
  uint64_t fpr[8];        // 0x48 d0..d7
  uint64_t ret_x;         // 0x88
  uint64_t ret_d;         // 0x90
  uint64_t clobber_mask;  // 0x98
  uint64_t tp_before;     // 0xa0
  uint64_t tp_after;      // 0xa8
  // AAPCS64 栈传参（第 9 个及之后的整型字；实测 CloudIDHelper.encrypt
  // 7 个 String 参数 = env+self+7 GPR 字，超出寄存器区）。asm 在 blr 前把
  // 这些槽铺到 [sp]（被调方栈参起址），保存区上移 0x40。
  uint64_t stack_args[8]; // 0xb0
  uint64_t stack_count;   // 0xf0
};
static_assert(sizeof(DexvmJniShieldFrame) == 0xf8, "shield frame size");
static_assert(offsetof(DexvmJniShieldFrame, fpr) == 0x48, "shield frame fpr offset");
static_assert(offsetof(DexvmJniShieldFrame, clobber_mask) == 0x98, "shield frame mask offset");
static_assert(offsetof(DexvmJniShieldFrame, tp_after) == 0xa8, "shield frame tp offset");
static_assert(offsetof(DexvmJniShieldFrame, stack_args) == 0xb0, "shield frame stack offset");
static_assert(offsetof(DexvmJniShieldFrame, stack_count) == 0xf0, "shield frame count offset");
extern "C" void dexvm_jni_shield_call(DexvmJniShieldFrame* frame);

struct ShieldReport {
  void* fn = nullptr;
  uint64_t mask = 0;
  uint64_t tp_before = 0;
  uint64_t tp_after = 0;
};
thread_local ShieldReport g_last_shield_report;
#endif

template <typename Ret, size_t... Is, size_t... Js>
Ret CallShaped(void* fn, JNIEnv* env, void* self, const int64_t* g, const double* f,
               const uint64_t* stack, size_t nstack, std::index_sequence<Is...>,
               std::index_sequence<Js...>) {
#if defined(__aarch64__)
  DexvmJniShieldFrame frame{};
  frame.fn = fn;
  frame.gpr[0] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(env));
  frame.gpr[1] = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(self));
  ((frame.gpr[Is + 2] = static_cast<uint64_t>(g[Is + 2])), ...);
  ((std::memcpy(&frame.fpr[Js], &f[Js], sizeof(double))), ...);
  for (size_t i = 0; i < nstack && i < 8; i++) frame.stack_args[i] = stack[i];
  frame.stack_count = nstack;
  dexvm_jni_shield_call(&frame);
  g_last_shield_report.fn = fn;
  g_last_shield_report.mask = frame.clobber_mask;
  g_last_shield_report.tp_before = frame.tp_before;
  g_last_shield_report.tp_after = frame.tp_after;
  if constexpr (std::is_void_v<Ret>) {
    return;
  } else if constexpr (std::is_floating_point_v<Ret> && sizeof(Ret) == 4) {
    uint32_t bits = static_cast<uint32_t>(frame.ret_d);
    jfloat v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
  } else if constexpr (std::is_floating_point_v<Ret>) {
    jdouble v;
    std::memcpy(&v, &frame.ret_d, sizeof(v));
    return v;
  } else {
    return static_cast<Ret>(frame.ret_x);
  }
#else
  (void)stack;
  (void)nstack;
  using FnT = Ret (*)(JNIEnv*, void*, decltype((void)Is, int64_t{})...,
                      decltype((void)Js, double{})...);
  // g 前两槽是 env/self（已单独传入）；参数槽从 g[2] 起
  return reinterpret_cast<FnT>(fn)(env, self, g[Is + 2]..., f[Js]...);
#endif
}

template <typename Ret>
Ret CallGrid(void* fn, JNIEnv* env, void* self, const int64_t* g, int ng, const double* f, int nf,
             const uint64_t* stack, size_t nstack) {
  // g 含 env/self 前两槽；ng/nf 为纯参数数（GPR ≤6、FPR ≤8）；stack 为
  // AAPCS64 栈传参（按声明序的第 7 个及之后整型字）。
  switch (ng * 16 + nf) {
#define CELL(ni, nf_)                                                                 \
  case (ni) * 16 + (nf_):                                                             \
    return CallShaped<Ret>(fn, env, self, g, f, stack, nstack,                        \
                           std::make_index_sequence<(ni)>{},                          \
                           std::make_index_sequence<(nf_)>{});                        \
    break;
#define ROW9(ni)                                                                      \
  CELL(ni, 0)                                                                         \
  CELL(ni, 1)                                                                         \
  CELL(ni, 2)                                                                         \
  CELL(ni, 3)                                                                         \
  CELL(ni, 4)                                                                         \
  CELL(ni, 5)                                                                         \
  CELL(ni, 6)                                                                         \
  CELL(ni, 7)                                                                         \
  CELL(ni, 8)
    ROW9(0)
    ROW9(1)
    ROW9(2)
    ROW9(3)
    ROW9(4)
    ROW9(5)
    ROW9(6)
#undef ROW9
#undef CELL
    default:
      break;
  }
  // 参数过多（>6 GPR / >8 FPR）：M3 汇编 trampoline 后置
  if constexpr (!std::is_void_v<Ret>) {
    return Ret();
  }
}

// ---- VMState -> JNIEnvExt 登记（解释器 hook 面）----
std::map<VMState*, JNIEnvExt*>& EnvRegistry() {
  static std::map<VMState*, JNIEnvExt*> registry;
  return registry;
}

JNIEnvExt* FindEnvForState(VMState* state) {
  auto it = EnvRegistry().find(state);
  return it == EnvRegistry().end() ? nullptr : it->second;
}

}  // namespace

// =====================================================================
// RegisterNatives / UnregisterNatives（jni_internal.cc:2701-2830）
// =====================================================================

int32_t RegisterNativesImpl(JNIEnvExt* env, VClass* klass, const JNINativeMethod* methods,
                            int32_t method_count) {
  if (method_count < 0) {
    env->Warn("RegisterNatives: negative method count: " + std::to_string(method_count));
    return JNI_ERR;
  }
  if (klass == nullptr) {
    env->Warn("RegisterNatives: null class");
    return JNI_ERR;
  }
  if (method_count == 0) {
    // jni_internal.cc:2716-2719：warning + JNI_OK
    env->Warn("RegisterNativeMethods: attempt to register 0 native methods for " +
              klass->descriptor);
    return JNI_OK;
  }
  if (methods == nullptr) {
    env->Warn("RegisterNatives: null methods array");
    return JNI_ERR;
  }
  VMState* st = env->vmstate;
  for (int32_t i = 0; i < method_count; i++) {
    const char* name = methods[i].name;
    const char* sig = methods[i].signature;
    const void* fnPtr = methods[i].fnPtr;
    if (name == nullptr || sig == nullptr || fnPtr == nullptr) {
      // ART：ReportInvalidJNINativeMethod -> JniAbortF（abort）；M2 记 WARNING + JNI_ERR
      env->Warn("RegisterNatives: invalid JNINativeMethod triple at index " + std::to_string(i));
      return JNI_ERR;
    }
    bool is_fast = false;
    // '!' 前缀 = FastNative（jni_internal.cc:2775-2783）：签名剥 '!'，标记 FastNative
    if (*sig == '!') {
      is_fast = true;
      ++sig;
    }
    // 沿继承链逐类：先 native 同名同签名，再任意同名同签名（jni_internal.cc:2795-2815）
    VMethod* m = nullptr;
    for (VClass* cur = klass; cur != nullptr && m == nullptr; cur = cur->super_class) {
      m = FindMatch(cur, name, sig, /*native_only=*/true);
      if (m == nullptr) m = FindMatch(cur, name, sig, /*native_only=*/false);
    }
    if (m == nullptr) {
      if (st != nullptr) {
        st->ThrowNew("Ljava/lang/NoSuchMethodError;",
                     std::string(name) + sig + " static or non-static");
      }
      return JNI_ERR;
    }
    if (!IsNativeMethod(*m)) {
      // 找到非 native：NoSuchMethodError（jni_internal.cc:2824-2830）
      if (st != nullptr) {
        st->ThrowNew("Ljava/lang/NoSuchMethodError;", std::string(name) + sig + " native");
      }
      return JNI_ERR;
    }
    if (is_fast) m->access_flags |= kAccFastNative;
    // SetEntryPointFromJni 等价（ClassLinker::RegisterNative）：
    // @CriticalNative 且类未 visibly initialized -> 延迟设置（docs 16.3）
    if ((m->access_flags & kAccCriticalNative) != 0 && m->declaring != nullptr &&
        m->declaring->status != ClassStatus::kInitialized) {
      if (env->vm != nullptr) env->vm->natives.deferred.push_back({m, fnPtr});
      continue;
    }
    m->jni_native_fn = const_cast<void*>(fnPtr);
  }
  return JNI_OK;
}

int32_t UnregisterNativesImpl(JNIEnvExt* env, VClass* klass) {
  if (klass == nullptr) {
    env->Warn("UnregisterNatives: null class");
    return JNI_ERR;
  }
  // docs 16.8：卸载不清 RegisterNatives 项；显式 UnregisterNatives 清 declared native
  for (std::vector<VMethod>* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.native_fn != nullptr) m.native_fn = nullptr;
      if (m.jni_native_fn != nullptr) m.jni_native_fn = nullptr;
    }
  }
  if (env->vm != nullptr) {
    auto& def = env->vm->natives.deferred;
    for (auto it = def.begin(); it != def.end();) {
      if (it->method != nullptr && it->method->declaring == klass) {
        it = def.erase(it);
      } else {
        ++it;
      }
    }
  }
  return JNI_OK;
}

void FlushDeferredNativeRegistrations(NativeManager* natives) {
  if (natives == nullptr) return;
  for (auto it = natives->deferred.begin(); it != natives->deferred.end();) {
    VMethod* m = it->method;
    if (m == nullptr || m->declaring == nullptr ||
        m->declaring->status == ClassStatus::kInitialized) {
      if (m != nullptr) m->jni_native_fn = const_cast<void*>(it->fn);
      it = natives->deferred.erase(it);
    } else {
      ++it;
    }
  }
}

// =====================================================================
// JNI 导出符号回落（art_jni_dlsym_lookup 等价；JNI 规范 §Resolving
// Native Method Names 两级命名）====
// =====================================================================

namespace {

// 名称转义（JNI 规范）：'.'/'/' -> '_'（类名包分隔）、'_' -> "_1"、
// '$' -> "_0"、';' -> "_2"、'[' -> "_3"（非 ASCII _0xxxx 后置）。
std::string MangleForJni(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    switch (c) {
      case '.':
      case '/':
        out += '_';
        break;
      case '_':
        out += "_1";
        break;
      case '$':
        out += "_0";
        break;
      case ';':
        out += "_2";
        break;
      case '[':
        out += "_3";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

// "Lcom/bangcle/CryptoTool;" -> "com_bangcle_CryptoTool"
std::string JniClassName(const std::string& descriptor) {
  std::string s = descriptor;
  if (!s.empty() && s.front() == 'L') s.erase(s.begin());
  if (!s.empty() && s.back() == ';') s.pop_back();
  return MangleForJni(s);
}

}  // namespace

bool ResolveNativeBySymbol(NativeManager* natives, VMethod* method) {
  if (natives == nullptr || method == nullptr) return false;
  if (method->jni_native_fn != nullptr) return true;
  std::string cls =
      method->declaring != nullptr ? JniClassName(method->declaring->descriptor) : std::string();
  std::string name = MangleForJni(method->name);
  std::string short_name = "Java_" + cls + "_" + name;
  // 签名长名（重载区分；仅参数表转义，返回型不参与）
  std::string long_name = short_name + "__";
  for (const auto& p : method->param_types) long_name += MangleForJni(p);
  for (const auto& lib : natives->libraries()) {
    if (lib == nullptr || lib->handle == nullptr) continue;
    for (const std::string& sym : {short_name, long_name}) {
      void* fn = ::dlsym(lib->handle, sym.c_str());
      if (fn != nullptr) {
        method->jni_native_fn = fn;
        return true;
      }
    }
  }
  return false;
}

// =====================================================================
// native trampoline（SetEntryPointFromJni 等价物的调用桩；docs 16.6）
// =====================================================================

bool InvokeNativeTrampoline(JNIEnvExt* env, VMethod* m, ObjHeader* self, const Value* args,
                            size_t arg_count, Value* result) {
  Value local_result{};
  if (result == nullptr) result = &local_result;
  result->l = 0;
  if (m == nullptr) {
    if (env->vmstate != nullptr) {
      env->vmstate->ThrowNew("Ljava/lang/NoSuchMethodError;", "null methodID");
    }
    return false;
  }
  // PAG（libpag）原生面暂时不执行：其渲染依赖真实 Surface/GL（Surface 桥
  // 未落地前传空面会在 libpag 内部 mutex 踩垃圾指针 SIGSEGV，08:42 实证）。
  // 但直接抛 UnsatisfiedLinkError 会让 PAGFile.<clinit>/PAGFile.LoadFromAssets
  // 打断 RecyclerView/Fragment 的整条 measure（基础框架 UI 永远不收敛）。
  // 因此改为零值兼容桩：返回默认 0/null/false，PAGView/PAGPlayer 能构造、
  // PAGFile.LoadFromAssets 得到空文件（动画不播）；宿主 Surface/GL 桥落地后
  // 用真实桥替换本桩。
  if (m->declaring != nullptr &&
      m->declaring->descriptor.rfind("Lorg/libpag/", 0) == 0) {
    static std::atomic<int> pag_stub_logs{0};
#if defined(__ANDROID__)
    if (pag_stub_logs.fetch_add(1) < 30) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmJni",
                          "PAG stub -> %s.%s", m->declaring->descriptor.c_str(),
                          m->name.c_str());
    }
#else
    pag_stub_logs.fetch_add(1);
#endif
    return true;
  }
  if (m->jni_native_fn == nullptr && env->vm != nullptr) {
    // @CriticalNative 延迟项兜底：类已初始化则此处生效
    FlushDeferredNativeRegistrations(&env->vm->natives);
    // JNI 导出符号回落（art_jni_dlsym_lookup 等价）：未 RegisterNatives
    // 绑定的 native 按 Java_* 导出符号在已加载库中查找绑定
    if (m->jni_native_fn == nullptr) ResolveNativeBySymbol(&env->vm->natives, m);
  }
  if (m->jni_native_fn == nullptr) {
    if (env->vmstate != nullptr) {
      env->vmstate->ThrowNew("Ljava/lang/UnsatisfiedLinkError;",
                             m->declaring != nullptr ? m->declaring->descriptor + "." + m->name
                                                     : m->name);
    }
    return false;
  }
  VMState* st = env->vmstate;

  // generic JNI stub：PushLocalFrame -> native -> PopLocalFrame（docs 16.6）
  NativeCallScope scope(env);

  jobject self_ref = nullptr;
  if (m->IsStatic()) {
    self_ref = EncodeJObject(env, reinterpret_cast<ObjHeader*>(m->declaring));
  } else {
    if (self == nullptr) {
      if (st != nullptr) dexvm::runtime::ThrowNullPointerException(*st, "JNI native call on null");
      return false;
    }
    self_ref = EncodeJObject(env, self);
  }

  // 铺参：GPR 类（含 env/self）在前、FPR 类在后。寄存器区 = env/self + 6 个
  // GPR 字；超出的 GPR 字按声明序走 AAPCS64 栈传参（实测 CloudIDHelper.encrypt
  // 7 个 String = env+self+7，第 7 个参数必须上栈；旧实现直接失败且不挂异常，
  // 解释器兜底 InternalError → cloudid 账户请求中止、feed 无账户）。
  // 注意：必须先判界再写（第 9 个 GPR 会先越界写一个槽——-O2 栈布局下直接踩
  // stack canary，SIGABRT "stack corruption detected"）。
  int64_t g[8];
  double fp[8];
  uint64_t stack_args[8];
  size_t nstack = 0;
  int ng = 2;
  int nf = 0;
  bool arg_overflow = false;
  g[0] = static_cast<int64_t>(reinterpret_cast<intptr_t>(static_cast<JNIEnv*>(env)));
  g[1] = static_cast<int64_t>(reinterpret_cast<intptr_t>(self_ref));
  auto push_g = [&](int64_t bits) {
    if (ng < 8) {
      g[ng++] = bits;
      return;
    }
    if (nstack < 8) {
      stack_args[nstack++] = static_cast<uint64_t>(bits);
      return;
    }
    arg_overflow = true;
  };
  auto push_f = [&](double d) {
    if (nf >= 8) {
      arg_overflow = true;
      return;
    }
    fp[nf++] = d;
  };
  size_t ai = 0;
  for (const auto& p : m->param_types) {
    if (ai >= arg_count) break;
    const Value& v = args[ai++];
    switch (p[0]) {
      case 'F': {
        uint64_t u = static_cast<uint32_t>(v.l);  // float 位型在低 32
        double d;
        std::memcpy(&d, &u, sizeof(d));  // 低 32 位 = float 位型（FPR 低半寄存器）
        push_f(d);
        break;
      }
      case 'D': {
        double d;
        std::memcpy(&d, &v.l, sizeof(d));
        push_f(d);
        break;
      }
      case 'J':
        push_g(v.l);
        break;
      case 'L':
      case '[':
        push_g(static_cast<int64_t>(reinterpret_cast<intptr_t>(EncodeJObject(env, v.ref))));
        break;
      default:  // Z B C S I
        push_g(static_cast<int64_t>(v.i));
        break;
    }
  }
  if (arg_overflow) {
    env->Warn("native trampoline: too many args (>6 GPR regs + 8 stack slots / >8 FPR)");
    return false;
  }
  const int gparams = ng - 2;  // 寄存器形状参数（排除 env/self）

  void* fn = m->jni_native_fn;
  Value res{};
  const char rtype = m->return_type.empty() ? 'V' : m->return_type[0];
  const bool diag_entered = NativeDiagBegin(m);
  switch (rtype) {
    case 'V':
      CallGrid<void>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      break;
    case 'F': {
      jfloat r = CallGrid<jfloat>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      uint32_t bits = 0;
      std::memcpy(&bits, &r, sizeof(bits));
      res.l = bits;
      break;
    }
    case 'D': {
      jdouble r = CallGrid<jdouble>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      uint64_t bits = 0;
      std::memcpy(&bits, &r, sizeof(bits));
      res.l = static_cast<int64_t>(bits);
      break;
    }
    case 'L':
    case '[': {
      int64_t r = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      jobject h = reinterpret_cast<jobject>(static_cast<uintptr_t>(r));
      // native 返回 local ref：PopLocalFrame 前解码为托管对象（docs 15.16.1）。
      // 陈旧解码必须失败（handle_table 严格 live+generation 校验）：native
      // 跨帧缓存旧句柄再返回 = 过期解码，得 null（不残留旧对象指针）。
      res.ref = DecodeJObject(env, h);
      if (h != nullptr && res.ref == nullptr) {
        env->Warn("native trampoline: native returned stale/invalid reference");
      }
      break;
    }
    case 'Z': {
      int64_t r = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      res.l = (r & 0xff) != 0 ? 1 : 0;
      break;
    }
    case 'B': {
      int64_t r = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      res.l = static_cast<int64_t>(static_cast<int8_t>(r & 0xff));
      break;
    }
    case 'C': {
      int64_t r = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      res.l = static_cast<int64_t>(static_cast<uint16_t>(r & 0xffff));
      break;
    }
    case 'S': {
      int64_t r = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      res.l = static_cast<int64_t>(static_cast<int16_t>(r & 0xffff));
      break;
    }
    case 'I': {
      int64_t r = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      res.l = static_cast<int64_t>(static_cast<int32_t>(r));
      break;
    }
    default: {  // 'J'
      res.l = CallGrid<int64_t>(fn, env, self_ref, g, gparams, fp, nf, stack_args, nstack);
      break;
    }
  }
  NativeDiagEnd(diag_entered);
#if defined(__aarch64__)
  // 诊断（临时）：ABI 硬墙报告本笔 native 调用违规改写了哪些被调用者保存
  // 寄存器；tp 变化 = 线程 TLS 迁移（另一类问题）。
  if (g_last_shield_report.mask != 0 ||
      g_last_shield_report.tp_before != g_last_shield_report.tp_after) {
    Dl_info di{};
    const char* fname = "?";
    unsigned long long foff = 0;
    if (fn != nullptr && dladdr(fn, &di) != 0 && di.dli_fbase != nullptr) {
      if (di.dli_fname != nullptr) fname = di.dli_fname;
      foff = static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(fn) -
                                             reinterpret_cast<uintptr_t>(di.dli_fbase));
    }
    __android_log_print(ANDROID_LOG_ERROR, "DexVmJni",
                        "ABIVIOL %s.%s fn=%p %s+0x%llx mask=0x%llx tp=%llx->%llx",
                        m->declaring != nullptr ? m->declaring->descriptor.c_str() : "?",
                        m->name.c_str(), fn, fname, foff,
                        (unsigned long long)g_last_shield_report.mask,
                        (unsigned long long)g_last_shield_report.tp_before,
                        (unsigned long long)g_last_shield_report.tp_after);
  }
#endif
  *result = res;
  // JNI 异常传播（JNI 规范：native 以 ThrowNew 挂起异常后返回，异常须在
  // 调用点抛出——guest catch 处理同 try 块内；否则异常会被推迟到调用者返回，
  // 期间的字节码在半异常态继续执行）。
  if (st != nullptr && st->HasPendingException()) return false;
  return true;
}

bool InvokeNativeTrampoline(VMState* state, VMethod* method, const Value* args, size_t arg_count,
                            Value* result) {
  if (state == nullptr || method == nullptr) return false;
  JNIEnvExt* env = FindEnvForState(state);
  if (env == nullptr) {
    state->ThrowNew("Ljava/lang/InternalError;", "no JNIEnvExt for VMState");
    return false;
  }
  ObjHeader* self = nullptr;
  const Value* params = args;
  size_t nparams = arg_count;
  if (!method->IsStatic() && arg_count > 0) {
    self = args[0].ref;  // InvokeMethod 约定：args[0] = receiver
    params = args + 1;
    nparams = arg_count - 1;
  }
  return InvokeNativeTrampoline(env, method, self, params, nparams, result);
}

void RegisterEnvForState(VMState* state, JNIEnvExt* env) {
  if (state != nullptr) EnvRegistry()[state] = env;
}

// =====================================================================
// SharedLibrary / LoadNativeLibrary / UnloadNativeLibrary
//（java_vm_ext.cc:952-1170；docs 16.2/16.8）
// =====================================================================

SharedLibrary* NativeManager::Find(const std::string& path) {
  for (auto& lib : libraries_) {
    if (lib->path == path) return lib.get();
  }
  return nullptr;
}

bool NativeManager::LoadNativeLibrary(JNIEnvExt* env, const std::string& path,
                                      VObject* class_loader, VClass* caller_class,
                                      std::string* error_msg) {
  std::string local_error;
  if (error_msg == nullptr) error_msg = &local_error;
  error_msg->clear();
  (void)caller_class;  // M2：caller_location 仅诊断用，后置

  // allocator 身份（java_vm_ext.cc:984-998）：M2 以 loader 指针为准
  void* allocator = class_loader;

  // 已加载：同 allocator 幂等 / 冲突拒绝（java_vm_ext.cc:999-1048）
  if (SharedLibrary* library = Find(path)) {
    if (library->class_loader_allocator != allocator) {
      char buf[160];
      std::snprintf(buf, sizeof(buf),
                    "Shared library \"%s\" already opened by ClassLoader %p; can't open in "
                    "ClassLoader %p",
                    path.c_str(), library->class_loader, class_loader);
      *error_msg = buf;
      env->Warn(*error_msg);
      return false;
    }
    if (library->on_load_called && !library->on_load_ok) {
      *error_msg = "JNI_OnLoad failed on a previous attempt to load \"" + path + "\"";
      return false;
    }
    return true;  // 幂等成功
  }

  // dlopen（android::OpenNativeLibrary 等价：POSIX 通用；docs 16.2 map/重定位交链接器）
  void* handle = ::dlopen(path.c_str(), RTLD_NOW);
  if (handle == nullptr) {
    const char* err = ::dlerror();
    *error_msg = std::string("dlopen failed: ") + (err != nullptr ? err : path);
    return false;
  }

  auto entry = std::make_unique<SharedLibrary>();
  entry->path = path;
  entry->handle = handle;
  entry->class_loader = class_loader;  // weak 语义（M2：显式卸载）
  entry->class_loader_allocator = allocator;
  libraries_.push_back(std::move(entry));
  SharedLibrary* library = libraries_.back().get();

  // JNI_OnLoad（java_vm_ext.cc:1124-1170）
  void* sym = ::dlsym(handle, "JNI_OnLoad");
  if (sym == nullptr) {
    // 无 JNI_OnLoad：成功（java_vm_ext.cc:1128-1130）
    library->on_load_called = true;
    library->on_load_ok = true;
    return true;
  }

  // SetClassLoaderOverride(class_loader) 包裹（jni_internal.cc:393-420 GetClassLoader：
  // nativeLoad 期间 FindClass 用 override loader）
  VObject* old_override = env->class_loader;
  env->class_loader = class_loader;

  using JNI_OnLoadFn = jint(JNICALL*)(JavaVM*, void*);
  jint version = reinterpret_cast<JNI_OnLoadFn>(sym)(reinterpret_cast<JavaVM*>(env->vm), nullptr);

  env->class_loader = old_override;

  bool ok = true;
  if (version == JNI_ERR) {
    *error_msg = "JNI_ERR returned from JNI_OnLoad in \"" + path + "\"";
    ok = false;
  } else if (!IsSupportedJniVersion(version)) {
    *error_msg = "Bad JNI version returned from JNI_OnLoad in \"" + path + "\": " +
                 std::to_string(version);
    ok = false;
  }
  library->on_load_called = true;
  library->on_load_version = version;
  library->on_load_ok = ok;
  return ok;
}

bool NativeManager::UnloadNativeLibrary(JNIEnvExt* env, const std::string& path,
                                        std::string* error_msg) {
  std::string local_error;
  if (error_msg == nullptr) error_msg = &local_error;
  error_msg->clear();

  for (auto it = libraries_.begin(); it != libraries_.end(); ++it) {
    SharedLibrary* library = it->get();
    if (library->path != path) continue;

    if (library->handle != nullptr) {
      // JNI_OnUnload（docs 16.8：Find JNI_OnUnload -> call -> delete -> dlclose）
      void* sym = ::dlsym(library->handle, "JNI_OnUnload");
      if (sym != nullptr) {
        VObject* old_override = env->class_loader;
        env->class_loader = library->class_loader;
        using JNI_OnUnloadFn = void(JNICALL*)(JavaVM*, void*);
        reinterpret_cast<JNI_OnUnloadFn>(sym)(reinterpret_cast<JavaVM*>(env->vm), nullptr);
        env->class_loader = old_override;
      }
      ::dlclose(library->handle);
      library->handle = nullptr;
    }
    // 不清理 RegisterNatives 注册项（docs 16.8：dlclose 后函数指针由加载器语义兜底）
    libraries_.erase(it);
    return true;
  }
  *error_msg = "library not loaded: " + path;
  return false;
}

}  // namespace dexvm::jni
