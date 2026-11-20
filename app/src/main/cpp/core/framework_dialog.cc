// Dialog 窗口面实现（见 core/framework_dialog.h）。
//
// 与 AOSP 的差异（单窗口 VM 口径）：Dialog 不是 WindowManager 真实子窗口，
// 而是「以会话内容根为逻辑屏幕」的叠加窗口根；布局由 VM 直接执行
// measure/layout，快照携带 kPropWindowDim 表达 dim，宿主 UI 桥负责物化叠加层。
#include "core/framework_dialog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/builtins_internal.h"
#include "core/framework_builtins_internal.h"
#include "core/gc.h"
#include "core/interpreter.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace dexvm::runtime {

namespace {

constexpr int kMatchParent = -1;
constexpr int kWrapContent = -2;
// 对话框与屏幕边缘的最小留白（guest 像素；540x960@320dpi 下约 10dp）。
constexpr int kDialogMargin = 32;
// View.MeasureSpec 模式位。
constexpr int kModeExactly = 0x40000000;
constexpr int kModeAtMost = 0x80000000;
// android.view.Gravity 常量（公开值）。
constexpr int kGravityCenter = 17;
constexpr int kGravityCenterHorizontal = 1;
constexpr int kGravityCenterVertical = 16;
constexpr int kGravityTop = 48;
constexpr int kGravityBottom = 80;
constexpr int kGravityLeft = 3;
constexpr int kGravityRight = 5;
constexpr int kGravityStart = 0x00800003;
constexpr int kGravityEnd = 0x00800005;

struct DialogEntry {
  VObject* dialog = nullptr;
  VObject* content = nullptr;
  int32_t width = kWrapContent;
  int32_t height = kWrapContent;
  int32_t gravity = -1;
  float dim = 0.32f;
  // PopupWindow 叠加层：kind=1 时 (x,y) 为 guest 坐标定位，token 为宿主弹层
  // 桥身份（外点关闭回灌）。
  int32_t kind = 0;
  int32_t x = 0;
  int32_t y = 0;
  uint32_t token = 0;
};

std::vector<DialogEntry>& Registry() {
  static std::vector<DialogEntry> registry;
  return registry;
}

VObject* g_host_content = nullptr;
int32_t g_host_width = 540;
int32_t g_host_height = 960;

int32_t FieldI(VObject* o, const char* name, int32_t fallback) {
  if (o == nullptr || o->klass == nullptr) return fallback;
  VField* f = FindInstanceField(o->klass, name);
  return f != nullptr ? GetIField(o, f->offset) : fallback;
}

int32_t MakeSpec(int32_t size, int32_t mode) {
  if (size < 0) size = 0;
  return mode | (size & 0x3FFFFFFF);
}

int32_t DialogHostWidth() {
  if (g_host_content != nullptr && g_host_content->klass != nullptr) {
    int32_t w = FieldI(g_host_content, "mRight", 0) - FieldI(g_host_content, "mLeft", 0);
    if (w > 0) return w;
  }
  return g_host_width > 0 ? g_host_width : 540;
}

int32_t DialogHostHeight() {
  if (g_host_content != nullptr && g_host_content->klass != nullptr) {
    int32_t h = FieldI(g_host_content, "mBottom", 0) - FieldI(g_host_content, "mTop", 0);
    if (h > 0) return h;
  }
  return g_host_height > 0 ? g_host_height : 960;
}

void Diag(const char* what, const std::string& detail) {
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmDialog", "%s %s", what, detail.c_str());
#else
  (void)what;
  (void)detail;
#endif
}

// GC 外部根：窗口登记表是 dialog/content 的唯一 C++ 侧强引用（AOSP 由
// WindowManager 持有；本 VM 由该表持有），不登记会被 GC 回收。
void MarkDialogRoots(void* ctx, dexvm::gc::ExternalMarkFn mark, void* mark_ctx) {
  (void)ctx;
  for (auto& e : Registry()) {
    if (e.dialog != nullptr) mark(mark_ctx, e.dialog);
    if (e.content != nullptr) mark(mark_ctx, e.content);
  }
}

void EnsureDialogRootsRegistered() {
  static bool registered = false;
  if (!registered) {
    registered = true;
    dexvm::gc::AddExternalRootVisitor(&MarkDialogRoots, nullptr);
  }
}

// 单个窗口的 measure/layout（Window 宽高意图 -> spec；默认居中）。
// PopupWindow 叠加层：不带 dim + 不收窄留白，位置取登记时的 guest 坐标
// （AOSP showAsDropDown/showAtLocation 的锚定结果），越界时向屏幕内侧收。
void LayoutOne(VMState& state, DialogEntry& e) {
  if (e.content == nullptr || e.content->klass == nullptr) return;
  const int32_t host_w = DialogHostWidth();
  const int32_t host_h = DialogHostHeight();
  const bool is_popup = e.kind == 1;
  const int32_t margin = is_popup ? 8 : kDialogMargin;
  const int32_t avail_w = std::max(1, host_w - 2 * margin);
  const int32_t avail_h = std::max(1, host_h - 2 * margin);

  int32_t width_spec_mode = kModeAtMost;
  int32_t width_size = avail_w;
  if (e.width == kMatchParent) {
    width_spec_mode = kModeExactly;
    width_size = avail_w;
  } else if (e.width > 0) {
    width_spec_mode = kModeExactly;
    width_size = std::min(e.width, avail_w);
  }
  int32_t height_spec_mode = kModeAtMost;
  int32_t height_size = avail_h;
  if (e.height == kMatchParent) {
    height_spec_mode = kModeExactly;
    height_size = avail_h;
  } else if (e.height > 0) {
    height_spec_mode = kModeExactly;
    height_size = std::min(e.height, avail_h);
  }

  Value margins[2] = {};
  margins[0].i = MakeSpec(width_size, width_spec_mode);
  margins[1].i = MakeSpec(height_size, height_spec_mode);
  if (!CallVirtual(state, e.content, "measure", {"I", "I"}, "V", margins, 2, nullptr)) {
    if (state.HasPendingException()) state.ClearException();
    Diag("measure failed", "content=" + std::to_string(reinterpret_cast<uintptr_t>(e.content)));
    return;
  }
  int32_t cw = FieldI(e.content, "mMeasuredWidth", 0);
  int32_t ch = FieldI(e.content, "mMeasuredHeight", 0);
  if (cw <= 0) cw = width_size;
  if (ch <= 0) ch = height_size;
  cw = std::min(cw, host_w);
  ch = std::min(ch, host_h);

  const int32_t g = e.gravity;
  int32_t left;
  int32_t top;
  if (is_popup) {
    // 弹层按锚点坐标定位；只做屏幕内收边（AOSP 同样会移入屏幕）。
    left = e.x;
    top = e.y;
  } else if (g == kGravityLeft || g == kGravityStart) {
    left = kDialogMargin;
  } else if (g == kGravityRight || g == kGravityEnd) {
    left = host_w - cw - kDialogMargin;
  } else {
    left = (host_w - cw) / 2;
  }
  if (!is_popup) {
    if (g == kGravityTop) {
      top = kDialogMargin;
    } else if (g == kGravityBottom) {
      top = host_h - ch - kDialogMargin;
    } else {
      (void)kGravityCenter;
      (void)kGravityCenterHorizontal;
      (void)kGravityCenterVertical;
      top = (host_h - ch) / 2;
    }
  }
  left = std::max(0, std::min(left, host_w - cw));
  top = std::max(0, std::min(top, host_h - ch));

  Value bounds[4] = {};
  bounds[0].i = left;
  bounds[1].i = top;
  bounds[2].i = left + cw;
  bounds[3].i = top + ch;
  if (!CallVirtual(state, e.content, "layout", {"I", "I", "I", "I"}, "V", bounds, 4, nullptr)) {
    if (state.HasPendingException()) state.ClearException();
    Diag("layout failed", "content=" + std::to_string(reinterpret_cast<uintptr_t>(e.content)));
  }
}

}  // namespace

