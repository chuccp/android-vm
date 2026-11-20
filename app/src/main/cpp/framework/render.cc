// guest 渲染管线实现（M4.2，docs 10 章 19.4/19.6；自绘渲染轨已退役）：
//   measure -> layout -> draw -> 缝A draw 命令流（bridge::DrawSink 协议记录，
//   display list 的收敛形态；不再产出像素 FrameBuffer，8x8 字体光栅化下线）。
// 文字度量：8s 网格口径（s = round(textSize/8)），行高 10s；'\n' 换行。
// guest 方法面：View.measure/layout/draw/onMeasure/onLayout/onDraw/
// setMeasuredDimension/getXxx + TextView.onMeasure/onDraw/setTextColor +
// ViewGroup.onDraw（递归子 View）+ LinearLayout.onMeasure/onLayout（纵向）
// + android.graphics.Canvas（mNativePtr）。全部挂 intrinsic（可覆写、super
// 可调），登记见 RegisterRenderPipeline（须在 RegisterFrameworkBuiltins 后）。
#include "framework/render.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "bridge/ui_bridge_input.h"
#include "core/class_linker.h"
#include "core/intrinsic.h"

// 桥接缝（core/framework_view.cc 实现体）：setOnClickListener NoOp -> 监听器
// 登记（viewId -> guest listener 注入面，bridge/ui_bridge_input.h 契约）。
namespace dexvm::runtime {
void ClearGuestLayoutRequested(VObject* v);
// Drawable 资源侧表（core/framework_app.cc）：每 VM 引导清表。
void ResetGuestDrawableResources();
Value ViewSetOnClickListener
(VMState& state, VObject* self, const Value* args, size_t n, bool* ok);
// 宿主真字体度量桥（core/framework_font.cc；false=不可用回落网格）
bool HostMeasureTextUtf16(const uint16_t* chars, int32_t len, float text_size,
                          float* out_width);
bool HostFontMetricsInt(float text_size, int32_t out[5]);
// Activity.addContentView 覆盖层登记（core/framework_inflate.cc 实现体）
std::vector<VObject*> AddedContentViews(VObject* activity);
// 窗口焦点语义（core/framework_view.cc 实现体）
void SetGuestWindowFocused(bool focused);
bool DispatchWindowFocusChanged(VMState& state, VObject* view, bool focused);
// 视图 attach / 窗口可见性语义（AOSP dispatchAttachedToWindow）
void SetGuestViewAttached(bool attached);
bool DispatchViewAttached(VMState& state, VObject* view, bool attached);
// ViewTreeObserver 派发（core/framework_view.cc 实现体）：AOSP performTraversals
// 布局后 dispatchOnGlobalLayout——app 常以此作为内容容器创建闸门。
void DispatchViewTreeGlobalLayout(VMState& state, VObject* root);
bool DispatchViewTreePreDraw(VMState& state, VObject* root);
}  // namespace dexvm::runtime

namespace dexvm::framework {

namespace {

using dexvm::runtime::ClassLinker;
using dexvm::runtime::IntrinsicFn;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::ToIntrinsicSlot;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccPrivate = 0x2;

// ---- 对象字段槽辅助（8 字节槽，同 core/framework_builtins.cc 约定） ----

VField* FindInstanceField(VClass* klass, const char* name) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == name) return &f;
    }
  }
  return nullptr;
}

ObjHeader* GetRefField(VObject* o, uint32_t off) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}

int32_t GetIField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}

void SetIField(VObject* o, uint32_t off, int32_t v) {
  uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
  std::memcpy(o->field_data() + off, &slot, sizeof(slot));
}

float GetFField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  float v = 0;
  std::memcpy(&v, &slot, sizeof(v));
  return v;
}

int64_t GetLField(VObject* o, uint32_t off) {
  int64_t v = 0;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}

void SetLField(VObject* o, uint32_t off, int64_t v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

ObjHeader* GetArrRef(VArray* a, int32_t i) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, a->data() + static_cast<size_t>(i) * 8, sizeof(v));
  return v;
}

// 虚分发一次（对照 core/framework_builtins.cc CallVirtual）。
bool CallVirtual(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
                 const char* ret, const Value* args, size_t n, Value* out) {
  if (obj == nullptr || obj->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "call virtual on null");
    return false;
  }
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", name);
    return false;
  }
  std::vector<Value> full;
  full.reserve(n + 1);
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!dexvm::runtime::InvokeMethod(state, m, full.data(), full.size(), &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

// ---- View 字段读写（读不到按 0/空，字段面见 framework_builtins） ----

int32_t ViewI(VObject* v, const char* name) {
  VField* f = FindInstanceField(v->klass, name);
  return f != nullptr ? GetIField(v, f->offset) : 0;
}

float ViewF(VObject* v, const char* name) {
  VField* f = FindInstanceField(v->klass, name);
  return f != nullptr ? GetFField(v, f->offset) : 0.0f;
}

ObjHeader* ViewRef(VObject* v, const char* name) {
  VField* f = FindInstanceField(v->klass, name);
  return f != nullptr ? GetRefField(v, f->offset) : nullptr;
}

void SetMeasured(VObject* v, int32_t w, int32_t h) {
  VField* wf = FindInstanceField(v->klass, "mMeasuredWidth");
  VField* hf = FindInstanceField(v->klass, "mMeasuredHeight");
  if (wf != nullptr) SetIField(v, wf->offset, w);
  if (hf != nullptr) SetIField(v, hf->offset, h);
}

// mText（CharSequence 骨架即 VString）-> UTF-16 chars。
VString* ViewText(VObject* v) {
  ObjHeader* s = ViewRef(v, "mText");
  return reinterpret_cast<VString*>(s);
}

VArray* ViewChildren(VObject* v) {
  return reinterpret_cast<VArray*>(ViewRef(v, "mChildren"));
}

// ---- MeasureSpec ----

int32_t ResolveSize(int32_t desired, int32_t spec) {
  int32_t size = GetSpecSize(spec);
  switch (GetSpecMode(spec)) {
    case kExactly:
      return size;
    case kAtMost:
      return desired < size ? desired : size;
    default:
      return desired;
  }
}

// AOSP getDefaultSize(0, spec)：UNSPECIFIED -> 0，否则 specSize。
int32_t DefaultSize(int32_t spec) {
  return GetSpecMode(spec) == kUnspecified ? 0 : GetSpecSize(spec);
}

// ---- guest 方法 intrinsic ----

Value NoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// View.measure(widthSpec, heightSpec) -> onMeasure（可覆写）。
Value ViewMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 2) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "measure needs 2 specs");
    *ok = false;
    return Value{};
  }
  if (!CallVirtual(state, self, "onMeasure", {"I", "I"}, "V", args, 2, nullptr)) *ok = false;
  return Value{};
}

// View.onMeasure 默认（AOSP View.onMeasure = getDefaultSize）。
Value ViewOnMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 2) {
    *ok = false;
    return Value{};
  }
  SetMeasured(self, DefaultSize(args[0].i), DefaultSize(args[1].i));
  return Value{};
}

Value SetMeasuredDimension(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 2) SetMeasured(self, args[0].i, args[1].i);
  return Value{};
}

Value GetMeasuredWidth(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = ViewI(self, "mMeasuredWidth");
  return r;
}

Value GetMeasuredHeight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = ViewI(self, "mMeasuredHeight");
  return r;
}

// View.getMeasuredState()：AOSP 打包态位（高字节 = width state；height state 左移）
Value GetMeasuredState(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  int32_t w = ViewI(self, "mMeasuredWidth");
  int32_t h = ViewI(self, "mMeasuredHeight");
  r.i = (w & static_cast<int32_t>(0xFF000000u)) |
        ((h >> 16) & static_cast<int32_t>(0x0000FF00u));
  return r;
}

Value GetMeasuredWidthAndState(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = ViewI(self, "mMeasuredWidth");
  return r;
}

Value GetMeasuredHeightAndState(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = ViewI(self, "mMeasuredHeight");
  return r;
}

Value GetMinimumSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};  // AOSP 默认 suggestMinimum = 0（无背景）
}

Value GetWidth(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = ViewI(self, "mRight") - ViewI(self, "mLeft");
  return r;
}

Value GetHeight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = ViewI(self, "mBottom") - ViewI(self, "mTop");
  return r;
}

// View.layout(l, t, r, b) -> 存 bounds -> onLayout(changed, l, t, r, b)。
Value ViewLayout(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 4) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "layout needs l,t,r,b");
    *ok = false;
    return Value{};
  }
  int32_t l = args[0].i, t = args[1].i, r = args[2].i, b = args[3].i;
  bool changed = ViewI(self, "mLeft") != l || ViewI(self, "mTop") != t ||
                 ViewI(self, "mRight") != r || ViewI(self, "mBottom") != b;
  VField* names[4] = {FindInstanceField(self->klass, "mLeft"),
                      FindInstanceField(self->klass, "mTop"),
                      FindInstanceField(self->klass, "mRight"),
                      FindInstanceField(self->klass, "mBottom")};
  int32_t vals[4] = {l, t, r, b};
  for (int i = 0; i < 4; i++) {
    if (names[i] != nullptr) SetIField(self, names[i]->offset, vals[i]);
  }
  Value oa[5] = {};
  oa[0].i = changed ? 1 : 0;
  oa[1].i = l;
  oa[2].i = t;
  oa[3].i = r;
  oa[4].i = b;
  if (!CallVirtual(state, self, "onLayout", {"Z", "I", "I", "I", "I"}, "V", oa, 5, nullptr)) {
    *ok = false;
  }
  // AOSP layout：清除 FORCE_LAYOUT（isLayoutRequested 状态位投影）。
  ClearGuestLayoutRequested(self);
  return Value{};
}

Value SetBackgroundColor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  VField* f = FindInstanceField(self->klass, "mBackgroundColor");
  if (f != nullptr && n >= 1) SetIField(self, f->offset, args[0].i);
  return Value{};
}

Value SetTextColor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  VField* f = FindInstanceField(self->klass, "mTextColor");
  if (f != nullptr && n >= 1) SetIField(self, f->offset, args[0].i);
  return Value{};
}

// 原生 Canvas 解包（android.graphics.Canvas.mNativePtr）。
Canvas* UnwrapCanvas(VMState& state, ObjHeader* ref) {
  if (ref == nullptr) return nullptr;
  VObject* o = reinterpret_cast<VObject*>(ref);
  VField* f = FindInstanceField(o->klass, "mNativePtr");
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "canvas without mNativePtr");
    return nullptr;
  }
  return reinterpret_cast<Canvas*>(GetLField(o, f->offset));
}

// View.draw(Canvas)：背景铺底 + onDraw（可覆写）。
// 约定：draw 被调时 Canvas 原点对齐该 View 左上角。
Value ViewDraw(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "draw canvas == null");
    *ok = false;
    return Value{};
  }
  Canvas* cx = UnwrapCanvas(state, args[0].ref);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  int32_t w = ViewI(self, "mRight") - ViewI(self, "mLeft");
  int32_t h = ViewI(self, "mBottom") - ViewI(self, "mTop");
  uint32_t bg = static_cast<uint32_t>(ViewI(self, "mBackgroundColor"));
  if (bg != 0 && w > 0 && h > 0) cx->FillRect(0, 0, w, h, bg);
  if (!CallVirtual(state, self, "onDraw", {"Landroid/graphics/Canvas;"}, "V", args, 1, nullptr)) {
    *ok = false;
  }
  return Value{};
}

// TextView.onMeasure：mText/mTextSize 估宽高 + padding，按 MeasureSpec 收敛。
Value TVOnMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 2) {
    *ok = false;
    return Value{};
  }
  VString* text = ViewText(self);
  const uint16_t* chars = text != nullptr ? text->chars() : nullptr;
  int32_t len = text != nullptr ? text->length : 0;
  float size = ViewF(self, "mTextSize");
  const int32_t pad_w = ViewI(self, "mPaddingLeft") + ViewI(self, "mPaddingRight");
  const int32_t pad_h = ViewI(self, "mPaddingTop") + ViewI(self, "mPaddingBottom");
  int32_t desired_w = 0;
  int32_t desired_h = 0;
  bool bridged = false;
  // 宿主真字体度量优先（AOSP：文本 shaping/measure 在平台 native）；逐行
  // 取最大行宽 + FontMetricsInt 行高。桥不可用（桌面单测）回落网格口径。
  if (chars != nullptr && len > 0) {
    int32_t lines = 1;
    int32_t max_line_w = 0;
    int32_t start = 0;
    bool ok = true;
    for (int32_t i = 0; i <= len; i++) {
      if (i != len && chars[i] != u'\n') continue;
      float w = 0.0f;
      if (!dexvm::runtime::HostMeasureTextUtf16(chars + start, i - start, size, &w)) {
        ok = false;
        break;
      }
      max_line_w = std::max(max_line_w, static_cast<int32_t>(std::ceil(w)));
      if (i != len) lines++;
      start = i + 1;
    }
    int32_t fm[5] = {0, 0, 0, 0, 0};
    if (ok && dexvm::runtime::HostFontMetricsInt(size, fm)) {
      desired_w = max_line_w + pad_w;
      desired_h = lines * std::max(fm[3] - fm[0], 1) + pad_h;
      bridged = true;
    }
  }
  if (!bridged) {
    desired_w = TextMeasureWidth(chars, len, size) + pad_w;
    desired_h = TextMeasureHeight(chars, len, size) + pad_h;
  }
  SetMeasured(self, ResolveSize(desired_w, args[0].i), ResolveSize(desired_h, args[1].i));
  return Value{};
}

