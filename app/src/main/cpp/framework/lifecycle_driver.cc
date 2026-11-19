// LifecycleDriver：AOSP 可观察顺序的生命周期链（docs 10 章 19.2/19.3、
// 11 章 S5/S6）。语义基准 AOSP ActivityThread.handleBindApplication /
// handleLaunchActivity 公开可观察顺序：
//   create Application -> attachBaseContext -> installContentProviders
//   （descending initOrder；逐个 attachInfo -> onCreate）-> Application.onCreate
//   -> launcher Activity -> attach -> onCreate -> onStart -> onResume -> onPostResume。
// 失败即停：Guest 异常记录诊断后终止（S5：不可重试），后续阶段不再执行。
#include "framework/lifecycle_driver.h"

#include <algorithm>
#include <chrono>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "session/class_loader.h"

namespace dexvm::framework {

namespace {

using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

// AOSP PackageParser 相对名展开：'.X' -> pkg.X；含 '.' 原样；否则 pkg.X。
std::string ExpandName(const std::string& pkg, const std::string& name) {
  if (name.empty()) return name;
  if (name[0] == '.') return pkg + name;
  if (name.find('.') != std::string::npos) return name;
  return pkg + "." + name;
}

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

void SetRefField(VObject* o, uint32_t off, ObjHeader* v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

// UTF-8 -> UTF-16（manifest 值含非 ASCII 时字符串面保真；非法序列 U+FFFD）
std::u16string Utf8ToUtf16(const std::string& s) {
  std::u16string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = 0;
    size_t len = 1;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1Fu;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0Fu;
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07u;
      len = 4;
    } else {
      out.push_back(0xFFFD);
      i++;
      continue;
    }
    if (i + len > s.size()) {
      out.push_back(0xFFFD);
      break;
    }
    bool bad = false;
    for (size_t k = 1; k < len; k++) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) {
        bad = true;
        break;
      }
      cp = (cp << 6) | (cc & 0x3Fu);
    }
    if (bad) {
      out.push_back(0xFFFD);
      i++;
      continue;
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
    i += len;
  }
  return out;
}

VString* AllocUtf8String(VMState& state, const std::string& s) {
  return state.linker->AllocString(Utf8ToUtf16(s));
}

// guest 虚调用（投影装配用；失败返回 false，pending exception 留给调用方处置）
bool CallGuest(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
               const char* ret, const Value* args, size_t n) {
  if (obj == nullptr || obj->klass == nullptr) return false;
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return false;
  std::vector<Value> full;
  full.reserve(n + 1);
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  return dexvm::runtime::InvokeMethod(state, m, full.data(), full.size(), &r);
}

}  // namespace

LifecycleConfig ConfigFromManifest(const session::ManifestInfo& manifest) {
  LifecycleConfig cfg;
  cfg.package_name = manifest.package_name;
  cfg.application_class =
      ExpandName(manifest.package_name, manifest.application_class);
  if (cfg.application_class.empty()) cfg.application_class = "android.app.Application";
  for (const auto& p : manifest.providers) {
    ProviderSpec spec;
    spec.class_name = ExpandName(manifest.package_name, p.name);
    spec.init_order = p.init_order;
    spec.entry = p;  // manifest 值投影源（ProviderInfo 装配）
    cfg.providers.push_back(std::move(spec));
  }
  cfg.activity_class = ExpandName(manifest.package_name, manifest.launcher_activity);
  // install Providers by descending initOrder（docs 19.2）
  std::stable_sort(cfg.providers.begin(), cfg.providers.end(),
                   [](const ProviderSpec& a, const ProviderSpec& b) {
                     return a.init_order > b.init_order;
                   });
  return cfg;
}

