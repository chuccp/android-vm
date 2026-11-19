// M3 加固考卷：知乎 APK 语义保真验证 harness（零壳适配）。
// 引导链照抄 tests/core/lifecycle_smoke.cc：RegisterBuiltins +
// RegisterFrameworkBuiltins + runtime.dex boot + 考卷 APK app loader +
// ConfigFromManifest -> LifecycleDriver::Run 驱动启动链。
// 逐 phase 记录（phase 名/耗时/结果/卡点诊断：缺类/缺方法/Unsupported/
// 异常类:消息）+ 抛出瞬间 guest 栈快照（VMState::last_throw_backtrace），
// 结构化缺口报告（每条：guest 类.方法/字段、调用方、缺口类型、建议修法）。
// 断言底线：至少进入 application.attachBaseContext（壳链）。
// 报告输出 stdout + tests/out/zhihu_exam_report.txt。
// 不识别壳、不写壳特判；失败归因只有两类：语义缺口 / 宿主特权边界。
#include <dlfcn.h>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "framework/lifecycle_driver.h"
#include "session/class_loader.h"
#include "session/session.h"

namespace {

using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::framework::PhaseRecord;
using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

// ---- 宿主 native 库加载语义（docs 07 章 16.2 宿主侧）：nativeLibraryDir 查找
// + dlopen。目标 ABI 库在宿主不可执行（如 macOS 无法 dlopen Android ARM64 ELF）
// 时如实返回失败——宿主特权边界，记录不伪造（docs 29.2：留待真机阶段验证）。----
std::string g_native_lib_dir;

bool ExamLibraryLoader(const std::string& name, std::string* error) {
  std::vector<std::string> candidates;
  if (name.find('/') != std::string::npos) {
    candidates.push_back(name);
  } else {
    if (!g_native_lib_dir.empty()) candidates.push_back(g_native_lib_dir + "/" + name);
    candidates.push_back(name);
  }
  std::string all_err;
  for (const std::string& c : candidates) {
    void* h = dlopen(c.c_str(), RTLD_NOW);
    if (h == nullptr) {
      const char* e = dlerror();
      if (!all_err.empty()) all_err += "; ";
      all_err += (e != nullptr) ? std::string(e) : ("dlopen failed: " + c);
      continue;
    }
    // 映射成功：JNI_OnLoad 需要 JNIEnv（jni 模块），考试构建未启用——
    // loadLibrary 语义（加载 + JNI_OnLoad）未完成，如实失败（不伪造）。
    dlclose(h);
    *error = "library mapped but JNI_OnLoad needs JNIEnv (exam build without jni module): " + c;
    return false;
  }
  *error = all_err.empty() ? ("couldn't find " + name) : all_err;
  return false;
}

// ---- 缺口分类 ----

enum class GapType {
  kMissingClass,
  kMissingMethod,
  kMissingMethodBody,
  kMissingField,
  kUnsupported,
  kException,
  kNativeLib,
  kOther,
};

const char* GapTypeName(GapType t) {
  switch (t) {
    case GapType::kMissingClass: return "missing-class";
    case GapType::kMissingMethod: return "missing-method";
    case GapType::kMissingMethodBody: return "missing-method-body";
    case GapType::kMissingField: return "missing-field";
    case GapType::kUnsupported: return "unsupported";
    case GapType::kException: return "exception";
    case GapType::kNativeLib: return "native-lib";
    default: return "other";
  }
}

struct Gap {
  GapType type = GapType::kOther;
  std::string target;   // guest 类.方法/字段（descriptor/签名文本）
  std::string phase;    // 生命周期 phase
  std::string caller;   // 调用方（guest 栈顶或 phase）
  std::vector<std::string> stack;  // guest 栈快照（抛出瞬间）
  std::string diagnosis;  // 异常类:消息 / 原始错误
  std::string fix;      // 建议修法（语义归因，非壳适配）
};

// jre-excluded 黑名单（runtime-libs/jre-excluded.txt：VM 内建职责）
bool IsJreExcluded(const std::string& desc) {
  static const char* kList[] = {
      "Ljava/lang/Object;",       "Ljava/lang/Class;",
      "Ljava/lang/ClassLoader;",  "Ljava/lang/Thread;",
      "Ljava/lang/ThreadGroup;",  "Ljava/lang/Throwable;",
      "Ljava/lang/System;",       "Ljava/lang/Runtime;",
      "Ljava/lang/Enum;",         "Ljava/lang/reflect/Method;",
      "Ljava/lang/reflect/Constructor;", "Ljava/lang/reflect/Proxy;",
      "Ljava/lang/reflect/InvocationHandler;",
      "Ljava/util/concurrent/locks/LockSupport;",
      "Ljava/io/FileDescriptor;", "Ljava/io/InputStream;",
      "Ljava/io/FileInputStream;", "Ljava/io/FileOutputStream;",
      "Ljava/net/Socket;",        "Ljava/net/ServerSocket;",
      "Ljava/lang/Process;",      "Ljava/lang/ProcessBuilder;",
      "Lsun/misc/Unsafe;",        "Ljdk/internal/misc/Unsafe;",
  };
  for (const char* d : kList) {
    if (desc == d) return true;
  }
  return false;
}

// 建议修法：按缺口类型 + 目标 descriptor 家族归因（语义面，非壳特判）
std::string SuggestFix(GapType type, const std::string& target) {
  // 目标可能是类 descriptor、"Lcls;.name(sig)ret" 或 "Lcls;.name:type"
  std::string cls = target;
  size_t dot = cls.find('.');
  if (dot != std::string::npos) cls = cls.substr(0, dot);
  bool javaish = cls.rfind("Ljava/", 0) == 0 || cls.rfind("Lsun/", 0) == 0 ||
                 cls.rfind("Ljdk/", 0) == 0;
  bool androidish = cls.rfind("Landroid/", 0) == 0 || cls.rfind("Landroidx/", 0) == 0;
  bool dalvik = cls.rfind("Ldalvik/", 0) == 0;

  switch (type) {
    case GapType::kMissingClass:
      if (IsJreExcluded(cls)) {
        return "VM 内建类（jre-excluded 黑名单，core/builtins 注册）";
      }
      if (javaish) return "VM 内建类（java 常用面，core/builtins 注册）";
      if (dalvik) return "VM 内建（dalvik.system 语义面，core/builtins）";
      if (androidish) return "framework 内建类补全（core/framework_builtins）";
      return "app dex 类缺失（loader 覆盖范围/类不存在——归因后置）";
    case GapType::kMissingMethod:
    case GapType::kMissingMethodBody:
      if (javaish || dalvik) return "VM intrinsic 方法面补全（core/builtins）";
      if (androidish) return "framework 内建方法面补全（core/framework_builtins）";
      return "guest 方法面缺口（接口/父类语义或解释器派发语义）";
    case GapType::kMissingField:
      if (javaish || dalvik) return "VM 内建字段面补全（core/builtins）";
      if (androidish) return "framework 内建字段面补全（core/framework_builtins）";
      return "guest 字段面缺口（类布局/链接语义）";
    case GapType::kUnsupported:
      return "解释器/内建语义缺口（core/interpreter 或 builtins 按 Android 语义补）";
    case GapType::kNativeLib:
      return "宿主特权边界（Android .so 无法在宿主 dlopen——真机验证）或 loadLibrary 语义面";
    case GapType::kException:
      return "异常路径按 Android 语义补齐（core/exception + builtins 异常类）";
    default:
      return "按卡点诊断归因（语义缺口或宿主特权边界）";
  }
}

// 从 phase 失败诊断 + guest 栈快照提取缺口
std::vector<Gap> ClassifyFailure(const std::string& phase, const std::string& error,
                                 const std::vector<std::string>& stack) {
  std::vector<Gap> out;
  Gap g;
  g.phase = phase;
  g.diagnosis = error;
  g.stack = stack;
  g.caller = stack.empty() ? "(host) phase=" + phase : stack[0] + " (phase=" + phase + ")";

  auto trim = [](std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == ')')) s.pop_back();
    return s;
  };

  // 1. "class not found: <desc>"
  size_t p = error.find("class not found: ");
  if (p != std::string::npos) {
    std::string desc = error.substr(p + std::strlen("class not found: "));
    size_t cut = desc.find(" (");
    if (cut != std::string::npos) desc = desc.substr(0, cut);
    g.type = GapType::kMissingClass;
    g.target = trim(desc);
    g.fix = SuggestFix(g.type, g.target);
    out.push_back(g);
    return out;
  }
  // 2. "no such method on <cls>"
  p = error.find("no such method on ");
  if (p != std::string::npos) {
    std::string cls = error.substr(p + std::strlen("no such method on "));
    size_t cut = cls.find(" (");
    if (cut != std::string::npos) cls = cls.substr(0, cut);
    g.type = GapType::kMissingMethod;
    g.target = trim(cls);
    g.fix = SuggestFix(g.type, g.target);
    out.push_back(g);
    return out;
  }
  // 3. 异常诊断 "<desc>;: <msg>"（desc 形如 Ljava/lang/XxxError;，可被外层
  //    "(...)" 包裹——如 class init failed 包装）
  {
    size_t semi = error.rfind(";: ");
    if (semi != std::string::npos) {
      // 向前回溯 descriptor（'L' + [A-Za-z0-9_/$]* + ';'；'L' 是 alnum 会被
      // 吞进回溯，故落点即 'L' 起点，校验前一字符为分隔符）
      size_t start = semi;
      while (start > 0) {
        char c = error[start - 1];
        if (isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' || c == '/') {
          start--;
        } else {
          break;
        }
      }
      bool boundary_ok =
          start < semi && error[start] == 'L' &&
          (start == 0 || !(isalnum(static_cast<unsigned char>(error[start - 1])) ||
                           error[start - 1] == '_' || error[start - 1] == '$' ||
                           error[start - 1] == '/'));
      if (boundary_ok) {
        std::string desc = error.substr(start, semi + 1 - start);
        std::string msg = error.substr(semi + 3);
        while (!msg.empty() && (msg.back() == ')' || msg.back() == ' ')) msg.pop_back();
        g.diagnosis = desc + ": " + msg;
        if (desc == "Ljava/lang/NoClassDefFoundError;") {
          g.type = GapType::kMissingClass;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/NoSuchMethodError;") {
          g.type = GapType::kMissingMethod;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/NoSuchFieldError;") {
          g.type = GapType::kMissingField;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/UnsupportedOperationException;" &&
                   msg.rfind("no code: ", 0) == 0) {
          g.type = GapType::kMissingMethodBody;
          g.target = trim(msg.substr(std::strlen("no code: ")));
        } else if (desc == "Ljava/lang/UnsupportedOperationException;") {
          g.type = GapType::kUnsupported;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/UnsatisfiedLinkError;") {
          g.type = GapType::kNativeLib;
          g.target = trim(msg);
        } else if (desc == "Ljava/lang/AbstractMethodError;") {
          g.type = GapType::kMissingMethodBody;
          g.target = trim(msg);
        } else {
          g.type = GapType::kException;
          g.target = desc + (msg.empty() ? "" : ": " + trim(msg));
        }
        g.fix = SuggestFix(g.type, g.target);
        out.push_back(g);
        return out;
      }
    }
  }
  // 4. 其它（含 "no code: <sig>" 裸消息）
  p = error.find("no code: ");
  if (p != std::string::npos) {
    g.type = GapType::kMissingMethodBody;
    g.target = trim(error.substr(p + std::strlen("no code: ")));
    g.fix = SuggestFix(g.type, g.target);
    out.push_back(g);
    return out;
  }
  g.type = GapType::kOther;
  g.target = error;
  g.fix = SuggestFix(g.type, g.target);
  out.push_back(g);
  return out;
}