// ProgressBar.onMeasure：横向进度条最小固有高度 8px（AOSP 主题样式近似面；
// 宿主物化 ProgressBar 用真实控件自绘）。
Value PBOnMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 2) {
    *ok = false;
    return Value{};
  }
  int32_t w = GetSpecMode(args[0].i) == kUnspecified ? 0 : GetSpecSize(args[0].i);
  int32_t h = ResolveSize(8, args[1].i);
  SetMeasured(self, w, h);
  return Value{};
}

// SeekBar.onMeasure：AOSP AbsSeekBar 视觉高度（48dp @ DisplayMetrics.density=2
// = 96px）；轨道/thumb 由宿主真 SeekBar 物化绘制。
Value SBOnMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 2) {
    *ok = false;
    return Value{};
  }
  int32_t w = GetSpecMode(args[0].i) == kUnspecified ? 0 : GetSpecSize(args[0].i);
  int32_t h = ResolveSize(96, args[1].i);
  SetMeasured(self, w, h);
  return Value{};
}

// TextView.onDraw：背景外的文字 drawText 命令（起点 padding 左上）。
Value TVOnDraw(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1) {
    *ok = false;
    return Value{};
  }
  Canvas* cx = UnwrapCanvas(state, args[0].ref);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  VString* text = ViewText(self);
  if (text == nullptr || text->length <= 0) return Value{};
  uint32_t color = static_cast<uint32_t>(ViewI(self, "mTextColor"));
  cx->DrawText(text->chars(), text->length, ViewI(self, "mPaddingLeft"), ViewI(self, "mPaddingTop"),
               ViewF(self, "mTextSize"), color);
  return Value{};
}

// ViewGroup.onDraw：递归 draw 子 View（按 mLeft/mTop 平移；docs 19.4 draw 递归）。
Value VGOnDraw(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1) {
    *ok = false;
    return Value{};
  }
  Canvas* cx = UnwrapCanvas(state, args[0].ref);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  VArray* children = ViewChildren(self);
  if (children == nullptr) return Value{};
  for (int32_t i = 0; i < children->length; i++) {
    ObjHeader* ref = GetArrRef(children, i);
    if (ref == nullptr) continue;
    VObject* child = reinterpret_cast<VObject*>(ref);
    int32_t l = ViewI(child, "mLeft");
    int32_t t = ViewI(child, "mTop");
    cx->Translate(l, t);
    bool child_ok =
        CallVirtual(state, child, "draw", {"Landroid/graphics/Canvas;"}, "V", args, 1, nullptr);
    cx->Translate(-l, -t);
    if (!child_ok) {
      *ok = false;
      return Value{};
    }
  }
  return Value{};
}

// 子 View 布局参数（资源布局面：LinearLayout$LayoutParams/MarginLayoutParams）。
// width/height：-1 match_parent / -2 wrap_content / 像素；margin 四向。
struct ChildLp {
  int32_t width = -2;
  int32_t height = -2;
  int32_t ml = 0;
  int32_t mt = 0;
  int32_t mr = 0;
  int32_t mb = 0;
  float weight = 0.0f;
};

ChildLp ReadChildLp(VObject* child) {
  ChildLp lp;
  ObjHeader* lp_ref = ViewRef(child, "mLayoutParams");
  VObject* o = reinterpret_cast<VObject*>(lp_ref);
  if (o == nullptr) return lp;
  auto rd = [&](const char* name, int32_t def) {
    VField* f = FindInstanceField(o->klass, name);
    return f != nullptr ? GetIField(o, f->offset) : def;
  };
  lp.width = rd("width", -2);
  lp.height = rd("height", -2);
  lp.ml = rd("leftMargin", 0);
  lp.mt = rd("topMargin", 0);
  lp.mr = rd("rightMargin", 0);
  lp.mb = rd("bottomMargin", 0);
  if (VField* wf = FindInstanceField(o->klass, "weight")) {
    lp.weight = GetFField(o, wf->offset);
  }
  return lp;
}

int32_t ChildWidthSpec(const ChildLp& lp, int32_t avail_w) {
  if (avail_w < 0) avail_w = 0;
  if (lp.width >= 0) return MakeMeasureSpec(lp.width, kExactly);
  if (lp.width == -1) return MakeMeasureSpec(avail_w, kExactly);  // match_parent
  return MakeMeasureSpec(avail_w, kAtMost);                       // wrap_content
}

int32_t ChildHeightSpec(const ChildLp& lp, int32_t avail_h) {
  if (lp.height >= 0) return MakeMeasureSpec(lp.height, kExactly);
  if (lp.height == -1) {
    // match_parent：父约束为精确时子亦精确（AOSP getChildMeasureSpec 同口径；
    // DrawerLayout 等要求 EXACTLY，之前 AT_MOST 近似被拒）
    return MakeMeasureSpec(std::max(avail_h, 0), kExactly);
  }
  return MakeMeasureSpec(0, kUnspecified);  // wrap_content：内容自量
}

// AOSP getChildMeasureSpec：父 spec + padding/margins/used + 子尺寸语义
int32_t ChildMeasureSpecFull(int32_t parent_spec, int32_t padding, int32_t margin_start,
                             int32_t margin_end, int32_t used, int32_t child_dimension) {
  int32_t parent_mode = GetSpecMode(parent_spec);
  int32_t parent_size = GetSpecSize(parent_spec);
  int32_t size = parent_size - padding - margin_start - margin_end - used;
  if (size < 0) size = 0;
  if (child_dimension >= 0) return MakeMeasureSpec(child_dimension, kExactly);
  if (child_dimension == -1) {  // MATCH_PARENT
    // AOSP getChildMeasureSpec：父 AT_MOST 时 match_parent 子项也是 AT_MOST
    // （可选范围而非强制相等）；只有父 EXACTLY 才下传 EXACTLY。
    if (parent_mode == kUnspecified) return MakeMeasureSpec(0, kUnspecified);
    return MakeMeasureSpec(size, parent_mode == kExactly ? kExactly : kAtMost);
  }
  // WRAP_CONTENT
  return parent_mode == kUnspecified ? MakeMeasureSpec(0, kUnspecified)
                                     : MakeMeasureSpec(size, kAtMost);
}

// ViewGroup.measureChildWithMargins(View, wUsed, wSpec, hUsed, hSpec)：AOSP 公开
// 方法面（DrawerLayout/FrameLayout/RelativeLayout 等 onMeasure 直接调用）。
Value VGMeasureChildWithMargins(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  *ok = true;
  if (n < 5 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "measureChildWithMargins args");
    *ok = false;
    return Value{};
  }
  VObject* child = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  ChildLp lp = ReadChildLp(child);
  // AOSP ViewGroup.measureChildWithMargins(child, parentWidthSpec, widthUsed,
  // parentHeightSpec, heightUsed)：参数位是 (1=wSpec, 2=wUsed, 3=hSpec, 4=hUsed)。
  // 之前把 args[2]/args[4]（used）当父 spec → 子项收到 UNSPECIFIED(0)，
  // LinearLayoutCompat/DrawerLayout 等整棵子树高度塌成 0（BottomNav 覆盖层
  // 反被绝对布局撑成全屏的根因）。
  int32_t wspec = ChildMeasureSpecFull(args[1].i,
                                       ViewI(self, "mPaddingLeft") + ViewI(self, "mPaddingRight"),
                                       lp.ml, lp.mr, args[2].i, lp.width);
  int32_t hspec = ChildMeasureSpecFull(args[3].i,
                                       ViewI(self, "mPaddingTop") + ViewI(self, "mPaddingBottom"),
                                       lp.mt, lp.mb, args[4].i, lp.height);
  Value cargs[2] = {};
  cargs[0].i = wspec;
  cargs[1].i = hspec;
  if (!CallVirtual(state, child, "measure", {"I", "I"}, "V", cargs, 2, nullptr)) {
    *ok = false;
  }
  return Value{};
}

// ViewGroup.measureChild(View, wSpec, hSpec)
Value VGMeasureChild(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 3 || args[0].ref == nullptr) {
    *ok = false;
    return Value{};
  }
  VObject* child = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  ChildLp lp = ReadChildLp(child);
  Value cargs[2] = {};
  cargs[0].i = ChildMeasureSpecFull(args[1].i,
                                    ViewI(self, "mPaddingLeft") + ViewI(self, "mPaddingRight"),
                                    0, 0, 0, lp.width);
  cargs[1].i = ChildMeasureSpecFull(args[2].i,
                                    ViewI(self, "mPaddingTop") + ViewI(self, "mPaddingBottom"),
                                    0, 0, 0, lp.height);
  if (!CallVirtual(state, child, "measure", {"I", "I"}, "V", cargs, 2, nullptr)) *ok = false;
  return Value{};
}


// LinearLayout.onMeasure：AOSP 主轴口径——VERTICAL 累加高度、HORIZONTAL 累加
// 宽度（此前不分方向一律纵向累加：AppBar 搜索行等水平容器第二个子项收到
// AT_MOST(0) 高度，整行塌成 0 高）。权重按主轴分配剩余空间。
Value LLOnMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 2) {
    *ok = false;
    return Value{};
  }
  int32_t wspec = args[0].i;
  int32_t hspec = args[1].i;
  int32_t pad_l = ViewI(self, "mPaddingLeft");
  int32_t pad_t = ViewI(self, "mPaddingTop");
  int32_t pad_r = ViewI(self, "mPaddingRight");
  int32_t pad_b = ViewI(self, "mPaddingBottom");
  int32_t content_w = GetSpecSize(wspec) - pad_l - pad_r;
  int32_t content_h = GetSpecSize(hspec) - pad_t - pad_b;
  if (content_w < 0) content_w = 0;
  if (content_h < 0) content_h = 0;
  int32_t orientation = ViewI(self, "mOrientation");
  bool horizontal = orientation != 1;  // AOSP HORIZONTAL=0 / VERTICAL=1
  int32_t max_child_w = 0;
  int32_t max_child_h = 0;
  int32_t sum_child_w = 0;
  int32_t sum_child_h = 0;
  float total_weight = 0.0f;
  VArray* children = ViewChildren(self);
  if (children != nullptr) {
    // 第一遍：非权重子 View 常规测量（0dp + weight 主轴占位子跳过）
    for (int32_t i = 0; i < children->length; i++) {
      ObjHeader* ref = GetArrRef(children, i);
      if (ref == nullptr) continue;
      VObject* child = reinterpret_cast<VObject*>(ref);
      if (ViewI(child, "mVisibility") == 8) continue;  // GONE：不参与 measure（AOSP）
      ChildLp lp = ReadChildLp(child);
      if (lp.weight > 0.0f && (horizontal ? lp.width : lp.height) == 0) {
        total_weight += lp.weight;  // 0dp + weight：主轴剩余空间分配
        continue;
      }
      Value cargs[2] = {};
      // AOSP getChildMeasureSpec：主轴已用空间逐子扣减；交叉轴不做累加。
      cargs[0].i = ChildMeasureSpecFull(wspec, pad_l + pad_r, lp.ml, lp.mr,
                                        horizontal ? sum_child_w : 0, lp.width);
      cargs[1].i = ChildMeasureSpecFull(hspec, pad_t + pad_b, lp.mt, lp.mb,
                                        horizontal ? 0 : sum_child_h, lp.height);
      if (!CallVirtual(state, child, "measure", {"I", "I"}, "V", cargs, 2, nullptr)) {
        *ok = false;
        return Value{};
      }
      max_child_w = std::max(max_child_w, ViewI(child, "mMeasuredWidth") + lp.ml + lp.mr);
      max_child_h = std::max(max_child_h, ViewI(child, "mMeasuredHeight") + lp.mt + lp.mb);
      sum_child_w += lp.ml + ViewI(child, "mMeasuredWidth") + lp.mr;
      sum_child_h += lp.mt + ViewI(child, "mMeasuredHeight") + lp.mb;
    }
    // 第二遍：权重子 View 分主轴剩余空间（AOSP：剩余 = 父内容 - 已用）
    if (total_weight > 0.0f) {
      int32_t remaining = (horizontal ? content_w - sum_child_w : content_h - sum_child_h);
      if (remaining < 0) remaining = 0;
      for (int32_t i = 0; i < children->length; i++) {
        ObjHeader* ref = GetArrRef(children, i);
        if (ref == nullptr) continue;
        VObject* child = reinterpret_cast<VObject*>(ref);
        if (ViewI(child, "mVisibility") == 8) continue;
        ChildLp lp = ReadChildLp(child);
        if (!(lp.weight > 0.0f && (horizontal ? lp.width : lp.height) == 0)) continue;
        int32_t share = static_cast<int32_t>(
            static_cast<float>(remaining) * (lp.weight / total_weight));
        Value cargs[2] = {};
        cargs[0].i =
            horizontal
                ? MakeMeasureSpec(std::max(share - lp.ml - lp.mr, 0), kExactly)
                : ChildMeasureSpecFull(wspec, pad_l + pad_r, lp.ml, lp.mr, 0, lp.width);
        cargs[1].i =
            horizontal
                ? ChildMeasureSpecFull(hspec, pad_t + pad_b, lp.mt, lp.mb, 0, lp.height)
                : MakeMeasureSpec(std::max(share - lp.mt - lp.mb, 0), kExactly);
        if (!CallVirtual(state, child, "measure", {"I", "I"}, "V", cargs, 2, nullptr)) {
          *ok = false;
          return Value{};
        }
        max_child_w = std::max(max_child_w, ViewI(child, "mMeasuredWidth") + lp.ml + lp.mr);
        max_child_h = std::max(max_child_h, ViewI(child, "mMeasuredHeight") + lp.mt + lp.mb);
        if (horizontal) sum_child_w += lp.ml + ViewI(child, "mMeasuredWidth") + lp.mr;
        else sum_child_h += lp.mt + ViewI(child, "mMeasuredHeight") + lp.mb;
      }
    }
  }
  int32_t desired_w = (horizontal ? sum_child_w : max_child_w) + pad_l + pad_r;
  int32_t desired_h = (horizontal ? max_child_h : sum_child_h) + pad_t + pad_b;
  SetMeasured(self, ResolveSize(desired_w, wspec), ResolveSize(desired_h, hspec));
  return Value{};
}

