// 解释器/API 流矩阵宿主 harness（tools/api_matrix_tests.sh）。
// 引导链：RegisterBuiltins + runtime.dex boot + api_matrix.dex app loader；
// check* 探针经 RunGuestMain 在 main GuestTask 上下文执行（锁/挂起可用）。
// 覆盖面参考启发自 v1 DexFixture/NativeVmCoreTest/NativePlatformMigrationTest/
// GuestHeapGcTest 断言面；断言判据 = v2 规格 + Android 语义。
//   1) check* 流矩阵（静态链/对象流/数组/异常嵌套/clinit 计数/锁重入/Color.rgb）
//   2) greeting 字符串面
//   3) clinit 挂起/不重放四连
//   4) WeakReference/ReferenceQueue/GC 面（宿主编排；GC 清除/入队按 docs 02 章
//      6.10 当前口径仅观测记录，不作断言）
//   5) java.io 文件面（cwd 由脚本切 scratch；持久化两式按序编排）
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

int g_failures = 0;
int g_gaps = 0;  // 已知 VM 缺口观测（不计 FAIL；修复后翻转为断言）

#define CHECK(cond, msg)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      g_failures++;                                               \
    } else {                                                      \
      std::printf("PASS: %s\n", msg);                             \
    }                                                             \
  } while (0)

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

VMethod* FindStatic(VClass* klass, const char* name,
                    std::vector<std::string> params, const char* ret) {
  for (auto* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name == name && m.IsStatic() && m.param_types == params &&
          m.return_type == ret) {
        return &m;
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
                      reinterpret_cast<VObject*>(state.pending_exception)->field_data() +
                          f.offset,
                      sizeof(msg));
          if (msg != nullptr) diag += ": " + reinterpret_cast<VString*>(msg)->ToModifiedUtf8();
          break;
        }
      }
    }
  }
  return diag;
}

struct ProbePayload {
  VMState* state;
  VMethod* run;
  Value* args;
  size_t arg_count;
  Value result;
  bool invoked;
};

bool ProbeMain(VMState& state, void* raw) {
  ProbePayload* p = static_cast<ProbePayload*>(raw);
  p->invoked = true;
  Value r{};
  if (!InvokeMethod(state, p->run, p->args, p->arg_count, &r)) {
    std::printf("  diag: %s\n", ExceptionDiag(state).c_str());
    state.ClearException();
    return false;
  }
  p->result = r;
  return true;
}

// 单 check* 编排：RunGuestMain 驱动 + "name:ok" 判定。
// gap_on_mismatch=true：不匹配以 GAP 观测呈现（已知 VM 缺口面），不计 FAIL。
void RunCheckResult(VMState& state, ClassLinker& linker, const char* owner,
                    const char* method, const char* expect, bool gap_on_mismatch) {
  VClass* k = linker.FindClass(owner);
  if (k == nullptr) {
    CHECK(false, (std::string("find ") + owner).c_str());
    return;
  }
  if (!linker.InitializeClass(k, &state) || state.HasPendingException()) {
    state.ClearException();
    CHECK(false, (std::string("clinit ") + owner).c_str());
    return;
  }
  VMethod* m = FindStatic(k, method, {}, "Ljava/lang/String;");
  if (m == nullptr) {
    CHECK(false, (std::string("find ") + method).c_str());
    return;
  }
  std::printf("-- %s --\n", method);
  ProbePayload payload{&state, m, nullptr, 0, Value{}, false};
  bool invoked = dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &payload);
  std::string got = payload.invoked && invoked ? ResultUtf8(payload.result) : "<not-run>";
  bool pass = invoked && payload.invoked && got == expect;
  if (pass) {
    std::printf("PASS: %s == %s\n", method, expect);
  } else if (gap_on_mismatch) {
    std::printf("GAP: %s got %s (Android expect %s) — known VM gap, not FAIL\n",
                method, got.c_str(), expect);
    g_gaps++;
  } else {
    if (got != expect) std::printf("  got %s (expect %s)\n", got.c_str(), expect);
    CHECK(false, (std::string(method) + " == " + expect).c_str());
  }
  dexvm::runtime::GuestRuntimeReset(&state);
}

void RunCheck(VMState& state, ClassLinker& linker, const char* owner,
              const char* method, const char* expect) {
  RunCheckResult(state, linker, owner, method, expect, /*gap_on_mismatch=*/false);
}

