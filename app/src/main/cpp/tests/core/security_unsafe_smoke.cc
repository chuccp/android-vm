// java.security 链 + Unsafe 对象操作族 + java.io 文件面 harness
//（tools/security_unsafe_tests.sh）。引导链：RegisterBuiltins + runtime.dex
// boot + security_unsafe.dex app loader；逐探针调用 static String run()，
// 按行判定 "name: OK"/"name: FAIL ..."。stderr 全程截获（System.err 输出
// 语义：Debug.println 面须落 "Provider: ..." 行）。
// 覆盖缺口：
//   1) sun.security.util.Debug（SecureRandom.<clinit> pdebug 面）
//   2) SecureRandom -> Providers/SHA1PRNG -> SeedGenerator /dev/urandom
//   3) jdk.internal.misc.Unsafe 对象操作族（putObjectVolatile/getObjectVolatile/
//      compareAndSetObject/getAndSetObject/putOrderedObject + Int/Long 镜像）
//   4) java.io.FileInputStream/FileDescriptor native 缝（/dev/urandom 直读）
#include <fcntl.h>
#include <unistd.h>

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

std::string ReadFileText(const char* path) {
  std::string out;
  FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return out;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== security/unsafe smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/security_unsafe.dex";

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

  // stderr 截获（Debug.println -> System.err -> 宿主 stderr）
  const char* capture_path = "/tmp/dexvm_security_unsafe_err.txt";
  std::fflush(stderr);
  int saved_stderr = dup(2);
  int cap_fd = open(capture_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (cap_fd >= 0) {
    dup2(cap_fd, 2);
    close(cap_fd);
  }

  struct ProbeRun {
    const char* name;
    const char* desc;
  } kProbes[] = {
      {"debug", "Lsectest/DebugProbe;"},
      {"fileio", "Lsectest/FileIOProbe;"},
      {"unsafe-obj", "Lsectest/UnsafeObjectProbe;"},
      {"sec-random", "Lsectest/SecureRandomProbe;"},
      {"sb-cs-range", "Lsectest/SbProbe;"},
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

  // 收 stderr 截获
  std::fflush(stderr);
  if (saved_stderr >= 0) {
    dup2(saved_stderr, 2);
    close(saved_stderr);
  }
  std::string captured = ReadFileText(capture_path);
  std::printf("-- System.err capture --\n%s", captured.c_str());
  if (captured.find("Provider: hello-debug-line") == std::string::npos) {
    std::printf("FAIL debug-stderr: missing \"Provider: hello-debug-line\"\n");
    all_ok = false;
  } else {
    std::printf("PASS debug-stderr (System.err 输出语义)\n");
  }

  std::printf("== security/unsafe smoke: %s ==\n", all_ok ? "PASS" : "FAIL");
  return all_ok ? 0 : 1;
}
