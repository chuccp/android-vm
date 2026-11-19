// Carrier / Scheduler（docs/art-runtime-v2/04-coroutine-runtime.md 14.4-14.8、14.12）
//
// M1 裁剪：单 OS 线程协作式——单 carrier、无多线程 carrier pool（14.7 pool 后置）、
// 无 IOReactor（kIo 与 kWait 同停放路径）；wake 可来自外部线程（IO/Binder 事件源），
// 全部调度状态由 mu_ 保护。
#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "coroutine/guest_task.h"

namespace dexvm::coroutine {

// 让出原因（14.7 SchedulerLoop switch reason；kSafepoint 为 14.12 停顿路径）
enum class SwitchReason : uint8_t {
  kYield,      // explicit coroutine yield
  kWait,       // wait/wake 协议让出（14.8）
  kIo,         // IO 等待（M1：与 kWait 同停放路径）
  kSleep,      // sleep/deadline -> TimerHeap（14.8）
  kPin,        // quantum 边界命中 native pin（14.9）：不释放 carrier
  kDone,       // task 结束
  kSafepoint,  // GC checkpoint 停顿（14.12）
};

// Carrier state（14.4）
enum class CarrierState : uint8_t {
  kIdle,
  kReady,
  kRunningTask,
  kPinnedToTask,
  kStopping,
};

// Carrier：宿主执行资源（14.4）。M1 裁剪：pthread_t host_thread / host_context /
// stack_boundary 后置；installed_thread 为 ART Thread* 占位。
struct Carrier {
  uint64_t carrier_id = 0;
  GuestTask* current_task = nullptr;
  void* installed_thread = nullptr;  // ART Thread*（M1 占位）
  CarrierState state = CarrierState::kIdle;
  uint32_t native_pin_count = 0;
};

class Scheduler {
 public:
  using Entry = GuestTask::Entry;

  Scheduler();
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  // 创建 GuestTask（lifecycle kCreated -> kRunnable 入 ReadyQueue）。
  // stack_bytes=0 取默认 fiber 栈；VM guest 解释执行可指定更大栈（spawn 面扩展）。
  GuestTask* Spawn(Entry entry, void* arg,
                   Priority priority = Priority::kP2,
                   size_t stack_bytes = 0);

  // SchedulerLoop（14.7）：跑至全部任务完成 / RequestStop / 超时。
  // 返回 true = 正常结束；false = 超时。
  bool Run(std::chrono::milliseconds timeout = std::chrono::milliseconds(30000));
  void RequestStop();

  // ---- wait / wake 协议（14.8）----
  // 等待：锁内查谓词 -> 注册 waiter + 置状态 -> 让出 -> 唤醒后锁内复查（防 lost wake）。
  // pred 在锁内调用，不得再调用调度器接口。
  template <typename Pred>
  void WaitWhile(WaitState kind, Pred&& pred) {
    std::unique_lock<std::mutex> lock(mu_);
    GuestTask* task = CurrentTaskLocked();
    while (!pred()) {
      RegisterWaiterLocked(task, kind);
      lock.unlock();
      SwapFromTask(task, kind == WaitState::kIo ? SwitchReason::kIo
                                                : SwitchReason::kWait);
      lock.lock();
    }
  }

  // 通用可超时等待（14.8 wait/wake + TimerHeap 复合）：谓词成立返回 true；
  // deadline（TimePoint::max() = 无期限）到期且谓词未成立返回 false。
  // 唤醒（同 kind wake / WakeAny / timer 到期）后一律重查谓词（防 lost wake）。
  // pred 在锁内调用，不得再调用调度器接口。
  template <typename Pred>
  bool WaitGen(WaitState kind, TimePoint deadline, Pred&& pred) {
    std::unique_lock<std::mutex> lock(mu_);
    GuestTask* task = CurrentTaskLocked();
    for (;;) {
      if (pred()) return true;
      if (deadline != TimePoint::max() && Now() >= deadline) return false;
      RegisterWaiterLocked(task, kind);
      if (deadline != TimePoint::max()) {
        task->deadline_ = deadline;
        RegisterTimerLocked(task, deadline);
      }
      lock.unlock();
      SwapFromTask(task, deadline == TimePoint::max()
                              ? (kind == WaitState::kIo ? SwitchReason::kIo
                                                        : SwitchReason::kWait)
                              : SwitchReason::kSleep);
      lock.lock();
    }
  }

  // 睡眠到 deadline（14.8 sleep/deadline -> TimerHeap）。
  void WaitUntil(TimePoint deadline);

  // wake：摘除 waiter + 置状态 + ReadyQueue.Push，全部在同一锁内（防 lost wake）。
  // 返回 true = 确实唤醒了 waiter；false = 未在等待该类事件。
  bool Wake(GuestTask* waiter, WaitState expected);

  // 任意 kind 唤醒（interrupt/unpark 语义）：不校验 wait_state，摘除即入
  // ReadyQueue。等待方谓词复查不成立时自行再等（Java spurious wakeup 允许）。
  // 返回 true = 确实唤醒了 waiter；false = 不在任何等待中。
  bool WakeAny(GuestTask* waiter);

