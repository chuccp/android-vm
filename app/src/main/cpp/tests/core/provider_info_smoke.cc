// ProviderInfo 装配 + manifest 值投影 harness（tools/provider_info_tests.sh）。
// 引导链：RegisterBuiltins + RegisterFrameworkBuiltins + runtime.dex boot +
// provider_info.apk app loader（tests/provider-src fixture，aapt2 打包真 binary
// manifest）。覆盖：
//   1) session 解析投影：ProviderEntry 全字段（authorities 分号多值原串、
//      exported/enabled/multiprocess、process/permission 族、<meta-data> 分型）+
//      ManifestInfo versionCode/versionName + <application> <meta-data>
//   2) ProviderInfo 装配全字段（framework_builtins 类面 + BuildProviderInfo
//      值语义：相对名展开、':'-私有进程、permission 回落、intent-filter 缺省
//      exported、metaData 投影、applicationInfo 单例）
//   3) attachInfo 链：fixture provider 覆写读 ProviderInfo 全字段并记录
//      （provider[1..35] 共同墙回归锚点——attachInfo 读字段不再空指针）
//   4) AssembleMetaBundle/ProjectPackageInfo（semface 共用投影：Bundle 分型
//      put、PackageInfo.versionCode/versionName = manifest 真值）
// 负例不在本 harness（失败即停语义 = lifecycle_smoke 既有覆盖）。
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "framework/lifecycle_driver.h"
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

using dexvm::framework::AssembleMetaBundle;
using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::framework::PhaseRecord;
using dexvm::framework::ProjectPackageInfo;
using dexvm::runtime::ClassLinker;
using dexvm::runtime::ClassStatus;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;
using dexvm::session::MetaEntry;
using dexvm::session::ManifestInfo;
using dexvm::session::ProviderEntry;

std::u16string ToU16(const std::string& s) { return std::u16string(s.begin(), s.end()); }

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

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

std::string ReadStaticString(VClass* klass, const char* name) {
  VField* f = klass != nullptr ? FindField(klass, name) : nullptr;
  if (f == nullptr || !f->is_static || f->offset >= klass->static_storage.size()) return "<null>";
  const Value& slot = klass->static_storage[f->offset];
  return slot.ref != nullptr ? static_cast<VString*>(slot.ref)->ToModifiedUtf8() : "<null>";
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

// 测试脚手架：最小 info 类面（semface 类面同型字段；值投影走生产代码）。
// 8 字节槽布局约定同 framework_builtins AddInstanceField。
VClass* MakeStubClass(ClassLinker* linker, const char* desc, uint32_t instance_bytes) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->descriptor = desc;
  k->super_class = linker->FindClass("Ljava/lang/Object;");
  k->status = ClassStatus::kInitialized;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + instance_bytes;
  k->klass = linker->FindClass("Ljava/lang/Class;");
  linker->RegisterBuiltinClass(k);
  return k;
}

void AddStubField(VClass* k, const char* name, const char* type, uint32_t off) {
  VField f;
  f.declaring = k;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = 0x2;
  f.offset = off;
  f.is_static = false;
  k->instance_fields.push_back(std::move(f));
}

