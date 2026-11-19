// target-app 考卷 8 轮编排——宿主跑卷（tests/target_exam_table.h 哨兵契约表）。
// boot classpath = runtime.dex（runtime-libs 流水线产物）；
// app loader = target-app-debug.apk（apk_run 引导链同款）。
// 逐轮执行哨兵 int 断言（R1-R6 表驱动；R7 桥接出画/R8 加固 .so 为设备轮，
// 宿主按 tests/jni_shell + tools/ui_bridge_tests.sh 专测覆盖，本表报告 SKIP）。
// 探针经 RunGuestMain 在 main GuestTask 上下文执行（并发/挂起可用，
// api_matrix/concurrency_matrix 先例：SetGuestSchedOps 装载调度器后端）。
// 用法: target_exam_smoke <runtime.dex> <target-app.apk> [rounds=1,2,3,4,5,6]
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "session/class_loader.h"
#include "session/session.h"
#include "tests/target_exam_table.h"

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
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

ObjHeader* FieldRefByName(VObject* o, const char* name) {
  if (o == nullptr || o->klass == nullptr) return nullptr;
  for (VClass* c = o->klass; c != nullptr; c = c->super_class) {
    for (auto& f : c->instance_fields) {
      if (f.name == name) {
        ObjHeader* v = nullptr;
        std::memcpy(&v, o->field_data() + f.offset, sizeof(v));
        return v;
      }
    }
  }
  return nullptr;
}

std::string ExceptionDiag(VMState& state) {
  std::string diag = "invoke failed";
  if (state.HasPendingException() && state.pending_exception != nullptr &&
      state.pending_exception->klass != nullptr) {
    diag = state.pending_exception->klass->descriptor;
    for (VClass* c = state.pending_exception->klass;
         c != nullptr && diag.find(":") == std::string::npos;
         c = c->super_class) {
      for (auto& f : c->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* msg = nullptr;
          std::memcpy(&msg,
                      reinterpret_cast<VObject*>(state.pending_exception)->field_data() + f.offset,
                      sizeof(msg));
          if (msg != nullptr) diag += ": " + reinterpret_cast<VString*>(msg)->ToModifiedUtf8();
          break;
        }
      }
    }
  }
  // cause 链（Throwable.cause；netty ChannelException 等包装异常的根因）
  {
    VObject* ex = reinterpret_cast<VObject*>(state.pending_exception);
    for (int depth = 0; depth < 5 && ex != nullptr; depth++) {
      ObjHeader* cause = FieldRefByName(ex, "cause");
      if (cause == nullptr || cause == reinterpret_cast<ObjHeader*>(ex)) {
        cause = FieldRefByName(ex, "target");  // InvocationTargetException.target
      }
      if (cause == nullptr || cause == reinterpret_cast<ObjHeader*>(ex)) break;
      VObject* c = reinterpret_cast<VObject*>(cause);
      diag += " <- " + (c->klass != nullptr ? c->klass->descriptor : std::string("?"));
      ObjHeader* msg = FieldRefByName(c, "detailMessage");
      if (msg != nullptr) {
        diag += ": " + static_cast<VString*>(msg)->ToModifiedUtf8();
      }
      ex = c;
    }
  }
  // 抛点栈（前 6 帧；诊断缺口归因用）
  if (!state.last_throw_backtrace.empty()) {
    diag += " @";
    for (size_t i = 0; i < state.last_throw_backtrace.size() && i < 6; i++) {
      diag += " " + state.last_throw_backtrace[i];
    }
  }
  return diag;
}

VMethod* FindMethod(VClass* klass, const char* name, const std::vector<std::string>& params,
                    const char* ret, bool want_static) {
  for (auto* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name == name && m.IsStatic() == want_static && m.param_types == params &&
          m.return_type == ret) {
        return &m;
      }
    }
  }
  return nullptr;
}

struct ProbeRun {
  VMState* state = nullptr;
  ClassLinker* linker = nullptr;
  VObject* app_loader = nullptr;
  const dexvm::tests::TargetExamProbe* probe = nullptr;
  int32_t got = 0;
  std::string diag;
};

