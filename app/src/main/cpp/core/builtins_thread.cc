// Thread/ThreadGroup/UEH/Object monitor wait/park 面 + Guest 线程运行时接缝。
// 纯移动自 core/builtins.cc（零行为变更；注册/绑定仍在 registrar）。

#include "core/builtins.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <set>
#include <string>
#include <time.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <arpa/inet.h>

#include "core/exception.h"

#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "session/class_loader.h"
#include "session/session.h"


#include "core/builtins_internal.h"
#include "core/tls_bridge.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace dexvm::runtime {

// ---- M4.4 Guest 线程运行时（docs 04 章 14.2/14.8）----
// Thread.start/join/interrupt、Object.wait/notify/notifyAll、Unsafe.park/unpark
// 的协程接线。线程模型口径：Thread.start() 创建 GuestTask（1:1 语义线程），
// 动态线程数、单执行流协程泵。scheduler 经 core/builtins.h GuestSchedOps 接缝
// 调入 coroutine/（不链 coroutine/ 的既有宿主脚本零改，退化旧单流）。
// 簿记仅在 guest 执行流上访问（M1 单 carrier；wake 与谓词变更同点完成，
// 防 lost wake 在单流下天然成立，docs 04 章 14.8）。
namespace {

const GuestSchedOps* g_guest_sched_ops = nullptr;

struct GuestThreadCtx {
  VMState* state = nullptr;
  VObject* thread = nullptr;   // Java Thread peer
  void* task = nullptr;        // guest task 句柄（GuestSchedOps::spawn）
  bool started = false;
  bool completed = false;
  bool daemon = false;
  bool interrupted = false;
  bool park_permit = false;    // LockSupport permit（sticky，至多 1）
  // getState 投影（Thread.State）：0=运行态 1=BLOCKED 2=WAITING 3=TIMED_WAITING
  // （各阻塞原语挂起前置位、返回后复位；单 carrier 下观察点一致）
  int wait_state = 0;
  bool wait_notified = false;  // Object.wait 被 notify 唤醒标记
  int64_t tid = 0;
  VObject* ueh = nullptr;
  std::vector<void*> joiners;  // Thread.join 等待者 task 句柄
};

// monitor 侧表（owner/等待队列；重入计数仍在 ObjHeader.monitor）。
// 对照 docs 04 章 14.8：monitor waiter list + 防 lost wake 协议。
struct MonitorRec {
  VClass* klass = nullptr;  // 地址复用陈旧校验（GC 防御）
  GuestThreadCtx* owner = nullptr;
  std::vector<GuestThreadCtx*> entry_waiters;  // 争用者（mutex waiter list）
  std::vector<GuestThreadCtx*> wait_waiters;   // Object.wait 池
};

bool g_in_guest_main = false;
bool g_main_done = false;
GuestThreadCtx* g_main_ctx = nullptr;
// 主线程 Thread peer 单例（tid=1）。Thread.theCurrentThread 静态槽只是"当前
// 执行流"镜像——worker 执行后槽会指向 worker，RunGuestMain 引导不得从槽取
// 身份（否则下一轮 main 误用 worker ctx，继承其 interrupt/生命周期状态）。
VObject* g_main_thread = nullptr;
std::unordered_map<void*, GuestThreadCtx*> g_ctx_by_task;
std::unordered_map<VObject*, GuestThreadCtx*> g_ctx_by_thread;
std::unordered_map<ObjHeader*, MonitorRec> g_monitors;
VObject* g_default_ueh = nullptr;
int64_t g_tid_counter = 1;  // 1 = 主线程
int g_thread_name_counter = 0;
// 设备考 guest 全程预算（默认 60s 救死锁；设备按需 SetGuestRunBudgetMs 放大——
// 预算中止归 GuestRunFailureReason，非信号失败不报 "unknown signal"）
long long g_guest_run_budget_ms = 60000;
std::string g_guest_run_error;
// kGuestStackBytes 见 core/builtins.h（main GuestTask 与 Thread.start 同口径）

GuestThreadCtx* CurrentCtx() {
  const GuestSchedOps* ops = g_guest_sched_ops;
  if (ops == nullptr) return nullptr;
  void* t = ops->current_task();
  if (t == nullptr) return nullptr;
  auto it = g_ctx_by_task.find(t);
  return it != g_ctx_by_task.end() ? it->second : nullptr;
}

GuestThreadCtx* CtxOfThread(VObject* thread) {
  if (thread == nullptr) return nullptr;
  auto it = g_ctx_by_thread.find(thread);
  return it != g_ctx_by_thread.end() ? it->second : nullptr;
}

GuestThreadCtx* EnsureCtx(VMState& state, VObject* thread) {
  GuestThreadCtx* ctx = CtxOfThread(thread);
  if (ctx != nullptr) return ctx;
  ctx = new GuestThreadCtx();  // M1：随进程存活（Thread peer 身份簿记）
  ctx->state = &state;
  ctx->thread = thread;
  ctx->tid = ++g_tid_counter;
  g_ctx_by_thread[thread] = ctx;
  return ctx;
}

// Thread.allThreads 静态数组（GC 根：挂起 task 的 Thread peer 保活；
// gc.cc MarkClass 扫静态引用字段）。
void AppendThreadRegistry(VMState& state, VClass* thread_cls, VObject* thread) {
  if (thread_cls == nullptr || thread_cls->static_storage.size() < 3) return;
  VArray* arr = reinterpret_cast<VArray*>(thread_cls->static_storage[2].ref);
  int32_t len = arr != nullptr ? arr->length : 0;
  VClass* arr_cls = state.linker->FindArrayClass(thread_cls);
  if (arr_cls == nullptr) return;
  VArray* grown = state.linker->AllocArray(arr_cls, len + 1);
  if (grown == nullptr) return;
  for (int32_t i = 0; i < len; i++) {
    ObjHeader* e = nullptr;
    std::memcpy(&e, arr->data() + i * sizeof(ObjHeader*), sizeof(e));
    std::memcpy(grown->data() + i * sizeof(ObjHeader*), &e, sizeof(e));
  }
  ObjHeader* t = reinterpret_cast<ObjHeader*>(thread);
  std::memcpy(grown->data() + len * sizeof(ObjHeader*), &t, sizeof(t));
  thread_cls->static_storage[2].ref = grown;
}

// 当前线程身份（theCurrentThread 单例机制扩展为 per-GuestTask 身份：
// task 上下文取各自 Thread peer，主流程 = main 单例；Looper 的
// g_thread_looper 身份键模型随之按新身份 key 化）。
// 主线程 peer 单例（确定性身份；不做 theCurrentThread 槽同步——framework 侧
// 线程身份判定用，避免在工作线程上调用时污染"当前执行流"镜像）。
VObject* MainThreadPeerImpl(VMState& state) {
  VClass* k = state.linker != nullptr ? state.linker->FindClass("Ljava/lang/Thread;") : nullptr;
  if (k == nullptr || k->static_storage.empty()) return nullptr;
  if (g_main_thread == nullptr) {
    VObject* t = state.linker->AllocObject(k);
    if (t == nullptr) return nullptr;
    g_main_thread = t;
    VField* nf = FindInstField(k, "name");
    if (nf != nullptr) {
      VString* s = state.linker->AllocString(u"main");
      ObjHeader** slot = reinterpret_cast<ObjHeader**>(t->field_data() + nf->offset);
      *slot = reinterpret_cast<ObjHeader*>(s);
    }
    VField* pf = FindInstField(k, "priority");
    if (pf != nullptr) {
      uint64_t slot = 5;  // 8 字节槽全宽写
      std::memcpy(t->field_data() + pf->offset, &slot, sizeof(slot));
    }
    VField* tf = FindInstField(k, "tid");
    if (tf != nullptr) {
      int64_t tid = 1;
      std::memcpy(t->field_data() + tf->offset, &tid, sizeof(tid));
    }
    GuestThreadCtx* ctx = EnsureCtx(state, t);
    ctx->tid = 1;  // 主线程恒为 1
    AppendThreadRegistry(state, k, t);
  }
  return g_main_thread;
}

// 主线程 peer + theCurrentThread 槽同步（当前执行流 = 主线程时的恢复面）。
VObject* EnsureMainThreadObj(VMState& state) {
  VObject* t = MainThreadPeerImpl(state);
  VClass* k = state.linker != nullptr ? state.linker->FindClass("Ljava/lang/Thread;") : nullptr;
  if (k != nullptr && !k->static_storage.empty() && t != nullptr) {
    k->static_storage[0].ref = t;
  }
  return t;
}

VObject* CurrentThreadObj(VMState& state) {
  VClass* k = state.linker != nullptr ? state.linker->FindClass("Ljava/lang/Thread;") : nullptr;
  if (k == nullptr || k->static_storage.empty()) return nullptr;
  GuestThreadCtx* me = CurrentCtx();
  if (me != nullptr && me->thread != nullptr) {
    k->static_storage[0].ref = me->thread;
    return me->thread;
  }
  return EnsureMainThreadObj(state);
}

// 阻塞原语准入：必须在 guest task 上下文（RunGuestMain 内）。返回 nullptr
// = 已按 InternalError 挂账。join 例外（未启动/已完成线程直返）。
GuestThreadCtx* RequireGuestTask(VMState& state, bool* ok) {
  GuestThreadCtx* me = CurrentCtx();
  if (g_guest_sched_ops == nullptr || me == nullptr || me->task == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;",
                   "blocking call requires guest thread runtime (RunGuestMain)");
    *ok = false;
    return nullptr;
  }
  return me;
}

