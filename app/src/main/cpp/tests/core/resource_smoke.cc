// Resources 面 + grantUriPermissions 投影 harness（tools/resource_tests.sh）。
// 引导链：RegisterBuiltins + RegisterFrameworkBuiltins + runtime.dex boot +
// resource.apk app loader（tests/resource-src fixture，aapt2 编 res/xml 真
// binary AXML + 真 binary manifest）。覆盖：
//   1) grantUri 投影：ProviderEntry.grant_uri_permissions（true/缺省 false 两面）
//      + BuildProviderInfo 投影（fixture attachInfo 同 FileProvider SecurityException
//      消费面断言 grantUriPermissions/exported 字段真值）
//   2) resources.arsc 面（C++ 缝直断言）：LookupResourceId(type,entry,pkg) ->
//      resId、ReadResourceFile(resId) -> res/xml/file_paths.xml AXML 字节、
//      ParseResourceXml -> 事件流（paths/files-path/cache-path/external-path/
//      root-path + name/path 属性）
//   3) guest API 面（fixture 探针，FileProvider 消费链同款）：
//      pm.resolveContentProvider -> info.loadXmlMetaData -> pm.
//      getResourcesForApplication -> Resources.getXml -> XmlResourceParser
//      next/getName/getAttributeValue；Resources.getIdentifier + getXml 直面
//      （meta-data 资源 id 对账）；openXmlResourceParser 别名（C++ CallVirtual）
//   4) 生命周期链推进至 application.onCreate（phase 序列 AOSP 序）
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/intrinsic.h"
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

using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::runtime::ClassLinker;
using dexvm::runtime::ClassStatus;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::ToIntrinsicSlot;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;
using dexvm::session::ManifestInfo;
using dexvm::session::ProviderEntry;
using dexvm::session::XmlResEvent;

std::u16string ToU16(const std::string& s) { return std::u16string(s.begin(), s.end()); }

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

std::string ReadStaticString(VClass* klass, const char* name) {
  VField* f = klass != nullptr ? FindField(klass, name) : nullptr;
  if (f == nullptr || !f->is_static || f->offset >= klass->static_storage.size()) return "<null>";
  const Value& slot = klass->static_storage[f->offset];
  return slot.ref != nullptr ? static_cast<VString*>(slot.ref)->ToModifiedUtf8() : "<null>";
}

void SetRef(VObject* o, uint32_t off, ObjHeader* v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
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

// intrinsic 方法骨架（semface AddIntrinsic 同型）
void AddStubIntrinsic(VClass* k, const char* name, std::vector<std::string> params,
                      const char* ret, uint32_t access, dexvm::runtime::IntrinsicFn fn) {
  VMethod m;
  m.declaring = k;
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
  k->direct_methods.push_back(std::move(m));
}

// java.io.File 构造（guest <init>(String)；失败回落直写 path 字段，semface 同型）
VObject* MakeGuestFile(VMState& state, const std::string& path) {
  VClass* file_cls = state.linker->FindClass("Ljava/io/File;");
  if (file_cls == nullptr) return nullptr;
  VObject* file = state.linker->AllocObject(file_cls);
  if (file == nullptr) return nullptr;
  VMethod* ctor = nullptr;
  for (auto& m : file_cls->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 1 &&
        m.param_types[0] == "Ljava/lang/String;") {
      ctor = &m;
      break;
    }
  }
  bool built = false;
  if (ctor != nullptr) {
    Value args[2] = {};
    args[0].ref = file;
    args[1].ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(ToU16(path)));
    Value ret{};
    built = InvokeMethod(state, ctor, args, 2, &ret) && !state.HasPendingException();
    state.ClearException();
  }
  if (!built) {
    for (const auto& f : file_cls->instance_fields) {
      if (f.name == "path") {
        SetRef(file, f.offset,
               reinterpret_cast<ObjHeader*>(state.linker->AllocString(ToU16(path))));
        built = true;
        break;
      }
    }
  }
  return built ? file : nullptr;
}

Value g_external_storage;  // Environment stub 数据（静态根扫描覆盖）

