// M4 阶段 1：VM 内 Framework 生命周期链冒烟。
// target-app 真实 APK 引导（同 apk_run_smoke 链）-> LifecycleDriver 按 AOSP
// 顺序驱动 Application->Provider->Activity 全生命周期（docs 10 章 19.2/19.3）
// -> 断言 RuntimeLifecycleApp.lifecycleLog 精确等于期望串；
// 负例：手写最小 DEX（NegProvider.onCreate 抛 RuntimeException）验证 S5
// 失败停链语义（docs 11 章：Guest 异常记录后终止，后续阶段不执行）。
#include <cstdio>
#include <cstring>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "framework/lifecycle_driver.h"
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

using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::framework::PhaseRecord;
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

VMethod* FindStaticStringGetter(VClass* klass, const char* name) {
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.IsStatic() && m.param_types.empty() &&
        m.return_type == "Ljava/lang/String;") {
      return &m;
    }
  }
  for (auto& m : klass->virtual_methods) {
    if (m.name == name && m.IsStatic() && m.param_types.empty() &&
        m.return_type == "Ljava/lang/String;") {
      return &m;
    }
  }
  return nullptr;
}

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

int CountOccurrences(const std::string& hay, const char* needle) {
  int count = 0;
  for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) {
    count++;
  }
  return count;
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

std::u16string ToU16(const std::string& s) {
  return std::u16string(s.begin(), s.end());
}

int32_t ReadIField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}

float ReadFField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  float v = 0;
  std::memcpy(&v, &slot, sizeof(v));
  return v;
}

// 虚调用（测试侧观测用）
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

// ---- 手写最小 DEX：Lcom/dexvm/neg/NegProvider; extends ContentProvider ----
// onCreate()Z：new RuntimeException("neg provider boom") 并 throw（S5 负例注入）。
// 结构校验面 = dex_verify.cc（map 可缺、checksum 不校验）；布局对照 DEX 规范。
struct DexWriter {
  std::vector<uint8_t> buf;

  void Pad4() { while (buf.size() % 4 != 0) buf.push_back(0); }
  void U16(uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xff));
    buf.push_back(static_cast<uint8_t>(v >> 8));
  }
  void U32(uint32_t v) {
    for (int i = 0; i < 4; i++) buf.push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xff));
  }
  void Uleb(uint32_t v) {
    for (;;) {
      uint8_t b = v & 0x7f;
      v >>= 7;
      if (v != 0) b |= 0x80;
      buf.push_back(b);
      if (v == 0) break;
    }
  }
};