// 全部探针在同一 guest main 生命周期内顺序执行（设备 nativeRunTargetExam
// 同形：单进程一轮跑卷）。逐探针 Reset/RunGuestMain 会让 kotlinx/线程池等
// 跨轮静态单例握住已销毁 Scheduler 的任务句柄——非真实进程语义。
struct ProbeSession {
  ClassLinker* linker = nullptr;
  VObject* app_loader = nullptr;
  bool round_on[9] = {};
  std::vector<std::string> filter_parts;
  int round_pass[9] = {};
  int round_total[9] = {};
  int failures = 0;

  bool wanted(const char* method) const {
    if (filter_parts.empty()) return true;
    for (const auto& f : filter_parts) {
      if (std::strstr(method, f.c_str()) != nullptr) return true;
    }
    return false;
  }
};

bool ProbeMain(VMState& state, void* raw) {
  ProbeRun* p = static_cast<ProbeRun*>(raw);
  const dexvm::tests::TargetExamProbe& probe = *p->probe;
  VClass* k = p->linker->FindClass(&state, p->app_loader, probe.owner);
  if (k == nullptr) {
    p->diag = "class not found: " + std::string(probe.owner);
    return false;
  }
  if (!p->linker->InitializeClass(k, &state)) {
    p->diag = "clinit failed: " + ExceptionDiag(state);
    state.ClearException();
    return false;
  }
  std::vector<std::string> params;
  if (probe.p0 != nullptr) params.push_back(probe.p0);
  if (probe.p1 != nullptr) params.push_back(probe.p1);
  VMethod* m = FindMethod(k, probe.method, params, probe.ret, probe.is_static);
  if (m == nullptr) {
    p->diag = "method not found: " + std::string(probe.method);
    return false;
  }
  Value argv[4];
  size_t argc = 0;
  if (!probe.is_static) {
    VObject* self = p->linker->AllocObject(k);
    if (self == nullptr) {
      p->diag = "alloc failed";
      return false;
    }
    VMethod* ctor = FindMethod(k, "<init>", {}, "V", false);
    if (ctor != nullptr) {
      Value cself{};
      cself.ref = self;
      Value cr{};
      if (!InvokeMethod(state, ctor, &cself, 1, &cr)) {
        p->diag = "ctor failed: " + ExceptionDiag(state);
        state.ClearException();
        return false;
      }
    }
    argv[argc++].ref = self;
  }
  if (probe.p0 != nullptr) {
    if (std::strcmp(probe.p0, "I") == 0) argv[argc].i = probe.a0;
    else argv[argc].ref = nullptr;
    argc++;
  }
  if (probe.p1 != nullptr) {
    if (std::strcmp(probe.p1, "I") == 0) argv[argc].i = probe.a1;
    else argv[argc].ref = nullptr;
    argc++;
  }
  Value r{};
  if (!InvokeMethod(state, m, argc > 0 ? argv : nullptr, argc, &r)) {
    p->diag = ExceptionDiag(state);
    state.ClearException();
    return false;
  }
  p->got = r.i;
  return true;
}