void SetRef(VObject* o, uint32_t off, ObjHeader* v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== provider_info smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_apk =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/provider_info.apk";

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

  // ---- fixture APK（真 binary manifest）----
  auto app_session = dexvm::session::Session::Open(fixture_apk, &err);
  CHECK(app_session != nullptr && !app_session->dex_blobs().empty(), "open fixture APK");
  if (app_session == nullptr) {
    std::printf("  error: %s\n", err.c_str());
    return 1;
  }

  // ---- 1) manifest 值投影（session 解析面）----
  const ManifestInfo& mi = app_session->manifest();
  CHECK(mi.package_name == "com.dexvm.pitest", "manifest package_name");
  CHECK(mi.version_code == 77001, "manifest versionCode == 77001");
  CHECK(mi.version_name == "7.7.1", "manifest versionName == 7.7.1");
  CHECK(mi.min_sdk == 21 && mi.target_sdk == 34, "manifest min/target sdk");
  CHECK(mi.application_class == ".ProbeApp", "manifest application name (raw)");
  CHECK(mi.launcher_activity.empty(), "manifest launcher absent");
  CHECK(mi.providers.size() == 3, "manifest provider count == 3");

  if (mi.providers.size() == 3) {
    const ProviderEntry& d = mi.providers[0];  // 声明序：Defaults/Perms/Multi
    CHECK(d.name == ".DefaultsProbe" && d.authorities == "com.dexvm.pitest.gamma",
          "entry DefaultsProbe name/authorities");
    CHECK(!d.exported && d.enabled && !d.multiprocess && d.init_order == 0,
          "entry DefaultsProbe exported=false enabled=true multiprocess=false initOrder=0");
    CHECK(d.process_name.empty() && d.permission.empty() && d.read_permission.empty() &&
              d.write_permission.empty(),
          "entry DefaultsProbe process/permission 未声明为空");
    CHECK(d.meta_data.empty(), "entry DefaultsProbe meta_data empty");

    const ProviderEntry& p = mi.providers[1];
    CHECK(p.name == "com.dexvm.pitest.PermsProbe" && p.authorities == "com.dexvm.pitest.delta",
          "entry PermsProbe name/authorities");
    CHECK(p.exported && !p.enabled && p.init_order == 5,
          "entry PermsProbe exported(缺省=intent-filter)=true enabled=false initOrder=5");
    CHECK(p.read_permission == "com.dexvm.pitest.R" &&
              p.write_permission == "com.dexvm.pitest.W",
          "entry PermsProbe read/writePermission 分立");
    CHECK(p.permission.empty(), "entry PermsProbe permission 未声明为空（无回落改写）");

    const ProviderEntry& m = mi.providers[2];
    CHECK(m.name == ".MultiProbe" && m.authorities == "com.dexvm.pitest.alpha;com.dexvm.pitest.beta",
          "entry MultiProbe 相对名 + authorities 分号多值原串");
    CHECK(m.exported && m.enabled && m.multiprocess && m.init_order == 10,
          "entry MultiProbe exported/enabled/multiprocess/initOrder");
    CHECK(m.process_name == ":remote", "entry MultiProbe process 原串 ':remote'");
    CHECK(m.permission == "com.dexvm.pitest.PERM" &&
              m.read_permission == "com.dexvm.pitest.PERM" &&
              m.write_permission == "com.dexvm.pitest.PERM",
          "entry MultiProbe read/writePermission 缺省回落 permission");
    CHECK(m.meta_data.size() == 3, "entry MultiProbe meta_data 3 条");
    if (m.meta_data.size() == 3) {
      CHECK(m.meta_data[0].name == "p.key" && m.meta_data[0].type == MetaEntry::Type::kString &&
                m.meta_data[0].str == "pval",
            "meta p.key = string pval");
      CHECK(m.meta_data[1].name == "p.n" && m.meta_data[1].type == MetaEntry::Type::kInt &&
                m.meta_data[1].num == 7,
            "meta p.n = int 7");
      CHECK(m.meta_data[2].name == "p.b" && m.meta_data[2].type == MetaEntry::Type::kBool &&
                m.meta_data[2].num == 1,
            "meta p.b = bool true");
    }
  }

  CHECK(mi.debuggable && mi.large_heap && mi.has_code,
        "application 属性投影 debuggable/largeHeap/hasCode（hasCode 缺省 true）");

  CHECK(mi.application_meta_data.size() == 3, "application meta_data 3 条");
  if (mi.application_meta_data.size() == 3) {
    CHECK(mi.application_meta_data[0].name == "app.flag" &&
              mi.application_meta_data[0].type == MetaEntry::Type::kBool &&
              mi.application_meta_data[0].num == 1,
          "app meta app.flag = bool true");
    CHECK(mi.application_meta_data[1].name == "app.count" &&
              mi.application_meta_data[1].type == MetaEntry::Type::kInt &&
              mi.application_meta_data[1].num == 42,
          "app meta app.count = int 42");
    CHECK(mi.application_meta_data[2].name == "app.tag" &&
              mi.application_meta_data[2].type == MetaEntry::Type::kString &&
              mi.application_meta_data[2].str == "hello",
          "app meta app.tag = string hello");
  }

  // ---- boot + app dex 装载 ----
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

  // ---- 测试脚手架：ApplicationInfo/PackageInfo 最小类面 + 单例 ----
  VObject* ai_stub = nullptr;
  VObject* pi_stub = nullptr;
  {
    VClass* ai = MakeStubClass(&linker, "Landroid/content/pm/ApplicationInfo;", 8);
    VClass* pi = MakeStubClass(&linker, "Landroid/content/pm/PackageInfo;", 40);
    CHECK(ai != nullptr && pi != nullptr, "stub ApplicationInfo/PackageInfo classes");
    if (ai == nullptr || pi == nullptr) return 1;
    AddStubField(ai, "packageName", "Ljava/lang/String;", 0);
    AddStubField(pi, "packageName", "Ljava/lang/String;", 0);
    AddStubField(pi, "applicationInfo", "Landroid/content/pm/ApplicationInfo;", 8);
    AddStubField(pi, "signatures", "[Landroid/content/pm/Signature;", 16);
    AddStubField(pi, "versionName", "Ljava/lang/String;", 24);
    AddStubField(pi, "versionCode", "I", 32);
    ai_stub = linker.AllocObject(ai);
    pi_stub = linker.AllocObject(pi);
    if (ai_stub != nullptr) {
      SetRef(ai_stub, 0, reinterpret_cast<ObjHeader*>(linker.AllocString(ToU16("com.dexvm.pitest"))));
    }
    dexvm::runtime::SetContextApplicationInfo(ai_stub);
  }

  // ---- 4) ProjectPackageInfo（semface 共用：versionCode/versionName = manifest 真值）----
  ProjectPackageInfo(state, pi_stub, mi);
  {
    VField* vn = FindField(pi_stub->klass, "versionName");
    VField* vc = FindField(pi_stub->klass, "versionCode");
    ObjHeader* v = nullptr;
    if (vn != nullptr) std::memcpy(&v, pi_stub->field_data() + vn->offset, sizeof(v));
    CHECK(v != nullptr && static_cast<VString*>(v)->ToModifiedUtf8() == "7.7.1",
          "PackageInfo.versionName = manifest 值");
    CHECK(vc != nullptr && ReadIField(pi_stub, vc->offset) == 77001,
          "PackageInfo.versionCode = manifest 值");
  }

  // ---- 4) AssembleMetaBundle（<meta-data> -> Bundle 分型 put）----
  {
    VObject* bundle = AssembleMetaBundle(state, mi.application_meta_data);
    CHECK(bundle != nullptr, "AssembleMetaBundle(application meta_data)");
    if (bundle != nullptr) {
      Value k{};
      k.ref = reinterpret_cast<ObjHeader*>(linker.AllocString(ToU16("app.tag")));
      Value r{};
      bool ok = CallVirtual(state, bundle, "getString", {"Ljava/lang/String;"},
                            "Ljava/lang/String;", &k, 1, &r);
      CHECK(ok && ResultUtf8(r) == "hello", "Bundle.getString(app.tag) == hello");
      k.ref = reinterpret_cast<ObjHeader*>(linker.AllocString(ToU16("app.count")));
      ok = CallVirtual(state, bundle, "getInt", {"Ljava/lang/String;"}, "I", &k, 1, &r);
      CHECK(ok && static_cast<int32_t>(r.l) == 42, "Bundle.getInt(app.count) == 42");
      k.ref = reinterpret_cast<ObjHeader*>(linker.AllocString(ToU16("app.flag")));
      ok = CallVirtual(state, bundle, "getBoolean", {"Ljava/lang/String;"}, "Z", &k, 1, &r);
      CHECK(ok && r.l != 0, "Bundle.getBoolean(app.flag) == true");
      state.ClearException();
    }
    CHECK(AssembleMetaBundle(state, {}) == nullptr, "AssembleMetaBundle(空清单) == null (AOSP)");
  }

  // ---- 2) ProviderInfo 类面全字段（framework_builtins）----
  {
    VClass* pi_cls = linker.FindClass("Landroid/content/pm/ProviderInfo;");
    CHECK(pi_cls != nullptr, "FindClass ProviderInfo");
    const char* kFields[] = {"name",        "packageName", "processName",   "authority",
                             "readPermission", "writePermission", "applicationInfo", "metaData",
                             "exported",    "enabled",     "multiprocess",  "initOrder"};
    bool all = pi_cls != nullptr;
    for (const char* n : kFields) {
      if (pi_cls == nullptr || FindField(pi_cls, n) == nullptr) {
        std::printf("  missing ProviderInfo field: %s\n", n);
        all = false;
      }
    }
    CHECK(all, "ProviderInfo 全字段面（12 项）");
  }

  // ---- 2/3) LifecycleDriver：ProviderInfo 装配 + attachInfo 链 ----
  LifecycleConfig cfg = ConfigFromManifest(mi);
  CHECK(cfg.providers.size() == 3, "cfg provider count == 3");
  if (cfg.providers.size() == 3) {
    // install 序 = initOrder 降序（docs 19.2）
    CHECK(cfg.providers[0].class_name == "com.dexvm.pitest.MultiProbe" &&
              cfg.providers[1].class_name == "com.dexvm.pitest.PermsProbe" &&
              cfg.providers[2].class_name == "com.dexvm.pitest.DefaultsProbe",
          "cfg 相对名展开 + initOrder 降序（Multi/Perms/Defaults）");
    CHECK(cfg.application_class == "com.dexvm.pitest.ProbeApp", "cfg application 展开");
    CHECK(cfg.activity_class.empty(), "cfg activity 跳过");
    CHECK(cfg.providers[0].entry.process_name == ":remote", "cfg entry 值投影保留");
  }

  LifecycleDriver driver(state, app_loader);
  bool ran = driver.Run(cfg);
  CHECK(ran, "lifecycle run completes");
  if (!ran) std::printf("  error: %s\n", driver.error().c_str());

  const char* kPhases[] = {
      "frameworkState",        "application.create",       "application.attachBaseContext",
      "provider[0].create",    "provider[0].attachInfo",   "provider[0].onCreate",
      "provider[1].create",    "provider[1].attachInfo",   "provider[1].onCreate",
      "provider[2].create",    "provider[2].attachInfo",   "provider[2].onCreate",
      "application.onCreate",
  };
  bool phases_ok = driver.phases().size() == std::size(kPhases);
  for (size_t i = 0; phases_ok && i < std::size(kPhases); i++) {
    if (driver.phases()[i].name != kPhases[i] || !driver.phases()[i].ok) {
      std::printf("  phase[%zu]: got %s (ok=%d), want %s\n", i,
                  driver.phases()[i].name.c_str(), driver.phases()[i].ok ? 1 : 0, kPhases[i]);
      phases_ok = false;
    }
  }
  CHECK(phases_ok, "phase 序列 = AOSP 序，全 ok（provider 链推进至 application.onCreate）");

  // ProbeApp 生命周期序（provider 全链后 Application.onCreate）
  CHECK(ReadStaticString(linker.FindClass("Lcom/dexvm/pitest/ProbeApp;"), "lifecycleLog") ==
            "attachBaseContext;onCreate;",
        "ProbeApp.lifecycleLog");

  // ---- 3) attachInfo 链：fixture provider 读字段断言（静态记录面）----
  {
    std::string obs = ReadStaticString(linker.FindClass("Lcom/dexvm/pitest/InfoProbeProvider;"),
                                       "observations");
    std::printf("observations:\n%s", obs.c_str());
    const std::string kExpected =
        // install 序：Multi(10) -> Perms(5) -> Defaults(0)；字段序 = 探针读序
        "com.dexvm.pitest.MultiProbe|com.dexvm.pitest.alpha;com.dexvm.pitest.beta|"
        "com.dexvm.pitest|com.dexvm.pitest:remote|true|true|10|com.dexvm.pitest.PERM|"
        "com.dexvm.pitest.PERM|true|pval,7,true|com.dexvm.pitest\n"
        "com.dexvm.pitest.PermsProbe|com.dexvm.pitest.delta|com.dexvm.pitest|com.dexvm.pitest|"
        "true|false|5|com.dexvm.pitest.R|com.dexvm.pitest.W|false|null|com.dexvm.pitest\n"
        "com.dexvm.pitest.DefaultsProbe|com.dexvm.pitest.gamma|com.dexvm.pitest|"
        "com.dexvm.pitest|false|true|0|null|null|false|null|com.dexvm.pitest\n";
    CHECK(obs == kExpected, "attachInfo 全字段断言（fixture provider 读字段）");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
