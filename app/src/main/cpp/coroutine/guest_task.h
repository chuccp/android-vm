// GuestTask / FiberContext / Native pin（docs/art-runtime-v2/04-coroutine-runtime.md
// 14.2 / 14.3 / 14.9；语义对照 third_party/art/runtime/thread*）
//
// M1 裁剪：14.3 中 ART Thread / JNIEnvExt / ManagedStack / HandleScope / ShadowFrame
// 字段随 ART Thread 落地后置；FiberContext 先保存恢复 ucontext 栈+寄存器。
//
// 注意：本头不暴露 ucontext_t。macOS SDK 的 ucontext_t 布局随 feature 宏漂移
// （<sys/_types/_ucontext.h> 仅在 _XOPEN_SOURCE 下嵌入 __mcontext_data），不同
// TU 不一致会 ODR 破坏；ucontext 统一收在 guest_task.cc，经 UContextStorage 使用。
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace dexvm::coroutine {

class Scheduler;
struct Carrier;

// 让出原因（14.7 SchedulerLoop switch reason）；定义见 scheduler.h
enum class SwitchReason : uint8_t;

using TimePoint = std::chrono::steady_clock::time_point;

// ---- 六正交状态字段（14.2）----

enum class LifecycleState : uint8_t {
  kCreated,    // 已构造，未入调度
  kRunnable,   // 可运行（含 ready/wait/suspended 中）
  kCompleted,  // 正常结束（含 completed-with-exception）
  kCancelled,  // 已取消
};

enum class ExecutionState : uint8_t {
  kNotRunning,       // 不在任何 carrier 上执行
  kRunningOnCarrier, // 正在某 carrier 上执行
};

enum class WaitState : uint8_t {
  kNone,
  kMutex,      // mutex waiter list
  kMonitor,    // object monitor waiter list
  kCondition,  // condition wait queue
  kIo,         // IOReactor（M1：与 WAIT 同停放路径，IOReactor 后置）
  kDeadline,   // TimerHeap（sleep/deadline）
  kJoin,       // task completion wait channel
  kBinder,     // binder reply wait
  kJniOnLoad,  // SharedLibrary condition wait
};

enum class NativeState : uint8_t {
  kNone,
  kPinnedNative,  // pin 期间不发生 carrier switch（14.9）
};

enum class GcState : uint8_t {
  kRunnable,
  kSafepointRequested,  // 已请求 checkpoint，等待任务在安全边界响应（14.12）
  kSuspended,           // 已停顿，GC root 扫描中
};

enum class ExceptionState : uint8_t {
  kNone,
  kPendingGuestException,  // 普通 Java exception：frame 内 catch 后清回 kNone
};

// 14.7 优先级：P0 lifecycle/startup、P1 main Looper/UI、P2 普通、P3 background
enum class Priority : uint8_t {
  kP0 = 0,
  kP1 = 1,
  kP2 = 2,
  kP3 = 3,
};

// LeaveNative 两条路径（14.9 修订语义）
enum class NativeExit : uint8_t {
  kContinue,  // 普通 native 方法返回：task 继续在当前 carrier 上执行
  kYield,     // native frame 结束同时让出 task
  kFinish,    // native frame 结束同时结束 task
};

// fiber 上下文存储（不透明）：自研被调用者保存寄存器集（aarch64/x86_64，
// 见 coroutine/fiber_switch.S）；bionic 无 ucontext 函数，不使用 ucontext_t。
struct UContextStorage {
  static constexpr size_t kSize = 256;
  alignas(16) uint8_t bytes[kSize];
};

// fiber 上下文操作（实现见 guest_task.cc + fiber_switch.S）
void UContextGet(UContextStorage* save);
void UContextSwap(UContextStorage* save, UContextStorage* target);

// ---- Fiber 栈（14.3 / 14.6）----
// mmap 可用栈 + 低地址 guard page，自研 fiber 切换（fiber_switch.S）保存恢复。
struct FiberContext {
  UContextStorage uc{};            // 非运行时保存的寄存器/栈指针
  UContextStorage* link = nullptr; // 兜底返回上下文（正常路径不经由它）

  uint8_t* stack_base = nullptr;  // mmap 基址（含 guard page）
  size_t stack_size = 0;          // 可用栈字节数
  size_t map_size = 0;            // mmap 总字节数（含 guard page）
  uint8_t* guard_page = nullptr;  // 低地址 PROT_NONE 页（可为 nullptr）
  uint64_t stack_generation = 0;  // 栈代（防旧栈引用，14.3）
  bool ready = false;             // fiber 上下文已装好

  // mmap 栈 + guard page 并 makecontext；失败返回 false（保持空栈）。
  bool Init(void (*trampoline)(), UContextStorage* fallback_link,
            size_t stack_bytes);
  void TearDown();  // munmap + 复位（完成回收，14.2）
};

// ---- GuestTask（14.2）----
// 状态迁移由本类执行方法 + Scheduler 完成；字段暴露只读访问器供调度/测试观测。
class GuestTask {
 public:
  using Entry = void (*)(GuestTask*, void*);

  GuestTask(Scheduler* scheduler, uint64_t task_id, Entry entry, void* arg,
            Priority priority);
  ~GuestTask();

  GuestTask(const GuestTask&) = delete;
  GuestTask& operator=(const GuestTask&) = delete;

  // ---- 身份 / 状态（14.2）----
  uint64_t task_id() const { return task_id_; }
  LifecycleState lifecycle_state() const { return lifecycle_state_; }
  ExecutionState execution_state() const { return execution_state_; }
  WaitState wait_state() const { return wait_state_; }
  NativeState native_state() const { return native_state_; }
  GcState gc_state() const { return gc_state_; }
  ExceptionState exception_state() const { return exception_state_; }
  Priority priority() const { return priority_; }
  TimePoint deadline() const { return deadline_; }
  Carrier* current_carrier() const { return current_carrier_; }
  const FiberContext& fiber() const { return fiber_; }

  // 普通 Java exception：frame 内 catch 后清回 kNone（14.2）
  void SetPendingGuestException() {
    exception_state_ = ExceptionState::kPendingGuestException;
  }
  void ClearGuestException() { exception_state_ = ExceptionState::kNone; }
  bool HasPendingGuestException() const {
    return exception_state_ == ExceptionState::kPendingGuestException;
  }

  // ---- 任务侧执行操作 ----
  // 让出执行权（14.7 reason=YIELD）。pin 期间不发生 switch：返回 false 且不切换。
  bool Yield();

  // 安全边界轮询（14.12）：有 checkpoint 请求则停顿让出并等待恢复。
  // 返回 true = 本次已响应停顿（恢复后返回）；false = 无请求 / pin 中不响应。
  bool PollSafepoint();

  // ---- Native pin（14.9）----
  // 进入 native：PINNED_NATIVE，carrier.native_pin_count++，置 PINNED_TO_TASK。
  // pin 期间不发生 carrier switch（Yield 拒绝、14.5 重装不打断 native 执行）。
  void EnterNative();
  // 离开 native，区分两条路径（14.9 修订语义）：
  //   kContinue：普通 native 返回，task 继续在当前 carrier 上执行；
  //   kYield/kFinish：native 退出同时让出/结束 task（置 NOT_RUNNING、
  //   清 current_carrier、唤醒调度器）。
  void LeaveNative(NativeExit mode);

  // quantum/抢占边界命中 pin（14.7 reason=PIN）：换出后调度器不释放 carrier、
  // 不切换 task，立即换回继续该 task 的 native frame。
  void EndQuantumWhilePinned();

  // ---- fiber 入口（仅 fiber 入口 / 调度器调用）----
  void RunEntry();
  [[noreturn]] void FinishFromEntry();

 private:
  friend class Scheduler;

  // 装入 fiber 栈 + trampoline（Scheduler::Spawn 调用）。stack_bytes=0 取默认
  // （256KB）；VM guest 解释执行（递归解释器 C++ 栈）走更大栈（Spawn 面扩展）。
  bool InitFiber(size_t stack_bytes = 0);
  void ReclaimFiber();  // 回收 fiber 栈（完成回收，14.2）

  Scheduler* scheduler_;
  uint64_t task_id_;
  Entry entry_;
  void* arg_;

  // 六正交状态字段（14.2）
  LifecycleState lifecycle_state_ = LifecycleState::kCreated;
  ExecutionState execution_state_ = ExecutionState::kNotRunning;
  WaitState wait_state_ = WaitState::kNone;
  NativeState native_state_ = NativeState::kNone;
  GcState gc_state_ = GcState::kRunnable;
  ExceptionState exception_state_ = ExceptionState::kNone;

  Priority priority_;
  TimePoint deadline_ = TimePoint::max();

  FiberContext fiber_;
  Carrier* current_carrier_ = nullptr;

  // ---- 调度器簿记（Scheduler 独占写）----
  uint64_t ready_seq_ = 0;       // 入队序号（FIFO / aging）
  uint64_t ready_enq_tick_ = 0;  // 入队时 pop_tick（aging）
  bool in_ready_ = false;
  bool in_wait_bucket_ = false;
  bool in_timer_ = false;
  uint64_t wait_epoch_ = 0;          // timer 惰性删除代
  SwitchReason pending_reason_{};    // 让出原因（SwapFromTask 写）
};

}  // namespace dexvm::coroutine