dexvm::runtime::VObject* AssembleMetaBundle(dexvm::runtime::VMState& state,
                                           const std::vector<session::MetaEntry>& metas) {
  if (metas.empty()) return nullptr;  // AOSP：无 <meta-data> = metaData null
  VClass* b = state.linker->FindClass("Landroid/os/Bundle;");
  if (b == nullptr) return nullptr;
  VObject* bundle = state.linker->AllocObject(b);
  if (bundle == nullptr) return nullptr;
  if (!CallGuest(state, bundle, "<init>", {}, "V", nullptr, 0)) {
    state.ClearException();
    return nullptr;
  }
  for (const auto& m : metas) {
    Value args[2] = {};
    VString* key = AllocUtf8String(state, m.name);
    if (key == nullptr) return nullptr;
    args[0].ref = reinterpret_cast<ObjHeader*>(key);
    bool ok = false;
    switch (m.type) {
      case session::MetaEntry::Type::kString: {
        VString* v = AllocUtf8String(state, m.str);
        if (v == nullptr) return nullptr;
        args[1].ref = reinterpret_cast<ObjHeader*>(v);
        ok = CallGuest(state, bundle, "putString", {"Ljava/lang/String;", "Ljava/lang/String;"},
                       "V", args, 2);
        break;
      }
      case session::MetaEntry::Type::kInt:
      case session::MetaEntry::Type::kResource:  // AOSP parseMetaData：resource -> putInt(resId)
        args[1].l = m.num;
        ok = CallGuest(state, bundle, "putInt", {"Ljava/lang/String;", "I"}, "V", args, 2);
        break;
      case session::MetaEntry::Type::kBool:
        args[1].l = m.num != 0 ? 1 : 0;
        ok = CallGuest(state, bundle, "putBoolean", {"Ljava/lang/String;", "Z"}, "V", args, 2);
        break;
      case session::MetaEntry::Type::kFloat: {
        uint32_t bits = 0;
        std::memcpy(&bits, &m.f, sizeof(bits));
        args[1].l = bits;
        ok = CallGuest(state, bundle, "putFloat", {"Ljava/lang/String;", "F"}, "V", args, 2);
        break;
      }
    }
    if (!ok) {
      state.ClearException();
      return nullptr;
    }
  }
  return bundle;
}

void ProjectPackageInfo(dexvm::runtime::VMState& state, dexvm::runtime::VObject* package_info,
                        const session::ManifestInfo& manifest) {
  if (package_info == nullptr || package_info->klass == nullptr) return;
  VField* vc = FindInstanceField(package_info->klass, "versionCode");
  if (vc != nullptr) {
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(manifest.version_code));
    std::memcpy(package_info->field_data() + vc->offset, &slot, sizeof(slot));
  }
  VField* vn = FindInstanceField(package_info->klass, "versionName");
  if (vn != nullptr && !manifest.version_name.empty()) {
    VString* s = AllocUtf8String(state, manifest.version_name);
    if (s != nullptr) SetRefField(package_info, vn->offset, reinterpret_cast<ObjHeader*>(s));
  }
}

LifecycleDriver::LifecycleDriver(dexvm::runtime::VMState& state,
                                 dexvm::runtime::VObject* app_class_loader)
    : state_(state), app_loader_(app_class_loader) {}

std::string LifecycleDriver::TakeExceptionDiagnosis() {
  if (!state_.HasPendingException()) return "no pending exception";
  ObjHeader* ex = state_.pending_exception;
  std::string desc = ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor
                                                          : "Ljava/lang/Throwable;";
  std::string msg;
  if (ex != nullptr && ex->klass != nullptr) {
    VObject* obj = reinterpret_cast<VObject*>(ex);
    for (VClass* k = obj->klass; k != nullptr && msg.empty(); k = k->super_class) {
      for (const auto& f : k->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* s = GetRefField(obj, f.offset);
          if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
          break;
        }
      }
    }
  }
  // 抛点栈（诊断归因：缺口在哪个 guest 方法）
  std::string trace;
  for (size_t i = 0; i < state_.last_throw_backtrace.size() && i < 8; i++) {
    trace += " @ " + state_.last_throw_backtrace[i];
  }
  state_.ClearException();
  return desc + ": " + msg + trace;
}

bool LifecycleDriver::Fail(const std::string& reason) {
  error_ = reason;
  ok_ = false;
  return false;
}

