// 协程运行时冒烟测试（docs/art-runtime-v2/04-coroutine-runtime.md 14 章 M1 面）：
//   1) 多任务交替执行顺序（priority 四级 + FIFO）
//   2) 优先级 + aging（低优先级最终前进）
//   3) wait/wake 计数平衡（1000 轮，外部线程 wake，防 lost wake）
//   4) deadline 定时唤醒（TimerHeap，不早醒）
//   5) PIN 期间 carrier 不切换 / 不释放（14.7 PIN + 14.9）
//   6) LeaveNative 两条路径（14.9 修订语义）
//   7) safepoint 暂停恢复（14.12）
//   8) task 完成回收（14.2）
// 宿主编译运行：tools/coroutine_tests.sh
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "coroutine/guest_task.h"
#include "coroutine/scheduler.h"

namespace {

using dexvm::coroutine::CarrierState;
using dexvm::coroutine::ExecutionState;
using dexvm::coroutine::GcState;
using dexvm::coroutine::GuestTask;
using dexvm::coroutine::LifecycleState;
using dexvm::coroutine::NativeExit;
using dexvm::coroutine::Priority;
using dexvm::coroutine::Scheduler;
using dexvm::coroutine::TimePoint;
using dexvm::coroutine::WaitState;
using namespace std::chrono_literals;

const char* g_test = "";
int g_failures = 0;

#define CHECK(cond, msg)                                                  \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("[FAIL] %s: %s (%s:%d)\n", g_test, msg, __FILE__,       \
                  __LINE__);                                              \
      g_failures++;                                                       \
    } else {                                                              \
      std::printf("[PASS] %s: %s\n", g_test, msg);                        \
    }                                                                     \
  } while (0)

void ExpectLog(const std::vector<std::string>& got,
               const std::vector<std::string>& want, const char* msg) {
  bool eq = got == want;
  CHECK(eq, msg);
  if (!eq) {
    std::printf("  want:");
    for (const auto& s : want) std::printf(" %s", s.c_str());
    std::printf("\n  got: ");
    for (const auto& s : got) std::printf(" %s", s.c_str());
    std::printf("\n");
  }
}

void ExpectLog(const std::vector<int>& got, const std::vector<int>& want,
               const char* msg) {
  bool eq = got == want;
  CHECK(eq, msg);
  if (!eq) {
    std::printf("  want:");
    for (int v : want) std::printf(" %d", v);
    std::printf("\n  got: ");
    for (int v : got) std::printf(" %d", v);
    std::printf("\n");
  }
}

// ---- 1) 多任务交替执行顺序 ----

struct AltCtx {
  int id;
  std::vector<int>* log;
};

void AltEntry(GuestTask* t, void* arg) {
  auto* c = static_cast<AltCtx*>(arg);
  for (int r = 0; r < 5; ++r) {
    c->log->push_back(c->id * 10 + r);
    t->Yield();
  }
}

void TestAlternatingOrder() {
  g_test = "alternating_order";
  Scheduler sched;
  std::vector<int> log;
  AltCtx a{1, &log}, b{2, &log}, c{3, &log};
  CHECK(sched.Spawn(AltEntry, &a, Priority::kP2) != nullptr, "spawn A");
  CHECK(sched.Spawn(AltEntry, &b, Priority::kP2) != nullptr, "spawn B");
  CHECK(sched.Spawn(AltEntry, &c, Priority::kP2) != nullptr, "spawn C");
  bool ok = sched.Run(10s);
  CHECK(ok, "run finished");
  std::vector<int> want;
  for (int r = 0; r < 5; ++r) {
    for (int id = 1; id <= 3; ++id) want.push_back(id * 10 + r);
  }
  ExpectLog(log, want, "同优先级严格 FIFO 轮转 A/B/C");
  CHECK(sched.completed_count() == 3, "3 tasks completed");
  CHECK(sched.LiveFiberStacks() == 0, "fiber stacks reclaimed");
}

// ---- 2) 优先级 + aging ----

struct LoopCtx {
  int id;
  int rounds;
  std::vector<int>* log;
};

