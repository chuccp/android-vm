// java.lang.invoke 家族冒烟（JLI 修复轮验收线）：
//   boot classpath = runtime-libs/build/runtime-dex/runtime.dex（家族入包）+
//   tests/out/mh-fixture.dex（javac+d8 min-api 26：invoke-polymorphic 真 opcode 形）。
// 断言：
//   1. 家族核心类入包（MethodHandles/VarHandle/VarHandleGuards/MethodHandle/
//      MethodType/MemberName/MethodHandleNatives/AccessDescriptor）；
//   2. StackOverflowError.super == VirtualMachineError（builtins 注册修复）；
//   3. CLQ.<clinit> 跑通（lookup + findVarHandle——考卷 attachBaseContext 停点）；
//   4. MhChecks.runAll() == ":ok"（lookup/findVarHandle/guard_*/marker/CLQ 全链）。
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"

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
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMState;
using dexvm::runtime::VMethod;

std::string ResultUtf8(const Value& r) {
  auto* s = reinterpret_cast<dexvm::runtime::VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

VMethod* FindStaticNoArgString(VClass* klass, const char* name) {
  for (auto* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name == name && m.IsStatic() && m.param_types.empty() &&
          m.return_type == "Ljava/lang/String;") {
        return &m;
      }
    }
  }
  return nullptr;
}