// LinearLayout.onLayout：自 padding 左上起纵向摆放子 View（边距/水平 gravity）。
Value LLOnLayout(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  *ok = true;
  int32_t l = n >= 4 ? args[1].i : 0;
  int32_t t = n >= 4 ? args[2].i : 0;
  int32_t r = n >= 4 ? args[3].i : 0;
  (void)t;  // 子项已按父容器相对坐标摆放（AOSP onLayout 口径）
  if (n < 4) {
    // 直接调用（非 super.onLayout）时按自身 bounds 推算
    r = ViewI(self, "mRight");
    t = ViewI(self, "mTop");
  }
  int32_t pad_l = ViewI(self, "mPaddingLeft");
  int32_t pad_t = ViewI(self, "mPaddingTop");
  int32_t pad_r = ViewI(self, "mPaddingRight");
  int32_t content_w = r - l - pad_l - pad_r;
  if (content_w < 0) content_w = 0;
  int32_t gravity = ViewI(self, "mGravity");
  int32_t content_h = ViewI(self, "mBottom") - ViewI(self, "mTop") - pad_t -
                      ViewI(self, "mPaddingBottom");
  if (content_h < 0) content_h = 0;
  bool horizontal = ViewI(self, "mOrientation") != 1;
  // 相对父容器坐标口径（同 ContainerOnLayout）：paddings 起算，不加 l/t。
  int32_t y = pad_t;
  int32_t x = pad_l;
  VArray* children = ViewChildren(self);
  if (children == nullptr) return Value{};
  for (int32_t i = 0; i < children->length; i++) {
    ObjHeader* ref = GetArrRef(children, i);
    if (ref == nullptr) continue;
    VObject* child = reinterpret_cast<VObject*>(ref);
    if (ViewI(child, "mVisibility") == 8) continue;  // GONE：不参与 layout
    ChildLp lp = ReadChildLp(child);
    int32_t cw = ViewI(child, "mMeasuredWidth");
    int32_t ch = ViewI(child, "mMeasuredHeight");
    int32_t cx;
    int32_t cy;
    if (horizontal) {
      // 主轴横向顺序摆放；交叉轴按子项 gravity 垂直居中/置底（AOSP 近似）。
      cx = x + lp.ml;
      cy = pad_t + lp.mt;
      int32_t child_gravity = 0;
      if (VObject* lpobj = reinterpret_cast<VObject*>(ViewRef(child, "mLayoutParams"))) {
        child_gravity = ViewI(lpobj, "gravity");
      }
      if ((child_gravity & 0x70) == 0x10) {  // CENTER_VERTICAL
        cy = pad_t + std::max(0, (content_h - ch) / 2);
      }
      x = cx + cw + lp.mr;
    } else {
      int32_t cxx = pad_l + lp.ml;
      if ((gravity & 0x07) == 0x01 && lp.width != -1) {  // Gravity.CENTER_HORIZONTAL
        cxx = pad_l + std::max(0, (content_w - cw) / 2);
      }
      cx = cxx;
      cy = y + lp.mt;
      y = cy + ch + lp.mb;
    }
    Value largs[4] = {};
    largs[0].i = cx;
    largs[1].i = cy;
    largs[2].i = cx + cw;
    largs[3].i = cy + ch;
    if (!CallVirtual(state, child, "layout", {"I", "I", "I", "I"}, "V", largs, 4, nullptr)) {
      state.ClearException();  // 单子项失败不中止同容器其余子项
      continue;
    }
  }
  return Value{};
}

// 通用容器（FrameLayout/RelativeLayout/ScrollView 近似面）：AOSP FrameLayout
// 口径——所有子项都以父内容区完整约束测量（used=0，重叠而非纵向累加），
// 容器自身取子项包围盒最大值。此前把已用高度（sum_h）逐子扣减，第二个
// 子项起收到 EXACTLY/AT_MOST(0) → 底部 Tab 容器/骨架/搜索栏整树塌成 0 高。
// RelativeLayout 相对规则仍属近似面（默认左上重叠），资源布局靠桥接出画。
Value ContainerOnMeasure(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 2) {
    *ok = false;
    return Value{};
  }
  int32_t wspec = args[0].i;
  int32_t hspec = args[1].i;
  int32_t max_w = 0;
  int32_t max_h = 0;
  VArray* children = ViewChildren(self);
  if (children != nullptr) {
    for (int32_t i = 0; i < children->length; i++) {
      ObjHeader* ref = GetArrRef(children, i);
      if (ref == nullptr) continue;
      VObject* child = reinterpret_cast<VObject*>(ref);
      if (ViewI(child, "mVisibility") == 8) continue;  // GONE：不参与 measure
      ChildLp lp = ReadChildLp(child);
      Value cargs[2] = {};
      cargs[0].i = ChildMeasureSpecFull(wspec,
                                        ViewI(self, "mPaddingLeft") +
                                            ViewI(self, "mPaddingRight"),
                                        lp.ml, lp.mr, 0, lp.width);
      cargs[1].i = ChildMeasureSpecFull(hspec,
                                        ViewI(self, "mPaddingTop") +
                                            ViewI(self, "mPaddingBottom"),
                                        lp.mt, lp.mb, 0, lp.height);
      if (!CallVirtual(state, child, "measure", {"I", "I"}, "V", cargs, 2, nullptr)) {
        *ok = false;
        return Value{};
      }
      max_w = std::max(max_w, ViewI(child, "mMeasuredWidth") + lp.ml + lp.mr);
      max_h = std::max(max_h, ViewI(child, "mMeasuredHeight") + lp.mt + lp.mb);
    }
  }
  SetMeasured(self, ResolveSize(max_w + ViewI(self, "mPaddingLeft") +
                                    ViewI(self, "mPaddingRight"), wspec),
              ResolveSize(max_h + ViewI(self, "mPaddingTop") + ViewI(self, "mPaddingBottom"),
                          hspec));
  return Value{};
}

Value ContainerOnLayout(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  *ok = true;
  int32_t l = n >= 4 ? args[1].i : 0;
  int32_t t = n >= 4 ? args[2].i : 0;
  // AOSP FrameLayout.layoutChildren：子项默认左上重叠（不纵向累加），
  // 按 lp.gravity 的 center/right/bottom 位修正（RelativeLayout 规则近似）。
  int32_t self_w = ViewI(self, "mRight") - ViewI(self, "mLeft");
  int32_t self_h = ViewI(self, "mBottom") - ViewI(self, "mTop");
  int32_t pad_l = ViewI(self, "mPaddingLeft");
  int32_t pad_t = ViewI(self, "mPaddingTop");
  int32_t pad_r = ViewI(self, "mPaddingRight");
  int32_t pad_b = ViewI(self, "mPaddingBottom");
  int32_t customer_w = std::max(self_w - pad_l - pad_r, 0);
  int32_t customer_h = std::max(self_h - pad_t - pad_b, 0);
  VArray* children = ViewChildren(self);
  if (children == nullptr) return Value{};
  for (int32_t i = 0; i < children->length; i++) {
    ObjHeader* ref = GetArrRef(children, i);
    if (ref == nullptr) continue;
    VObject* child = reinterpret_cast<VObject*>(ref);
    if (ViewI(child, "mVisibility") == 8) continue;  // GONE：不参与 layout
    ChildLp lp = ReadChildLp(child);
    int32_t cw = ViewI(child, "mMeasuredWidth");
    int32_t ch = ViewI(child, "mMeasuredHeight");
    int32_t gravity = 0;
    if (VObject* lpobj = reinterpret_cast<VObject*>(ViewRef(child, "mLayoutParams"))) {
      gravity = ViewI(lpobj, "gravity");
    }
    // AOSP 坐标口径：子 View 的 layout(l,t) 是相对父容器的位置，
    // onLayout 内的 l/t 本身不参与子项累加（父位置在 draw/location 时叠加）。
    int32_t x = pad_l + lp.ml;
    int32_t cy = pad_t + lp.mt;
    // Gravity 是位域组合：必须先按轴掩码再比较（BOTTOM=0x50 含 0x10 位，
    // RIGHT=0x05 含 0x01 位；直接测位会把 BOTTOM|RIGHT 误判成 CENTER）。
    const int32_t hgrav = gravity & 0x07;   // Gravity.HORIZONTAL_GRAVITY_MASK
    const int32_t vgrav = gravity & 0x70;  // Gravity.VERTICAL_GRAVITY_MASK
    if (hgrav == 0x01) {  // CENTER_HORIZONTAL
      x = pad_l + std::max(0, (customer_w - cw) / 2) + lp.ml;
    } else if (hgrav == 0x05) {  // RIGHT
      x = self_w - pad_r - lp.mr - cw;
    }
    if (vgrav == 0x10) {  // CENTER_VERTICAL
      cy = pad_t + std::max(0, (customer_h - ch) / 2) + lp.mt;
    } else if (vgrav == 0x50) {  // BOTTOM
      cy = self_h - pad_b - lp.mb - ch;
    }
    Value largs[4] = {};
    largs[0].i = x;
    largs[1].i = cy;
    largs[2].i = x + cw;
    largs[3].i = cy + ch;
    if (!CallVirtual(state, child, "layout", {"I", "I", "I", "I"}, "V", largs, 4, nullptr)) {
#if defined(__ANDROID__)
      // 单子项 layout 失败（如 RecyclerView adapter 类型未注册）不中止兄弟
      // 节点布局：AOSP 会抛到主线程，这里记录后继续，保证后续兄弟有 bounds。
      static int child_layout_fail_logs = 0;
      if (child_layout_fail_logs++ < 24) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmRender", "child layout failed %s: %s",
                            child->klass != nullptr ? child->klass->descriptor.c_str() : "?",
                            state.pending_exception != nullptr &&
                                    state.pending_exception->klass != nullptr
                                ? state.pending_exception->klass->descriptor.c_str()
                                : "?");
      }
#endif
      state.ClearException();
      continue;
    }
  }
  return Value{};
}

// android.graphics.Canvas.translate(float, float)：原点平移（取整累积）。
Value CanvasTranslate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 2) {
    cx->Translate(static_cast<int32_t>(std::lround(args[0].f)),
                  static_cast<int32_t>(std::lround(args[1].f)));
  }
  return Value{};
}

// ---- Paint/Matrix/Bitmap lib 字段读取（render 缝布局契约；见 lib 源注释）----

uint32_t PaintColor(VObject* paint) {
  if (paint == nullptr) return 0xFF000000u;
  VField* f = FindInstanceField(paint->klass, "mColor");
  return f != nullptr ? static_cast<uint32_t>(GetIField(paint, f->offset)) : 0xFF000000u;
}

float PaintTextSize(VObject* paint) {
  if (paint == nullptr) return 15.0f;
  VField* f = FindInstanceField(paint->klass, "mTextSize");
  return f != nullptr ? GetFField(paint, f->offset) : 15.0f;
}

float PaintStrokeWidth(VObject* paint) {
  if (paint == nullptr) return 0.0f;
  VField* f = FindInstanceField(paint->klass, "mStrokeWidth");
  return f != nullptr ? GetFField(paint, f->offset) : 0.0f;
}