void OnceEntry(GuestTask*, void* arg) {
  auto* c = static_cast<LoopCtx*>(arg);
  c->log->push_back(c->id);
}

void LoopEntry(GuestTask* t, void* arg) {
  auto* c = static_cast<LoopCtx*>(arg);
  for (int r = 0; r < c->rounds; ++r) {
    c->log->push_back(c->id);
    t->Yield();
  }
}

void TestPriorityAndAging() {
  g_test = "priority_and_aging";

  // 2a) 纯优先级：低 id = 高优先级，后入队的高优先级先跑
  {
    Scheduler sched;
    std::vector<int> log;
    LoopCtx p3{3, 1, &log}, p2{2, 1, &log}, p1{1, 1, &log}, p0{0, 1, &log};
    sched.Spawn(OnceEntry, &p3, Priority::kP3);
    sched.Spawn(OnceEntry, &p2, Priority::kP2);
    sched.Spawn(OnceEntry, &p1, Priority::kP1);
    sched.Spawn(OnceEntry, &p0, Priority::kP0);
    bool ok = sched.Run(10s);
    CHECK(ok, "2a run finished");
    ExpectLog(log, std::vector<int>{0, 1, 2, 3}, "2a 优先级顺序 P0>P1>P2>P3");
  }

  // 2b) aging：P0/P1/P2 持续让出，P3 最终必须前进（防饿死）
  {
    Scheduler sched;
    std::vector<int> log;
    LoopCtx a{0, 60, &log}, b{1, 60, &log}, c{2, 60, &log}, d{3, 1, &log};
    sched.Spawn(LoopEntry, &a, Priority::kP0);
    sched.Spawn(LoopEntry, &b, Priority::kP1);
    sched.Spawn(LoopEntry, &c, Priority::kP2);
    sched.Spawn(LoopEntry, &d, Priority::kP3);
    bool ok = sched.Run(10s);
    CHECK(ok, "2b run finished");

    size_t d_pos = log.size();
    for (size_t i = 0; i < log.size(); ++i) {
      if (log[i] == 3) {
        d_pos = i;
        break;
      }
    }
    CHECK(d_pos >= 3, "2b 低优先级不是最先执行");
    CHECK(d_pos < 45, "2b aging：低优先级在有限步内前进（防饿死）");
    CHECK(log.size() == 181, "2b 总步数 = 60*3 + 1");
    CHECK(sched.completed_count() == 4, "2b all completed");
  }
}

// ---- 3) wait/wake 计数平衡（1000 轮，防 lost wake）----

struct WwCtx {
  Scheduler* sched;
  GuestTask* consumer = nullptr;
  int produced = 0;  // waker 在调度器锁内写（UpdateAndWake）
  int consumed = 0;  // consumer 自己写
  int wake_hit = 0;
  int wake_miss = 0;
};

void WwConsumerEntry(GuestTask*, void* arg) {
  auto* c = static_cast<WwCtx*>(arg);
  while (c->consumed < 1000) {
    // 14.8：锁内查谓词 -> 注册 -> 让出 -> 唤醒后锁内复查
    c->sched->WaitWhile(WaitState::kCondition,
                        [c] { return c->produced > c->consumed; });
    c->consumed++;
  }
}

void TestWaitWakeBalance() {
  g_test = "wait_wake_balance_1000";
  Scheduler sched;
  WwCtx ctx;
  ctx.sched = &sched;
  ctx.consumer = sched.Spawn(WwConsumerEntry, &ctx, Priority::kP2);
  CHECK(ctx.consumer != nullptr, "spawn consumer");

  std::thread waker([&] {
    for (int i = 0; i < 1000; ++i) {
      bool hit = sched.UpdateAndWake(ctx.consumer, WaitState::kCondition,
                                     [&ctx] { ctx.produced++; });
      if (hit) {
        ctx.wake_hit++;
      } else {
        ctx.wake_miss++;
      }
    }
  });

  bool ok = sched.Run(15s);
  waker.join();
  CHECK(ok, "run finished（超时 = lost wake）");
  CHECK(ctx.produced == 1000, "produced == 1000");
  CHECK(ctx.consumed == 1000, "consumed == 1000（计数平衡，无 lost wake）");
  CHECK(ctx.wake_hit + ctx.wake_miss == 1000, "wake 总数 = 1000");
}

