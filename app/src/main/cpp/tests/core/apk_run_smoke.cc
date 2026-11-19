// M3 阶段 1（收官迁移轮）：target-app 考卷（34 fixture 类）在 VM 里执行。
// boot classpath = runtime.dex（runtime-libs 流水线产物）；
// app loader = target-app-debug.apk（session::Session + LoaderState，BaseDexClassLoader
// 语义：parent=boot；docs 03 章 8.5/10.3、14 章 28.1）。
// 分发契约（target-app/README）：没有 in-APK 注册表——按 (owner 描述符
// Lcom/coke/target/<Class>;, 方法名, JVM 签名) 选择 fixture 方法并断言其返回的
// **哨兵 int**（VM 少实现一个语义就返回错值）。本表取 VM 语义面一组（clinit/
// switch/继承/lambda/异常/集合/原子/数学/随机/时间区域/Java 库面/挂起式 clinit），
// 探针经 RunGuestMain 在 main GuestTask 上下文执行（锁/挂起可用，同 api_matrix
// 先例）。原 builtin 长尾缺口面（String.matches/Field.isSynthetic/
// Method.getAnnotation/Intent.putExtra/JCA provider/泛型超类/IO nativeMap）已
// 闭合，全部翻转为断言（api_matrix GAP 口径归档）。
// 语义保真原则（docs 14 章零壳适配）：不许针对 fixture 类特判，缺口一律按
// Android 语义补内置类/解释器/链接器。
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "session/class_loader.h"
#include "session/session.h"

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
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

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

VMethod* FindMethod(VClass* klass, const char* name, const std::vector<std::string>& params,
                    const char* ret, bool want_static) {
  for (auto* list : {&klass->direct_methods, &klass->virtual_methods}) {
    for (auto& m : *list) {
      if (m.name == name && m.IsStatic() == want_static && m.param_types == params &&
          m.return_type == ret) {
        return &m;
      }
    }
  }
  return nullptr;
}

// ---- 哨兵 int 断言表（target-app 考面：owner 描述符 / 方法 / 签名 / 哨兵） ----
struct Probe {
  const char* owner;
  const char* method;
  std::vector<std::string> params;
  const char* ret;
  bool is_static;
  int32_t expect;
  bool gap;         // true = 已知 VM 缺口面观测（api_matrix g_gaps 口径）
  const char* gap_note;
};

// 代表性哨兵（README 考面清单）：VM 语义面大矩阵 + 反射/Unsafe/加密/数学/随机/
// Intent/时间区域 + Java 库面（正则/StringIO/集合/java.time/Calendar/泛型反射/
// mmap/延迟队列/注解/Objects/数值工具）+ 挂起式 <clinit>。
const Probe kProbes[] = {
    // ---- VM 语义（clinit 计数/对象流/DEX 形状矩阵/集合/原子） ----
    {"Lcom/coke/target/TargetEntry;", "run", {}, "I", true, 42, false, ""},
    {"Lcom/coke/target/RuntimeCapabilities;", "run", {}, "I", true, 283, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "mapFlow", {}, "I", true, 15, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "listLengthFlow", {}, "I", true, 9, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "listPrefixFlow", {}, "I", true, 16, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "concurrentFlow", {}, "I", true, 32, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "atomicFlow", {}, "I", true, 192, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "arrayFlow", {}, "I", true, 5, false, ""},
    {"Lcom/coke/target/RuntimeCollections;", "overlapCopyFlow", {}, "I", true, 193, false, ""},
    // ---- 反射 / Unsafe / 加密 / 数学 / 随机 / Intent（长尾面；缺口已闭合翻断言） ----
    {"Lcom/coke/target/RuntimeReflectionFixture;", "reflectionFlow", {}, "I", false, 93, false, ""},
    {"Lcom/coke/target/RuntimeUnsafeFixture;", "unsafeFlow", {}, "I", false, 94, false, ""},
    {"Lcom/coke/target/RuntimeDigestFixture;", "digestFlow", {}, "I", false, 95, false, ""},
    {"Lcom/coke/target/RuntimeBigIntegerFixture;", "bigIntegerFlow", {}, "I", false, 96, false, ""},
    {"Lcom/coke/target/RuntimeRandomFixture;", "randomFlow", {}, "I", false, 97, false, ""},
    {"Lcom/coke/target/RuntimeIntentFixture;", "intentFlow", {}, "I", false, 98, false, ""},
    {"Lcom/coke/target/RuntimeTimeLocaleFixture;", "timeLocaleFlow", {}, "I", false, 99, false, ""},
    // ---- Java 库面（RuntimeLibraryCompatibilityFixture 纯 JVM 侧） ----
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "regexFlow", {}, "I", false, 101, false,
     ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "stringReaderWriterFlow", {}, "I",
     false, 102, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "localDateFlow", {}, "I", false, 104,
     false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "calendarFlow", {}, "I", false, 105,
     false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "mapFlow", {}, "I", false, 106, false,
     ""},
    // ---- 挂起式 <clinit>（协作调度保返回值） ----
    {"Lcom/coke/target/RuntimeSuspendingInitFixture;", "flow", {}, "I", false, 107, false, ""},
    {"Lcom/coke/target/RuntimeSuspendingEntryFixture;", "flow", {}, "I", false, 108, false, ""},
    // ---- 泛型反射 / mmap / 延迟队列 / 注解 / Objects / 数值工具（缺口已闭合翻断言） ----
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "arrayAndGenericReflectionFlow", {},
     "I", false, 109, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "mappedBufferFlow", {}, "I", false,
     110, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "delayedQueueFlow", {}, "I", false,
     111, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "methodAnnotationFlow", {}, "I", false,
     112, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "objectsAndModifierFlow", {}, "I",
     false, 113, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "androidValueLibraryFlow", {}, "I",
     false, 114, false, ""},
    {"Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "valueUtilitiesFlow", {}, "I", false,
     115, false, ""},
};