  // 锁内变更谓词状态并 wake（14.8：wake 与谓词变更同锁）。
  template <typename Mutate>
  bool UpdateAndWake(GuestTask* waiter, WaitState expected, Mutate&& mutate) {
    std::lock_guard<std::mutex> lock(mu_);
    mutate();
    bool woke = WakeLocked(waiter, expected);
    if (woke) cv_.notify_all();
    return woke;
  }

  // ---- GC safepoint（14.12，M1 单线程协作式）----
  // 请求 checkpoint：任务在下个安全边界（PollSafepoint）响应停顿。
  void RequestCheckpoint(GuestTask* task);
  // GC 完成：SUSPENDED -> RUNNABLE 并入 ReadyQueue。
  void ResumeFromSafepoint(GuestTask* task);

  // ---- GC 保守栈扫描根枚举（docs 02 章 6.9）----
  // 全部存活 GuestTask 的 fiber 可用栈（跳过低地址 guard page，不可读）+
  // ucontext 寄存器保存区（callee-saved 可能持 guest 引用）+ 调度器换出点
  // 保存区。经 GuestSchedOps::visit_stacks 接缝供 gc.cc 消费。
  void VisitRootRanges(void (*add)(void* base, size_t bytes, void* arg), void* arg);

  // ---- 观测（测试 / 诊断）----
  Carrier* carrier() { return &carrier_; }
  const Carrier* carrier() const { return &carrier_; }
  size_t ReadyCount() const;
  size_t LiveFiberStacks() const;
  size_t completed_count() const;
  uint64_t switch_count() const;   // SwitchCarrierToTask 次数
  uint64_t release_count() const;  // ReleaseCarrier 次数

  // 当前正在执行的 task（carrier 绑定；非执行线程为 nullptr）
  static GuestTask* CurrentTask();
  static TimePoint Now();

 private:
  friend class GuestTask;

  // 任务侧让出入口：置让出原因后 swap 回调度器（resume 后返回）。
  void SwapFromTask(GuestTask* task, SwitchReason reason);
  //14.5 carrier switch（M1 裁剪 ART TLS/entrypoints 重装）：断言 + 绑定 + 置状态。
  void SwitchCarrierToTask(Carrier* carrier, GuestTask* task);
  // 14.7 修订语义：仅清 current_task / installed_thread，carrier 归位 IDLE 不销毁。
  void ReleaseCarrier(Carrier* carrier);
  SwitchReason RunTaskQuantum(GuestTask* task);

  // ---- 锁内簿记（调用方持 mu_）----
  GuestTask* CurrentTaskLocked() const;
  void ReadyPushLocked(GuestTask* task);
  GuestTask* ReadyPopLocked();
  void RegisterWaiterLocked(GuestTask* task, WaitState kind);
  void RegisterTimerLocked(GuestTask* task, TimePoint deadline);
  void RemoveWaiterLocked(GuestTask* task);
  bool WakeLocked(GuestTask* waiter, WaitState expected);
  void CheckTimersLocked(TimePoint now);
  void ParkAfterWaitLocked(GuestTask* task);   // kWait/kIo/kSleep 让出后的停放确认
  void ParkSuspendedLocked(GuestTask* task);   // kSafepoint 停顿
  void CompleteTaskLocked(GuestTask* task);
  bool AllWorkDoneLocked() const;
  bool WaitForEventLocked(std::unique_lock<std::mutex>& lock, TimePoint run_deadline);
  uint32_t EffectivePriorityLocked(const GuestTask* task) const;

  struct TimerEntry {
    GuestTask* task;
    uint64_t epoch;
  };

  static constexpr size_t kPriorityLevels = 4;
  static constexpr uint64_t kAgingInterval = 8;  // 每 8 个 pop 提升一级（防饿死）

  mutable std::mutex mu_;
  std::condition_variable cv_;

  Carrier carrier_;
  UContextStorage sched_uc_{};  // 调度器上下文（fiber swap 对端）

  std::vector<std::unique_ptr<GuestTask>> tasks_;  // 所有权（对象保留至析构）
  std::deque<GuestTask*> ready_[kPriorityLevels];  // 14.7 ReadyQueue：priority+FIFO
  std::unordered_map<WaitState, std::vector<GuestTask*>> waiters_;  // 14.8 WaitQueue
  std::multimap<TimePoint, TimerEntry> timers_;                     // 14.8 TimerHeap
  std::vector<GuestTask*> suspended_;                               // 14.12 停顿

  uint64_t next_task_id_ = 0;
  uint64_t ready_seq_counter_ = 0;
  uint64_t pop_tick_ = 0;
  size_t live_fiber_stacks_ = 0;
  size_t completed_count_ = 0;
  uint64_t switch_count_ = 0;
  uint64_t release_count_ = 0;
  bool runtime_running_ = false;

  static thread_local GuestTask* tls_current_task_;
};

}  // namespace dexvm::coroutine