// ---- monitor 侧表 ----

MonitorRec& GetMonitor(ObjHeader* obj) {
  MonitorRec& rec = g_monitors[obj];
  if (rec.klass != obj->klass) {
    // 地址复用/失同步（GC 陈旧防御）：owner/等待者不得跨对象继承
    bool stale = rec.klass != nullptr;
    rec = MonitorRec{};
    rec.klass = obj->klass;
    if (stale) {
      std::fprintf(stderr, "monitor: stale entry reset %p\n",
                   static_cast<void*>(obj));
    }
  }
  return rec;
}

// 释放全部持有（wait 路径）或最后一跳（exit 路径）后放行争用队首：
// 授予语义（owner=队首、count=1、入 ready），唤醒即已持有（防插队/lost grant）。
void GrantEntryWaiter(MonitorRec& rec, ObjHeader* obj) {
  if (rec.entry_waiters.empty()) return;
  GuestThreadCtx* w = rec.entry_waiters.front();
  rec.entry_waiters.erase(rec.entry_waiters.begin());
  rec.owner = w;
  obj->monitor = 1;
  if (w->task != nullptr && g_guest_sched_ops != nullptr) {
    g_guest_sched_ops->wake(w->task, kGuestWaitMutex);
  }
}

}  // namespace

// 公开面（framework 侧主线程身份判定；匿名实现体转发，external linkage）。
VObject* MainThreadPeer(VMState& state) { return MainThreadPeerImpl(state); }

namespace {

// ---- task 生命周期 ----

void ReportUncaught(VMState& state, GuestThreadCtx* ctx, ObjHeader* ex) {
  VObject* handler = ctx->ueh != nullptr ? ctx->ueh : g_default_ueh;
  if (handler != nullptr && handler->klass != nullptr) {
    VMethod probe;
    probe.name = "uncaughtException";
    probe.param_types = {"Ljava/lang/Thread;", "Ljava/lang/Throwable;"};
    probe.return_type = "V";
    VMethod* m = handler->klass->FindInterfaceImpl(&probe);
    if (m != nullptr) {
      Value a[3] = {};
      a[0].ref = reinterpret_cast<ObjHeader*>(handler);
      a[1].ref = reinterpret_cast<ObjHeader*>(ctx->thread);
      a[2].ref = ex;
      Value rv{};
      if (InvokeMethod(state, m, a, 3, &rv) && !state.HasPendingException()) return;
      state.ClearException();
    }
  }
  // 默认 UEH 最小面：stderr 摘要（Java 默认处理器语义投影）
  std::string desc = ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor
                                                           : "Ljava/lang/Throwable;";
  std::string msg;
  if (ex != nullptr && ex->klass != nullptr) {
    VObject* eo = reinterpret_cast<VObject*>(ex);
    for (VClass* kk = eo->klass; kk != nullptr && msg.empty(); kk = kk->super_class) {
      for (const auto& f : kk->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* s = nullptr;
          std::memcpy(&s, eo->field_data() + f.offset, sizeof(s));
          if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
          break;
        }
      }
    }
  }
  std::string name = "Thread";
  VField* nf = ctx->thread != nullptr ? FindInstField(ctx->thread->klass, "name") : nullptr;
  if (nf != nullptr) {
    ObjHeader* s = nullptr;
    std::memcpy(&s, ctx->thread->field_data() + nf->offset, sizeof(s));
    if (s != nullptr) name = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  std::fprintf(stderr, "Exception in thread \"%s\" %s: %s\n", name.c_str(), desc.c_str(),
               msg.c_str());
#if defined(__ANDROID__)
  // 设备可见性：Android app 进程 stderr 被丢弃——未捕获异常同落 logcat。
  __android_log_print(ANDROID_LOG_ERROR, "DexVmThread",
                      "Exception in thread \"%s\" %s: %s", name.c_str(), desc.c_str(),
                      msg.c_str());
  for (const std::string& frame : state.last_throw_backtrace) {
    __android_log_print(ANDROID_LOG_ERROR, "DexVmThread", "  at %s", frame.c_str());
  }
#endif
}

// Java 进程退出策略：主流程结束且无存活非 daemon 线程 -> 结束调度循环。
void MaybeRequestExit(GuestThreadCtx* self) {
  if (!g_main_done) return;
  for (auto& [task, c] : g_ctx_by_task) {
    (void)task;
    if (c == self) continue;
    if (c->started && !c->completed && !c->daemon) return;
  }
  if (g_guest_sched_ops != nullptr) g_guest_sched_ops->request_stop();
}

void CompleteThread(GuestThreadCtx* ctx) {
  ctx->completed = true;
  if (g_guest_sched_ops != nullptr) {
    for (void* j : ctx->joiners) g_guest_sched_ops->wake(j, kGuestWaitJoin);
    ctx->joiners.clear();
    MaybeRequestExit(ctx);
  }
}

// Thread.start 的 GuestTask 入口：跑该 Thread 实例的 run() 虚方法
//（子类覆盖优先，object_model FindInterfaceImpl；默认 run 跑 target）。
void RunThreadEntry(void* task_handle, void* arg) {
  GuestThreadCtx* ctx = static_cast<GuestThreadCtx*>(arg);
  VMState& state = *ctx->state;
  ctx->task = task_handle;
  g_ctx_by_task[task_handle] = ctx;
  VMethod probe;
  probe.name = "run";
  probe.param_types = {};
  probe.return_type = "V";
  VMethod* run_m =
      ctx->thread->klass != nullptr ? ctx->thread->klass->FindInterfaceImpl(&probe) : nullptr;
  Value selfv{};
  selfv.ref = reinterpret_cast<ObjHeader*>(ctx->thread);
  Value rv{};
  bool ok = run_m != nullptr && InvokeMethod(state, run_m, &selfv, 1, &rv);
  if (!ok && state.HasPendingException()) {
    ObjHeader* ex = state.pending_exception;
    state.ClearException();
    ReportUncaught(state, ctx, ex);
  }
#if defined(__ANDROID__)
  std::string tname = "Thread";
  if (ctx->thread != nullptr) {
    if (VField* nf = FindInstField(ctx->thread->klass, "name")) {
      ObjHeader* s = nullptr;
      std::memcpy(&s, ctx->thread->field_data() + nf->offset, sizeof(s));
      if (s != nullptr) tname = static_cast<VString*>(s)->ToModifiedUtf8();
    }
  }
  __android_log_print(ANDROID_LOG_INFO, "DexVmThread", "exit \"%s\" ok=%d", tname.c_str(),
                      ok ? 1 : 0);
#endif
  CompleteThread(ctx);
}

struct MainPayload {
  VMState* state;
  GuestMainFn fn;
  void* arg;
  bool ok;
  bool entered = false;
};

void RunMainEntry(void* task_handle, void* arg) {
  MainPayload* p = static_cast<MainPayload*>(arg);
  p->entered = true;
  g_main_ctx->task = task_handle;
  g_ctx_by_task[task_handle] = g_main_ctx;
  p->ok = p->fn(*p->state, p->arg);
  p->state->ClearException();
  g_main_done = true;
  CompleteThread(g_main_ctx);
}

// ---- 阻塞原语谓词 ----

struct JoinPredArg {
  GuestThreadCtx* target;
  GuestThreadCtx* me;
};
bool PredJoinDone(void* raw) {
  auto* a = static_cast<JoinPredArg*>(raw);
  return a->target->completed || a->me->interrupted;
}
long long NowWallMs() {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return static_cast<long long>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
}
bool PredInterrupted(void* raw) {
  return static_cast<GuestThreadCtx*>(raw)->interrupted;
}
bool PredNotifiedOrInterrupted(void* raw) {
  GuestThreadCtx* me = static_cast<GuestThreadCtx*>(raw);
  return me->wait_notified || me->interrupted;
}
bool PredParkedOrInterrupted(void* raw) {
  GuestThreadCtx* me = static_cast<GuestThreadCtx*>(raw);
  return me->park_permit || me->interrupted;
}

// 中断传播（sleep/wait/join 共用）：清标志 + InterruptedException。
bool ThrowIfInterrupted(VMState& state, GuestThreadCtx* me, bool* ok) {
  if (!me->interrupted) return false;
  me->interrupted = false;
  state.ThrowNew("Ljava/lang/InterruptedException;", "thread interrupted");
  *ok = false;
  return true;
}

// Object.wait 重入恢复（Java：先拿回 monitor 再抛/返回）
void RestoreMonitorAfterWait(VMState& state, ObjHeader* obj, GuestThreadCtx* me,
                             uint32_t saved) {
  MonitorEnterImpl(state, obj);
  if (state.HasPendingException()) return;
  if (GetMonitor(obj).owner == me) obj->monitor = saved;  // 恢复重入计数
}

}  // namespace

