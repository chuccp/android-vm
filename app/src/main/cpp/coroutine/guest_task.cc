// GuestTask / FiberContext 实现（docs 04 章 14.2 / 14.3 / 14.9）
//
// fiber 切换用自研最小原语（coroutine/fiber_switch.S，被调用者保存寄存器）：
// bionic 已移除 ucontext 函数，macOS ucontext_t 有 ODR 陷阱，故两者皆不用。

#include "coroutine/guest_task.h"

// fiber_switch.S（aarch64/x86_64）：
//   swap(save, load)：保存当前上下文到 save，恢复 load 上下文并跳转。
//   init(buf, trampoline, stack_top)：装入"首次 swap 即进入 trampoline"的上下文。
extern "C" void dexvm_fiber_swap(void* save, void* load);
extern "C" void dexvm_fiber_init(void* buf, void* trampoline, void* stack_top);

#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include "coroutine/scheduler.h"

namespace dexvm::coroutine {

namespace {

// -fno-exceptions：契约失败直接终止
#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, \
                   __LINE__);                                            \
      std::abort();                                                      \
    }                                                                    \
  } while (0)

constexpr size_t kDefaultStackSize = 256 * 1024;  // 可用栈（14.6）

// fiber 入口 trampoline：经 Scheduler tls 取当前 task。
void FiberEntryTrampoline() {
  GuestTask* task = Scheduler::CurrentTask();
  CHECK(task != nullptr);
  task->RunEntry();
  task->FinishFromEntry();  // entry 返回 = task 结束（14.2）
}

}  // namespace

static_assert(UContextStorage::kSize >= 256,
              "UContextStorage too small for fiber register dump");

void UContextGet(UContextStorage* save) {
  // 保存当前上下文（swap 同端 = 存后即回，等价 getcontext）
  dexvm_fiber_swap(save->bytes, save->bytes);
}

void UContextSwap(UContextStorage* save, UContextStorage* target) {
  dexvm_fiber_swap(save->bytes, target->bytes);
}

// ---- FiberContext（14.3 / 14.6）----

bool FiberContext::Init(void (*trampoline)(), UContextStorage* fallback_link,
                        size_t stack_bytes) {
  CHECK(stack_base == nullptr);
  long page = sysconf(_SC_PAGESIZE);
  CHECK(page > 0);
  size_t guard = static_cast<size_t>(page);
  size_t map = stack_bytes + guard;

  void* p = mmap(nullptr, map, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) return false;

  stack_base = static_cast<uint8_t*>(p);
  stack_size = stack_bytes;
  map_size = map;
  guard_page = stack_base;  // 低地址 guard page（栈向下增长）
  if (mprotect(guard_page, guard, PROT_NONE) != 0) {
    munmap(stack_base, map_size);
    stack_base = nullptr;
    return false;
  }

  // 装入 fiber 上下文：栈顶 16 对齐，首次 swap 进入 trampoline
  void* stack_top = stack_base + map_size;  // 栈向下增长
  dexvm_fiber_init(uc.bytes, reinterpret_cast<void*>(trampoline), stack_top);

  link = fallback_link;
  ready = true;
  return true;
}

void FiberContext::TearDown() {
  if (stack_base != nullptr) {
    munmap(stack_base, map_size);
  }
  stack_base = nullptr;
  stack_size = 0;
  map_size = 0;
  guard_page = nullptr;
  ready = false;
  ++stack_generation;  // 旧栈引用作废（14.3）
}

// ---- GuestTask ----

GuestTask::GuestTask(Scheduler* scheduler, uint64_t task_id, Entry entry,
                     void* arg, Priority priority)
    : scheduler_(scheduler),
      task_id_(task_id),
      entry_(entry),
      arg_(arg),
      priority_(priority) {
  CHECK(scheduler_ != nullptr);
  CHECK(entry_ != nullptr);
}

GuestTask::~GuestTask() {
  // 安全网：未回收则回收 fiber 栈
  if (fiber_.stack_base != nullptr) {
    ReclaimFiber();
  }
}

bool GuestTask::InitFiber(size_t stack_bytes) {
  return fiber_.Init(&FiberEntryTrampoline, &scheduler_->sched_uc_,
                     stack_bytes != 0 ? stack_bytes : kDefaultStackSize);
}

void GuestTask::ReclaimFiber() {
  fiber_.TearDown();
  scheduler_->live_fiber_stacks_--;
}

