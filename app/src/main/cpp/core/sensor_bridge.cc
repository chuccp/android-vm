// Host 传感器桥实现：互斥队列 + 描述表（见 core/sensor_bridge.h）。
#include "core/sensor_bridge.h"

#include <deque>
#include <mutex>

namespace dexvm::runtime {
namespace {

constexpr size_t kMaxQueuedEvents = 512;  // 宿主采样率高于 guest 泵速时丢最旧

struct SensorBridgeState {
  std::mutex mu;
  std::deque<HostSensorEvent> events;
  std::string list_encoded;
};

SensorBridgeState& State() {
  static SensorBridgeState state;
  return state;
}

}  // namespace

void SetHostSensorList(const std::string& encoded) {
  SensorBridgeState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  s.list_encoded = encoded;
}

std::string GetHostSensorList() {
  SensorBridgeState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  return s.list_encoded;
}

void PushHostSensorEvent(int32_t type, const float* values, int32_t count, int64_t timestamp) {
  HostSensorEvent ev;
  ev.type = type;
  ev.timestamp = timestamp;
  if (values != nullptr && count > 0) {
    if (count > static_cast<int32_t>(sizeof(ev.values) / sizeof(ev.values[0]))) {
      count = static_cast<int32_t>(sizeof(ev.values) / sizeof(ev.values[0]));
    }
    ev.count = count;
    for (int32_t i = 0; i < count; i++) ev.values[i] = values[i];
  }
  SensorBridgeState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.events.size() >= kMaxQueuedEvents) s.events.pop_front();
  s.events.push_back(ev);
}

bool PollHostSensorEvent(HostSensorEvent* out) {
  if (out == nullptr) return false;
  SensorBridgeState& s = State();
  std::lock_guard<std::mutex> lock(s.mu);
  if (s.events.empty()) return false;
  *out = s.events.front();
  s.events.pop_front();
  return true;
}

}  // namespace dexvm::runtime