// Paint$Style 枚举 ordinal（0=FILL, 1=STROKE, 2=FILL_AND_STROKE）。
int PaintStyleOrdinal(VObject* paint) {
  if (paint == nullptr) return 0;
  VField* f = FindInstanceField(paint->klass, "mStyle");
  if (f == nullptr) return 0;
  ObjHeader* ref = GetRefField(paint, f->offset);
  if (ref == nullptr) return 0;
  VObject* style = reinterpret_cast<VObject*>(ref);
  VField* of = FindInstanceField(style->klass, "ordinal");
  return of != nullptr ? GetIField(style, of->offset) : 0;
}

// Matrix.mValues float[9] -> 3x2（Android 平面序 {MSCALE_X,MSKEW_X,MTRANS_X,
// MSKEW_Y,SCALE_Y,MTRANS_Y,...} -> {a,b,c,d,e,f}）。
bool MatrixRead6(VObject* mo, float out[6]) {
  out[0] = 1;
  out[1] = 0;
  out[2] = 0;
  out[3] = 1;
  out[4] = 0;
  out[5] = 0;
  if (mo == nullptr) return false;
  VField* f = FindInstanceField(mo->klass, "mValues");
  if (f == nullptr) return false;
  ObjHeader* ref = GetRefField(mo, f->offset);
  if (ref == nullptr) return false;
  VArray* arr = reinterpret_cast<VArray*>(ref);
  if (arr->length < 6) return false;
  // M1 统一 8 字节/元素：float[] 每元素占 8 字节槽（低 4 字节载值）。
  auto slot_f = [&](int i) {
    float v = 0;
    std::memcpy(&v, arr->data() + static_cast<size_t>(i) * 8, sizeof(v));
    return v;
  };
  out[0] = slot_f(0);  // a = MSCALE_X
  out[1] = slot_f(3);  // b = MSKEW_Y
  out[2] = slot_f(1);  // c = MSKEW_X
  out[3] = slot_f(4);  // d = MSCALE_Y
  out[4] = slot_f(2);  // e = MTRANS_X
  out[5] = slot_f(5);  // f = MTRANS_Y
  return true;
}

void MatrixWrite6(VObject* mo, const float in[6]) {
  if (mo == nullptr) return;
  VField* f = FindInstanceField(mo->klass, "mValues");
  if (f == nullptr) return;
  ObjHeader* ref = GetRefField(mo, f->offset);
  if (ref == nullptr) return;
  VArray* arr = reinterpret_cast<VArray*>(ref);
  if (arr->length < 6) return;
  // M1 统一 8 字节/元素槽。
  auto set_f = [&](int i, float v) {
    std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &v, sizeof(v));
  };
  set_f(0, in[0]);
  set_f(3, in[1]);
  set_f(1, in[2]);
  set_f(4, in[3]);
  set_f(2, in[4]);
  set_f(5, in[5]);
}

// Bitmap：mWidth/mHeight/mBuffer（int[] ARGB）。
bool BitmapRead(VObject* bmp, int32_t* w, int32_t* h, const int32_t** buf) {
  if (bmp == nullptr) return false;
  VField* wf = FindInstanceField(bmp->klass, "mWidth");
  VField* hf = FindInstanceField(bmp->klass, "mHeight");
  VField* pf = FindInstanceField(bmp->klass, "mBuffer");
  if (wf == nullptr || hf == nullptr || pf == nullptr) return false;
  *w = GetIField(bmp, wf->offset);
  *h = GetIField(bmp, hf->offset);
  ObjHeader* ref = GetRefField(bmp, pf->offset);
  *buf = ref != nullptr ? reinterpret_cast<const int32_t*>(reinterpret_cast<VArray*>(ref)->data())
                        : nullptr;
  return true;
}

// Rect/RectF 字段读取
bool RectReadI(VObject* ro, int32_t out[4]) {
  static const char* kNames[4] = {"left", "top", "right", "bottom"};
  if (ro == nullptr) return false;
  for (int i = 0; i < 4; i++) {
    VField* f = FindInstanceField(ro->klass, kNames[i]);
    if (f == nullptr) return false;
    out[i] = GetIField(ro, f->offset);
  }
  return true;
}

bool RectReadF(VObject* ro, float out[4]) {
  static const char* kNames[4] = {"left", "top", "right", "bottom"};
  if (ro == nullptr) return false;
  for (int i = 0; i < 4; i++) {
    VField* f = FindInstanceField(ro->klass, kNames[i]);
    if (f == nullptr) return false;
    out[i] = GetFField(ro, f->offset);
  }
  return true;
}

// ---- Canvas intrinsics（android.graphics.Canvas 全面；docs 10 章 19.4 缝）----

Value CanvasCtor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  VField* f = FindInstanceField(self->klass, "mNativePtr");
  if (f != nullptr) SetLField(self, f->offset, reinterpret_cast<int64_t>(new Canvas()));
  return Value{};
}

Value CanvasCtorBitmap(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Canvas* cx = new Canvas();
  if (n >= 1 && args[0].ref != nullptr) {
    int32_t w = 0;
    int32_t h = 0;
    const int32_t* buf = nullptr;
    if (BitmapRead(reinterpret_cast<VObject*>(args[0].ref), &w, &h, &buf) && buf != nullptr) {
      cx->SetExternalPixels(reinterpret_cast<uint32_t*>(const_cast<int32_t*>(buf)), w, h);
    }
  }
  VField* f = FindInstanceField(self->klass, "mNativePtr");
  if (f != nullptr) SetLField(self, f->offset, reinterpret_cast<int64_t>(cx));
  return Value{};
}

Value CanvasSetBitmap(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 1 && args[0].ref != nullptr) {
    int32_t w = 0;
    int32_t h = 0;
    const int32_t* buf = nullptr;
    if (BitmapRead(reinterpret_cast<VObject*>(args[0].ref), &w, &h, &buf) && buf != nullptr) {
      cx->SetExternalPixels(reinterpret_cast<uint32_t*>(const_cast<int32_t*>(buf)), w, h);
    }
  }
  return Value{};
}

Value CanvasRotate1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 1) cx->Rotate(args[0].f);
  return Value{};
}

Value CanvasRotate3(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 3) cx->Rotate(args[0].f, args[1].f, args[2].f);
  return Value{};
}

Value CanvasScale2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 2) cx->Scale(args[0].f, args[1].f);
  return Value{};
}

Value CanvasScale4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 4) cx->Scale(args[0].f, args[1].f, args[2].f, args[3].f);
  return Value{};
}

Value CanvasSkew(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 2) cx->Skew(args[0].f, args[1].f);
  return Value{};
}

Value CanvasConcat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 1 && args[0].ref != nullptr) {
    float m[6];
    MatrixRead6(reinterpret_cast<VObject*>(args[0].ref), m);
    cx->Concat(m);
  }
  return Value{};
}

Value CanvasGetMatrix(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 1 && args[0].ref != nullptr) {
    float m[6];
    cx->GetMatrix6(m);
    MatrixWrite6(reinterpret_cast<VObject*>(args[0].ref), m);
  }
  return Value{};
}

Value CanvasSetMatrix(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 1 && args[0].ref != nullptr) {
    float m[6];
    MatrixRead6(reinterpret_cast<VObject*>(args[0].ref), m);
    cx->SetMatrix6(m);
  }
  return Value{};
}

Value CanvasSave(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = cx->Save();
  return r;
}

Value CanvasSaveLayer(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  // 分层语义后置：save 计数等价（内容直接落像素面）。
  Value r{};
  r.i = cx->Save();
  return r;
}

Value CanvasRestore(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  cx->Restore();
  return Value{};
}

Value CanvasRestoreToCount(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n >= 1) cx->RestoreToCount(args[0].i);
  return Value{};
}

Value CanvasClipRectF4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  if (n >= 4) {
    r.i = cx->ClipRectF(args[0].f, args[1].f, args[2].f, args[3].f) ? 1 : 0;
  } else {
    r.i = 1;
  }
  return r;
}

Value CanvasClipRectI4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  if (n >= 4) {
    r.i = cx->ClipRectF(static_cast<float>(args[0].i), static_cast<float>(args[1].i),
                        static_cast<float>(args[2].i), static_cast<float>(args[3].i))
              ? 1
              : 0;
  } else {
    r.i = 1;
  }
  return r;
}

Value CanvasClipRectObj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = 1;
  if (n >= 1 && args[0].ref != nullptr) {
    int32_t ir[4];
    float fr[4];
    if (RectReadI(reinterpret_cast<VObject*>(args[0].ref), ir)) {
      r.i = cx->ClipRectF(static_cast<float>(ir[0]), static_cast<float>(ir[1]),
                          static_cast<float>(ir[2]), static_cast<float>(ir[3]))
                ? 1
                : 0;
    } else if (RectReadF(reinterpret_cast<VObject*>(args[0].ref), fr)) {
      r.i = cx->ClipRectF(fr[0], fr[1], fr[2], fr[3]) ? 1 : 0;
    }
  }
  return r;
}

Value CanvasClipRectOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return CanvasClipRectObj(state, self, args, n, ok);
}

Value CanvasClipPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  // 路径裁剪缝后置：占位 true（非空裁剪）。
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

Value CanvasDrawText4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 4 || args[0].ref == nullptr) return Value{};
  VString* text = reinterpret_cast<VString*>(args[0].ref);
  VObject* paint = n >= 4 ? reinterpret_cast<VObject*>(args[3].ref) : nullptr;
  cx->DrawText(text->chars(), text->length, static_cast<int32_t>(std::lround(args[1].f)),
               static_cast<int32_t>(std::lround(args[2].f)), PaintTextSize(paint), PaintColor(paint));
  return Value{};
}

Value CanvasDrawText6(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 6 || args[0].ref == nullptr) return Value{};
  // (text, start, end, x, y, paint) —— text 可为 String 或 char[]。
  int32_t start = args[1].i;
  int32_t end = args[2].i;
  VObject* paint = reinterpret_cast<VObject*>(args[5].ref);
  const uint16_t* chars = nullptr;
  int32_t len = 0;
  ObjHeader* tobj = args[0].ref;
  if (tobj->klass != nullptr && tobj->klass->descriptor == "Ljava/lang/String;") {
    VString* text = reinterpret_cast<VString*>(tobj);
    if (start < 0) start = 0;
    if (end > text->length) end = text->length;
    if (end < start) end = start;
    chars = text->chars() + start;
    len = end - start;
  } else if (tobj->klass != nullptr && tobj->klass->descriptor == "[C") {
    VArray* arr = reinterpret_cast<VArray*>(tobj);
    if (start < 0) start = 0;
    if (end > arr->length) end = arr->length;
    if (end < start) end = start;
    // M1 统一 8 字节/元素：char[] 每元素 8 字节槽（低 2 字节载值）——展开为紧密缓冲。
    static thread_local std::vector<uint16_t> tl_buf;
    tl_buf.clear();
    for (int32_t i = start; i < end; i++) {
      uint16_t c = 0;
      std::memcpy(&c, arr->data() + static_cast<size_t>(i) * 8, sizeof(c));
      tl_buf.push_back(c);
    }
    chars = tl_buf.data();
    len = end - start;
  } else {
    // CharSequence 非 String（Spannable 等）：toString 缝后置——画空（缺口显式）。
    return Value{};
  }
  cx->DrawText(chars, len, static_cast<int32_t>(std::lround(args[3].f)),
               static_cast<int32_t>(std::lround(args[4].f)), PaintTextSize(paint), PaintColor(paint));
  return Value{};
}

Value CanvasDrawLine(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 5) return Value{};
  VObject* paint = reinterpret_cast<VObject*>(args[4].ref);
  cx->DrawLineF(args[0].f, args[1].f, args[2].f, args[3].f, PaintStrokeWidth(paint),
                PaintColor(paint));
  return Value{};
}

Value CanvasDrawLines(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 2 || args[0].ref == nullptr) return Value{};
  VArray* pts = reinterpret_cast<VArray*>(args[0].ref);
  VObject* paint = reinterpret_cast<VObject*>(args[1].ref);
  // M1 统一 8 字节/元素：float[] 每元素 8 字节槽。
  auto slot_f = [&](int i) {
    float v = 0;
    std::memcpy(&v, pts->data() + static_cast<size_t>(i) * 8, sizeof(v));
    return v;
  };
  int32_t pairs = pts->length / 2;
  for (int32_t i = 0; i + 1 < pairs; i += 2) {
    cx->DrawLineF(slot_f(i * 2), slot_f(i * 2 + 1), slot_f(i * 2 + 2), slot_f(i * 2 + 3),
                  PaintStrokeWidth(paint), PaintColor(paint));
  }
  return Value{};
}

Value CanvasDrawRectF4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 5) return Value{};
  VObject* paint = reinterpret_cast<VObject*>(args[4].ref);
  cx->FillRectF(args[0].f, args[1].f, args[2].f, args[3].f, PaintColor(paint));
  return Value{};
}

