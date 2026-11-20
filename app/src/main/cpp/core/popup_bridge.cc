// Host 弹层桥实现（见 core/popup_bridge.h）。
#include "core/popup_bridge.h"

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>

namespace dexvm::runtime {
namespace {

constexpr size_t kMaxQueuedRequests = 64;

struct PopupBridgeState {
  std::mutex mu;
  std::condition_variable cv;
  std::deque<HostPopupRequest> requests;
};

PopupBridgeState& State() {
  static PopupBridgeState state;
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

void PushHostPopupRequest(HostPopupRequest req) {
  PopupBridgeState& s = State();
  {
    std::lock_guard<std::mutex> lock(s.mu);
    if (s.requests.size() >= kMaxQueuedRequests) s.requests.pop_front();
    s.requests.push_back(std::move(req));
  }
  s.cv.notify_one();
}

bool TakeHostPopupRequest(HostPopupRequest* out, int timeout_ms) {
  if (out == nullptr) return false;
  PopupBridgeState& s = State();
  std::unique_lock<std::mutex> lock(s.mu);
  if (!s.cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                     [&s] { return !s.requests.empty(); })) {
    return false;
  }
  *out = std::move(s.requests.front());
  s.requests.pop_front();
  return true;
}

std::string EncodeHostPopupRequest(const HostPopupRequest& req) {
  std::string out;
  out.reserve(24 + req.titles.size());
  AppendI32(&out, req.kind);
  AppendI32(&out, req.anchor_view_id);
  AppendI32(&out, static_cast<int32_t>(req.token));
  AppendStr(&out, req.titles);
  return out;
}

}  // namespace dexvm::runtime
