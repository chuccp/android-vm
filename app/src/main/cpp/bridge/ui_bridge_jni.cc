// UI 桥接 JNI 面（设备侧桥轨取画/点击回灌通道）。
// 符号按 JNI 命名约定导出（Java_com_dexvm_host_NativeBridge_*）——不进
// runtime_contract 的 RegisterNatives 表（jni/ 不动），由 VM 标准 native 名
// 查找解析。编入 dexvm SHARED 目标（--exclude-libs 只放行 SHARED 直编单元，
// JNIEXPORT = default visibility）。
//
//   nativeGetViewSnapshot()   [B  —— 桥轨 last PresentedViewTree envelope
//                                  （View 快照/patch + draw 命令流，小端）
//   nativeDispatchViewClick(I) Ljava/lang/String; —— 点击回灌通路（viewId ->
//                                  guest listener 注入）。设备侧宿主点击时 guest
//                                  会话通常已结束：只回通路状态，不触 guest 堆
//                                  （bridge/ui_bridge_input.h 会话外形态）。
//
// 本 TU 同时锚定 bridge/ui_bridge_protocol.cc（RegisterUiBridgeProtocol）：
// 其加载自注册 SetViewTreeExporter/SetGlobalDrawSink 才会生效。
#include "bridge/bridge.h"
#include "bridge/ui_bridge_input.h"
#include "bridge/ui_bridge_protocol.h"

#if defined(__ANDROID__)

#include <jni.h>

#include <string>
#include <vector>

namespace {

// 锚定协议 TU + 模块加载即挂缝A/缝B（幂等）。
struct JniAnchor {
  JniAnchor() { dexvm::bridge::RegisterUiBridgeProtocol(); }
} g_jni_anchor;

}  // namespace

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_dexvm_host_NativeBridge_nativeGetViewSnapshot(JNIEnv* env, jclass) {
  const std::vector<uint8_t>* bytes = dexvm::bridge::LastPresentedViewTree();
  if (bytes == nullptr || bytes->empty()) return nullptr;
  jbyteArray arr = env->functions->NewByteArray(env, static_cast<jsize>(bytes->size()));
  if (arr != nullptr) {
    env->functions->SetByteArrayRegion(env, arr, 0, static_cast<jsize>(bytes->size()),
                                       reinterpret_cast<const jbyte*>(bytes->data()));
  }
  return arr;
}

// 常驻会话点击入队（runtime_contract.cpp：活会话内由会话线程派发 guest
// listener；返回 false = 无活会话）。
extern "C" bool DexVmSessionEnqueueClick(int32_t view_id);
extern "C" bool DexVmSessionEnqueueObjectClick(uint32_t object_id);

extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewClick(JNIEnv* env, jclass, jint view_id) {
  // 活会话：入队由 VM 会话线程派发 guest listener.onClick（不跨线程触达堆）。
  std::string status;
  if (DexVmSessionEnqueueClick(view_id)) {
    status = "queued viewId=" + std::to_string(view_id);
  } else {
    // 会话外探测形态（vm_state = null）：通路确认 + 登记计数，不触 guest 堆。
    status = dexvm::bridge::InjectGuestClick(nullptr, view_id);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// 按快照 handle.object_id 的点击入队（宿主物化器推荐路径：guest View 身份
// 稳定，mId==0 的视图不会误命中其它无 id View）。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewObjectClick(JNIEnv* env, jclass,
                                                               jint object_id) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueObjectClick(id)) {
    status = "queued objectId=" + std::to_string(id);
  } else {
    status = dexvm::bridge::InjectGuestObjectClick(nullptr, id);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

extern "C" bool DexVmSessionEnqueueLongClick(uint32_t object_id);
extern "C" bool DexVmSessionEnqueueText(uint32_t object_id, const char* text, size_t len);
extern "C" bool DexVmSessionEnqueueSeek(uint32_t object_id, int32_t progress, int32_t phase);
extern "C" bool DexVmSessionEnqueueRating(uint32_t object_id, int32_t milli_rating);
extern "C" bool DexVmSessionEnqueueDrawer(uint32_t object_id, int32_t open);
extern "C" bool DexVmSessionEnqueueMenuClick(uint32_t object_id, int32_t index);
extern "C" bool DexVmSessionEnqueueFields(uint32_t object_id, int32_t a, int32_t b, int32_t c);

// 长按回灌：host 长按 -> objectId -> guest performLongClick。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewLongClick(JNIEnv* env, jclass,
                                                             jint object_id) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueLongClick(id)) {
    status = "queued objectId=" + std::to_string(id);
  } else {
    status = dexvm::bridge::InjectGuestObjectLongClick(nullptr, id);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// 文本输入回灌：host EditText 键入 -> objectId + UTF-8 文本 -> guest setText。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewText(JNIEnv* env, jclass, jint object_id,
                                                        jstring text) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string value;
  if (text != nullptr) {
    const char* utf = env->functions->GetStringUTFChars(env, text, nullptr);
    if (utf != nullptr) {
      value.assign(utf);
      env->functions->ReleaseStringUTFChars(env, text, utf);
    }
  }
  std::string status;
  if (DexVmSessionEnqueueText(id, value.data(), value.size())) {
    status = "queued objectId=" + std::to_string(id) + " text-len=" +
             std::to_string(value.size());
  } else {
    status = dexvm::bridge::InjectGuestObjectText(nullptr, id, value);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// SeekBar 拖拽回灌：host SeekBar -> objectId + progress/phase -> guest listener。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewSeek(JNIEnv* env, jclass, jint object_id,
                                                        jint progress, jint phase) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueSeek(id, progress, phase)) {
    status = "queued objectId=" + std::to_string(id) + " seek-phase=" +
             std::to_string(phase) + " value=" + std::to_string(progress);
  } else {
    status = dexvm::bridge::InjectGuestObjectSeek(nullptr, id, progress, phase);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// RatingBar 拖星回灌：host RatingBar -> objectId + milliRating -> guest listener。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewRating(JNIEnv* env, jclass, jint object_id,
                                                          jint milli_rating) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueRating(id, milli_rating)) {
    status = "queued objectId=" + std::to_string(id) + " rating-milli=" +
             std::to_string(milli_rating);
  } else {
    status = dexvm::bridge::InjectGuestObjectRating(nullptr, id, milli_rating);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// DrawerLayout 开合回灌：host 手势开合 -> objectId + open -> guest listener。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewDrawer(JNIEnv* env, jclass, jint object_id,
                                                          jboolean open) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueDrawer(id, open ? 1 : 0)) {
    status = "queued objectId=" + std::to_string(id) + " drawer-open=" +
             (open ? "1" : "0");
  } else {
    status = dexvm::bridge::InjectGuestObjectDrawer(nullptr, id, open == JNI_TRUE);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// Toolbar 菜单项点击回灌：host 真 Toolbar 溢出菜单 -> objectId + index。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewMenuClick(JNIEnv* env, jclass,
                                                             jint object_id, jint index) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueMenuClick(id, index)) {
    status = "queued objectId=" + std::to_string(id) + " menu-index=" +
             std::to_string(index);
  } else {
    status = dexvm::bridge::InjectGuestObjectMenuClick(nullptr, id, index);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// 选择器字段回灌：host 真 DatePicker/TimePicker -> objectId + (a,b,c)。
extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchViewFields(JNIEnv* env, jclass, jint object_id,
                                                          jint a, jint b, jint c) {
  uint32_t id = static_cast<uint32_t>(object_id);
  std::string status;
  if (DexVmSessionEnqueueFields(id, a, b, c)) {
    status = "queued objectId=" + std::to_string(id) + " fields=" + std::to_string(a) + "," +
             std::to_string(b) + "," + std::to_string(c);
  } else {
    status = dexvm::bridge::InjectGuestObjectFields(nullptr, id, a, b, c);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

// 弹层回灌（PopupMenu 菜单项点击 / PopupWindow 外点关闭）：token + kind + index。
extern "C" bool DexVmSessionEnqueuePopupEvent(uint32_t token, int32_t kind, int32_t index);

extern "C" JNIEXPORT jstring JNICALL
Java_com_dexvm_host_NativeBridge_nativeDispatchPopupEvent(JNIEnv* env, jclass, jint token,
                                                          jint kind, jint index) {
  uint32_t t = static_cast<uint32_t>(token);
  std::string status;
  if (DexVmSessionEnqueuePopupEvent(t, kind, index)) {
    status = "queued token=" + std::to_string(t) + " kind=" + std::to_string(kind) +
             " index=" + std::to_string(index);
  } else {
    status = dexvm::bridge::InjectGuestObjectPopup(nullptr, t, kind, index);
  }
  return env->functions->NewStringUTF(env, status.c_str());
}

#else  // !__ANDROID__（宿主测试构建不编本 TU 的 JNI 段）

namespace dexvm::bridge {
// 占位锚（非 Android 构建只用协议面，无 JNI 消费者）。
void UiBridgeJniHostAnchor() { RegisterUiBridgeProtocol(); }
}  // namespace dexvm::bridge

#endif
