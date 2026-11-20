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

// 按快照 handle.object_id 寻址的点击回灌（推荐路径）。guest View 身份稳定，
// 对 mId==0 的视图（列表容器/自绘行等）不会像 viewId 通道那样退化成
// "第一条 mId==0 记录"，避免派发到已回收的旧监听器。状态串同 viewId 通道：
//   "dispatched objectId=N listener=1/0" / "no-view objectId=N" / channel-ok …
std::string InjectGuestObjectClick(dexvm::runtime::VMState* vm_state, uint32_t object_id);

// 长按回灌：objectId -> guest View.performLongClick()（派发登记的
// OnLongClickListener.onLongClick(View)）。状态串：
//   "dispatched objectId=N long=1/0" / "no-view objectId=N" / channel-ok …
std::string InjectGuestObjectLongClick(dexvm::runtime::VMState* vm_state, uint32_t object_id);

// 文本输入回灌：objectId -> guest setText(CharSequence) + TextWatcher 通知
// （宿主 EditText 键入 -> guest 文本/监听器）。状态串：
//   "dispatched objectId=N text-len=M" / "no-view objectId=N" / channel-ok …
std::string InjectGuestObjectText(dexvm::runtime::VMState* vm_state, uint32_t object_id,
                                  const std::string& text);

// SeekBar 拖动回灌（phase：0 progress / 1 startTracking / 2 stopTracking）：
// objectId -> guest SeekBar.onHostProgressChanged/onHostStartTrackingTouch/
// onHostStopTrackingTouch（lib SeekBar 的 AOSP 监听器语义）。状态串：
//   "dispatched objectId=N seek-phase=P value=V" / "no-view …" / channel-ok …
std::string InjectGuestObjectSeek(dexvm::runtime::VMState* vm_state, uint32_t object_id,
                                  int32_t progress, int32_t phase);

// RatingBar 拖星回灌：objectId + milliRating -> guest onHostRatingChanged
// （fromUser=true 的 OnRatingBarChangeListener 语义）。状态串：
//   "dispatched objectId=N rating-milli=V" / "no-view …" / channel-ok …
std::string InjectGuestObjectRating(dexvm::runtime::VMState* vm_state, uint32_t object_id,
                                    int32_t milli_rating);

// DrawerLayout 开合回灌：objectId + open -> guest onHostDrawerStateChanged
// （lib DrawerLayout 按新状态通知 DrawerListener）。状态串：
//   "dispatched objectId=N drawer-open=1/0" / "no-view …" / channel-ok …
std::string InjectGuestObjectDrawer(dexvm::runtime::VMState* vm_state, uint32_t object_id,
                                    bool open);

// 该 guest View 是否登记了 long-click 监听器（快照 kPropHasLongClickListener 用）。
bool HasGuestLongClickListener(dexvm::runtime::VObject* view);

// 当前登记的 guest 监听器个数（会话内断言用）。
size_t GuestClickListenerCount();

// 该 guest View 是否登记了监听器（快照 kPropHasClickListener 用；无副作用）。
bool HasGuestClickListener(dexvm::runtime::VObject* view);

// 视图登记（快照导出 walk 时调用：object_id -> view 映射，点击回灌查路）。
void NoteGuestView(dexvm::runtime::VObject* view, uint32_t object_id);

// 快照 walk 边界（BuildSnapshotFromGuest 首尾调用）：推进/淘汰登记表代号，
// 只让当前树内视图参与点击派发与 listener GC 根。
void BeginGuestSnapshotWalk();
void EndGuestSnapshotWalk();

// 登记表（监听器 + 视图映射）清空（RegisterRenderPipeline 每 VM 引导调用；
// 防跨会话悬挂）。
void ClearGuestClickListeners();

}  // namespace dexvm::bridge
