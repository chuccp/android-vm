// Dialog 窗口面（guest 侧）：Dialog.show/dismiss -> 窗口登记 -> 内容树作为
// 独立窗口根进入 UI 桥快照（宿主只看到视图树与 dim 属性，看不到 Dialog 对象）。
//
// 单窗口 VM 语义：逻辑窗口尺寸 = 会话内容根（540x960 口径）。窗口内容在登记时
// 及每次快照前由 VM 执行 measure/layout（Window.setLayout 的宽高意图），
// 位置按 gravity（默认居中）；真正出画/命中仍由宿主 UI 桥完成。
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/vm_types.h"

namespace dexvm::runtime {

struct VMState;

// 会话宿主内容根与逻辑窗口尺寸（runtime_contract 会话启动时登记）。
void SetDialogHostView(VObject* content, int32_t width, int32_t height);

// Dialog.show(...) / Dialog.dismiss(...)：VmLayoutHost intrinsic 实现体。
Value VmDialogShow(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
Value VmDialogDismiss(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);

// 快照前布局所有登记窗口（measure/layout）；返回当前可见窗口数。
size_t LayoutDialogWindows(VMState& state);

// 快照导出用：按显示顺序收集窗口内容根 + dim（最多 max 个）。
struct DialogWindowInfo {
  VObject* content = nullptr;
  float dim = 0.0f;
};
size_t CollectDialogWindows(DialogWindowInfo* out, size_t max);

// 设备验证探针（marker 文件触发，仅诊断面）：用真实 lib Dialog/Window +
// 程序化内容树走 show/dismiss 全链路，验证快照叠加根与宿主物化。
bool DebugDialogProbeShow(VMState& state, VObject* activity);
// AppCompat AlertDialog 探针（app 真实弹框路径：Delegate + AlertController）。
bool DebugDialogProbeShowAppCompat(VMState& state, VObject* activity);
void DebugDialogProbeDismiss(VMState& state);

}  // namespace dexvm::runtime
