// "壳形态" JNI 语义宿主 harness（tools/jni_shell_tests.sh）。
// 引导链：RegisterBuiltins + runtime.dex boot + jni_shell.dex app loader +
// JavaVMExt/JNIEnvExt（JNI trampoline 接线）+ LibraryLoadHook（guest System.load
// -> NativeManager.LoadNativeLibrary）。
// 覆盖面参考启发自 v1 hardened_fixture.cpp/HardenedNativeFixture.java/
// GuestNativeLibraryTest.kt（构造 cookie、TracerPid、Rolling-XOR、ThrowNew、
// RegisterNatives、CallStaticVoidMethod 回调、dladdr 完整性、真实商业 .so 加载）；
// 断言判据 = Android/JNI 语义。
//   1) guest check* 语义面（System.load -> JNI_OnLoad -> register/RegisterNatives
//      -> mixed/decrypted/transform/null-throw/throw/callback/integrity/tracer）
//   2) 宿主 dlsym 观测（g_onload_calls、payload 构造计数）
//   3) 可选真实 .so 加载面（libbangcle_crypto_tool.so 形态；缺省 SKIP）
// 用法: jni_shell_smoke <fixture.dex> <libshell_fixture.so> [real_vendor.so]
#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "jni/jni_env.h"
#include "jni/native_manager.h"
#include "memory/heap.h"
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

#define CHECK(cond, msg)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      g_failures++;                                               \
    } else {                                                      \
      std::printf("PASS: %s\n", msg);                             \
    }                                                             \
  } while (0)

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

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
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

// ---- LibraryLoadHook：guest System.load(path) -> NativeManager ----
dexvm::jni::JavaVMExt* g_hook_vm = nullptr;
dexvm::jni::JNIEnvExt* g_hook_env = nullptr;

bool HookLoad(const std::string& name, std::string* error) {
  if (g_hook_vm == nullptr || g_hook_env == nullptr) {
    if (error != nullptr) *error = "native loader not wired";
    return false;
  }
  return g_hook_vm->natives.LoadNativeLibrary(g_hook_env, name, nullptr, nullptr,
                                              error);
}