Value EnvGetExternalStorage(VMState& state, VObject*, const Value*, size_t, bool* ok) {
  *ok = true;
  (void)state;
  return g_external_storage;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== resource smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_apk =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/resource.apk";

  // ---- 引导 VM（同 provider_info_smoke）----
  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  // Context 文件面数据根（getFilesDir/getCacheDir 基准；预期路径锚点）
  const std::string data_dir = "/tmp/dexvm_resource_smoke_data";
  dexvm::runtime::SetContextDataDir(data_dir);
  // 外部存储根（getExternal*Dirs + Environment 同源；设备 = /storage/emulated/0）
  const std::string ext_root = "/tmp/dexvm_resource_smoke_ext";
  dexvm::runtime::SetContextExternalRoot(ext_root);

  std::string err;
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;

  auto app_session = dexvm::session::Session::Open(fixture_apk, &err);
  CHECK(app_session != nullptr && !app_session->dex_blobs().empty(), "open resource fixture APK");
  if (app_session == nullptr) {
    std::printf("  error: %s\n", err.c_str());
    return 1;
  }

  // ---- 1) grantUri 投影（session 解析面）----
  const ManifestInfo& mi = app_session->manifest();
  CHECK(mi.package_name == "com.dexvm.rtest", "manifest package_name");
  CHECK(mi.providers.size() == 2, "manifest provider count == 2");
  if (mi.providers.size() == 2) {
    const ProviderEntry& pp = mi.providers[0];  // 声明序：Paths/Plain
    CHECK(pp.name == ".PathsProbeProvider" && pp.authorities == "com.dexvm.rtest.files",
          "entry PathsProbe name/authorities");
    CHECK(pp.grant_uri_permissions, "entry PathsProbe grant_uri_permissions == true (0x0101001b)");
    CHECK(!pp.exported && pp.init_order == 10, "entry PathsProbe exported=false initOrder=10");
    CHECK(pp.meta_data.size() == 1 && pp.meta_data[0].name == "android.support.FILE_PROVIDER_PATHS",
          "entry PathsProbe meta FILE_PROVIDER_PATHS");
    if (!pp.meta_data.empty()) {
      CHECK(pp.meta_data[0].type == dexvm::session::MetaEntry::Type::kResource &&
                pp.meta_data[0].num != 0,
            "meta resource id != 0（android:resource 投影）");
    }
    const ProviderEntry& pl = mi.providers[1];
    CHECK(!pl.grant_uri_permissions, "entry PlainProbe grant_uri_permissions 缺省 false");
  }

  // ---- 2) resources.arsc 缝（C++ 直断言）----
  int32_t rid = dexvm::session::LookupResourceId(fixture_apk, "com.dexvm.rtest", "xml",
                                                 "file_paths");
  CHECK(rid != 0, "LookupResourceId(xml/file_paths) != 0");
  {
    std::string entry;
    std::vector<uint8_t> bytes;
    std::string rerr;
    CHECK(dexvm::session::ReadResourceFile(fixture_apk, static_cast<uint32_t>(rid), &entry, &bytes,
                                           &rerr),
          "ReadResourceFile(resId)");
    CHECK(entry == "res/xml/file_paths.xml", "entry path = res/xml/file_paths.xml");
    std::vector<XmlResEvent> events;
    CHECK(dexvm::session::ParseResourceXml(bytes, &events, &rerr), "ParseResourceXml(AXML)");
    size_t starts = 0;
    for (const auto& e : events) {
      if (e.type == XmlResEvent::kStartTag) starts++;
    }
    CHECK(!events.empty() && events.back().type == XmlResEvent::kEndDocument,
          "事件流恒以 END_DOCUMENT 收口");
    CHECK(starts == 8, "事件流 START_TAG = 8（paths + 7 子元素）");
    bool attrs_ok = false;
    for (const auto& e : events) {
      if (e.type == XmlResEvent::kStartTag && e.name == "cache-path") {
        attrs_ok = e.attrs.size() == 2 && e.attrs[0].name == "name" && e.attrs[0].value == "cache" &&
                   e.attrs[1].name == "path" && e.attrs[1].value == "c1";
      }
    }
    CHECK(attrs_ok, "cache-path name/path 属性解析");
    // getIdentifier 语义源对账（package/type/entry -> resId）
    CHECK(dexvm::session::LookupResourceId(fixture_apk, "", "xml", "file_paths") == rid,
          "LookupResourceId 空包名回落同 resId");
    CHECK(dexvm::session::LookupResourceId(fixture_apk, "com.dexvm.rtest", "xml", "nope") == 0,
          "LookupResourceId 未命中 == 0");
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

  // ---- 服务桥装配：ApplicationInfo（sourceDir=fixture APK）/ PackageManager /
  //      Resources / Environment ----
  {
    VClass* ai = MakeStubClass(&linker, "Landroid/content/pm/ApplicationInfo;", 16);
    CHECK(ai != nullptr, "stub ApplicationInfo class");
    if (ai == nullptr) return 1;
    AddStubField(ai, "packageName", "Ljava/lang/String;", 0);
    AddStubField(ai, "sourceDir", "Ljava/lang/String;", 8);
    VObject* ai_stub = linker.AllocObject(ai);
    SetRef(ai_stub, 0,
           reinterpret_cast<ObjHeader*>(linker.AllocString(ToU16("com.dexvm.rtest"))));
    SetRef(ai_stub, 8, reinterpret_cast<ObjHeader*>(linker.AllocString(ToU16(fixture_apk))));
    dexvm::runtime::SetContextApplicationInfo(ai_stub);

    VClass* pm_cls = linker.FindClass("Landroid/content/pm/PackageManager;");
    CHECK(pm_cls != nullptr, "FindClass PackageManager（framework 内建）");
    VObject* pm = pm_cls != nullptr ? linker.AllocObject(pm_cls) : nullptr;
    CHECK(pm != nullptr, "PackageManager instance");
    dexvm::runtime::SetContextPackageManager(pm);

    VObject* res = dexvm::runtime::AllocResources(state, fixture_apk, "com.dexvm.rtest");
    CHECK(res != nullptr, "AllocResources(fixture APK)");
    dexvm::runtime::SetContextResources(res);

    VClass* env = MakeStubClass(&linker, "Landroid/os/Environment;", 0);
    if (env != nullptr) {
      AddStubIntrinsic(env, "getExternalStorageDirectory", {}, "Ljava/io/File;",
                       0x9 /* public static */, EnvGetExternalStorage);
    }
    g_external_storage.ref = MakeGuestFile(state, ext_root);
    CHECK(g_external_storage.ref != nullptr, "Environment stub File(ext root)");
  }

  // ---- openXmlResourceParser 别名（任务口径；getXml 同实现）----
  {
    dexvm::framework::InstallResourceSeam();  // Run 入口亦自挂（幂等）；此处显式先挂
    VObject* res = dexvm::runtime::AllocResources(state, fixture_apk, "com.dexvm.rtest");
    Value arg{};
    arg.l = rid;
    Value out{};
    bool ok = res != nullptr &&
              CallVirtual(state, res, "openXmlResourceParser", {"I"},
                          "Landroid/content/res/XmlResourceParser;", &arg, 1, &out);
    CHECK(ok && out.ref != nullptr, "Resources.openXmlResourceParser(resId) -> parser");
    if (ok && out.ref != nullptr) {
      Value t{};
      bool nok = CallVirtual(state, reinterpret_cast<VObject*>(out.ref), "next", {}, "I", nullptr,
                             0, &t);
      CHECK(nok && static_cast<int32_t>(t.i) == 2, "parser.next() 首事件 == START_TAG(paths)");
    }
    state.ClearException();
  }

  // ---- 3/4) 生命周期链（fixture 探针消费面全链）----
  LifecycleConfig cfg = ConfigFromManifest(mi);
  CHECK(cfg.providers.size() == 2, "cfg provider count == 2");
  LifecycleDriver driver(state, app_loader);
  bool ran = driver.Run(cfg);
  CHECK(ran, "lifecycle run completes");
  if (!ran) std::printf("  error: %s\n", driver.error().c_str());

  const char* kPhases[] = {
      "frameworkState",      "application.create",     "application.attachBaseContext",
      "provider[0].create",  "provider[0].attachInfo", "provider[0].onCreate",
      "provider[1].create",  "provider[1].attachInfo", "provider[1].onCreate",
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
  CHECK(phases_ok, "phase 序列 = AOSP 序，全 ok（provider 链 + application.onCreate）");

  CHECK(ReadStaticString(linker.FindClass("Lcom/dexvm/rtest/RTestApp;"), "lifecycleLog") ==
            "attachBaseContext;onCreate;",
        "RTestApp.lifecycleLog");

  // fixture 探针观测（FileProvider 消费链全断言）
  {
    std::string obs =
        ReadStaticString(linker.FindClass("Lcom/dexvm/rtest/PathsProbeProvider;"), "observations");
    std::printf("observations:\n%s", obs.c_str());
    char rid_hex[16];
    std::snprintf(rid_hex, sizeof(rid_hex), "%x", static_cast<unsigned>(rid));
    const std::string kTags[2] = {"metaParse:\n", "idParse:\n"};
    std::string kExpected = std::string("grant=true|exported=false;\n") +
                            "resolved=com.dexvm.rtest.files;\n";
    for (int pass = 0; pass < 2; pass++) {
      if (pass == 1) {
        kExpected += std::string("idmatch=true|id=0x") + rid_hex + ";\n";
      }
      kExpected += kTags[pass];
      kExpected += "files-path|files|" + data_dir + "/files/d1;\n";
      kExpected += "cache-path|cache|" + data_dir + "/cache/c1;\n";
      kExpected += "external-path|ext|" + ext_root + "/e1;\n";
      kExpected += "root-path|root|/r1;\n";
      kExpected += "external-files-path|efiles|" + ext_root +
                   "/Android/data/com.dexvm.rtest/files/ef1;\n";
      kExpected += "external-cache-path|ecache|" + ext_root +
                   "/Android/data/com.dexvm.rtest/cache/ec1;\n";
      kExpected += "external-media-path|emedia|" + ext_root +
                   "/Android/media/com.dexvm.rtest/em1;\n";
    }
    CHECK(obs == kExpected, "FileProvider 消费链观测全对（meta 链 + getIdentifier/getXml 链）");
  }
  {
    std::string obs =
        ReadStaticString(linker.FindClass("Lcom/dexvm/rtest/PlainProbeProvider;"), "observations");
    CHECK(obs == "grant=false|exported=false|auth=com.dexvm.rtest.plain;\n",
          "PlainProbe grantUriPermissions 缺省 false 投影");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
