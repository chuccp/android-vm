// 异常辅助 + try/handler 解析（语义对照 third_party/art/runtime/art_method.cc FindCatchBlock）
#pragma once

#include <cstdint>
#include <string_view>

#include "core/interpreter.h"

namespace dexvm::runtime {

// ---- 常用异常辅助（抛出即挂 VMState::pending_exception）----
void ThrowNullPointerException(VMState& state, std::string_view msg);
void ThrowIllegalArgumentException(VMState& state, std::string_view msg);
void ThrowArrayIndexOutOfBoundsException(VMState& state, int32_t index, int32_t length);
void ThrowClassCastException(VMState& state, std::string_view from_desc, std::string_view to_desc);
void ThrowOutOfMemoryError(VMState& state, std::string_view msg);
void ThrowArithmeticException(VMState& state, std::string_view msg);
void ThrowUnsupportedOperationException(VMState& state, std::string_view msg);

// try/handler 解析（art_method.cc:328-365）：
// 在 frame 所属方法的 try 区中查找覆盖 dex_pc 的 handler。
// typed catch 按声明顺序 IsAssignableFrom 匹配 -> catch-all；未命中返回 false。
// 命中返回 true 并写 *handler_pc（handler 首指令 code-unit 偏移）。
// 异常对象取 state.pending_exception；解析 handler 类型期间若产生新的
// pending（NoClassDefFoundError 等）按对照语义清掉并跳过该 handler。
bool FindCatchBlock(VMState& state, const Frame& frame, uint32_t dex_pc, uint32_t* handler_pc);

}  // namespace dexvm::runtime