// ---- 4) deadline 定时唤醒 ----

struct SleepCtx {
  Scheduler* sched;
  TimePoint t0;
  std::chrono::milliseconds delay;
  std::string name;
  std::vector<std::string>* log;
  long long wake_ms = -1;
};

void SleeperEntry(GuestTask*, void* arg) {
  auto* c = static_cast<SleepCtx*>(arg);
  c->sched->WaitUntil(c->t0 + c->delay);
  auto elapsed = Scheduler::Now() - c->t0;
  c->wake_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  c->log->push_back(c->name);
}

void SpinEntry(GuestTask* t, void* arg) {
  auto* spins = static_cast<int*>(arg);
  for (int i = 0; i < 3; ++i) {
    (*spins)++;
    t->Yield();
  }
}

void TestDeadlineWake() {
  g_test = "deadline_wake";
  Scheduler sched;
  std::vector<std::string> log;
  TimePoint t0 = Scheduler::Now();
  SleepCtx s1{&sched, t0, 50ms, "S1", &log, -1};
  SleepCtx s2{&sched, t0, 20ms, "S2", &log, -1};
  int spins = 0;
  sched.Spawn(SleeperEntry, &s1, Priority::kP2);
  sched.Spawn(SleeperEntry, &s2, Priority::kP2);
  sched.Spawn(SpinEntry, &spins, Priority::kP2);
  bool ok = sched.Run(10s);
  CHECK(ok, "run finished");
  ExpectLog(log, std::vector<std::string>{"S2", "S1"}, "TimerHeap 按 deadline 先后唤醒");
  CHECK(s2.wake_ms >= 20, "S2 不早醒（>= 20ms）");
  CHECK(s1.wake_ms >= 50, "S1 不早醒（>= 50ms）");
  CHECK(s2.wake_ms < 1000 && s1.wake_ms < 1000, "定时唤醒不显著迟到");
  CHECK(spins == 3, "睡眠期间其余任务正常推进");
}

// ---- 5) PIN 期间 carrier 不切换 / 不释放 ----

struct PinCtx {
  Scheduler* sched;
  GuestTask* peer = nullptr;
  int peer_progress = 0;
  bool ok = true;
  std::vector<std::string>* log;
};

void PinPeerEntry(GuestTask*, void* arg) {
  auto* c = static_cast<PinCtx*>(arg);
  c->peer_progress++;
  c->log->push_back("peer");
}

void PinMainEntry(GuestTask* t, void* arg) {
  auto* c = static_cast<PinCtx*>(arg);
  c->log->push_back("T.enter");

  t->EnterNative();
  auto* carrier = t->current_carrier();
  if (carrier == nullptr || carrier->state != CarrierState::kPinnedToTask ||
      carrier->native_pin_count != 1 || carrier->current_task != t) {
    c->ok = false;
  }
  // 14.9：pin 期间不发生 carrier switch
  if (t->Yield()) c->ok = false;

  uint64_t sw0 = c->sched->switch_count();
  uint64_t rel0 = c->sched->release_count();
  if (c->sched->ReadyCount() != 1) c->ok = false;  // peer 在队但不得被调度

  // 14.7 PIN：quantum 边界命中 pin -> 不释放 carrier、不切换，立即换回
  t->EndQuantumWhilePinned();

  if (c->sched->switch_count() != sw0) c->ok = false;    // 未切换
  if (c->sched->release_count() != rel0) c->ok = false;  // 未释放
  if (c->peer_progress != 0) c->ok = false;              // peer 未运行
  if (carrier->state != CarrierState::kPinnedToTask ||
      carrier->current_task != t || carrier->native_pin_count != 1) {
    c->ok = false;
  }
  c->log->push_back("T.pinned");

  t->LeaveNative(NativeExit::kContinue);
  // 14.9 路径 1：继续在当前 carrier 上执行
  if (t->execution_state() != ExecutionState::kRunningOnCarrier ||
      t->current_carrier() != carrier || carrier->native_pin_count != 0 ||
      carrier->state != CarrierState::kRunningTask) {
    c->ok = false;
  }
  c->log->push_back("T.leave");
}

