// Context 文件/存储面 + Proxy 面 harness（tools/context_file_tests.sh）。
// 引导链：RegisterBuiltins + RegisterFrameworkBuiltins + runtime.dex boot +
// context.dex app loader；数据根 = SetContextDataDir(argv[3])（AOSP app data
// dir 语义，测试自设临时根）。
// 逐探针类调用 static String run()，按行判定 "name: OK"/"name: FAIL ..."。
// 覆盖（core/framework_builtins.cc B 档方法面 + core/builtins.cc Proxy 缝）：
//   1) getDataDir/getFilesDir/getCacheDir/getCodeCacheDir/getNoBackupFilesDir
//      路径自洽（filesDir.parent == dataDir）+ 目录取用即建
//   2) File 面读写（FileOutputStream/FileInputStream 长度/字节自洽）
//   3) getDir/getDatabasePath/getDatabaseNames/deleteDatabase/
//      getSharedPreferencesPath/move*From 族语义
//   4) Proxy 分发（装箱/拆箱/void/null）、Object 公共面路由、元面幂等、
//      handler 抛出穿出、Method.invoke(target) 虚派发委托链
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
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

std::string ExceptionDiag(VMState& state) {
  std::string diag = "invoke failed";
  if (state.HasPendingException() && state.pending_exception != nullptr &&
      state.pending_exception->klass != nullptr) {
    diag = state.pending_exception->klass->descriptor;
    for (VClass* c = state.pending_exception->klass; c != nullptr; c = c->super_class) {
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
  return diag;
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

bool RunProbe(VMState& state, ClassLinker& linker, const char* desc, bool* all_ok) {
  VClass* k = linker.FindClass(desc);
  if (k == nullptr) {
    std::printf("FAIL %s: class not found: %s\n", desc, linker.last_error().c_str());
    *all_ok = false;
    return false;
  }
  if (!linker.InitializeClass(k, &state) || state.HasPendingException()) {
    std::printf("FAIL %s: class init failed: %s\n", desc, linker.last_error().c_str());
    *all_ok = false;
    return false;
  }
  VMethod* run = FindStaticRun(k);
  if (run == nullptr) {
    std::printf("FAIL %s: static run() not found\n", desc);
    *all_ok = false;
    return false;
  }
  Value r{};
  if (!InvokeMethod(state, run, nullptr, 0, &r)) {
    std::printf("FAIL %s: %s\n", desc, ExceptionDiag(state).c_str());
    for (size_t bi = 0; bi < state.last_throw_backtrace.size() && bi < 8; bi++) {
      std::printf("    at %s\n", state.last_throw_backtrace[bi].c_str());
    }
    *all_ok = false;
    return false;
  }
  std::string text = ResultUtf8(r);
  std::printf("-- %s --\n%s", desc, text.c_str());
  if (!text.empty() && text.back() != '\n') std::printf("\n");
  // 最近一次 guest 抛出栈（探针内 catch 也留痕；FAIL 定位用）
  if (text.find(": FAIL") != std::string::npos) {
    for (size_t bi = 0; bi < state.last_throw_backtrace.size() && bi < 10; bi++) {
      std::printf("    thrown-at %s\n", state.last_throw_backtrace[bi].c_str());
    }
  }
  int ok_lines = 0;
  if (!Judge(text, &ok_lines)) {
    std::printf("FAIL %s (ok_lines=%d)\n", desc, ok_lines);
    *all_ok = false;
  } else {
    std::printf("PASS %s (%d checks)\n", desc, ok_lines);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== context_file smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex = argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/context.dex";
  const std::string data_dir = argc > 3 ? argv[3] : "/tmp/dexvm_ctx_face_data";

  // 数据根（AOSP app data dir 语义；测试自设临时根，路径自洽断言基于此）
  dexvm::runtime::SetContextDataDir(data_dir);
  std::printf("data dir: %s\n", dexvm::runtime::GetContextDataDir().c_str());

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

  bool all_ok = true;
  // Context 服务桥单例（宿主测试自设占位 PM/ApplicationInfo；探针断言同单例语义）
  {
    VClass* obj_cls = linker.FindClass("Ljava/lang/Object;");
    VObject* pm = obj_cls != nullptr ? linker.AllocObject(obj_cls) : nullptr;
    VObject* ai = obj_cls != nullptr ? linker.AllocObject(obj_cls) : nullptr;
    if (pm != nullptr) {
      std::memset(pm->field_data(), 0, obj_cls->object_size - sizeof(dexvm::runtime::ObjHeader));
    }
    if (ai != nullptr) {
      std::memset(ai->field_data(), 0, obj_cls->object_size - sizeof(dexvm::runtime::ObjHeader));
    }
    dexvm::runtime::SetContextPackageManager(pm);
    dexvm::runtime::SetContextApplicationInfo(ai);
  }
  RunProbe(state, linker, "Lcontexttest/CtxFaceProbe;", &all_ok);
  RunProbe(state, linker, "Lcontexttest/ProxyFaceProbe;", &all_ok);

  std::printf("== context_file smoke: %s ==\n", all_ok ? "PASS" : "FAIL");
  return all_ok ? 0 : 1;
}
