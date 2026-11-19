// VM 侧方法解析 bug 复现（知乎考卷 Math.max(II)I NoSuchMethodError）。
//
// 现象：runtime.dex 里 Ljava/lang/Math; 已定义、max(II)I/(JJ)J/(DD)D/(FF)F 均在
// class_data，但解释器 invoke-static 解析（ResolveMethod）抛 NoSuchMethodError。
// 本用例沿解释器【同一条路径】（ResolveMethod(dex, method_idx)）复现，并对照
// FindClass + direct_methods 直查，把差异定位到类定义 / 方法查找 / DexCache。
//
// 编译：见 tools/method_resolve_tests.sh（独立脚本，不动 host_tests.sh）。
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "memory/heap.h"

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
using dexvm::runtime::VClass;
using dexvm::runtime::VMState;
using dexvm::runtime::VMethod;

// 在 dex 的 method_id 表里找 Ljava/lang/Math;.<name>(<params>)<ret>。
// 返回 method_idx，未命中返回 -1。这是调用方 dex 的 method_id 引用（即解释器
// ResolveMethod(dex, cs.idx) 里的 cs.idx）。
int FindMethodId(const dexvm::dex::DexFile& d, const char* cls, const char* name,
                 const std::vector<std::string>& params, const char* ret) {
  for (uint32_t i = 0; i < d.NumMethodIds(); i++) {
    const dexvm::dex::MethodId& mid = d.GetMethodId(i);
    if (d.GetTypeDescriptor(mid.class_idx) != cls) continue;
    if (d.GetString(mid.name_idx) != name) continue;
    // 展开 proto 参数
    std::vector<std::string> got;
    uint32_t n = d.GetProtoParamCount(mid.proto_idx);
    for (uint32_t k = 0; k < n; k++)
      got.push_back(std::string(d.GetTypeDescriptor(d.GetProtoParamTypeIdx(mid.proto_idx, k))));
    std::string r(d.GetTypeDescriptor(d.GetProto(mid.proto_idx).return_type_idx));
    if (got == params && r == ret) return static_cast<int>(i);
  }
  return -1;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== method resolve smoke (Math.max NoSuchMethodError repro) ==\n");
  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  // ---- boot classpath = runtime.dex ----
  std::string err;
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned;
  std::unique_ptr<dexvm::dex::DexFile> boot_dex;
  {
    // 直接 Open 单 .dex（method_resolve 用例不需要 zip 会话）
    FILE* f = std::fopen(runtime_dex.c_str(), "rb");
    CHECK(f != nullptr, "open runtime.dex");
    if (f == nullptr) return 1;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> bytes(static_cast<size_t>(sz));
    size_t rd = std::fread(bytes.data(), 1, bytes.size(), f);
    std::fclose(f);
    CHECK(rd == bytes.size(), "read runtime.dex bytes");
    boot_dex = dexvm::dex::DexFile::Open(bytes.data(), bytes.size(), &err);
    CHECK(boot_dex != nullptr, "parse runtime.dex");
    if (boot_dex == nullptr) {
      std::printf("  error: %s\n", err.c_str());
      return 1;
    }
    static std::vector<uint8_t> keep;  // DexFile 不接管字节，保活
    keep = std::move(bytes);
    linker.AddBootDexFile(boot_dex.get());
    std::printf("boot dex: %u classes, %u method_ids\n", boot_dex->NumClassDefs(),
                boot_dex->NumMethodIds());
  }

  // ---- 1. FindClass + direct_methods 直查（对照面）----
  VClass* math = linker.FindClass("Ljava/lang/Math;");
  std::printf("[diag] FindClass(Ljava/lang/Math;) = %p, last_error=%s\n", (void*)math,
              linker.last_error().c_str());
  CHECK(math != nullptr, "FindClass java.lang.Math");
  bool has_max_ii_direct = false;
  bool has_max_ii_virtual = false;
  if (math != nullptr) {
    std::printf("[diag] Math.status=%d dex_file=%p direct=%zu virtual=%zu super=%p\n",
                (int)math->status, (void*)math->dex_file, math->direct_methods.size(),
                math->virtual_methods.size(), (void*)math->super_class);
    for (auto& m : math->direct_methods) {
      if (m.name == "max" && m.param_types == std::vector<std::string>{"I", "I"} &&
          m.return_type == "I") {
        has_max_ii_direct = true;
        std::printf("[diag] direct max(II)I found: code=%p static=%d\n", (void*)m.code,
                    (int)m.IsStatic());
      }
    }
    for (auto& m : math->virtual_methods) {
      if (m.name == "max" && m.param_types == std::vector<std::string>{"I", "I"} &&
          m.return_type == "I") {
        has_max_ii_virtual = true;
      }
    }
  }
  CHECK(has_max_ii_direct, "Math.max(II)I present in direct_methods");

  // ---- 2. 解释器同路径：ResolveMethod(dex, method_idx) ----
  int idx_max_ii = FindMethodId(*boot_dex, "Ljava/lang/Math;", "max", {"I", "I"}, "I");
  std::printf("[diag] runtime.dex method_id for Math.max(II)I = %d\n", idx_max_ii);
  CHECK(idx_max_ii >= 0, "runtime.dex has method_id Math.max(II)I");
  if (idx_max_ii >= 0) {
    VMethod* resolved = linker.ResolveMethod(boot_dex.get(), static_cast<uint32_t>(idx_max_ii));
    std::printf("[diag] ResolveMethod(runtime.dex, %d) = %p, last_error=%s\n", idx_max_ii,
                (void*)resolved, linker.last_error().c_str());
    CHECK(resolved != nullptr, "ResolveMethod Math.max(II)I");
    // 二次调用走 DexCache 命中
    VMethod* again = linker.ResolveMethod(boot_dex.get(), static_cast<uint32_t>(idx_max_ii));
    CHECK(again == resolved, "ResolveMethod DexCache 稳定命中");
  }

  // 其余 overload：max(JJ)J / max(DD)D / max(FF)F
  struct {
    const char* ret;
    std::vector<std::string> params;
  } kOverloads[] = {
      {"J", {"J", "J"}},
      {"D", {"D", "D"}},
      {"F", {"F", "F"}},
  };
  for (const auto& ov : kOverloads) {
    int idx = FindMethodId(*boot_dex, "Ljava/lang/Math;", "max", ov.params, ov.ret);
    CHECK(idx >= 0, ("runtime.dex method_id Math.max" + std::string(ov.ret)).c_str());
    if (idx < 0) continue;
    VMethod* r = linker.ResolveMethod(boot_dex.get(), static_cast<uint32_t>(idx));
    CHECK(r != nullptr, ("ResolveMethod Math.max(" + std::string(ov.ret) + ")").c_str());
  }

  // ---- 3. Math 类初始化诊断：<clinit> 依赖（twoToTheDoubleScaleUp/Down =
  //        powerOfTwoD(...) -> DoubleConsts.*）是否在包内。失败时 dump 异常全貌 ----
  auto dump_ex = [&](const char* when) {
    if (!state.HasPendingException()) {
      std::printf("[diag] %s: no pending exception\n", when);
      return;
    }
    dexvm::runtime::ObjHeader* ex = state.pending_exception;
    std::string idesc =
        ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor : "<null>";
    std::string imsg;
    if (ex != nullptr && ex->klass != nullptr) {
      dexvm::runtime::VObject* eo = reinterpret_cast<dexvm::runtime::VObject*>(ex);
      for (VClass* k = eo->klass; k != nullptr && imsg.empty(); k = k->super_class) {
        for (const auto& f : k->instance_fields) {
          if (f.name == "detailMessage") {
            dexvm::runtime::ObjHeader* s = nullptr;
            std::memcpy(&s, eo->field_data() + f.offset, sizeof(s));
            if (s != nullptr) imsg = static_cast<dexvm::runtime::VString*>(s)->ToModifiedUtf8();
            break;
          }
        }
      }
    }
    std::printf("[diag] %s: exception %s: %s\n", when, idesc.c_str(), imsg.c_str());
    for (const auto& fr : state.last_throw_backtrace)
      std::printf("[diag]     at %s\n", fr.c_str());
    state.ClearException();
  };

  if (idx_max_ii >= 0) {
    VMethod* m = linker.ResolveMethod(boot_dex.get(), static_cast<uint32_t>(idx_max_ii));
    CHECK(m != nullptr, "re-resolve Math.max(II)I before init");
    if (m != nullptr) {
      std::printf("[diag] Math.max declaring=%s code=%p\n",
                  m->declaring != nullptr ? m->declaring->descriptor.c_str() : "?",
                  (void*)m->code);
      // <clinit> / init —— 失败 dump 根因（区分 lib 缺依赖 vs VM 诊断）
      bool inited = linker.InitializeClass(m->declaring, &state);
      if (!inited || state.HasPendingException()) {
        dump_ex("Math InitializeClass");
      }
      CHECK(inited, "Math <clinit> / init");
      // 即使 init 失败也试调用（诊断：解析与 init 解耦）
      dexvm::runtime::Value args[2] = {};
      args[0].i = 3;
      args[1].i = 5;
      dexvm::runtime::Value r{};
      bool ok = dexvm::runtime::InvokeMethod(state, m, args, 2, &r);
      if (!ok || state.HasPendingException()) dump_ex("Math.max invoke");
      CHECK(ok, "invoke Math.max(3,5)");
      CHECK(ok && r.i == 5, "Math.max(3,5) == 5");
    }
  }

  // ---- 3b. Math <clinit> 依赖链：<clinit> 引用的类（DoubleConsts/FloatConsts/
  //         RandomNumberGeneratorHolder 等）是否可解析 ----
  {
    const char* kDeps[] = {
        "Ljdk/internal/math/DoubleConsts;",
        "Ljdk/internal/math/FloatConsts;",
        "Ljava/lang/StrictMath;",
        "Ljava/util/Random;",
        "Ljava/math/BigDecimal;",
    };
    for (const char* d : kDeps) {
      VClass* dk = linker.FindClass(d);
      std::printf("[diag] Math dep %-40s -> %p (%s)\n", d, (void*)dk,
                  dk != nullptr ? "ok" : linker.last_error().c_str());
    }
  }

  // ---- 4. 真解释器 invoke-static Math.max：callMax(II)I dex 方法体内 invoke-static
  //        Ljava/lang/Math;.max(II)I。经 ExecuteFrame 走解释器 invoke 路径
  //        （ResolveMethod + EnsureClassInitialized + DoCall），捕获 guest 异常。
  //        这是【设备考卷 NoSuchMethodError】的最小对照——看解释器到底抛什么。----
  const std::string caller_dex = argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/mathcaller.dex";
  {
    FILE* f = std::fopen(caller_dex.c_str(), "rb");
    if (f == nullptr) {
      std::printf("[diag] skip interpreter invoke-static (no %s)\n", caller_dex.c_str());
    } else {
      std::fseek(f, 0, SEEK_END);
      long sz = std::ftell(f);
      std::fseek(f, 0, SEEK_SET);
      std::vector<uint8_t> bytes(static_cast<size_t>(sz));
      std::fread(bytes.data(), 1, bytes.size(), f);
      std::fclose(f);
      std::string derr;
      auto cdex = dexvm::dex::DexFile::Open(bytes.data(), bytes.size(), &derr);
      static std::vector<uint8_t> keep2;
      keep2 = std::move(bytes);
      CHECK(cdex != nullptr, "parse mathcaller.dex");
      if (cdex != nullptr) {
        // 作为 app dex 挂载 + 绑定 app loader（模拟考卷 app 上下文）
        linker.AddAppDexFile(cdex.get());
        VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
        dexvm::runtime::VObject* app_loader = linker.AllocObject(cl);
        linker.loaders().BindObject(app_loader, linker.loaders().default_app());
        VClass* mc = linker.FindClass(&state, app_loader, "LMathCaller;");
        CHECK(mc != nullptr, "FindClass MathCaller");
        VMethod* call_max = nullptr;
        if (mc != nullptr) {
          for (auto& m : mc->direct_methods) {
            if (m.name == "callMax" && m.IsStatic()) call_max = &m;
          }
        }
        CHECK(call_max != nullptr, "find MathCaller.callMax");
        if (call_max != nullptr) {
          dexvm::runtime::Value args[2] = {};
          args[0].i = 3;
          args[1].i = 5;
          dexvm::runtime::Value r{};
          bool ok = dexvm::runtime::InvokeMethod(state, call_max, args, 2, &r);
          std::printf("[diag] interpreter invoke-static Math.max via callMax: ok=%d r.i=%d\n",
                      (int)ok, r.i);
          if (!ok || state.HasPendingException()) dump_ex("callMax (interpreter invoke-static)");
          CHECK(ok, "interpreter invoke-static Math.max runs");
          CHECK(ok && r.i == 5, "callMax(3,5) == 5 via interpreter");
        }
      }
    }
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