Value CanvasDrawRectObj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 2 || args[0].ref == nullptr) return Value{};
  VObject* paint = reinterpret_cast<VObject*>(args[1].ref);
  int32_t ir[4];
  float fr[4];
  if (RectReadI(reinterpret_cast<VObject*>(args[0].ref), ir)) {
    cx->FillRect(ir[0], ir[1], ir[2], ir[3], PaintColor(paint));
  } else if (RectReadF(reinterpret_cast<VObject*>(args[0].ref), fr)) {
    cx->FillRectF(fr[0], fr[1], fr[2], fr[3], PaintColor(paint));
  }
  return Value{};
}

Value CanvasDrawCircle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 4) return Value{};
  VObject* paint = reinterpret_cast<VObject*>(args[3].ref);
  int style = PaintStyleOrdinal(paint);
  bool fill = style != 1;  // FILL / FILL_AND_STROKE
  cx->DrawCircleF(args[0].f, args[1].f, args[2].f, fill, PaintStrokeWidth(paint),
                  PaintColor(paint));
  return Value{};
}

Value CanvasDrawPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  // 路径光栅化缝后置：占位无操作（缺口记录于需求报告）。
  *ok = true;
  return Value{};
}

Value CanvasDrawBitmap4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 3 || args[0].ref == nullptr) return Value{};
  int32_t w = 0;
  int32_t h = 0;
  const int32_t* buf = nullptr;
  if (!BitmapRead(reinterpret_cast<VObject*>(args[0].ref), &w, &h, &buf) || buf == nullptr) {
    return Value{};
  }
  cx->DrawBitmapBlock(buf, w, h, 0, 0, w, h, args[1].f, args[2].f, args[1].f + w,
                      args[2].f + h);
  return Value{};
}

Value CanvasDrawBitmapRect(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 4 || args[0].ref == nullptr) return Value{};
  int32_t w = 0;
  int32_t h = 0;
  const int32_t* buf = nullptr;
  if (!BitmapRead(reinterpret_cast<VObject*>(args[0].ref), &w, &h, &buf) || buf == nullptr) {
    return Value{};
  }
  int32_t sr[4];
  if (!RectReadI(reinterpret_cast<VObject*>(args[1].ref), sr)) {
    sr[0] = 0;
    sr[1] = 0;
    sr[2] = w;
    sr[3] = h;
  }
  float dr[4];
  int32_t dri[4];
  bool dst_is_f = RectReadF(reinterpret_cast<VObject*>(args[2].ref), dr);
  if (!dst_is_f) {
    if (!RectReadI(reinterpret_cast<VObject*>(args[2].ref), dri)) {
      dri[0] = 0;
      dri[1] = 0;
      dri[2] = w;
      dri[3] = h;
    }
    for (int i = 0; i < 4; i++) dr[i] = static_cast<float>(dri[i]);
  }
  cx->DrawBitmapBlock(buf, w, h, sr[0], sr[1], sr[2], sr[3], dr[0], dr[1], dr[2], dr[3]);
  return Value{};
}

Value CanvasDrawRGB(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  if (n < 3) return Value{};
  uint32_t color = 0xFF000000u | (static_cast<uint32_t>(args[0].i & 0xFF) << 16) |
                   (static_cast<uint32_t>(args[1].i & 0xFF) << 8) |
                   static_cast<uint32_t>(args[2].i & 0xFF);
  cx->FillRectF(-1e6f, -1e6f, 1e6f, 1e6f, color);
  return Value{};
}

Value CanvasGetWidth(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = cx->Width();
  return r;
}

Value CanvasGetHeight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = cx->Height();
  return r;
}

Value CanvasGetClipBounds(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Canvas* cx = UnwrapCanvas(state, self);
  if (cx == nullptr) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = 0;
  if (n >= 1 && args[0].ref != nullptr) {
    int32_t b[4];
    if (cx->GetClipBounds(b)) {
      VObject* ro = reinterpret_cast<VObject*>(args[0].ref);
      static const char* kNames[4] = {"left", "top", "right", "bottom"};
      for (int i = 0; i < 4; i++) {
        VField* f = FindInstanceField(ro->klass, kNames[i]);
        if (f != nullptr) SetIField(ro, f->offset, b[i]);
      }
      r.i = 1;
    }
  }
  return r;
}

// ---- 方法登记（幂等） ----

void AddIntrinsic(VClass* klass, const char* name, std::vector<std::string> params,
                  const char* ret, uint32_t access, IntrinsicFn fn) {
  if (klass == nullptr) return;
  // 同签名已存在（api_stubs.inc 的补桩先于本渲染管线登记）时以真实实现覆盖：
  // 补桩是 ReturnZero*/NoOp 默认语义，若在此静默跳过，View.measure/layout 会
  // 永远落到补桩上（宿主 render_smoke 的尺寸断言全 0）。覆盖只改 builtin 骨架
  // 的 native_fn，不影响 guest dex 方法。
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.return_type == ret && m.param_types == params) {
      m.declaring = klass;
      m.access_flags = access;
      m.code = nullptr;
      m.native_fn = ToIntrinsicSlot(fn);
      return;
    }
  }
  for (auto& m : klass->virtual_methods) {
    if (m.name == name && m.return_type == ret && m.param_types == params) {
      m.declaring = klass;
      m.access_flags = access;
      m.code = nullptr;
      m.native_fn = ToIntrinsicSlot(fn);
      return;
    }
  }
  VMethod m;
  m.declaring = klass;
  m.name = name;
  m.param_types = std::move(params);
  m.return_type = ret;
  std::string shorty;
  shorty.push_back(ret[0] == 'L' || ret[0] == '[' ? 'L' : ret[0]);
  for (const auto& p : m.param_types) {
    shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
  }
  m.shorty = shorty;
  m.access_flags = access;
  m.code = nullptr;
  m.native_fn = ToIntrinsicSlot(fn);
  klass->direct_methods.push_back(std::move(m));
}

// android.graphics.Canvas（mNativePtr + translate）。
VClass* MakeCanvasClass(ClassLinker* linker) {
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  VClass* class_class = linker->FindClass("Ljava/lang/Class;");
  if (object == nullptr || class_class == nullptr) return nullptr;
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->status = dexvm::runtime::ClassStatus::kInitialized;
  k->descriptor = "Landroid/graphics/Canvas;";
  k->super_class = object;
  k->access_flags = kAccPublic;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + 8;
  k->klass = class_class;
  VField f;
  f.declaring = k;
  f.name = "mNativePtr";
  f.type_descriptor = "J";
  f.access_flags = kAccPrivate;
  f.offset = 0;
  f.is_static = false;
  k->instance_fields.push_back(std::move(f));
  constexpr uint32_t kCtor = 0x10000 | kAccPublic;
  AddIntrinsic(k, "<init>", {}, "V", kCtor, CanvasCtor);
  AddIntrinsic(k, "<init>", {"Landroid/graphics/Bitmap;"}, "V", kCtor, CanvasCtorBitmap);
  AddIntrinsic(k, "translate", {"F", "F"}, "V", kAccPublic, CanvasTranslate);
  AddIntrinsic(k, "rotate", {"F"}, "V", kAccPublic, CanvasRotate1);
  AddIntrinsic(k, "rotate", {"F", "F", "F"}, "V", kAccPublic, CanvasRotate3);
  AddIntrinsic(k, "scale", {"F", "F"}, "V", kAccPublic, CanvasScale2);
  AddIntrinsic(k, "scale", {"F", "F", "F", "F"}, "V", kAccPublic, CanvasScale4);
  AddIntrinsic(k, "skew", {"F", "F"}, "V", kAccPublic, CanvasSkew);
  AddIntrinsic(k, "concat", {"Landroid/graphics/Matrix;"}, "V", kAccPublic, CanvasConcat);
  AddIntrinsic(k, "getMatrix", {"Landroid/graphics/Matrix;"}, "V", kAccPublic, CanvasGetMatrix);
  AddIntrinsic(k, "setMatrix", {"Landroid/graphics/Matrix;"}, "V", kAccPublic, CanvasSetMatrix);
  AddIntrinsic(k, "save", {}, "I", kAccPublic, CanvasSave);
  AddIntrinsic(k, "saveLayer", {"F", "F", "F", "F", "Landroid/graphics/Paint;"}, "I", kAccPublic,
               CanvasSaveLayer);
  AddIntrinsic(k, "saveLayerAlpha", {"F", "F", "F", "F", "I", "I"}, "I", kAccPublic,
               CanvasSaveLayer);
  AddIntrinsic(k, "saveLayerAlpha", {"F", "F", "F", "F", "I"}, "I", kAccPublic, CanvasSaveLayer);
  AddIntrinsic(k, "saveLayerAlpha", {"Landroid/graphics/RectF;", "I", "I"}, "I", kAccPublic,
               CanvasSaveLayer);
  AddIntrinsic(k, "saveLayerAlpha", {"Landroid/graphics/RectF;", "I"}, "I", kAccPublic,
               CanvasSaveLayer);
  AddIntrinsic(k, "restore", {}, "V", kAccPublic, CanvasRestore);
  AddIntrinsic(k, "restoreToCount", {"I"}, "V", kAccPublic, CanvasRestoreToCount);
  AddIntrinsic(k, "clipRect", {"F", "F", "F", "F"}, "Z", kAccPublic, CanvasClipRectF4);
  AddIntrinsic(k, "clipRect", {"I", "I", "I", "I"}, "Z", kAccPublic, CanvasClipRectI4);
  AddIntrinsic(k, "clipRect", {"Landroid/graphics/Rect;"}, "Z", kAccPublic, CanvasClipRectObj);
  AddIntrinsic(k, "clipRect", {"Landroid/graphics/RectF;"}, "Z", kAccPublic, CanvasClipRectObj);
  AddIntrinsic(k, "clipRect", {"Landroid/graphics/Rect;", "Landroid/graphics/Region$Op;"}, "Z",
               kAccPublic, CanvasClipRectOp);
  AddIntrinsic(k, "clipPath", {"Landroid/graphics/Path;"}, "Z", kAccPublic, CanvasClipPath);
  AddIntrinsic(k, "drawText",
               {"Ljava/lang/String;", "F", "F", "Landroid/graphics/Paint;"}, "V", kAccPublic,
               CanvasDrawText4);
  AddIntrinsic(k, "drawText",
               {"Ljava/lang/String;", "I", "I", "F", "F", "Landroid/graphics/Paint;"}, "V",
               kAccPublic, CanvasDrawText6);
  AddIntrinsic(k, "drawText",
               {"[C", "I", "I", "F", "F", "Landroid/graphics/Paint;"}, "V", kAccPublic,
               CanvasDrawText6);
  AddIntrinsic(k, "drawText",
               {"Ljava/lang/CharSequence;", "I", "I", "F", "F", "Landroid/graphics/Paint;"}, "V",
               kAccPublic, CanvasDrawText6);
  AddIntrinsic(k, "drawLine",
               {"F", "F", "F", "F", "Landroid/graphics/Paint;"}, "V", kAccPublic, CanvasDrawLine);
  AddIntrinsic(k, "drawLines", {"[F", "Landroid/graphics/Paint;"}, "V", kAccPublic,
               CanvasDrawLines);
  AddIntrinsic(k, "drawRect",
               {"F", "F", "F", "F", "Landroid/graphics/Paint;"}, "V", kAccPublic, CanvasDrawRectF4);
  AddIntrinsic(k, "drawRect",
               {"Landroid/graphics/Rect;", "Landroid/graphics/Paint;"}, "V", kAccPublic,
               CanvasDrawRectObj);
  AddIntrinsic(k, "drawRect",
               {"Landroid/graphics/RectF;", "Landroid/graphics/Paint;"}, "V", kAccPublic,
               CanvasDrawRectObj);
  AddIntrinsic(k, "drawCircle",
               {"F", "F", "F", "Landroid/graphics/Paint;"}, "V", kAccPublic, CanvasDrawCircle);
  AddIntrinsic(k, "drawPath",
               {"Landroid/graphics/Path;", "Landroid/graphics/Paint;"}, "V", kAccPublic,
               CanvasDrawPath);
  AddIntrinsic(k, "drawBitmap",
               {"Landroid/graphics/Bitmap;", "F", "F", "Landroid/graphics/Paint;"}, "V",
               kAccPublic, CanvasDrawBitmap4);
  AddIntrinsic(k, "drawBitmap",
               {"Landroid/graphics/Bitmap;", "Landroid/graphics/Rect;",
                "Landroid/graphics/Rect;", "Landroid/graphics/Paint;"},
               "V", kAccPublic, CanvasDrawBitmapRect);
  AddIntrinsic(k, "drawBitmap",
               {"Landroid/graphics/Bitmap;", "Landroid/graphics/Rect;",
                "Landroid/graphics/RectF;", "Landroid/graphics/Paint;"},
               "V", kAccPublic, CanvasDrawBitmapRect);
  AddIntrinsic(k, "drawRGB", {"I", "I", "I"}, "V", kAccPublic, CanvasDrawRGB);
  AddIntrinsic(k, "getWidth", {}, "I", kAccPublic, CanvasGetWidth);
  AddIntrinsic(k, "getHeight", {}, "I", kAccPublic, CanvasGetHeight);
  AddIntrinsic(k, "getClipBounds", {"Landroid/graphics/Rect;"}, "Z", kAccPublic,
               CanvasGetClipBounds);
  AddIntrinsic(k, "setBitmap", {"Landroid/graphics/Bitmap;"}, "V", kAccPublic, CanvasSetBitmap);
  linker->RegisterBuiltinClass(k);
  return k;
}

}  // namespace

