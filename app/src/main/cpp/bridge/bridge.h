// UI Bridge 最小面（M4.2，docs 09 章 18.3）：桥轨 present（View 树
// 快照/patch + draw 命令流 envelope）+ 首帧完成条件状态机（docs 10 章 19.6
// 首帧完成条件：launcher Activity RESUMED、Guest Window attached、traversal
// complete、render submitted、Host surface frame presented）。
// 自绘渲染轨已退役（FrameBuffer 像素产出/PresentFrame 位图链下线）：
// PresentViewTree 是唯一 present 形制，宿主物化器消费 envelope 物化真
// android.view.View；Surface/fence/InputChannel/遮罩后置。
#pragma once

#include <cstdint>
#include <vector>

namespace dexvm::runtime {
struct VMState;
struct VObject;
}  // namespace dexvm::runtime

namespace dexvm::bridge {

class UIBridge;

// 首帧完成条件（docs 10 章 19.6）。
enum FirstFrameCondition : uint32_t {
  kActivityResumed = 1u << 0,
  kWindowAttached = 1u << 1,
  kTraversalComplete = 1u << 2,
  kRenderSubmitted = 1u << 3,
  kFramePresented = 1u << 4,
  kFirstFrameAll = 0x1f,
};

// ---- draw 命令记录 sink（缝A；docs 设计：drawText/Rect/Bitmap/save/restore/
// 矩阵进协议流）----
// Canvas 构造自动挂全局 sink（GetDrawSink）；未登记（自绘轨）= 零行为变化。
// 实现体在 bridge/ui_bridge_protocol.cc（协议流收集）。
class DrawSink {
 public:
  virtual ~DrawSink() = default;
  virtual void OnFrameBegin() {}
  virtual void OnSave() = 0;
  virtual void OnRestore() = 0;
  virtual void OnRestoreToCount(int32_t count) = 0;
  virtual void OnTranslate(float dx, float dy) = 0;
  virtual void OnConcatMatrix(const float m[6]) = 0;
  virtual void OnSetMatrix(const float m[6]) = 0;
  virtual void OnClipRect(float l, float t, float r, float b) = 0;
  virtual void OnFillRect(float l, float t, float r, float b, uint32_t argb) = 0;
  virtual void OnDrawText(const uint16_t* chars, int32_t len, int32_t x, int32_t y,
                          float text_size, uint32_t argb) = 0;
  virtual void OnDrawBitmap(int32_t sw, int32_t sh, int32_t sl, int32_t st, int32_t sr,
                            int32_t sb, float dl, float dt, float dr, float db,
                            const int32_t* pixels) = 0;
  virtual void OnDrawLine(float x0, float y0, float x1, float y1, float stroke,
                          uint32_t argb) = 0;
  virtual void OnDrawCircle(float cx, float cy, float r, bool fill, float stroke,
                            uint32_t argb) = 0;
};

// 全局 sink 登记（协议 TU 加载时设置）。
void SetGlobalDrawSink(DrawSink* sink);
DrawSink* GetDrawSink();

// ---- 缝B 导出器钩子（View 树快照/patch 导出）----
// 由 bridge/ui_bridge_protocol.cc 注册（app / ui_bridge 测试）；未注册时
// 自绘轨零行为变化。导出器内部完成 diff/序列化并 ui->PresentViewTree。
using ViewTreeExporterFn = bool (*)(dexvm::runtime::VMState* state,
                                    dexvm::runtime::VObject* content_view,
                                    int32_t width, int32_t height, UIBridge* ui);
void SetViewTreeExporter(ViewTreeExporterFn fn);
ViewTreeExporterFn GetViewTreeExporter();

class UIBridge {
 public:
  // 置位条件位（OR 语义）；条件集齐时打 "mark first guest frame" 标记（只标一次）。
  void MarkCondition(uint32_t conditions);
  uint32_t conditions() const { return conditions_; }

  // 首帧完成条件是否全部满足。
  bool first_frame_complete() const { return (conditions_ & kFirstFrameAll) == kFirstFrameAll; }
  // "mark first guest frame" 状态（条件集齐瞬间打标，之后保持）。
  bool first_frame_marked() const { return first_frame_marked_; }

  // 桥轨 present（缝A/缝B 产出：View 快照/patch + draw 命令流，序列化
  // envelope）。docs 09 章 18.3 的 View 树出画形制：宿主物化器消费 bytes
  // 物化真 android.view.View。空 envelope 拒绝；成功即 frame-present 确认
  // （置 kFramePresented + 首帧检查）并全局留存供 JNI 取走。
  bool PresentViewTree(std::vector<uint8_t> envelope);

  const std::vector<uint8_t>& last_view_tree() const { return last_view_tree_; }
  // 首个全量快照（便于消费端/考卷断言取完整树；patch 流以它为基线）
  const std::vector<uint8_t>& first_full_tree() const { return first_full_tree_; }
  int32_t view_tree_present_count() const { return view_tree_present_count_; }

 private:
  uint32_t conditions_ = 0;
  bool first_frame_marked_ = false;
  std::vector<uint8_t> last_view_tree_;
  std::vector<uint8_t> first_full_tree_;
  int32_t view_tree_present_count_ = 0;
};

// 全局留存面（JNI/宿主物化器取走；PresentViewTree 成功时更新）。
const std::vector<uint8_t>* LastPresentedViewTree();
void ClearLastPresentedViewTree();

}  // namespace dexvm::bridge
