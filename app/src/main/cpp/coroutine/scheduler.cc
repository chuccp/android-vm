// Carrier / Scheduler 实现（docs 04 章 14.4-14.8、14.12）
#include "coroutine/scheduler.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

#include "core/builtins.h"  // M4.4 VM scheduler 接线（GuestSchedOps POD 面）

namespace dexvm::coroutine {

namespace {

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::fprintf(stderr, "CHECK failed: %s (%s:%d)\n", #cond, __FILE__, \
                   __LINE__);                                            \
      std::abort();                                                      \
    }                                                                    \
  } while (0)

}  // namespace

thread_local GuestTask* Scheduler::tls_current_task_ = nullptr;

Scheduler::Scheduler() {
  carrier_.carrier_id = 1;
  // fiber 兜底返回上下文（正常路径经 SwapFromTask，不走 uc_link）
  UContextGet(&sched_uc_);
}

Scheduler::~Scheduler() {
  RequestStop();
  tasks_.clear();  // 先回收 task（含 fiber 栈），live_fiber_stacks_ 尚有效
}

GuestTask* Scheduler::Spawn(Entry entry, void* arg, Priority priority,
                            size_t stack_bytes) {
  std::lock_guard<std::mutex> lock(mu_);
  uint64_t id = ++next_task_id_;
  auto task = std::make_unique<GuestTask>(this, id, entry, arg, priority);
  if (!task->InitFiber(stack_bytes)) return nullptr;
  live_fiber_stacks_++;
  GuestTask* raw = task.get();
  tasks_.push_back(std::move(task));
  raw->lifecycle_state_ = LifecycleState::kRunnable;
  ReadyPushLocked(raw);
  cv_.notify_all();
  return raw;
}

void Scheduler::RequestStop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    runtime_running_ = false;
  }
  cv_.notify_all();
}

GuestTask* Scheduler::CurrentTask() { return tls_current_task_; }

TimePoint Scheduler::Now() { return std::chrono::steady_clock::now(); }

size_t Scheduler::ReadyCount() const {
  std::lock_guard<std::mutex> lock(mu_);
  size_t n = 0;
  for (const auto& q : ready_) n += q.size();
  return n;
}

size_t Scheduler::LiveFiberStacks() const {
  std::lock_guard<std::mutex> lock(mu_);
  return live_fiber_stacks_;
}

size_t Scheduler::completed_count() const {
  std::lock_guard<std::mutex> lock(mu_);
  return completed_count_;
}

uint64_t Scheduler::switch_count() const {
  std::lock_guard<std::mutex> lock(mu_);
  return switch_count_;
}

uint64_t Scheduler::release_count() const {
  std::lock_guard<std::mutex> lock(mu_);
  return release_count_;
}

// ---- SchedulerLoop（14.7）----

bool Scheduler::Run(std::chrono::milliseconds timeout) {
  const TimePoint run_deadline =
      timeout >= std::chrono::milliseconds::max() ? TimePoint::max()
                                                  : Now() + timeout;
  {
    std::lock_guard<std::mutex> lock(mu_);
    runtime_running_ = true;
    carrier_.state = CarrierState::kReady;
  }

  std::unique_lock<std::mutex> lock(mu_);
  while (runtime_running_) {
    CheckTimersLocked(Now());
    if (AllWorkDoneLocked()) break;
    if (run_deadline != TimePoint::max() && Now() >= run_deadline) {
      lock.unlock();
      return false;  // 超时（疑似死锁 / 事件丢失）
    }

    GuestTask* task = ReadyPopLocked();
    if (task == nullptr) {
      if (!WaitForEventLocked(lock, run_deadline)) return false;
      continue;
    }

    SwitchCarrierToTask(&carrier_, task);
    lock.unlock();
    SwitchReason reason = RunTaskQuantum(task);
    while (reason == SwitchReason::kPin) {
      // 14.7 PIN：不释放 carrier、不切换 task，继续该 task 的 native frame
      CHECK(carrier_.current_task == task);
      CHECK(task->native_state_ == NativeState::kPinnedNative);
      CHECK(task->execution_state_ == ExecutionState::kRunningOnCarrier);
      reason = RunTaskQuantum(task);
    }
    lock.lock();

    switch (reason) {
      case SwitchReason::kYield:
        ReadyPushLocked(task);
        ReleaseCarrier(&carrier_);
        break;
      case SwitchReason::kWait:
      case SwitchReason::kIo:
      case SwitchReason::kSleep:
        ParkAfterWaitLocked(task);
        ReleaseCarrier(&carrier_);
        break;
      case SwitchReason::kDone:
        CompleteTaskLocked(task);
        ReleaseCarrier(&carrier_);
        break;
      case SwitchReason::kSafepoint:
        ParkSuspendedLocked(task);
        ReleaseCarrier(&carrier_);
        break;
      case SwitchReason::kPin:
        CHECK(false);  // 上面的 while 已消化
        break;
    }
  }

  carrier_.state = CarrierState::kStopping;
  return true;
}