std::string ToStr(const std::u16string& s) { return std::string(s.begin(), s.end()); }

// ---- 语义面自检（新内建面经真实解释器执行验证，报告记录）----
struct Probe {
  std::string name;
  bool ok = false;
  std::string detail;
};

VMethod* ProbeFindStatic(VClass* klass, const char* name, std::vector<std::string> params,
                         const char* ret) {
  for (auto& m : klass->direct_methods) {
    if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) return &m;
  }
  return nullptr;
}
VMethod* ProbeFindVirtual(VClass* klass, const char* name, std::vector<std::string> params,
                          const char* ret) {
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  return klass->FindInterfaceImpl(&probe);
}

void RunFaceProbes(VMState& state, ClassLinker& linker, std::vector<Probe>* out) {
  auto add = [&](const char* name, bool ok, const std::string& detail) {
    Probe p;
    p.name = name;
    p.ok = ok;
    p.detail = detail;
    out->push_back(std::move(p));
    state.ClearException();
  };

  // 1. 链接补全接口：java.util.ArrayList 可链接+初始化（Iterable/Cloneable 面）
  {
    VClass* al = linker.FindClass("Ljava/util/ArrayList;");
    bool ok = al != nullptr && linker.InitializeClass(al, &state) && !state.HasPendingException();
    std::string detail = al == nullptr ? linker.last_error() : "linked+initialized";
    if (!ok && state.HasPendingException()) detail += " (pending)";
    add("link java.util.ArrayList", ok, detail);
  }
  // 2. System 常用静态面：currentTimeMillis
  {
    VClass* sys = linker.FindClass("Ljava/lang/System;");
    VMethod* m = sys != nullptr ? ProbeFindStatic(sys, "currentTimeMillis", {}, "J") : nullptr;
    Value r{};
    bool ok = m != nullptr && InvokeMethod(state, m, nullptr, 0, &r) && r.l != 0;
    add("System.currentTimeMillis()", ok, ok ? std::to_string(r.l) : "invoke failed");
  }
  // 3. 反射面：Class.getDeclaredMethod + Method.invoke（docs 29 壳必用）
  {
    VClass* cls_class = linker.FindClass("Ljava/lang/Class;");
    VClass* sb = linker.FindClass("Ljava/lang/StringBuilder;");
    VClass* cls_arr = linker.FindArrayClass(cls_class);
    bool ok = false;
    std::string detail = "skipped";
    if (cls_class != nullptr && sb != nullptr) {
      // StringBuilder.append(String) 反射调用
      VArray* params = linker.AllocArray(cls_arr, 1);
      if (params != nullptr) {
        ObjHeader* str_cls = reinterpret_cast<ObjHeader*>(linker.FindClass("Ljava/lang/String;"));
        std::memcpy(params->data(), &str_cls, sizeof(str_cls));
      }
      VString* name = linker.AllocString(u"append");
      VMethod* gdm = ProbeFindVirtual(cls_class, "getDeclaredMethod",
                                      {"Ljava/lang/String;", "[Ljava/lang/Class;"},
                                      "Ljava/lang/reflect/Method;");
      Value args[3] = {};
      args[0].ref = reinterpret_cast<ObjHeader*>(sb);
      args[1].ref = reinterpret_cast<ObjHeader*>(name);
      args[2].ref = reinterpret_cast<ObjHeader*>(params);
      Value mirror{};
      if (gdm != nullptr && InvokeMethod(state, gdm, args, 3, &mirror) && mirror.ref != nullptr) {
        // setAccessible(true) + invoke(sbInstance, ["hi"])
        VMethod* sa = ProbeFindVirtual(mirror.ref->klass, "setAccessible", {"Z"}, "V");
        Value sa_a[2] = {};
        sa_a[0].ref = mirror.ref;
        sa_a[1].i = 1;
        if (sa != nullptr) InvokeMethod(state, sa, sa_a, 2, nullptr);
        VObject* sbi = linker.AllocObject(sb);
        VMethod* ctor = ProbeFindVirtual(sb, "<init>", {}, "V");
        Value cself{};
        cself.ref = sbi;
        if (ctor != nullptr) InvokeMethod(state, ctor, &cself, 1, nullptr);
        VClass* obj_arr = linker.FindArrayClass(linker.FindClass("Ljava/lang/Object;"));
        VArray* argv = linker.AllocArray(obj_arr, 1);
        ObjHeader* hi = reinterpret_cast<ObjHeader*>(linker.AllocString(u"hi"));
        if (argv != nullptr) std::memcpy(argv->data(), &hi, sizeof(hi));
        VMethod* inv = ProbeFindVirtual(mirror.ref->klass, "invoke",
                                        {"Ljava/lang/Object;", "[Ljava/lang/Object;"},
                                        "Ljava/lang/Object;");
        Value ia[3] = {};
        ia[0].ref = mirror.ref;
        ia[1].ref = reinterpret_cast<ObjHeader*>(sbi);
        ia[2].ref = reinterpret_cast<ObjHeader*>(argv);
        Value ret{};
        if (inv != nullptr && InvokeMethod(state, inv, ia, 3, &ret) && ret.ref != nullptr) {
          // append 返回 this；toString 应为 "hi"
          VMethod* ts = ProbeFindVirtual(ret.ref->klass, "toString", {}, "Ljava/lang/String;");
          Value tself{};
          tself.ref = ret.ref;
          Value out{};
          if (ts != nullptr && InvokeMethod(state, ts, &tself, 1, &out) && out.ref != nullptr) {
            std::string got = static_cast<VString*>(out.ref)->ToModifiedUtf8();
            ok = got == "hi";
            detail = ok ? "append(String) via reflection -> \"hi\"" : "got \"" + got + "\"";
          }
        }
        if (!ok && detail == "skipped") detail = "invoke chain failed";
      }
    }
    add("Class.getDeclaredMethod + Method.invoke", ok, detail);
  }
  // 4. hidden API 面：VMRuntime.setHiddenApiExemptions（docs 29.2③）
  {
    VClass* vmr = linker.FindClass("Ldalvik/system/VMRuntime;");
    VMethod* gr = vmr != nullptr ? ProbeFindStatic(vmr, "getRuntime", {}, "Ldalvik/system/VMRuntime;")
                                : nullptr;
    Value rt{};
    bool ok = gr != nullptr && InvokeMethod(state, gr, nullptr, 0, &rt) && rt.ref != nullptr;
    std::string detail = "getRuntime failed";
    if (ok) {
      VClass* str_cls2 = linker.FindClass("Ljava/lang/String;");
      VClass* str_arr = linker.FindArrayClass(str_cls2);
      VArray* ex = linker.AllocArray(str_arr, 1);
      ObjHeader* L = reinterpret_cast<ObjHeader*>(linker.AllocString(u"L"));
      if (ex != nullptr) std::memcpy(ex->data(), &L, sizeof(L));
      VMethod* se = ProbeFindVirtual(rt.ref->klass, "setHiddenApiExemptions",
                                     {"[Ljava/lang/String;"}, "V");
      Value sa[2] = {};
      sa[0].ref = rt.ref;
      sa[1].ref = reinterpret_cast<ObjHeader*>(ex);
      ok = se != nullptr && InvokeMethod(state, se, sa, 2, nullptr);
      detail = ok ? "setHiddenApiExemptions([L]) recorded" : "setHiddenApiExemptions failed";
    }
    add("VMRuntime.setHiddenApiExemptions", ok, detail);
  }
  // 5. Thread.currentThread 语义面
  {
    VClass* thr = linker.FindClass("Ljava/lang/Thread;");
    VMethod* ct = thr != nullptr ? ProbeFindStatic(thr, "currentThread", {}, "Ljava/lang/Thread;")
                                : nullptr;
    Value r{};
    bool ok = ct != nullptr && InvokeMethod(state, ct, nullptr, 0, &r) && r.ref != nullptr;
    std::string detail = ok ? "current thread allocated" : "currentThread failed";
    if (ok) {
      VMethod* gn = ProbeFindVirtual(r.ref->klass, "getName", {}, "Ljava/lang/String;");
      Value self{};
      self.ref = r.ref;
      Value nm{};
      if (gn != nullptr && InvokeMethod(state, gn, &self, 1, &nm) && nm.ref != nullptr) {
        detail += " name=" + static_cast<VString*>(nm.ref)->ToModifiedUtf8();
      }
    }
    add("Thread.currentThread()", ok, detail);
  }
  // 6. 异常类补全面
  {
    const char* kNeed[] = {"Ljava/lang/UnsatisfiedLinkError;", "Ljava/lang/IllegalAccessError;",
                           "Ljava/lang/NoSuchMethodException;", "Ljava/lang/SecurityException;",
                           "Ljava/lang/reflect/InvocationTargetException;"};
    bool ok = true;
    std::string missing;
    for (const char* d : kNeed) {
      if (linker.FindClass(d) == nullptr) {
        ok = false;
        missing += d;
      }
    }
    add("exception class batch", ok, ok ? "all present" : "missing " + missing);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== zhihu exam smoke (M3 语义保真验证) ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string target_apk = argc > 2 ? argv[2] : "/tmp/target-apks/zhihu.apk";
  const std::string report_path =
      argc > 3 ? argv[3] : "tests/out/zhihu_exam_report.txt";
  g_native_lib_dir = argc > 4 ? argv[4] : "";
  dexvm::runtime::SetLibraryLoadHook(ExamLibraryLoader);

  // ---- 引导 VM（同 lifecycle_smoke）----
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

  auto app_session = dexvm::session::Session::Open(target_apk, &err);
  if (app_session == nullptr || app_session->dex_blobs().empty()) {
    std::printf("FAIL: open target APK: %s\n", err.c_str());
    return 1;
  }
  std::printf("open %s: %zu dex blobs\n", target_apk.c_str(),
              app_session->dex_blobs().size());

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
  VObject* app_loader = nullptr;
  for (const auto& blob : app_session->dex_blobs()) {
    std::string derr;
    auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
    if (dex_file == nullptr) {
      std::printf("FAIL: parse app dex %s: %s\n", blob.name.c_str(), derr.c_str());
      return 1;
    }
    linker.AddAppDexFile(dex_file.get());
    owned_dex.push_back(std::move(dex_file));
  }
  {
    VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
    app_loader = linker.AllocObject(cl);
    linker.loaders().BindObject(app_loader, linker.loaders().default_app());
    if (app_loader == nullptr) {
      std::printf("FAIL: app loader alloc\n");
      return 1;
    }
  }

  // ---- Manifest（考卷身份核对 = 测试数据断言，非壳适配）----
  const dexvm::session::ManifestInfo& mi = app_session->manifest();
  LifecycleConfig cfg = ConfigFromManifest(mi);
  std::printf("manifest: pkg=%s application=%s launcher=%s providers=%zu\n",
              mi.package_name.c_str(), cfg.application_class.c_str(),
              cfg.activity_class.c_str(), cfg.providers.size());
  bool identity_ok = cfg.application_class == "com.zhihu.android.app.ZhihuApplication";
  std::printf("%s: manifest application identity (%s)\n", identity_ok ? "PASS" : "FAIL",
              cfg.application_class.c_str());

  // ---- 语义面自检（新内建面经真实解释器验证）----
  std::vector<Probe> probes;
  RunFaceProbes(state, linker, &probes);
  for (const Probe& p : probes) {
    std::printf("%s: face probe %s (%s)\n", p.ok ? "PASS" : "FAIL", p.name.c_str(),
                p.detail.c_str());
  }

  // ---- LifecycleDriver 驱动 + 逐 phase 记录 ----
  std::vector<PhaseRecord> records;
  std::vector<std::vector<std::string>> stack_at_failure;
  LifecycleDriver driver(state, app_loader);
  driver.set_observer([&](const std::string& phase, bool begin, const PhaseRecord* rec) {
    if (begin) {
      state.last_throw_backtrace.clear();
      return;
    }
    if (rec == nullptr) return;
    records.push_back(*rec);
    if (!rec->ok) {
      stack_at_failure.push_back(state.last_throw_backtrace);
    } else {
      stack_at_failure.push_back({});
    }
  });

  std::printf("== run lifecycle chain ==\n");
  bool ran = driver.Run(cfg);

  // ---- 缺口收集 ----
  std::vector<Gap> gaps;
  for (size_t i = 0; i < records.size(); i++) {
    if (records[i].ok) continue;
    std::vector<Gap> g = ClassifyFailure(records[i].name, records[i].error,
                                         stack_at_failure[i]);
    gaps.insert(gaps.end(), g.begin(), g.end());
  }

  // 断言底线：至少进入 application.attachBaseContext（壳链）。
  // 宿主特权边界兜底（docs 29.2：模拟器检测类硬失败记录为环境限制、留待真机
  // 验证）：链停点若【唯一】卡在 native-lib（宿主无法执行目标 ABI 库/无 JNI
  // 执行面），按环境限制记录，不计语义失败。
  bool entered_attach = false;
  for (const auto& r : records) {
    if (r.name == "application.attachBaseContext") entered_attach = true;
  }
  bool native_boundary_only = !entered_attach && !records.empty() && !records.back().ok;
  if (native_boundary_only) {
    std::vector<Gap> blocking = ClassifyFailure(records.back().name, records.back().error, {});
    for (const Gap& g : blocking) {
      if (g.type != GapType::kNativeLib) native_boundary_only = false;
    }
  }
  bool bottom_ok = entered_attach || native_boundary_only;

  // ---- 结构化缺口报告 ----
  auto render = [&](FILE* out) {
    std::fprintf(out, "== zhihu exam report (M3 语义保真验证) ==\n");
    std::fprintf(out, "target: %s\n", target_apk.c_str());
    std::fprintf(out, "boot: %s (builtin classes after boot: %zu)\n", runtime_dex.c_str(),
                 linker.all_classes().size());
    std::fprintf(out, "manifest: package=%s application=%s launcher=%s providers=%zu\n",
                 mi.package_name.c_str(), cfg.application_class.c_str(),
                 cfg.activity_class.c_str(), cfg.providers.size());
    std::fprintf(out, "identity check: %s\n", identity_ok ? "ok" : "MISMATCH");
    std::fprintf(out, "\n-- semantic face probes (%zu) --\n", probes.size());
    for (const Probe& p : probes) {
      std::fprintf(out, "  %s %-40s %s\n", p.ok ? "ok  " : "FAIL", p.name.c_str(),
                   p.detail.c_str());
    }
    std::fprintf(out, "\n-- phase sequence (%zu, chain %s) --\n", records.size(),
                 ran ? "completed" : "fail-stop");
    for (size_t i = 0; i < records.size(); i++) {
      std::fprintf(out, "  [%zu] %-32s %s %lldms\n", i + 1, records[i].name.c_str(),
                   records[i].ok ? "ok  " : "FAIL",
                   static_cast<long long>(records[i].elapsed_ms));
      if (!records[i].ok) {
        std::fprintf(out, "        diagnosis: %s\n", records[i].error.c_str());
      }
    }
    std::fprintf(out, "\n-- gap list (%zu entries) --\n", gaps.size());
    for (size_t i = 0; i < gaps.size(); i++) {
      const Gap& g = gaps[i];
      std::fprintf(out, "  #%zu [%s] %s\n", i + 1, GapTypeName(g.type), g.target.c_str());
      std::fprintf(out, "      caller: %s\n", g.caller.c_str());
      std::fprintf(out, "      diagnosis: %s\n", g.diagnosis.c_str());
      for (size_t f = 0; f < g.stack.size() && f < 12; f++) {
        std::fprintf(out, "        at %s\n", g.stack[f].c_str());
      }
      std::fprintf(out, "      fix: %s\n", g.fix.c_str());
    }
    // 分类统计（unique target）
    std::map<std::string, int> by_type;
    std::map<std::string, int> unique;
    for (const Gap& g : gaps) {
      by_type[GapTypeName(g.type)]++;
      unique[std::string(GapTypeName(g.type)) + " " + g.target]++;
    }
    std::fprintf(out, "\n-- gap summary --\n");
    for (const auto& [k, v] : by_type) {
      std::fprintf(out, "  %-22s %d\n", k.c_str(), v);
    }
    std::fprintf(out, "  unique (type,target)   %zu\n", unique.size());
    std::fprintf(out, "\n-- bottom line --\n");
    std::fprintf(out, "entered application.attachBaseContext: %s\n",
                 entered_attach ? "YES" : "NO");
    if (!entered_attach && native_boundary_only) {
      std::fprintf(out,
                   "blocked at shell native boundary: environment limitation "
                   "(docs 29.2: 留待真机阶段验证)\n");
    }
    std::fprintf(out, "last phase: %s\n",
                 records.empty() ? "(none)" : records.back().name.c_str());
    const char* verdict = "FAIL";
    if (identity_ok) {
      if (entered_attach) verdict = "PASS";
      else if (native_boundary_only) verdict = "PASS (env-boundary)";
    }
    std::fprintf(out, "RESULT: %s\n", verdict);
  };

  render(stdout);
  {
    FILE* f = std::fopen(report_path.c_str(), "w");
    if (f == nullptr) {
      std::printf("FAIL: cannot write report: %s\n", report_path.c_str());
      return 1;
    }
    render(f);
    std::fclose(f);
    std::printf("report written: %s\n", report_path.c_str());
  }

  return (bottom_ok && identity_ok) ? 0 : 1;
}