bool ProbeSessionMain(VMState& state, void* raw) {
  ProbeSession* s = static_cast<ProbeSession*>(raw);
  for (int i = 0; i < dexvm::tests::kTargetExamProbeCount; i++) {
    const dexvm::tests::TargetExamProbe& probe = dexvm::tests::kTargetExamProbes[i];
    if (!s->round_on[probe.round]) continue;
    if (!s->wanted(probe.method)) continue;
    s->round_total[probe.round]++;
    ProbeRun run;
    run.state = &state;
    run.linker = s->linker;
    run.app_loader = s->app_loader;
    run.probe = &probe;
    bool ok = ProbeMain(state, &run);
    std::string label =
        std::string(probe.owner) + "." + probe.method + " [" + probe.note + "]";
    if (!ok && !run.diag.empty()) {
      std::printf("  diag: %s -> %s\n", label.c_str(), run.diag.c_str());
    }
    bool pass = ok && run.got == probe.expect;
    if (pass) {
      s->round_pass[probe.round]++;
      std::printf("PASS: R%d %s == %d\n", probe.round, label.c_str(), probe.expect);
    } else {
      s->failures++;
      std::printf("FAIL: R%d %s", probe.round, label.c_str());
      if (ok) std::printf(" got %d expect %d", run.got, probe.expect);
      std::printf("\n");
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== target-app exam smoke (8 rounds, host) ==\n");

  // 诊断过滤：TARGET_EXAM_FILTER="advancedPhaserFlow,timeoutAndCancellationFlow"
  //（逗号分隔方法名子串；仅缩小跑卷面，不改判据）
  std::vector<std::string> filter_parts;
  if (const char* filter = std::getenv("TARGET_EXAM_FILTER")) {
    std::string all(filter);
    size_t start = 0;
    while (start <= all.size()) {
      size_t comma = all.find(',', start);
      std::string part = all.substr(start, comma == std::string::npos ? std::string::npos
                                                                     : comma - start);
      if (!part.empty()) filter_parts.push_back(part);
      if (comma == std::string::npos) break;
      start = comma + 1;
    }
  }

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string target_apk =
      argc > 2 ? argv[2] : "target-app/build/outputs/apk/debug/target-app-debug.apk";
  std::string round_sel = argc > 3 ? argv[3] : "1,2,3,4,5,6";

  // netty HTTP 面：服务流默认 600s 挂住——考卷口径 1s 自闭（fastHttp 同型）
  ::setenv("DEXVM_HTTP_SECONDS", "1", 1);

  dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());

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
    std::printf("boot dex files: %zu (runtime.dex)\n", boot->dex_blobs().size());
  }

  VObject* app_loader = nullptr;
  {
    auto app = dexvm::session::Session::Open(target_apk, &err);
    CHECK(app != nullptr && !app->dex_blobs().empty(), "open target-app-debug.apk");
    if (app == nullptr) return 1;
    for (const auto& blob : app->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse app dex: " + blob.name).c_str());
      if (dex_file == nullptr) return 1;
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    std::printf("app dex files: %zu (target APK)\n", app->dex_blobs().size());

    VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
    app_loader = linker.AllocObject(cl);
    CHECK(app_loader != nullptr, "alloc app ClassLoader object");
    linker.loaders().BindObject(app_loader, linker.loaders().default_app());
    linker.loaders().default_app()->resource_zips.push_back(target_apk);
  }

  // ---- 逐轮哨兵断言 ----
  bool round_on[9] = {};
  for (size_t i = 0; i < round_sel.size();) {
    size_t comma = round_sel.find(',', i);
    std::string tok = round_sel.substr(i, comma == std::string::npos ? std::string::npos
                                                                    : comma - i);
    if (tok.rfind("R", 0) == 0) tok = tok.substr(1);
    int r = std::atoi(tok.c_str());
    if (r >= 1 && r <= 8) round_on[r] = true;
    if (comma == std::string::npos) break;
    i = comma + 1;
  }

  ProbeSession session;
  session.linker = &linker;
  session.app_loader = app_loader;
  for (int r = 1; r <= 8; r++) session.round_on[r] = round_on[r];
  session.filter_parts = filter_parts;
  bool run_ok = dexvm::runtime::RunGuestMainImpl(state, ProbeSessionMain, &session);
  if (!run_ok) {
    std::printf("guest run failed: %s\n", dexvm::runtime::GuestRunFailureReason());
    g_failures++;
  }
  g_failures += session.failures;

  std::printf("\n== per-round verdicts ==\n");
  int rounds_green = 0;
  int rounds_run = 0;
  for (int r = 1; r <= 8; r++) {
    if (r == 7) {
      std::printf("[R7] SKIP host (device round: bridge 出画断言 tools/target_exam_tests.sh)\n");
      continue;
    }
    if (r == 8) {
      std::printf("[R8] SKIP host (device round: 加固 .so JNI; host 先例 tests/jni_shell)\n");
      continue;
    }
    if (!round_on[r] || session.round_total[r] == 0) continue;
    rounds_run++;
    bool green = session.round_pass[r] == session.round_total[r];
    if (green) rounds_green++;
    std::printf("[R%d] %s %d/%d (%s)\n", r, green ? "PASS" : "FAIL", session.round_pass[r],
                session.round_total[r], dexvm::tests::kRoundNames[r]);
  }
  std::printf("rounds green: %d/%d (host table rounds)\n", rounds_green, rounds_run);
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
