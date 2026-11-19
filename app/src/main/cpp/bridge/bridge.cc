// UI Bridge 最小面实现（docs 09 章 18.3）：首帧完成条件 + 桥轨 View 树
// present（缝A/缝B 消费面）。
// host present 确认为同步路径（frame-present callback 直落），envelope 全局
// 留存供 JNI 侧取走物化；自绘位图轨（PresentFrame/FrameBuffer）已退役。
#include "bridge/bridge.h"

#include <utility>

namespace dexvm::bridge {

namespace {

// 全局 draw sink / 缝B 导出器钩子（协议 TU 登记；未登记 = 自绘轨零行为）。
DrawSink* g_draw_sink = nullptr;
ViewTreeExporterFn g_view_tree_exporter = nullptr;

// 桥轨 present 全局留存（JNI nativeGetViewSnapshot 取走；本 TU 之外只读）。
std::vector<uint8_t> g_last_view_tree;

}  // namespace

void SetGlobalDrawSink(DrawSink* sink) { g_draw_sink = sink; }
DrawSink* GetDrawSink() { return g_draw_sink; }

void SetViewTreeExporter(ViewTreeExporterFn fn) { g_view_tree_exporter = fn; }
ViewTreeExporterFn GetViewTreeExporter() { return g_view_tree_exporter; }

const std::vector<uint8_t>* LastPresentedViewTree() {
  return g_last_view_tree.empty() ? nullptr : &g_last_view_tree;
}

void ClearLastPresentedViewTree() { g_last_view_tree.clear(); }

void UIBridge::MarkCondition(uint32_t conditions) {
  conditions_ |= conditions;
  // "mark first guest frame"：条件集齐瞬间打标（19.6 frame-present
  // callback -> mark first guest frame），之后保持。
  if (!first_frame_marked_ && first_frame_complete()) first_frame_marked_ = true;
}

bool UIBridge::PresentViewTree(std::vector<uint8_t> envelope) {
  // 空 envelope 拒绝（空产出无画可出）。
  if (envelope.empty()) return false;
  // 首个全量快照留存（envelope kind@6：1=full 2=patch；作 patch 基线/断言面）
  if (first_full_tree_.empty() && envelope.size() > 7 && envelope[6] == 1) {
    first_full_tree_ = envelope;
  }
  last_view_tree_ = std::move(envelope);
  view_tree_present_count_++;
  g_last_view_tree = last_view_tree_;
  // 桥轨 present 确认（frame-present 回调直落）-> 标记首帧条件。
  MarkCondition(kFramePresented);
  return true;
}

}  // namespace dexvm::bridge