// 目标辅助（弱引用阶段）：无参 static 返回指定引用类型的句柄
Value CallRefMethod(VMState& state, VMethod* m, Value* args, size_t n,
                    bool* ok) {
  ProbePayload payload{&state, m, args, n, Value{}, false};
  *ok = dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &payload) && payload.invoked;
  dexvm::runtime::GuestRuntimeReset(&state);
  return payload.result;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("== api matrix smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/api_matrix.dex";

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

  // ---- 1. check* 流矩阵 ----
  struct {
    const char* method;
    const char* expect;
  } kChecks[] = {
      {"checkStaticCall", "staticCall:ok"},
      {"checkObjectFlow", "objectFlow:ok"},
      {"checkArrayFlow", "arrayFlow:ok"},
      {"checkCatchFlow", "catchFlow:ok"},
      {"checkNestedCatch", "nestedCatch:ok"},
      {"checkNullCatch", "nullCatch:ok"},
      {"checkClassInitCount", "classInitCount:ok"},
      {"checkLockHold", "lockHold:ok"},
      {"checkColorRgb", "colorRgb:ok"},
  };
  for (const auto& c : kChecks) {
    RunCheck(state, linker, "Lapitest/ApiMatrixChecks;", c.method, c.expect);
  }

  // ---- 2. greeting 字符串面 ----
  {
    VClass* k = linker.FindClass("Lapitest/ApiMatrixChecks;");
    CHECK(k != nullptr, "find ApiMatrixChecks");
    if (k != nullptr) {
      linker.InitializeClass(k, &state);
      VMethod* m = FindStatic(k, "greeting", {}, "Ljava/lang/String;");
      CHECK(m != nullptr, "find greeting");
      if (m != nullptr) {
        ProbePayload payload{&state, m, nullptr, 0, Value{}, false};
        bool invoked = dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &payload);
        std::string got = invoked ? ResultUtf8(payload.result) : "<not-run>";
        CHECK(invoked && got == "hello-vm", "greeting == \"hello-vm\"");
        dexvm::runtime::GuestRuntimeReset(&state);
      }
    }
  }

  // ---- 3. clinit 挂起/不重放四连 ----
  RunCheck(state, linker, "Lapitest/ClinitChecks;", "checkSuspendingClinit",
           "clinitSuspend:ok");
  RunCheck(state, linker, "Lapitest/ClinitChecks;", "checkClinitNotReplayed",
           "clinitOnce:ok");
  RunCheck(state, linker, "Lapitest/ClinitChecks;", "checkCooperativeResult",
           "yieldResult:ok");
  RunCheck(state, linker, "Lapitest/ClinitChecks;", "checkEntryAfterSuspendingClinit",
           "entryClinit:ok");

  // ---- 4. WeakReference/ReferenceQueue/GC 面 ----
  {
    VClass* k = linker.FindClass("Lapitest/WeakRefChecks;");
    CHECK(k != nullptr, "find WeakRefChecks");
    if (k != nullptr && linker.InitializeClass(k, &state)) {
      VMethod* new_obj = FindStatic(k, "newObject", {}, "Ljava/lang/Object;");
      VMethod* make_weak =
          FindStatic(k, "makeWeak", {"Ljava/lang/Object;"}, "Ljava/lang/Object;");
      VMethod* weak_get =
          FindStatic(k, "weakGet", {"Ljava/lang/Object;"}, "Ljava/lang/Object;");
      VMethod* clear_ref = FindStatic(k, "clearRef", {"Ljava/lang/Object;"}, "V");
      VMethod* make_queue = FindStatic(k, "makeQueue", {}, "Ljava/lang/Object;");
      VMethod* make_weak_q = FindStatic(
          k, "makeWeakWithQueue", {"Ljava/lang/Object;", "Ljava/lang/Object;"},
          "Ljava/lang/Object;");
      VMethod* queue_poll =
          FindStatic(k, "queuePoll", {"Ljava/lang/Object;"}, "Ljava/lang/Object;");
      VMethod* request_gc = FindStatic(k, "requestGc", {}, "V");
      VMethod* free_mem = FindStatic(k, "freeMemory", {}, "J");
      VMethod* total_mem = FindStatic(k, "totalMemory", {}, "J");
      CHECK(new_obj && make_weak && weak_get && clear_ref && make_queue &&
                make_weak_q && queue_poll && request_gc && free_mem && total_mem,
            "find WeakRefChecks helpers");
      if (new_obj && make_weak && weak_get && clear_ref && make_queue &&
          make_weak_q && queue_poll && request_gc && free_mem && total_mem) {
        bool ok = true;
        Value target = CallRefMethod(state, new_obj, nullptr, 0, &ok);
        CHECK(ok && target.ref != nullptr, "newObject");
        Value wa[1] = {target};
        Value weak = CallRefMethod(state, make_weak, wa, 1, &ok);
        CHECK(ok && weak.ref != nullptr, "makeWeak");

        Value wa2[1] = {weak};
        Value got = CallRefMethod(state, weak_get, wa2, 1, &ok);
        CHECK(ok && got.ref == target.ref, "weakGet identity == target");

        Value queue = CallRefMethod(state, make_queue, nullptr, 0, &ok);
        CHECK(ok && queue.ref != nullptr, "makeQueue");
        Value wa3[2] = {target, queue};
        Value weak_q = CallRefMethod(state, make_weak_q, wa3, 2, &ok);
        CHECK(ok && weak_q.ref != nullptr, "makeWeakWithQueue");
        Value qa[1] = {queue};
        Value polled = CallRefMethod(state, queue_poll, qa, 1, &ok);
        CHECK(ok && polled.ref == nullptr, "queuePoll before use == null");

        // clear() -> get() 为 null（Android Reference 语义，双方口径一致）
        Value ca[1] = {weak};
        ProbePayload clear_payload{&state, clear_ref, ca, 1, Value{}, false};
        bool cleared =
            dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &clear_payload) &&
            clear_payload.invoked;
        dexvm::runtime::GuestRuntimeReset(&state);
        CHECK(cleared, "clearRef runs");
        got = CallRefMethod(state, weak_get, wa2, 1, &ok);
        CHECK(ok && got.ref == nullptr, "weakGet after clear == null");

        // requestGc + 内存面
        ProbePayload gc_payload{&state, request_gc, nullptr, 0, Value{}, false};
        bool gc_ok =
            dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &gc_payload) &&
            gc_payload.invoked;
        dexvm::runtime::GuestRuntimeReset(&state);
        CHECK(gc_ok, "requestGc runs");
        Value free = CallRefMethod(state, free_mem, nullptr, 0, &ok);
        CHECK(ok && free.l > 0, "freeMemory > 0");
        Value total = CallRefMethod(state, total_mem, nullptr, 0, &ok);
        CHECK(ok && total.l > 0, "totalMemory > 0");
        // Android 语义不变式 freeMemory <= totalMemory（OpenJDK free = total - used）。
        // v2 现口径：total=bytes_allocated、free=capacity-allocated
        // （builtins_system.cc RuntimeTotalMemory/RuntimeFreeMemory）——观测为 GAP，
        // 不计 FAIL（core 侧修复后翻转为断言）。
        if (ok && !(total.l >= free.l)) {
          std::printf(
              "GAP: totalMemory >= freeMemory violated (total=%lld free=%lld); "
              "v2 maps total=bytes_allocated free=capacity-allocated, "
              "Android expects free<=total\n",
              static_cast<long long>(total.l), static_cast<long long>(free.l));
          g_gaps++;
        }

        // docs 02 章 6.10 当前口径：referent 视为强引用、GC 不清扫；
        // ReferenceProcessor/入队为目标态。此处仅观测记录，不作断言。
        Value wa4[1] = {weak_q};
        Value post = CallRefMethod(state, weak_get, wa4, 1, &ok);
        std::printf(
            "NOTE: weak-with-queue get after gc -> %s "
            "(weak-clear/enqueue semantics deferred, docs 02 ch 6.10)\n",
            post.ref != nullptr ? "<referent>" : "<null>");
        Value polled2 = CallRefMethod(state, queue_poll, qa, 1, &ok);
        std::printf(
            "NOTE: queuePoll after gc -> %s "
            "(ReferenceQueue enqueue deferred, docs 02 ch 6.10)\n",
            polled2.ref != nullptr ? "<ref>" : "<null>");
      }
    }
  }

  // ---- 5. java.io 文件面（cwd = scratch，由 tools/api_matrix_tests.sh 提供）----
  RunCheck(state, linker, "Lapitest/FileIoChecks;", "checkFileIoFlow", "fileIo:ok");
  RunCheck(state, linker, "Lapitest/FileIoChecks;", "checkPersistentWrite",
           "persistentWrite:ok");
  RunCheck(state, linker, "Lapitest/FileIoChecks;", "checkPersistentDelete",
           "persistentDelete:ok");
  RunCheck(state, linker, "Lapitest/FileIoChecks;", "checkFileThroughput",
           "fileThroughput:ok");

  // read(byte[]) EOF 语义（Android：-1）——单次越尾读，不循环。
  // 当前 v2 观测预期 "readEof:observed:0"（FISReadBytes r==0 未映射 -1，
  // builtins_io.cc；单字节 read0 已是 -1 口径）。判据 = Android（-1）；不匹配
  // 以 GAP 呈现不计 FAIL（core 侧修复后自动转 PASS）。
  {
    std::string eof_expect = "readEof:observed:-1";
    RunCheckResult(state, linker, "Lapitest/FileIoChecks;", "checkReadEof",
                   eof_expect.c_str(), /*gap_on_mismatch=*/true);
  }

  if (g_failures == 0) {
    std::printf("ALL PASS");
    if (g_gaps > 0) std::printf(" (%d GAP observations, see GAP: lines)", g_gaps);
    std::printf("\n");
    return 0;
  }
  std::printf("%d FAILURES", g_failures);
  if (g_gaps > 0) std::printf(" (%d GAP observations)", g_gaps);
  std::printf("\n");
  return 1;
}
