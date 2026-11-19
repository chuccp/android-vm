// 自动 GC 接线冒烟（docs 12 章 22.10 AllocateInternalWithGc 语义）：
//   1. 分配压力：对象流 > 容量、存活集保持——分配失败→GC 重试链全程救回
//   2. OOM 语义：可达内存耗尽（GC 后仍不足）抛真 OutOfMemoryError 类
//   3. GC 后引用完整性（gc_smoke 语义）：存活串/数组元素/对象字段完好
//   4. 多 GuestTask 并发分配：挂起 task 的 C++ 局部引用经 fiber 栈保守扫描保活
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "coroutine/guest_task.h"
#include "coroutine/scheduler.h"
#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/gc.h"
#include "core/interpreter.h"
#include "core/vm_types.h"
#include "memory/heap.h"

namespace {

int g_fail = 0;
#define CHECK(cond, msg)                          \
  do {                                            \
    if (!(cond)) {                                \
      std::printf("FAIL %s\n", msg);              \
      g_fail++;                                   \
    } else {                                      \
      std::printf("PASS %s\n", msg);              \
    }                                             \
  } while (0)

using namespace dexvm;

// 最小 VM 装置（每场景独立：堆小容量逼 GC 频发）
struct Rig {
  std::unique_ptr<mem::Heap> heap;
  std::unique_ptr<runtime::InternTable> intern;
  std::unique_ptr<runtime::ClassLinker> linker;
  runtime::VMState state;
  std::unique_ptr<runtime::Frame> root_frame;  // 帧根（存活集/数组锚）

  explicit Rig(size_t capacity) {
    heap = std::make_unique<mem::Heap>(capacity);
    intern = std::make_unique<runtime::InternTable>();
    linker = std::make_unique<runtime::ClassLinker>(heap.get(), intern.get());
    runtime::RegisterBuiltins(linker.get());
    state.heap = heap.get();
    state.intern = intern.get();
    state.linker = linker.get();
    gc::GcAttach(state);
    runtime::EnsureOomReserve(state);
    root_frame = std::make_unique<runtime::Frame>();
    root_frame->regs.assign(4, 0u);
    state.frame_stack.push_back(root_frame.get());
  }
  ~Rig() { state.frame_stack.clear(); }

