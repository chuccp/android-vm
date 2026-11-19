// UI 渲染桥接 P1 冒烟（guest View 树 -> 桥接出画）：
//   1) 协议往返：ViewSnapshot/ViewPatch/draw 命令流序列化（revision 管理、
//      全量/增量、viewId 映射）；
//   2) 快照/patch 断言：guest View 树字段态（id/text/visibility/bounds/padding…）
//      快照导出（缝B）+ 增量 patch 应用一致性；
//   3) 缝A 命令记录：drawText/Rect/Bitmap/save/restore/矩阵进协议流；
//   4) 点击回灌：host(viewId) -> guest listener 注入 -> onClick 派发（真实
//      guest 字节码探针）+ 负例；
//   5) 桥轨首帧：target-app 全链 + PresentViewTree（宿主物化器消费面）+ 首帧
//      五条件（docs 10 章 19.6）。
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "bridge/bridge.h"
#include "bridge/ui_bridge_input.h"
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

using dexvm::bridge::ApplyPatch;
using dexvm::bridge::BuildSnapshotFromGuest;
using dexvm::bridge::BuildViewIdMap;
using dexvm::bridge::DiffSnapshots;
using dexvm::bridge::DrawCommand;
using dexvm::bridge::DrawOp;
using dexvm::bridge::FindByGuestId;
using dexvm::bridge::GetViewTreeExporter;
using dexvm::bridge::GlobalProtocolSink;
using dexvm::bridge::InjectGuestClick;
using dexvm::bridge::ParseEnvelope;
using dexvm::bridge::Prop;
using dexvm::bridge::PropType;
using dexvm::bridge::PropValue;
using dexvm::bridge::ResetUiBridgeProtocol;
using dexvm::bridge::SerializeFull;
using dexvm::bridge::SerializePatch;
using dexvm::bridge::UIBridge;
using dexvm::bridge::ViewNode;
using dexvm::bridge::ViewPatch;
using dexvm::bridge::ViewSnapshot;
using dexvm::bridge::ViewTreeMirror;
using dexvm::bridge::kNoObject;
using dexvm::bridge::kPropAlpha;
using dexvm::bridge::kPropBackgroundColor;
using dexvm::bridge::kPropBounds;
using dexvm::bridge::kPropHasClickListener;
using dexvm::bridge::kPropLayoutParams;
using dexvm::bridge::kPropPadding;
using dexvm::bridge::kPropText;
using dexvm::bridge::kPropTextColor;
using dexvm::bridge::kPropTextSize;
using dexvm::bridge::kPropVisibility;
using dexvm::framework::Canvas;
using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::framework::MakeMeasureSpec;
using dexvm::framework::PerformTraversals;
using dexvm::framework::PhaseRunner;
using dexvm::framework::RegisterRenderPipeline;
using dexvm::framework::RunFirstFramePipeline;
using dexvm::framework::WrapCanvas;
using dexvm::framework::kUnspecified;
using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

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

void WriteIField(VObject* o, uint32_t off, int32_t v) {
  uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
  std::memcpy(o->field_data() + off, &slot, sizeof(slot));
}

int32_t ViewI(VObject* o, const char* name) {
  VField* f = FindField(o->klass, name);
  return f != nullptr ? ReadIField(o, f->offset) : -12345;
}

Value StaticValue(VClass* k, const char* name) {
  Value v{};
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& f : c->static_fields) {
      if (f.name == name && f.offset < c->static_storage.size()) {
        return c->static_storage[f.offset];
      }
    }
  }
  return v;
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

const Prop* NodeProp(const ViewNode& nd, uint16_t key) {
  for (const Prop& pr : nd.props) {
    if (pr.key == key) return &pr;
  }
  return nullptr;
}