SwitchReason Scheduler::RunTaskQuantum(GuestTask* task) {
  CHECK(tls_current_task_ == task);
  CHECK(task->fiber_.ready);
  UContextSwap(&sched_uc_, &task->fiber_.uc);
  // tls 绑定随 carrier（SwitchCarrierToTask 装 / ReleaseCarrier 卸）：
  // PIN 量子换出后需继续绑定该 task（14.7 不释放 carrier）
  return task->pending_reason_;
}

// ---- 14.5 carrier switch（M1 裁剪 ART TLS/entrypoints/card table 重装）----

void Scheduler::SwitchCarrierToTask(Carrier* carrier, GuestTask* task) {
  CHECK(carrier->current_task == nullptr);           // 14.5 断言
  CHECK(task->execution_state_ == ExecutionState::kNotRunning);
  CHECK(task->native_state_ == NativeState::kNone);
  CHECK(task->gc_state_ != GcState::kSuspended);
  CHECK(task->lifecycle_state_ == LifecycleState::kRunnable);

  carrier->current_task = task;
  carrier->installed_thread = nullptr;  // ART Thread* 安装后置
  carrier->state = CarrierState::kRunningTask;
  task->current_carrier_ = carrier;
  task->execution_state_ = ExecutionState::kRunningOnCarrier;
  tls_current_task_ = task;
  switch_count_++;
}

// 14.7 修订语义：仅清 current_task / installed_thread
void Scheduler::ReleaseCarrier(Carrier* carrier) {
  carrier->current_task = nullptr;
  carrier->installed_thread = nullptr;
  carrier->state = CarrierState::kIdle;
  tls_current_task_ = nullptr;
  release_count_++;
}

void Scheduler::SwapFromTask(GuestTask* task, SwitchReason reason) {
  CHECK(task == tls_current_task_);
  {
    std::lock_guard<std::mutex> lock(mu_);
    task->pending_reason_ = reason;
    if (reason != SwitchReason::kPin) {
      // pin 让出（kPin）保持 carrier 绑定（14.7/14.9）；其余让出离开 carrier
      task->execution_state_ = ExecutionState::kNotRunning;
      task->current_carrier_ = nullptr;
    }
  }
  UContextSwap(&task->fiber_.uc, &sched_uc_);
  // 恢复执行后返回调用者
}

// ---- ReadyQueue：priority 四级 + FIFO + aging（14.7）----

uint32_t Scheduler::EffectivePriorityLocked(const GuestTask* task) const {
  uint64_t waited = pop_tick_ - task->ready_enq_tick_;
  uint64_t boost = waited / kAgingInterval;
  uint32_t base = static_cast<uint32_t>(task->priority_);
  return boost >= base ? 0 : static_cast<uint32_t>(base - boost);
}

void Scheduler::ReadyPushLocked(GuestTask* task) {
  CHECK(task->lifecycle_state_ == LifecycleState::kRunnable);
  if (task->in_ready_) return;  // 防重复入队
  task->in_ready_ = true;
  task->ready_seq_ = ++ready_seq_counter_;
  task->ready_enq_tick_ = pop_tick_;
  ready_[static_cast<size_t>(task->priority_)].push_back(task);
}