std::vector<uint8_t> BuildNegProviderDex() {
  // string_ids（UTF-16 升序）
  const char* kStrings[] = {
      "<init>",                         // 0
      "Landroid/content/ContentProvider;",  // 1
      "Lcom/dexvm/neg/NegProvider;",    // 2
      "Ljava/lang/RuntimeException;",   // 3
      "Ljava/lang/String;",             // 4
      "V",                              // 5
      "VL",                             // 6
      "Z",                              // 7
      "neg provider boom",              // 8
      "onCreate",                       // 9
  };
  constexpr uint32_t kNumStrings = 10;
  // type_ids -> descriptor string idx
  const uint32_t kTypes[] = {1, 2, 3, 4, 5, 7};  // 6
  constexpr uint32_t kNumTypes = 6;

  // ---- data 段（code_off 等为绝对文件偏移：data_off 先行算出） ----
  constexpr uint32_t kHeaderSize = 0x70;
  constexpr uint32_t kNumProtos = 3;
  constexpr uint32_t kNumMethods = 4;
  constexpr uint32_t kNumClasses = 1;
  uint32_t string_ids_off = kHeaderSize;
  uint32_t type_ids_off = string_ids_off + kNumStrings * 4;
  uint32_t proto_ids_off = type_ids_off + kNumTypes * 4;
  uint32_t method_ids_off = proto_ids_off + kNumProtos * 12;  // field_ids 为空
  uint32_t class_defs_off = method_ids_off + kNumMethods * 8;
  uint32_t data_off = class_defs_off + kNumClasses * 32;

  DexWriter data;
  uint32_t string_data_off[kNumStrings];
  for (uint32_t i = 0; i < kNumStrings; i++) {
    string_data_off[i] = static_cast<uint32_t>(data.buf.size());
    const char* s = kStrings[i];
    uint32_t len = static_cast<uint32_t>(std::strlen(s));
    data.Uleb(len);  // utf16_size == byte_len（ASCII）
    for (uint32_t j = 0; j < len; j++) data.buf.push_back(static_cast<uint8_t>(s[j]));
    data.buf.push_back(0);
  }
  data.Pad4();
  uint32_t typelist_off = static_cast<uint32_t>(data.buf.size());
  data.U32(1);  // count
  data.U16(3);  // [Ljava/lang/String; = type 3
  data.Pad4();
  // code item：<init>()V
  uint32_t code_init_off = static_cast<uint32_t>(data.buf.size());
  {
    data.U16(1);  // registers_size
    data.U16(1);  // ins_size
    data.U16(1);  // outs_size
    data.U16(0);  // tries_size
    data.U32(0);  // debug_info_off
    data.U32(4);  // insns_size
    data.U16(0x1070);  // invoke-direct {v0}, ContentProvider.<init>()V
    data.U16(0x0000);
    data.U16(0x0000);
    data.U16(0x000e);  // return-void
  }
  data.Pad4();
  // code item：onCreate()Z -> throw new RuntimeException("neg provider boom")
  uint32_t code_oncreate_off = static_cast<uint32_t>(data.buf.size());
  {
    data.U16(3);  // registers_size (v0 obj, v1 str, v2 this)
    data.U16(1);  // ins_size
    data.U16(2);  // outs_size
    data.U16(0);
    data.U32(0);
    data.U32(8);  // insns_size
    data.U16(0x0022);  // new-instance v0, Ljava/lang/RuntimeException;
    data.U16(0x0002);
    data.U16(0x011a);  // const-string v1, "neg provider boom"
    data.U16(0x0008);
    data.U16(0x2070);  // invoke-direct {v0, v1}, RuntimeException.<init>(String)V
    data.U16(0x0003);
    data.U16(0x0010);
    data.U16(0x0027);  // throw v0
  }
  uint32_t class_data_off = static_cast<uint32_t>(data.buf.size());
  {
    data.Uleb(0);  // static_fields_size
    data.Uleb(0);  // instance_fields_size
    data.Uleb(1);  // direct_methods_size
    data.Uleb(1);  // virtual_methods_size
    data.Uleb(1);        // direct[0] method_idx_diff = 1 -> idx 1 NegProvider.<init>
    data.Uleb(0x10001);  // public constructor
    data.Uleb(data_off + code_init_off);  // code_off 为绝对偏移
    data.Uleb(2);  // virtual[0] method_idx_diff = 2 -> idx 2 NegProvider.onCreate
    data.Uleb(0x1);
    data.Uleb(data_off + code_oncreate_off);
  }

  DexWriter w;
  // header_item
  const char kMagic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
  w.buf.insert(w.buf.end(), kMagic, kMagic + 8);
  w.U32(0);  // checksum（dex_verify 不校验）
  for (int i = 0; i < 20; i++) w.buf.push_back(0);  // signature
  w.U32(data_off + static_cast<uint32_t>(data.buf.size()));  // file_size
  w.U32(kHeaderSize);
  w.U32(0x12345678);  // endian_tag
  w.U32(0);           // link_size
  w.U32(0);           // link_off
  w.U32(0);           // map_off（无 map：跳过校验）
  w.U32(kNumStrings);
  w.U32(string_ids_off);
  w.U32(kNumTypes);
  w.U32(type_ids_off);
  w.U32(kNumProtos);
  w.U32(proto_ids_off);
  w.U32(0);  // field_ids_size
  w.U32(0);
  w.U32(kNumMethods);
  w.U32(method_ids_off);
  w.U32(kNumClasses);
  w.U32(class_defs_off);
  w.U32(static_cast<uint32_t>(data.buf.size()));  // data_size
  w.U32(data_off);

  // string_ids
  for (uint32_t i = 0; i < kNumStrings; i++) w.U32(data_off + string_data_off[i]);
  // type_ids
  for (uint32_t i = 0; i < kNumTypes; i++) w.U32(kTypes[i]);
  // proto_ids：()V、()Z、(Ljava/lang/String;)V
  w.U32(5);  // shorty "V"
  w.U32(4);  // ret type V
  w.U32(0);
  w.U32(7);  // shorty "Z"
  w.U32(5);  // ret type Z
  w.U32(0);
  w.U32(6);                    // shorty "VL"
  w.U32(4);                    // ret type V
  w.U32(data_off + typelist_off);
  // method_ids：ContentProvider.<init>、NegProvider.<init>、NegProvider.onCreate、
  // RuntimeException.<init>(String)
  w.U16(0); w.U16(0); w.U32(0);
  w.U16(1); w.U16(0); w.U32(0);
  w.U16(1); w.U16(1); w.U32(9);
  w.U16(2); w.U16(2); w.U32(0);
  // class_defs[0]：NegProvider
  w.U32(1);                   // class_idx
  w.U32(0x1);                 // access_flags public
  w.U32(0);                   // superclass_idx = ContentProvider
  w.U32(0);                   // interfaces_off
  w.U32(0xffffffffu);         // source_file_idx
  w.U32(0);                   // annotations_off
  w.U32(data_off + class_data_off);
  w.U32(0);                   // static_values_off
  // data 段
  w.buf.insert(w.buf.end(), data.buf.begin(), data.buf.end());
  return w.buf;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== lifecycle smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string target_apk =
      argc > 2 ? argv[2] : "target-app/build/outputs/apk/debug/target-app-debug.apk";

  // ---- 引导 VM（同 apk_run_smoke）----
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

  auto app_session = dexvm::session::Session::Open(target_apk, &err);
  CHECK(app_session != nullptr && !app_session->dex_blobs().empty(), "open target APK");
  if (app_session == nullptr) {
    std::printf("  error: %s\n", err.c_str());
    return 1;
  }

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

  // ---- framework 内建类面 ----
  {
    const char* kFwClasses[] = {
        "Landroid/content/Context;",
        "Landroid/content/ContextWrapper;",
        "Landroid/app/Application;",
        "Landroid/content/ContentProvider;",
        "Landroid/app/Activity;",
        "Landroid/os/Bundle;",
        "Landroid/content/Intent;",
        "Landroid/view/View;",
        "Landroid/widget/TextView;",
        "Landroid/widget/LinearLayout;",
        "Landroid/util/Log;",
        "Landroid/app/Instrumentation;",
        "Landroid/app/ActivityThread;",
    };
    bool all = true;
    for (const char* d : kFwClasses) {
      if (linker.FindClass(d) == nullptr) {
        std::printf("  missing framework class: %s\n", d);
        all = false;
      }
    }
    CHECK(all, "framework builtin classes present");
    // ActivityThread 静态面（docs 29.2 ①：mInitialApplication/mAllApplications 字段）
    VClass* at = linker.FindClass("Landroid/app/ActivityThread;");
    CHECK(at != nullptr && FindField(at, "mInitialApplication") != nullptr &&
              FindField(at, "mAllApplications") != nullptr &&
              FindField(at, "mInstrumentation") != nullptr,
          "ActivityThread mInitialApplication/mAllApplications/mInstrumentation fields");
  }

  // ---- Manifest（docs 19.2/19.3 组件来源；aapt2 已展开相对名为全名） ----
  const dexvm::session::ManifestInfo& mi = app_session->manifest();
  CHECK(mi.package_name == "com.coke.target", "manifest package_name");
  CHECK(mi.application_class == "com.coke.target.RuntimeLifecycleApp",
        "manifest application_class");
  CHECK(mi.providers.size() == 1 &&
            mi.providers[0].name == "com.coke.target.RuntimeLifecycleProvider",
        "manifest provider RuntimeLifecycleProvider");
  CHECK(mi.launcher_activity == "com.coke.target.MainActivity", "manifest launcher activity");

  LifecycleConfig cfg = ConfigFromManifest(mi);
  CHECK(cfg.application_class == "com.coke.target.RuntimeLifecycleApp",
        "expanded application class");
  CHECK(cfg.providers.size() == 1 &&
            cfg.providers[0].class_name == "com.coke.target.RuntimeLifecycleProvider",
        "expanded provider class");
  CHECK(cfg.activity_class == "com.coke.target.MainActivity", "expanded activity class");

  // ---- 正例：全生命周期 ----
  const std::string kExpectedLog =
      "attachBaseContext;provider.onCreate;onCreate;activity.onCreate;activity.onStart;"
      "activity.onResume;";
  int observed_begin = 0;
  int observed_end = 0;
  LifecycleDriver driver(state, app_loader);
  driver.set_observer([&](const std::string& phase, bool begin, const PhaseRecord* rec) {
    (void)phase;
    (void)rec;
    if (begin) observed_begin++;
    else observed_end++;
  });
  bool ran = driver.Run(cfg);
  CHECK(ran, "lifecycle run completes");
  if (!ran) std::printf("  error: %s\n", driver.error().c_str());

  // 阶段序列（docs 19.2/19.3 顺序）
  const char* kPhases[] = {
      "frameworkState",           "application.create",        "application.attachBaseContext",
      "provider[0].create",       "provider[0].attachInfo",    "provider[0].onCreate",
      "application.onCreate",     "activity.create",           "activity.attach",
      "activity.onCreate",        "activity.onStart",          "activity.onResume",
      "activity.onPostResume",
  };
  bool phases_ok = driver.phases().size() == std::size(kPhases);
  for (size_t i = 0; phases_ok && i < std::size(kPhases); i++) {
    if (driver.phases()[i].name != kPhases[i] || !driver.phases()[i].ok) {
      std::printf("  phase[%zu]: got %s (ok=%d), want %s\n", i,
                  driver.phases()[i].name.c_str(), driver.phases()[i].ok ? 1 : 0, kPhases[i]);
      phases_ok = false;
    }
  }
  CHECK(phases_ok, "phase sequence = AOSP order, all ok");
  CHECK(observed_begin == static_cast<int>(std::size(kPhases)) &&
            observed_end == static_cast<int>(std::size(kPhases)),
        "observer begin/end per phase");

  // lifecycleLog 精确断言
  VClass* app_cls = linker.FindClass("Lcom/coke/target/RuntimeLifecycleApp;");
  CHECK(app_cls != nullptr, "FindClass RuntimeLifecycleApp");
  std::string log;
  VField* log_f = app_cls != nullptr ? FindField(app_cls, "lifecycleLog") : nullptr;
  if (log_f != nullptr && log_f->is_static) {
    Value slot = app_cls->static_storage[log_f->offset];
    log = slot.ref != nullptr ? static_cast<VString*>(slot.ref)->ToModifiedUtf8() : "";
  }
  std::printf("lifecycleLog: %s\n", log.c_str());
  CHECK(log == kExpectedLog, "lifecycleLog == AOSP 顺序串");
  CHECK(CountOccurrences(log, "attachBaseContext;") == 1 &&
            CountOccurrences(log, "provider.onCreate;") == 1 &&
            CountOccurrences(log, "activity.onResume;") == 1,
        "each onXxx exactly once");

  // ActivityThread.mInitialApplication / currentApplication（docs 29.2 壳反射面）
  {
    VObject* at = driver.activity_thread();
    CHECK(at != nullptr && driver.application() != nullptr, "driver objects");
    if (at != nullptr && driver.application() != nullptr) {
      VField* f = FindField(at->klass, "mInitialApplication");
      ObjHeader* v = nullptr;
      if (f != nullptr) std::memcpy(&v, at->field_data() + f->offset, sizeof(v));
      CHECK(v == driver.application(), "ActivityThread.mInitialApplication == application");
    }
    // 静态面：currentApplication()（壳反射面，docs 29.2）
    VClass* at_cls = linker.FindClass("Landroid/app/ActivityThread;");
    VMethod* cur_app = nullptr;
    for (auto& m : at_cls->direct_methods) {
      if (m.name == "currentApplication" && m.IsStatic() && m.param_types.empty()) cur_app = &m;
    }
    Value r{};
    bool ok = cur_app != nullptr && InvokeMethod(state, cur_app, nullptr, 0, &r);
    CHECK(ok && r.ref == driver.application(), "ActivityThread.currentApplication()");
  }

  // Context 面：getPackageName / getApplicationContext（docs 19.1 绑定）
  {
    Value r{};
    bool ok = CallVirtual(state, driver.application(), "getPackageName", {}, "Ljava/lang/String;",
                          nullptr, 0, &r);
    CHECK(ok && ResultUtf8(r) == "com.coke.target", "application.getPackageName()");
    ok = CallVirtual(state, driver.activity(), "getApplicationContext", {},
                     "Landroid/content/Context;", nullptr, 0, &r);
    CHECK(ok && r.ref == driver.application(), "activity.getApplicationContext() == application");
  }

  // ---- TextView 路径（MainActivity 便携首帧树，可执行不崩 + 语义合理）----
  {
    VObject* act = driver.activity();
    CHECK(act != nullptr, "activity instance");
    if (act != nullptr) {
      VField* cv_f = FindField(act->klass, "mContentView");
      ObjHeader* cv = nullptr;
      if (cv_f != nullptr) std::memcpy(&cv, act->field_data() + cv_f->offset, sizeof(cv));
      CHECK(cv != nullptr && cv->klass != nullptr &&
                cv->klass->descriptor == "Landroid/widget/TextView;",
          "setContentView stored TextView");
      if (cv != nullptr && cv->klass != nullptr &&
          cv->klass->descriptor == "Landroid/widget/TextView;") {
        VObject* tv = reinterpret_cast<VObject*>(cv);
        VField* text_f = FindField(tv->klass, "mText");
        ObjHeader* text = nullptr;
        if (text_f != nullptr) std::memcpy(&text, tv->field_data() + text_f->offset, sizeof(text));
        std::string got = text != nullptr ? static_cast<VString*>(text)->ToModifiedUtf8() : "";
        // 期望：tv.setText("target ok\n" + lifecycleLog@onCreate)
        std::string expected =
            "target ok\nattachBaseContext;provider.onCreate;onCreate;activity.onCreate;";
        CHECK(got == expected, "TextView text == lifecycle fixture concat content");
        VField* size_f = FindField(tv->klass, "mTextSize");
        float ts = size_f != nullptr ? ReadFField(tv, size_f->offset) : -1;
        CHECK(ts == 16.0f, "TextView textSize == 16");
        VField* pl = FindField(tv->klass, "mPaddingLeft");
        VField* pt = FindField(tv->klass, "mPaddingTop");
        VField* pr = FindField(tv->klass, "mPaddingRight");
        VField* pb = FindField(tv->klass, "mPaddingBottom");
        CHECK(pl != nullptr && ReadIField(tv, pl->offset) == 48 &&
                  ReadIField(tv, pt->offset) == 48 && ReadIField(tv, pr->offset) == 48 &&
                  ReadIField(tv, pb->offset) == 48,
              "TextView padding == 48x4");
      }
    }
  }

  // ---- 负例：Provider.onCreate 抛异常 -> S5 失败停链（docs 11 章）----
  {
    std::vector<uint8_t> neg_bytes = BuildNegProviderDex();
    std::string derr;
    auto neg_dex =
        dexvm::dex::DexFile::Open(neg_bytes.data(), neg_bytes.size(), &derr);
    CHECK(neg_dex != nullptr, "open hand-written NegProvider dex");
    if (neg_dex == nullptr) {
      std::printf("  error: %s\n", derr.c_str());
    } else {
      linker.AddAppDexFile(neg_dex.get());
      owned_dex.push_back(std::move(neg_dex));

      // 重置 lifecycleLog（前一轮已写满）
      if (log_f != nullptr && log_f->is_static) {
        app_cls->static_storage[log_f->offset].ref =
            reinterpret_cast<ObjHeader*>(linker.AllocString(u""));
      }

      LifecycleConfig neg_cfg;
      neg_cfg.package_name = "com.coke.target";
      neg_cfg.application_class = "com.coke.target.RuntimeLifecycleApp";
      // initOrder 降序：NegProvider(5) 先于 RuntimeLifecycleProvider(0)——若顺序反了，
      // RuntimeLifecycleProvider.onCreate 会先把 "provider.onCreate;" 写进 log。
      neg_cfg.providers.push_back({"com.coke.target.RuntimeLifecycleProvider", 0});
      neg_cfg.providers.push_back({"com.dexvm.neg.NegProvider", 5});
      neg_cfg.activity_class = "com.coke.target.MainActivity";

      LifecycleDriver neg_driver(state, app_loader);
      bool neg_ran = neg_driver.Run(neg_cfg);
      CHECK(!neg_ran, "negative run fails-stop");
      const auto& phases = neg_driver.phases();
      CHECK(phases.size() == 6, "chain stopped at provider[0].onCreate (6 phases)");
      bool stop_ok = phases.size() == 6 && phases.back().name == "provider[0].onCreate" &&
                     !phases.back().ok;
      CHECK(stop_ok, "failed phase == provider[0].onCreate");
      CHECK(neg_driver.error().find("neg provider boom") != std::string::npos &&
                neg_driver.error().find("RuntimeException") != std::string::npos,
            "diagnostic contains guest exception class+message");
      std::printf("negative error: %s\n", neg_driver.error().c_str());

      // S5：异常前已跑 attachBaseContext；Application.onCreate/Activity 未执行
      std::string neg_log;
      if (log_f != nullptr && log_f->is_static) {
        Value slot = app_cls->static_storage[log_f->offset];
        neg_log = slot.ref != nullptr ? static_cast<VString*>(slot.ref)->ToModifiedUtf8() : "";
      }
      std::printf("negative lifecycleLog: %s\n", neg_log.c_str());
      CHECK(neg_log == "attachBaseContext;",
            "fail-stop: only attachBaseContext ran (descending initOrder)");
      CHECK(neg_driver.activity() == nullptr && neg_driver.application() != nullptr,
            "application created, activity never reached");
    }
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