void DumpThrow(VMState& state) {
  if (state.pending_exception != nullptr) {
    auto* obj = reinterpret_cast<dexvm::runtime::VObject*>(state.pending_exception);
    std::string desc = obj->klass != nullptr ? obj->klass->descriptor : "null";
    std::string msg;
    for (auto* k = obj->klass; k != nullptr; k = k->super_class) {
      for (const auto& f : k->instance_fields) {
        if (f.name == "detailMessage") {
          uint64_t slot;
          std::memcpy(&slot, obj->field_data() + f.offset, sizeof(slot));
          auto* str = reinterpret_cast<dexvm::runtime::VString*>(
              static_cast<uintptr_t>(slot));
          if (str != nullptr) msg = str->ToModifiedUtf8();
          break;
        }
      }
      if (!msg.empty()) break;
    }
    std::printf("  exception: %s: %s\n", desc.c_str(), msg.c_str());
  }
  for (const auto& line : state.last_throw_backtrace) {
    std::printf("  throw: %s\n", line.c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== methodhandles smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/mh-fixture.dex";

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned;
  // 打开两个 dex（读入内存后 DexFile::Open(data,size)）
  struct Blob {
    std::vector<uint8_t> bytes;
    std::string name;
  };
  std::vector<Blob> blobs;
  for (const std::string& path : {runtime_dex, fixture_dex}) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) {
      std::printf("FAIL: open %s\n", path.c_str());
      return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    Blob b;
    b.name = path;
    b.bytes.resize(static_cast<size_t>(sz));
    if (sz > 0 && std::fread(b.bytes.data(), 1, static_cast<size_t>(sz), f) !=
                      static_cast<size_t>(sz)) {
      std::fclose(f);
      std::printf("FAIL: read %s\n", path.c_str());
      return 1;
    }
    std::fclose(f);
    blobs.push_back(std::move(b));
  }
  for (auto& b : blobs) {
    std::string err;
    auto dex = dexvm::dex::DexFile::Open(b.bytes.data(), b.bytes.size(), &err);
    CHECK(dex != nullptr, ("parse " + b.name).c_str());
    if (dex == nullptr) {
      std::printf("  error: %s\n", err.c_str());
      return 1;
    }
    linker.AddBootDexFile(dex.get());
    owned.push_back(std::move(dex));
  }

  // ---- 1. 家族核心类入包 ----
  const char* kFamily[] = {
      "Ljava/lang/invoke/MethodHandles;",
      "Ljava/lang/invoke/MethodHandles$Lookup;",
      "Ljava/lang/invoke/VarHandle;",
      "Ljava/lang/invoke/VarHandle$AccessDescriptor;",
      "Ljava/lang/invoke/VarHandle$AccessMode;",
      "Ljava/lang/invoke/VarHandleGuards;",
      "Ljava/lang/invoke/MethodHandle;",
      "Ljava/lang/invoke/MethodType;",
      "Ljava/lang/invoke/MemberName;",
      "Ljava/lang/invoke/MethodHandleNatives;",
      "Ljava/lang/invoke/MethodHandleInfo;",
  };
  for (const char* desc : kFamily) {
    VClass* k = linker.FindClass(desc);
    CHECK(k != nullptr, (std::string("family class in dex: ") + desc).c_str());
    if (k == nullptr) std::printf("  error: %s\n", linker.last_error().c_str());
  }

  // guard_* 形状矩阵在包（d8 脱糖形状派发面）
  {
    VClass* g = linker.FindClass("Ljava/lang/invoke/VarHandleGuards;");
    if (g != nullptr) {
      const char* kGuards[] = {"guard_L_I", "guard_LI_V", "guard_LII_Z", "guard_LI_I",
                               "guard__I",  "guard_I_V", "guard_L_L",  "guard_LLL_Z",
                               "guard__L",  "guard_L_V"};
      for (const char* gn : kGuards) {
        bool found = false;
        for (auto* list : {&g->direct_methods, &g->virtual_methods}) {
          for (auto& m : *list) {
            if (m.name == gn) found = true;
          }
        }
        CHECK(found, (std::string("guard shape present: ") + gn).c_str());
      }
    }
  }

  // ---- 2. StackOverflowError.super == VirtualMachineError ----
  {
    VClass* soe = linker.FindClass("Ljava/lang/StackOverflowError;");
    CHECK(soe != nullptr, "find StackOverflowError");
    if (soe != nullptr) {
      VClass* sup = soe->super_class;
      CHECK(sup != nullptr && sup->descriptor == "Ljava/lang/VirtualMachineError;",
            "StackOverflowError.super == VirtualMachineError");
      if (sup != nullptr && sup->descriptor != "Ljava/lang/VirtualMachineError;") {
        std::printf("  got super: %s\n", sup->descriptor.c_str());
      }
      // 双向断言面：同描述符不得重复注册（后建覆盖前建 = 本 bug 根因）
      int count = 0;
      for (VClass* k : linker.all_classes()) {
        if (k != nullptr && k->descriptor == "Ljava/lang/StackOverflowError;") count++;
      }
      CHECK(count == 1, "StackOverflowError registered exactly once");
    }
  }

  // ---- 3. CLQ.<clinit>（lookup + findVarHandle——验收线） ----
  {
    VClass* clq = linker.FindClass("Ljava/util/concurrent/ConcurrentLinkedQueue;");
    CHECK(clq != nullptr, "find ConcurrentLinkedQueue");
    if (clq != nullptr) {
      bool ok = linker.InitializeClass(clq, &state);
      if (!ok) DumpThrow(state);
      CHECK(ok, "CLQ <clinit> runs (MethodHandles.lookup + findVarHandle)");
      if (state.HasPendingException()) state.ClearException();
    }
  }

  // ---- 4. MhChecks.runAll() 全链 ----
  {
    VClass* mh = linker.FindClass("Ltest/mh/MhChecks;");
    CHECK(mh != nullptr, "find test.mh.MhChecks");
    if (mh != nullptr) {
      bool ok = linker.InitializeClass(mh, &state);
      if (!ok) DumpThrow(state);
      CHECK(ok, "MhChecks <clinit>");
      if (state.HasPendingException()) state.ClearException();
      VMethod* run = FindStaticNoArgString(mh, "runAll");
      CHECK(run != nullptr, "find MhChecks.runAll");
      if (run != nullptr) {
        Value r{};
        bool ran = InvokeMethod(state, run, nullptr, 0, &r);
        if (!ran) DumpThrow(state);
        CHECK(ran, "MhChecks.runAll invoke");
        if (ran) {
          std::string got = ResultUtf8(r);
          if (got != ":ok") std::printf("  runAll -> %s\n", got.c_str());
          CHECK(got == ":ok", "MhChecks.runAll == \":ok\"");
        }
        if (state.HasPendingException()) state.ClearException();
      }
    }
  }

  std::printf("== methodhandles smoke: %s ==\n", g_failures == 0 ? "OK" : "FAILED");
  return g_failures == 0 ? 0 : 1;
}