GuestTask* Scheduler::ReadyPopLocked() {
  // 选 effective priority 最小者；同级 FIFO（ready_seq_ 小者优先）。
  // 内部跳过已取消任务（惰性回收）。
  GuestTask* best = nullptr;
  uint32_t best_eff = 0;
  for (auto& q : ready_) {
    for (GuestTask* t : q) {
      if (t->lifecycle_state_ != LifecycleState::kRunnable) continue;
      uint32_t eff = EffectivePriorityLocked(t);
      if (best == nullptr || eff < best_eff ||
          (eff == best_eff && t->ready_seq_ < best->ready_seq_)) {
        best = t;
        best_eff = eff;
      }
    }
  }
  if (best == nullptr) return nullptr;
  auto& q = ready_[static_cast<size_t>(best->priority_)];
  for (auto it = q.begin(); it != q.end(); ++it) {
    if (*it == best) {
      q.erase(it);
      break;
    }
  }
  best->in_ready_ = false;
  pop_tick_++;
  return best;
}

// ---- WaitQueue / TimerHeap / wait-wake 协议（14.8）----

void Scheduler::RegisterWaiterLocked(GuestTask* task, WaitState kind) {
  CHECK(kind != WaitState::kNone);
  CHECK(task->wait_state_ == WaitState::kNone);
  CHECK(!task->in_wait_bucket_ && !task->in_timer_);
  task->wait_state_ = kind;
  task->in_wait_bucket_ = true;
  task->wait_epoch_++;
  waiters_[kind].push_back(task);
}

void Scheduler::RegisterTimerLocked(GuestTask* task, TimePoint deadline) {
  CHECK(!task->in_timer_);
  task->in_timer_ = true;
  task->wait_epoch_++;
  timers_.emplace(deadline, TimerEntry{task, task->wait_epoch_});
}

void Scheduler::RemoveWaiterLocked(GuestTask* task) {
  if (task->in_wait_bucket_) {
    auto it = waiters_.find(task->wait_state_);
    if (it != waiters_.end()) {
      auto& v = it->second;
      for (auto vit = v.begin(); vit != v.end(); ++vit) {
        if (*vit == task) {
          v.erase(vit);
          break;
        }
      }
    }
    task->in_wait_bucket_ = false;
  }
  if (task->in_timer_) {
    task->wait_epoch_++;  // 使 timer 堆中残留项失效（惰性删除）
    task->in_timer_ = false;
  }
}

bool Scheduler::WakeLocked(GuestTask* waiter, WaitState expected) {
  if (waiter->lifecycle_state_ != LifecycleState::kRunnable) return false;
  if (waiter->wait_state_ != expected) return false;  // 不在等该类事件
  if (!waiter->in_wait_bucket_ && !waiter->in_timer_) return false;

  // 14.8 wake 流程：同一锁内摘除 + 置状态 + ReadyQueue.Push
  RemoveWaiterLocked(waiter);
  waiter->wait_state_ = WaitState::kNone;
  waiter->execution_state_ = ExecutionState::kNotRunning;
  ReadyPushLocked(waiter);
  return true;
}

bool Scheduler::Wake(GuestTask* waiter, WaitState expected) {
  std::lock_guard<std::mutex> lock(mu_);
  bool woke = WakeLocked(waiter, expected);
  if (woke) cv_.notify_all();
  return woke;
}

bool Scheduler::WakeAny(GuestTask* waiter) {
  std::lock_guard<std::mutex> lock(mu_);
  if (waiter->lifecycle_state_ != LifecycleState::kRunnable) return false;
  if (!waiter->in_wait_bucket_ && !waiter->in_timer_) return false;
  // 14.8 wake 流程（不校验 kind）：同一锁内摘除 + 置状态 + ReadyQueue.Push
  RemoveWaiterLocked(waiter);
  waiter->wait_state_ = WaitState::kNone;
  waiter->execution_state_ = ExecutionState::kNotRunning;
  ReadyPushLocked(waiter);
  cv_.notify_all();
  return true;
}

