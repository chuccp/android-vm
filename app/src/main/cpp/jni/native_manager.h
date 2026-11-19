// Guest Native Library 管理 + native 方法登记/调用（docs 07 章 16.x；
// 对照 java_vm_ext.cc:952-1170 LoadNativeLibrary、jni_internal.cc:2701-2830 RegisterNatives）
//
// 职责：
//   - SharedLibrary 登记（path、dlopen handle、weak class_loader、JNI_OnLoad state）
//   - Runtime.nativeLoad 等价入口 LoadNativeLibrary（dlopen -> JNI_OnLoad ->
//     SetClassLoaderOverride 包裹 -> 版本校验 -> SetResult；同 path/allocator 幂等、
//     冲突拒绝；M2 单线程：pending JNI_OnLoad 直接执行，不走条件等待）
//   - JNI_OnUnload/dlclose 卸载路径（docs 16.8：不清理 RegisterNatives 项）
//   - RegisterNatives/UnregisterNatives（'!' FastNative、@CriticalNative 延迟设置）
//   - native trampoline（generic JNI stub 调用面：SetEntryPointFromJni 等价，
//     VMethod::native_fn 登记后的 ABI 调用桩）
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/vm_types.h"

namespace dexvm::runtime {
struct VMState;
}  // namespace dexvm::runtime

// RegisterNatives 三元组（定义见 jni/jni_env.h，真实 jni.h 同型；此处仅前置声明）
struct JNINativeMethod;

namespace dexvm::jni {

struct JNIEnvExt;

// VMethod.access_flags 内部标记（annotations 解析前由 VM/测试设置；M3 由
// dex 注解 @FastNative/@CriticalNative 解析写入）：
//   '!' 前缀签名（jni_internal.cc RegisterNatives）等价 @FastNative
inline constexpr uint32_t kAccFastNative = 0x40000;
inline constexpr uint32_t kAccCriticalNative = 0x80000;

// ---- SharedLibrary（docs 16.2）----
struct SharedLibrary {
  std::string path;
  void* handle = nullptr;                    // dlopen handle
  runtime::VObject* class_loader = nullptr;  // weak（M2：显式卸载，无 GC 联动）
  void* class_loader_allocator = nullptr;    // M2：loader 指针身份（allocator 语义）
  bool on_load_called = false;
  bool on_load_ok = false;
  int32_t on_load_version = 0;
};

class NativeManager {
 public:
  // Runtime.nativeLoad(path, class_loader, caller_class) 等价入口。
  // class_loader/caller_class 为已解码对象（nullptr = boot）。
  // 成功返回 true；失败返回 false 并写 error_msg（java_vm_ext.cc 语义）。
  bool LoadNativeLibrary(JNIEnvExt* env, const std::string& path,
                         runtime::VObject* class_loader, runtime::VClass* caller_class,
                         std::string* error_msg);

  // JNI_OnUnload + dlclose（docs 16.8）
  bool UnloadNativeLibrary(JNIEnvExt* env, const std::string& path, std::string* error_msg);

  SharedLibrary* Find(const std::string& path);
  const std::vector<std::unique_ptr<SharedLibrary>>& libraries() const { return libraries_; }

  // @CriticalNative 延迟设置缓冲（docs 16.3：类未 visibly initialized 时延迟）
  struct DeferredNative {
    runtime::VMethod* method = nullptr;
    const void* fn = nullptr;
  };
  std::vector<DeferredNative> deferred;

