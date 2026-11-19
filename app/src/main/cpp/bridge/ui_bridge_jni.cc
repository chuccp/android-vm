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

#else  // !__ANDROID__（宿主测试构建不编本 TU 的 JNI 段）

namespace dexvm::bridge {
// 占位锚（非 Android 构建只用协议面，无 JNI 消费者）。
void UiBridgeJniHostAnchor() { RegisterUiBridgeProtocol(); }
}  // namespace dexvm::bridge

#endif