void Scheduler::WaitUntil(TimePoint deadline) {
  std::unique_lock<std::mutex> lock(mu_);
  GuestTask* task = CurrentTaskLocked();
  CHECK(task->native_state_ == NativeState::kNone);  // pin 期间不得睡眠
  while (Now() < deadline) {
    task->deadline_ = deadline;
    if (task->wait_state_ == WaitState::kNone) {
      RegisterWaiterLocked(task, WaitState::kDeadline);
    }
    if (!task->in_timer_) {
      RegisterTimerLocked(task, deadline);
    }
    lock.unlock();
    SwapFromTask(task, SwitchReason::kSleep);
    lock.lock();
  }
}

void Scheduler::CheckTimersLocked(TimePoint now) {
  while (!timers_.empty() && timers_.begin()->first <= now) {
    TimerEntry entry = timers_.begin()->second;
    timers_.erase(timers_.begin());
    GuestTask* t = entry.task;
    // 陈旧判定只看 in_timer_ + epoch（RemoveWaiter/RegisterWaiter 均换代）：
    // 复合等待（WaitGen：kMonitor/kMutex/kJoin + timer）同样按 deadline 唤醒，
    // 唤醒后由等待方谓词复查区分 notify / timeout / interrupt。
    if (!t->in_timer_ || t->wait_epoch_ != entry.epoch) {
      continue;  // 惰性删除的陈旧项
    }
    // 14.8 wake 流程（deadline 到期）
    RemoveWaiterLocked(t);
    t->wait_state_ = WaitState::kNone;
    t->execution_state_ = ExecutionState::kNotRunning;
    ReadyPushLocked(t);
    cv_.notify_all();
  }
}

void Scheduler::ParkAfterWaitLocked(GuestTask* task) {
  // 14.8 防 lost wake 的让出窗口：SwapFromTask 与停放之间被 wake 时，
  // wait_state 已置 kNone 且已入 ReadyQueue，不得再停放。
  if (task->wait_state_ == WaitState::kNone) {
    CHECK(task->in_ready_);
    return;
  }
  CHECK(task->in_wait_bucket_ || task->in_timer_);
}

void Scheduler::ParkSuspendedLocked(GuestTask* task) {
  // 14.12：gc_state 已由 PollSafepoint 置 SUSPENDED
  CHECK(task->gc_state_ == GcState::kSuspended);
  CHECK(task->wait_state_ == WaitState::kNone);
  suspended_.push_back(task);
}

// ---- GC safepoint（14.12，M1 协作式）----

void Scheduler::RequestCheckpoint(GuestTask* task) {
  std::lock_guard<std::mutex> lock(mu_);
  if (task->lifecycle_state_ != LifecycleState::kRunnable) return;
  if (task->gc_state_ != GcState::kRunnable) return;
  // 管理态 RUNNABLE -> 请求停顿；WAITING 任务直接扫 root（14.12），
  // PINNED_NATIVE 任务在返回 managed 时观察（PollSafepoint pin 路径）。
  task->gc_state_ = GcState::kSafepointRequested;
}

void Scheduler::ResumeFromSafepoint(GuestTask* task) {
  std::lock_guard<std::mutex> lock(mu_);
  CHECK(task->gc_state_ == GcState::kSuspended);
  task->gc_state_ = GcState::kRunnable;
  for (auto it = suspended_.begin(); it != suspended_.end(); ++it) {
    if (*it == task) {
      suspended_.erase(it);
      break;
    }
  }
  ReadyPushLocked(task);
  cv_.notify_all();
}

// ---- GC 保守栈扫描根枚举（docs 02 章 6.9）----
// 任务执行期间 mu_ 不持有（Run 换入前 unlock）——GC 触发点（分配失败的
// allocator 入口）在任务上下文，本方法可安全加锁。
void Scheduler::VisitRootRanges(void (*add)(void* base, size_t bytes, void* arg), void* arg) {
  std::lock_guard<std::mutex> lock(mu_);
  // 调度器换出点寄存器保存区（Run 主循环帧的 callee-saved）
  add(sched_uc_.bytes, sizeof(sched_uc_.bytes), arg);
  for (auto& tp : tasks_) {
    GuestTask* t = tp.get();
    FiberContext& f = t->fiber_;
    if (f.stack_base != nullptr && f.stack_size > 0) {
      // 可用栈区（低地址 guard page PROT_NONE 不可读，须跳过）
      size_t guard = f.map_size > f.stack_size ? f.map_size - f.stack_size : 0;
      add(f.stack_base + guard, f.stack_size, arg);
    }
    // ucontext 寄存器保存区（fiber 换出点 callee-saved 可能持 guest 引用）
    add(f.uc.bytes, sizeof(f.uc.bytes), arg);
  }
}

