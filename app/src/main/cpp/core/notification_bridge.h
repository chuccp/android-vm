// Host 通知桥：guest lib android.app.NotificationManager 把发布/取消/渠道请求
// 入 VM 队列；宿主 NotificationBridge.java 线程经 nativeNotificationTake 取出，
// 用宿主的真 NotificationManager 下发（AOSP 通知语义面，真系统通知）。
//
// 线程模型与 TLS 桥一致：guest 任意线程入队（互斥），宿主分发线程阻塞取；
// 只传值不触 guest 堆。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {

enum class HostNotificationKind : int32_t {
  kNotify = 0,
  kCancel = 1,
  kCancelAll = 2,
  kCreateChannel = 3,
  kDeleteChannel = 4,
};

struct HostNotificationRequest {
  int32_t kind = 0;
  int64_t notify_id = 0;
  int32_t flags = 0;       // AOSP Notification.flags 位（AUTO_CANCEL/ONGOING 等）
  int32_t priority = 0;    // AOSP Notification.priority
  int32_t importance = 3;  // 渠道重要性（NotificationChannel.IMPORTANCE_DEFAULT）
  std::string tag;         // notify/cancel 的 tag（可空）
  std::string channel;     // 渠道 id
  std::string channel_name;
  std::string title;
  std::string text;
};

// 入队（guest 线程，FIFO；队列上限丢最旧）。
void PushHostNotificationRequest(HostNotificationRequest req);

// 宿主分发线程阻塞取一条（timeout_ms 后无请求返回 false）。
bool TakeHostNotificationRequest(HostNotificationRequest* out, int timeout_ms);

// 小端线格式编码（宿主 NotificationBridge.java 解析）：
//   i32 kind | i64 notifyId | i32 flags | i32 priority | i32 importance |
//   str tag | str channel | str channelName | str title | str text
//   str = i32 len + UTF-8 bytes
std::string EncodeHostNotificationRequest(const HostNotificationRequest& req);

}  // namespace dexvm::runtime