// ---- Canvas ----

void Canvas::InitClip() {
  clip_l_ = 0;
  clip_t_ = 0;
  clip_r_ = Width();
  clip_b_ = Height();
}

void Canvas::SetExternalPixels(uint32_t* px, int32_t w, int32_t h) {
  ext_pixels_ = px;
  ext_w_ = w;
  ext_h_ = h;
  InitClip();
}

int32_t Canvas::Width() const {
  if (ext_pixels_ != nullptr) return ext_w_;
  return vp_w_;
}

int32_t Canvas::Height() const {
  if (ext_pixels_ != nullptr) return ext_h_;
  return vp_h_;
}

bool Canvas::GetClipBounds(int32_t out[4]) const {
  out[0] = clip_l_;
  out[1] = clip_t_;
  out[2] = clip_r_;
  out[3] = clip_b_;
  return clip_r_ > clip_l_ && clip_b_ > clip_t_;
}

void Canvas::MapPoint(float x, float y, float* ox, float* oy) const {
  *ox = m_[0] * x + m_[2] * y + m_[4];
  *oy = m_[1] * x + m_[3] * y + m_[5];
}

uint32_t* Canvas::RowPtr(int32_t y) {
  // 自绘位图轨已退役：像素直写只剩 Canvas(Bitmap) 外置像素面。
  if (ext_pixels_ == nullptr) return nullptr;
  return ext_pixels_ + static_cast<size_t>(y) * ext_w_;
}

void Canvas::FillDevice(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t argb) {
  x0 = std::max(x0, clip_l_);
  y0 = std::max(y0, clip_t_);
  x1 = std::min(x1, clip_r_);
  y1 = std::min(y1, clip_b_);
  for (int32_t y = y0; y < y1; y++) {
    uint32_t* row = RowPtr(y);
    if (row == nullptr) return;
    for (int32_t x = x0; x < x1; x++) row[x] = argb;
  }
}

void Canvas::FillRect(int32_t l, int32_t t, int32_t r, int32_t b, uint32_t argb) {
  FillRectF(static_cast<float>(l), static_cast<float>(t), static_cast<float>(r),
            static_cast<float>(b), argb);
}

void Canvas::FillRectF(float l, float t, float r, float b, uint32_t argb) {
  // 缝A：命令记录（自绘像素轨退役后与像素面解耦）。
  if (sink_ != nullptr && !record_suppress_) sink_->OnFillRect(l, t, r, b, argb);
  if (ext_pixels_ == nullptr) return;  // 像素直写只剩 Canvas(Bitmap) 外置面
  float x0f, y0f, x1f, y1f;
  MapPoint(l, t, &x0f, &y0f);
  MapPoint(r, b, &x1f, &y1f);
  int32_t x0 = static_cast<int32_t>(std::floor(std::min(x0f, x1f)));
  int32_t y0 = static_cast<int32_t>(std::floor(std::min(y0f, y1f)));
  int32_t x1 = static_cast<int32_t>(std::ceil(std::max(x0f, x1f)));
  int32_t y1 = static_cast<int32_t>(std::ceil(std::max(y0f, y1f)));
  FillDevice(x0, y0, x1, y1, argb);
}

void Canvas::DrawText(const uint16_t* chars, int32_t len, int32_t x, int32_t y, float text_size,
                      uint32_t argb) {
  if (chars == nullptr || len <= 0) return;
  // 缝A：drawText 进协议流（8x8 字体光栅化已退役，不产像素）。
  if (sink_ != nullptr && !record_suppress_) {
    sink_->OnDrawText(chars, len, x, y, text_size, argb);
  }
}

void Canvas::TranslateF(float dx, float dy) {
  m_[4] += dx;
  m_[5] += dy;
  origin_x_ += static_cast<int32_t>(std::lround(dx));
  origin_y_ += static_cast<int32_t>(std::lround(dy));
  if (sink_ != nullptr && !record_suppress_) sink_->OnTranslate(dx, dy);
}

void Canvas::Rotate(float degrees) {
  Rotate(degrees, 0.0f, 0.0f);
}

void Canvas::Rotate(float degrees, float px, float py) {
  float rad = degrees * 3.14159265358979323846f / 180.0f;
  float c = std::cos(rad);
  float s = std::sin(rad);
  // T(px,py) · R · T(-px,-py) 与当前矩阵复合（左乘）
  float r[6] = {c, s, -s, c, px - c * px + s * py, py - s * px - c * py};
  Concat(r);
}

void Canvas::Scale(float sx, float sy) {
  Scale(sx, sy, 0.0f, 0.0f);
}

void Canvas::Scale(float sx, float sy, float px, float py) {
  float r[6] = {sx, 0, 0, sy, px - sx * px, py - sy * py};
  Concat(r);
}

void Canvas::Skew(float kx, float ky) {
  float r[6] = {1, ky, kx, 1, 0, 0};
  Concat(r);
}

void Canvas::Concat(const float m[6]) {
  // out = m × 当前（标准 preConcat：先经 m 再经当前？Android concat 语义 =
  // M' = M · M_cur（右乘）——按 AOSP：concat(M) 后 p 先过 M 再过原变换）。
  if (sink_ != nullptr && !record_suppress_) sink_->OnConcatMatrix(m);
  float a = m[0] * m_[0] + m[1] * m_[2];
  float b = m[0] * m_[1] + m[1] * m_[3];
  float c = m[2] * m_[0] + m[3] * m_[2];
  float d = m[2] * m_[1] + m[3] * m_[3];
  float e = m[4] * m_[0] + m[5] * m_[2] + m_[4];
  float f = m[4] * m_[1] + m[5] * m_[3] + m_[5];
  m_[0] = a;
  m_[1] = b;
  m_[2] = c;
  m_[3] = d;
  m_[4] = e;
  m_[5] = f;
}

void Canvas::GetMatrix6(float out[6]) const {
  for (int i = 0; i < 6; i++) out[i] = m_[i];
}

void Canvas::SetMatrix6(const float m[6]) {
  for (int i = 0; i < 6; i++) m_[i] = m[i];
  origin_x_ = static_cast<int32_t>(std::lround(m_[4]));
  origin_y_ = static_cast<int32_t>(std::lround(m_[5]));
  if (sink_ != nullptr && !record_suppress_) sink_->OnSetMatrix(m);
}

int32_t Canvas::Save() {
  if (sink_ != nullptr && !record_suppress_) sink_->OnSave();
  SavedState st;
  for (int i = 0; i < 6; i++) st.m[i] = m_[i];
  st.cl = clip_l_;
  st.ct = clip_t_;
  st.cr = clip_r_;
  st.cb = clip_b_;
  st.ox = origin_x_;
  st.oy = origin_y_;
  stack_.push_back(st);
  return static_cast<int32_t>(stack_.size());
}

void Canvas::Restore() {
  if (stack_.empty()) return;
  // 缝A：restore 记 kRestore（内部 RestoreToCount 不重复记录）。
  bool suppress = record_suppress_;
  if (sink_ != nullptr && !suppress) {
    sink_->OnRestore();
    record_suppress_ = true;
  }
  RestoreToCount(static_cast<int32_t>(stack_.size()) - 1);
  record_suppress_ = suppress;
}

void Canvas::RestoreToCount(int32_t count) {
  if (sink_ != nullptr && !record_suppress_) sink_->OnRestoreToCount(count);
  while (static_cast<int32_t>(stack_.size()) > count && !stack_.empty()) {
    const SavedState& st = stack_.back();
    for (int i = 0; i < 6; i++) m_[i] = st.m[i];
    clip_l_ = st.cl;
    clip_t_ = st.ct;
    clip_r_ = st.cr;
    clip_b_ = st.cb;
    origin_x_ = st.ox;
    origin_y_ = st.oy;
    stack_.pop_back();
  }
}

bool Canvas::ClipRectF(float l, float t, float r, float b) {
  if (sink_ != nullptr && !record_suppress_) sink_->OnClipRect(l, t, r, b);
  float x0f, y0f, x1f, y1f;
  MapPoint(l, t, &x0f, &y0f);
  MapPoint(r, b, &x1f, &y1f);
  int32_t x0 = static_cast<int32_t>(std::floor(std::min(x0f, x1f)));
  int32_t y0 = static_cast<int32_t>(std::floor(std::min(y0f, y1f)));
  int32_t x1 = static_cast<int32_t>(std::ceil(std::max(x0f, x1f)));
  int32_t y1 = static_cast<int32_t>(std::ceil(std::max(y0f, y1f)));
  clip_l_ = std::max(clip_l_, x0);
  clip_t_ = std::max(clip_t_, y0);
  clip_r_ = std::min(clip_r_, x1);
  clip_b_ = std::min(clip_b_, y1);
  return clip_r_ > clip_l_ && clip_b_ > clip_t_;
}

void Canvas::DrawLineF(float x0, float y0, float x1, float y1, float stroke_w, uint32_t argb) {
  if (sink_ != nullptr && !record_suppress_) sink_->OnDrawLine(x0, y0, x1, y1, stroke_w, argb);
  if (ext_pixels_ == nullptr) return;  // 像素直写只剩 Canvas(Bitmap) 外置面
  float ax, ay, bx, by;
  MapPoint(x0, y0, &ax, &ay);
  MapPoint(x1, y1, &bx, &by);
  float half = stroke_w > 0 ? stroke_w * 0.5f : 0.5f;
  // 沿线段走点（步长 1px），画 2*half 见方的点块（轴对齐近似）。
  float dx = bx - ax;
  float dy = by - ay;
  int32_t steps = static_cast<int32_t>(std::max(std::fabs(dx), std::fabs(dy))) + 1;
  if (steps > 4096) steps = 4096;
  for (int32_t i = 0; i <= steps; i++) {
    float t = steps == 0 ? 0.0f : static_cast<float>(i) / steps;
    float px = ax + dx * t;
    float py = ay + dy * t;
    FillDevice(static_cast<int32_t>(std::floor(px - half)),
               static_cast<int32_t>(std::floor(py - half)),
               static_cast<int32_t>(std::ceil(px + half)),
               static_cast<int32_t>(std::ceil(py + half)), argb);
  }
}

void Canvas::DrawCircleF(float cx, float cy, float r, bool fill, float stroke_w, uint32_t argb) {
  if (sink_ != nullptr && !record_suppress_) {
    sink_->OnDrawCircle(cx, cy, r, fill, stroke_w, argb);
  }
  if (ext_pixels_ == nullptr) return;  // 像素直写只剩 Canvas(Bitmap) 外置面
  float mx, my;
  MapPoint(cx, cy, &mx, &my);
  // 半径按矩阵平均缩放折算
  float sx = std::sqrt(m_[0] * m_[0] + m_[1] * m_[1]);
  float sy = std::sqrt(m_[2] * m_[2] + m_[3] * m_[3]);
  float scale = (sx + sy) * 0.5f;
  float rr = r * scale;
  float half = stroke_w > 0 ? stroke_w * 0.5f * scale : 0.0f;
  int32_t x0 = static_cast<int32_t>(std::floor(mx - rr - half - 1));
  int32_t y0 = static_cast<int32_t>(std::floor(my - rr - half - 1));
  int32_t x1 = static_cast<int32_t>(std::ceil(mx + rr + half + 1));
  int32_t y1 = static_cast<int32_t>(std::ceil(my + rr + half + 1));
  x0 = std::max(x0, clip_l_);
  y0 = std::max(y0, clip_t_);
  x1 = std::min(x1, clip_r_);
  y1 = std::min(y1, clip_b_);
  float inner2 = fill ? 0.0f : (rr - half) * (rr - half);
  float outer2 = (rr + (fill ? 0.0f : half)) * (rr + (fill ? 0.0f : half));
  for (int32_t y = y0; y < y1; y++) {
    uint32_t* row = RowPtr(y);
    if (row == nullptr) return;
    for (int32_t x = x0; x < x1; x++) {
      float ddx = x + 0.5f - mx;
      float ddy = y + 0.5f - my;
      float d2 = ddx * ddx + ddy * ddy;
      if (fill ? d2 <= outer2 : (d2 >= inner2 && d2 <= outer2)) {
        row[x] = argb;
      }
    }
  }
}