void GuestTask::RunEntry() {
  CHECK(lifecycle_state_ == LifecycleState::kRunnable);
  CHECK(execution_state_ == ExecutionState::kRunningOnCarrier);
  entry_(this, arg_);
}

void GuestTask::FinishFromEntry() {
  // entry 返回 = task 结束：reason=DONE，调度器 CompleteTask 回收（14.2/14.7）
  scheduler_->SwapFromTask(this, SwitchReason::kDone);
  std::abort();  // 不可达：kDone 不恢复该 fiber
}

bool GuestTask::Yield() {
  // 14.9：pin 期间不发生 carrier switch
  if (native_state_ == NativeState::kPinnedNative) return false;
  CHECK(this == Scheduler::CurrentTask());
  CHECK(lifecycle_state_ == LifecycleState::kRunnable);
  scheduler_->SwapFromTask(this, SwitchReason::kYield);
  return true;
}

bool GuestTask::PollSafepoint() {
  CHECK(this == Scheduler::CurrentTask());
  if (gc_state_ != GcState::kSafepointRequested) return false;
  if (native_state_ == NativeState::kPinnedNative) {
    // 14.12：PINNED_NATIVE 不停顿，返回 managed 时再观察挂起请求
    return false;
  }
  // 响应 checkpoint：保存上下文（swap 即保存）-> SUSPENDED -> 让出（14.12）
  gc_state_ = GcState::kSuspended;
  scheduler_->SwapFromTask(this, SwitchReason::kSafepoint);
  // 恢复后由 ResumeFromSafepoint 置回 RUNNABLE
  CHECK(gc_state_ == GcState::kRunnable);
  return true;
}

void GuestTask::EnterNative() {
  // 14.9：EnterNative -> PINNED_NATIVE + pin_count++（M1 单级 pin）
  CHECK(this == Scheduler::CurrentTask());
  CHECK(native_state_ == NativeState::kNone);
  CHECK(execution_state_ == ExecutionState::kRunningOnCarrier);
  Carrier* c = current_carrier_;
  CHECK(c != nullptr && c->current_task == this);
  native_state_ = NativeState::kPinnedNative;
  c->native_pin_count++;
  c->state = CarrierState::kPinnedToTask;
  // current_carrier 保持（14.9：pin 期间 carrier 绑定稳定）
}

void GuestTask::LeaveNative(NativeExit mode) {
  // 14.9（修订语义）：pop JNI local frame -> pin_count-- -> 置 NONE，
  // 再区分「返回继续执行」与「让出/结束」两条路径。
  CHECK(this == Scheduler::CurrentTask());
  CHECK(native_state_ == NativeState::kPinnedNative);
  Carrier* c = current_carrier_;
  CHECK(c != nullptr && c->current_task == this);
  // pop JNI local frame（M1：JNI local frame 随 jni_core 后置，no-op）
  CHECK(c->native_pin_count > 0);
  c->native_pin_count--;
  native_state_ = NativeState::kNone;
  if (c->native_pin_count == 0 && c->state == CarrierState::kPinnedToTask) {
    c->state = CarrierState::kRunningTask;
  }

  switch (mode) {
    case NativeExit::kContinue:
      // 路径 1：普通 native 方法返回，task 继续在当前 carrier 上执行
      CHECK(execution_state_ == ExecutionState::kRunningOnCarrier);
      CHECK(current_carrier_ == c);
      // notify scheduler 可回收多余 carrier（M1 单 carrier：no-op）
      break;
    case NativeExit::kYield:
    case NativeExit::kFinish:
      // 路径 2：native frame 结束同时让出/结束 task -> 唤醒调度器
      current_carrier_ = nullptr;
      execution_state_ = ExecutionState::kNotRunning;
      scheduler_->SwapFromTask(
          this, mode == NativeExit::kYield ? SwitchReason::kYield
                                           : SwitchReason::kDone);
      break;
  }
}

void GuestTask::EndQuantumWhilePinned() {
  // 14.7 reason=PIN：quantum/抢占边界命中 pin。换出后调度器不释放 carrier、
  // 不切换 task（跳过该 carrier 的 ReadyQueue），立即换回继续 native frame。
  CHECK(this == Scheduler::CurrentTask());
  CHECK(native_state_ == NativeState::kPinnedNative);
  scheduler_->SwapFromTask(this, SwitchReason::kPin);
  // 恢复后仍在同一 carrier 的 native frame 中
  CHECK(native_state_ == NativeState::kPinnedNative);
  CHECK(execution_state_ == ExecutionState::kRunningOnCarrier);
}

}  // namespace dexvm::coroutine