bool SnapshotEquals(const ViewSnapshot& a, const ViewSnapshot& b) {
  if (a.nodes.size() != b.nodes.size()) return false;
  for (size_t i = 0; i < a.nodes.size(); i++) {
    const ViewNode& x = a.nodes[i];
    const ViewNode& y = b.nodes[i];
    if (x.handle.object_id != y.handle.object_id || x.guest_view_id != y.guest_view_id ||
        x.class_name != y.class_name || x.parent_id != y.parent_id ||
        x.child_index != y.child_index || x.props.size() != y.props.size()) {
      return false;
    }
    for (size_t j = 0; j < x.props.size(); j++) {
      if (x.props[j].key != y.props[j].key || !(x.props[j].value == y.props[j].value)) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== ui bridge smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string target_apk =
      argc > 2 ? argv[2] : "target-app/build/outputs/apk/debug/target-app-debug.apk";
  const std::string probe_dex =
      argc > 3 ? argv[3] : "app/src/main/cpp/tests/out/ui_bridge_fixture.dex";

  // ================================================================
  // 1) 协议往返（零 VM：数据形 + 序列化 + revision 管理）
  // ================================================================
  std::printf("---- protocol roundtrip ----\n");
  {
    ViewSnapshot s1;
    ViewNode root;
    root.handle.object_id = 1;
    root.class_name = "LinearLayout";
    root.parent_id = kNoObject;
    root.guest_view_id = 100;
    root.props.push_back({kPropVisibility, PropValue::Int(0)});
    root.props.push_back({kPropBounds, PropValue::Rect(0, 0, 540, 960)});
    root.props.push_back({kPropPadding, PropValue::Rect(4, 8, 12, 16)});
    root.props.push_back({kPropBackgroundColor, PropValue::Int(static_cast<int32_t>(0xFF112233))});
    s1.nodes.push_back(root);
    ViewNode kid;
    kid.handle.object_id = 2;
    kid.class_name = "TextView";
    kid.parent_id = 1;
    kid.child_index = 0;
    kid.guest_view_id = -1;
    kid.props.push_back({kPropText, PropValue::String("hello 桥接")});
    kid.props.push_back({kPropTextSize, PropValue::Float(16.0f)});
    kid.props.push_back({kPropTextColor, PropValue::Int(static_cast<int32_t>(0xFF00FF00))});
    kid.props.push_back({kPropBounds, PropValue::Rect(4, 8, 200, 40)});
    s1.nodes.push_back(kid);
    s1.nodes[0].children.push_back(2);

    std::vector<DrawCommand> draw;
    DrawCommand save;
    save.op = DrawOp::kSave;
    draw.push_back(save);
    DrawCommand tr;
    tr.op = DrawOp::kTranslate;
    tr.f[0] = 4.0f;
    tr.f[1] = 8.0f;
    draw.push_back(tr);
    DrawCommand txt;
    txt.op = DrawOp::kDrawText;
    txt.text = u"hello 桥接";
    txt.i[0] = 4;
    txt.i[1] = 8;
    txt.f[0] = 16.0f;
    txt.color = 0xFF00FF00u;
    draw.push_back(txt);
    DrawCommand bmp;
    bmp.op = DrawOp::kDrawBitmap;
    bmp.i[0] = 2;
    bmp.i[1] = 2;
    bmp.i[2] = 0;
    bmp.i[3] = 0;
    bmp.i[4] = 2;
    bmp.i[5] = 2;
    bmp.f[0] = 1.0f;
    bmp.f[1] = 2.0f;
    bmp.f[2] = 3.0f;
    bmp.f[3] = 4.0f;
    bmp.bits = {0xFF0000FFu, 0xFF00FF00u, 0xFFFF0000u, 0xFFFFFFFFu};
    draw.push_back(bmp);
    DrawCommand rst;
    rst.op = DrawOp::kRestore;
    draw.push_back(rst);

    ViewTreeMirror mirror;
    std::vector<uint8_t> env1 = mirror.Submit(s1, draw);
    CHECK(!env1.empty(), "full envelope serialized");

    ViewSnapshot parsed;
    ViewPatch patch;
    std::vector<DrawCommand> parsed_draw;
    uint32_t rev = 0;
    bool is_patch = true;
    CHECK(ParseEnvelope(env1, &parsed, &patch, &parsed_draw, &rev, &is_patch) && !is_patch &&
              rev == 1,
          "parse full envelope (rev 1)");
    CHECK(SnapshotEquals(parsed, s1), "full snapshot roundtrip == source");
    CHECK(parsed.nodes[0].children.size() == 1 && parsed.nodes[0].children[0] == 2,
          "children rebuilt from parent_id");
    CHECK(parsed_draw.size() == draw.size(), "draw stream size roundtrip");
    if (parsed_draw.size() == draw.size()) {
      CHECK(parsed_draw[2].op == DrawOp::kDrawText && parsed_draw[2].text == u"hello 桥接" &&
                parsed_draw[2].color == 0xFF00FF00u,
            "drawText roundtrip (UTF-16 text + color)");
      CHECK(parsed_draw[3].op == DrawOp::kDrawBitmap && parsed_draw[3].bits.size() == 4 &&
                parsed_draw[3].bits[2] == 0xFFFF0000u && parsed_draw[3].i[0] == 2 &&
                parsed_draw[3].f[3] == 4.0f,
            "drawBitmap roundtrip (pixels + slots)");
    }

    // 增量：改 text + 换 alpha -> patch（只含变更项）
    ViewSnapshot s2 = s1;
    s2.nodes[1].props.clear();
    s2.nodes[1].props.push_back({kPropText, PropValue::String("changed")});
    s2.nodes[1].props.push_back({kPropTextSize, PropValue::Float(16.0f)});
    s2.nodes[1].props.push_back({kPropTextColor, PropValue::Int(static_cast<int32_t>(0xFF00FF00))});
    s2.nodes[1].props.push_back({kPropBounds, PropValue::Rect(4, 8, 200, 40)});
    s2.nodes[1].props.push_back({kPropAlpha, PropValue::Float(0.5f)});
    std::vector<uint8_t> env2 = mirror.Submit(s2, {});
    ViewSnapshot full2;
    ViewPatch patch2;
    uint32_t rev2 = 0;
    bool is_patch2 = false;
    CHECK(ParseEnvelope(env2, &full2, &patch2, nullptr, &rev2, &is_patch2) && is_patch2 &&
              rev2 == 2 && patch2.base_revision == 1,
          "second submit is patch (rev 2, base 1)");
    CHECK(patch2.ops.size() == 1 && patch2.ops[0].op == dexvm::bridge::PatchOpKind::kUpdate &&
              patch2.ops[0].handle.object_id == 2,
          "patch has single Update op on node 2");
    if (!patch2.ops.empty()) {
      // 变更项 = text 变化 + alpha 新增（textSize/color/bounds 不变不进 patch）
      bool text_changed = false;
      bool alpha_added = false;
      for (const Prop& pr : patch2.ops[0].props) {
        if (pr.key == kPropText && pr.value.s == "changed") text_changed = true;
        if (pr.key == kPropAlpha) alpha_added = true;
      }
      CHECK(patch2.ops[0].props.size() == 2 && text_changed && alpha_added,
            "patch props = changed-only (text + alpha)");
    }
    ViewSnapshot applied;
    CHECK(ApplyPatch(parsed, patch2, &applied), "ApplyPatch succeeds");
    CHECK(SnapshotEquals(applied, s2), "ApplyPatch(base, patch) == next snapshot");

    // 结构变更：加子节点 -> kAdd；删根 -> kRemove
    ViewSnapshot s3 = s2;
    ViewNode extra;
    extra.handle.object_id = 3;
    extra.class_name = "TextView";
    extra.parent_id = 1;
    extra.child_index = 1;
    extra.guest_view_id = 7;
    extra.props.push_back({kPropText, PropValue::String("new")});
    s3.nodes.push_back(extra);
    s3.nodes[0].children.push_back(3);
    ViewPatch diff3 = DiffSnapshots(s2, s3);
    bool add_ok = false;
    for (const auto& op : diff3.ops) {
      if (op.op == dexvm::bridge::PatchOpKind::kAdd && op.handle.object_id == 3 &&
          op.parent_id == 1 && op.child_index == 1 && op.guest_view_id == 7) {
        add_ok = true;
      }
    }
    CHECK(add_ok, "structural Add op carries parent/index/guest id");
    ViewSnapshot s4;
    s4.nodes.push_back(s2.nodes[1]);  // 只剩叶子 -> 根被移除
    s4.nodes[0].parent_id = kNoObject;
    ViewPatch diff4 = DiffSnapshots(s2, s4);
    bool rm_ok = false;
    for (const auto& op : diff4.ops) {
      if (op.op == dexvm::bridge::PatchOpKind::kRemove && op.handle.object_id == 1) rm_ok = true;
    }
    CHECK(rm_ok, "structural Remove op for deleted root");

    // viewId 映射
    std::map<int32_t, uint32_t> idmap = BuildViewIdMap(s1);
    CHECK(idmap.size() == 1 && idmap[100] == 1, "viewId map (guest mId -> object_id)");
    const ViewNode* by_id = FindByGuestId(s1, 100);
    CHECK(by_id != nullptr && by_id->class_name == "LinearLayout", "FindByGuestId resolves node");
    CHECK(FindByGuestId(s1, 999) == nullptr, "FindByGuestId miss -> null");

    // 坏数据负例
    std::vector<uint8_t> bad = {1, 2, 3};
    CHECK(!ParseEnvelope(bad, &parsed, &patch, nullptr, &rev, &is_patch), "bad magic rejected");
    std::vector<uint8_t> trunc = env1;
    trunc.resize(trunc.size() / 2);
    CHECK(!ParseEnvelope(trunc, &parsed, &patch, nullptr, &rev, &is_patch),
          "truncated envelope rejected");
  }

  // ================================================================
  // 2) VM 引导（runtime.dex + target APK + 点击探针 DEX）
  // ================================================================
  std::printf("---- boot VM ----\n");
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
  // boot dex 之后登记：lib widget 类（FrameLayout 等）此时才可 Find，桥接缝
  // 覆写（setOnClickListener/onMeasure/onLayout）随此落到真实类上。
  RegisterRenderPipeline(&linker);
  auto add_app_dex = [&](const std::string& path, const char* label) -> bool {
    auto session = dexvm::session::Session::Open(path, &err);
    if (session == nullptr || session->dex_blobs().empty()) {
      CHECK(false, (std::string("open ") + label + ": " + err).c_str());
      return false;
    }
    for (const auto& blob : session->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      if (dex_file == nullptr) {
        CHECK(false, (std::string("parse ") + label + " dex").c_str());
        return false;
      }
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    return true;
  };
  if (!add_app_dex(target_apk, "target APK")) return 1;
  if (!add_app_dex(probe_dex, "click probe dex")) return 1;
  VObject* app_loader = nullptr;
  {
    VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
    app_loader = linker.AllocObject(cl);
    linker.loaders().BindObject(app_loader, linker.loaders().default_app());
    CHECK(app_loader != nullptr, "app loader bound");
  }

  // ================================================================
  // 3) 快照/patch 断言：guest View 树字段态导出（缝B）
  // ================================================================
  std::printf("---- guest snapshot (field state) ----\n");
  ResetUiBridgeProtocol();
  auto make_view = [&](const char* desc) -> VObject* {
    VClass* k = linker.FindClass(desc);
    if (k == nullptr) return nullptr;
    VObject* v = linker.AllocObject(k);
    if (v == nullptr) return nullptr;
    Value ctx{};
    if (!CallVirtual(state, v, "<init>", {"Landroid/content/Context;"}, "V", &ctx, 1, nullptr)) {
      return nullptr;
    }
    return v;
  };
  auto set_text = [&](VObject* v, const char* text) {
    Value sv{};
    sv.ref = linker.AllocString(std::u16string(text, text + std::strlen(text)));
    CallVirtual(state, v, "setText", {"Ljava/lang/CharSequence;"}, "V", &sv, 1, nullptr);
  };

  VObject* root_ll = make_view("Landroid/widget/LinearLayout;");
  VObject* tv_a = make_view("Landroid/widget/TextView;");
  VObject* tv_b = make_view("Landroid/widget/TextView;");
  CHECK(root_ll != nullptr && tv_a != nullptr && tv_b != nullptr, "guest tree alloc");
  if (root_ll == nullptr || tv_a == nullptr || tv_b == nullptr) return 1;
  set_text(tv_a, "alpha");
  set_text(tv_b, "beta");
  {
    Value sz{};
    sz.f = 12.0f;
    CallVirtual(state, tv_a, "setTextSize", {"F"}, "V", &sz, 1, nullptr);
    Value cv{};
    cv.i = static_cast<int32_t>(0xFFFF0000u);
    CallVirtual(state, tv_a, "setTextColor", {"I"}, "V", &cv, 1, nullptr);
    Value bgv{};
    bgv.i = static_cast<int32_t>(0xFFEEEEEEu);
    CallVirtual(state, tv_a, "setBackgroundColor", {"I"}, "V", &bgv, 1, nullptr);
    Value pv[4] = {};
    pv[0].i = 4;
    pv[1].i = 5;
    pv[2].i = 6;
    pv[3].i = 7;
    CallVirtual(state, tv_a, "setPadding", {"I", "I", "I", "I"}, "V", pv, 4, nullptr);
    Value vis{};
    vis.i = 4;  // INVISIBLE
    CallVirtual(state, tv_b, "setVisibility", {"I"}, "V", &vis, 1, nullptr);
    Value av{};
    av.ref = tv_a;
    CallVirtual(state, root_ll, "addView", {"Landroid/view/View;"}, "V", &av, 1, nullptr);
    av.ref = tv_b;
    CallVirtual(state, root_ll, "addView", {"Landroid/view/View;"}, "V", &av, 1, nullptr);
    // mId（无 setId 面：字段缝直写，viewId 映射用）；root 置 NO_ID(-1)
    VField* id_root = FindField(root_ll->klass, "mId");
    if (id_root != nullptr) WriteIField(root_ll, id_root->offset, -1);
    VField* id_a = FindField(tv_a->klass, "mId");
    if (id_a != nullptr) WriteIField(tv_a, id_a->offset, 7);
    VField* id_b = FindField(tv_b->klass, "mId");
    if (id_b != nullptr) WriteIField(tv_b, id_b->offset, 8);
    // mLayoutParams（wrap_content x wrap_content）
    VClass* lp_cls = linker.FindClass("Landroid/view/ViewGroup$LayoutParams;");
    if (lp_cls != nullptr) {
      VObject* lp = linker.AllocObject(lp_cls);
      if (lp != nullptr) {
        Value lp_args[2] = {};
        lp_args[0].i = -2;
        lp_args[1].i = -2;
        if (CallVirtual(state, lp, "<init>", {"I", "I"}, "V", lp_args, 2, nullptr)) {
          Value set_lp{};
          set_lp.ref = lp;
          CallVirtual(state, root_ll, "setLayoutParams", {"Landroid/view/ViewGroup$LayoutParams;"},
                      "V", &set_lp, 1, nullptr);
        }
      }
    }
  }
  // measure/layout（traversal 只当布局引擎用）
  {
    Value m2[2] = {};
    m2[0].i = MakeMeasureSpec(540, dexvm::framework::kExactly);
    m2[1].i = MakeMeasureSpec(960, dexvm::framework::kExactly);
    CallVirtual(state, root_ll, "measure", {"I", "I"}, "V", m2, 2, nullptr);
    Value l4[4] = {};
    l4[2].i = 540;
    l4[3].i = 960;
    CallVirtual(state, root_ll, "layout", {"I", "I", "I", "I"}, "V", l4, 4, nullptr);
  }

  ViewSnapshot snap = BuildSnapshotFromGuest(state, root_ll);
  CHECK(snap.nodes.size() == 3, "snapshot has 3 nodes");
  if (snap.nodes.size() == 3) {
    const ViewNode& nd0 = snap.nodes[0];
    const ViewNode& nd1 = snap.nodes[1];
    const ViewNode& nd2 = snap.nodes[2];
    CHECK(nd0.class_name == "LinearLayout" && nd0.parent_id == kNoObject &&
              nd0.children.size() == 2,
          "root = LinearLayout with 2 children");
    CHECK(nd1.class_name == "TextView" && nd1.parent_id == nd0.handle.object_id &&
              nd1.child_index == 0,
          "child0 = TextView at index 0");
    CHECK(nd2.class_name == "TextView" && nd2.child_index == 1, "child1 at index 1");
    const Prop* text_a = NodeProp(nd1, kPropText);
    const Prop* text_b = NodeProp(nd2, kPropText);
    CHECK(text_a != nullptr && text_a->value.s == "alpha", "text prop (alpha)");
    CHECK(text_b != nullptr && text_b->value.s == "beta", "text prop (beta)");
    const Prop* vis_b = NodeProp(nd2, kPropVisibility);
    CHECK(vis_b != nullptr && vis_b->value.i == 4, "visibility prop (INVISIBLE=4)");
    const Prop* pad_a = NodeProp(nd1, kPropPadding);
    CHECK(pad_a != nullptr && pad_a->value.rect[0] == 4 && pad_a->value.rect[1] == 5 &&
              pad_a->value.rect[2] == 6 && pad_a->value.rect[3] == 7,
          "padding prop (4,5,6,7)");
    const Prop* ts_a = NodeProp(nd1, kPropTextSize);
    CHECK(ts_a != nullptr && ts_a->value.f == 12.0f, "textSize prop (12)");
    const Prop* tc_a = NodeProp(nd1, kPropTextColor);
    CHECK(tc_a != nullptr && tc_a->value.i == static_cast<int32_t>(0xFFFF0000u),
          "textColor prop");
    const Prop* bounds = NodeProp(nd1, kPropBounds);
    CHECK(bounds != nullptr && bounds->value.rect[2] - bounds->value.rect[0] > 0,
          "bounds prop laid out (w>0)");
    CHECK(nd1.guest_view_id == 7 && nd2.guest_view_id == 8, "guest viewId carried (7/8)");
    const Prop* lp = NodeProp(nd0, kPropLayoutParams);
    CHECK(lp != nullptr && lp->value.rect[0] == -2 && lp->value.rect[1] == -2,
          "layoutParams prop (wrap_content=-2)");
    const Prop* click = NodeProp(nd1, kPropHasClickListener);
    CHECK(click != nullptr && click->value.i == 0, "hasClickListener=false before set");
    std::map<int32_t, uint32_t> idmap = BuildViewIdMap(snap);
    CHECK(idmap.size() == 2 && idmap[7] == nd1.handle.object_id && idmap[8] == nd2.handle.object_id,
          "viewId map from guest tree (7/8)");
  }

  // 导出器（缝B）present：全量 -> 增量，ApplyPatch 一致
  UIBridge ui;
  auto run_exporter = [&]() -> bool {
    auto exporter = GetViewTreeExporter();
    if (exporter == nullptr) return false;
    return exporter(&state, root_ll, 540, 960, &ui);
  };
  CHECK(GetViewTreeExporter() != nullptr, "view tree exporter registered (protocol TU loaded)");
  CHECK(run_exporter(), "exporter presents full snapshot");
  CHECK(ui.view_tree_present_count() == 1 && ui.last_view_tree().size() > 0,
        "PresentViewTree stored envelope");
  {
    ViewSnapshot full;
    ViewPatch patch;
    uint32_t rev = 0;
    bool is_patch = true;
    CHECK(ParseEnvelope(ui.last_view_tree(), &full, &patch, nullptr, &rev, &is_patch) &&
              !is_patch && rev == 1,
          "first export = full (rev 1)");
    CHECK(SnapshotEquals(full, snap), "exported snapshot == BuildSnapshotFromGuest");
  }
  set_text(tv_a, "alpha2");
  ViewSnapshot snap2 = BuildSnapshotFromGuest(state, root_ll);
  CHECK(run_exporter(), "exporter presents patch");
  {
    ViewSnapshot full;
    ViewPatch patch;
    uint32_t rev = 0;
    bool is_patch = false;
    CHECK(ParseEnvelope(ui.last_view_tree(), &full, &patch, nullptr, &rev, &is_patch) &&
              is_patch && rev == 2 && patch.base_revision == 1,
          "second export = patch (rev 2, base 1)");
    ViewSnapshot applied;
    ViewSnapshot base = snap;
    base.revision = 1;  // 镜像首帧全量基线 revision
    CHECK(ApplyPatch(base, patch, &applied) && SnapshotEquals(applied, snap2),
          "patch applies to previous snapshot == current tree");
    bool text_op = false;
    for (const auto& op : patch.ops) {
      for (const Prop& pr : op.props) {
        if (pr.key == kPropText && pr.value.s == "alpha2") text_op = true;
      }
    }
    CHECK(text_op, "patch carries text change only for changed node");
  }

  // ================================================================
  // 4) 缝A：draw 命令记录（drawText/Rect/Bitmap/save/restore/矩阵）
  // ================================================================
  std::printf("---- draw command recording (seam A) ----\n");
  {
    GlobalProtocolSink().Clear();
    Canvas canvas(64, 64);
    CHECK(canvas.draw_sink() == &GlobalProtocolSink(), "canvas auto-attached global sink");
    VObject* cx = WrapCanvas(state, &canvas);
    Value dv{};
    dv.ref = cx;
    CHECK(CallVirtual(state, tv_a, "draw", {"Landroid/graphics/Canvas;"}, "V", &dv, 1, nullptr),
          "guest draw (TextView)");
    const std::vector<DrawCommand>& stream = GlobalProtocolSink().stream();
    bool has_fill = false;
    bool has_text = false;
    bool has_translate = false;
    for (const DrawCommand& c : stream) {
      if (c.op == DrawOp::kFillRect) has_fill = true;
      if (c.op == DrawOp::kDrawText && c.text.find(u"alpha2") != std::u16string::npos) {
        has_text = true;
      }
      if (c.op == DrawOp::kTranslate) has_translate = true;
    }
    CHECK(has_fill, "fillRect recorded (background)");
    CHECK(has_text, "drawText recorded (with text payload)");
    CHECK(!has_translate, "leaf draw has no translate (root-relative)");
    // ViewGroup 递归 draw：translate 进流（子 View 平移）
    GlobalProtocolSink().Clear();
    CHECK(CallVirtual(state, root_ll, "draw", {"Landroid/graphics/Canvas;"}, "V", &dv, 1,
                      nullptr),
          "guest draw (LinearLayout tree)");
    bool has_tr2 = false;
    for (const DrawCommand& c : GlobalProtocolSink().stream()) {
      if (c.op == DrawOp::kTranslate) has_tr2 = true;
    }
    CHECK(has_tr2, "translate recorded for child placement");
    // save/restore/矩阵/bitmap 直调记录
    GlobalProtocolSink().Clear();
    canvas.Save();
    canvas.TranslateF(3.0f, 4.0f);
    canvas.ClipRectF(0, 0, 32, 32);
    float m[6] = {2, 0, 0, 2, 1, 1};
    canvas.SetMatrix6(m);
    canvas.Restore();
    int32_t bits[2 * 2 * 2];  // 2x2，8 字节/元素槽
    for (int i = 0; i < 4; i++) {
      uint32_t px = 0xFF000000u + i;
      std::memcpy(&bits[i * 2], &px, sizeof(px));
    }
    canvas.DrawBitmapBlock(bits, 2, 2, 0, 0, 2, 2, 0, 0, 8, 8);
    const std::vector<DrawCommand>& s2 = GlobalProtocolSink().stream();
    auto has_op = [&](DrawOp op) {
      for (const DrawCommand& c : s2) {
        if (c.op == op) return true;
      }
      return false;
    };
    CHECK(has_op(DrawOp::kSave) && has_op(DrawOp::kRestore), "save/restore recorded");
    CHECK(has_op(DrawOp::kTranslate) && has_op(DrawOp::kSetMatrix) && has_op(DrawOp::kClipRect),
          "matrix/clip recorded");
    CHECK(has_op(DrawOp::kDrawBitmap), "drawBitmap recorded");
    // 命令流序列化往返（进协议流）
    std::vector<uint8_t> env = SerializeFull(snap, s2);
    std::vector<DrawCommand> rt;
    CHECK(ParseEnvelope(env, nullptr, nullptr, &rt, nullptr, nullptr) && rt.size() == s2.size(),
          "draw stream serializes in envelope");
    if (rt.size() == s2.size()) {
      bool bmp_ok = false;
      for (const DrawCommand& c : rt) {
        if (c.op == DrawOp::kDrawBitmap && c.bits.size() == 4 && c.bits[3] == 0xFF000003u) {
          bmp_ok = true;
        }
      }
      CHECK(bmp_ok, "bitmap pixels roundtrip through protocol stream");
    }
  }

  // ================================================================
  // 5) 点击回灌：host(viewId) -> guest listener 注入 -> onClick 派发
  // ================================================================
  std::printf("---- click injection (viewId -> guest listener) ----\n");
  {
    VClass* probe_cls = linker.FindClass("Luibridge/UiBridgeClickProbe;");
    CHECK(probe_cls != nullptr, "click probe class loaded");
    if (probe_cls == nullptr) return 1;
    CHECK(linker.InitializeClass(probe_cls, &state), "probe class initialized");
    VObject* probe = linker.AllocObject(probe_cls);
    CHECK(probe != nullptr && CallVirtual(state, probe, "<init>", {}, "V", nullptr, 0, nullptr),
          "probe alloc+init");
    if (probe == nullptr) return 1;

    CHECK(dexvm::bridge::GuestClickListenerCount() == 0, "no listeners registered yet");
    Value lv{};
    lv.ref = probe;
    CHECK(CallVirtual(state, tv_a, "setOnClickListener", {"Landroid/view/View$OnClickListener;"},
                      "V", &lv, 1, nullptr),
          "setOnClickListener (bridge seam override)");
    CHECK(dexvm::bridge::GuestClickListenerCount() == 1, "listener registered");
    CHECK(dexvm::bridge::HasGuestClickListener(tv_a), "HasGuestClickListener(view)");
    CHECK(!dexvm::bridge::HasGuestClickListener(tv_b), "other view has no listener");

    // host 点击回灌：viewId=7 -> tv_a 的 listener.onClick(tv_a)
    std::string status = InjectGuestClick(&state, 7);
    CHECK(status.find("dispatched") != std::string::npos &&
              status.find("listener=1") != std::string::npos,
          (std::string("InjectGuestClick(7) dispatches listener (") + status + ")").c_str());
    CHECK(StaticValue(probe_cls, "clicks").i == 1, "probe.clicks == 1 (guest onClick ran)");
    CHECK(StaticValue(probe_cls, "lastViewId").i == 7, "probe.lastViewId == 7 (guest getId)");
    CHECK(StaticValue(probe_cls, "lastView").ref == tv_a, "probe.lastView == guest view");

    // guest 侧 performClick 缝：同样派发
    Value clicked{};
    CHECK(CallVirtual(state, tv_a, "performClick", {}, "Z", nullptr, 0, &clicked) && clicked.i == 1,
          "performClick returns true");
    CHECK(StaticValue(probe_cls, "clicks").i == 2 &&
              StaticValue(probe_cls, "lastView").ref == tv_a,
          "performClick dispatched to same listener");

    // 无监听器 viewId：不派发
    std::string status2 = InjectGuestClick(&state, 8);
    CHECK(status2.find("dispatched") != std::string::npos &&
              status2.find("listener=0") != std::string::npos,
          (std::string("InjectGuestClick(8) no listener (") + status2 + ")").c_str());
    CHECK(StaticValue(probe_cls, "clicks").i == 2, "no listener -> onClick not called");
    // 未知 viewId
    std::string status3 = InjectGuestClick(&state, 4242);
    CHECK(status3.find("no-view") != std::string::npos, "unknown viewId -> no-view");
    // 会话外（设备侧安全形态）：不触 guest 堆
    std::string status4 = InjectGuestClick(nullptr, 7);
    CHECK(status4.find("channel-ok") != std::string::npos &&
              status4.find("guest-session=absent") != std::string::npos,
          (std::string("null session -> channel probe only (") + status4 + ")").c_str());
    CHECK(StaticValue(probe_cls, "clicks").i == 2, "null session does not dispatch");

    // 快照 hasClickListener 现为真
    ViewSnapshot snap_click = BuildSnapshotFromGuest(state, root_ll);
    const ViewNode* node_a = FindByGuestId(snap_click, 7);
    CHECK(node_a != nullptr && NodeProp(*node_a, kPropHasClickListener) != nullptr &&
              NodeProp(*node_a, kPropHasClickListener)->value.i == 1,
          "snapshot hasClickListener=true after set");

    // 登记表清空
    dexvm::bridge::ClearGuestClickListeners();
    CHECK(dexvm::bridge::GuestClickListenerCount() == 0, "ClearGuestClickListeners empties");
  }

  // ================================================================
  // 6) 桥轨首帧：target-app 全链 + PresentViewTree + 五条件（19.6）
  // ================================================================
  std::printf("---- bridge-track first frame (target-app lifecycle) ----\n");
  ResetUiBridgeProtocol();
  {
    auto app_session = dexvm::session::Session::Open(target_apk, &err);
    CHECK(app_session != nullptr && app_session->has_manifest(), "target manifest present");
    if (app_session == nullptr) return 1;
    LifecycleConfig cfg = ConfigFromManifest(app_session->manifest());
    UIBridge ui2;
    LifecycleDriver driver(state, app_loader);
    driver.set_first_frame_hook([&](VObject* activity, const PhaseRunner& run_phase,
                                    std::string* error) {
      return RunFirstFramePipeline(state, activity, &ui2, 540, 960, run_phase, error);
    });
    bool ran = driver.Run(cfg);
    CHECK(ran, "lifecycle + first-frame pipeline completes");
    if (!ran) std::printf("  error: %s\n", driver.error().c_str());
    CHECK(ui2.first_frame_complete() && ui2.first_frame_marked(),
          "five conditions -> first frame marked (bridge track)");
    CHECK(ui2.view_tree_present_count() == 1, "exactly one view-tree present");

    // 桥轨出画内容：快照（TextView "target ok"）+ draw 命令流
    ViewSnapshot full;
    ViewPatch patch;
    std::vector<DrawCommand> draw;
    uint32_t rev = 0;
    bool is_patch = false;
    CHECK(ParseEnvelope(ui2.last_view_tree(), &full, &patch, &draw, &rev, &is_patch) && !is_patch,
          "target export = full snapshot");
    CHECK(!full.nodes.empty(), "snapshot non-empty");
    bool text_ok = false;
    for (const ViewNode& nd : full.nodes) {
      const Prop* t = NodeProp(nd, kPropText);
      if (t != nullptr && t->value.s.find("target ok") != std::string::npos) text_ok = true;
    }
    CHECK(text_ok, "snapshot carries 'target ok' text (materializer input)");
    bool draw_text_ok = false;
    for (const DrawCommand& c : draw) {
      if (c.op == DrawOp::kDrawText && c.text.find(u"target ok") != std::u16string::npos) {
        draw_text_ok = true;
      }
    }
    CHECK(draw_text_ok, "draw stream carries 'target ok' drawText");
    CHECK(!draw.empty(), "draw stream non-empty in envelope");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
