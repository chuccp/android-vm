// Host 弹层桥（PopupMenu）：guest lib android.widget.PopupMenu.show() 把
// 锚点/菜单项/token 入 VM 队列；宿主 PopupMenuBridge.java 线程经
// nativePopupTake 取出，用宿主真 android.widget.PopupMenu 展示（原生动画/
// 阴影/外点关闭），菜单项点击经 nativeDispatchPopupEvent 回灌 guest。
//
// 线程模型与 Toast 桥一致：guest 任意线程入队（互斥），宿主分发线程阻塞取；
// 只传值不触 guest 堆。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {

struct HostPopupRequest {
  int32_t kind = 0;             // 0 = show menu / 1 = dismiss menu
  int32_t anchor_view_id = 0;   // guest mId（宿主按 id 解析锚点视图）
  uint32_t token = 0;           // guest PopupMenu 身份（点击/关闭回灌用）
  std::string titles;           // 菜单项标题（'\n' 连接）
};

// 入队（guest 线程，FIFO；队列上限丢最旧）。
void PushHostPopupRequest(HostPopupRequest req);

// 宿主分发线程阻塞取一条（timeout_ms 后无请求返回 false）。
bool TakeHostPopupRequest(HostPopupRequest* out, int timeout_ms);

// 小端线格式编码（宿主 PopupMenuBridge.java 解析）：
//   i32 kind | i32 anchor_view_id | u32 token | str titles
std::string EncodeHostPopupRequest(const HostPopupRequest& req);

}  // namespace dexvm::runtime