// ---- 完成回收（14.2）----

void Scheduler::CompleteTaskLocked(GuestTask* task) {
  CHECK(task->pending_reason_ == SwitchReason::kDone);
  task->lifecycle_state_ = LifecycleState::kCompleted;
  task->execution_state_ = ExecutionState::kNotRunning;
  task->current_carrier_ = nullptr;
  task->ReclaimFiber();  // 回收 fiber 栈
  completed_count_++;
}

// ---- idle / 事件等待 ----

bool Scheduler::AllWorkDoneLocked() const {
  for (const auto& q : ready_) {
    if (!q.empty()) return false;
  }
  if (!timers_.empty()) return false;
  if (!suspended_.empty()) return false;
  for (const auto& [kind, v] : waiters_) {
    (void)kind;
    if (!v.empty()) return false;
  }
  return true;
}

bool Scheduler::WaitForEventLocked(std::unique_lock<std::mutex>& lock,
                                   TimePoint run_deadline) {
  // 14.7 wait_for_event_or_idle_timeout：空闲时等 wake / timer / 超时
  TimePoint wake_at = run_deadline;
  if (!timers_.empty() && timers_.begin()->first < wake_at) {
    wake_at = timers_.begin()->first;
  }
  if (wake_at == TimePoint::max()) {
    cv_.wait(lock);
  } else {
    cv_.wait_until(lock, wake_at);
  }
  return true;
}

GuestTask* Scheduler::CurrentTaskLocked() const {
  GuestTask* task = tls_current_task_;
  CHECK(task != nullptr);  // wait 只能由运行中的 task 发起
  return task;
}

}  // namespace dexvm::coroutine