bool LifecycleDriver::CallVirtualOn(VObject* obj, const char* name,
                                    std::vector<std::string> params, const char* ret,
                                    const Value* args, size_t n, Value* out) {
  if (obj == nullptr || obj->klass == nullptr) {
    return Fail(std::string(name) + ": receiver == null");
  }
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    return Fail(std::string(name) + ": no such method on " + obj->klass->descriptor);
  }
  std::vector<Value> full;
  full.reserve(n + 1);
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!dexvm::runtime::InvokeMethod(state_, m, full.data(), full.size(), &r)) {
    return Fail("guest exception " + TakeExceptionDiagnosis());
  }
  if (out != nullptr) *out = r;
  return true;
}

// 实例化：FindClass -> InitializeClass -> AllocObject -> <init>()V
bool LifecycleDriver::Instantiate(const std::string& class_name, VObject** out) {
  std::string desc = session::DotToDescriptor(class_name);
  dexvm::runtime::VClass* klass =
      state_.linker->FindClass(&state_, app_loader_, desc);
  if (klass == nullptr) {
    std::string reason = "instantiate " + class_name + ": class not found: " + desc;
    if (state_.HasPendingException()) reason += " (" + TakeExceptionDiagnosis() + ")";
    return Fail(reason);
  }
  if (!dexvm::runtime::CheckClassDefinition(state_, klass)) {
    std::string reason = "instantiate " + class_name + ": " + TakeExceptionDiagnosis();
    return Fail(reason);
  }
  if (!state_.linker->InitializeClass(klass, &state_)) {
    std::string reason = "instantiate " + class_name + ": class init failed: " + desc;
    if (state_.HasPendingException()) reason += " (" + TakeExceptionDiagnosis() + ")";
    return Fail(reason);
  }
  VObject* obj = state_.linker->AllocObject(klass);
  if (obj == nullptr) {
    return Fail("instantiate " + class_name + ": alloc failed (OOM)");
  }
  VMethod probe;
  probe.name = "<init>";
  probe.return_type = "V";
  VMethod* ctor = klass->FindInterfaceImpl(&probe);
  if (ctor != nullptr) {
    Value self_v{};
    self_v.ref = obj;
    Value r{};
    if (!dexvm::runtime::InvokeMethod(state_, ctor, &self_v, 1, &r)) {
      return Fail("instantiate " + class_name + ": ctor threw " + TakeExceptionDiagnosis());
    }
  }
  *out = obj;
  return true;
}

