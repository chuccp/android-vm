// android.hardware 传感器桥接面：
//   - Context.getSystemService("sensor"/SensorManager.class) = SensorManager
//     单例（lib 类 android/hardware/SensorManager.java；AOSP 服务缓存同形）。
//   - android.hardware.VmSensors：guest lib 的桥接钩子——list() 取宿主
//     SensorManager.getSensorList(TYPE_ALL) 描述表（"\n" 分隔，行内 "|" 分
//     列），pollEvent([F[J)I 取一条宿主事件（返回 sensor type，无事件 -1）。
// 事件来源：宿主 SensorBridge.java 注册真监听 -> nativeSensorPush ->
// core/sensor_bridge.cc 队列。VM 不伪造传感器身份/数据。
#include <cstring>
#include <string>

#include "core/builtins_internal.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/intrinsic.h"
#include "core/sensor_bridge.h"
#include "core/vm_types.h"

namespace dexvm::runtime {
namespace {

VObject* g_sensor_manager = nullptr;

// UTF-8 -> UTF-16（宿主描述串；非法字节替换，不抛）。
std::u16string Utf8ToUtf16(const std::string& bytes) {
  std::u16string out;
  out.reserve(bytes.size());
  size_t i = 0;
  while (i < bytes.size()) {
    unsigned char c = static_cast<unsigned char>(bytes[i]);
    uint32_t cp = 0xFFFD;
    size_t n = 1;
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0 && i + 1 < bytes.size()) {
      cp = ((c & 0x1F) << 6) | (static_cast<unsigned char>(bytes[i + 1]) & 0x3F);
      n = 2;
    } else if ((c & 0xF0) == 0xE0 && i + 2 < bytes.size()) {
      cp = ((c & 0x0F) << 12) | ((static_cast<unsigned char>(bytes[i + 1]) & 0x3F) << 6) |
           (static_cast<unsigned char>(bytes[i + 2]) & 0x3F);
      n = 3;
    } else if ((c & 0xF8) == 0xF0 && i + 3 < bytes.size()) {
      cp = ((c & 0x07) << 18) | ((static_cast<unsigned char>(bytes[i + 1]) & 0x3F) << 12) |
           ((static_cast<unsigned char>(bytes[i + 2]) & 0x3F) << 6) |
           (static_cast<unsigned char>(bytes[i + 3]) & 0x3F);
      n = 4;
    }
    i += n;
    if (cp <= 0xFFFF) {
      out.push_back(static_cast<char16_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

}  // namespace

// SensorManager 单例（AOSP getSystemService(SENSOR_SERVICE) 缓存面同形）。
VObject* AllocSensorManager(VMState& state) {
  if (g_sensor_manager != nullptr) return g_sensor_manager;
  bool lib_ok = true;
  VObject* obj = NewLibObject(state, "Landroid/hardware/SensorManager;", {}, nullptr, 0, &lib_ok);
  if (obj == nullptr) {
    state.ClearException();
    VClass* klass = state.linker->FindClass("Landroid/hardware/SensorManager;");
    if (klass == nullptr) return nullptr;
    obj = state.linker->AllocObject(klass);
  }
  if (obj == nullptr) return nullptr;
  g_sensor_manager = obj;
  if (state.heap != nullptr) state.heap->Pin(obj);  // 系统服务缓存即 GC 根
  return obj;
}

// android.hardware.VmSensors.list()Ljava/lang/String;
Value VmSensorsList(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(Utf8ToUtf16(GetHostSensorList())));
  return r;
}

// android.hardware.VmSensors.pollEvent([F[J)I
// values 为 float[]（8 字节槽：低 32 位 float 位型）；timestamp 为 long[2]：
// [0] = 事件时间戳（ns），[1] = 本次有效值个数（guest 据此裁剪 values 长度）。
// 返回 sensor type；无事件返回 -1（guest 泵据此停止本轮轮询）。
Value VmSensorsPoll(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = -1;
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr) return r;
  HostSensorEvent ev;
  if (!PollHostSensorEvent(&ev)) return r;
  VArray* values = reinterpret_cast<VArray*>(args[0].ref);
  size_t cap = values->length > 0 ? static_cast<size_t>(values->length) : 0;
  size_t count = static_cast<size_t>(ev.count);
  if (count > cap) count = cap;
  uint8_t* dst = values->data();
  for (size_t i = 0; i < count; i++) {
    uint32_t bits = 0;
    std::memcpy(&bits, &ev.values[i], sizeof(bits));
    uint64_t slot = bits;  // Narrow::kWord：float 位型在低 32 位
    std::memcpy(dst + i * 8, &slot, sizeof(slot));
  }
  VArray* ts = reinterpret_cast<VArray*>(args[1].ref);
  if (ts->length > 0) {
    uint64_t t = static_cast<uint64_t>(ev.timestamp);
    std::memcpy(ts->data(), &t, sizeof(t));
  }
  if (ts->length > 1) {
    uint64_t c = static_cast<uint64_t>(count);
    std::memcpy(ts->data() + 8, &c, sizeof(c));
  }
  r.i = ev.type;
  return r;
}

}  // namespace dexvm::runtime
