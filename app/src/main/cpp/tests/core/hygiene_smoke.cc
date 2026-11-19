// dirty reference 卫生墙压力（tools/hygiene_tests.sh）——死对象槽形态复现/回归。
// 形态链（设备停点同族）：容器对象经【继承字段槽】持引用 -> GC 穿插 ->
// 容器存活、目标被 mark-sweep 清扫 -> iget-object 读槽得悬挂指针
// （CheckRef "dirty reference"，位型=reclaimed-block）。
// 停点原形：RxJava Observable.subscribe -> ThreadPoolExecutor.execute ->
// ensureInitialized 的 iget-object workQueue @ pc=0。
//
// 每探针三步（GC 穿插 + iget 消费）：
//   make() -> 只留容器句柄（目标仅经槽可达）-> 宿主堆 churn ->
//   CollectGarbage x3 -> consume()（iget-object 继承槽消费）。
// 覆盖：
//   inherit2/inherit3  2/3 级继承链引用槽（通用形态）
//   pool               ThreadPoolExecutor 子类（设备 RxJava 形态；ensureInitialized
//                      iget-object workQueue 停点同字节码面）
//   lock               ReentrantLock 子类（sync/AQS 继承槽）
//   plain              纯 ThreadPoolExecutor 对照（自家槽口径，恒绿）
//   refkeep            WeakReference.referent（docs：referent 视为强引用）
// 用法: hygiene_smoke <runtime.dex> <hygiene.dex>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/gc.h"
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

// 容器锚（精确根）：ASAN 构建关闭保守栈扫描——C++ 局部不是 GC 根，
// 容器句柄必须落帧根槽（gc_auto_smoke AnchorRoot 同款），否则容器本身
// 被清扫（检出形态：check-cast 面 reclaimed——与目标死对象槽无关）。
// P(0) 槽持有当前容器；目标仍只经继承槽可达。
struct Anchor {
  std::unique_ptr<dexvm::runtime::Frame> frame;

  Anchor() {
    frame = std::make_unique<dexvm::runtime::Frame>();
    frame->regs.assign(4, 0u);
  }
  void Hold(ObjHeader* obj) {
    uintptr_t p = reinterpret_cast<uintptr_t>(obj);
    frame->regs[0] = static_cast<uint32_t>(p & 0xFFFFFFFFu);
    frame->regs[1] = static_cast<uint32_t>(p >> 32);
    frame->regs[2] = 0;
    frame->regs[3] = 0;
  }
};

VMethod* FindStatic(VClass* klass, const char* name, const char* ret) {
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.IsStatic() && m.param_types.empty() && m.return_type == ret) {
      return &m;
    }
  }
  return nullptr;
}

VMethod* FindStatic1(VClass* klass, const char* name, const char* p0, const char* ret) {
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.IsStatic() && m.param_types.size() == 1 && m.param_types[0] == p0 &&
        m.return_type == ret) {
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
    for (VClass* c = state.pending_exception->klass;
         c != nullptr && diag.find(":") == std::string::npos; c = c->super_class) {
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
  state.ClearException();
  return diag;
}

// 宿主堆 churn：变长字符串流（尺寸类别漂移，压空闲链；不保留句柄）
void HostChurn(ClassLinker& linker, int n) {
  char16_t buf[224];
  for (int i = 0; i < n; i++) {
    int len = 8 + (i * 7) % 200;
    for (int j = 0; j < len; j++) buf[j] = static_cast<char16_t>(u'a' + (j + i) % 26);
    VString* s = linker.AllocString(std::u16string_view(buf, len));
    (void)s;  // 立即弃：垃圾流
  }
}

struct Probe {
  const char* name;
  const char* klass;      // 探针类描述符
  const char* make;       // static Object make()
  const char* consume;    // static String consume(Object)
  int rounds;
};

int RunProbe(VMState& state, ClassLinker& linker, const Probe& p, Anchor& anchor) {
  int fails = 0;
  VClass* k = linker.FindClass(p.klass);
  if (k == nullptr) {
    std::printf("FAIL %s: class not found (%s): %s\n", p.name, p.klass,
                linker.last_error().c_str());
    return 1;
  }
  if (!linker.InitializeClass(k, &state) || state.HasPendingException()) {
    std::printf("FAIL %s: class init failed: %s\n", p.name, ExceptionDiag(state).c_str());
    return 1;
  }
  VMethod* make = FindStatic(k, p.make, "Ljava/lang/Object;");
  VMethod* consume = FindStatic1(k, p.consume, "Ljava/lang/Object;", "Ljava/lang/String;");
  if (make == nullptr || consume == nullptr) {
    std::printf("FAIL %s: make/consume not found\n", p.name);
    return 1;
  }

  for (int round = 0; round < p.rounds; round++) {
    // 1. make：容器句柄只进 C++ 局部（目标仅经继承槽可达）
    Value r{};
    if (!InvokeMethod(state, make, nullptr, 0, &r) || r.ref == nullptr) {
      std::printf("FAIL %s#%d: make: %s\n", p.name, round, ExceptionDiag(state).c_str());
      fails++;
      continue;
    }
    ObjHeader* container = r.ref;
    r.l = 0;  // 容器唯一根 = anchor 帧根槽（常规/ASAN 同根结构）
    anchor.Hold(container);

    // 2. churn + GC 穿插（死对象槽形成点）：
    //    目标无任何【被扫描根】引用（C++ 局部只持容器；旧调用帧已出栈）；
    //    继承槽漏标时目标被清扫，槽内残留悬挂指针。
    HostChurn(linker, 256);
    for (int g = 0; g < 3; g++) {
      dexvm::gc::CollectGarbage(state);
      HostChurn(linker, 64);
    }

    // 3. iget 消费（设备停点同面：ensureInitialized/继承槽 iget-object）
    Value args[1];
    args[0].ref = container;
    Value out{};
    std::string line;
    if (!InvokeMethod(state, consume, args, 1, &out)) {
      line = std::string(p.name) + ": FAIL " + ExceptionDiag(state);
      fails++;
    } else {
      line = ResultUtf8(out);
      if (line.find(": OK") == std::string::npos) fails++;
    }
    std::printf("%s (round %d)\n", line.c_str(), round);
    if (line.find("dirty reference") != std::string::npos) {
      std::printf("  ^^ dead-slot residue form (swept target in inherited slot)\n");
    }
  }
  return fails;
}

}  // namespace

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::printf("== dirty-reference hygiene smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_dex =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/hygiene.dex";

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

  const Probe kProbes[] = {
      {"inherit2", "Lhtest/InheritSlotProbe;", "make", "consume", 3},
      {"inherit3", "Lhtest/InheritSlotProbe;", "makeDeep", "consumeDeep", 3},
      {"pool", "Lhtest/JucChainProbe;", "makePoolSub", "consumePool", 3},
      {"lock", "Lhtest/JucChainProbe;", "makeLockSub", "consumeLock", 3},
      {"plain", "Lhtest/JucChainProbe;", "makePlainPool", "consumePlain", 2},
      {"refkeep", "Lhtest/RefKeepProbe;", "make", "consume", 3},
  };

  int fails = 0;
  Anchor anchor;
  state.frame_stack.push_back(anchor.frame.get());  // 精确根（帧槽扫描）
  for (const Probe& p : kProbes) {
    std::printf("-- %s --\n", p.name);
    fails += RunProbe(state, linker, p, anchor);
  }
  state.frame_stack.clear();

  if (fails == 0) {
    std::printf("hygiene smoke: ALL PASS\n");
    return 0;
  }
  std::printf("hygiene smoke: %d FAILURES\n", fails);
  return 1;
}