void Canvas::DrawBitmapBlock(const int32_t* src, int32_t sw, int32_t sh, int32_t sl, int32_t st,
                             int32_t sr, int32_t sb, float dl, float dt, float dr, float db) {
  if (src == nullptr) return;
  if (sr <= sl || sb <= st || dr <= dl || db <= dt) return;
  // 缝A：drawBitmap 进协议流（源像素拷贝，8 字节/元素 -> 紧密）。
  if (sink_ != nullptr && !record_suppress_) {
    sink_->OnDrawBitmap(sw, sh, sl, st, sr, sb, dl, dt, dr, db, src);
  }
  if (ext_pixels_ == nullptr) return;  // 像素直写只剩 Canvas(Bitmap) 外置面
  // src = VM int[] 数据面（M1 统一 8 字节/元素：每元素 8 字节槽，低 4 字节载值）。
  auto src_px = [&](int32_t x, int32_t y) -> uint32_t {
    uint32_t c = 0;
    std::memcpy(&c, reinterpret_cast<const uint8_t*>(src) +
                        (static_cast<size_t>(y) * sw + x) * 8,
                sizeof(c));
    return c;
  };
  float x0f, y0f, x1f, y1f;
  MapPoint(dl, dt, &x0f, &y0f);
  MapPoint(dr, db, &x1f, &y1f);
  int32_t x0 = static_cast<int32_t>(std::floor(std::min(x0f, x1f)));
  int32_t y0 = static_cast<int32_t>(std::floor(std::min(y0f, y1f)));
  int32_t x1 = static_cast<int32_t>(std::ceil(std::max(x0f, x1f)));
  int32_t y1 = static_cast<int32_t>(std::ceil(std::max(y0f, y1f)));
  int32_t cx0 = std::max(x0, clip_l_);
  int32_t cy0 = std::max(y0, clip_t_);
  int32_t cx1 = std::min(x1, clip_r_);
  int32_t cy1 = std::min(y1, clip_b_);
  if (x1 - x0 <= 0 || y1 - y0 <= 0) return;
  for (int32_t y = cy0; y < cy1; y++) {
    uint32_t* row = RowPtr(y);
    if (row == nullptr) return;
    int32_t sy = st + (y - y0) * (sb - st) / (y1 - y0);
    if (sy < 0) sy = 0;
    if (sy >= sh) sy = sh - 1;
    for (int32_t x = cx0; x < cx1; x++) {
      int32_t sx = sl + (x - x0) * (sr - sl) / (x1 - x0);
      if (sx < 0) sx = 0;
      if (sx >= sw) sx = sw - 1;
      uint32_t c = src_px(sx, sy);
      if ((c >> 24) != 0) row[x] = c;
    }
  }
}

// ---- 文字度量 ----

int32_t TextScale(float text_size) {
  float ts = text_size > 0 ? text_size : 15.0f;
  int32_t s = static_cast<int32_t>(std::lround(ts / 8.0f));
  return s < 1 ? 1 : s;
}

int32_t TextMeasureWidth(const uint16_t* chars, int32_t len, float text_size) {
  if (chars == nullptr || len <= 0) return 0;
  int32_t max_len = 0;
  int32_t cur = 0;
  for (int32_t i = 0; i < len; i++) {
    if (chars[i] == u'\n') {
      max_len = std::max(max_len, cur);
      cur = 0;
    } else {
      cur++;
    }
  }
  max_len = std::max(max_len, cur);
  return max_len * 8 * TextScale(text_size);
}

int32_t TextMeasureHeight(const uint16_t* chars, int32_t len, float text_size) {
  if (chars == nullptr || len <= 0) return 0;
  int32_t lines = 1;
  for (int32_t i = 0; i < len; i++) {
    if (chars[i] == u'\n') lines++;
  }
  return lines * 10 * TextScale(text_size);
}

int32_t MakeMeasureSpec(int32_t size, int32_t mode) { return (size & 0x3fffffff) | mode; }
int32_t GetSpecSize(int32_t spec) { return spec & 0x3fffffff; }
int32_t GetSpecMode(int32_t spec) { return spec & ~0x3fffffff; }

// ---- 登记 ----

void RegisterRenderPipeline(ClassLinker* linker) {
  if (linker == nullptr) return;
  dexvm::runtime::ResetGuestDrawableResources();
  VClass* view = linker->FindClass("Landroid/view/View;");
  VClass* view_group = linker->FindClass("Landroid/view/ViewGroup;");
  VClass* text_view = linker->FindClass("Landroid/widget/TextView;");
  VClass* linear_layout = linker->FindClass("Landroid/widget/LinearLayout;");
  if (view == nullptr || view_group == nullptr || text_view == nullptr ||
      linear_layout == nullptr) {
    return;  // RegisterFrameworkBuiltins 未跑
  }

  // View：measure/layout/draw 面（onXxx 默认实现可覆写、super 可调）
  AddIntrinsic(view, "measure", {"I", "I"}, "V", kAccPublic, ViewMeasure);
  AddIntrinsic(view, "onMeasure", {"I", "I"}, "V", kAccPublic, ViewOnMeasure);
  AddIntrinsic(view, "layout", {"I", "I", "I", "I"}, "V", kAccPublic, ViewLayout);
  AddIntrinsic(view, "onLayout", {"Z", "I", "I", "I", "I"}, "V", kAccPublic, NoOp);
  AddIntrinsic(view, "draw", {"Landroid/graphics/Canvas;"}, "V", kAccPublic, ViewDraw);
  AddIntrinsic(view, "onDraw", {"Landroid/graphics/Canvas;"}, "V", kAccPublic, NoOp);
  AddIntrinsic(view, "setMeasuredDimension", {"I", "I"}, "V", kAccPublic, SetMeasuredDimension);
  AddIntrinsic(view, "getMeasuredWidth", {}, "I", kAccPublic, GetMeasuredWidth);
  AddIntrinsic(view, "getMeasuredHeight", {}, "I", kAccPublic, GetMeasuredHeight);
  AddIntrinsic(view, "getWidth", {}, "I", kAccPublic, GetWidth);
  AddIntrinsic(view, "getMeasuredState", {}, "I", kAccPublic, GetMeasuredState);
  AddIntrinsic(view, "getMeasuredWidthAndState", {}, "I", kAccPublic, GetMeasuredWidthAndState);
  AddIntrinsic(view, "getMeasuredHeightAndState", {}, "I", kAccPublic, GetMeasuredHeightAndState);
  AddIntrinsic(view, "getMinimumWidth", {}, "I", kAccPublic, GetMinimumSize);
  AddIntrinsic(view, "getMinimumHeight", {}, "I", kAccPublic, GetMinimumSize);
  AddIntrinsic(view, "getSuggestedMinimumWidth", {}, "I", kAccPublic, GetMinimumSize);
  AddIntrinsic(view, "getSuggestedMinimumHeight", {}, "I", kAccPublic, GetMinimumSize);
  AddIntrinsic(view, "getHeight", {}, "I", kAccPublic, GetHeight);
  AddIntrinsic(view, "setBackgroundColor", {"I"}, "V", kAccPublic, SetBackgroundColor);

  // TextView：文字度量 + drawText 命令记录
  AddIntrinsic(text_view, "onMeasure", {"I", "I"}, "V", kAccPublic, TVOnMeasure);
  AddIntrinsic(text_view, "onDraw", {"Landroid/graphics/Canvas;"}, "V", kAccPublic, TVOnDraw);
  AddIntrinsic(text_view, "setTextColor", {"I"}, "V", kAccPublic, SetTextColor);

  // ViewGroup：递归 draw 子 View
  AddIntrinsic(view_group, "onDraw", {"Landroid/graphics/Canvas;"}, "V", kAccPublic, VGOnDraw);
  // AOSP ViewGroup 钩子默认空实现（addView 时 CallVirtual 派发覆写）
  AddIntrinsic(view_group, "onViewAdded", {"Landroid/view/View;"}, "V", kAccPublic, NoOp);
  AddIntrinsic(view_group, "onViewRemoved", {"Landroid/view/View;"}, "V", kAccPublic, NoOp);

  // ViewGroup 子测量方法面（DrawerLayout/FrameLayout 等 guest onMeasure 消费）
  AddIntrinsic(view_group, "measureChildWithMargins",
               {"Landroid/view/View;", "I", "I", "I", "I"}, "V", kAccPublic,
               VGMeasureChildWithMargins);
  AddIntrinsic(view_group, "measureChild",
               {"Landroid/view/View;", "I", "I"}, "V", kAccPublic, VGMeasureChild);

  // LinearLayout：纵向 measure/layout（布局参数/边距/水平 gravity 感知）
  AddIntrinsic(linear_layout, "onMeasure", {"I", "I"}, "V", kAccPublic, LLOnMeasure);
  AddIntrinsic(linear_layout, "onLayout", {"Z", "I", "I", "I", "I"}, "V", kAccPublic, LLOnLayout);

  // 资源布局容器（FrameLayout/RelativeLayout/ScrollView 近似面）
  for (const char* desc : {"Landroid/widget/FrameLayout;", "Landroid/widget/RelativeLayout;",
                           "Landroid/widget/ScrollView;",
                           "Landroid/widget/HorizontalScrollView;"}) {
    VClass* container = linker->FindClass(desc);
    if (container == nullptr) continue;
    AddIntrinsic(container, "onMeasure", {"I", "I"}, "V", kAccPublic, ContainerOnMeasure);
    AddIntrinsic(container, "onLayout", {"Z", "I", "I", "I", "I"}, "V", kAccPublic,
                 ContainerOnLayout);
  }
  // ProgressBar：横向固有高度面
  if (VClass* progress = linker->FindClass("Landroid/widget/ProgressBar;")) {
    AddIntrinsic(progress, "onMeasure", {"I", "I"}, "V", kAccPublic, PBOnMeasure);
  }
  // SeekBar：拖拽条视觉高度面（ProgressBar.onMeasure 的 8px 会把 thumb 压扁）
  if (VClass* seek_bar = linker->FindClass("Landroid/widget/SeekBar;")) {
    AddIntrinsic(seek_bar, "onMeasure", {"I", "I"}, "V", kAccPublic, SBOnMeasure);
  }

  // android.graphics.Canvas（幂等：FindClass 命中即已建）
  if (linker->FindClass("Landroid/graphics/Canvas;") == nullptr) MakeCanvasClass(linker);

  // 桥接缝覆写：setOnClickListener（framework_builtins 登记为 NoOp）->
  // 监听器登记（点击回灌：viewId -> guest listener）。每 VM 引导调用一次；
  // 重复调用清登记表（防跨会话悬挂，见 bridge/ui_bridge_input.h）。
  auto override_listener = [&](std::vector<runtime::VMethod>& methods) {
    for (auto& m : methods) {
      if (m.name == "setOnClickListener" && m.param_types.size() == 1 &&
          m.param_types[0] == "Landroid/view/View$OnClickListener;") {
        m.native_fn = runtime::ToIntrinsicSlot(runtime::ViewSetOnClickListener);
      }
    }
  };
  override_listener(view->direct_methods);
  override_listener(view->virtual_methods);
  dexvm::bridge::ClearGuestClickListeners();
}

VObject* WrapCanvas(VMState& state, Canvas* native) {
  if (native == nullptr) return nullptr;
  VClass* k = state.linker->FindClass("Landroid/graphics/Canvas;");
  if (k == nullptr) k = MakeCanvasClass(state.linker);
  if (k == nullptr) return nullptr;
  VObject* o = state.linker->AllocObject(k);
  if (o == nullptr) return nullptr;
  SetLField(o, 0, reinterpret_cast<int64_t>(native));
  return o;
}

// Activity.addContentView 覆盖层（AOSP decor content parent 左上叠放）：
// traversal 后按窗口原点 measure/layout（内容树照常在其下）。
bool LayoutAddedContentViews(VMState& state, VObject* activity, int32_t width, int32_t height) {
  for (VObject* view : dexvm::runtime::AddedContentViews(activity)) {
    if (view == nullptr) continue;
    ChildLp lp = ReadChildLp(view);
    Value margs[2] = {};
    margs[0].i = lp.width >= 0 ? MakeMeasureSpec(lp.width, kExactly)
                 : lp.width == -1 ? MakeMeasureSpec(width, kExactly)
                                  : MakeMeasureSpec(width, kAtMost);
    margs[1].i = lp.height >= 0 ? MakeMeasureSpec(lp.height, kExactly)
                                : MakeMeasureSpec(height, kAtMost);
    if (!CallVirtual(state, view, "measure", {"I", "I"}, "V", margs, 2, nullptr)) return false;
    int32_t mw = ViewI(view, "mMeasuredWidth");
    int32_t mh = ViewI(view, "mMeasuredHeight");
    Value largs[4] = {};
    largs[0].i = 0;
    largs[1].i = 0;
    largs[2].i = mw;
    largs[3].i = mh;
    if (!CallVirtual(state, view, "layout", {"I", "I", "I", "I"}, "V", largs, 4, nullptr)) {
      return false;
    }
  }
  return true;
}