std::string DefaultPath(const char* rel) {
  // 宿主脚本以仓库根为 cwd 传参；默认值兜底用相对路径
  return std::string(rel);
}

struct ProbeRun {
  VMState* state = nullptr;
  ClassLinker* linker = nullptr;
  VObject* app_loader = nullptr;
  const Probe* probe = nullptr;
  int32_t got = 0;
  std::string diag;
};

bool ProbeMain(VMState& state, void* raw) {
  ProbeRun* p = static_cast<ProbeRun*>(raw);
  const Probe& probe = *p->probe;
  VClass* k = p->linker->FindClass(&state, p->app_loader, probe.owner);
  if (k == nullptr) {
    p->diag = "class not found: " + std::string(probe.owner);
    return false;
  }
  if (!p->linker->InitializeClass(k, &state)) {
    p->diag = "clinit failed: " + ExceptionDiag(state);
    state.ClearException();
    return false;
  }
  VMethod* m = FindMethod(k, probe.method, probe.params, probe.ret, probe.is_static);
  if (m == nullptr) {
    p->diag = "method not found: " + std::string(probe.method);
    return false;
  }
  Value self_v{};
  const Value* args = nullptr;
  size_t argc = 0;
  if (!probe.is_static) {
    VObject* self = p->linker->AllocObject(k);
    if (self == nullptr) {
      p->diag = "alloc failed";
      return false;
    }
    // 构造器 <init>：无参调用（无码骨架 = 空实现）
    VMethod* ctor = FindMethod(k, "<init>", {}, "V", false);
    if (ctor != nullptr) {
      Value cself{};
      cself.ref = self;
      Value cr{};
      if (!InvokeMethod(state, ctor, &cself, 1, &cr)) {
        p->diag = "ctor failed: " + ExceptionDiag(state);
        state.ClearException();
        return false;
      }
    }
    self_v.ref = self;
    args = &self_v;
    argc = 1;
  }
  Value r{};
  if (!InvokeMethod(state, m, args, argc, &r)) {
    p->diag = ExceptionDiag(state);
    state.ClearException();
    return false;
  }
  p->got = r.i;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== apk run smoke (target-app sentinel table) ==\n");

  const std::string runtime_dex = argc > 1 ? argv[1] : DefaultPath("runtime-libs/build/runtime-dex/runtime.dex");
  const std::string target_apk = argc > 2 ? argv[2] : DefaultPath("target-app/build/outputs/apk/debug/target-app-debug.apk");

  // ---- 引导 VM ----
  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);  // Intent/Bundle 等 fixture 依赖面
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  std::string err;

  // ---- boot classpath：runtime.dex（Session::Open 单 .dex → AddBootDexFile）----
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;
  {
    auto boot = dexvm::session::Session::Open(runtime_dex, &err);
    CHECK(boot != nullptr && !boot->dex_blobs().empty(), "open runtime.dex");
    if (boot == nullptr) {
      std::printf("  error: %s\n", err.c_str());
      return 1;
    }
    for (const auto& blob : boot->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse boot dex: " + blob.name).c_str());
      if (dex_file == nullptr) {
        std::printf("  error: %s\n", derr.c_str());
        return 1;
      }
      linker.AddBootDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    std::printf("boot dex files: %zu (runtime.dex)\n", boot->dex_blobs().size());
  }

  // ---- app loader：target-app APK（session::Session + LoaderState；BaseDexClassLoader
  // 语义 parent=boot = loaders().default_app()）→ AddAppDexFile/DefineClass 绑定 loader ----
  VObject* app_loader = nullptr;
  {
    auto app = dexvm::session::Session::Open(target_apk, &err);
    CHECK(app != nullptr && !app->dex_blobs().empty(), "open target-app-debug.apk");
    if (app == nullptr) {
      std::printf("  error: %s\n", err.c_str());
      return 1;
    }
    for (const auto& blob : app->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse app dex: " + blob.name).c_str());
      if (dex_file == nullptr) {
        std::printf("  error: %s\n", derr.c_str());
        return 1;
      }
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    std::printf("app dex files: %zu (target APK)\n", app->dex_blobs().size());

    // app loader 对象绑定 default_app_ LoaderState（parent=boot，DexPathList 顺序）
    VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
    app_loader = linker.AllocObject(cl);
    CHECK(app_loader != nullptr, "alloc app ClassLoader object");
    linker.loaders().BindObject(app_loader, linker.loaders().default_app());
    CHECK(linker.loaders().Lookup(app_loader) == linker.loaders().default_app(),
          "app loader bound to app LoaderState (parent=boot)");
  }

  // ---- docs 03 章 8.4：VM 内建优先，runtime.dex 不得遮蔽 ----
  {
    VClass* str = linker.FindClass(&state, app_loader, "Ljava/lang/String;");
    CHECK(str != nullptr && str->dex_file == nullptr,
          "java.lang.String: VM builtin wins over runtime.dex");
    VClass* sb = linker.FindClass(&state, app_loader, "Ljava/lang/StringBuilder;");
    CHECK(sb != nullptr && sb->dex_file == nullptr,
          "java.lang.StringBuilder: VM builtin wins (runtime.dex 不得遮蔽)");
  }

  // ---- 哨兵 int 断言表（34 类分发契约；main GuestTask 上下文执行）----
  int g_gaps = 0;  // 已知 VM 缺口观测（api_matrix 口径：不计 FAIL）
  for (const Probe& probe : kProbes) {
    ProbeRun run;
    run.state = &state;
    run.linker = &linker;
    run.app_loader = app_loader;
    run.probe = &probe;
    bool ok = dexvm::runtime::RunGuestMainImpl(state, ProbeMain, &run);
    std::string label = std::string(probe.owner) + "." + probe.method;
    bool pass = ok && run.got == probe.expect;
    if (probe.gap) {
      if (pass) {
        std::printf("GAP FIXED?: %s == %d（%s）——翻转为断言\n", label.c_str(), probe.expect,
                    probe.gap_note);
        g_gaps++;
      } else {
        std::printf("GAP: %s（%s）", label.c_str(), probe.gap_note);
        if (!ok && !run.diag.empty()) std::printf(" diag: %s", run.diag.c_str());
        if (ok) std::printf(" got %d expect %d", run.got, probe.expect);
        std::printf("\n");
        g_gaps++;
      }
      continue;
    }
    if (!ok && !run.diag.empty()) {
      std::printf("  diag: %s -> %s\n", label.c_str(), run.diag.c_str());
    }
    CHECK(ok, (std::string("invoke ") + label).c_str());
    if (ok) {
      bool exact = run.got == probe.expect;
      if (!exact) {
        std::printf("  %s -> %d (expect %d)\n", label.c_str(), run.got, probe.expect);
      }
      CHECK(exact, (label + " == " + std::to_string(probe.expect)).c_str());
    }
  }

  std::printf("gap observations: %d\n", g_gaps);
  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
