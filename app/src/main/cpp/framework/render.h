// guest 渲染管线（M4.2，docs 10 章 19.4/19.6；自绘渲染轨已退役）：
//   View.measure(widthSpec,heightSpec) -> onMeasure（TextView 按 mText/mTextSize
//   估文字宽高；LinearLayout 纵向累加子 View）；
//   View.layout(l,t,r,b) -> onLayout（LinearLayout 纵向摆放子 View）；
//   View.draw(Canvas) -> onDraw（TextView 文字/背景命令；LinearLayout 递归
//   draw 子 View）。
//   ViewRootImpl 等价流程 performTraversals（measure->layout->draw）驱动 guest
//   绘制面——产出不再收敛为像素 FrameBuffer（自绘位图轨退役），收敛为缝A
//   draw 命令流（bridge::DrawSink 协议记录，宿主物化器出画）。
// 坐标约定：View.draw 被调时 Canvas 原点对齐该 View 左上角（ViewGroup.onDraw
// 按子 View mLeft/mTop 平移后再递归）。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "bridge/bridge.h"
#include "core/interpreter.h"
#include "framework/lifecycle_driver.h"

namespace dexvm::framework {

// ---- MeasureSpec（AOSP 打包：高 2 位 mode，低 30 位 size）----
enum MeasureSpecMode : int32_t {
  kUnspecified = 0,
  kExactly = 0x40000000,
  kAtMost = static_cast<int32_t>(0x80000000u),
};
int32_t MakeMeasureSpec(int32_t size, int32_t mode);
int32_t GetSpecSize(int32_t spec);
int32_t GetSpecMode(int32_t spec);

// Canvas：guest 绘制面（View.draw(Canvas) 的原生实现面）。
// 变换 = 3x2 仿射矩阵（同 android.graphics.Matrix 平面口径；translate/scale/
// rotate/skew/concat + save/restore 栈）；裁剪 = 视口矩形（clipRect 交叠）。
// Translate 语义同 android.graphics.Canvas.translate（dx/dy 取整累积）。
// 缝A 命令记录：构造自动挂 bridge::GetDrawSink（协议 TU 登记；未登记 =
// 零记录）；drawText/Rect/Bitmap/save/restore/矩阵进协议流。自绘像素
// 光栅化已退役——本类不再向 FrameBuffer 产像素（FrameBuffer 类型下线）；
// Canvas(Bitmap) 外置像素面（VM 堆 int[] 直写，guest Bitmap 绘制语义）保留。
class Canvas {
 public:
  explicit Canvas(int32_t width, int32_t height) : vp_w_(width), vp_h_(height) {
    InitClip();
    sink_ = dexvm::bridge::GetDrawSink();
  }
  Canvas() : vp_w_(0), vp_h_(0) {
    InitClip();
    sink_ = dexvm::bridge::GetDrawSink();
  }

  // 显式挂载/查询记录 sink（测试可替换；nullptr = 不记录）。
  void SetDrawSink(dexvm::bridge::DrawSink* sink) { sink_ = sink; }
  dexvm::bridge::DrawSink* draw_sink() const { return sink_; }
  // 帧起点（清命令流）；PerformTraversals draw 前调用。
  void BeginFrame() {
    if (sink_ != nullptr) sink_->OnFrameBegin();
  }

  void Translate(int32_t dx, int32_t dy) {
    origin_x_ += dx;
    origin_y_ += dy;
    m_[4] += dx;
    m_[5] += dy;
    if (sink_ != nullptr && !record_suppress_) {
      sink_->OnTranslate(static_cast<float>(dx), static_cast<float>(dy));
    }
  }
  void TranslateF(float dx, float dy);
  void Rotate(float degrees);
  void Rotate(float degrees, float px, float py);
  void Scale(float sx, float sy);
  void Scale(float sx, float sy, float px, float py);
  void Skew(float kx, float ky);
  void Concat(const float m[6]);
  void GetMatrix6(float out[6]) const;
  void SetMatrix6(const float m[6]);
  int32_t Save();
  void Restore();
  void RestoreToCount(int32_t count);
  bool ClipRectF(float l, float t, float r, float b);
  // Canvas(Bitmap)：外置像素面（VM 堆 int[] 直写）。
  void SetExternalPixels(uint32_t* px, int32_t w, int32_t h);
  int32_t Width() const;
  int32_t Height() const;
  bool GetClipBounds(int32_t out[4]) const;

