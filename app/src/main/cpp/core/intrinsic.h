// Intrinsic 派发约定（M2，docs 14 章 28.1/28.2）：
// VMethod::native_fn 非空时，解释器 InvokeMethod 走 IntrinsicFn 直调
// （自研等价面：ART quick entrypoint / JNI stub 里 VM 内建方法的直落路径）。
#pragma once

#include <cstddef>

#include "core/vm_types.h"

namespace dexvm::runtime {

struct VMState;

// self：实例方法接收者（static 为 nullptr；VObject* 形态，String/Array 除外）。
// args/n：声明参数（【不含】self；wide 一个 Value 装 64 位）。
// 成功返回返回值（void 方法返回 Value{}）；失败置 *ok = false 并挂
// state.pending_exception。-fno-exceptions：错误一律走 ok/pending_exception。
using IntrinsicFn = Value (*)(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok);

// VMethod::native_fn 槽位（void*）与 IntrinsicFn 互转
inline IntrinsicFn AsIntrinsic(void* slot) { return reinterpret_cast<IntrinsicFn>(slot); }
inline void* ToIntrinsicSlot(IntrinsicFn fn) { return reinterpret_cast<void*>(fn); }

}  // namespace dexvm::runtime