// ---- java.lang.Thread / ThreadGroup ----
Value ThreadInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  // name @0、priority @8、group @16、target @24、tid @32、daemon @40。
  // AOSP 8 个 <init> 形（()/(String)/(Runnable)/(Runnable,String)/(Group,String)/
  // (Group,Runnable)/(Group,Runnable,String)/(Group,Runnable,String,J)）统一按
  // 形参类型归位：String->name、ThreadGroup->group、其余引用->target；宽值槽
  // （J stackSize）按堆指针判别剔除（stackSize 位型恒非堆址）。
  VField* pf = FindInstField(self->klass, "priority");
  if (pf != nullptr) {
    // 8 字节槽全宽写（半槽写留脏高 32 位，见 EnumInit 注释）
    uint64_t slot = 5;
    std::memcpy(self->field_data() + pf->offset, &slot, sizeof(slot));
  }
  std::string name;
  ObjHeader* group = nullptr;
  ObjHeader* target = nullptr;
  for (size_t i = 0; i < n; i++) {
    ObjHeader* a = args[i].ref;
    if (a == nullptr || state.heap == nullptr || !state.heap->IsHeapStart(a)) continue;
    if (a->klass != nullptr && a->klass->descriptor == "Ljava/lang/String;") {
      name = static_cast<VString*>(a)->ToModifiedUtf8();
    } else if (a->klass != nullptr && a->klass->descriptor == "Ljava/lang/ThreadGroup;") {
      group = a;
    } else {
      target = a;
    }
  }
  if (name.empty()) name = "Thread-" + std::to_string(++g_thread_name_counter);
  VField* nf = FindInstField(self->klass, "name");
  if (nf != nullptr) {
    VString* s = state.linker->AllocString(std::u16string(name.begin(), name.end()));
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + nf->offset);
    *slot = reinterpret_cast<ObjHeader*>(s);
  }
  // group 参数（ThreadGroup）归位（getThreadGroup 语义面）
  if (group != nullptr) {
    VField* gf = FindInstField(self->klass, "group");
    if (gf != nullptr) {
      ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + gf->offset);
      *slot = group;
    }
  }
  // target 参数（Runnable）归位
  if (target != nullptr) {
    VField* tf = FindInstField(self->klass, "target");
    if (tf != nullptr) {
      ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + tf->offset);
      *slot = target;
    }
  }
  // M4.4：构造即建身份（tid + GuestThreadCtx 簿记 + allThreads 注册）
  GuestThreadCtx* ctx = EnsureCtx(state, self);
  VField* tf = FindInstField(self->klass, "tid");
  if (tf != nullptr) {
    int64_t tid = ctx->tid;
    std::memcpy(self->field_data() + tf->offset, &tid, sizeof(tid));
  }
  AppendThreadRegistry(state, state.linker->FindClass("Ljava/lang/Thread;"), self);
  return Value{};
}
Value ThreadCurrentThread(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  // M4.4：per-GuestTask 身份（task 上下文取各自 Thread peer；主流程 = main 单例）
  VObject* t = CurrentThreadObj(state);
  if (t == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Thread missing");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(t);
  return r;
}
// 线程上下文 ClassLoader（lib ServiceLoader.load(Class) 语义面；单线程 VM 静态槽）
Value ThreadGetContextClassLoader(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  VClass* k = state.linker->FindClass("Ljava/lang/Thread;");
  Value r{};
  if (k != nullptr && k->static_storage.size() > 1) r.ref = k->static_storage[1].ref;
  *ok = true;
  return r;
}
Value ThreadSetContextClassLoader(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)self;
  VClass* k = state.linker->FindClass("Ljava/lang/Thread;");
  if (k == nullptr || k->static_storage.size() <= 1) {
    state.ThrowNew("Ljava/lang/InternalError;", "Thread contextClassLoader slot missing");
    *ok = false;
    return Value{};
  }
  k->static_storage[1].ref = (n >= 1) ? args[0].ref : nullptr;
  *ok = true;
  return Value{};
}
Value ThreadSleep(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int64_t ms = (n > 0) ? args[0].l : 0;
  int32_t ns = (n > 1) ? args[1].i : 0;
  if (ms < 0 || ns < 0 || ns >= 1000000) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "sleep: invalid argument");
    *ok = false;
    return Value{};
  }
  // M4.3 泵点（builtins.h MessagePumpHook）：sleep 即虚拟时间推进泵出已投递
  // 消息（docs 14.10 单线程投影；postDelayed(0/小延迟) 必须能被执行到）。
  // 泵出的 dispatch 异常按"消息回调抛出"语义上抛。
  if (g_message_pump_hook != nullptr) {
    std::string pump_err;
    if (!g_message_pump_hook(state, &pump_err)) {
      state.ThrowNew("Ljava/lang/RuntimeException;", pump_err);
      *ok = false;
      return Value{};
    }
  }
  if (ms > 0 || ns > 0) {
    GuestThreadCtx* me = CurrentCtx();
    if (g_guest_sched_ops != nullptr && me != nullptr && me->task != nullptr) {
      // M4.4：guest task 上协程 deadline 等待（中断传播 InterruptedException）
      long long total_ms = ms + (ns + 999999) / 1000000;
      long long deadline_ms = g_guest_sched_ops->now_ms() + total_ms;
      me->wait_state = 3;  // TIMED_WAITING（getState 投影）
      g_guest_sched_ops->wait(kGuestWaitDeadline, deadline_ms, &PredInterrupted, me);
      me->wait_state = 0;
      if (ThrowIfInterrupted(state, me, ok)) return Value{};
      return Value{};
    }
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(ms / 1000);
    ts.tv_nsec = static_cast<long>((ms % 1000) * 1000000L + ns);
    nanosleep(&ts, nullptr);
  }
  return Value{};
}
Value ThreadGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  VField* nf = FindInstField(self->klass, "name");
  if (nf != nullptr) std::memcpy(&r.ref, self->field_data() + nf->offset, sizeof(r.ref));
  return r;
}
Value ThreadSetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  VField* nf = FindInstField(self->klass, "name");
  if (nf != nullptr && n > 0) {
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + nf->offset);
    *slot = args[0].ref;
  }
  return Value{};
}
Value ThreadGetId(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  VField* tf = FindInstField(self->klass, "tid");
  if (tf != nullptr) std::memcpy(&r.l, self->field_data() + tf->offset, sizeof(r.l));
  return r;
}
Value ThreadIsAlive(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  Value r{};
  // 当前执行流身份恒活；其余按 GuestTask 生命周期：started && 未 completed
  // && task 存续。theCurrentThread 静态槽不作判据（槽内可能残留已让出身份）。
  GuestThreadCtx* me = CurrentCtx();
  if (me != nullptr && me->thread == self && !me->completed) {
    r.i = 1;
    return r;
  }
  GuestThreadCtx* ctx = CtxOfThread(self);
  if (ctx != nullptr) {
    bool live = ctx->started && !ctx->completed && ctx->task != nullptr;
    if (!live && me == nullptr && ctx->tid == 1) live = true;  // 旧单流主线程恒活
    r.i = live ? 1 : 0;
    return r;
  }
  VClass* k = state.linker->FindClass("Ljava/lang/Thread;");
  r.i = (k != nullptr && !k->static_storage.empty() && k->static_storage[0].ref == self) ? 1 : 0;
  return r;
}
// Thread.getState()：GuestTask 生命周期 -> Thread.State 常量（Android 语义）。
// NEW=0 RUNNABLE=1 BLOCKED=2 WAITING=3 TIMED_WAITING=4 TERMINATED=5
// （常量实例由 builtins.cc 注册 Thread$State 时静态槽预置）。
Value ThreadGetState(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  (void)ok;
  int idx = 1;  // RUNNABLE
  GuestThreadCtx* ctx = CtxOfThread(self);
  if (ctx == nullptr || !ctx->started) {
    idx = 0;  // NEW
  } else if (ctx->completed) {
    idx = 5;  // TERMINATED
  } else if (ctx->wait_state == 1) {
    idx = 2;  // BLOCKED
  } else if (ctx->wait_state == 2) {
    idx = 3;  // WAITING
  } else if (ctx->wait_state == 3) {
    idx = 4;  // TIMED_WAITING
  }
  Value r{};
  VClass* tk =
      state.linker != nullptr ? state.linker->FindClass("Ljava/lang/Thread$State;") : nullptr;
  if (tk != nullptr && static_cast<size_t>(idx) < tk->static_storage.size()) {
    r.ref = tk->static_storage[idx].ref;
  }
  return r;
}
Value ThreadHoldsLock(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)n; (void)ok;
  Value r{};
  ObjHeader* o = (n > 0) ? args[0].ref : nullptr;
  if (o == nullptr) {
    ThrowNullPointerException(state, "holdsLock: null");
    *ok = false;
    return Value{};
  }
  r.i = o->monitor > 0 ? 1 : 0;
  (void)state;
  return r;
}
Value ThreadIsDaemon(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  VField* df = FindInstField(self->klass, "daemon");
  int32_t v = 0;
  if (df != nullptr) std::memcpy(&v, self->field_data() + df->offset, sizeof(v));
  r.i = v;
  return r;
}
Value ThreadSetDaemon(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  VField* df = FindInstField(self->klass, "daemon");
  if (df != nullptr && n > 0) {
    // 8 字节槽全宽写（kWord 零扩展）：daemon=false 写 0 时半槽写会留脏高 32 位
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(args[0].i));
    std::memcpy(self->field_data() + df->offset, &slot, sizeof(slot));
  }
  return Value{};
}
Value ThreadGetPriority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = 5;
  VField* pf = FindInstField(self->klass, "priority");
  if (pf != nullptr) std::memcpy(&r.i, self->field_data() + pf->offset, sizeof(r.i));
  return r;
}
Value ThreadSetPriority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  if (n > 0 && (args[0].i < 1 || args[0].i > 10)) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "priority out of range");
    *ok = false;
    return Value{};
  }
  VField* pf = FindInstField(self->klass, "priority");
  if (pf != nullptr && n > 0) {
    // 8 字节槽全宽写（kWord 零扩展）
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(args[0].i));
    std::memcpy(self->field_data() + pf->offset, &slot, sizeof(slot));
  }
  return Value{};
}
Value ThreadGroupInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  // name @0、parent @16?
  VField* nf = FindInstField(self->klass, "name");
  std::string name = "main";
  if (n > 0 && args[0].ref != nullptr && args[0].ref->klass != nullptr &&
      args[0].ref->klass->descriptor == "Ljava/lang/String;") {
    name = static_cast<VString*>(args[0].ref)->ToModifiedUtf8();
  } else if (n > 1 && args[1].ref != nullptr && args[1].ref->klass != nullptr &&
             args[1].ref->klass->descriptor == "Ljava/lang/String;") {
    name = static_cast<VString*>(args[1].ref)->ToModifiedUtf8();
  }
  if (nf != nullptr) {
    VString* s = state.linker->AllocString(std::u16string(name.begin(), name.end()));
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + nf->offset);
    *slot = reinterpret_cast<ObjHeader*>(s);
  }
  if (n > 0 && args[0].ref != nullptr && args[0].ref->klass != nullptr &&
      args[0].ref->klass->descriptor == "Ljava/lang/ThreadGroup;") {
    VField* pf = FindInstField(self->klass, "parent");
    if (pf != nullptr) {
      ObjHeader** slot = reinterpret_cast<ObjHeader**>(self->field_data() + pf->offset);
      *slot = args[0].ref;
    }
  }
  return Value{};
}
Value ThreadGroupGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  VField* nf = FindInstField(self->klass, "name");
  if (nf != nullptr) std::memcpy(&r.ref, self->field_data() + nf->offset, sizeof(r.ref));
  return r;
}
// ThreadGroup.getParent/parentOf/activeCount（Android java.lang 语义；对照
// vendored java.lang.ThreadGroup.java 同名面——内建遮蔽下由本缝承接）。
namespace {
// group 是否位于 ancestor 子树（含自身；沿 parent 链走）。
bool GroupInSubtree(VObject* group, VObject* ancestor) {
  for (VObject* g = group; g != nullptr;) {
    if (g == ancestor) return true;
    VField* pf = FindInstField(g->klass, "parent");
    ObjHeader* p = nullptr;
    if (pf != nullptr) std::memcpy(&p, g->field_data() + pf->offset, sizeof(p));
    g = static_cast<VObject*>(p);
  }
  return false;
}
}  // namespace
Value ThreadGroupGetParent(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  VField* pf = FindInstField(self->klass, "parent");
  if (pf != nullptr) std::memcpy(&r.ref, self->field_data() + pf->offset, sizeof(r.ref));
  return r;
}
Value ThreadGroupParentOf(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)n; (void)ok;
  Value r{};
  // OpenJDK：沿 g 的 parent 链找 this；g == null -> false。
  VObject* g = (n > 0) ? static_cast<VObject*>(args[0].ref) : nullptr;
  r.i = (g != nullptr && GroupInSubtree(g, self)) ? 1 : 0;
  return r;
}
Value ThreadGroupActiveCount(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  // OpenJDK：本组 + 递归子组的活动线程估计数。VM 口径：Thread.allThreads
  // 注册表里存活且组归属落在 self 子树（含自身）的线程计数。
  Value r{};
  VClass* thread_cls =
      state.linker != nullptr ? state.linker->FindClass("Ljava/lang/Thread;") : nullptr;
  int32_t count = 0;
  if (thread_cls != nullptr && thread_cls->static_storage.size() >= 3) {
    VArray* arr = reinterpret_cast<VArray*>(thread_cls->static_storage[2].ref);
    if (arr != nullptr) {
      for (int32_t i = 0; i < arr->length; i++) {
        ObjHeader* e = nullptr;
        std::memcpy(&e, arr->data() + i * sizeof(ObjHeader*), sizeof(e));
        VObject* t = static_cast<VObject*>(e);
        if (t == nullptr) continue;
        VField* gf = FindInstField(t->klass, "group");
        ObjHeader* g = nullptr;
        if (gf != nullptr) std::memcpy(&g, t->field_data() + gf->offset, sizeof(g));
        if (!GroupInSubtree(static_cast<VObject*>(g), self)) continue;
        bool lok = true;
        Value alive = ThreadIsAlive(state, t, nullptr, 0, &lok);
        if (lok && alive.i != 0) count++;
      }
    }
  }
  r.i = count;
  return r;
}
// Thread.group 取值 + 惰性默认组（Java：Thread 构造期归入
// currentThread().getThreadGroup()；VM 默认 main 组）。getGroup/
// getUncaughtExceptionHandler 同源。
namespace {
VObject* EnsureThreadGroup(VMState& state, VObject* thread) {
  VField* gf = FindInstField(thread->klass, "group");
  ObjHeader* g = nullptr;
  if (gf != nullptr) std::memcpy(&g, thread->field_data() + gf->offset, sizeof(g));
  if (g != nullptr) return static_cast<VObject*>(g);
  VClass* tg = state.linker->FindClass("Ljava/lang/ThreadGroup;");
  if (tg == nullptr) {
    state.ClearException();
    return nullptr;
  }
  VObject* obj = state.linker->AllocObject(tg);
  if (obj == nullptr) {
    state.ClearException();
    return nullptr;
  }
  VField* nf = FindInstField(tg, "name");
  if (nf != nullptr) {
    VString* s = state.linker->AllocString(u"main");
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(obj->field_data() + nf->offset);
    *slot = reinterpret_cast<ObjHeader*>(s);
  }
  if (gf != nullptr) {
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(thread->field_data() + gf->offset);
    *slot = obj;
  }
  return obj;
}

}  // namespace
Value ThreadGetGroup(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  Value r{};
  r.ref = EnsureThreadGroup(state, self);
  return r;
}