 private:
  std::vector<std::unique_ptr<SharedLibrary>> libraries_;
};

// ---- RegisterNatives / UnregisterNatives（jni_internal.cc:2701-2830）----
// 返回 JNI_OK / JNI_ERR。失败按 ART 抛 NoSuchMethodError（非 native 目标/未命中）；
// 三元组非法（name/sig/fnPtr 缺失、count<0）ART 为 JniAbortF，M2 记 WARNING + JNI_ERR。
int32_t RegisterNativesImpl(JNIEnvExt* env, runtime::VClass* klass, const JNINativeMethod* methods,
                            int32_t method_count);
int32_t UnregisterNativesImpl(JNIEnvExt* env, runtime::VClass* klass);

// @CriticalNative 延迟项应用点：类初始化完成后调用（hook：
// core/interpreter.cc InvokeMethod 派发 + ClassLinker::InitializeClass 收尾）。
void FlushDeferredNativeRegistrations(NativeManager* natives);

// JNI 导出符号回落（art_jni_dlsym_lookup 等价；JNI 规范两级命名）：
// native 方法未 RegisterNatives 绑定时，按 Java_<类>_<方法>[__<签名>] 导出
// 符号在已加载库（LoadNativeLibrary 登记集）中 dlsym 查找并绑定
// m->jni_native_fn（SetEntryPointFromJni 等价）。短名先试、签名长名后试；
// 名称转义按 JNI 规范（'_'->_1、'$'->_0、';'->_2、'['->_3、'.'/'/'->_）。
// 命中并绑定返回 true。
bool ResolveNativeBySymbol(NativeManager* natives, runtime::VMethod* method);

// ---- native trampoline（generic JNI stub 调用面）----
// SetEntryPointFromJni 等价物的调用桩：按 shorty 分类铺参（GPR 类参数在前、
// FPR 类参数在后——SysV x86_64 与 AAPCS64 按类分配寄存器，重排后每参落入被调方
// 期望寄存器；GPR 参数 ≤6、FPR 参数 ≤8，超出为 M3 汇编 trampoline 后置）。
// self：实例方法为 receiver，静态为 declaring class；会话内 Push/PopLocalFrame
// 包裹（docs 16.6 generic JNI trampoline）。
// 返回 false 表示 pending 异常（UnsatisfiedLinkError/参数问题）。
bool InvokeNativeTrampoline(JNIEnvExt* env, runtime::VMethod* method, runtime::ObjHeader* self,
                            const runtime::Value* args, size_t arg_count, runtime::Value* result);

// 解释器 hook 面：由 VMState 找回其 JNIEnvExt（InitJNIEnvExt 登记）。
// core/interpreter.cc InvokeMethod 对 !HasCode() && native_fn 的派发点（hook 后置，
// 见交付报告）：
//   if (method->native_fn != nullptr)
//     return jni::InvokeNativeTrampoline(state, method, args, arg_count, result);
bool InvokeNativeTrampoline(runtime::VMState* state, runtime::VMethod* method,
                            const runtime::Value* args, size_t arg_count, runtime::Value* result);

// VMState -> JNIEnvExt 登记（InitJNIEnvExt 调用）
void RegisterEnvForState(runtime::VMState* state, JNIEnvExt* env);

// ---- 调用期诊断（TEMP DIAG；-DDEXVM_CALL_DIAGNOSTICS=1 构建，事后移除）----
// 记录「当前线程正在执行哪个 guest native 方法」以及该 native 经 JNIEnv 发起的
// 回调面（FindClass/Get*ID/Call*/ThrowNew），用于归因加固 native（bangcle
// JniLib.cL 等）在 VM 内失败的具体 JNI 交互点。默认构建下全部为空实现。
// Begin 返回本次调用是否纳入诊断（仅加固 native）；End 需以该返回值配对，
// 未纳入的调用不得递减嵌套深度（否则内层普通 native 会误清外层上下文）。
bool NativeDiagBegin(runtime::VMethod* m);
void NativeDiagEnd(bool entered);
// 当前线程最内层 guest native 方法的描述符.name（无则空串；全量 native，不限加固）。
std::string NativeDiagCurrentNative();
// 内存环形记录（不写 logcat，避免同步 I/O 拖慢 guest）；仅在失败点一次性转储。
int NativeDiagDepth();
void NativeDiagRecord(const std::string& entry);
void NativeDiagDumpRing(const char* why);
bool NativeDiagActive();
void NativeDiagLog(const char* fn, const std::string& detail);

}  // namespace dexvm::jni
