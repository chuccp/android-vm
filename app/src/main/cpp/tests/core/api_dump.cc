// 内建 API 面 dump（tools/api_stub_gen.py 的输入之一）：
// 引导 RegisterBuiltins + RegisterFrameworkBuiltins（不加载 runtime.dex），
// 枚举全部内建类的方法面，按 "Ldesc; name(params)ret" 行输出。
// 用途：与 android.jar 的 javap 面做差集，批量生成缺失方法的可调用面桩。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"

using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;

int main() {
  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);

  for (VClass* k : linker.all_classes()) {
    if (k == nullptr || k->dex_file != nullptr) continue;  // 只要内建（非 dex 类）
    std::printf("CLASS %s\n", k->descriptor.c_str());
    auto emit = [&](const VMethod& m) {
      std::string params;
      for (size_t i = 0; i < m.param_types.size(); i++) {
        if (i > 0) params += ",";
        params += m.param_types[i];
      }
      // 第三段标记：N = intrinsic/native 实现（VM 缝），C = dex 代码实现
      const char* impl = m.native_fn != nullptr || m.jni_native_fn != nullptr ? "N" : "C";
      std::printf("  %s %s %s(%s)%s\n", m.IsStatic() ? "S" : "I", impl, m.name.c_str(),
                  params.c_str(), m.return_type.c_str());
    };
    for (const auto& m : k->direct_methods) emit(m);
    for (const auto& m : k->virtual_methods) emit(m);
  }
  return 0;
}