// ---- M4.4 Thread 线程语义面（GuestTask 接线；docs 04 章 14.2）----

// start()：创建 GuestTask（跑该 Thread 实例的 run() 虚方法）；重复 start 抛
// IllegalThreadStateException；启动后 isAlive 语义（task 存续且未完成）。
Value ThreadStart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  if (g_guest_sched_ops == nullptr || !g_in_guest_main) {
    state.ThrowNew("Ljava/lang/InternalError;",
                   "Thread.start: guest thread runtime not active (RunGuestMain)");
    *ok = false;
    return Value{};
  }
  GuestThreadCtx* ctx = EnsureCtx(state, self);
  if (ctx->started) {
    state.ThrowNew("Ljava/lang/IllegalThreadStateException;", "thread already started");
    *ok = false;
    return Value{};
  }
  VField* df = FindInstField(self->klass, "daemon");
  int32_t daemon = 0;
  if (df != nullptr) std::memcpy(&daemon, self->field_data() + df->offset, sizeof(daemon));
  ctx->daemon = daemon != 0;
  ctx->state = &state;
  ctx->started = true;
  void* handle = g_guest_sched_ops->spawn(&RunThreadEntry, ctx, kGuestStackBytes);
  if (handle == nullptr) {
    ctx->started = false;
    state.ThrowNew("Ljava/lang/InternalError;", "Thread.start: GuestTask spawn failed");
    *ok = false;
    return Value{};
  }
  ctx->task = handle;
  g_ctx_by_task[handle] = ctx;
