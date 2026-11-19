// java.net socket native 面 harness（tools/net_tests.sh）。
// 引导链：RegisterBuiltins + runtime.dex boot + net.dex app loader；
// 逐探针调用 static String run()，按行判定 "name: OK"/"name: FAIL ..."。
// 覆盖缺口（Q2 缝：core/builtins.cc java.net POSIX 直通）：
//   1) DNS 解析面（InetAddress.getByName/getAllByName → getaddrinfo 直通；
//      字面量/未知主机）
//   2) socket 回环 echo（create/connect/bind/listen/accept/read/write/close）
//   3) socket 选项面（TCP_NODELAY/SO_TIMEOUT/SO_REUSEADDR/SO_KEEPALIVE）
//   4) available/shutdownOutput 语义
//   5) fetch 收敛语义（失败轮 ConnectException + 成功轮 HTTP/1.0 mock server）
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
  std::printf("== net smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex = argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/net.dex";

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

  bool all_ok = true;
  VClass* k = linker.FindClass("Lnettest/NetProbe;");
  if (k == nullptr) {
    std::printf("FAIL netprobe: class not found: %s\n", linker.last_error().c_str());
    return 1;
  }
  if (!linker.InitializeClass(k, &state) || state.HasPendingException()) {
    std::printf("FAIL netprobe: class init failed: %s\n", linker.last_error().c_str());
    return 1;
  }
  VMethod* run = FindStaticRun(k);
  if (run == nullptr) {
    std::printf("FAIL netprobe: static run() not found\n");
    return 1;
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
    std::printf("FAIL netprobe: %s\n", diag.c_str());
    for (size_t bi = 0; bi < state.last_throw_backtrace.size() && bi < 8; bi++) {
      std::printf("    at %s\n", state.last_throw_backtrace[bi].c_str());
    }
    return 1;
  }
  std::string text = ResultUtf8(r);
  std::printf("-- netprobe --\n%s", text.c_str());
  if (!text.empty() && text.back() != '\n') std::printf("\n");
  int ok_lines = 0;
  if (!Judge(text, &ok_lines)) {
    std::printf("FAIL netprobe (ok_lines=%d)\n", ok_lines);
    all_ok = false;
  } else {
    std::printf("PASS netprobe (%d checks)\n", ok_lines);
  }

  std::printf("== net smoke: %s ==\n", all_ok ? "PASS" : "FAIL");
  return all_ok ? 0 : 1;
}
