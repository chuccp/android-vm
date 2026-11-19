// native 派发钩子（core→jni 解耦：函数指针注册，见 interpreter.h）
#include "core/interpreter.h"

namespace dexvm::runtime {

namespace {
NativeCallHook g_native_call_hook = nullptr;
}

void SetNativeCallHook(NativeCallHook hook) { g_native_call_hook = hook; }

NativeCallHook GetNativeCallHook() { return g_native_call_hook; }

}  // namespace dexvm::runtime