#if defined(__ANDROID__)
  // 线程生命周期诊断（低量）：确认 app 的线程池/网络调度是否真的启动。
  std::string tname = "Thread";
  if (VField* nf = FindInstField(self->klass, "name")) {
    ObjHeader* s = nullptr;
    std::memcpy(&s, self->field_data() + nf->offset, sizeof(s));
    if (s != nullptr) tname = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  ObjHeader* tgt = nullptr;
  if (VField* tf = FindInstField(self->klass, "target")) {
    std::memcpy(&tgt, self->field_data() + tf->offset, sizeof(tgt));
  }
  __android_log_print(ANDROID_LOG_INFO, "DexVmThread", "start \"%s\" target=%s",
                      tname.c_str(),
                      tgt != nullptr && tgt->klass != nullptr ? tgt->klass->descriptor.c_str()
                                                              : "-");
#endif
  return Value{};
}
Value ThreadRun(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VField* tf = FindInstField(self->klass, "target");
  ObjHeader* target = nullptr;
  if (tf != nullptr) std::memcpy(&target, self->field_data() + tf->offset, sizeof(target));
  if (target == nullptr) return Value{};
  VMethod probe;
  probe.name = "run";
  probe.param_types = {};
  probe.return_type = "V";
  VMethod* m = target->klass != nullptr ? target->klass->FindInterfaceImpl(&probe) : nullptr;
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", "Runnable.run");
    *ok = false;
    return Value{};
  }
  Value recv{};
  recv.ref = target;
  Value rv{};
  if (!InvokeMethod(state, m, &recv, 1, &rv)) {
    *ok = false;
    return Value{};
  }
  return Value{};
}

// join()/join(long)/join(long,int)：协程 wait 到 task 完成（deadline 语义；
// deadline 到期仍存活则静默返回）。未启动/已完成线程直返（Java 语义）。
Value ThreadJoin(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int64_t millis = (n > 0) ? args[0].l : 0;
  int32_t nanos = (n > 1) ? args[1].i : 0;
  if (millis < 0 || nanos < 0 || nanos > 999999) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "join: invalid timeout");
    *ok = false;
    return Value{};
  }
  if (nanos > 0 && millis < INT64_MAX) millis++;  // OpenJDK 8 口径
  GuestThreadCtx* target = CtxOfThread(self);
  if (target == nullptr || !target->started || target->completed || target->task == nullptr) {
    return Value{};  // 未启动/已完成：立即返回
  }
  GuestThreadCtx* me = RequireGuestTask(state, ok);
  if (me == nullptr) return Value{};
  target->joiners.push_back(me->task);
  long long deadline_ms =
      millis == 0 ? -1 : g_guest_sched_ops->now_ms() + static_cast<long long>(millis);
  JoinPredArg pa{target, me};
  me->wait_state = millis == 0 ? 2 : 3;  // WAITING / TIMED_WAITING（getState 投影）
  g_guest_sched_ops->wait(kGuestWaitJoin, deadline_ms, &PredJoinDone, &pa);
  me->wait_state = 0;
  // 摘除 joiners 残留
  auto& js = target->joiners;
  for (auto it = js.begin(); it != js.end(); ++it) {
    if (*it == me->task) {
      js.erase(it);
      break;
    }
  }
  if (ThrowIfInterrupted(state, me, ok)) return Value{};
  return Value{};
}

// interrupt()：中断标志 + 唤醒 wait/sleep/park（InterruptedException 语义由
// 等待方传播）。
Value ThreadInterrupt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  GuestThreadCtx* ctx = CtxOfThread(self);
  if (ctx == nullptr) return Value{};  // 未构造身份：无标志可置
  ctx->interrupted = true;
  if (ctx->task != nullptr && g_guest_sched_ops != nullptr) {
    g_guest_sched_ops->wake(ctx->task, 0);  // 任意等待唤醒（谓词复查分派）
  }
  return Value{};
}
Value ThreadIsInterrupted(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  GuestThreadCtx* ctx = CtxOfThread(self);
  r.i = (ctx != nullptr && ctx->interrupted) ? 1 : 0;
  return r;
}
Value ThreadInterrupted(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  GuestThreadCtx* ctx = CurrentCtx();
  if (ctx != nullptr) {
    r.i = ctx->interrupted ? 1 : 0;
    ctx->interrupted = false;  // static interrupted() 清标志
  }
  return r;
}