// AOSP ProviderInfo 装配：manifest ProviderEntry -> 真实实例（字段面 =
// framework_builtins ProviderInfo；值语义 = AOSP ProviderInfo/PackageItemInfo/
// ComponentInfo 公开字段）。authority 保留 ';'-多值原串（AOSP ContentProvider
// setAuthorities 内拆分口径的源串）；name = 相对名展开（AOSP PackageItemInfo）。
// 供 BuildProviderInfo（attachInfo 链）与 resolveContentProvider 注册表共用。
dexvm::runtime::VObject* AssembleProviderInfo(dexvm::runtime::VMState& state,
                                              const std::string& package_name,
                                              const session::ProviderEntry& entry,
                                              dexvm::runtime::VObject** out) {
  VClass* klass = state.linker->FindClass("Landroid/content/pm/ProviderInfo;");
  if (klass == nullptr) return nullptr;
  VObject* info = state.linker->AllocObject(klass);
  if (info == nullptr) return nullptr;
  const session::ProviderEntry& e = entry;
  auto set_str = [&](const char* field, const std::string& v) {
    if (v.empty()) return;  // AOSP：未声明属性保持 null
    VField* f = FindInstanceField(klass, field);
    if (f == nullptr) return;
    VString* s = AllocUtf8String(state, v);
    if (s != nullptr) SetRefField(info, f->offset, reinterpret_cast<ObjHeader*>(s));
  };
  auto set_bool = [&](const char* field, bool v) {
    VField* f = FindInstanceField(klass, field);
    if (f == nullptr) return;
    uint64_t slot = v ? 1 : 0;
    std::memcpy(info->field_data() + f->offset, &slot, sizeof(slot));
  };
  auto set_int = [&](const char* field, int32_t v) {
    VField* f = FindInstanceField(klass, field);
    if (f == nullptr) return;
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
    std::memcpy(info->field_data() + f->offset, &slot, sizeof(slot));
  };
  set_str("name", ExpandName(package_name, e.name));  // 相对名展开（AOSP 同口径）
  set_str("packageName", package_name);
  set_str("authority", e.authorities);
  // processName：AOSP buildProcessName——空 = 包名；':' 前缀 = 私有进程 pkg+name；
  // 其余（含全局进程名）原样。
  std::string proc = e.process_name;
  if (proc.empty()) {
    proc = package_name;
  } else if (proc[0] == ':') {
    proc = package_name + proc;
  }
  set_str("processName", proc);
  set_str("readPermission", e.read_permission);
  set_str("writePermission", e.write_permission);
  set_bool("exported", e.exported);
  set_bool("enabled", e.enabled);
  set_bool("multiprocess", e.multiprocess);
  // grantUriPermissions（0x0101001b 值投影）：FileProvider.attachInfo 消费面。
  // pathPermissions 后置：<path-permission> 元组投影待需求出现再补（字段面已留）。
  set_bool("grantUriPermissions", e.grant_uri_permissions);
  set_int("initOrder", e.init_order);
  // applicationInfo = Context 服务桥单例（AOSP ComponentInfo.applicationInfo 同源）
  VField* ai_f = FindInstanceField(klass, "applicationInfo");
  if (ai_f != nullptr) {
    SetRefField(info, ai_f->offset, dexvm::runtime::GetContextApplicationInfo());
  }
  // metaData：<meta-data> 投影（AOSP：无 <meta-data> = null）
  if (!e.meta_data.empty()) {
    VObject* bundle = AssembleMetaBundle(state, e.meta_data);
    if (bundle == nullptr) {
      return nullptr;  // 装配失败（Bundle 不可得/put 抛）；调用方诊断
    }
    VField* md_f = FindInstanceField(klass, "metaData");
    if (md_f != nullptr) SetRefField(info, md_f->offset, bundle);
  }
  *out = info;
  return info;
}

// Resources 面字节缝挂载（session -> framework_builtins；幂等）
void InstallResourceSeam() {
  dexvm::runtime::SetResourceSeam(dexvm::runtime::ResourceSeam{
      &session::ReadResourceFile,
      &session::LookupResourceId,
      &session::ParseResourceXml,
      &session::LookupResourceString,
      &session::LookupResourceStringArray,
      &session::LookupResourceValue,
      &session::LookupResourceStyle,
      &session::LookupResourceName,
      &session::ReadAssetFile,
  });
}

void LifecycleDriver::RecordProviderRegistration(const std::string& line) {
  provider_registration_.push_back(line);
  if (diagnostic_sink_) diagnostic_sink_(line);
}

// authority 注册（';' 多值逐键 + 原串；预装配与 attach 时共用——AOSP PMS
// 全表可查语义：attachInfo 期间任意 authority 的 resolveContentProvider 命中）。
static void RegisterProviderAuthorities(VMState& state, const std::string& authorities,
                                        VObject* info) {
  if (authorities.empty() || info == nullptr) return;
  size_t start = 0;
  do {
    size_t semi = authorities.find(';', start);
    std::string tok =
        authorities.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
    if (!tok.empty()) dexvm::runtime::RegisterProviderInfo(state, tok, info);
    start = semi == std::string::npos ? authorities.size() : semi + 1;
  } while (start < authorities.size());
  dexvm::runtime::RegisterProviderInfo(state, authorities, info);
}

// attachInfo 链装配入口（诊断收口；实例 = AssembleProviderInfo 同型）
bool LifecycleDriver::BuildProviderInfo(const LifecycleConfig& config, const ProviderSpec& spec,
                                        VObject** out) {
  VObject* info = AssembleProviderInfo(state_, config.package_name, spec.entry, out);
  if (info == nullptr) {
    RecordProviderRegistration("attach-reg " + spec.class_name + " FAILED (assembly)");
    return Fail(state_.HasPendingException() ? "ProviderInfo assembly: " + TakeExceptionDiagnosis()
                                            : "ProviderInfo assembly failed (OOM or metaData put)");
  }
  // 兜底注册（预装配若有遗漏，本次 attach 前自注册 -> 自身 resolveContentProvider
  // 命中；FileProvider.getPathStrategy 消费面；AOSP 语义 PMS 先于 attach 全知）。
  RegisterProviderAuthorities(state_, spec.entry.authorities, info);
  RecordProviderRegistration("attach-reg " + spec.entry.authorities + " ok");
  return true;
}

