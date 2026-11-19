// VM 侧 Framework 生命周期驱动器（M4 阶段 1，docs 10 章 19.2/19.3、
// 11 章 S5/S6）：按 AOSP 可观察顺序驱动
//   Application 实例化 -> attachBaseContext -> install Providers（desc initOrder，
//   逐个 attachInfo+onCreate）-> Application.onCreate -> launcher Activity 实例化
//   -> attach -> onCreate -> onStart -> onResume -> onPostResume。
// 每步可插观测（phase 名/耗时），失败即停并带原因（S5：Guest 异常记录后终止）。
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "core/interpreter.h"
#include "session/session.h"

namespace dexvm::framework {

// 一个 manifest 组件（provider 等）
struct ProviderSpec {
  std::string class_name;  // dot 名或 descriptor
  int32_t init_order = 0;
  session::ProviderEntry entry;  // manifest 值投影源（缺省 = 无 manifest 条目）
};

struct LifecycleConfig {
  std::string package_name;
  std::string application_class;  // 空 = android.app.Application（AOSP 默认）
  std::vector<ProviderSpec> providers;
  std::string activity_class;  // 空 = 跳过 Activity 阶段
};

// 从 ManifestInfo 生成配置（相对名按包名展开；providers 按 initOrder 降序）。
LifecycleConfig ConfigFromManifest(const session::ManifestInfo& manifest);

// ---- manifest 值投影装配（AOSP PackageParser/PackageItemInfo 同型语义）----
// <meta-data> 投影 -> android.os.Bundle（typed value 分型 put：string/int/bool/
// float/resource->int；AOSP parseMetaData 同口径）。空清单 / 无 Bundle 类 /
// 构造失败 -> nullptr（AOSP：无 <meta-data> = metaData null）。
dexvm::runtime::VObject* AssembleMetaBundle(dexvm::runtime::VMState& state,
                                           const std::vector<session::MetaEntry>& metas);

// PackageInfo.versionCode/versionName = manifest 真值（字段按名定位；
// semface（jni/runtime_contract.cpp）与宿主测试共用同一投影）。
void ProjectPackageInfo(dexvm::runtime::VMState& state, dexvm::runtime::VObject* package_info,
                        const session::ManifestInfo& manifest);

// manifest ProviderEntry -> ProviderInfo 实例（AOSP ProviderInfo 公开字段面全
// 装配：相对名已展开的 name 由调用方给；authority 原串保留；applicationInfo =
// Context 服务桥单例；metaData = AssembleMetaBundle）。BuildProviderInfo 与
// resolveContentProvider 注册表（RegisterProviderInfo）共用本装配。
dexvm::runtime::VObject* AssembleProviderInfo(dexvm::runtime::VMState& state,
                                              const std::string& package_name,
                                              const session::ProviderEntry& entry,
                                              dexvm::runtime::VObject** out);

// Resources 面字节缝挂载（session arsc/zip/AXML -> framework_builtins 钩子；
// 幂等；LifecycleDriver::Run 入口自挂，设备/宿主测试零额外接线）。
void InstallResourceSeam();

struct PhaseRecord {
  std::string name;
  int64_t elapsed_ms = 0;
  bool ok = false;
  std::string error;  // 失败原因（含 guest 异常类与消息）
};

// 观测钩子：每阶段 begin/end 各一次（end 时 record 已填）。
using PhaseObserver =
    std::function<void(const std::string& phase, bool begin, const PhaseRecord* record)>;

// 单阶段执行器（与 Run 内部 phase 一致：观测 + 计时 + PhaseRecord 落盘；
// 首帧管线用它把 19.6 子阶段并入同一 phase 序列）。
using PhaseRunner = std::function<bool(const std::string& name,
                                       const std::function<bool(std::string*)>& fn)>;

// 首帧管线钩子（M4.2，docs 10 章 19.6）：Run() 在 launcher Activity RESUMED
// （onPostResume）后调用，按 addView→traversal→submit→present→mark first
// frame 补跑（activity.onCreate 的 setContentView 已存 mContentView）。
// 实现落点 framework/render.cc RunFirstFramePipeline；未设置钩子时不产生
// 任何额外 phase（M4.1 13 phase 行为不变）。
using FirstFrameHook = std::function<bool(dexvm::runtime::VObject* activity,
                                          const PhaseRunner& run_phase, std::string* error)>;

class LifecycleDriver {
 public:
  LifecycleDriver(dexvm::runtime::VMState& state, dexvm::runtime::VObject* app_class_loader);

  void set_observer(PhaseObserver observer) { observer_ = std::move(observer); }
  void set_first_frame_hook(FirstFrameHook hook) { first_frame_hook_ = std::move(hook); }
  // ProviderInfo 注册诊断实时汇（进度日志消费；nullptr = 只留存报告列表）
  void set_diagnostic_sink(std::function<void(const std::string&)> sink) {
    diagnostic_sink_ = std::move(sink);
  }

  // 跑完整生命周期链；失败即停。成功 true。
  bool Run(const LifecycleConfig& config);

  const std::vector<PhaseRecord>& phases() const { return phases_; }
  const std::string& error() const { return error_; }
  bool ok() const { return ok_; }
  // ProviderInfo 预装配/注册诊断（authority 注册 ok/failed 逐条；exam 报告消费）
  const std::vector<std::string>& provider_registration() const { return provider_registration_; }

  dexvm::runtime::VObject* application() const { return application_; }
  dexvm::runtime::VObject* activity() const { return activity_; }
  dexvm::runtime::VObject* activity_thread() const { return activity_thread_; }
  dexvm::runtime::VObject* base_context() const { return base_context_; }

 private:
  bool Fail(const std::string& reason);  // 失败原因（phase 前缀由 Run 统一加）
  // guest 异常 -> "descriptor: message"（detailMessage），并清 pending。
  std::string TakeExceptionDiagnosis();
  bool CallVirtualOn(dexvm::runtime::VObject* obj, const char* name,
                     std::vector<std::string> params, const char* ret,
                     const dexvm::runtime::Value* args, size_t n,
                     dexvm::runtime::Value* out);
  bool Instantiate(const std::string& class_name, dexvm::runtime::VObject** out);
  // manifest ProviderEntry -> 真实 ProviderInfo 实例（AOSP ProviderInfo 公开
  // 字段面全装配；见 BuildProviderInfo 实现注释）。
  bool BuildProviderInfo(const LifecycleConfig& config, const ProviderSpec& spec,
                         dexvm::runtime::VObject** out);

  dexvm::runtime::VMState& state_;
  dexvm::runtime::VObject* app_loader_ = nullptr;
  PhaseObserver observer_;
  FirstFrameHook first_frame_hook_;
  std::function<void(const std::string&)> diagnostic_sink_;
  void RecordProviderRegistration(const std::string& line);
  std::vector<PhaseRecord> phases_;
  std::vector<std::string> provider_registration_;
  std::string error_;
  bool ok_ = true;

  dexvm::runtime::VObject* activity_thread_ = nullptr;
  dexvm::runtime::VObject* instrumentation_ = nullptr;
  dexvm::runtime::VObject* base_context_ = nullptr;
  dexvm::runtime::VObject* application_ = nullptr;
  dexvm::runtime::VObject* activity_ = nullptr;
};

}  // namespace dexvm::framework