// Thread.yield()：让出协程执行权（多 GuestTask 并发推进）。
Value ThreadYield(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  if (g_guest_sched_ops != nullptr) g_guest_sched_ops->yield_now();
  return Value{};
}

// Thread.onSpinWait()（Java 9+ 自旋等待提示；Phaser/SynchronousQueue/StampedLock
// 自旋面调用点）。A 档 Q2 口径：GuestTask 调度下无内存序/自旋提示语义，按
// yield 形落点（协程让出保证自旋配对任务可推进，语义超集 no-op）。
Value ThreadOnSpinWait(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ThreadYield(state, self, args, n, ok);
}

// ---- UEH 最小骨架（get/set/Default；uncaughtException 派发见 ReportUncaught）----
Value ThreadGetUEH(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  Value r{};
  GuestThreadCtx* ctx = CtxOfThread(self);
  if (ctx != nullptr && ctx->ueh != nullptr) {
    r.ref = reinterpret_cast<ObjHeader*>(ctx->ueh);
    return r;
  }
  // Java：无显式 UEH 回落 thread group（组本身即 handler，uncaughtException
  // 沿父组递归到默认 UEH/stderr——RxJavaPlugins.uncaught 链依赖非空 handler）。
  r.ref = EnsureThreadGroup(state, self);
  if (r.ref != nullptr) return r;
  r.ref = reinterpret_cast<ObjHeader*>(g_default_ueh);
  return r;
}
Value ThreadSetUEH(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)ok;
  GuestThreadCtx* ctx = EnsureCtx(state, self);
  ctx->ueh = (n > 0) ? reinterpret_cast<VObject*>(args[0].ref) : nullptr;
  // 侧表簿记持有（GuestThreadCtx 为 C++ 堆对象，GC 不可见）：钉住根保活
  if (ctx->ueh != nullptr && state.heap != nullptr) state.heap->Pin(ctx->ueh);
  return Value{};
}
Value ThreadGetDefaultUEH(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(g_default_ueh);
  return r;
}
Value ThreadSetDefaultUEH(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)ok;
  g_default_ueh = (n > 0) ? reinterpret_cast<VObject*>(args[0].ref) : nullptr;
  // 侧表簿记持有（C++ 全局；静态根扫描兜底 + 钉住根显式保活）
  if (g_default_ueh != nullptr && state.heap != nullptr) state.heap->Pin(g_default_ueh);
  return Value{};
}

// ---- M4.4 Object monitor wait 面（docs 04 章 14.8：monitor waiter list）----
// wait 释放重入计数并挂协程 wait；notify/notifyAll 唤醒 waiter list；
// IllegalMonitorStateException / wait(long) deadline / 中断传播 InterruptedException。

// wait()/wait(long)/wait(long,int) 统一落点。
Value ObjWait(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int64_t millis = (n > 0) ? args[0].l : 0;
  int32_t nanos = (n > 1) ? args[1].i : 0;
  if (millis < 0 || nanos < 0 || nanos > 999999) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "wait: invalid timeout");
    *ok = false;
    return Value{};
  }
  if (nanos > 0 && millis < INT64_MAX) millis++;  // OpenJDK 8 口径
  GuestThreadCtx* me = RequireGuestTask(state, ok);
  if (me == nullptr) return Value{};
  MonitorRec& rec = GetMonitor(self);
  if (rec.owner != me) {
    state.ThrowNew("Ljava/lang/IllegalMonitorStateException;", "Object.wait: not owner");
    *ok = false;
    return Value{};
  }
  uint32_t saved = self->monitor;
  // 完全释放（含重入计数）并放行争用队首（授予语义）
  self->monitor = 0;
  rec.owner = nullptr;
  GrantEntryWaiter(rec, self);
  me->wait_notified = false;
  rec.wait_waiters.push_back(me);
  long long deadline_ms =
      millis == 0 ? -1 : g_guest_sched_ops->now_ms() + static_cast<long long>(millis);
  me->wait_state = millis == 0 ? 2 : 3;  // WAITING / TIMED_WAITING（getState 投影）
  g_guest_sched_ops->wait(kGuestWaitMonitor, deadline_ms, &PredNotifiedOrInterrupted, me);
  me->wait_state = 0;
  // 摘除（notify 路径可能已摘）
  auto& wq = rec.wait_waiters;
  for (auto it = wq.begin(); it != wq.end(); ++it) {
    if (*it == me) {
      wq.erase(it);
      break;
    }
  }
  // Java：先重入拿回 monitor（不可中断），再按中断传播
  RestoreMonitorAfterWait(state, self, me, saved);
  if (state.HasPendingException()) {
    *ok = false;
    return Value{};
  }
  if (ThrowIfInterrupted(state, me, ok)) return Value{};
  return Value{};
}

Value ObjNotify(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  GuestThreadCtx* me = CurrentCtx();
  MonitorRec& rec = GetMonitor(self);
  if (rec.owner != me || (me == nullptr && self->monitor == 0)) {
    state.ThrowNew("Ljava/lang/IllegalMonitorStateException;", "Object.notify: not owner");
    *ok = false;
    return Value{};
  }
  if (!rec.wait_waiters.empty()) {
    GuestThreadCtx* w = rec.wait_waiters.front();
    rec.wait_waiters.erase(rec.wait_waiters.begin());
    w->wait_notified = true;
    if (w->task != nullptr && g_guest_sched_ops != nullptr) {
      g_guest_sched_ops->wake(w->task, kGuestWaitMonitor);
    }
  }
  return Value{};
}

Value ObjNotifyAll(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  GuestThreadCtx* me = CurrentCtx();
  MonitorRec& rec = GetMonitor(self);
  if (rec.owner != me || (me == nullptr && self->monitor == 0)) {
    state.ThrowNew("Ljava/lang/IllegalMonitorStateException;", "Object.notifyAll: not owner");
    *ok = false;
    return Value{};
  }
  while (!rec.wait_waiters.empty()) {
    GuestThreadCtx* w = rec.wait_waiters.front();
    rec.wait_waiters.erase(rec.wait_waiters.begin());
    w->wait_notified = true;
    if (w->task != nullptr && g_guest_sched_ops != nullptr) {
      g_guest_sched_ops->wake(w->task, kGuestWaitMonitor);
    }
  }
  return Value{};
}

// ---- M4.4 Unsafe park/unpark（A 档 Unsafe native 面；协程 wait/wake 承接）----
// park 非打断语义（LockSupport）：interrupt 只唤醒不抛，标志保留。
Value UnsafePark(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)ok;
  bool is_absolute = (n > 0) && args[0].i != 0;
  int64_t time = (n > 1) ? args[1].l : 0;
  GuestThreadCtx* me = CurrentCtx();
  if (g_guest_sched_ops == nullptr || me == nullptr || me->task == nullptr) return Value{};
  if (me->interrupted) return Value{};  // park 不抛 IE；标志保留
  if (me->park_permit) {
    me->park_permit = false;
    return Value{};
  }
  long long deadline_ms = -1;
  if (time != 0) {
    if (is_absolute) {
      // 绝对时刻（ms since epoch）-> 单调 deadline（近似换算）
      deadline_ms = g_guest_sched_ops->now_ms() + (time - NowWallMs());
    } else {
      deadline_ms = g_guest_sched_ops->now_ms() + (time + 999999) / 1000000;
    }
    if (deadline_ms < 0) deadline_ms = 0;
  }
  me->wait_state = deadline_ms < 0 ? 2 : 3;  // WAITING / TIMED_WAITING（getState 投影）
  g_guest_sched_ops->wait(kGuestWaitCondition, deadline_ms, &PredParkedOrInterrupted, me);
  me->wait_state = 0;
  if (me->park_permit) me->park_permit = false;  // 消费 permit；中断唤醒不消费
  return Value{};
}

