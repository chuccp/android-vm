// android.media 最小内建面（AudioManager/AudioDeviceInfo/MediaPlayer）——
// framework 桥接档：设备播放面归宿主（后续桥接），本层保证 Java 语义形状
// （对象可构造、方法不缺失、返回值类型正确）。首个消费方 = target-app
// MainActivity.initializeMedia（音频管理器身份断言 + MediaPlayer 准备/播放链）。
//
// 语义口径：
//   - Context.getSystemService(AUDIO_SERVICE/"audio") = AudioManager 单例
//     （AOSP ActivityThread 系统服务缓存同形；类/字符串两形同实例）。
//   - AudioManager.getDevices：本 VM 无音频设备桥，返回空 AudioDeviceInfo[]
//     （不伪造设备身份；app 侧 speaker=null 分支自然成立）。
//   - MediaPlayer：数据源/播放状态机为宿主桥接前的最小可执行形态（不产声）。
#include <string>

#include "core/builtins_internal.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/interpreter.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"

namespace dexvm::runtime {
namespace {

VObject* g_audio_manager = nullptr;

}  // namespace



// AudioManager 单例（AOSP getSystemService(AUDIO_SERVICE) 缓存面同形）
VObject* AllocAudioManager(VMState& state) {
  if (g_audio_manager != nullptr) return g_audio_manager;
  // AudioManager 已归位 lib（android/media/AudioManager.java）：经 lib 构造器
  // 建实例（字段态初始化 mVolumes 等），lib 不可得时回落裸分配。
  bool lib_ok = true;
  VObject* obj = NewLibObject(state, "Landroid/media/AudioManager;", {}, nullptr, 0, &lib_ok);
  if (obj == nullptr) {
    state.ClearException();
    VClass* klass = state.linker->FindClass("Landroid/media/AudioManager;");
    if (klass == nullptr) return nullptr;
    obj = state.linker->AllocObject(klass);
  }
  if (obj == nullptr) return nullptr;
  g_audio_manager = obj;
  if (state.heap != nullptr) state.heap->Pin(obj);  // 系统服务缓存即 GC 根
  return obj;
}

// ---- 宿主桥接值面（Host.get：宿主启动时推入 telephony.* 等真值快照）----
namespace {
std::map<std::string, std::string> g_host_values;
VObject* g_telephony_manager = nullptr;
}  // namespace

void SetHostValue(const std::string& key, const std::string& value) {
  g_host_values[key] = value;
}

void ClearHostValues() { g_host_values.clear(); }

// com.dexvm.runtime.Host.get(String)：宿主快照读取（无键 = null）
Value HostGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  const std::string key = Mutf8Arg(args[0]);
  auto it = g_host_values.find(key);
  if (it == g_host_values.end()) return r;
  std::u16string v(it->second.begin(), it->second.end());
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(v));
  return r;
}

VObject* AllocTelephonyManager(VMState& state) {
  if (g_telephony_manager != nullptr) return g_telephony_manager;
  bool lib_ok = true;
  VObject* obj = NewLibObject(state, "Landroid/telephony/TelephonyManager;", {}, nullptr, 0,
                              &lib_ok);
  if (obj == nullptr) {
    state.ClearException();
    VClass* klass = state.linker->FindClass("Landroid/telephony/TelephonyManager;");
    if (klass == nullptr) return nullptr;
    obj = state.linker->AllocObject(klass);
  }
  if (obj == nullptr) return nullptr;
  g_telephony_manager = obj;
  if (state.heap != nullptr) state.heap->Pin(obj);
  return obj;
}

// Context.getSystemService(String)：audio -> AudioManager 单例；phone -> 桥接
// TelephonyManager（宿主真值快照）；其余 null（未桥接服务不伪造身份）。

