// 「内建注册集 == 派生集」一致性断言（防再犯核心，单一事实源）：
// 派生集 = core/builtin_classes.def（X 宏清单，构建打包排除集与注册面同源）；
// 注册集 = RegisterBuiltins/RegisterFrameworkBuiltins 实际 RegisterBuiltinClass
// 的类名集（ClassLinker 空实例上的 all_classes()）。双向比对，任一方向漂移
// （注册了清单外类 / 清单内类未注册）即 FAIL。
// 由 tools/runtime_coverage_tests.sh 编译并运行。
#include <cstdio>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "memory/heap.h"

namespace {

int g_failures = 0;

void Check(bool cond, const char* msg) {
  if (!cond) {
    std::printf("FAIL: %s\n", msg);
    g_failures++;
  } else {
    std::printf("PASS: %s\n", msg);
  }
}

}  // namespace

int main() {
  using dexvm::runtime::BuiltinClassManifest;
  using dexvm::runtime::ClassLinker;
  using dexvm::runtime::CoreBuiltinClassManifest;
  using dexvm::runtime::FrameworkBuiltinClassManifest;
  using dexvm::runtime::InternTable;
  using dexvm::runtime::RegisterBuiltins;
  using dexvm::runtime::RegisterFrameworkBuiltins;
  using dexvm::runtime::VerifyBuiltinClassSet;

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);

  // 注册面内部自带 AssertBuiltinRegistration（不一致即 abort）；
  // 此处再做全量并集断言（内建注册集 == 派生集）。
  RegisterBuiltins(&linker);
  std::vector<std::string> registered;
  for (auto* k : linker.all_classes()) registered.push_back(k->descriptor);
  std::string error;
  Check(VerifyBuiltinClassSet(registered, CoreBuiltinClassManifest(), &error),
        "core registered set == core manifest (builtin_classes.def)");
  if (!error.empty()) std::printf("%s", error.c_str());

  RegisterFrameworkBuiltins(&linker);
  registered.clear();
  for (auto* k : linker.all_classes()) registered.push_back(k->descriptor);
  error.clear();
  Check(VerifyBuiltinClassSet(registered, BuiltinClassManifest(), &error),
        "full registered set == full manifest (builtin_classes.def)");
  if (!error.empty()) std::printf("%s", error.c_str());

  // 清单类均已装入 class table（FindClass 可达）
  const std::vector<std::string>& manifest = BuiltinClassManifest();
  int missing = 0;
  for (const std::string& desc : manifest) {
    if (linker.FindClass(desc) == nullptr) {
      std::printf("FAIL: manifest class not in class table: %s\n", desc.c_str());
      missing++;
    }
  }
  Check(missing == 0, "all manifest classes reachable via FindClass");

  std::printf("builtin_manifest_smoke: %s (registered=%zu manifest=%zu core=%zu framework=%zu)\n",
              g_failures == 0 ? "PASS" : "FAIL", registered.size(), manifest.size(),
              CoreBuiltinClassManifest().size(), FrameworkBuiltinClassManifest().size());
  return g_failures == 0 ? 0 : 1;
}
