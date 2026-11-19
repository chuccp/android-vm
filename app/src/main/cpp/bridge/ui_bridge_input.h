// 点击回灌缝（P1 最小面）：host 点击 -> viewId -> guest listener 注入/派发。
// 契约面（docs 设计："监听器派发归桥接层"）：
//   host 点击（宿主物化器 onClick）-> viewId -> InjectGuestClick -> guest
//   View.performClick 缝 -> setOnClickListener 登记的 guest listener.onClick。
// 实现体在 core/framework_view.cc（缝接 TU）：登记表持有 guest View/listener
// 指针，随 VM 会话存活——只允许在活会话（调用方持 VMState）里触达 guest 堆；
// vm_state == nullptr 为设备侧会话外探测（只回通路状态，零 guest 堆访问）。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {
struct VMState;
struct VObject;
}  // namespace dexvm::runtime

namespace dexvm::bridge {

// host 点击回灌通路：viewId -> 登记的 guest listener -> onClick(View) 派发。
// 返回状态串（宿主 logcat/报告消费）：
//   "dispatched viewId=N listener=1"       —— listener.onClick 已执行
//   "dispatched viewId=N listener=0"       —— 会话内已知 view 但无监听器
//   "no-view viewId=N"                     —— 会话内无匹配 viewId
//   "channel-ok viewId=N guest-session=absent" —— 会话外（设备侧安全形态）
std::string InjectGuestClick(dexvm::runtime::VMState* vm_state, int32_t view_id);

// 当前登记的 guest 监听器个数（会话内断言用）。
size_t GuestClickListenerCount();

// 该 guest View 是否登记了监听器（快照 kPropHasClickListener 用；无副作用）。
bool HasGuestClickListener(dexvm::runtime::VObject* view);

// 视图登记（快照导出 walk 时调用：viewId -> view 映射，InjectGuestClick 查路）。
void NoteGuestView(dexvm::runtime::VObject* view);

// 登记表（监听器 + 视图映射）清空（RegisterRenderPipeline 每 VM 引导调用；
// 防跨会话悬挂）。
void ClearGuestClickListeners();

}  // namespace dexvm::bridge