void SetDialogHostView(VObject* content, int32_t width, int32_t height) {
  g_host_content = content;
  if (width > 0) g_host_width = width;
  if (height > 0) g_host_height = height;
}

Value VmDialogShow(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr) return Value{};
  VObject* dialog = reinterpret_cast<VObject*>(args[0].ref);
  VObject* content = reinterpret_cast<VObject*>(args[1].ref);
  Diag("show dialog class=",
       std::string(dialog->klass != nullptr ? dialog->klass->descriptor : "?") + " content=" +
           (content->klass != nullptr ? content->klass->descriptor : "?"));
  DialogEntry entry;
  entry.dialog = dialog;
  entry.content = content;
  entry.width = n >= 3 ? static_cast<int32_t>(args[2].i) : kWrapContent;
  entry.height = n >= 4 ? static_cast<int32_t>(args[3].i) : kWrapContent;
  entry.gravity = n >= 5 ? static_cast<int32_t>(args[4].i) : -1;
  entry.dim = n >= 6 ? args[5].f : 0.32f;
  if (entry.dim < 0.0f) entry.dim = 0.0f;
  if (entry.dim > 0.9f) entry.dim = 0.9f;

  auto& reg = Registry();
  bool replaced = false;
  for (auto& e : reg) {
    if (e.dialog == dialog || e.content == content) {
      e = entry;
      replaced = true;
      break;
    }
  }
  if (!replaced) reg.push_back(entry);
  EnsureDialogRootsRegistered();
  for (auto& e : reg) {
    if (e.dialog == dialog) {
      LayoutOne(state, e);
      break;
    }
  }
  // 窗口根出现/内容变化 -> 下一轮 traversal 重绘
  RequestGuestLayout();
  return Value{};
}

