// M4.2（收官迁移轮）：guest 渲染管线冒烟——桥接断言形（自绘像素轨已退役）。
//   1) TextView measure/layout 尺寸语义（文字宽高随 textSize 变化、MeasureSpec
//      收敛）+ 缝A draw 命令流结构断言（drawText/矩形命令的文本/坐标/颜色）；
//   2) LinearLayout 子 View 布局顺序断言（addView 存子 View + 纵向摆放）+
//      递归 draw 命令序（子 View 平移 + 各自 drawText）；
//   3) target-app 全链：lifecycle + 首帧管线 phase 序（13+5）保留；像素金样改
//      桥轨结构断言：协议侧 ViewSnapshot 字段（text/textSize/padding）、
//      revision、首帧标记（参考 tools/ui_bridge_tests.sh 设备断言形）；
//   4) 负例：setContentView 未调用时 traversal 不产出；UI Bridge 首帧完成
//      条件缺一不可（桥轨 PresentViewTree）。
#include <cstdio>
#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "bridge/bridge.h"
#include "bridge/ui_bridge_protocol.h"
#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "framework/lifecycle_driver.h"
#include "framework/render.h"
#include "session/class_loader.h"
#include "session/session.h"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      g_failures++;                                               \
    } else {                                                      \
      std::printf("PASS: %s\n", msg);                             \
    }                                                             \
  } while (0)

using dexvm::bridge::BuildSnapshotFromGuest;
using dexvm::bridge::DrawCommand;
using dexvm::bridge::DrawOp;
using dexvm::bridge::GlobalProtocolSink;
using dexvm::bridge::ParseEnvelope;
using dexvm::bridge::Prop;
using dexvm::bridge::ResetUiBridgeProtocol;
using dexvm::bridge::UIBridge;
using dexvm::bridge::ViewNode;
using dexvm::bridge::ViewPatch;
using dexvm::bridge::ViewSnapshot;
using dexvm::bridge::kPropBounds;
using dexvm::bridge::kPropPadding;
using dexvm::bridge::kPropText;
using dexvm::bridge::kPropTextSize;
using dexvm::framework::Canvas;
using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::framework::MakeMeasureSpec;
using dexvm::framework::PerformTraversals;
using dexvm::framework::PhaseRecord;
using dexvm::framework::PhaseRunner;
using dexvm::framework::RegisterRenderPipeline;
using dexvm::framework::RunFirstFramePipeline;
using dexvm::framework::WrapCanvas;
using dexvm::framework::kAtMost;
using dexvm::framework::kExactly;
using dexvm::framework::kUnspecified;
using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

constexpr uint32_t kRed = 0xFFFF0000u;
constexpr uint32_t kGreen = 0xFF00FF00u;
constexpr uint32_t kBlue = 0xFF0000FFu;

VField* FindField(VClass* klass, const char* name) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == name) return &f;
    }
    for (auto& f : k->static_fields) {
      if (f.name == name) return &f;
    }
  }
  return nullptr;
}

int32_t ReadIField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}

int32_t ViewI(VObject* o, const char* name) {
  VField* f = FindField(o->klass, name);
  return f != nullptr ? ReadIField(o, f->offset) : -12345;
}