// ---- M4.4 VM scheduler 接线（GuestTask/调度接线，docs 04 章 14.2/14.8）----
// core/builtins.cc 的 Thread/Object/Unsafe 内建经 core/builtins.h GuestSchedOps
// 调入本实现（Spawn/WaitGen/Wake/Yield 面）。链入本 TU 的二进制
// （thread_wait 冒烟、设备 dexvm_coroutine）调 InstallVmSchedBackend 装载；
// 不链 coroutine/ 的既有宿主测试脚本零改（VM 侧自动退化旧单流）。
namespace {

using dexvm::coroutine::FiberContext;
using dexvm::coroutine::GuestTask;
using dexvm::coroutine::Priority;
using dexvm::coroutine::Scheduler;
using dexvm::coroutine::TimePoint;
using dexvm::coroutine::WaitState;

Scheduler* g_vm_sched = nullptr;  // run_main 期间有效（M1 单执行流）

struct VmEntryPayload {
  void (*entry)(void*, void*);
  void* arg;
};

void VmEntryTramp(GuestTask* task, void* raw) {
  VmEntryPayload* p = static_cast<VmEntryPayload*>(raw);
  void (*entry)(void*, void*) = p->entry;
  void* arg = p->arg;
  delete p;
  entry(task, arg);
}

WaitState MapWaitKind(int kind) {
  switch (kind) {
    case 1: return WaitState::kMutex;
    case 2: return WaitState::kMonitor;
    case 3: return WaitState::kCondition;
    case 4: return WaitState::kDeadline;
    case 5: return WaitState::kJoin;
    default: return WaitState::kCondition;
  }
}

bool OpsRunMain(void (*entry)(void*, void*), void* arg, unsigned timeout_ms) {
  Scheduler sched;
  g_vm_sched = &sched;
  auto* p = new VmEntryPayload{entry, arg};
  // main GuestTask 与 Thread.start spawn 同口径（kGuestStackBytes=4MB）。
  // 【配套缓解，非根因结论】业务代码不应深递归；若观测到栈深增长/溢出，
  // 根因是语义偏差制造的人工递归或终止条件失效（见 CrashGuard 帧链形态
  // 诊断 + jni_deep 压力）。栈口径只统一，不掩盖：溢出一律经解释器栈护栏
  // 抛 StackOverflowError 归因，不靠加大栈吞掉。
  GuestTask* main_task =
      sched.Spawn(&VmEntryTramp, p, Priority::kP1, dexvm::runtime::kGuestStackBytes);
  if (main_task == nullptr) {
    delete p;
    g_vm_sched = nullptr;
    return false;
  }
  bool ok = sched.Run(std::chrono::milliseconds(timeout_ms));
  g_vm_sched = nullptr;
  return ok;
}

void* OpsSpawn(void (*entry)(void*, void*), void* arg, unsigned stack_bytes) {
  if (g_vm_sched == nullptr) return nullptr;
  auto* p = new VmEntryPayload{entry, arg};
  GuestTask* task = g_vm_sched->Spawn(&VmEntryTramp, p, Priority::kP2, stack_bytes);
  if (task == nullptr) delete p;
  return task;
}

bool OpsWait(int kind, long long deadline_ms, bool (*pred)(void*), void* arg) {
  Scheduler* s = g_vm_sched;
  if (s == nullptr) return pred(arg);
  // deadline_ms = now_ms() 约定的绝对单调时刻（与 TimePoint 同 steady epoch）；
  // <0 = 无期限。
  TimePoint dl = deadline_ms < 0
                     ? TimePoint::max()
                     : TimePoint(std::chrono::milliseconds(deadline_ms));
  return s->WaitGen(MapWaitKind(kind), dl, [&] { return pred(arg); });
}

void OpsWake(void* task, int kind) {
  if (g_vm_sched == nullptr || task == nullptr) return;
  GuestTask* t = static_cast<GuestTask*>(task);
  if (kind == 0) {
    g_vm_sched->WakeAny(t);
  } else {
    g_vm_sched->Wake(t, MapWaitKind(kind));
  }
}

bool OpsYieldNow() {
  GuestTask* t = Scheduler::CurrentTask();
  if (t == nullptr || g_vm_sched == nullptr) return false;
  return t->Yield();
}

bool OpsHasReady() { return g_vm_sched != nullptr && g_vm_sched->ReadyCount() > 0; }

void OpsRequestStop() {
  if (g_vm_sched != nullptr) g_vm_sched->RequestStop();
}

void* OpsCurrentTask() { return Scheduler::CurrentTask(); }

void OpsVisitStacks(void (*add)(void* base, size_t bytes, void* arg), void* arg) {
  if (g_vm_sched != nullptr) g_vm_sched->VisitRootRanges(add, arg);
}

// 当前 guest task 的 fiber 栈边界（解释器 StackOverflowError 预检消费）：
// low = guard 页下沿（可用栈底），high = mmap 顶端。非 task 上下文 = false。
bool OpsCurrentStackBounds(uintptr_t* low, uintptr_t* high) {
  GuestTask* t = Scheduler::CurrentTask();
  if (t == nullptr) return false;
  const FiberContext& f = t->fiber();
  if (f.stack_base == nullptr) return false;
  if (low != nullptr) *low = reinterpret_cast<uintptr_t>(f.guard_page != nullptr ? f.guard_page : f.stack_base);
  if (high != nullptr) *high = reinterpret_cast<uintptr_t>(f.stack_base + f.map_size);
  return true;
}

long long OpsNowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             Scheduler::Now().time_since_epoch())
      .count();
}

}  // namespace

namespace dexvm::runtime {

// 接线表出口：消费方（thread_wait 冒烟 / jni/runtime_contract 设备考）自行
// SetGuestSchedOps(VmSchedBackendOps()) 装载——本 TU 不引用 builtins.cc 符号，
// 既有宿主脚本（不链 builtins 的 coroutine_tests 等）零改。
const GuestSchedOps* VmSchedBackendOps() {
  static const GuestSchedOps kOps = {
      &OpsRunMain,  &OpsSpawn,     &OpsWait,      &OpsWake,       &OpsYieldNow,
      &OpsHasReady, &OpsRequestStop, &OpsCurrentTask, &OpsNowMs, &OpsVisitStacks,
      &OpsCurrentStackBounds,
  };
  return &kOps;
}

}  // namespace dexvm::runtime