  // 帧根槽写入（P(0)：64 位保守槽对）
  void AnchorRoot(runtime::ObjHeader* obj) {
    uintptr_t p = reinterpret_cast<uintptr_t>(obj);
    root_frame->regs[0] = static_cast<uint32_t>(p & 0xFFFFFFFFu);
    root_frame->regs[1] = static_cast<uint32_t>(p >> 32);
  }
};

// ---- 1+3. 分配压力 + 引用完整性 ----
void TestAllocPressure() {
  std::printf("-- alloc pressure (stream > capacity, live set held) --\n");
  Rig rig(2 * 1024 * 1024);  // 2MB
  runtime::VClass* str_cls = rig.linker->FindClass("Ljava/lang/String;");
  runtime::VClass* arr_cls = rig.linker->FindArrayClass(str_cls);
  CHECK(str_cls != nullptr && arr_cls != nullptr, "string/array class");

  // 存活集：512 个已知内容串存入帧根数组（GC 后逐一校验完整性）
  const int32_t kLive = 512;
  runtime::VArray* live_arr = rig.linker->AllocArray(arr_cls, kLive);
  CHECK(live_arr != nullptr, "live array alloc");
  rig.AnchorRoot(reinterpret_cast<runtime::ObjHeader*>(live_arr));
  for (int32_t i = 0; i < kLive; i++) {
    char16_t buf[16] = u"live-0000";
    buf[5] = u'0' + (i / 1000) % 10;
    buf[6] = u'0' + (i / 100) % 10;
    buf[7] = u'0' + (i / 10) % 10;
    buf[8] = u'0' + i % 10;
    runtime::VString* s = rig.linker->AllocString(std::u16string_view(buf, 9));
    reinterpret_cast<runtime::ObjHeader**>(live_arr->data())[i] =
        reinterpret_cast<runtime::ObjHeader*>(s);
  }

  // 对象流：总量 >> 容量（~14MB 垃圾压 2MB 堆），尺寸类别变化（空闲链单键
  // 覆盖不全，持续逼出分配失败→GC 重试），全部不可达
  size_t gc0 = rig.heap->gc_runs();
  char16_t buf[224];
  for (int i = 0; i < 60000; i++) {
    int len = 8 + (i * 7) % 200;
    for (int j = 0; j < len; j++) buf[j] = static_cast<char16_t>(u'a' + (j + i) % 26);
    runtime::VString* s = rig.linker->AllocString(std::u16string_view(buf, len));
    if (s == nullptr) break;
  }
  size_t gc1 = rig.heap->gc_runs();
  std::printf("gc runs during churn: %zu, recovered %zu\n", gc1 - gc0,
              rig.heap->alloc_recovered());
  CHECK(gc1 - gc0 >= 3, "alloc failure triggered GC rounds");
  CHECK(rig.heap->alloc_recovered() > 0, "GC retry chain recovered allocations");
  CHECK(rig.heap->bytes_allocated() < 2 * 1024 * 1024, "heap bounded by GC (no OOM)");
  CHECK(rig.state.pending_exception == nullptr, "no OOM thrown under pressure");

  // 引用完整性：存活集内容逐槽完好（未被清扫/覆写）
  bool intact = true;
  for (int32_t i = 0; i < kLive; i++) {
    auto* s = reinterpret_cast<runtime::VString*>(
        reinterpret_cast<runtime::ObjHeader**>(live_arr->data())[i]);
    if (s == nullptr || s->length != 9 || s->chars()[0] != u'l' || s->chars()[8] != u'0' + i % 10) {
      intact = false;
      break;
    }
  }
  CHECK(intact, "live set contents intact after GC churn");
}

// ---- 2. OOM 语义：可达内存耗尽 -> 真 OutOfMemoryError 类 ----
void TestOomSemantics() {
  std::printf("-- OOM semantics (reachable set exhausts heap) --\n");
  Rig rig(1 * 1024 * 1024);  // 1MB
  runtime::VClass* obj_cls = rig.linker->FindClass("Ljava/lang/Object;");
  runtime::VClass* oom_cls = rig.linker->FindClass("Ljava/lang/OutOfMemoryError;");
  CHECK(obj_cls != nullptr && oom_cls != nullptr, "Object/OutOfMemoryError class");

  // 可达填充：大槽位数组（帧根）逐一装满小对象——全部可达，GC 回收不动
  runtime::VClass* arr_cls = rig.linker->FindArrayClass(obj_cls);
  runtime::VArray* sink = rig.linker->AllocArray(arr_cls, 65536);
  CHECK(sink != nullptr, "sink array alloc");
  rig.AnchorRoot(reinterpret_cast<runtime::ObjHeader*>(sink));
  int filled = 0;
  for (int i = 0; i < 65536; i++) {
    runtime::VObject* o = rig.linker->AllocObject(obj_cls);
    if (o == nullptr) break;
    reinterpret_cast<runtime::ObjHeader**>(sink->data())[i] =
        reinterpret_cast<runtime::ObjHeader*>(o);
    filled++;
  }
  std::printf("reachable fill: %d objects, %zu B used / %zu B cap\n", filled,
              rig.heap->bytes_allocated(), rig.heap->capacity());
  CHECK(filled > 10000, "reachable set grew to exhaustion");

  // 进一步分配：可达内存耗尽（GC 后仍不足）——分配失败
  size_t gc0 = rig.heap->gc_runs();
  runtime::VObject* fail = rig.linker->AllocObject(obj_cls);
  CHECK(fail == nullptr, "allocation fails when reachable set exhausts heap");
  CHECK(rig.heap->gc_runs() > gc0, "retry chain ran GC rounds before giving up");

  // OOM 抛出语义：真 OutOfMemoryError 类（可 catch），非裸兜底（klass==null）
  runtime::ThrowOOM(rig.state, "test heap exhaustion");
  runtime::ObjHeader* ex = rig.state.pending_exception;
  CHECK(ex != nullptr, "OOM exception pending");
  CHECK(ex != nullptr && ex->klass != nullptr, "OOM is not bare fallback");
  CHECK(ex != nullptr && ex->klass == oom_cls, "OOM class is OutOfMemoryError");
  CHECK(ex != nullptr && ex->klass != nullptr &&
            oom_cls->IsAssignableFrom(ex->klass),
        "catch(OutOfMemoryError) assignable");
  // 预留单例在堆耗尽前已建（Heap::Pin 恒保活）——重复 OOM 仍为真类
  runtime::ThrowOOM(rig.state, "second OOM");
  CHECK(rig.state.pending_exception != nullptr &&
            rig.state.pending_exception->klass == oom_cls,
        "second OOM still real class");
}

// ---- 4. 多 GuestTask 并发分配（fiber 栈保守扫描面）----
struct TaskPayload {
  runtime::ClassLinker* linker;
  mem::Heap* heap;
  int task_id;
  int rounds;
  std::atomic<int>* errors;
};

// 每轮：分配 keeper（仅 C++ 局部持引）→ 让出（task 挂起、他 task 触发 GC）→
// 校验 keeper 内容完好（fiber 栈保守扫描保活）。keeper 若被清扫，内容必坏。
void AllocTaskEntry(coroutine::GuestTask* task, void* raw) {
  auto* p = static_cast<TaskPayload*>(raw);
  for (int r = 0; r < p->rounds; r++) {
    char16_t buf[16] = u"keep-0-00";
    buf[5] = u'0' + p->task_id;
    buf[7] = u'0' + (r / 10) % 10;
    buf[8] = u'0' + r % 10;
    runtime::VString* keeper = p->linker->AllocString(std::u16string_view(buf, 9));
    if (keeper == nullptr) {
      p->errors->fetch_add(1);
      break;
    }
    // 不可达 churn（尺寸类别变化）：逼他/自 task 的分配失败触发 GC
    char16_t junk[128];
    for (int i = 0; i < 600; i++) {
      int len = 4 + (i + r * 3) % 100;
      for (int j = 0; j < len; j++) junk[j] = u'x';
      if (p->linker->AllocString(std::u16string_view(junk, len)) == nullptr) break;
    }
    task->Yield();  // 挂起：keeper 仅存于本 fiber 栈 C++ 局部
    // resume：校验 keeper 未经清扫/覆写
    if (keeper->klass == nullptr || keeper->length != 9 || keeper->chars()[0] != u'k' ||
        keeper->chars()[5] != u'0' + p->task_id || keeper->chars()[8] != u'0' + r % 10) {
      p->errors->fetch_add(1);
      break;
    }
    task->Yield();
  }
}

void TestMultiTaskAlloc() {
  std::printf("-- multi GuestTask concurrent allocation --\n");
  Rig rig(512 * 1024);  // 512KB：GC 高频
  std::atomic<int> errors{0};

  // fiber 栈枚举接缝自装（gc.cc 经 memory/heap.cc 栈区枚举口消费）
  coroutine::Scheduler sched;
  mem::SetGlobalStackProvider(
      [](void* ctx, void (*add)(void*, size_t, void*), void* arg) {
        static_cast<coroutine::Scheduler*>(ctx)->VisitRootRanges(add, arg);
      },
      &sched);

  constexpr int kTasks = 4;
  constexpr int kRounds = 25;
  TaskPayload payloads[kTasks];
  for (int i = 0; i < kTasks; i++) {
    payloads[i] = TaskPayload{rig.linker.get(), rig.heap.get(), i, kRounds, &errors};
    auto* t = sched.Spawn(&AllocTaskEntry, &payloads[i], coroutine::Priority::kP2,
                          256 * 1024);
    CHECK(t != nullptr, "spawn alloc task");
  }
  bool ran = sched.Run(std::chrono::milliseconds(30000));
  CHECK(ran, "scheduler run completes");
  mem::SetGlobalStackProvider(nullptr, nullptr);
  CHECK(errors.load() == 0, "keepers intact across suspended-task GCs");
  std::printf("switches=%llu gc_runs=%zu recovered=%zu\n",
              static_cast<unsigned long long>(sched.switch_count()), rig.heap->gc_runs(),
              rig.heap->alloc_recovered());
  CHECK(rig.heap->gc_runs() > 0, "multi-task churn triggered GC");
}

}  // namespace

int main() {
  TestAllocPressure();
  TestOomSemantics();
  TestMultiTaskAlloc();
  if (g_fail == 0) std::printf("ALL PASS\n");
  return g_fail == 0 ? 0 : 1;
}