bool LifecycleDriver::Run(const LifecycleConfig& config) {
  phases_.clear();
  error_.clear();
  ok_ = true;
  dexvm::runtime::ClearPendingActivityLaunch();  // 起点清（跨轮残留）

  // Resources 面字节缝（session arsc/zip/AXML 解析；幂等自挂——设备/宿主测试
  // 零额外接线，未跑 Run 的宿主面保持"查无"语义不变）
  InstallResourceSeam();

  // manifest ProviderEntry -> ProviderInfo 全量预装配注册（AOSP PMS 全表可查
  // 语义：任一 provider attachInfo 期间可 resolveContentProvider 任意 authority
  // ——FileProvider.parsePathStrategy 消费面）。authority ';' 多值逐键注册（AOSP
  // 每 authority 一条记录同口径）。装配失败 = 该键查无（后续 loadXmlMetaData
  // 缺口可见），不阻塞生命周期链。
  provider_registration_.clear();
  for (const ProviderSpec& p : config.providers) {
    VObject* info = nullptr;
    if (AssembleProviderInfo(state_, config.package_name, p.entry, &info) == nullptr) {
      state_.ClearException();
      RecordProviderRegistration("pre-reg " + p.class_name + " FAILED (assembly)");
      continue;
    }
    RegisterProviderAuthorities(state_, p.entry.authorities, info);
    RecordProviderRegistration("pre-reg " + p.class_name + " auth=" + p.entry.authorities + " ok");
  }

  // 每步观测 + 失败即停（docs 11 章 S5：Guest 异常记录后终止启动）
  auto phase = [this](const std::string& name, auto&& fn) -> bool {
    PhaseRecord rec;
    rec.name = name;
    if (observer_) observer_(name, true, nullptr);
    auto t0 = std::chrono::steady_clock::now();
    std::string reason;
    rec.ok = fn(&reason);
    rec.elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - t0)
                         .count();
    rec.error = reason;
    phases_.push_back(rec);
    if (observer_) observer_(name, false, &phases_.back());
    if (!rec.ok) {
      error_ = name + ": " + reason;
      ok_ = false;
      return false;
    }
    // M4.3 消息泵（docs 14.10）：阶段间只派发**已到期**消息（realtime 语义，
    // 与 session tick 一致）。旧实现用虚拟时间推进 postDelayed，启动期会把
    // 应用的延迟初始化/网络重试任务提前连跑，activity.launch 被拖到 90s+；
    // AOSP 主线程不会快进延迟消息，这里按真实时间出队，启动阶段只推进
    // 立即任务，延迟任务留给首帧后的 session tick。
    // dispatch 抛 guest 异常：记录 messagePump 缺口但**不中止**——真机语义是
    // 未捕获异常崩溃进程，本 VM 里这类异常多来自长尾 API 缺口，不应阻断与
    // 首个画面无关的主流程。
    std::string pump_err;
    auto pump_t0 = std::chrono::steady_clock::now();
    if (!dexvm::runtime::PumpLooperQueuesRealtime(state_, &pump_err, nullptr)) {
      PhaseRecord prec;
      prec.name = "messagePump";
      prec.ok = false;
      prec.error = pump_err;
      if (observer_) observer_("messagePump", true, nullptr);
      phases_.push_back(prec);
      if (observer_) observer_("messagePump", false, &phases_.back());
      if (diagnostic_sink_) diagnostic_sink_("messagePump tolerated: " + pump_err);
    }
    auto pump_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - pump_t0)
                       .count();
    if (pump_ms >= 100 && diagnostic_sink_) {
      diagnostic_sink_("pump after " + name + " took " + std::to_string(pump_ms) + " ms");
    }
    return true;
  };

  // providers：descending initOrder（Run 内再次排序，语义归驱动器）
  std::vector<ProviderSpec> providers = config.providers;
  std::stable_sort(providers.begin(), providers.end(),
                   [](const ProviderSpec& a, const ProviderSpec& b) {
                     return a.init_order > b.init_order;
                   });

  // ---- S4 最小 Framework state（docs 19.1）----
  if (!phase("frameworkState", [&](std::string* reason) {
        using dexvm::runtime::ClassLinker;
        ClassLinker* linker = state_.linker;
        auto* thread_cls = linker->FindClass("Landroid/app/ActivityThread;");
        auto* instr_cls = linker->FindClass("Landroid/app/Instrumentation;");
        auto* ctxw_cls = linker->FindClass("Landroid/content/ContextWrapper;");
        if (thread_cls == nullptr || instr_cls == nullptr || ctxw_cls == nullptr) {
          *reason = "framework builtins missing (RegisterFrameworkBuiltins?)";
          return false;
        }
        if (!Instantiate("android.app.ActivityThread", &activity_thread_)) {
          *reason = error_;
          return false;
        }
        if (!Instantiate("android.app.Instrumentation", &instrumentation_)) {
          *reason = error_;
          return false;
        }
        if (!Instantiate("android.content.ContextWrapper", &base_context_)) {
          *reason = error_;
          return false;
        }
        // Context 绑定：包名（docs 29.2 ①：虚拟身份类加载前就绪）
        VField* pkg_f = FindInstanceField(base_context_->klass, "mPackageName");
        if (pkg_f != nullptr && !config.package_name.empty()) {
          VString* pkg =
              state_.linker->AllocString(std::u16string(config.package_name.begin(),
                                                        config.package_name.end()));
          SetRefField(base_context_, pkg_f->offset, reinterpret_cast<ObjHeader*>(pkg));
        }
        // ActivityThread 状态：sCurrentActivityThread / mInstrumentation
        if (!thread_cls->static_fields.empty()) {
          thread_cls->static_storage[0].ref = activity_thread_;
        }
        VField* instr_f = FindInstanceField(activity_thread_->klass, "mInstrumentation");
        if (instr_f != nullptr) SetRefField(activity_thread_, instr_f->offset, instrumentation_);
        return true;
      })) {
    return false;
  }

  // ---- S5：Application ----
  if (!phase("application.create", [&](std::string* reason) {
        if (!Instantiate(config.application_class, &application_)) {
          *reason = error_;
          return false;
        }
        VField* app_f = FindInstanceField(activity_thread_->klass, "mInitialApplication");
        if (app_f != nullptr) SetRefField(activity_thread_, app_f->offset, application_);
        return true;
      })) {
    return false;
  }

  if (!phase("application.attachBaseContext", [&](std::string* reason) {
        // getApplicationContext 语义：base 上的 app context = Application（AOSP ContextImpl）
        VField* app_ctx_f = FindInstanceField(base_context_->klass, "mApplicationContext");
        if (app_ctx_f != nullptr) SetRefField(base_context_, app_ctx_f->offset, application_);
        Value ctx{};
        ctx.ref = base_context_;
        if (!CallVirtualOn(application_, "attachBaseContext", {"Landroid/content/Context;"}, "V",
                           &ctx, 1, nullptr)) {
          *reason = error_;
          return false;
        }
        return true;
      })) {
    return false;
  }

  // ---- install Providers（docs 19.2：descending initOrder，逐个 attachInfo+onCreate）----
  std::vector<VObject*> provider_objs;
  provider_objs.reserve(providers.size());
  for (size_t i = 0; i < providers.size(); i++) {
    const std::string tag = "provider[" + std::to_string(i) + "]";
    VObject* pobj = nullptr;
    if (!phase(tag + ".create", [&](std::string* reason) {
          if (!Instantiate(providers[i].class_name, &pobj)) {
            *reason = error_;
            return false;
          }
          return true;
        })) {
      return false;
    }
    provider_objs.push_back(pobj);
    if (!phase(tag + ".attachInfo", [&](std::string* reason) {
          // AOSP installProvider：attachInfo(context, ProviderInfo) 携真实
          // ProviderInfo（manifest 值投影装配，attachInfo 覆写读字段不空指针）；
          // context = Application（AOSP ActivityThread.installContentProviders(app, ...)
          // -> installProvider 经 ContextImpl.getOuterContext 取 Application 实例——
          // provider 侧 (Application) getContext() 强转语义成立）
          VObject* info = nullptr;
          if (!BuildProviderInfo(config, providers[i], &info)) {
            *reason = error_;
            return false;
          }
          Value args[2] = {};
          args[0].ref = application_;
          args[1].ref = info;
          if (!CallVirtualOn(pobj, "attachInfo",
                             {"Landroid/content/Context;", "Landroid/content/pm/ProviderInfo;"},
                             "V", args, 2, nullptr)) {
            *reason = error_;
            return false;
          }
          return true;
        })) {
      return false;
    }
    if (!phase(tag + ".onCreate", [&](std::string* reason) {
          // AOSP ContentProvider.attachInfo 内调 onCreate，返回值忽略；异常上抛（S5）
          if (!CallVirtualOn(pobj, "onCreate", {}, "Z", nullptr, 0, nullptr)) {
            *reason = error_;
            return false;
          }
          return true;
        })) {
      return false;
    }
  }

  if (!phase("application.onCreate", [&](std::string* reason) {
        if (!CallVirtualOn(application_, "onCreate", {}, "V", nullptr, 0, nullptr)) {
          *reason = error_;
          return false;
        }
        return true;
      })) {
    return false;
  }

  // ---- S6：launcher Activity（docs 19.3）----
  if (!config.activity_class.empty()) {
    if (!phase("activity.create", [&](std::string* reason) {
          if (!Instantiate(config.activity_class, &activity_)) {
            *reason = error_;
            return false;
          }
          return true;
        })) {
      return false;
    }
    if (!phase("activity.attach", [&](std::string* reason) {
          Value args[4] = {};
          args[0].ref = base_context_;
          args[1].ref = activity_thread_;
          args[2].ref = instrumentation_;
          args[3].ref = nullptr;  // Intent 骨架
          if (!CallVirtualOn(activity_, "attach",
                             {"Landroid/content/Context;", "Landroid/app/ActivityThread;",
                              "Landroid/app/Instrumentation;", "Landroid/content/Intent;"},
                             "V", args, 4, nullptr)) {
            *reason = error_;
            return false;
          }
          return true;
        })) {
      return false;
    }
    struct Step {
      const char* name;
      const char* method;
      bool has_bundle;
    };
    // AOSP Activity 生命周期顺序：onCreate -> onStart -> onPostCreate ->
    // onResume -> onPostResume。onPostCreate 是 app 挂"一次性的建立动作"的
    // 标准点（知乎 SetupTabsOnCreate.onPostCreate 即在此建首页 Tab 容器）。
    const Step kSteps[] = {
        {"activity.onCreate", "onCreate", true},
        {"activity.onStart", "onStart", false},
        {"activity.onPostCreate", "onPostCreate", true},
        {"activity.onResume", "onResume", false},
        {"activity.onPostResume", "onPostResume", false},
    };
    for (const Step& s : kSteps) {
      if (!phase(s.name, [&](std::string* reason) {
            Value args[1] = {};  // savedInstanceState = null
            const Value* ap = nullptr;
            size_t an = 0;
            std::vector<std::string> params;
            if (s.has_bundle) {
              params.push_back("Landroid/os/Bundle;");
              ap = args;
              an = 1;
            }
            if (!CallVirtualOn(activity_, s.method, std::move(params), "V", ap, an, nullptr)) {
              *reason = error_;
              return false;
            }
            return true;
          })) {
        return false;
      }
    }

    // ---- startActivity 请求排空（AOSP AMS 调度等价面）----
    // 当前 Activity 到 onPostResume 后，按 guest 请求逐个驱动目标 Activity
    // （AOSP 顺序：旧 onPause/onStop -> 新 attach -> onCreate/onStart/onResume/
    // onPostResume）。首帧以最终前台 Activity 的内容树出画。
    for (int guard = 0; guard < 8; guard++) {
      std::string launch_pkg;
      std::string launch_cls;
      ObjHeader* launch_intent = nullptr;
      if (!dexvm::runtime::TakePendingActivityLaunch(&launch_pkg, &launch_cls,
                                                     &launch_intent)) {
        break;
      }
      VObject* next_activity = nullptr;
      const std::string tag = "activity.launch[" +
                              dexvm::session::DescriptorToDot(launch_cls) + "]";
      if (!phase(tag, [&](std::string* reason) {
            auto step_t = std::chrono::steady_clock::now();
            auto mark = [&](const char* step) {
              auto now = std::chrono::steady_clock::now();
              auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - step_t)
                            .count();
#if defined(__ANDROID__)
              __android_log_print(ANDROID_LOG_INFO, "DexVmProgress",
                                  "launch step %s %lld ms", step,
                                  static_cast<long long>(ms));
#endif
              step_t = now;
            };
            // 旧 Activity 退前台（AOSP onPause -> onStop）
            if (activity_ != nullptr) {
              CallVirtualOn(activity_, "onPause", {}, "V", nullptr, 0, nullptr);
              CallVirtualOn(activity_, "onStop", {}, "V", nullptr, 0, nullptr);
            }
            mark("old.pause.stop");
            if (!Instantiate(launch_cls, &next_activity)) {
              *reason = error_;
              return false;
            }
            mark("instantiate");
            VField* intent_f = FindInstanceField(next_activity->klass, "mIntent");
            if (intent_f != nullptr) {
              SetRefField(next_activity, intent_f->offset, launch_intent);
            }
            Value attach_args[4] = {};
            attach_args[0].ref = base_context_;
            attach_args[1].ref = activity_thread_;
            attach_args[2].ref = instrumentation_;
            attach_args[3].ref = launch_intent;
            if (!CallVirtualOn(next_activity, "attach",
                               {"Landroid/content/Context;", "Landroid/app/ActivityThread;",
                                "Landroid/app/Instrumentation;", "Landroid/content/Intent;"},
                               "V", attach_args, 4, nullptr)) {
              *reason = error_;
              return false;
            }
            mark("attach");
            Value bundle_arg[1] = {};
            if (!CallVirtualOn(next_activity, "onCreate", {"Landroid/os/Bundle;"}, "V",
                               bundle_arg, 1, nullptr)) {
              *reason = error_;
              return false;
            }
            mark("onCreate");
            Value post_create_arg[1] = {};
            if (!CallVirtualOn(next_activity, "onStart", {}, "V", nullptr, 0, nullptr)) {
              *reason = error_;
              return false;
            }
            mark("onStart");
            if (!CallVirtualOn(next_activity, "onPostCreate", {"Landroid/os/Bundle;"}, "V",
                               post_create_arg, 1, nullptr)) {
              *reason = error_;
              return false;
            }
            mark("onPostCreate");
            if (!CallVirtualOn(next_activity, "onResume", {}, "V", nullptr, 0, nullptr)) {
              *reason = error_;
              return false;
            }
            mark("onResume");
            if (!CallVirtualOn(next_activity, "onPostResume", {}, "V", nullptr, 0, nullptr)) {
              *reason = error_;
              return false;
            }
            mark("onPostResume");
            return true;
          })) {
        return false;
      }
      activity_ = next_activity;
    }
  }

  // ---- M4.2：首帧管线（docs 10 章 19.6：addView→traversal→submit→present
  // →mark first frame）。仅在钩子存在且 Activity 已 RESUMED（onPostResume
  // 已跑完）时执行；未设钩子时 phase 序列保持 M4.1 的 13 项不变。----
  if (first_frame_hook_ && activity_ != nullptr) {
    PhaseRunner runner = [this, &phase](const std::string& name,
                                        const std::function<bool(std::string*)>& fn) {
      return phase(name, fn);
    };
    std::string reason;
    if (!first_frame_hook_(activity_, runner, &reason)) {
      // run_phase 内失败已带 "phase 名: 原因" 落 error_；钩子级失败补前缀。
      if (error_.empty()) error_ = reason.empty() ? "firstFrame: failed" : "firstFrame: " + reason;
      ok_ = false;
      return false;
    }
  }

  return true;
}

}  // namespace dexvm::framework
