// TextPaint/Paint/Typeface/StaticLayout 文本测量面冒烟（framework 面批量补充轮）：
// boot = runtime.dex（lib 版 android.text/graphics 面），断言：
//   1) 类面：TextPaint/Paint/Typeface/StaticLayout/Layout 均 lib（dex_file != nullptr）
//   2) Paint 测量缝：measureText/getFontMetricsInt 网格口径（8s 格 / 10s 行高）与
//      render.cc TextMeasureWidth/Height 一致（测量缝契约）
//   3) Typeface 风格态：create/defaultFromStyle
//   4) StaticLayout：Builder + 经典构造；贪心行折断（'\n' 强制断行）
//   5) 命令流断言（接 render 管线；自绘像素轨已退役）：Layout.draw(Canvas) ->
//      缝A drawText 命令（文本/坐标/字号）与 measureText 同源
// 独立脚本 tools/text_paint_tests.sh 驱动（不动既有 tools/*.sh）。
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "bridge/bridge.h"
#include "bridge/ui_bridge_protocol.h"
#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "framework/render.h"
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

using dexvm::runtime::ClassLinker;
using dexvm::runtime::ClassStatus;
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

// ---- 测试侧辅助 ----

bool CallV(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
           const char* ret, const Value* args, size_t n, Value* out) {
  if (obj == nullptr || obj->klass == nullptr) return false;
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

bool CallStatic(VMState& state, VClass* k, const char* name, std::vector<std::string> params,
                const char* ret, const Value* args, size_t n, Value* out) {
  if (k == nullptr) return false;
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = nullptr;
  for (auto& dm : k->direct_methods) {
    if (dm.name == name && dm.return_type == ret && dm.param_types == probe.param_types) {
      m = &dm;
      break;
    }
  }
  if (m == nullptr) return false;
  Value r{};
  if (!InvokeMethod(state, m, args, n, &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

VObject* AllocZeroed(ClassLinker* linker, VClass* k) {
  VObject* o = linker->AllocObject(k);
  if (o != nullptr && k->object_size > sizeof(ObjHeader)) {
    std::memset(o->field_data(), 0, k->object_size - sizeof(ObjHeader));
  }
  return o;
}

void DumpEx(VMState& state) {
  if (!state.HasPendingException()) {
    std::printf("  (no pending exception)\n");
    return;
  }
  ObjHeader* ex = state.pending_exception;
  std::string desc = (ex != nullptr && ex->klass != nullptr) ? ex->klass->descriptor : "?";
  std::printf("  exception: %s\n", desc.c_str());
  for (const auto& f : state.last_throw_backtrace) {
    std::printf("    at %s\n", f.c_str());
  }
  state.ClearException();
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== text_paint smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";

  // ---- 引导 VM ----
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
  // 渲染管线登记（Canvas 内建 + View 缝）
  dexvm::framework::RegisterRenderPipeline(&linker);

  // ---- 1. 类面（lib 版 = dex_file != nullptr）----
  VClass* paint_cls = linker.FindClass("Landroid/graphics/Paint;");
  VClass* tpaint_cls = linker.FindClass("Landroid/text/TextPaint;");
  VClass* type_cls = linker.FindClass("Landroid/graphics/Typeface;");
  VClass* layout_cls = linker.FindClass("Landroid/text/Layout;");
  VClass* slay_cls = linker.FindClass("Landroid/text/StaticLayout;");
  VClass* rect_cls = linker.FindClass("Landroid/graphics/Rect;");
  CHECK(paint_cls != nullptr && paint_cls->dex_file != nullptr, "Paint lib 类");
  CHECK(tpaint_cls != nullptr && tpaint_cls->dex_file != nullptr, "TextPaint lib 类");
  CHECK(type_cls != nullptr && type_cls->dex_file != nullptr, "Typeface lib 类");
  CHECK(layout_cls != nullptr && layout_cls->dex_file != nullptr, "Layout lib 类");
  CHECK(slay_cls != nullptr && slay_cls->dex_file != nullptr, "StaticLayout lib 类");
  CHECK(rect_cls != nullptr && rect_cls->dex_file != nullptr, "Rect lib 类");
  if (paint_cls == nullptr || tpaint_cls == nullptr || slay_cls == nullptr) return 1;

  // ---- 2. Paint 字段态 + 测量缝 ----
  VObject* paint = AllocZeroed(&linker, paint_cls);
  bool ok = CallV(state, paint, "<init>", {}, "V", nullptr, 0, nullptr);
  CHECK(ok, "Paint.<init>()");
  if (!ok) DumpEx(state);

  Value r{};
  ok = CallV(state, paint, "getTextSize", {}, "F", nullptr, 0, &r);
  CHECK(ok && r.f == 15.0f, "Paint 默认 textSize=15");
  ok = CallV(state, paint, "setTextSize", {"F"}, "V", nullptr, 0, nullptr);
  {
    Value a[1];
    a[0].f = 16.0f;
    ok = CallV(state, paint, "setTextSize", {"F"}, "V", a, 1, nullptr);
    CHECK(ok, "Paint.setTextSize(16)");
  }

  // measureText("abc") = 3 chars × 8s, s=round(16/8)=2 → 48
  {
    VString* s = linker.AllocString(std::u16string(u"abc"));
    Value a[1];
    a[0].ref = s;
    Value out{};
    ok = CallV(state, paint, "measureText", {"Ljava/lang/String;"}, "F", a, 1, &out);
    CHECK(ok && out.f == 48.0f, "measureText(abc)=48（网格缝）");
    if (!ok) DumpEx(state);
  }
  // measureText("ab\ncd") = max line 2 chars → 32
  {
    VString* s = linker.AllocString(std::u16string(u"ab\ncd"));
    Value a[1];
    a[0].ref = s;
    Value out{};
    ok = CallV(state, paint, "measureText", {"Ljava/lang/String;"}, "F", a, 1, &out);
    CHECK(ok && out.f == 32.0f, "measureText(ab\\ncd)=32（换行取最长行）");
  }
  // 测量缝契约：与 render.cc TextMeasureWidth 同值
  {
    const uint16_t chars[3] = {u'a', u'b', u'c'};
    int32_t native_w = dexvm::framework::TextMeasureWidth(chars, 3, 16.0f);
    CHECK(native_w == 48, "native TextMeasureWidth(abc,16)=48");
  }
  // getFontMetricsInt：ascent=-16, descent=4（s=2）
  {
    Value out{};
    ok = CallV(state, paint, "getFontMetricsInt", {}, "Landroid/graphics/Paint$FontMetricsInt;",
               nullptr, 0, &out);
    CHECK(ok && out.ref != nullptr, "getFontMetricsInt() 返回对象");
    if (ok && out.ref != nullptr) {
      VObject* fmi = reinterpret_cast<VObject*>(out.ref);
      auto read_i = [&](const char* name) {
        for (VClass* k = fmi->klass; k != nullptr; k = k->super_class) {
          for (auto& f : k->instance_fields) {
            if (f.name == name) {
              int32_t v = 0;
              std::memcpy(&v, fmi->field_data() + f.offset, 4);
              return v;
            }
          }
        }
        return 0;
      };
      CHECK(read_i("ascent") == -16 && read_i("descent") == 4 && read_i("top") == -16 &&
                read_i("bottom") == 4 && read_i("leading") == 0,
            "FontMetricsInt 网格值（-16/4）");
    }
  }
  // setColor/getColor
  {
    Value a[1];
    a[0].i = 0xFFFF0000u;
    ok = CallV(state, paint, "setColor", {"I"}, "V", a, 1, nullptr);
    Value out{};
    ok = ok && CallV(state, paint, "getColor", {}, "I", nullptr, 0, &out);
    CHECK(ok && static_cast<uint32_t>(out.i) == 0xFFFF0000u, "setColor/getColor roundtrip");
  }

  // ---- 3. TextPaint 字段态 ----
  VObject* tpaint = AllocZeroed(&linker, tpaint_cls);
  {
    Value a[1];
    a[0].ref = paint;
    ok = CallV(state, tpaint, "<init>", {"Landroid/graphics/Paint;"}, "V", a, 1, nullptr);
    CHECK(ok, "TextPaint.<init>(Paint)");
    if (!ok) DumpEx(state);
  }
  {
    Value out{};
    ok = CallV(state, tpaint, "getTypeface", {}, "Landroid/graphics/Typeface;", nullptr, 0, &out);
    CHECK(ok, "TextPaint.getTypeface() 可调");
    ok = CallV(state, tpaint, "isElegantTextHeight", {}, "Z", nullptr, 0, &out);
    CHECK(ok && out.i == 0, "isElegantTextHeight=false 默认");
    ok = CallV(state, tpaint, "getTextScaleX", {}, "F", nullptr, 0, &out);
    CHECK(ok && out.f == 1.0f, "getTextScaleX=1 默认");
    ok = CallV(state, tpaint, "getFlags", {}, "I", nullptr, 0, &out);
    CHECK(ok, "getFlags 可调");
    ok = CallV(state, tpaint, "getTextLocale", {}, "Ljava/util/Locale;", nullptr, 0, &out);
    CHECK(ok && out.ref != nullptr, "getTextLocale 非空");
    ok = CallV(state, tpaint, "getTextLocales", {}, "Landroid/os/LocaleList;", nullptr, 0, &out);
    CHECK(ok && out.ref != nullptr, "getTextLocales 非空");
    ok = CallV(state, tpaint, "getLetterSpacing", {}, "F", nullptr, 0, &out);
    CHECK(ok && out.f == 0.0f, "getLetterSpacing=0 默认");
    ok = CallV(state, tpaint, "setTextSize", {"F"}, "V", nullptr, 0, nullptr);
    Value a2[1];
    a2[0].f = 24.0f;
    ok = CallV(state, tpaint, "setTextSize", {"F"}, "V", a2, 1, nullptr);
    ok = CallV(state, tpaint, "getTextSize", {}, "F", nullptr, 0, &out);
    CHECK(ok && out.f == 24.0f, "setTextSize/getTextSize roundtrip");
  }

  // ---- 4. Typeface 风格态 ----
  {
    // static create(Typeface, int)
    Value a[2];
    a[0].ref = nullptr;
    a[1].i = 1;  // BOLD
    Value out{};
    ok = CallStatic(state, type_cls, "create",
                    {"Landroid/graphics/Typeface;", "I"}, "Landroid/graphics/Typeface;", a, 2,
                    &out);
    CHECK(ok && out.ref != nullptr, "Typeface.create(null,BOLD)");
    if (ok && out.ref != nullptr) {
      VObject* t = reinterpret_cast<VObject*>(out.ref);
      Value so{};
      bool ok2 = CallV(state, t, "getStyle", {}, "I", nullptr, 0, &so);
      CHECK(ok2 && so.i == 1, "create(...).getStyle()==BOLD");
    }
    Value out2{};
    ok = CallStatic(state, type_cls, "defaultFromStyle", {"I"},
                    "Landroid/graphics/Typeface;", a + 1, 1, &out2);
    CHECK(ok && out2.ref != nullptr, "defaultFromStyle(BOLD)");
    // DEFAULT 静态字段
    VField* df = nullptr;
    for (auto& f : type_cls->static_fields) {
      if (f.name == "DEFAULT") df = &f;
    }
    CHECK(df != nullptr, "Typeface.DEFAULT 静态字段");
    if (df != nullptr) {
      ObjHeader* def_ref = nullptr;
      for (auto& sf : type_cls->static_fields) {
        if (sf.name == "DEFAULT") {
          std::memcpy(&def_ref, type_cls->static_storage.data() + sf.offset, sizeof(def_ref));
        }
      }
      CHECK(def_ref != nullptr, "Typeface.DEFAULT 已初始化");
    }
  }

  // ---- 5. StaticLayout：Builder + 经典构造 ----
  VClass* builder_cls = linker.FindClass("Landroid/text/StaticLayout$Builder;");
  CHECK(builder_cls != nullptr && builder_cls->dex_file != nullptr, "StaticLayout$Builder lib 类");
  {
    // Builder.obtain("ab\ncd", 0, 5, paint, 1000).build()
    VString* s = linker.AllocString(std::u16string(u"ab\ncd"));
    Value a[5];
    a[0].ref = s;
    a[1].i = 0;
    a[2].i = 5;
    a[3].ref = paint;
    a[4].i = 1000;
    Value out{};
    ok = CallStatic(state, builder_cls, "obtain",
                    {"Ljava/lang/CharSequence;", "I", "I", "Landroid/text/TextPaint;", "I"},
                    "Landroid/text/StaticLayout$Builder;", a, 5, &out);
    CHECK(ok && out.ref != nullptr, "Builder.obtain");
    if (!ok) DumpEx(state);
    if (ok && out.ref != nullptr) {
      VObject* b = reinterpret_cast<VObject*>(out.ref);
      Value bout{};
      ok = CallV(state, b, "build", {}, "Landroid/text/StaticLayout;", nullptr, 0, &bout);
      CHECK(ok && bout.ref != nullptr, "Builder.build()");
      if (!ok) DumpEx(state);
      if (ok && bout.ref != nullptr) {
        VObject* lay = reinterpret_cast<VObject*>(bout.ref);
        Value lo{};
        CallV(state, lay, "getLineCount", {}, "I", nullptr, 0, &lo);
        CHECK(lo.i == 2, "StaticLayout 两行（\\n 强制断行）");
        CallV(state, lay, "getLineStart", {"I"}, "I", nullptr, 0, nullptr);
        Value a1[1];
        a1[0].i = 1;
        Value s1{};
        CallV(state, lay, "getLineStart", {"I"}, "I", a1, 1, &s1);
        CHECK(s1.i == 3, "line1.start==3");
        Value e0{};
        Value a0[1];
        a0[0].i = 0;
        CallV(state, lay, "getLineEnd", {"I"}, "I", a0, 1, &e0);
        CHECK(e0.i == 2, "line0.end==2（不含换行）");
        Value w{};
        CallV(state, lay, "getWidth", {}, "I", nullptr, 0, &w);
        CHECK(w.i == 1000, "layout.getWidth()==1000");
      }
    }
  }
  {
    // 经典构造：(source, bufstart, bufend, paint, outerwidth, align, spacingmult,
    //  spacingadd, includepad)——"hello" width=16（s=2 时每行 1 字符）→ 5 行
    VString* s = linker.AllocString(std::u16string(u"hello"));
    VClass* align_cls = linker.FindClass("Landroid/text/Layout$Alignment;");
    CHECK(align_cls != nullptr, "Layout$Alignment 枚举类");
    ObjHeader* align_normal = nullptr;
    if (align_cls != nullptr) {
      for (auto& sf : align_cls->static_fields) {
        if (sf.name == "ALIGN_NORMAL") {
          std::memcpy(&align_normal, align_cls->static_storage.data() + sf.offset,
                      sizeof(align_normal));
        }
      }
    }
    CHECK(align_normal != nullptr, "ALIGN_NORMAL 静态枚举值");
    Value a[9];
    a[0].ref = s;
    a[1].i = 0;
    a[2].i = 5;
    a[3].ref = paint;
    a[4].i = 16;
    a[5].ref = align_normal;
    a[6].d = 1.0;  // spacingmult float——宽槽；float 传参单 Value
    a[7].d = 0.0;
    a[8].i = 1;
    // float 参数按 Value.f（单槽）传递
    a[6].f = 1.0f;
    a[7].f = 0.0f;
    VObject* lay = AllocZeroed(&linker, slay_cls);
    ok = CallV(state, lay, "<init>",
               {"Ljava/lang/CharSequence;", "I", "I", "Landroid/text/TextPaint;", "I",
                "Landroid/text/Layout$Alignment;", "F", "F", "Z"},
               "V", a, 9, nullptr);
    CHECK(ok, "StaticLayout 9 参构造");
    if (!ok) DumpEx(state);
    if (ok) {
      Value lc{};
      CallV(state, lay, "getLineCount", {}, "I", nullptr, 0, &lc);
      CHECK(lc.i == 5, "hello @ width16 -> 5 行（每行 1 字符）");
    }
  }

  // ---- 6. 命令流断言（测量→draw 同源；缝A，render 管线）----
  {
    dexvm::framework::Canvas native_canvas(128, 64);
    native_canvas.SetDrawSink(&dexvm::bridge::GlobalProtocolSink());
    dexvm::bridge::GlobalProtocolSink().Clear();
    dexvm::runtime::VObject* canvas_obj = dexvm::framework::WrapCanvas(state, &native_canvas);
    CHECK(canvas_obj != nullptr, "WrapCanvas");

    VString* s = linker.AllocString(std::u16string(u"abc"));
    Value a[7];
    a[0].ref = s;
    a[1].i = 0;
    a[2].i = 3;
    a[3].ref = paint;  // textSize=16（上文已设）
    a[4].i = 1000;
    Value bout{};
    ok = CallStatic(state, builder_cls, "obtain",
                    {"Ljava/lang/CharSequence;", "I", "I", "Landroid/text/TextPaint;", "I"},
                    "Landroid/text/StaticLayout$Builder;", a, 5, &bout);
    CHECK(ok && bout.ref != nullptr, "pixel: Builder.obtain(abc)");
    VObject* lay = nullptr;
    if (ok && bout.ref != nullptr) {
      Value lo{};
      ok = CallV(state, reinterpret_cast<VObject*>(bout.ref), "build", {},
                 "Landroid/text/StaticLayout;", nullptr, 0, &lo);
      lay = reinterpret_cast<VObject*>(lo.ref);
    }
    CHECK(lay != nullptr, "pixel: layout 就绪");
    if (lay != nullptr) {
      Value dargs[1];
      dargs[0].ref = canvas_obj;
      ok = CallV(state, lay, "draw", {"Landroid/graphics/Canvas;"}, "V", dargs, 1, nullptr);
      CHECK(ok, "layout.draw(canvas)");
      if (!ok) DumpEx(state);
      // 命令流：drawText(abc) 起点 (0,0)、字号 16；网格口径 3 格 x 16px = 48
      // 与 measureText(abc)=48 同源（文本测量→draw 契约）
      const std::vector<dexvm::bridge::DrawCommand>& stream =
          dexvm::bridge::GlobalProtocolSink().stream();
      int text_ops = 0;
      bool text_ok = false;
      for (const dexvm::bridge::DrawCommand& c : stream) {
        if (c.op != dexvm::bridge::DrawOp::kDrawText) continue;
        text_ops++;
        if (c.text == u"abc" && c.i[0] == 0 && c.i[1] == 0 && c.f[0] == 16.0f) text_ok = true;
      }
      CHECK(text_ops == 1, "single drawText command (one line)");
      CHECK(text_ok, "drawText(abc) at (0,0) size 16 (measure 同源)");
    }
  }

  std::printf("== text_paint smoke done: %d failures ==\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