void TestPinNoSwitchNoRelease() {
  g_test = "pin_no_switch_no_release";
  Scheduler sched;
  std::vector<std::string> log;
  PinCtx ctx;
  ctx.sched = &sched;
  ctx.log = &log;
  sched.Spawn(PinMainEntry, &ctx, Priority::kP2);
  ctx.peer = sched.Spawn(PinPeerEntry, &ctx, Priority::kP2);
  bool ok = sched.Run(10s);
  CHECK(ok, "run finished");
  CHECK(ctx.ok, "pin 期间 carrier 绑定稳定（不切换/不释放/不调度 peer）");
  ExpectLog(log,
            std::vector<std::string>{"T.enter", "T.pinned", "T.leave", "peer"},
            "pin 全程无 peer 插入，LeaveNative(kContinue) 后 task 继续");
  CHECK(ctx.peer_progress == 1, "peer 仅在 pin 结束后运行一次");
}

// ---- 6) LeaveNative 两条路径 ----

struct LnCtx {
  Scheduler* sched;
  GuestTask* t2 = nullptr;
  bool ok = true;
  std::vector<std::string>* log;
};

void LnT1Entry(GuestTask* t, void* arg) {
  auto* c = static_cast<LnCtx*>(arg);
  t->EnterNative();
  t->LeaveNative(NativeExit::kContinue);
  if (t->execution_state() != ExecutionState::kRunningOnCarrier) c->ok = false;
  c->log->push_back("T1.cont");
}

void LnT2Entry(GuestTask* t, void* arg) {
  auto* c = static_cast<LnCtx*>(arg);
  c->log->push_back("T2.pre");
  t->EnterNative();
  t->LeaveNative(NativeExit::kYield);  // 路径 2a：让出
  // 恢复后继续
  if (t->execution_state() != ExecutionState::kRunningOnCarrier) c->ok = false;
  c->log->push_back("T2.post");
}

void LnT3Entry(GuestTask* t, void* arg) {
  auto* c = static_cast<LnCtx*>(arg);
  c->log->push_back("T3.pre");
  // T2 已走路径 2：NOT_RUNNING 且离开 carrier
  if (c->t2->execution_state() != ExecutionState::kNotRunning) c->ok = false;
  if (c->t2->current_carrier() != nullptr) c->ok = false;
  t->EnterNative();
  t->LeaveNative(NativeExit::kFinish);  // 路径 2b：结束
  c->log->push_back("T3.UNREACHABLE");
}

void TestLeaveNativeTwoPaths() {
  g_test = "leave_native_two_paths";
  Scheduler sched;
  std::vector<std::string> log;
  LnCtx ctx;
  ctx.sched = &sched;
  ctx.log = &log;
  GuestTask* t1 = sched.Spawn(LnT1Entry, &ctx, Priority::kP2);
  ctx.t2 = sched.Spawn(LnT2Entry, &ctx, Priority::kP2);
  GuestTask* t3 = sched.Spawn(LnT3Entry, &ctx, Priority::kP2);
  bool ok = sched.Run(10s);
  CHECK(ok, "run finished");
  CHECK(ctx.ok, "两条路径状态迁移正确");
  ExpectLog(log,
            std::vector<std::string>{"T1.cont", "T2.pre", "T3.pre", "T2.post"},
            "kContinue 同 quantum 继续；kYield 让出后恢复；kFinish 不再执行");
  CHECK(t1->lifecycle_state() == LifecycleState::kCompleted, "T1 completed");
  CHECK(ctx.t2->lifecycle_state() == LifecycleState::kCompleted, "T2 completed");
  CHECK(t3->lifecycle_state() == LifecycleState::kCompleted, "T3 completed");
}

// ---- 7) safepoint 暂停恢复 ----

struct SpCtx {
  Scheduler* sched;
  GuestTask* target = nullptr;
  int progress = 0;
  bool ok = true;
  std::vector<std::string>* log;
};