void RunCheck(VMState& state, ClassLinker& linker, VClass* k, const char* method,
              std::vector<std::string> params, Value* args,
              const char* expect) {
  VMethod* m = FindStatic(k, method, std::move(params), "Ljava/lang/String;");
  if (m == nullptr) {
    CHECK(false, (std::string("find ") + method).c_str());
    return;
  }
  Value r{};
  std::printf("-- %s --\n", method);
  if (!InvokeMethod(state, m, args, args != nullptr ? 1u : 0u, &r)) {
    std::printf("  diag: %s\n", ExceptionDiag(state).c_str());
    state.ClearException();
    CHECK(false, (std::string("invoke ") + method).c_str());
    return;
  }
  std::string got = ResultUtf8(r);
  if (got != expect) std::printf("  got %s (expect %s)\n", got.c_str(), expect);
  CHECK(got == expect, (std::string(method) + " == " + expect).c_str());
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("== jni shell smoke ==\n");

  const std::string fixture_dex =
      argc > 1 ? argv[1] : "app/src/main/cpp/tests/out/jni_shell.dex";
  const std::string so_path = argc > 2 ? argv[2] : "";
  const std::string real_so_path = argc > 3 ? argv[3] : "";
  if (so_path.empty()) {
    std::printf("FAIL: no libshell_fixture.so argument\n");
    return 1;
  }

  std::vector<uint8_t> dex_bytes;
  {
    std::FILE* f = std::fopen(fixture_dex.c_str(), "rb");
    if (f == nullptr) {
      std::printf("FAIL: cannot read fixture dex: %s\n", fixture_dex.c_str());
      return 1;
    }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    dex_bytes.resize(static_cast<size_t>(n));
    if (n > 0 && std::fread(dex_bytes.data(), 1, dex_bytes.size(), f) != dex_bytes.size()) {
      std::fclose(f);
      std::printf("FAIL: short read fixture dex\n");
      return 1;
    }
    std::fclose(f);
  }

  // boot classpath：runtime.dex（guest java.lang 面）
  std::string runtime_dex = "runtime-libs/build/runtime-dex/runtime.dex";
  std::string err;
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

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
    std::string derr;
    auto app = dexvm::dex::DexFile::Open(dex_bytes.data(), dex_bytes.size(), &derr);
    if (app == nullptr) {
      std::printf("FAIL: parse app dex: %s\n", derr.c_str());
      return 1;
    }
    linker.AddAppDexFile(app.get());
    owned_dex.push_back(std::move(app));
  }

  // JNI 接线（trampoline + native 库加载）
  dexvm::jni::JavaVMExt vm;
  dexvm::jni::InitJavaVMExt(&vm);
  dexvm::jni::JNIEnvExt env;
  dexvm::jni::InitJNIEnvExt(&env, &vm, &state);
  g_hook_vm = &vm;
  g_hook_env = &env;
  dexvm::runtime::SetLibraryLoadHook(&HookLoad);

  VClass* checks = linker.FindClass("Lshelltest/ShellChecks;");
  CHECK(checks != nullptr, "find ShellChecks");
  if (checks == nullptr) {
    std::printf("  error: %s\n", linker.last_error().c_str());
    return 1;
  }
  CHECK(linker.InitializeClass(checks, &state), "ShellChecks <clinit>");

  // ---- 1. guest check*（checkLoad 先行）----
  {
    // checkLoad(String)：so 路径以实参传入
    VMethod* m = FindStatic(checks, "checkLoad", {"Ljava/lang/String;"},
                            "Ljava/lang/String;");
    CHECK(m != nullptr, "find checkLoad");
    if (m != nullptr) {
      std::printf("-- checkLoad --\n");
      Value args[1]{};
      std::u16string u16path;
      for (char c : so_path) u16path.push_back(static_cast<char16_t>(c));
      args[0].ref = linker.AllocString(u16path);
      Value r{};
      bool ok = InvokeMethod(state, m, args, 1, &r);
      std::string got = ok ? ResultUtf8(r) : "<failed>";
      if (!ok) std::printf("  diag: %s\n", ExceptionDiag(state).c_str());
      state.ClearException();
      CHECK(ok && got == "load:ok", "checkLoad == load:ok");
    }
  }

  RunCheck(state, linker, checks, "checkMixed", {}, nullptr, "mixed:ok");
  RunCheck(state, linker, checks, "checkDecrypted", {}, nullptr, "decrypted:ok");
  RunCheck(state, linker, checks, "checkTransform", {}, nullptr, "transform:ok");
  RunCheck(state, linker, checks, "checkNullTransform", {}, nullptr,
           "transformNull:ok");
  RunCheck(state, linker, checks, "checkFailThrows", {}, nullptr, "throw:ok");
  RunCheck(state, linker, checks, "checkCallback", {}, nullptr, "callback:ok");
  RunCheck(state, linker, checks, "checkIntegrity", {}, nullptr, "integrity:ok");
  RunCheck(state, linker, checks, "checkTracerProbe", {}, nullptr,
           "tracerProbe:ok");

  // ---- 2. 宿主 dlsym 观测 ----
  {
    void* th = dlopen(so_path.c_str(), RTLD_NOW);
    CHECK(th != nullptr, "test-side dlopen shell fixture");
    if (th != nullptr) {
      int* calls = static_cast<int*>(dlsym(th, "g_onload_calls"));
      CHECK(calls != nullptr && *calls == 1, "JNI_OnLoad ran exactly once");
      dlclose(th);
    }
    // payload 构造计数（跨库依赖：libshell_payload.so 与 fixture 同目录）
    std::string payload_path = so_path;
    size_t slash = payload_path.rfind('/');
    payload_path = (slash == std::string::npos ? std::string(".") : payload_path.substr(0, slash)) +
                   "/libshell_payload.so";
    void* ph = dlopen(payload_path.c_str(), RTLD_NOW);
    CHECK(ph != nullptr, "test-side dlopen shell payload");
    if (ph != nullptr) {
      int (*inits)() =
          reinterpret_cast<int (*)()>(dlsym(ph, "shell_payload_init_calls"));
      CHECK(inits != nullptr && inits() == 1, "payload constructor ran exactly once");
      int (*alive)() = reinterpret_cast<int (*)()>(dlsym(ph, "shell_alive_7f"));
      CHECK(alive != nullptr && alive() == 1, "payload cookie alive");
      dlclose(ph);
    }
  }

  // ---- 3. 可选真实 .so 加载面（libbangcle_crypto_tool.so 形态）----
  if (real_so_path.empty()) {
    std::printf(
        "NOTE: real vendor .so not provided (libbangcle_crypto_tool.so 形态用例 "
        "SKIP；提供路径即可启用真实商业 .so JNI_OnLoad 加载断言)\n");
  } else {
    std::string load_err;
    bool ok = vm.natives.LoadNativeLibrary(&env, real_so_path, nullptr, nullptr,
                                           &load_err);
    CHECK(ok, "real vendor .so loads (JNI_OnLoad chain)");
    if (!ok) std::printf("  error: %s\n", load_err.c_str());
    dexvm::jni::SharedLibrary* lib = vm.natives.Find(real_so_path);
    CHECK(lib != nullptr && lib->on_load_ok && lib->on_load_version > 0,
          "real vendor .so JNI_OnLoad version > 0");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
