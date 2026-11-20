// UI 桥接协议面（P1，docs 09 章 18.3 桥轨形制）：guest View 树快照/patch +
// draw 命令流的协议数据形、序列化、revision 管理与 viewId 映射。
//
// 通用 framework 桥接形态预留（View 只是第一类）：
//   HostHandle = {session_generation, object_id, kind}——kind 打头为 kView，
//   后续 Context 服务/Window/资源等非 View framework 对象复用同一句柄/属性/patch
//   编码（PropValue 泛型值 + PropKey 扩展点），无需改协议骨架。
//
// 线格式（小端）：
//   Envelope := magic "UIBR" u32 | u16 version | u8 kind | u8 reserved
//              | u32 revision | u32 snapshot_bytes | u32 draw_bytes
//              | snapshot 段 | draw 段
//   kind: 1 = 全量快照（新基线），2 = 增量 patch（base_revision -> revision）
// 本 TU 零外部依赖面（除 core 字段读取约定）；宿主物化器（Java）按同形解析。
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "bridge/bridge.h"
#include "core/vm_types.h"

namespace dexvm::bridge {

// ---- 通用宿主对象句柄（framework 桥接通用形）----

enum class HostObjectKind : uint8_t {
  kView = 1,           // android.view.View 族（P1 唯一实现类）
  kWindow = 2,         // 预留：Window/WindowManager 面
  kContextService = 3, // 预留：Context 服务桥（Clipboard/Vibrator/…）
  kResource = 4,       // 预留：资源对象（TypedValue/Configuration/…）
};

struct HostHandle {
  uint32_t object_id = 0;
  HostObjectKind kind = HostObjectKind::kView;
};

// ---- 属性（通用类型编码；后续对象族复用同一值形）----

enum class PropType : uint8_t {
  kInt = 1,
  kFloat = 2,
  kBool = 3,
  kString = 4,
  kRect = 5,   // (l,t,r,b) 4×i32
  kPoint = 6,  // (x,y) 2×i32
  kHandle = 7, // HostHandle（宿主对象互引）
  kBytes = 8,  // u32 len + bytes（资源文件字节；APK 图元出画面）
};

enum PropKey : uint16_t {
  kPropText = 1,              // kString  — mText
  kPropVisibility = 2,        // kInt     — mVisibility（0/4/8 同 Android）
  kPropBounds = 3,            // kRect    — mLeft/mTop/mRight/mBottom
  kPropPadding = 4,           // kRect    — mPadding{Left,Top,Right,Bottom}
  kPropTextSize = 5,          // kFloat   — mTextSize
  kPropTextColor = 6,         // kInt     — mTextColor（ARGB）
  kPropBackgroundColor = 7,   // kInt     — mBackgroundColor（ARGB）
  kPropAlpha = 8,             // kFloat   — mAlpha
  kPropScroll = 9,            // kPoint   — mScrollX/mScrollY
  kPropMeasured = 10,         // kPoint   — mMeasuredWidth/mMeasuredHeight
  kPropLayoutParams = 11,     // kPoint   — mLayoutParams.width/height
  kPropHasClickListener = 12, // kBool    — 桥接登记的 guest listener 存在
  kPropTranslation = 13,      // kPoint   — mTranslationX/mTranslationY
  kPropGravity = 14,          // kInt     — mGravity（资源布局 text/容器 gravity）
  kPropImageResource = 15,    // kInt     — ImageView.mImageResource（APK 资源 id）
  kPropImageBytes = 16,       // kBytes   — 资源字节（APK 内 PNG 等；宿主解码出画）
  kPropProgress = 17,         // kInt     — ProgressBar.mProgress
  kPropVideoPath = 18,        // kString  — VideoView.mVideoPath（宿主真播放）
  kPropVideoSeek = 19,        // kInt     — VideoView.mVideoSeek
  kPropVideoPlaying = 20,     // kBool    — VideoView.mVideoPlaying
  // 布局面（traversal 未收敛时宿主 measure/layout 的输入；已收敛时仅作辅助）。
  kPropLayoutGravity = 21,    // kInt     — ViewGroup.LayoutParams.gravity
  kPropOrientation = 22,      // kInt     — LinearLayout.mOrientation（0 H / 1 V）
  kPropLayoutMargins = 23,    // kRect    — LayoutParams 四边 margin
  kPropLayoutWeight = 24,     // kFloat   — LinearLayout$LayoutParams.weight
  kPropLayoutKind = 25,       // kInt     — 1 Linear / 2 Frame / 3 Relative /
                              //            4 ScrollView / 0 其他（宿主布局语义）
  kPropWindowDim = 26,        // kFloat   — 窗口根 dim（Dialog 叠加层遮罩 0..1；
                              //            仅额外根携带，宿主据此加 scrim）
  kPropChecked = 27,          // kBool    — CompoundButton/CheckedTextView.mChecked
                              //            （宿主 CheckBox/RadioButton/ToggleButton/Switch）
  kPropHasLongClickListener = 28, // kBool — 桥接登记的 guest OnLongClickListener 存在
                              //            （宿主据此装 long-click 回灌；缺省不装，
                              //             避免吞掉 ScrollView/ListView 的长按手势）
  kPropRating = 29,           // kFloat   — RatingBar.mRating（宿主原生星级）
  kPropNumStars = 30,         // kInt     — RatingBar.mNumStars
  kPropDrawerOpen = 31,       // kBool    — DrawerLayout.mDrawerOpen（guest 开合态；
                              //            宿主真 androidx DrawerLayout 联动，
                              //            宿主手势开合经回灌通道反向同步）
};

struct PropValue {
  PropType type = PropType::kInt;
  int32_t i = 0;
  float f = 0.0f;
  std::string s;
  int32_t rect[4] = {0, 0, 0, 0};
  HostHandle handle;
  std::vector<uint8_t> bytes;