Value UnsafeUnpark(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)self; (void)ok;
  VObject* thread = (n > 0) ? reinterpret_cast<VObject*>(args[0].ref) : nullptr;
  GuestThreadCtx* ctx = CtxOfThread(thread);
  if (ctx == nullptr) return Value{};
  ctx->park_permit = true;  // sticky permit（至多 1）
  if (ctx->task != nullptr && g_guest_sched_ops != nullptr) {
    g_guest_sched_ops->wake(ctx->task, 0);
  }
  return Value{};
}
// 非当前线程无 live 栈面可取（执行=单线程+协程）→ 空数组，不伪造。
Value ThreadGetStackTrace(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)args;
  (void)n;
  VClass* arr_k = state.linker->FindClass("[Ljava/lang/StackTraceElement;");
  VClass* ste_cls = state.linker->FindClass("Ljava/lang/StackTraceElement;");
  if (arr_k == nullptr || ste_cls == nullptr) {
    *ok = false;
    return Value{};
  }
  VObject* cur = CurrentThreadObj(state);
  size_t count = 0;
  if (self == nullptr || self == cur) {
    for (const Frame* f : state.frame_stack) {
      if (f != nullptr && f->method != nullptr) count++;
      if (count >= 32) break;
    }
  }
  VArray* arr = state.linker->AllocArray(arr_k, static_cast<int32_t>(count));
  if (arr == nullptr) {
    ThrowOOM(state, "Thread.getStackTrace");
    *ok = false;
    return Value{};
  }
  int32_t i = 0;
  if (count > 0) {
    for (auto it = state.frame_stack.rbegin(); it != state.frame_stack.rend(); ++it) {
      if (i >= static_cast<int32_t>(count)) break;
      const Frame* f = *it;
      if (f == nullptr || f->method == nullptr) continue;
      const VMethod* m = f->method;
      std::string cls = m->declaring != nullptr ? ClassName(m->declaring) : "?";
      bool sok = true;
      VObject* ste = MakeStackTraceElement(state, ste_cls, cls, m->name, &sok);
      if (ste == nullptr) {
        state.ClearException();
        break;
      }
      ObjHeader* slot = ste;
      std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &slot, sizeof(slot));
      i++;
    }
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

// Thread.getAllStackTraces：注册表 allThreads → Map<Thread, StackTraceElement[]>。
// VM 单执行流+协程，非当前线程无 live 栈面 → 空数组（与 getStackTrace 同口径，
// 不伪造）。ANR/监控日志链只消费 map 形状。
Value ThreadGetAllStackTraces(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  ClassLinker* linker = state.linker;
  VClass* thr_cls = linker != nullptr ? linker->FindClass("Ljava/lang/Thread;") : nullptr;
  VClass* map_cls = linker != nullptr ? linker->FindClass("Ljava/util/HashMap;") : nullptr;
  if (thr_cls == nullptr || map_cls == nullptr || thr_cls->static_storage.size() < 3) {
    ThrowOOM(state, "Thread.getAllStackTraces");
    *ok = false;
    return r;
  }
  if (!linker->InitializeClass(map_cls, &state)) {
    state.ClearException();
  }
  VObject* map = linker->AllocObject(map_cls);
  if (map == nullptr) {
    ThrowOOM(state, "Thread.getAllStackTraces map");
    *ok = false;
    return r;
  }
  Value tmp{};
  VMethod* ctor = LookupDeclaredMethod(map_cls, "<init>", {}, false);
  if (ctor != nullptr) {
    Value a[1] = {};
    a[0].ref = map;
    if (!InvokeMethod(state, ctor, a, 1, &tmp)) state.ClearException();
  }
  VMethod* put = LookupDeclaredMethod(
      map_cls, "put", {"Ljava/lang/Object;", "Ljava/lang/Object;"}, false);
  VArray* threads = reinterpret_cast<VArray*>(thr_cls->static_storage[2].ref);
  if (threads != nullptr && put != nullptr) {
    for (int32_t i = 0; i < threads->length; i++) {
      ObjHeader* ref = nullptr;
      std::memcpy(&ref, threads->data() + static_cast<size_t>(i) * 8, sizeof(ref));
      if (ref == nullptr) continue;
      bool sok = true;
      Value trace = ThreadGetStackTrace(state, reinterpret_cast<VObject*>(ref), nullptr, 0, &sok);
      if (!sok) {
        state.ClearException();
        continue;
      }
      Value a[3] = {};
      a[0].ref = map;
      a[1].ref = ref;
      a[2].ref = trace.ref;
      if (!InvokeMethod(state, put, a, 3, &tmp)) {
        state.ClearException();
        break;
      }
    }
  }
  r.ref = map;
  return r;
}

namespace {
bool ThreadGroupDispatchUEH(VMState& state, VObject* handler, VObject* thread, VObject* ex,
                            bool* ok) {
  if (handler == nullptr || handler->klass == nullptr) return true;
  VMethod* m = LookupDeclaredMethod(handler->klass, "uncaughtException",
                                    {"Ljava/lang/Thread;", "Ljava/lang/Throwable;"}, true);
  if (m == nullptr) return true;
  Value a[3];
  a[0].ref = handler;
  a[1].ref = thread;
  a[2].ref = ex;
  Value r{};
  return InvokeMethod(state, m, a, 3, &r);
}

}  // namespace
Value ThreadGroupUncaughtException(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  VObject* thread = (n > 0) ? static_cast<VObject*>(args[0].ref) : nullptr;
  VObject* ex = (n > 1) ? static_cast<VObject*>(args[1].ref) : nullptr;
  VField* pf = FindInstField(self->klass, "parent");
  VObject* parent = nullptr;
  if (pf != nullptr) std::memcpy(&parent, self->field_data() + pf->offset, sizeof(parent));
  if (parent != nullptr) {
    ThreadGroupDispatchUEH(state, parent, thread, ex, ok);
    return Value{};
  }
  if (g_default_ueh != nullptr) {
    ThreadGroupDispatchUEH(state, g_default_ueh, thread, ex, ok);
    return Value{};
  }
  // 默认处理器语义（OpenJDK ThreadGroup）：非 ThreadDeath 打印 stderr
  bool is_thread_death = false;
  for (VClass* k = ex != nullptr ? ex->klass : nullptr; k != nullptr; k = k->super_class) {
    if (k->descriptor == "Ljava/lang/ThreadDeath;") {
      is_thread_death = true;
      break;
    }
  }
  if (!is_thread_death && ex != nullptr) {
    VObject* err = SystemErrStream(state);
    if (err != nullptr) {
      VMethod* print = LookupDeclaredMethod(err->klass, "print", {"Ljava/lang/String;"}, true);
      std::string head = "Exception in thread \"";
      if (thread != nullptr) {
        ObjHeader* nm = GetRefSlot(thread, thread->klass, "name");
        head += nm != nullptr ? AsString(nm)->ToModifiedUtf8() : "?";
      } else {
        head += "?";
      }
      head += "\" ";
      if (print != nullptr) {
        Value a[2];
        a[0].ref = err;
        std::u16string tmp;
        AppendUtf16(&tmp, head.c_str());
        a[1] = MakeStr(state, tmp, ok);
        Value r{};
        if (*ok) InvokeMethod(state, print, a, 2, &r);
      }
      if (*ok) ThrowablePrintTo(state, ex, err, ok);
    }
  }
  return Value{};
}
// ---- M4.4 guest 线程运行时 seam（core/builtins.h 面；实现依托上方簿记）----

