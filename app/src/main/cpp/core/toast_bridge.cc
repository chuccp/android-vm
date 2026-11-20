// Host Toast 桥实现（见 core/toast_bridge.h）。
#include "core/toast_bridge.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>

namespace dexvm::runtime {
namespace {

constexpr size_t kMaxQueuedRequests = 128;

struct ToastBridgeState {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<HostToastRequest> requests;
};

ToastBridgeState& State() {
  static ToastBridgeState state;
  return state;
}

void AppendI32(std::string* out, int32_t v) {
  out->append(reinterpret_cast<const char*>(&v), sizeof(v));
}

void AppendStr(std::string* out, const std::string& s) {
  AppendI32(out, static_cast<int32_t>(s.size()));
  out->append(s);
}

}  // namespace

void PushHostToastRequest(HostToastRequest req) {
  ToastBridgeState& s = State();
  {
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.requests.size() >= kMaxQueuedRequests) s.requests.pop_front();
    s.requests.push_back(std::move(req));
  }
  s.cv.notify_one();
}

bool TakeHostToastRequest(HostToastRequest* out, int timeout_ms) {
  if (out == nullptr) return false;
  ToastBridgeState& s = State();
  std::unique_lock<std::mutex> lock(s.mu);
  if (!s.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [&s] { return !s.requests.empty(); })) {
    return false;
  }
  *out = std::move(s.requests.front());
  s.requests.pop_front();
  return true;
}

std::string EncodeHostToastRequest(const HostToastRequest& req) {
  std::string out;
  out.reserve(32 + req.text.size());
  AppendI32(&out, req.duration);
  AppendStr(&out, req.text);
  return out;
}

}  // namespace dexvm::runtime