Value VmDialogDismiss(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)state;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  VObject* dialog = reinterpret_cast<VObject*>(args[0].ref);
  auto& reg = Registry();
  for (auto it = reg.begin(); it != reg.end(); ++it) {
    if (it->dialog == dialog) {
      reg.erase(it);
      RequestGuestLayout();
      break;
    }
  }
  return Value{};
}

size_t LayoutDialogWindows(VMState& state) {
  auto& reg = Registry();
  for (auto& e : reg) LayoutOne(state, e);
  return reg.size();
}

void ShowPopupWindow(VMState& state, VObject* popup, VObject* content, int32_t x, int32_t y,
                     int32_t width, int32_t height, uint32_t token) {
  if (popup == nullptr || content == nullptr) return;
  Diag("show popup class=",
       std::string(popup->klass != nullptr ? popup->klass->descriptor : "?") + " content=" +
           (content->klass != nullptr ? content->klass->descriptor : "?") + " at " +
           std::to_string(x) + "," + std::to_string(y) + " token=" + std::to_string(token));
  DialogEntry entry;
  entry.dialog = popup;
  entry.content = content;
  entry.width = width;
  entry.height = height;
  entry.dim = 0.0f;
  entry.kind = 1;
  entry.x = x;
  entry.y = y;
  entry.token = token;

  auto& reg = Registry();
  bool replaced = false;
  for (auto& e : reg) {
    if (e.dialog == popup || e.content == content) {
      e = entry;
      replaced = true;
      break;
    }
  }
  if (!replaced) reg.push_back(entry);
  EnsureDialogRootsRegistered();
  // 用登记后的条目布局（替换位置可能不在表尾）。
  for (auto& e : reg) {
    if (e.dialog == popup) {
      LayoutOne(state, e);
      break;
    }
  }
  RequestGuestLayout();
}

void DismissPopupWindow(VMState& state, VObject* popup) {
  (void)state;
  if (popup == nullptr) return;
  auto& reg = Registry();
  for (auto it = reg.begin(); it != reg.end(); ++it) {
    if (it->dialog == popup) {
      reg.erase(it);
      Diag("dismiss popup", "content=" + std::to_string(reinterpret_cast<uintptr_t>(popup)));
      RequestGuestLayout();
      break;
    }
  }
}

size_t CollectDialogWindows(DialogWindowInfo* out, size_t max) {
  if (out == nullptr || max == 0) return 0;
  auto& reg = Registry();
  size_t count = 0;
  for (auto& e : reg) {
    if (count >= max) break;
    out[count].content = e.content;
    out[count].dim = e.dim;
    out[count].kind = e.kind;
    out[count].token = e.token;
    count++;
  }
  return count;
}

