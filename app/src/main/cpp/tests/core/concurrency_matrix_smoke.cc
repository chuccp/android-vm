// 并发原语矩阵宿主 harness（tools/concurrency_matrix_tests.sh）。
// 引导链：RegisterBuiltins + runtime.dex boot + concurrency_matrix.dex app loader；
// 每探针经 RunGuestMain 以 main GuestTask 执行（GuestTask 1:1 线程模型，docs 04 章
// 14.2/14.8），静态 String run() 按行判定 "name: OK"/"name: FAIL"。
// 覆盖面参考启发自 v1 NativeVmModernConcurrencyTest.kt + RuntimeModernConcurrency
// （sleep/park/join/lock 中断、Condition、Semaphore 公平序、CountDownLatch、
// Barrier/Phaser、Future/CompletableFuture、ConcurrentMap、BlockingQueue、
// ThreadGroup）；断言判据 = Android j.u.c/java.lang 语义。
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "session/session.h"

namespace {

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

VMethod* FindStaticRun(VClass* klass) {
  for (auto* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name == "run" && m.IsStatic() && m.param_types.empty() &&
          m.return_type == "Ljava/lang/String;") {
        return &m;
      }
    }
  }
  return nullptr;
}

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

// 行判定：": FAIL" = 失败；": OK" 计数；GAP/NOTE 行中性（打印留档）
bool Judge(const std::string& text, int* ok_lines) {
  bool fail = false;
  *ok_lines = 0;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t nl = text.find('\n', pos);
    if (nl == std::string::npos) nl = text.size();
    std::string line = text.substr(pos, nl - pos);
    pos = nl + 1;
    if (line.find(": OK") != std::string::npos) (*ok_lines)++;
    if (line.find(": FAIL") != std::string::npos) fail = true;
  }
  return !fail && *ok_lines > 0;
}

struct ProbePayload {
  VMState* state;
  VMethod* run;
  std::string* out;
  bool invoked;
};

bool ProbeMain(VMState& state, void* raw) {
  ProbePayload* p = static_cast<ProbePayload*>(raw);
  p->invoked = true;
  Value r{};
  if (!InvokeMethod(state, p->run, nullptr, 0, &r)) {
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
                        reinterpret_cast<VObject*>(state.pending_exception)->field_data() +
                            f.offset,
                        sizeof(msg));
            if (msg != nullptr) diag += ": " + reinterpret_cast<VString*>(msg)->ToModifiedUtf8();
            break;
          }
        }
      }
    }
    *(p->out) = "probe: FAIL " + diag + "\n";
    state.ClearException();
    return false;
  }
  *(p->out) = ResultUtf8(r);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("== concurrency matrix smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/concurrency_matrix.dex";

  dexvm::runtime::SetGuestSchedOps(dexvm::runtime::VmSchedBackendOps());

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  std::string err;
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;
  {
    auto boot = dexvm::session::Session::Open(runtime_dex, &err);
    if (boot == nullptr || boot->dex_blobs().empty()) {
      std::printf("FAIL: open runtime.dex: %s\n", err.c_str());
      return 1;
    }
    for (const auto& blob : boot->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      if (dex_file == nullptr) {
        std::printf("FAIL: parse boot dex %s: %s\n", blob.name.c_str(), derr.c_str());
        return 1;
      }
      linker.AddBootDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
  }
  {
    auto app = dexvm::session::Session::Open(fixture_dex, &err);
    if (app == nullptr || app->dex_blobs().empty()) {
      std::printf("FAIL: open fixture dex: %s\n", err.c_str());
      return 1;
    }
    for (const auto& blob : app->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      if (dex_file == nullptr) {
        std::printf("FAIL: parse app dex %s: %s\n", blob.name.c_str(), derr.c_str());
        return 1;
      }
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
  }

  struct ProbeRun {
    const char* name;
    const char* desc;
  } kProbes[] = {
      {"interrupt", "Lcmtest/InterruptProbe;"},
      {"lock-condition", "Lcmtest/LockConditionProbe;"},
      {"synchronizer", "Lcmtest/SynchronizerProbe;"},
      {"executor-future", "Lcmtest/ExecutorProbe;"},
      {"queue-map-group", "Lcmtest/QueueMapProbe;"},
  };

  bool all_ok = true;
  for (const ProbeRun& p : kProbes) {
    VClass* k = linker.FindClass(p.desc);
    if (k == nullptr) {
      std::printf("FAIL %s: class not found (%s): %s\n", p.name, p.desc,
                  linker.last_error().c_str());
      all_ok = false;
      continue;
    }
    if (!linker.InitializeClass(k, &state) || state.HasPendingException()) {
      std::printf("FAIL %s: class init failed: %s\n", p.name,
                  linker.last_error().c_str());
      state.ClearException();
      all_ok = false;
      continue;
    }
    VMethod* run = FindStaticRun(k);
    if (run == nullptr) {
      std::printf("FAIL %s: static run() not found\n", p.name);
      all_ok = false;
      continue;
    }
    std::string text;
    ProbePayload payload{&state, run, &text, false};
    std::printf("-- %s --\n", p.name);
    bool invoked = dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &payload);
    std::printf("%s", text.c_str());
    if (!text.empty() && text.back() != '\n') std::printf("\n");
    if (!invoked || !payload.invoked) {
      std::printf("FAIL %s: guest main drive failed (timeout/deadlock? %s)\n",
                  p.name, dexvm::runtime::GuestRunFailureReason());
      all_ok = false;
      continue;
    }
    int ok_lines = 0;
    if (!Judge(text, &ok_lines)) {
      all_ok = false;
    } else {
      std::printf("   %d checks OK\n", ok_lines);
    }
    dexvm::runtime::GuestRuntimeReset(&state);
  }

  if (all_ok) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("FAILURES\n");
  return 1;
}
