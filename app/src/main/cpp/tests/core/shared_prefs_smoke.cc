// SharedPreferences 面冒烟（§31.7 B 档 lib 路径：runtime-libs android.content.
// SharedPreferences* 纯 Java 源随 runtime.dex 打包；VM 侧仅 Context.
// getSharedPreferences 薄钩子转调 lib SharedPreferencesImpl.getInstance）：
// boot = runtime.dex，断言 SP 五类 dex_file != nullptr（lib 版生效）+
// 同名单例语义 / get-put 全型 / remove / clear / commit 返回 / apply 立即
// 生效 + 回调后置（下次 commit/apply 落）/ listener 注册-注销-通知 /
// putString(null)=remove / 类型不符 ClassCastException（AOSP 公开面语义）。
// 独立脚本 tools/shared_prefs_tests.sh 驱动（不动既有 tools/*.sh）。
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
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
using dexvm::runtime::ToIntrinsicSlot;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

// ---- 测试侧辅助 ----

// 虚调用（与 invoke-interface 同派发路径：FindInterfaceImpl 同签名）
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

// 字段区清零（AllocObject 不保证字段清零）
VObject* AllocZeroed(ClassLinker* linker, VClass* k) {
  VObject* o = linker->AllocObject(k);
  if (o != nullptr && k->object_size > sizeof(ObjHeader)) {
    std::memset(o->field_data(), 0, k->object_size - sizeof(ObjHeader));
  }
  return o;
}

std::string StrUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

// 失败诊断：pending exception 描述符/消息 + guest 栈
void DumpEx(VMState& state) {
  if (!state.HasPendingException()) {
    std::printf("  (no pending exception)\n");
    return;
  }
  ObjHeader* ex = state.pending_exception;
  std::string desc = (ex != nullptr && ex->klass != nullptr) ? ex->klass->descriptor : "?";
  std::string msg;
  if (ex != nullptr && ex->klass != nullptr) {
    VObject* o = reinterpret_cast<VObject*>(ex);
    for (VClass* k = o->klass; k != nullptr && msg.empty(); k = k->super_class) {
      for (const auto& f : k->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* s = nullptr;
          std::memcpy(&s, o->field_data() + f.offset, sizeof(s));
          if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
          break;
        }
      }
    }
  }
  std::printf("  exception: %s: %s\n", desc.c_str(), msg.c_str());
  for (const auto& f : state.last_throw_backtrace) {
    std::printf("    at %s\n", f.c_str());
  }
}

// ---- 录音 listener（测试内建类：onSharedPreferenceChanged intrinsic）----
std::vector<std::string> g_listener_calls;

Value RecordOnChanged(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  std::string key = (n >= 2 && args[1].ref != nullptr)
                        ? static_cast<VString*>(args[1].ref)->ToModifiedUtf8()
                        : "<null>";
  g_listener_calls.push_back(key);
  return Value{};
}

