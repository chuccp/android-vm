// 泛型反射面 + java.lang.ref×ThreadLocal 压力用例 harness（tools/generics_ref_tests.sh）。
// 引导链：RegisterBuiltins + runtime.dex boot + generics_ref.dex app loader；
// 逐探针调用 static String run()，按行判定 "name: OK"/"name: FAIL ..."。
// 覆盖缺口：
//   1) Class.getGenericSuperclass/getGenericInterfaces/getTypeParameters
//      （dex Signature 属性解析 → lib Type 镜像）
//   2) ClassCastException: SoftReference -> ThreadLocal（ThreadLocalMap$Entry
//      extends WeakReference 链布局混型复现/回归）
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
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

VMethod* FindStaticRun(VClass* klass) {
  for (auto& m : klass->direct_methods) {
    if (m.name == "run" && m.IsStatic() && m.param_types.empty() &&
        m.return_type == "Ljava/lang/String;") {
      return &m;
    }
  }
  for (auto& m : klass->virtual_methods) {
    if (m.name == "run" && m.IsStatic() && m.param_types.empty() &&
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

// 判定探针输出：任何一行含 ": FAIL" 记失败；无输出/无 OK 行也算失败
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

}  // namespace

int main(int argc, char** argv) {
  std::printf("== generics/ref smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex = argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/generics_ref.dex";

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
      {"generics", "Lgenerictest/GenericsProbe;"},
      {"ref-stress", "Lgenerictest/RefProbe;"},
      {"ref-inherit", "Ljava/lang/RefMapProbe;"},
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
      std::printf("FAIL %s: class init failed: %s\n", p.name, linker.last_error().c_str());
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
    Value r{};
    if (!InvokeMethod(state, run, nullptr, 0, &r)) {
      std::string diag = "invoke failed";
      if (state.HasPendingException() && state.pending_exception != nullptr &&
          state.pending_exception->klass != nullptr) {
        diag = state.pending_exception->klass->descriptor;
        // detailMessage 字段（诊断保真）
        for (VClass* c = state.pending_exception->klass; c != nullptr; c = c->super_class) {
          for (auto& f : c->instance_fields) {
            if (f.name == "detailMessage") {
              ObjHeader* msg = nullptr;
              std::memcpy(&msg,
                          reinterpret_cast<VObject*>(state.pending_exception)->field_data() +
                              f.offset,
                          sizeof(msg));
              if (msg != nullptr) {
                diag += ": " + reinterpret_cast<VString*>(msg)->ToModifiedUtf8();
              }
              break;
            }
          }
        }
      }
      std::printf("FAIL %s: %s\n", p.name, diag.c_str());
      for (size_t bi = 0; bi < state.last_throw_backtrace.size() && bi < 6; bi++) {
        std::printf("    at %s\n", state.last_throw_backtrace[bi].c_str());
      }
      state.ClearException();
      all_ok = false;
      continue;
    }
    std::string text = ResultUtf8(r);
    std::printf("-- %s --\n%s", p.name, text.c_str());
    if (!text.empty() && text.back() != '\n') std::printf("\n");
    int ok_lines = 0;
    if (!Judge(text, &ok_lines)) {
      std::printf("FAIL %s (ok_lines=%d)\n", p.name, ok_lines);
      all_ok = false;
    } else {
      std::printf("PASS %s (%d checks)\n", p.name, ok_lines);
    }
    state.ClearException();
  }

  std::printf("== generics/ref smoke: %s ==\n", all_ok ? "PASS" : "FAIL");
  return all_ok ? 0 : 1;
}