  static PropValue Int(int32_t v);
  static PropValue Float(float v);
  static PropValue Bool(bool v);
  static PropValue String(std::string v);
  static PropValue Rect(int32_t l, int32_t t, int32_t r, int32_t b);
  static PropValue Point(int32_t x, int32_t y);
  static PropValue Bytes(std::vector<uint8_t> v);
  bool operator==(const PropValue& o) const;
};

struct Prop {
  uint16_t key = 0;
  PropValue value;
};

// ---- View 节点（第一类 HostNode；字段态）----

constexpr uint32_t kNoObject = 0xFFFFFFFFu;

struct ViewNode {
  HostHandle handle;
  int32_t guest_view_id = -1; // guest mId（-1 = NO_ID）
  std::string class_name;     // "TextView" / "LinearLayout" / "ViewGroup" / "ImageView" / "View"
  uint32_t parent_id = kNoObject; // kNoObject = 根
  uint32_t child_index = 0;
  std::vector<Prop> props;
  std::vector<uint32_t> children; // 子节点 object_id（展开序追加）
};

struct ViewSnapshot {
  uint32_t revision = 0;
  std::vector<ViewNode> nodes; // 先根展开序（父在子前）
};

// ---- 增量 patch ----

enum class PatchOpKind : uint8_t {
  kAdd = 1,    // 新增节点（props 全量；parent_id/child_index 定位）
  kRemove = 2, // 删除子树（按 object_id）
  kUpdate = 3, // 属性变更（props 只含变更项）
};

struct NodePatch {
  PatchOpKind op = PatchOpKind::kUpdate;
  HostHandle handle;
  int32_t guest_view_id = -1;
  std::string class_name; // kAdd 用
  uint32_t parent_id = kNoObject;
  uint32_t child_index = 0;
  std::vector<Prop> props;
};

struct ViewPatch {
  uint32_t base_revision = 0;
  uint32_t revision = 0;
  std::vector<NodePatch> ops;
};

// ---- draw 命令流（缝A：drawText/Rect/Bitmap/save/restore/矩阵进协议流）----

enum class DrawOp : uint8_t {
  kSave = 1,
  kRestore = 2,
  kRestoreToCount = 3,
  kTranslate = 4,
  kConcatMatrix = 5,
  kSetMatrix = 6,
  kClipRect = 7,
  kFillRect = 8,
  kDrawText = 9,
  kDrawBitmap = 10,
  kDrawLine = 11,
  kDrawCircle = 12,
  kFrameBegin = 13,
};

struct DrawCommand {
  DrawOp op = DrawOp::kSave;
  float f[4] = {0, 0, 0, 0};
  // i 槽位约定：text=(x,y)；restoreToCount=(count)；matrix=(e,f)定点4096；
  // bitmap=(sw,sh,sl,st,sr,sb)；line=(stroke 定点)；circle=(fill,stroke 定点)。
  int32_t i[8] = {0, 0, 0, 0, 0, 0, 0, 0};
  uint32_t color = 0;
  std::u16string text;        // kDrawText
  std::vector<uint32_t> bits; // kDrawBitmap（源像素紧密拷贝，ARGB8888 行主序）
};

// ---- 序列化 ----

// Envelope 编码（全量或 patch + draw 段）。
std::vector<uint8_t> SerializeFull(const ViewSnapshot& snap,
                                   const std::vector<DrawCommand>& draw);
std::vector<uint8_t> SerializePatch(const ViewPatch& patch,
                                    const std::vector<DrawCommand>& draw);
// 解码；失败返回 false（magic/version/长度校验）。
bool ParseEnvelope(const std::vector<uint8_t>& bytes, ViewSnapshot* full,
                   ViewPatch* patch, std::vector<DrawCommand>* draw,
                   uint32_t* revision, bool* is_patch);
// patch 应用到基线快照 -> 新快照（revision 取 patch.revision）。
bool ApplyPatch(const ViewSnapshot& base, const ViewPatch& patch, ViewSnapshot* out);
std::vector<DrawCommand> ParseDrawSegment(const uint8_t* data, uint32_t len);

// ---- revision 管理（全量/增量决策 + viewId 映射）----

class ViewTreeMirror {
 public:
  // 提交新树形 -> envelope 字节（首帧全量；之后增量，diff 膨胀（>全量）回退全量）。
  std::vector<uint8_t> Submit(const ViewSnapshot& next,
                              const std::vector<DrawCommand>& draw);
  uint32_t revision() const { return revision_; }
  const ViewSnapshot& last() const { return last_; }
  bool has_baseline() const { return has_baseline_; }
  void Reset();