Value CtxGetSystemService(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const std::string name = Mutf8Arg(args[0]);
  if (name == "audio") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocAudioManager(state));
  } else if (name == "phone") {
    // AOSP Context.TELEPHONY_SERVICE = "phone"
    r.ref = reinterpret_cast<ObjHeader*>(AllocTelephonyManager(state));
  } else if (name == "connectivity") {
    // AOSP Context.CONNECTIVITY_SERVICE = "connectivity"（宿主在线投影）
    r.ref = reinterpret_cast<ObjHeader*>(AllocConnectivityManager(state));
  } else if (name == "wifi") {
    // AOSP Context.WIFI_SERVICE = "wifi"
    r.ref = reinterpret_cast<ObjHeader*>(AllocWifiManager(state));
  } else if (name == "layout_inflater") {
    // AOSP ContextImpl 缓存 per-context LayoutInflater（VM 版 = lib VmLayoutInflater）
    r.ref = reinterpret_cast<ObjHeader*>(AllocPlatformLayoutInflater(state, self));
  } else if (name == "sensor") {
    // AOSP Context.SENSOR_SERVICE = "sensor"（宿主 SensorManager 桥接单例）
    r.ref = reinterpret_cast<ObjHeader*>(AllocSensorManager(state));
  } else if (name == "notification") {
    // AOSP Context.NOTIFICATION_SERVICE = "notification"（宿主通知桥单例）
    r.ref = reinterpret_cast<ObjHeader*>(AllocNotificationManager(state));
  } else if (name == "bluetooth") {
    // AOSP Context.BLUETOOTH_SERVICE = "bluetooth"（lib BluetoothManager；
    // 适配器身份/状态由宿主 bluetooth.* 快照投影）
    bool bt_ok = true;
    VObject* bt = NewLibObject(state, "Landroid/bluetooth/BluetoothManager;", {}, nullptr, 0,
                               &bt_ok);
    if (bt == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(bt);
  } else if (name == "uimode") {
    // AOSP Context.UI_MODE_SERVICE = "uimode"（lib UiModeManager；宿主
    // uimode.* 快照投影）
    bool um_ok = true;
    VObject* um = NewLibObject(state, "Landroid/app/UiModeManager;", {}, nullptr, 0, &um_ok);
    if (um == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(um);
  } else if (name == "accessibility") {
    // AOSP Context.ACCESSIBILITY_SERVICE = "accessibility"：lib AccessibilityManager
    // 单例（状态恒关闭）。缺此返回 null → ViewCompat/AccessibilityDelegate 调用点
    // NPE（ViewPager2 构造链实证）。
    bool am_ok = true;
    VObject* am = NewLibObject(state, "Landroid/view/accessibility/AccessibilityManager;", {},
                               nullptr, 0, &am_ok);
    if (am == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(am);
  } else if (name == "jobscheduler") {
    // AOSP Context.JOB_SCHEDULER_SERVICE = "jobscheduler"（lib JobScheduler；
    // VM 无作业调度服务：调度调用本地 no-op，但不返回 null——AOSP 恒非空，
    // 实测调用方直接 cancel() 会 NPE）
    bool js_ok = true;
    VObject* js = NewLibObject(state, "Landroid/app/job/JobScheduler;", {}, nullptr, 0, &js_ok);
    if (js == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(js);
  }
  return r;
}

// Context.getSystemService(Class)：泛型擦除签名 (Ljava/lang/Class;)Ljava/lang/Object;。
// Class 镜像对象即 VClass*（VM 类元数据同形）。
Value CtxGetSystemServiceClass(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  VClass* klass = reinterpret_cast<VClass*>(args[0].ref);
  if (klass->descriptor == "Landroid/media/AudioManager;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocAudioManager(state));
  } else if (klass->descriptor == "Landroid/telephony/TelephonyManager;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocTelephonyManager(state));
  } else if (klass->descriptor == "Landroid/net/ConnectivityManager;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocConnectivityManager(state));
  } else if (klass->descriptor == "Landroid/net/wifi/WifiManager;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocWifiManager(state));
  } else if (klass->descriptor == "Landroid/view/LayoutInflater;" ||
             klass->descriptor == "Landroid/view/VmLayoutInflater;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocPlatformLayoutInflater(state, self));
  } else if (klass->descriptor == "Landroid/hardware/SensorManager;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocSensorManager(state));
  } else if (klass->descriptor == "Landroid/app/NotificationManager;") {
    r.ref = reinterpret_cast<ObjHeader*>(AllocNotificationManager(state));
  } else if (klass->descriptor == "Landroid/bluetooth/BluetoothManager;") {
    bool bt_ok = true;
    VObject* bt = NewLibObject(state, "Landroid/bluetooth/BluetoothManager;", {}, nullptr, 0,
                               &bt_ok);
    if (bt == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(bt);
  } else if (klass->descriptor == "Landroid/app/UiModeManager;") {
    bool um_ok = true;
    VObject* um = NewLibObject(state, "Landroid/app/UiModeManager;", {}, nullptr, 0, &um_ok);
    if (um == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(um);
  } else if (klass->descriptor == "Landroid/app/job/JobScheduler;") {
    bool js_ok = true;
    VObject* js = NewLibObject(state, "Landroid/app/job/JobScheduler;", {}, nullptr, 0, &js_ok);
    if (js == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(js);
  } else if (klass->descriptor == "Landroid/view/accessibility/AccessibilityManager;") {
    bool am_ok = true;
    VObject* am = NewLibObject(state, "Landroid/view/accessibility/AccessibilityManager;", {},
                               nullptr, 0, &am_ok);
    if (am == nullptr) state.ClearException();
    r.ref = reinterpret_cast<ObjHeader*>(am);
  }
  return r;
}

// AudioManager.getDevices(int)：空设备表（无音频设备桥）
Value AudioManagerGetDevices(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* cls = state.linker->FindClass("Landroid/media/AudioDeviceInfo;");
  if (cls == nullptr) return r;
  VClass* arr_cls = state.linker->FindArrayClass(cls);
  if (arr_cls == nullptr) return r;
  r.ref = state.linker->AllocArray(arr_cls, 0);
  return r;
}

// AudioDeviceInfo.getType()：无设备实例时不可达；骨架返回 0（AOSP TYPE_UNKNOWN）
Value AudioDeviceGetType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  return r;
}

// ---- MediaPlayer 骨架（宿主桥接前的可执行形状；不产声）----
Value MediaPlayerInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}
Value MediaPlayerNoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}
Value MediaPlayerSetPreferredDevice(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;  // true：设备优先路由请求接受（AOSP setPreferredDevice 返回请求受理）
  return r;
}
Value MediaPlayerGetDuration(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1000;  // 时长探针面（宿主桥接后由真实数据源填值）
  return r;
}
Value MediaPlayerIsPlaying(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  return r;
}

// ---- VideoView 播放状态面（宿主桥接出画：字段 -> bridge props）----

Value VideoViewSetVideoPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mVideoPath");
    if (f != nullptr) SetRefField(self, f->offset, args[0].ref);
  }
  return Value{};
}

Value VideoViewSeekTo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mVideoSeek");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value VideoViewStart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  if (VField* f = FindInstanceField(self->klass, "mVideoPlaying")) SetIField(self, f->offset, 1);
  return Value{};
}

Value VideoViewStop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  if (VField* f = FindInstanceField(self->klass, "mVideoPlaying")) SetIField(self, f->offset, 0);
  return Value{};
}

Value VideoViewIsPlaying(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mVideoPlaying");
  if (f != nullptr) r.i = GetIField(self, f->offset) != 0 ? 1 : 0;
  return r;
}

// 新 VM 引导复位（旧堆销毁后静态单例不得跨 VM 复用——dirty intrinsic 守卫
// 会以 outside-heap 拒绝旧指针）。
void ResetMediaState() {
  g_audio_manager = nullptr;
  g_telephony_manager = nullptr;
  g_host_values.clear();
}

}  // namespace dexvm::runtime