VClass* MakeListenerClass(ClassLinker* linker) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->status = ClassStatus::kInitialized;
  k->descriptor = "Ltest/RecordingListener;";
  k->super_class = linker->FindClass("Ljava/lang/Object;");
  k->access_flags = 0x1;  // public
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader));
  // 接口向量补挂（guest check-cast/instance-of 到 Listener 接口要求可赋值）
  VClass* iface =
      linker->FindClass("Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;");
  if (iface != nullptr) k->interfaces.push_back(iface);
  VMethod m;
  m.declaring = k;
  m.name = "onSharedPreferenceChanged";
  m.param_types = {"Landroid/content/SharedPreferences;", "Ljava/lang/String;"};
  m.return_type = "V";
  m.shorty = "VLL";
  m.access_flags = 0x1;
  m.code = nullptr;
  m.native_fn = ToIntrinsicSlot(RecordOnChanged);
  k->direct_methods.push_back(std::move(m));
  k->klass = linker->FindClass("Ljava/lang/Class;");
  return k;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== shared_prefs smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";

  // ---- 引导 VM（同 lifecycle_smoke；无 app APK——纯内建面）----
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
    if (boot == nullptr) {
      std::printf("  error: %s\n", err.c_str());
      return 1;
    }
    for (const auto& blob : boot->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse boot dex: " + blob.name).c_str());
      if (dex_file == nullptr) return 1;
      linker.AddBootDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
  }

  // ---- 1. 类面（接口三件 + 实现两件；§31.7 B 档 lib 版：dex_file != nullptr）----
  VClass* sp_cls = linker.FindClass("Landroid/content/SharedPreferences;");
  VClass* ed_cls = linker.FindClass("Landroid/content/SharedPreferences$Editor;");
  VClass* ls_cls = linker.FindClass("Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;");
  VClass* sp_impl_cls = linker.FindClass("Landroid/content/SharedPreferencesImpl;");
  VClass* ed_impl_cls = linker.FindClass("Landroid/content/SharedPreferencesImpl$EditorImpl;");
  CHECK(sp_cls != nullptr && sp_cls->IsInterface(), "SharedPreferences 接口");
  CHECK(ed_cls != nullptr && ed_cls->IsInterface(), "Editor 接口");
  CHECK(ls_cls != nullptr && ls_cls->IsInterface(), "OnSharedPreferenceChangeListener 接口");
  CHECK(sp_impl_cls != nullptr && !sp_impl_cls->IsInterface(), "SharedPreferencesImpl 实现");
  CHECK(ed_impl_cls != nullptr && !ed_impl_cls->IsInterface(), "EditorImpl 实现");
  // lib 版生效：五类均来自 runtime.dex（dex_file != nullptr，非内建）
  CHECK(sp_cls != nullptr && sp_cls->dex_file != nullptr &&
            ed_cls != nullptr && ed_cls->dex_file != nullptr &&
            ls_cls != nullptr && ls_cls->dex_file != nullptr &&
            sp_impl_cls != nullptr && sp_impl_cls->dex_file != nullptr &&
            ed_impl_cls != nullptr && ed_impl_cls->dex_file != nullptr,
        "SP 五类为 lib 版（dex_file != nullptr）");
  CHECK(sp_cls != nullptr && sp_impl_cls != nullptr &&
            sp_cls->IsAssignableFrom(sp_impl_cls),
        "impl 可赋值 SharedPreferences（接口向量）");
  CHECK(ed_cls != nullptr && ed_impl_cls != nullptr &&
            ed_cls->IsAssignableFrom(ed_impl_cls),
        "EditorImpl 可赋值 Editor（接口向量）");

  // ---- 2. Context.getSharedPreferences 接线 + 同名单例 ----
  VClass* ctxw = linker.FindClass("Landroid/content/ContextWrapper;");
  CHECK(ctxw != nullptr, "ContextWrapper 类");
  VObject* ctx = AllocZeroed(&linker, ctxw);
  CHECK(ctx != nullptr, "Context 实例");
  auto get_prefs = [&](const char16_t* name, Value* out) {
    Value args[2];
    args[0].ref = linker.AllocString(name);
    args[1].i = 0;
    return CallV(state, ctx, "getSharedPreferences", {"Ljava/lang/String;", "I"},
                 "Landroid/content/SharedPreferences;", args, 2, out);
  };
  Value ra{}, ra2{}, rb{};
  CHECK(get_prefs(u"alpha", &ra) && ra.ref != nullptr, "getSharedPreferences(alpha)");
  CHECK(get_prefs(u"alpha", &ra2) && ra2.ref == ra.ref, "同名单例（同名同实例）");
  CHECK(get_prefs(u"beta", &rb) && rb.ref != nullptr && rb.ref != ra.ref, "异名分立");
  VObject* sp = reinterpret_cast<VObject*>(ra.ref);

  // ---- 3. Editor 链式 + put 全型 + commit 返回 true ----
  Value red{};
  CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
              &red) &&
            red.ref != nullptr,
        "edit() 返回 Editor");
  VObject* ed = reinterpret_cast<VObject*>(red.ref);
  CHECK(ed->klass == ed_impl_cls, "Editor 实现类");

  auto put = [&](const char* name, std::vector<std::string> params, const char* ret,
                 const Value* args, size_t n, Value* out) {
    return CallV(state, ed, name, std::move(params), ret, args, n, out);
  };
  Value v_str[2];
  v_str[0].ref = linker.AllocString(u"s");
  v_str[1].ref = linker.AllocString(u"hello");
  Value chain{};
  CHECK(put("putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
            "Landroid/content/SharedPreferences$Editor;", v_str, 2, &chain) &&
            chain.ref == ed,
        "putString 返回 Editor 自链");
  Value v_int[2];
  v_int[0].ref = linker.AllocString(u"i");
  v_int[1].i = 42;
  CHECK(put("putInt", {"Ljava/lang/String;", "I"}, "Landroid/content/SharedPreferences$Editor;",
            v_int, 2, nullptr),
        "putInt");
  Value v_long[2];
  v_long[0].ref = linker.AllocString(u"j");
  v_long[1].l = 1234567890123LL;
  CHECK(put("putLong", {"Ljava/lang/String;", "J"}, "Landroid/content/SharedPreferences$Editor;",
            v_long, 2, nullptr),
        "putLong");
  Value v_flt[2];
  v_flt[0].ref = linker.AllocString(u"f");
  v_flt[1].f = 3.5f;
  CHECK(put("putFloat", {"Ljava/lang/String;", "F"}, "Landroid/content/SharedPreferences$Editor;",
            v_flt, 2, nullptr),
        "putFloat");
  Value v_bool[2];
  v_bool[0].ref = linker.AllocString(u"b");
  v_bool[1].i = 1;
  CHECK(put("putBoolean", {"Ljava/lang/String;", "Z"}, "Landroid/content/SharedPreferences$Editor;",
            v_bool, 2, nullptr),
        "putBoolean");
  // putStringSet（guest HashSet 入参）
  VClass* hs_cls = linker.FindClass("Ljava/util/HashSet;");
  CHECK(hs_cls != nullptr, "HashSet 类（runtime.dex）");
  VObject* hs = AllocZeroed(&linker, hs_cls);
  CHECK(hs != nullptr && CallV(state, hs, "<init>", {}, "V", nullptr, 0, nullptr), "HashSet 构造");
  for (const char16_t* e : {u"a", u"b", u"c"}) {
    Value add_a{};
    add_a.ref = linker.AllocString(e);
    CHECK(CallV(state, hs, "add", {"Ljava/lang/Object;"}, "Z", &add_a, 1, nullptr), "HashSet.add");
  }
  Value v_set[2];
  v_set[0].ref = linker.AllocString(u"set");
  v_set[1].ref = hs;
  CHECK(put("putStringSet", {"Ljava/lang/String;", "Ljava/util/Set;"},
            "Landroid/content/SharedPreferences$Editor;", v_set, 2, nullptr),
        "putStringSet");
  Value commit_r{};
  CHECK(CallV(state, ed, "commit", {}, "Z", nullptr, 0, &commit_r) && commit_r.i != 0,
        "commit 返回 true");

  // ---- 4. get 全型 ----
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"s");
    a[1].ref = linker.AllocString(u"dflt");
    Value r{};
    CHECK(CallV(state, sp, "getString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Ljava/lang/String;", a, 2, &r) &&
              StrUtf8(r) == "hello",
          "getString 命中");
  }
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"i");
    a[1].i = -1;
    Value r{};
    CHECK(CallV(state, sp, "getInt", {"Ljava/lang/String;", "I"}, "I", a, 2, &r) && r.i == 42,
          "getInt 命中");
  }
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"j");
    a[1].l = -1;
    Value r{};
    CHECK(CallV(state, sp, "getLong", {"Ljava/lang/String;", "J"}, "J", a, 2, &r) &&
              r.l == 1234567890123LL,
          "getLong 命中");
  }
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"f");
    a[1].f = -1.0f;
    Value r{};
    CHECK(CallV(state, sp, "getFloat", {"Ljava/lang/String;", "F"}, "F", a, 2, &r) && r.f == 3.5f,
          "getFloat 命中");
  }
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"b");
    a[1].i = 0;
    Value r{};
    CHECK(CallV(state, sp, "getBoolean", {"Ljava/lang/String;", "Z"}, "Z", a, 2, &r) && r.i != 0,
          "getBoolean 命中");
  }
  // 默认值面（缺键）
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"missing");
    a[1].ref = linker.AllocString(u"dflt");
    Value r{};
    CHECK(CallV(state, sp, "getString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Ljava/lang/String;", a, 2, &r) &&
              StrUtf8(r) == "dflt",
          "getString 缺键回落默认值");
    a[1].i = 7;
    CHECK(CallV(state, sp, "getInt", {"Ljava/lang/String;", "I"}, "I", a, 2, &r) && r.i == 7,
          "getInt 缺键回落默认值");
  }
  // getStringSet
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"set");
    a[1].ref = nullptr;
    Value r{};
    CHECK(CallV(state, sp, "getStringSet", {"Ljava/lang/String;", "Ljava/util/Set;"},
                "Ljava/util/Set;", a, 2, &r) &&
              r.ref != nullptr,
          "getStringSet 命中");
    VObject* set = reinterpret_cast<VObject*>(r.ref);
    Value sz{};
    CHECK(CallV(state, set, "size", {}, "I", nullptr, 0, &sz) && sz.i == 3, "Set.size == 3");
    Value has_a{};
    has_a.ref = linker.AllocString(u"a");
    CHECK(CallV(state, set, "contains", {"Ljava/lang/Object;"}, "Z", &has_a, 1, &sz) && sz.i != 0,
          "Set.contains(a)");
    Value has_z{};
    has_z.ref = linker.AllocString(u"z");
    CHECK(CallV(state, set, "contains", {"Ljava/lang/Object;"}, "Z", &has_z, 1, &sz) && sz.i == 0,
          "Set 不含 z");
  }
  // contains
  {
    Value a{};
    a.ref = linker.AllocString(u"s");
    Value r{};
    CHECK(CallV(state, sp, "contains", {"Ljava/lang/String;"}, "Z", &a, 1, &r) && r.i != 0,
          "contains 命中");
    a.ref = linker.AllocString(u"nope");
    CHECK(CallV(state, sp, "contains", {"Ljava/lang/String;"}, "Z", &a, 1, &r) && r.i == 0,
          "contains 缺键");
  }
  // getAll（HashMap；值装箱）
  {
    Value r{};
    bool ok_all = CallV(state, sp, "getAll", {}, "Ljava/util/Map;", nullptr, 0, &r);
    if (!ok_all || r.ref == nullptr) DumpEx(state);
    CHECK(ok_all && r.ref != nullptr, "getAll 返回 Map");
    VObject* map = reinterpret_cast<VObject*>(r.ref);
    Value sz{};
    CHECK(CallV(state, map, "size", {}, "I", nullptr, 0, &sz) && sz.i == 6, "Map.size == 6");
    Value gk{};
    gk.ref = linker.AllocString(u"s");
    Value gv{};
    CHECK(CallV(state, map, "get", {"Ljava/lang/Object;"}, "Ljava/lang/Object;", &gk, 1, &gv) &&
              StrUtf8(gv) == "hello",
          "Map.get(s) == hello");
    gk.ref = linker.AllocString(u"i");
    CHECK(CallV(state, map, "get", {"Ljava/lang/Object;"}, "Ljava/lang/Object;", &gk, 1, &gv) &&
              gv.ref != nullptr,
          "Map.get(i) 装箱");
    Value iv{};
    CHECK(CallV(state, reinterpret_cast<VObject*>(gv.ref), "intValue", {}, "I", nullptr, 0, &iv) &&
              iv.i == 42,
          "装箱 Integer.intValue == 42");
  }

  // ---- 5. 类型不符（AOSP ClassCastException）----
  {
    Value a[2];
    a[0].ref = linker.AllocString(u"i");
    a[1].ref = linker.AllocString(u"dflt");
    Value r{};
    bool ok = CallV(state, sp, "getString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                    "Ljava/lang/String;", a, 2, &r);
    CHECK(!ok && state.HasPendingException(), "getString 撞 int 失败");
    if (state.HasPendingException()) {
      ObjHeader* ex = state.pending_exception;
      if (ex == nullptr || ex->klass == nullptr ||
          ex->klass->descriptor != "Ljava/lang/ClassCastException;") {
        DumpEx(state);
      }
      CHECK(ex != nullptr && ex->klass != nullptr &&
                ex->klass->descriptor == "Ljava/lang/ClassCastException;",
            "异常为 ClassCastException");
      state.ClearException();
    }
  }

  // ---- 6. remove / putString(null)=remove ----
  {
    Value a{};
    a.ref = linker.AllocString(u"s");
    Value r{};
    Value red2{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red2) &&
              red2.ref != nullptr,
          "edit() 再取 Editor");
    VObject* ed2 = reinterpret_cast<VObject*>(red2.ref);
    CHECK(CallV(state, ed2, "remove", {"Ljava/lang/String;"},
                "Landroid/content/SharedPreferences$Editor;", &a, 1, nullptr),
          "remove");
    CHECK(CallV(state, ed2, "commit", {}, "Z", nullptr, 0, &r) && r.i != 0, "remove commit");
    CHECK(CallV(state, sp, "contains", {"Ljava/lang/String;"}, "Z", &a, 1, &r) && r.i == 0,
          "remove 后 contains false");
    Value g[2];
    g[0] = a;
    g[1].ref = linker.AllocString(u"dflt");
    CHECK(CallV(state, sp, "getString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Ljava/lang/String;", g, 2, &r) &&
              StrUtf8(r) == "dflt",
          "remove 后 get 回落默认");
  }
  {
    // putString(null) = remove（AOSP 语义）
    Value a[2];
    a[0].ref = linker.AllocString(u"i");
    a[1].ref = nullptr;
    Value red2{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red2),
          "edit()");
    VObject* ed2 = reinterpret_cast<VObject*>(red2.ref);
    CHECK(CallV(state, ed2, "putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Landroid/content/SharedPreferences$Editor;", a, 2, nullptr),
          "putString(null)");
    CHECK(CallV(state, ed2, "commit", {}, "Z", nullptr, 0, nullptr), "commit");
    Value r{};
    a[1].i = -1;
    CHECK(CallV(state, sp, "contains", {"Ljava/lang/String;"}, "Z", a, 1, &r) && r.i == 0,
          "putString(null) 等价 remove");
  }

  // ---- 7. listener 注册/注销/通知（commit 同步；apply 后置）----
  {
    VClass* lcls = MakeListenerClass(&linker);
    CHECK(lcls != nullptr, "录音 listener 类");
    VObject* l1 = AllocZeroed(&linker, lcls);
    Value la{};
    la.ref = l1;
    CHECK(CallV(state, sp, "registerOnSharedPreferenceChangeListener",
                {"Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;"}, "V", &la,
                1, nullptr),
          "listener 注册");
    g_listener_calls.clear();
    Value red2{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red2),
          "edit()");
    VObject* ed2 = reinterpret_cast<VObject*>(red2.ref);
    Value a[2];
    a[0].ref = linker.AllocString(u"k1");
    a[1].ref = linker.AllocString(u"v1");
    CHECK(CallV(state, ed2, "putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Landroid/content/SharedPreferences$Editor;", a, 2, nullptr),
          "putString(k1)");
    CHECK(CallV(state, ed2, "commit", {}, "Z", nullptr, 0, nullptr), "commit");
    if (g_listener_calls.empty()) DumpEx(state);
    CHECK(g_listener_calls.size() == 1 && g_listener_calls[0] == "k1",
          "commit 同步通知 listener（key=k1）");
    // 注销后不再通知
    CHECK(CallV(state, sp, "unregisterOnSharedPreferenceChangeListener",
                {"Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;"}, "V", &la,
                1, nullptr),
          "listener 注销");
    g_listener_calls.clear();
    Value red3{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red3),
          "edit()");
    VObject* ed3 = reinterpret_cast<VObject*>(red3.ref);
    a[0].ref = linker.AllocString(u"k2");
    a[1].ref = linker.AllocString(u"v2");
    CHECK(CallV(state, ed3, "putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Landroid/content/SharedPreferences$Editor;", a, 2, nullptr),
          "putString(k2)");
    CHECK(CallV(state, ed3, "commit", {}, "Z", nullptr, 0, nullptr), "commit");
    CHECK(g_listener_calls.empty(), "注销后 commit 不通知");
    // apply：立即生效 + 回调后置（下次 commit/apply 前落，lib 语义）
    CHECK(CallV(state, sp, "registerOnSharedPreferenceChangeListener",
                {"Landroid/content/SharedPreferences$OnSharedPreferenceChangeListener;"}, "V", &la,
                1, nullptr),
          "listener 再注册");
    g_listener_calls.clear();
    Value red4{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red4),
          "edit()");
    VObject* ed4 = reinterpret_cast<VObject*>(red4.ref);
    a[0].ref = linker.AllocString(u"k3");
    a[1].ref = linker.AllocString(u"v3");
    CHECK(CallV(state, ed4, "putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Landroid/content/SharedPreferences$Editor;", a, 2, nullptr),
          "putString(k3)");
    CHECK(CallV(state, ed4, "apply", {}, "V", nullptr, 0, nullptr), "apply");
    Value r{};
    Value g[2];
    g[0] = a[0];
    g[1].ref = linker.AllocString(u"WRONG");
    CHECK(CallV(state, sp, "getString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Ljava/lang/String;", g, 2, &r) &&
              StrUtf8(r) == "v3",
          "apply 立即生效（内存可见）");
    CHECK(g_listener_calls.empty(), "apply 回调后置（commit/apply 前不落）");
    // 下次 commit：落后置回调（k3）+ 自身通知（k4）
    Value red5{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red5),
          "edit()");
    VObject* ed5 = reinterpret_cast<VObject*>(red5.ref);
    a[0].ref = linker.AllocString(u"k4");
    a[1].ref = linker.AllocString(u"v4");
    CHECK(CallV(state, ed5, "putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                "Landroid/content/SharedPreferences$Editor;", a, 2, nullptr),
          "putString(k4)");
    CHECK(CallV(state, ed5, "commit", {}, "Z", nullptr, 0, nullptr), "commit");
    CHECK(g_listener_calls.size() == 2 && g_listener_calls[0] == "k3" &&
              g_listener_calls[1] == "k4",
          "apply 后置回调随下次 commit 落下（k3,k4）");
  }

  // ---- 8. clear ----
  {
    Value red2{};
    CHECK(CallV(state, sp, "edit", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                &red2),
          "edit()");
    VObject* ed2 = reinterpret_cast<VObject*>(red2.ref);
    CHECK(CallV(state, ed2, "clear", {}, "Landroid/content/SharedPreferences$Editor;", nullptr, 0,
                nullptr) &&
              CallV(state, ed2, "commit", {}, "Z", nullptr, 0, nullptr),
          "clear + commit");
    if (state.HasPendingException()) DumpEx(state);
    Value a{};
    a.ref = linker.AllocString(u"j");
    Value r{};
    CHECK(CallV(state, sp, "contains", {"Ljava/lang/String;"}, "Z", &a, 1, &r) && r.i == 0,
          "clear 后 contains false");
    a.ref = linker.AllocString(u"set");
    CHECK(CallV(state, sp, "contains", {"Ljava/lang/String;"}, "Z", &a, 1, &r) && r.i == 0,
          "clear 后 getStringSet 键清空");
    CHECK(CallV(state, sp, "getAll", {}, "Ljava/util/Map;", nullptr, 0, &r) && r.ref != nullptr,
          "clear 后 getAll");
    VObject* map = reinterpret_cast<VObject*>(r.ref);
    Value sz{};
    CHECK(CallV(state, map, "size", {}, "I", nullptr, 0, &sz) && sz.i == 0, "clear 后 Map.size == 0");
  }

  std::printf("== shared_prefs smoke: %s (%d failures) ==\n",
              g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