// ---- 设备验证探针（marker 文件触发；仅设备诊断，不参与 app 正常路径）----
//
// 用真实 lib 链路走一遍 Dialog.show：android.app.Dialog + Window + 内容树
// （LinearLayout/TextView builtin）→ VmLayoutHost.showDialog intrinsic →
// 窗口登记 → 快照叠加根（kPropWindowDim）→ 宿主物化。验证 show/dismiss
// 两端与 dim 遮罩，避免只能等 app 恰好走到某个弹框路径。
namespace {

VObject* g_probe_dialog = nullptr;

void ProbeClearException(VMState& state) {
  if (state.HasPendingException()) state.ClearException();
}

void SetTextColorField(VObject* view, int32_t argb) {
  if (view == nullptr || view->klass == nullptr) return;
  VField* f = FindInstanceField(view->klass, "mTextColor");
  if (f != nullptr) SetIField(view, f->offset, argb);
}

bool ProbeCall(VMState& state, VObject* self, const char* name,
               std::vector<std::string> params, const char* ret, const Value* args, size_t n) {
  if (self == nullptr) return false;
  Value ignored{};
  if (CallVirtual(state, self, name, std::move(params), ret, args, n, &ignored)) return true;
  ProbeClearException(state);
  return false;
}

VObject* MakeProbeView(VMState& state, const char* desc, VObject* activity, bool needs_attrs) {
  bool ok = false;
  Value args[2] = {};
  args[0].ref = activity;
  args[1].ref = nullptr;
  if (needs_attrs) {
    return NewLibObject(state, desc,
                        {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, args, 2,
                        &ok);
  }
  return NewLibObject(state, desc, {"Landroid/content/Context;"}, args, 1, &ok);
}

}  // namespace

bool DebugDialogProbeShow(VMState& state, VObject* activity) {
  if (activity == nullptr || state.linker == nullptr || g_probe_dialog != nullptr) return false;
  VObject* dialog = MakeProbeView(state, "Landroid/app/Dialog;", activity, false);
  if (dialog == nullptr) {
    ProbeClearException(state);
    Diag("probe: dialog create failed", state.linker->last_error());
    return false;
  }
  // LinearLayout 仅注册了 (Context, AttributeSet) 构造（XML inflate 口径）。
  VObject* linear = MakeProbeView(state, "Landroid/widget/LinearLayout;", activity, true);
  if (linear == nullptr) {
    ProbeClearException(state);
    Diag("probe: linear create failed", state.linker->last_error());
    return false;
  }
  Value iv[1] = {};
  iv[0].i = 1;  // VERTICAL
  ProbeCall(state, linear, "setOrientation", {"I"}, "V", iv, 1);
  iv[0].i = 0xFFFFFFFF;  // 白底
  ProbeCall(state, linear, "setBackgroundColor", {"I"}, "V", iv, 1);
  Value pad[4] = {};
  pad[0].i = pad[1].i = pad[2].i = pad[3].i = 48;
  ProbeCall(state, linear, "setPadding", {"I", "I", "I", "I"}, "V", pad, 4);

  VObject* title = MakeProbeView(state, "Landroid/widget/TextView;", activity, false);
  VObject* body = MakeProbeView(state, "Landroid/widget/TextView;", activity, false);
  if (title == nullptr || body == nullptr) {
    ProbeClearException(state);
    Diag("probe: text create failed", state.linker->last_error());
    return false;
  }
  Value tv[1] = {};
  tv[0].ref = state.linker->AllocString(u"DexVM Dialog 窗口验证");
  ProbeCall(state, title, "setText", {"Ljava/lang/CharSequence;"}, "V", tv, 1);
  tv[0].f = 18.0f;
  ProbeCall(state, title, "setTextSize", {"F"}, "V", tv, 1);
  SetTextColorField(title, 0xFF222222);

  tv[0].ref = state.linker->AllocString(u"guest 内容树经快照导出的叠加窗口（dim 遮罩）");
  ProbeCall(state, body, "setText", {"Ljava/lang/CharSequence;"}, "V", tv, 1);
  tv[0].f = 14.0f;
  ProbeCall(state, body, "setTextSize", {"F"}, "V", tv, 1);
  SetTextColorField(body, 0xFF666666);

  Value av[1] = {};
  av[0].ref = title;
  ProbeCall(state, linear, "addView", {"Landroid/view/View;"}, "V", av, 1);
  av[0].ref = body;
  ProbeCall(state, linear, "addView", {"Landroid/view/View;"}, "V", av, 1);

  Value cv[1] = {};
  cv[0].ref = linear;
  if (!ProbeCall(state, dialog, "setContentView", {"Landroid/view/View;"}, "V", cv, 1)) {
    Diag("probe: setContentView failed", state.linker->last_error());
    return false;
  }
  // 诊断：确认 Dialog.mWindow.mContentView 的身份（应为我们创建的 linear）。
  {
    VField* wf = FindInstanceField(dialog->klass, "mWindow");
    VObject* win =
        wf != nullptr ? reinterpret_cast<VObject*>(GetRefField(dialog, wf->offset)) : nullptr;
    VField* cf =
        win != nullptr && win->klass != nullptr ? FindInstanceField(win->klass, "mContentView")
                                                : nullptr;
    VObject* wc =
        cf != nullptr ? reinterpret_cast<VObject*>(GetRefField(win, cf->offset)) : nullptr;
    Diag("probe window content class=",
        std::string(wc != nullptr && wc->klass != nullptr ? wc->klass->descriptor : "null") +
            " window=" +
            (win != nullptr && win->klass != nullptr ? win->klass->descriptor : "null") +
            " linear=" + (linear->klass != nullptr ? linear->klass->descriptor : "?"));
  }
  if (!ProbeCall(state, dialog, "show", {}, "V", nullptr, 0)) {
    Diag("probe: show failed", state.linker->last_error());
    return false;
  }
  g_probe_dialog = dialog;
  Diag("probe: shown", "content=" + std::to_string(reinterpret_cast<uintptr_t>(linear)));
  return true;
}

// AppCompat AlertDialog 探针：app 的真实弹框（隐私弹框 t 等）都走
// androidx.appcompat.app.h 家族；该路径经 AppCompatDelegate/AlertController
// installContent（Window.setContentView(int)）落内容树，最贴近 app 行为。
bool DebugDialogProbeShowAppCompat(VMState& state, VObject* activity) {
  if (activity == nullptr || state.linker == nullptr || g_probe_dialog != nullptr) return false;
  VObject* dialog = MakeProbeView(state, "Landroidx/appcompat/app/d;", activity, false);
  if (dialog == nullptr) {
    ProbeClearException(state);
    Diag("probe appcompat: create failed", state.linker->last_error());
    return false;
  }
  Value tv[1] = {};
  tv[0].ref = state.linker->AllocString(u"AppCompat 弹框验证");
  if (!ProbeCall(state, dialog, "setTitle", {"Ljava/lang/CharSequence;"}, "V", tv, 1)) {
    Diag("probe appcompat: setTitle failed", state.linker->last_error());
  }
  tv[0].ref = state.linker->AllocString(u"AlertController installContent -> 窗口内容根");
  ProbeCall(state, dialog, "setMessage", {"Ljava/lang/CharSequence;"}, "V", tv, 1);
  ProbeClearException(state);
  if (!ProbeCall(state, dialog, "show", {}, "V", nullptr, 0)) {
    Diag("probe appcompat: show failed", state.linker->last_error());
    return false;
  }
  g_probe_dialog = dialog;
  Diag("probe appcompat: shown", "");
  return true;
}

void DebugDialogProbeDismiss(VMState& state) {
  if (g_probe_dialog == nullptr) return;
  ProbeCall(state, g_probe_dialog, "dismiss", {}, "V", nullptr, 0);
  ProbeClearException(state);
  g_probe_dialog = nullptr;
  Diag("probe: dismissed", "");
}

}  // namespace dexvm::runtime