void SpTargetEntry(GuestTask* t, void* arg) {
  auto* c = static_cast<SpCtx*>(arg);
  c->log->push_back("T.pre");
  c->progress = 1;
  bool responded = t->PollSafepoint();  // 14.12：安全边界响应 checkpoint
  if (!responded) c->ok = false;
  c->log->push_back("T.post");
  c->progress = 2;
}

void SpDriverEntry(GuestTask* t, void* arg) {
  auto* c = static_cast<SpCtx*>(arg);
  c->sched->RequestCheckpoint(c->target);
  c->log->push_back("D.req");
  t->Yield();

  // target 已在安全边界停顿
  if (c->target->gc_state() != GcState::kSuspended) c->ok = false;
  if (c->progress != 1) c->ok = false;
  c->log->push_back("D.saw");

  c->sched->ResumeFromSafepoint(c->target);  // GC 完成 -> RUNNABLE
  c->log->push_back("D.resume");
  t->Yield();

  if (c->progress != 2) c->ok = false;
  if (c->target->lifecycle_state() != LifecycleState::kCompleted) {
    c->ok = false;
  }
  c->log->push_back("D.end");
}

void TestSafepointPauseResume() {
  g_test = "safepoint_pause_resume";
  Scheduler sched;
  std::vector<std::string> log;
  SpCtx ctx;
  ctx.sched = &sched;
  ctx.log = &log;
  sched.Spawn(SpDriverEntry, &ctx, Priority::kP2);
  ctx.target = sched.Spawn(SpTargetEntry, &ctx, Priority::kP2);
  bool ok = sched.Run(10s);
  CHECK(ok, "run finished");
  CHECK(ctx.ok, "请求 -> 停顿 -> 恢复 -> 推进");
  ExpectLog(log,
            std::vector<std::string>{"D.req", "T.pre", "D.saw", "D.resume",
                                     "T.post", "D.end"},
            "safepoint 暂停期间 target 不推进，恢复后继续");
}

// ---- 8) task 完成回收 ----

struct RecCtx {
  int id;
  std::vector<int>* log;
};

void RecEntry(GuestTask*, void* arg) {
  auto* c = static_cast<RecCtx*>(arg);
  c->log->push_back(c->id);
}

void TestTaskReclaim() {
  g_test = "task_completion_reclaim";
  Scheduler sched;
  std::vector<int> log;
  RecCtx a{1, &log}, b{2, &log}, c{3, &log}, d{4, &log};
  GuestTask* tasks[4] = {
      sched.Spawn(RecEntry, &a, Priority::kP2),
      sched.Spawn(RecEntry, &b, Priority::kP1),
      sched.Spawn(RecEntry, &c, Priority::kP3),
      sched.Spawn(RecEntry, &d, Priority::kP0),
  };
  CHECK(sched.LiveFiberStacks() == 4, "4 fiber stacks allocated");
  bool ok = sched.Run(10s);
  CHECK(ok, "run finished");
  ExpectLog(log, std::vector<int>{4, 2, 1, 3}, "完成顺序按优先级 P0>P1>P2>P3");
  CHECK(sched.completed_count() == 4, "completed_count == 4");
  CHECK(sched.LiveFiberStacks() == 0, "fiber stacks reclaimed");
  for (GuestTask* t : tasks) {
    if (t == nullptr || t->lifecycle_state() != LifecycleState::kCompleted ||
        t->fiber().stack_base != nullptr ||
        t->execution_state() != ExecutionState::kNotRunning) {
      CHECK(false, "task 完成回收（lifecycle/stack/execution）");
      return;
    }
  }
  CHECK(true, "task 完成回收（lifecycle/stack/execution）");
}

}  // namespace

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);  // 失败时也能看到进度
  std::printf("==== coroutine smoke (docs 04 ch14 M1) ====\n");
  TestAlternatingOrder();
  TestPriorityAndAging();
  TestWaitWakeBalance();
  TestDeadlineWake();
  TestPinNoSwitchNoRelease();
  TestLeaveNativeTwoPaths();
  TestSafepointPauseResume();
  TestTaskReclaim();

  if (g_failures == 0) {
    std::printf("==== ALL PASS ====\n");
    return 0;
  }
  std::printf("==== %d FAILURE(S) ====\n", g_failures);
  return 1;
}
