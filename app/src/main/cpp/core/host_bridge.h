// 宿主通道桥（framework 桥接底座）。
//
// core 只认函数指针；runtime_contract 在会话引导时装接（宿主 Java 侧实现 =
// com.dexvm.host.VmHostBridge）。全部在 dexvm-open 线程同步调用（该线程是
// 宿主 Java 线程，JNI 直连零跨线程）。
#pragma once

#include <string>

namespace dexvm::runtime {

struct HostBridgeOps {
  // 文字测量（px）：宽度 = 宿主 Paint.measureText。
  bool (*measure_text)(const std::u16string& text, float size_px, int style,
                       float letter_spacing, float* out_width);
  // 字体度量：ascent/descent/top/bottom（px）。
  bool (*font_metrics)(float size_px, int style, float* out_ascent, float* out_descent,
                       float* out_top, float* out_bottom);
  // 无障碍真实状态。
  bool (*accessibility_enabled)();
  bool (*accessibility_touch_exploration)();
  // 系统栏控制（宿主 WindowInsetsController）。
  void (*insets_show)(int types);
  void (*insets_hide)(int types);
  void (*insets_set_appearance)(int appearance, int mask);
  // 输入法。
  bool (*show_ime)();
  bool (*hide_ime)();
};

void SetHostBridgeOps(const HostBridgeOps* ops);
const HostBridgeOps* HostBridge();

}  // namespace dexvm::runtime
