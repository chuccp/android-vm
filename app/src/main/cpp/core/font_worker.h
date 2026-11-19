// 宿主字体 worker 通道（guest fiber -> 宿主 dexvm-font 线程）。
//
// 约束：guest 解释器运行在自研 fiber 栈上，ART 的 JNI 入口按 Java 线程栈
// 边界判定栈溢出——从 fiber 直接调宿主 JNI 会以 pending StackOverflowError
// abort（实测）。因此 guest 侧只用 native 队列同步，宿主 JNI 全部发生在
// 普通 Java 线程（FontBridge.workerLoop，Java->native 方向，ART 安全）。
#pragma once

#include <cstdint>
#include <string>

namespace dexvm::runtime {

enum HostFontOp : int {
  kFontOpCreateFamily = 1,
  kFontOpCreateFile = 2,
  kFontOpRestyle = 3,
  kFontOpMeasureText = 4,
  kFontOpFontMetrics = 5,
};

// guest 侧：发起请求并等待结果（timeout_ms 到期返回 false，调用方回落）。
bool RequestHostFont(int op, int style, float size, int64_t base, const std::string& str,
                     const std::u16string& text, int64_t* out_handle, float* out_width,
                     int32_t out_metrics[5], bool* out_metrics_ok, int timeout_ms = 3000);

// 宿主 worker 侧（runtime_contract JNI 入口调用）。
uint64_t HostFontNextRequest();  // 阻塞；0 = 无（当前实现不返回 0）
int HostFontOp(uint64_t id);
int HostFontStyle(uint64_t id);
float HostFontSize(uint64_t id);
int64_t HostFontBase(uint64_t id);
std::string HostFontString(uint64_t id);
std::u16string HostFontText(uint64_t id);
void HostFontComplete(uint64_t id, int64_t handle, float width, const int32_t* metrics,
                      bool metrics_ok);
bool HostFontWorkerAlive();

}  // namespace dexvm::runtime