  // 直接覆盖写（不做 alpha 混合；alpha<255 原样落像素）。坐标经矩阵映射 + 裁剪。
  // 仅落 Canvas(Bitmap) 外置像素面；缝A 命令照常记录。
  void FillRect(int32_t l, int32_t t, int32_t r, int32_t b, uint32_t argb);
  // drawText：缝A 命令记录（textSize 按 round(textSize/8) 整数倍网格口径度量）。
  // 8x8 字体光栅化已退役（不产像素）。
  void DrawText(const uint16_t* chars, int32_t len, int32_t x, int32_t y, float text_size,
                uint32_t argb);
  void DrawLineF(float x0, float y0, float x1, float y1, float stroke_w, uint32_t argb);
  void DrawCircleF(float cx, float cy, float r, bool fill, float stroke_w, uint32_t argb);
  // 位图块（src int[] ARGB 行优先）→ 目的矩形（最近邻；仅外置像素面）。
  void DrawBitmapBlock(const int32_t* src, int32_t sw, int32_t sh, int32_t sl, int32_t st,
                       int32_t sr, int32_t sb, float dl, float dt, float dr, float db);
  void FillRectF(float l, float t, float r, float b, uint32_t argb);

  int32_t origin_x() const { return origin_x_; }
  int32_t origin_y() const { return origin_y_; }

 private:
  struct SavedState {
    float m[6];
    int32_t cl, ct, cr, cb;
    int32_t ox, oy;
  };

  void InitClip();
  void MapPoint(float x, float y, float* ox, float* oy) const;
  uint32_t* RowPtr(int32_t y);
  void FillDevice(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t argb);

  // 视口尺寸（裁剪面；无像素面）。
  int32_t vp_w_ = 0;
  int32_t vp_h_ = 0;
  uint32_t* ext_pixels_ = nullptr;
  int32_t ext_w_ = 0;
  int32_t ext_h_ = 0;
  // 3x2 仿射：x' = a*x + c*y + e; y' = b*x + d*y + f（m_ = {a,b,c,d,e,f}）
  float m_[6] = {1, 0, 0, 1, 0, 0};
  int32_t clip_l_ = 0;
  int32_t clip_t_ = 0;
  int32_t clip_r_ = 0;
  int32_t clip_b_ = 0;
  std::vector<SavedState> stack_;
  int32_t origin_x_ = 0;
  int32_t origin_y_ = 0;
  // 缝A 记录（bridge::DrawSink；nullptr = 不记录）。
  dexvm::bridge::DrawSink* sink_ = nullptr;
  bool record_suppress_ = false;
};

// ---- 文字度量（8s 网格口径；textSize<=0 按 15sp 默认语义折算）----
// 字符格 8s x 8s（s = max(1, lround(textSize/8))），行高 10s。
int32_t TextScale(float text_size);
int32_t TextMeasureWidth(const uint16_t* chars, int32_t len, float text_size);
int32_t TextMeasureHeight(const uint16_t* chars, int32_t len, float text_size);

// 登记渲染面 guest 方法（measure/layout/draw/onMeasure/onLayout/onDraw/
// setMeasuredDimension/getXxx/setBackgroundColor/setTextColor + Canvas 类）
// 到 RegisterFrameworkBuiltins 建好的 View 类上（幂等：重复调用无副作用）。
void RegisterRenderPipeline(dexvm::runtime::ClassLinker* linker);

// 包一层 guest android.graphics.Canvas（mNativePtr 指向原生 Canvas）。
dexvm::runtime::VObject* WrapCanvas(dexvm::runtime::VMState& state, Canvas* native);

// ViewRootImpl 等价：performTraversals（measure->layout->draw，缝A 命令记录）。
// content_view == nullptr（setContentView 未调用）：不产出绘制面，返回 false。
bool PerformTraversals(dexvm::runtime::VMState& state, dexvm::runtime::VObject* content_view,
                       int32_t width, int32_t height);

// docs 19.6 首帧管线：addView→traversal→submit→present→mark first frame。
// activity = RESUMED launcher Activity（run() 经 FirstFrameHook 在
// onPostResume 后调入）；run_phase 与 LifecycleDriver phase 语义一致。
// 阶段名：window.attach / firstFrame.traversal / firstFrame.submit /
// firstFrame.present / firstFrame.marked。present = 缝B View 树导出
// （PresentViewTree 桥轨；导出器未注册/导出失败即管线失败）。失败写 *error
// 返回 false。
bool RunFirstFramePipeline(dexvm::runtime::VMState& state, dexvm::runtime::VObject* activity,
                           dexvm::bridge::UIBridge* ui, int32_t width, int32_t height,
                           const PhaseRunner& run_phase, std::string* error);

}  // namespace dexvm::framework
