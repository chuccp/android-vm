// Host Toast 桥：guest builtin android.widget.Toast.show() 把文案/时长入 VM
// 队列；宿主 ToastBridge.java 线程经 nativeToastTake 取出，用宿主真 Toast
// 显示（真系统提示，不伪造）。
//
// 线程模型与通知桥一致：guest 任意线程入队（互斥），宿主分发线程阻塞取；
// 只传值不触 guest 堆。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {

struct HostToastRequest {
  int32_t duration = 0;  // Toast.LENGTH_SHORT(0) / LENGTH_LONG(1)
  std::string text;
};

// 入队（guest 线程，FIFO；队列上限丢最旧）。
void PushHostToastRequest(HostToastRequest req);

// 宿主分发线程阻塞取一条（timeout_ms 后无请求返回 false）。
bool TakeHostToastRequest(HostToastRequest* out, int timeout_ms);

// 小端线格式编码（宿主 ToastBridge.java 解析）：i32 duration | str text
// str = i32 len + UTF-8 bytes
std::string EncodeHostToastRequest(const HostToastRequest& req);

}  // namespace dexvm::runtime