namespace {
// GC 保守栈扫描接缝：GuestSchedOps::visit_stacks -> memory/heap.cc 栈区枚举口
void SchedStacksTrampoline(void* ctx, void (*add)(void* base, size_t bytes, void* arg), void* arg) {
  const GuestSchedOps* ops = static_cast<const GuestSchedOps*>(ctx);
  if (ops != nullptr && ops->visit_stacks != nullptr) ops->visit_stacks(add, arg);
}
}  // namespace

void SetGuestSchedOps(const GuestSchedOps* ops) {
  g_guest_sched_ops = ops;
  mem::SetGlobalStackProvider(ops != nullptr ? &SchedStacksTrampoline : nullptr,
                              const_cast<GuestSchedOps*>(ops));
}
const GuestSchedOps* GuestSched() { return g_guest_sched_ops; }

void MonitorEnterImpl(VMState& state, ObjHeader* obj) {
  (void)state;
  GuestThreadCtx* me = CurrentCtx();
  MonitorRec& rec = GetMonitor(obj);
  if (me != nullptr && rec.owner == me) {
    obj->monitor += 1;  // 重入
    return;
  }
  if (rec.owner == nullptr) {
    rec.owner = me;  // 无主取得（含旧单流 nullptr owner）
    obj->monitor += 1;
    return;
  }
  // 争用（owner 挂起持锁中）：协程挂起等授予（Java synchronized 不可中断）
  if (g_guest_sched_ops == nullptr || me == nullptr || me->task == nullptr) {
    // 旧单流防御：不可能真争用——接管保活
    rec.owner = me;
    obj->monitor += 1;
    return;
  }
  rec.entry_waiters.push_back(me);
  struct PredArg {
    MonitorRec* rec;
    GuestThreadCtx* me;
  } pa{&rec, me};
  me->wait_state = 1;  // BLOCKED（monitor entry 等待；getState 投影）
  g_guest_sched_ops->wait(
      kGuestWaitMutex, -1,
      [](void* raw) -> bool {
        auto* a = static_cast<PredArg*>(raw);
        return a->rec->owner == a->me;
      },
      &pa);
  me->wait_state = 0;
  // 授予已在 GrantEntryWaiter 完成（owner=me、count=1）
}

bool MonitorExitImpl(VMState& state, ObjHeader* obj) {
  GuestThreadCtx* me = CurrentCtx();
  MonitorRec& rec = GetMonitor(obj);
  if (rec.owner != me || obj->monitor == 0) {
    state.ThrowNew("Ljava/lang/IllegalMonitorStateException;", "monitor-exit: not owner");
    return false;
  }
  obj->monitor -= 1;
  if (obj->monitor != 0) return true;  // 重入未清
  rec.owner = nullptr;
  GrantEntryWaiter(rec, obj);
  return true;
}

void SetGuestRunBudgetMs(long long ms) {
  if (ms > 0) g_guest_run_budget_ms = ms;
}
long long GuestRunBudgetMs() { return g_guest_run_budget_ms; }
const char* GuestRunFailureReason() { return g_guest_run_error.c_str(); }


bool RunGuestMainImpl(VMState& state, GuestMainFn fn, void* arg) {
  EnsureOomReserve(state);  // 预分配 OOM 单例（堆耗尽后无法再建；引导点预建）
  // 上一轮调度器已销毁：TLS 桥接残留 waiter task 指针作废（防跨轮 wake 悬挂）。
  TlsResetWaiters();
  g_guest_run_error.clear();
  const GuestSchedOps* ops = g_guest_sched_ops;
  if (ops == nullptr || g_in_guest_main) {
    return fn(state, arg);  // 旧单流 / 嵌套：直通
  }
  // 主线程身份取单例（不走 theCurrentThread 槽——worker 执行后槽指向 worker，
  // 误取会让下一轮 main 继承 worker 的 ctx：interrupted/completed 等状态污染）。
  VObject* main_thread = EnsureMainThreadObj(state);
  GuestThreadCtx* main_ctx = EnsureCtx(state, main_thread);
  MainPayload payload{&state, fn, arg, false};
  g_in_guest_main = true;
  g_main_done = false;
  g_main_ctx = main_ctx;
  main_ctx->started = true;
  main_ctx->completed = false;
  // 新一次 main 调用：中断/park/等待标志清零（JLS main 线程初始态；跨
  // RunGuestMain 复用时不得继承上一轮残留）
  main_ctx->interrupted = false;
  main_ctx->park_permit = false;
  main_ctx->wait_notified = false;
  main_ctx->wait_state = 0;
  main_ctx->state = &state;
  bool run_ok =
      ops->run_main(&RunMainEntry, &payload, static_cast<unsigned>(g_guest_run_budget_ms));
  // 无信号失败归因（"unknown signal" 误报修正）：预算中止 = 调度器 deadline
  // 命中后 mid-flight 放弃 guest task（fiber 随调度器析构回收）；其余区分
  // 未进入/进入后失败。信号崩溃路径不进此（CrashGuard 先截）。
  if (!payload.ok) {
    if (!run_ok) {
      g_guest_run_error = "scheduler budget " + std::to_string(g_guest_run_budget_ms) +
                          "ms exceeded (guest run abandoned mid-flight)";
    } else if (!payload.entered) {
      g_guest_run_error = "main guest task never entered (spawn/run failed)";
    } else {
      g_guest_run_error = "guest main entry did not complete";
    }
    // 帧深度计数（安全：仅计数不解引用——预算中止后 fiber 栈已随调度器析构
    // 回收，帧指针不可读）。深度大 = 语义套娃线索；深度浅 = 同层循环。
    g_guest_run_error += ", guest frame depth at abandon=" + std::to_string(state.frame_stack.size());
  }
  // guest task 句柄随调度器销毁失效：清簿记（身份/统计保留）
  for (auto& [task, c] : g_ctx_by_task) {
    (void)task;
    c->task = nullptr;
    c->completed = true;
    c->joiners.clear();
  }
  g_ctx_by_task.clear();
  // 被终止 task 的帧残留在共享 frame_stack（fiber 已回收 = 悬挂指针）：
  // 清活跃帧集（GC root / 抛点栈快照安全），并归零挂死 owner 的 monitor 计数。
  state.frame_stack.clear();
  state.last_throw_backtrace.clear();
  for (auto& [obj, rec] : g_monitors) {
    (void)rec;
    // 存活校验（IsHeapStart = sizes_ 成员判，不解引用）：防脏键悬挂指针
    if (obj != nullptr && state.heap != nullptr && state.heap->IsHeapStart(obj)) {
      obj->monitor = 0;
    }
  }
  g_monitors.clear();
  g_main_ctx = nullptr;
  g_in_guest_main = false;
  return payload.ok;
}

void GuestRuntimeReset(VMState* state) {
  // vm-crash 后 guest task 句柄失效（siglongjmp 跳出 run_main，调度器已弃）：
  // 防再入踩悬挂指针；残留帧/monitor 计数同 RunGuestMainImpl 收尾口径。
  TlsResetWaiters();  // TLS 桥接 waiter task 同口径作废
  for (auto& [task, c] : g_ctx_by_task) {
    (void)task;
    c->task = nullptr;
    c->completed = true;
    c->joiners.clear();
  }
  g_ctx_by_task.clear();
  if (state != nullptr) {
    state->frame_stack.clear();
    state->last_throw_backtrace.clear();
    state->ClearException();
  }
  for (auto& [obj, rec] : g_monitors) {
    (void)rec;
    if (obj != nullptr && state != nullptr && state->heap != nullptr &&
        state->heap->IsHeapStart(obj)) {
      obj->monitor = 0;
    }
  }
  g_monitors.clear();
  g_main_ctx = nullptr;
  g_main_thread = nullptr;  // 主线程 peer 随 heap 生命周期（reset 后不得悬挂）
  g_ctx_by_thread.clear();
  g_in_guest_main = false;
  g_main_done = false;
}

void GuestYieldPoint() {
  const GuestSchedOps* ops = g_guest_sched_ops;
  if (ops == nullptr || !ops->has_ready()) return;
  ops->yield_now();
}

}  // namespace dexvm::runtime