#if defined(__ANDROID__)
// 整体子树 dump：定位 AppBarLayout 等塌缩容器的内部测量面（一次/进程）。
void DumpViewSubtree(VObject* v, int depth, int* budget) {
  if (v == nullptr || depth > 12 || *budget <= 0) return;
  --*budget;
  int32_t w = ViewI(v, "mRight") - ViewI(v, "mLeft");
  int32_t h = ViewI(v, "mBottom") - ViewI(v, "mTop");
  int32_t mw = ViewI(v, "mMeasuredWidth");
  int32_t mh = ViewI(v, "mMeasuredHeight");
  ChildLp lp = ReadChildLp(v);
  VString* text = ViewText(v);
  __android_log_print(ANDROID_LOG_INFO, "DexVmMeasure",
                      "T%*s%s vis=%d b=%d,%d %dx%d m=%dx%d lp=%dx%d ts=%.1f len=%d", depth * 2, "",
                      v->klass != nullptr ? v->klass->descriptor.c_str() : "?",
                      ViewI(v, "mVisibility"), ViewI(v, "mLeft"), ViewI(v, "mTop"), w, h, mw, mh,
                      lp.width, lp.height, static_cast<double>(ViewF(v, "mTextSize")),
                      text != nullptr ? text->length : -1);
  VArray* children = ViewChildren(v);
  if (children == nullptr) return;
  for (int32_t i = 0; i < children->length; i++) {
    DumpViewSubtree(reinterpret_cast<VObject*>(GetArrRef(children, i)), depth + 1, budget);
  }
}

VObject* FindViewByClass(VObject* v, const char* needle, int depth) {
  if (v == nullptr || depth > 14) return nullptr;
  if (v->klass != nullptr && v->klass->descriptor.find(needle) != std::string::npos) return v;
  VArray* children = ViewChildren(v);
  if (children == nullptr) return nullptr;
  for (int32_t i = 0; i < children->length; i++) {
    if (VObject* hit = FindViewByClass(reinterpret_cast<VObject*>(GetArrRef(children, i)), needle,
                                       depth + 1)) {
      return hit;
    }
  }
  return nullptr;
}

// 布局收敛诊断（一次/进程，限量）：列出"可见但高度/测量高度异常小"的节点，
// 定位文本/容器塌缩的具体类（bounds+ 里也可能高度=4）。
void DumpTinyViews(VObject* v, int depth, int* budget, int* small) {
  if (v == nullptr || depth > 14 || *budget <= 0) return;
  --*budget;
  int32_t w = ViewI(v, "mRight") - ViewI(v, "mLeft");
  int32_t h = ViewI(v, "mBottom") - ViewI(v, "mTop");
  int32_t mw = ViewI(v, "mMeasuredWidth");
  int32_t mh = ViewI(v, "mMeasuredHeight");
  int32_t vis = ViewI(v, "mVisibility");
  bool tiny_bounds = w > 0 && h > 0 && h < 12;
  bool tiny_measured = mw > 0 && mh > 0 && mh < 12;
  if (vis == 0 && (tiny_bounds || tiny_measured) && *small < 48) {
    VString* text = ViewText(v);
    ChildLp lp = ReadChildLp(v);
    __android_log_print(ANDROID_LOG_INFO, "DexVmMeasure",
                        "%*s%s b=%d,%d %dx%d m=%dx%d lp=%dx%d ts=%.1f len=%d", depth * 2, "",
                        v->klass != nullptr ? v->klass->descriptor.c_str() : "?", ViewI(v, "mLeft"),
                        ViewI(v, "mTop"), w, h, mw, mh, lp.width, lp.height,
                        static_cast<double>(ViewF(v, "mTextSize")),
                        text != nullptr ? text->length : -1);
    ++*small;
  }
  VArray* children = ViewChildren(v);
  if (children == nullptr) return;
  for (int32_t i = 0; i < children->length; i++) {
    DumpTinyViews(reinterpret_cast<VObject*>(GetArrRef(children, i)), depth + 1, budget, small);
  }
}
#endif

namespace {
// traversal 失败归因：guest 异常 descriptor + message（诊断面；异常清掉，
// phase 记录仍按通用原因）
void LogTraversalFailure(VMState& state, const char* stage) {
#if defined(__ANDROID__)
  if (!state.HasPendingException() || state.pending_exception == nullptr) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmRender", "traversal %s failed (no exception)",
                        stage);
    return;
  }
  ObjHeader* ex = state.pending_exception;
  std::string desc = ex->klass != nullptr ? ex->klass->descriptor : "?";
  std::string msg;
  for (VClass* k = ex->klass; k != nullptr && msg.empty(); k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == "detailMessage") {
        ObjHeader* m = nullptr;
        std::memcpy(&m, reinterpret_cast<VObject*>(ex)->field_data() + f.offset, sizeof(m));
        if (m != nullptr) msg = static_cast<VString*>(m)->ToModifiedUtf8();
        break;
      }
    }
  }
  __android_log_print(ANDROID_LOG_INFO, "DexVmRender", "traversal %s failed: %s: %s", stage,
                      desc.c_str(), msg.c_str());
  const auto& bt = state.last_throw_backtrace;
  for (size_t i = 0; i < bt.size() && i < 16; i++) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmRender", "  at %s", bt[i].c_str());
  }
#else
  (void)state;
  (void)stage;
#endif
}
}  // namespace

bool PerformTraversals(VMState& state, VObject* content_view, int32_t width, int32_t height) {
  // setContentView 未调用（无 content view）：不产出绘制面（负例）。
  if (content_view == nullptr || width <= 0 || height <= 0) return false;

  // measure -> layout -> draw（docs 19.4；根按 EXACTLY 窗口尺寸）
  Value margs[2] = {};
  margs[0].i = MakeMeasureSpec(width, kExactly);
  margs[1].i = MakeMeasureSpec(height, kExactly);
  if (!CallVirtual(state, content_view, "measure", {"I", "I"}, "V", margs, 2, nullptr)) {
    LogTraversalFailure(state, "measure");
    state.ClearException();
    return false;
  }
  Value largs[4] = {};
  largs[2].i = width;
  largs[3].i = height;
  if (!CallVirtual(state, content_view, "layout", {"I", "I", "I", "I"}, "V", largs, 4, nullptr)) {
    LogTraversalFailure(state, "layout");
    state.ClearException();
    return false;
  }
  Canvas canvas(width, height);
  canvas.BeginFrame();  // 缝A：帧起点（命令流清零）
  VObject* cx = WrapCanvas(state, &canvas);
  if (cx == nullptr) return false;
  Value darg{};
  darg.ref = cx;
  if (!CallVirtual(state, content_view, "draw", {"Landroid/graphics/Canvas;"}, "V", &darg, 1,
                   nullptr)) {
    LogTraversalFailure(state, "draw");
    state.ClearException();
    return false;
  }
  return true;
}

bool RunFirstFramePipeline(VMState& state, VObject* activity, dexvm::bridge::UIBridge* ui,
                           int32_t width, int32_t height, const PhaseRunner& run_phase,
                           std::string* error) {
  if (activity == nullptr || ui == nullptr || run_phase == nullptr) {
    if (error != nullptr) *error = "firstFrame: null activity/ui/run_phase";
    return false;
  }
  // 19.6 首帧完成条件首项：launcher Activity RESUMED（驱动器在 onPostResume 后调入）。
  ui->MarkCondition(dexvm::bridge::kActivityResumed);

  VObject* content =
      reinterpret_cast<VObject*>(ViewRef(activity, "mContentView"));  // setContentView 落点
  bool ok = true;
  std::string fail_reason;
  auto step = [&](const char* name, const std::function<bool(std::string*)>& body) {
    if (!ok) return;
    std::string reason;
    if (!run_phase(name, [&](std::string* r) {
          if (!body(&reason)) {
            if (r != nullptr) *r = reason;
            return false;
          }
          return true;
        })) {
      ok = false;
      fail_reason = std::string(name) + ": " + reason;
    }
  };

  // 19.6：addView -> WindowManagerGlobal addView（Guest Window attached）
  step("window.attach", [&](std::string* reason) {
    (void)reason;
    ui->MarkCondition(dexvm::bridge::kWindowAttached);
    // AOSP ViewRootImpl：窗口 attach 后整树收到 onAttachedToWindow +
    // onWindowVisibilityChanged(VISIBLE)。自定义导航/Pager 常在此启动控制器
    // （知乎 BottomNav 的 onWindowVisibilityChanged 即此形态）；异常不失败首帧。
    dexvm::runtime::SetGuestViewAttached(true);
    if (content != nullptr) {
      dexvm::runtime::DispatchViewAttached(state, content, true);
    }
    return true;
  });
  // 19.6/19.4：performTraversals measure->layout->draw -> 缝A draw 命令流
  step("firstFrame.traversal", [&](std::string* reason) {
    if (content == nullptr) {
      *reason = "no content view (setContentView not called)";
      return false;
    }
    // 框架呈现优先（桥接语义）：某个 View 的 measure/layout/draw 因本 VM 尚缺
    // 面失败（PAG/字体等）时，不得让整窗不出画——清异常后照常 present 现有树，
    // 宿主物化器按已落字段渲染（无网络也有基础界面）。后续 session tick 随缺失
    // 面补齐逐轮收敛。
    bool traversed = PerformTraversals(state, content, width, height);
    if (!traversed) {
      state.ClearException();
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "DexVmRender",
                          "traversal degraded: present without full measure/layout");
#endif
    } else {
      // addContentView 覆盖层面（decor content 叠放）：窗口原点重排（尽力）
      if (!LayoutAddedContentViews(state, activity, width, height)) {
        state.ClearException();
      }
      // AOSP performTraversals：layout 后 dispatchOnGlobalLayout + dispatchOnPreDraw
      // （监听器可能同步产生新 View，需在 draw/present 前再走一轮 measure/layout）。
      dexvm::runtime::DispatchViewTreeGlobalLayout(state, content);
      if (state.HasPendingException()) state.ClearException();
      dexvm::runtime::DispatchViewTreePreDraw(state, content);
      if (state.HasPendingException()) state.ClearException();
      if (!PerformTraversals(state, content, width, height)) {
        state.ClearException();
      }
    }
#if defined(__ANDROID__)
    // 布局收敛诊断：首帧后打一次"可见但高度塌缩"的节点清单。
    static std::atomic<bool> tiny_dumped{false};
    if (!tiny_dumped.exchange(true, std::memory_order_relaxed)) {
      int budget = 4000;
      int small = 0;
      DumpTinyViews(content, 0, &budget, &small);
      __android_log_print(ANDROID_LOG_INFO, "DexVmMeasure", "tiny list done count=%d", small);
      VObject* appbar = FindViewByClass(content, "AppBarLayout", 0);
      int subtree_budget = 120;
      __android_log_print(ANDROID_LOG_INFO, "DexVmMeasure", "appbar subtree root=%s",
                          appbar != nullptr ? "found" : "fallback-content");
      DumpViewSubtree(appbar != nullptr ? appbar : content, 0, &subtree_budget);
    }
#endif
    ui->MarkCondition(dexvm::bridge::kTraversalComplete);
    return true;
  });
  // 19.6：HWUI/RenderThread submit（render submitted）
  step("firstFrame.submit", [&](std::string* reason) {
    (void)reason;
    ui->MarkCondition(dexvm::bridge::kRenderSubmitted);
    return true;
  });
  // 19.6：桥轨 present（缝A/缝B）：draw 命令流 + View 树快照 -> PresentViewTree
  // （宿主物化器消费面；自绘位图轨已退役，桥轨是唯一 present 形制）。
  step("firstFrame.present", [&](std::string* reason) {
    dexvm::bridge::ViewTreeExporterFn exporter = dexvm::bridge::GetViewTreeExporter();
    if (exporter == nullptr) {
      *reason = "view tree exporter not registered (link ui_bridge_protocol)";
      return false;
    }
    if (!exporter(&state, content, width, height, ui)) {
      *reason = "view tree export/present rejected";
      return false;
    }
    return true;
  });
  // 19.6：frame-present callback -> mark first guest frame
  step("firstFrame.marked", [&](std::string* reason) {
    if (!ui->first_frame_marked()) {
      *reason = "first frame conditions incomplete";
      return false;
    }
    // AOSP ViewRootImpl.handleWindowFocusChanged：首帧后窗口获得焦点，
    // Activity/View 树收到 onWindowFocusChanged(true)，hasWindowFocus() 变真。
    // app 常以此为数据加载闸门（知乎 feed 链即此形态）；回调异常不失败首帧。
    dexvm::runtime::SetGuestWindowFocused(true);
    Value focus_args[1] = {};
    focus_args[0].i = 1;
    Value focus_out{};
    if (!CallVirtual(state, activity, "onWindowFocusChanged", {"Z"}, "V", focus_args, 1,
                     &focus_out)) {
      state.ClearException();
    }
    if (content != nullptr &&
        !dexvm::runtime::DispatchWindowFocusChanged(state, content, true)) {
      state.ClearException();
    }
    // 焦点回调里常注册 OnGlobalLayoutListener/创建内容：焦点派发后再走一轮
    // global layout + traversal，新视图在后续 session tick 的 diff present 出画。
    if (content != nullptr) {
      dexvm::runtime::DispatchViewTreeGlobalLayout(state, content);
      if (!PerformTraversals(state, content, width, height)) {
        state.ClearException();
      }
    }
    return true;
  });

  if (!ok) {
    if (error != nullptr) *error = fail_reason;
    return false;
  }
  return true;
}

}  // namespace dexvm::framework