bool CallVirtual(VMState& state, VObject* obj, const char* name,
                 std::vector<std::string> params, const char* ret, const Value* args, size_t n,
                 Value* out) {
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return false;
  std::vector<Value> full;
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!InvokeMethod(state, m, full.data(), full.size(), &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

// ---- 缝A 命令流辅助（桥轨结构断言） ----
const Prop* NodeProp(const ViewNode& nd, uint16_t key) {
  for (const Prop& p : nd.props) {
    if (p.key == key) return &p;
  }
  return nullptr;
}

const DrawCommand* FindText(const std::vector<DrawCommand>& stream, const std::u16string& needle) {
  for (const DrawCommand& c : stream) {
    if (c.op == DrawOp::kDrawText && c.text.find(needle) != std::u16string::npos) return &c;
  }
  return nullptr;
}

int CountOp(const std::vector<DrawCommand>& stream, DrawOp op) {
  int n = 0;
  for (const DrawCommand& c : stream) {
    if (c.op == op) n++;
  }
  return n;
}

std::u16string Utf16(const char* s) { return std::u16string(s, s + std::strlen(s)); }

}  // namespace

int main(int argc, char** argv) {
  std::printf("== render smoke (bridge-track assertions) ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string target_apk =
      argc > 2 ? argv[2] : "target-app/build/outputs/apk/debug/target-app-debug.apk";

  // ---- 引导 VM（同 lifecycle_smoke）----
  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  std::string err;
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;
  {
    auto boot = dexvm::session::Session::Open(runtime_dex, &err);
    CHECK(boot != nullptr && !boot->dex_blobs().empty(), "open runtime.dex");
    if (boot == nullptr) return 1;
    for (const auto& blob : boot->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse boot dex: " + blob.name).c_str());
      if (dex_file == nullptr) return 1;
      linker.AddBootDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
  }
  // boot dex 之后登记：lib widget 类（FrameLayout 等）此时才可 Find。
  RegisterRenderPipeline(&linker);
  auto app_session = dexvm::session::Session::Open(target_apk, &err);
  CHECK(app_session != nullptr && !app_session->dex_blobs().empty(), "open target APK");
  if (app_session == nullptr) return 1;
  VObject* app_loader = nullptr;
  {
    for (const auto& blob : app_session->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse app dex: " + blob.name).c_str());
      if (dex_file == nullptr) return 1;
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
    app_loader = linker.AllocObject(cl);
    linker.loaders().BindObject(app_loader, linker.loaders().default_app());
    CHECK(app_loader != nullptr, "app loader bound");
  }

  // 观测面：渲染方法已登记（measure/draw + android.graphics.Canvas）
  CHECK(linker.FindClass("Landroid/graphics/Canvas;") != nullptr, "Canvas class registered");

  // ---- 构件辅助 ----
  auto make_view = [&](const char* desc, bool with_ctx_ctor) -> VObject* {
    VClass* k = linker.FindClass(desc);
    if (k == nullptr) return nullptr;
    VObject* v = linker.AllocObject(k);
    if (v == nullptr) return nullptr;
    Value ctx{};
    bool ok = with_ctx_ctor
                  ? CallVirtual(state, v, "<init>", {"Landroid/content/Context;"}, "V", &ctx, 1,
                                nullptr)
                  : CallVirtual(state, v, "<init>", {}, "V", nullptr, 0, nullptr);
    if (!ok) {
      std::printf("  init failed for %s\n", desc);
      return nullptr;
    }
    return v;
  };
  auto set_text = [&](VObject* v, const char* text) {
    Value sv{};
    sv.ref = linker.AllocString(std::u16string(text, text + std::strlen(text)));
    CHECK(CallVirtual(state, v, "setText", {"Ljava/lang/CharSequence;"}, "V", &sv, 1, nullptr),
          (std::string("setText('") + text + "')").c_str());
  };
  auto set_text_size = [&](VObject* v, float size) {
    Value sv{};
    sv.f = size;
    CHECK(CallVirtual(state, v, "setTextSize", {"F"}, "V", &sv, 1, nullptr), "setTextSize");
  };
  auto set_color = [&](VObject* v, const char* method, uint32_t argb) {
    Value cv{};
    cv.i = static_cast<int32_t>(argb);
    CHECK(CallVirtual(state, v, method, {"I"}, "V", &cv, 1, nullptr), method);
  };
  auto measure = [&](VObject* v, int32_t wspec, int32_t hspec) {
    Value m2[2] = {};
    m2[0].i = wspec;
    m2[1].i = hspec;
    CHECK(CallVirtual(state, v, "measure", {"I", "I"}, "V", m2, 2, nullptr), "measure");
  };
  auto layout = [&](VObject* v, int32_t l, int32_t t, int32_t r, int32_t b) {
    Value lv[4] = {};
    lv[0].i = l;
    lv[1].i = t;
    lv[2].i = r;
    lv[3].i = b;
    CHECK(CallVirtual(state, v, "layout", {"I", "I", "I", "I"}, "V", lv, 4, nullptr), "layout");
  };
  // draw -> 缝A 命令流（自绘像素轨退役：命令记录是唯一 draw 产出）
  auto draw = [&](VObject* v) {
    GlobalProtocolSink().Clear();
    Canvas canvas(64, 48);
    VObject* cx = WrapCanvas(state, &canvas);
    Value dv{};
    dv.ref = cx;
    CHECK(CallVirtual(state, v, "draw", {"Landroid/graphics/Canvas;"}, "V", &dv, 1, nullptr),
          "draw");
    return GlobalProtocolSink().stream();
  };

  // ================================================================
  // 1) TextView measure/layout + 缝A 命令流结构断言
  // ================================================================
  std::printf("---- TextView measure/layout + draw commands ----\n");
  VObject* tv = make_view("Landroid/widget/TextView;", true);
  CHECK(tv != nullptr, "TextView alloc+init");
  if (tv != nullptr) {
    set_text(tv, "hi");
    set_text_size(tv, 16.0f);
    Value pv[4] = {};
    pv[0].i = pv[1].i = pv[2].i = pv[3].i = 4;
    CHECK(CallVirtual(state, tv, "setPadding", {"I", "I", "I", "I"}, "V", pv, 4, nullptr),
          "setPadding(4x4)");

    // UNSPECIFIED：s=2 -> 文字 32x20 + padding 8x8 = 40x28
    measure(tv, MakeMeasureSpec(0, kUnspecified), MakeMeasureSpec(0, kUnspecified));
    CHECK(ViewI(tv, "mMeasuredWidth") == 40, "TextView measured width == 40 (textSize 16)");
    CHECK(ViewI(tv, "mMeasuredHeight") == 28, "TextView measured height == 28 (textSize 16)");
    // AT_MOST(30)：宽被收敛到 30
    measure(tv, MakeMeasureSpec(30, kAtMost), MakeMeasureSpec(0, kUnspecified));
    CHECK(ViewI(tv, "mMeasuredWidth") == 30 && ViewI(tv, "mMeasuredHeight") == 28,
          "TextView AT_MOST clamp");
    // EXACTLY：100x50
    measure(tv, MakeMeasureSpec(100, kExactly), MakeMeasureSpec(50, kExactly));
    CHECK(ViewI(tv, "mMeasuredWidth") == 100 && ViewI(tv, "mMeasuredHeight") == 50,
          "TextView EXACTLY wins");

    // 宽高随 textSize 变化：8 -> 24x18；24 -> 56x38（含 padding 8x8）
    set_text_size(tv, 8.0f);
    measure(tv, MakeMeasureSpec(0, kUnspecified), MakeMeasureSpec(0, kUnspecified));
    CHECK(ViewI(tv, "mMeasuredWidth") == 24 && ViewI(tv, "mMeasuredHeight") == 18,
          "textSize 8 -> 24x18");
    set_text_size(tv, 24.0f);
    measure(tv, MakeMeasureSpec(0, kUnspecified), MakeMeasureSpec(0, kUnspecified));
    CHECK(ViewI(tv, "mMeasuredWidth") == 56 && ViewI(tv, "mMeasuredHeight") == 38,
          "textSize 24 -> 56x38");

    // 多行：'hi\nhi' textSize 16 pad 4 -> 40x48
    set_text(tv, "hi\nhi");
    set_text_size(tv, 16.0f);
    measure(tv, MakeMeasureSpec(0, kUnspecified), MakeMeasureSpec(0, kUnspecified));
    CHECK(ViewI(tv, "mMeasuredWidth") == 40 && ViewI(tv, "mMeasuredHeight") == 48,
          "multiline height doubles");

    // draw 命令流：背景矩形红 + 文字 "ok" 蓝（坐标 = padding 起点）
    set_text(tv, "ok");
    set_color(tv, "setBackgroundColor", kRed);
    set_color(tv, "setTextColor", kBlue);
    layout(tv, 0, 0, 64, 48);
    CHECK(ViewI(tv, "mRight") - ViewI(tv, "mLeft") == 64 &&
              ViewI(tv, "mBottom") - ViewI(tv, "mTop") == 48,
          "layout bounds == 64x48");
    std::vector<DrawCommand> cmds = draw(tv);
    const DrawCommand* bg = nullptr;
    for (const DrawCommand& c : cmds) {
      if (c.op == DrawOp::kFillRect && c.color == kRed) bg = &c;
    }
    CHECK(bg != nullptr && bg->f[0] == 0.0f && bg->f[1] == 0.0f && bg->f[2] == 64.0f &&
              bg->f[3] == 48.0f,
          "background fillRect covers bounds (red)");
    const DrawCommand* tx = FindText(cmds, Utf16("ok"));
    CHECK(tx != nullptr && tx->i[0] == 4 && tx->i[1] == 4 && tx->color == kBlue &&
              tx->f[0] == 16.0f,
          "drawText('ok') at padding(4,4) size 16 color blue");
    CHECK(CountOp(cmds, DrawOp::kDrawText) == 1 && CountOp(cmds, DrawOp::kFillRect) == 1,
          "one drawText + one fillRect in leaf draw");
  }

  // 默认颜色面：无背景 -> 无背景矩形；默认黑字命令
  {
    VObject* tv2 = make_view("Landroid/widget/TextView;", true);
    set_text(tv2, "A");
    set_text_size(tv2, 8.0f);
    layout(tv2, 0, 0, 32, 16);
    std::vector<DrawCommand> cmds = draw(tv2);
    CHECK(CountOp(cmds, DrawOp::kFillRect) == 0, "no background -> no fillRect command");
    const DrawCommand* tx = FindText(cmds, Utf16("A"));
    CHECK(tx != nullptr && tx->color == 0xFF000000u, "default text color draws black glyph command");
  }

  // ================================================================
  // 2) LinearLayout 子 View 布局顺序 + 递归 draw 命令序
  // ================================================================
  std::printf("---- LinearLayout child order + recursive draw commands ----\n");
  VObject* ll = make_view("Landroid/widget/LinearLayout;", true);
  CHECK(ll != nullptr, "LinearLayout alloc+init");
  if (ll != nullptr) {
    struct Child {
      const char* text;
      uint32_t color;
      VObject* obj;
    };
    Child kids[3] = {{"a", kRed, nullptr}, {"bb", kGreen, nullptr}, {"ccc", kBlue, nullptr}};
    for (Child& c : kids) {
      c.obj = make_view("Landroid/widget/TextView;", true);
      set_text(c.obj, c.text);
      set_text_size(c.obj, 8.0f);  // s=1：字格 8x8，行高 10
      set_color(c.obj, "setTextColor", c.color);
      Value av{};
      av.ref = c.obj;
      CHECK(CallVirtual(state, ll, "addView", {"Landroid/view/View;"}, "V", &av, 1, nullptr),
            "addView");
    }
    // addView 存子 View（修 no-op）
    Value cnt{};
    CHECK(CallVirtual(state, ll, "getChildCount", {}, "I", nullptr, 0, &cnt) && cnt.i == 3,
          "getChildCount == 3 after addView");
    Value idx{};
    idx.i = 1;
    Value got{};
    CHECK(CallVirtual(state, ll, "getChildAt", {"I"}, "Landroid/view/View;", &idx, 1, &got) &&
              got.ref == kids[1].obj,
          "getChildAt(1) == second child");

    // measure：宽 max(8,16,24)=24，高 10+10+10=30
    measure(ll, MakeMeasureSpec(0, kUnspecified), MakeMeasureSpec(0, kUnspecified));
    CHECK(ViewI(ll, "mMeasuredWidth") == 24 && ViewI(ll, "mMeasuredHeight") == 30,
          "LinearLayout stacks children vertically (24x30)");
    // layout：子 View 纵向首尾相接
    layout(ll, 0, 0, 24, 30);
    bool order_ok = true;
    for (int i = 0; i < 3; i++) {
      int32_t top = i * 10;
      order_ok = order_ok && ViewI(kids[i].obj, "mLeft") == 0 && ViewI(kids[i].obj, "mTop") == top &&
                 ViewI(kids[i].obj, "mRight") == (i == 0 ? 8 : (i == 1 ? 16 : 24)) &&
                 ViewI(kids[i].obj, "mBottom") == top + 10;
    }
    CHECK(order_ok, "children laid out in addView order (0/10/20)");
    CHECK(ViewI(kids[1].obj, "mTop") == ViewI(kids[0].obj, "mBottom") &&
              ViewI(kids[2].obj, "mTop") == ViewI(kids[1].obj, "mBottom"),
          "child stacking contiguous");

    // 递归 draw：三种文字颜色命令按 addView 序 + 子 View 平移记录
    std::vector<DrawCommand> cmds = draw(ll);
    const DrawCommand* t0 = FindText(cmds, Utf16("a"));
    const DrawCommand* t1 = FindText(cmds, Utf16("bb"));
    const DrawCommand* t2 = FindText(cmds, Utf16("ccc"));
    CHECK(t0 != nullptr && t0->color == kRed, "child0 drawText (red)");
    CHECK(t1 != nullptr && t1->color == kGreen, "child1 drawText (green)");
    CHECK(t2 != nullptr && t2->color == kBlue, "child2 drawText (blue)");
    CHECK(t0 < t1 && t1 < t2, "drawText order follows addView order");
    CHECK(CountOp(cmds, DrawOp::kTranslate) >= 3, "child placement translates recorded");

    // addView(View, index)：插到队首
    VObject* head = make_view("Landroid/widget/TextView;", true);
    Value hv[2] = {};
    hv[0].ref = head;
    hv[1].i = 0;
    CHECK(CallVirtual(state, ll, "addView", {"Landroid/view/View;", "I"}, "V", hv, 2, nullptr),
          "addView(child, 0)");
    Value cnt2{};
    CallVirtual(state, ll, "getChildCount", {}, "I", nullptr, 0, &cnt2);
    Value idx0{};
    idx0.i = 0;
    Value got0{};
    CallVirtual(state, ll, "getChildAt", {"I"}, "Landroid/view/View;", &idx0, 1, &got0);
    Value idx1{};
    idx1.i = 1;
    Value got1{};
    CallVirtual(state, ll, "getChildAt", {"I"}, "Landroid/view/View;", &idx1, 1, &got1);
    CHECK(cnt2.i == 4 && got0.ref == head && got1.ref == kids[0].obj,
          "addView(child, 0) inserts at front");
  }

  // ================================================================
  // 3) target-app 全链：lifecycle + 首帧管线 + 桥轨结构断言
  // ================================================================
  std::printf("---- target-app first frame chain (bridge track) ----\n");
  ResetUiBridgeProtocol();
  const dexvm::session::ManifestInfo& mi = app_session->manifest();
  LifecycleConfig cfg = ConfigFromManifest(mi);
  UIBridge ui;
  LifecycleDriver driver(state, app_loader);
  driver.set_first_frame_hook([&](VObject* activity, const PhaseRunner& run_phase,
                                  std::string* error) {
    return RunFirstFramePipeline(state, activity, &ui, 540, 960, run_phase, error);
  });
  bool ran = driver.Run(cfg);
  CHECK(ran, "lifecycle + first-frame pipeline completes");
  if (!ran) std::printf("  error: %s\n", driver.error().c_str());

  // 阶段序列：13 个 AOSP 生命周期 + 5 个首帧阶段（19.6 顺序）
  const char* kExpectedPhases[] = {
      "frameworkState",        "application.create",     "application.attachBaseContext",
      "provider[0].create",    "provider[0].attachInfo", "provider[0].onCreate",
      "application.onCreate",  "activity.create",        "activity.attach",
      "activity.onCreate",     "activity.onStart",       "activity.onResume",
      "activity.onPostResume", "window.attach",          "firstFrame.traversal",
      "firstFrame.submit",     "firstFrame.present",     "firstFrame.marked",
  };
  bool phases_ok = driver.phases().size() == std::size(kExpectedPhases);
  for (size_t i = 0; phases_ok && i < std::size(kExpectedPhases); i++) {
    if (driver.phases()[i].name != kExpectedPhases[i] || !driver.phases()[i].ok) {
      std::printf("  phase[%zu]: got %s (ok=%d), want %s\n", i,
                  driver.phases()[i].name.c_str(), driver.phases()[i].ok ? 1 : 0,
                  kExpectedPhases[i]);
      phases_ok = false;
    }
  }
  CHECK(phases_ok, "phase sequence = AOSP 13 + first-frame 5 (19.6 order)");

  // "首帧完成" 状态（docs 10 章 L133-141 五条件）
  CHECK(ui.first_frame_complete() && ui.first_frame_marked(),
        "first frame complete + marked (RESUMED/attached/traversal/render/presented)");
  CHECK(ui.view_tree_present_count() == 1, "exactly one view-tree present");

  // 桥轨出画内容（协议侧 ViewSnapshot 字段 + revision + draw 命令流；
  // 对照 tools/ui_bridge_tests.sh 设备断言形：文本/类名/bridge rev）
  {
    ViewSnapshot full;
    ViewPatch patch;
    std::vector<DrawCommand> draw_cmds;
    uint32_t rev = 0;
    bool is_patch = true;
    CHECK(ParseEnvelope(ui.last_view_tree(), &full, &patch, &draw_cmds, &rev, &is_patch) &&
              !is_patch && rev == 1,
          "first export = full snapshot (revision 1)");
    CHECK(!full.nodes.empty(), "snapshot non-empty");
    const ViewNode* root = nullptr;
    bool class_ok = false;
    bool text_ok = false;
    for (const ViewNode& nd : full.nodes) {
      if (nd.parent_id == dexvm::bridge::kNoObject) root = &nd;
      if (nd.class_name == "TextView") class_ok = true;
      const Prop* t = NodeProp(nd, kPropText);
      if (t != nullptr && t->value.s.find("target ok") != std::string::npos) text_ok = true;
    }
    CHECK(root != nullptr && root->class_name == "TextView",
          "snapshot root = TextView (setContentView 落点)");
    CHECK(class_ok, "snapshot carries TextView node (materializer class)");
    CHECK(text_ok, "snapshot carries 'target ok' text (materializer input)");
    if (root != nullptr) {
      const Prop* t = NodeProp(*root, kPropText);
      CHECK(t != nullptr &&
                t->value.s == "target ok\nattachBaseContext;provider.onCreate;onCreate;"
                              "activity.onCreate;",
            "root text prop == lifecycle fixture content");
      const Prop* ts = NodeProp(*root, kPropTextSize);
      CHECK(ts != nullptr && ts->value.f == 16.0f, "root textSize prop == 16");
      const Prop* pad = NodeProp(*root, kPropPadding);
      CHECK(pad != nullptr && pad->value.rect[0] == 48 && pad->value.rect[1] == 48 &&
                pad->value.rect[2] == 48 && pad->value.rect[3] == 48,
            "root padding prop == 48x4");
      const Prop* bounds = NodeProp(*root, kPropBounds);
      CHECK(bounds != nullptr && bounds->value.rect[2] - bounds->value.rect[0] == 540 &&
                bounds->value.rect[3] - bounds->value.rect[1] == 960,
            "root bounds prop == 540x960 window");
    }
    bool draw_text_ok = false;
    for (const DrawCommand& c : draw_cmds) {
      if (c.op == DrawOp::kDrawText && c.text.find(Utf16("target ok")) != std::u16string::npos) {
        draw_text_ok = true;
      }
    }
    CHECK(draw_text_ok, "draw stream carries 'target ok' drawText");
    CHECK(!draw_cmds.empty(), "draw stream non-empty in envelope");
  }

  // ================================================================
  // 4) 负例
  // ================================================================
  std::printf("---- negative: no contentView / bridge conditions ----\n");
  {
    // setContentView 未调用：traversal 不产出（空语义）
    VObject* bare = make_view("Landroid/app/Activity;", false);
    CHECK(bare != nullptr, "bare Activity alloc");
    VField* cv_f = bare != nullptr ? FindField(bare->klass, "mContentView") : nullptr;
    ObjHeader* cv = nullptr;
    if (cv_f != nullptr) std::memcpy(&cv, bare->field_data() + cv_f->offset, sizeof(cv));
    CHECK(cv == nullptr, "setContentView not called -> mContentView == null");

    CHECK(!PerformTraversals(state, reinterpret_cast<VObject*>(cv), 540, 960),
          "traversal without content view fails");

    UIBridge ui2;
    std::vector<PhaseRecord> phases2;
    PhaseRunner runner2 = [&](const std::string& name,
                              const std::function<bool(std::string*)>& fn) {
      PhaseRecord rec;
      rec.name = name;
      std::string reason;
      rec.ok = fn(&reason);
      rec.error = reason;
      phases2.push_back(rec);
      return rec.ok;
    };
    std::string err2;
    bool neg_ok = RunFirstFramePipeline(state, bare, &ui2, 540, 960, runner2, &err2);
    CHECK(!neg_ok, "first-frame pipeline fails without content view");
    CHECK(phases2.size() == 2 && phases2[0].name == "window.attach" && phases2[0].ok &&
              phases2[1].name == "firstFrame.traversal" && !phases2[1].ok,
          "phases stop at firstFrame.traversal");
    CHECK(err2.find("content view") != std::string::npos ||
              err2.find("setContentView") != std::string::npos,
          "negative diagnostic mentions missing content view");
    CHECK(!ui2.first_frame_complete() && !ui2.first_frame_marked(),
          "negative: first frame not marked");
  }
  {
    // UI Bridge 首帧完成条件缺一不可（桥轨 PresentViewTree 形）
    UIBridge b;
    b.MarkCondition(dexvm::bridge::kActivityResumed | dexvm::bridge::kWindowAttached);
    CHECK(!b.first_frame_complete() && !b.first_frame_marked(),
          "conditions incomplete before traversal/submit");
    CHECK(b.PresentViewTree({0x55, 0x49, 0x42, 0x52}) && b.view_tree_present_count() == 1,
          "PresentViewTree accepts non-empty envelope");
    CHECK(!b.first_frame_complete() && !b.first_frame_marked(),
          "present alone does not complete first frame");
    b.MarkCondition(dexvm::bridge::kTraversalComplete | dexvm::bridge::kRenderSubmitted);
    CHECK(b.first_frame_complete() && b.first_frame_marked(),
          "all five conditions -> first frame marked");
    CHECK(!b.PresentViewTree({}), "PresentViewTree rejects empty envelope");
    CHECK(b.view_tree_present_count() == 1, "present count retained");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