 private:
  ViewSnapshot last_;
  bool has_baseline_ = false;
  uint32_t revision_ = 0;
};

// 全量 diff -> patch（无基线/差异过大由调用方回退全量）。
ViewPatch DiffSnapshots(const ViewSnapshot& base, const ViewSnapshot& next);

// viewId 映射（guest mId -> 桥内 object_id）。
std::map<int32_t, uint32_t> BuildViewIdMap(const ViewSnapshot& snap);
const ViewNode* FindByGuestId(const ViewSnapshot& snap, int32_t guest_view_id);

// ---- guest View 树 -> 快照（字段态：id/text/visibility/bounds/padding/…）----
// 字段读取走 framework_view.cc 字段面约定（8 字节槽，按名 FindInstanceField）。
ViewSnapshot BuildSnapshotFromGuest(dexvm::runtime::VMState& state,
                                    dexvm::runtime::VObject* content_view);

// ---- 缝A 记录 sink（bridge::DrawSink 实现体；协议流收集）----

class ProtocolDrawSink : public DrawSink {
 public:
  void OnFrameBegin() override;
  void OnSave() override;
  void OnRestore() override;
  void OnRestoreToCount(int32_t count) override;
  void OnTranslate(float dx, float dy) override;
  void OnConcatMatrix(const float m[6]) override;
  void OnSetMatrix(const float m[6]) override;
  void OnClipRect(float l, float t, float r, float b) override;
  void OnFillRect(float l, float t, float r, float b, uint32_t argb) override;
  void OnDrawText(const uint16_t* chars, int32_t len, int32_t x, int32_t y,
                  float text_size, uint32_t argb) override;
  void OnDrawBitmap(int32_t sw, int32_t sh, int32_t sl, int32_t st, int32_t sr,
                    int32_t sb, float dl, float dt, float dr, float db,
                    const int32_t* pixels) override;
  void OnDrawLine(float x0, float y0, float x1, float y1, float stroke,
                  uint32_t argb) override;
  void OnDrawCircle(float cx, float cy, float r, bool fill, float stroke,
                    uint32_t argb) override;

  const std::vector<DrawCommand>& stream() const { return stream_; }
  void Clear() { stream_.clear(); }

 private:
  std::vector<DrawCommand> stream_;
};

// 全局记录 sink（协议 TU 加载时登记进 bridge::SetGlobalDrawSink；Canvas 构造
// 自动挂载）。未链接本 TU = 自绘轨零行为变化。
ProtocolDrawSink& GlobalProtocolSink();
ViewTreeMirror& GlobalViewTreeMirror();
// 会话复位（镜像/命令流/映射清零；测试逐场景调用）。
void ResetUiBridgeProtocol();

// ---- 缝B 导出器（bridge::ViewTreeExporterFn 实现体）----
// traversal 后快照（字段态）-> diff/序列化 -> ui->PresentViewTree（桥轨 present，
// 确认即首帧条件）。加到 Bridge 注册面（加载即挂）。
bool ExportViewTree(dexvm::runtime::VMState* state,
                    dexvm::runtime::VObject* content_view, int32_t width,
                    int32_t height, UIBridge* ui);

// 模块加载自注册（SetViewTreeExporter + SetGlobalDrawSink；幂等）。
void RegisterUiBridgeProtocol();

// 全局 mirror/draw sink 复位（考卷/会话起点：下一次 present 走全量基线）。
void ResetUiBridgeProtocol();

}  // namespace dexvm::bridge
