// Host 通知桥实现（见 core/notification_bridge.h）。
#include "core/notification_bridge.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>

namespace dexvm::runtime {
namespace {

constexpr size_t kMaxQueuedRequests = 128;

struct NotificationBridgeState {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<HostNotificationRequest> requests;
};

NotificationBridgeState& State() {
  static NotificationBridgeState state;
  return state;
}

void AppendI32(std::string* out, int32_t v) {
  out->append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void AppendI64(std::string* out, int64_t v) {
  out->append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void AppendStr(std::string* out, const std::string& s) {
  AppendI32(out, static_cast<int32_t>(s.size()));
  out->append(s);
}

}  // namespace

void PushHostNotificationRequest(HostNotificationRequest req) {
  NotificationBridgeState& s = State();
  {
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.requests.size() >= kMaxQueuedRequests) s.requests.pop_front();
    s.requests.push_back(std::move(req));
  }
  s.cv.notify_one();
}

bool TakeHostNotificationRequest(HostNotificationRequest* out, int timeout_ms) {
  if (out == nullptr) return false;
  NotificationBridgeState& s = State();
  std::unique_lock<std::mutex> lock(s.mu);
  if (!s.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [&s] { return !s.requests.empty(); })) {
    return false;
  }
  *out = std::move(s.requests.front());
  s.requests.pop_front();
  return true;
}

std::string EncodeHostNotificationRequest(const HostNotificationRequest& req) {
  std::string out;
  out.reserve(128 + req.title.size() + req.text.size());
  AppendI32(&out, req.kind);
  AppendI64(&out, req.notify_id);
  AppendI32(&out, req.flags);
  AppendI32(&out, req.priority);
  AppendI32(&out, req.importance);
  AppendStr(&out, req.tag);
  AppendStr(&out, req.channel);
  AppendStr(&out, req.channel_name);
  AppendStr(&out, req.title);
  AppendStr(&out, req.text);
  return out;
}

}  // namespace dexvm::runtime
