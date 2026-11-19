// Host 传感器桥：宿主 SensorBridge.java 注册真 SensorManager 监听并把
// 传感器描述/事件推入 VM（nativeSensorSetList / nativeSensorPush）；guest
// lib android.hardware.SensorManager 经 android.hardware.VmSensors 轮询派发
// （AOSP SensorEventListener 语义面：onSensorChanged/onAccuracyChanged）。
//
// 线程模型：宿主任意线程推入（当前为宿主主线程 Looper），guest 会话线程轮询；
// 队列互斥保护，只传值不触 guest 堆（与 TLS/点击回灌同口径）。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {

// 单条传感器事件（值域最多 16 个 float；加速度计 3、旋转向量 5 等）。
struct HostSensorEvent {
  int32_t type = 0;
  int32_t count = 0;
  float values[16] = {};
  int64_t timestamp = 0;
};

// 传感器描述表：每行 "type|name|vendor|version|maxRange|resolution|power|minDelay"。
// 宿主 SensorManager.getSensorList(TYPE_ALL) 的真值投影（VM 不伪造传感器身份）。
void SetHostSensorList(const std::string& encoded);
std::string GetHostSensorList();

// 事件入队（宿主 SensorEventListener 回调线程调用）。FIFO，队列上限丢最旧。
void PushHostSensorEvent(int32_t type, const float* values, int32_t count, int64_t timestamp);

// guest 轮询一条事件；无事件返回 false。
bool PollHostSensorEvent(HostSensorEvent* out);

}  // namespace dexvm::runtime
