// android.os 消息循环面（Looper/Handler/Message/MessageQueue/HandlerThread）
// + Process/UserHandle + android.system.Os + SystemClock。纯移动自
// core/framework_builtins.cc（零行为变更；注册仍在 registrar）。

#include "core/framework_builtins.h"

#include <unistd.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "core/class_linker.h"
#include "core/builtins.h"
#include "core/exception.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "core/framework_builtins_internal.h"
#include "core/builtins_internal.h"

namespace dexvm::runtime {

// ---- 宽字段槽（J） ----
namespace {
int64_t GetLField(VObject* o, uint32_t off) {
  int64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return slot;
}

void SetLField(VObject* o, uint32_t off, int64_t v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

// SystemClock.uptimeMillis 基准（真实时钟，自首次取用；注册时复位）
}  // namespace
uint64_t g_uptime_anchor_us = 0;

namespace {
int64_t UptimeNowMs() {
  uint64_t us = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  if (g_uptime_anchor_us == 0 || us < g_uptime_anchor_us) g_uptime_anchor_us = us;
  return static_cast<int64_t>((us - g_uptime_anchor_us) / 1000);
}

// ---- 消息循环 VM 侧状态（RegisterFrameworkBuiltins 复位；非移动 GC 下裸指针安全） ----
// 线程模型（用户确认）：执行 = 单线程 + 协程（消息泵驱动，正确性优先）；
// 但 Looper 一律按【线程身份】key 化——myLooper/prepare/Handler 默认绑定走
// Thread*->Looper* 身份查找，每个 Looper 有自己的 MessageQueue（无全局单例队列）。
// 动态线程（GuestTask 1:1、Carrier 池扩容）就绪：新线程=新身份键，语义不变。
}  // namespace
std::vector<VObject*> g_loopers;               // 全部已 prepare 的 Looper（泵目标；单线程泵统一驱动）
std::map<VObject*, VObject*> g_thread_looper;  // 线程身份 -> Looper（myLooper 语义键）
VObject* g_main_thread = nullptr;              // 主线程身份（引导约定：首个进入消息循环面的
                                               // 线程 = ActivityThread.main 所在线程）
bool g_pumping = false;                        // 泵重入护栏（loop/pump 嵌套不重复排空）

// 泵收敛上限（单次 PumpQueues 的 dispatch 条数；M4.3 消息泵无界排空的收敛边界）：
// 应用层重试循环（zonfig 配置 fetch 退避重试 = dispatch 内再 postDelayed 重投）
// 会让"排空至空"永不终止——单次泵按条数封顶后返回（剩余消息留在队列，下个泵点/
// 下一阶段继续），既保证 postDelayed(Runnable, 0/小延迟) 必达，又保证阶段推进
// 有界。量级 = 合法启动链（数十~数百条）之上留足余量；非特判，通用收敛口径。
namespace {
constexpr size_t kPumpDispatchLimit = 1024;

// Message 池（AOSP sPool 语义：单链经 next 字段、MAX_POOL_SIZE=50、recycleUnchecked
// 收池 / obtain 复用）。同 g_loopers 口径：非移动 GC 下裸指针安全。
}  // namespace
VObject* g_msg_pool = nullptr;
int32_t g_msg_pool_size = 0;
namespace {
constexpr int32_t kMsgPoolMax = 50;

// 对象字段布局（8 字节槽；注册区手工布局与此一致）
constexpr uint32_t kMsgWhat = 0;
constexpr uint32_t kMsgArg1 = 8;
constexpr uint32_t kMsgArg2 = 16;
constexpr uint32_t kMsgObj = 24;
constexpr uint32_t kMsgTarget = 32;
constexpr uint32_t kMsgCallback = 40;
constexpr uint32_t kMsgWhen = 48;
constexpr uint32_t kMsgNext = 56;
constexpr uint32_t kMsgFlags = 64;  // AOSP flags（int 槽）：IN_USE/ASYNCHRONOUS/REMOVED
constexpr uint32_t kMsgData = 72;   // AOSP mData（Bundle 引用；lib 缝，§31.7 B 档）
constexpr uint32_t kMsgSize = 80;   // Message 实例字段区总字节
// AOSP Message flags 位（FLAG_IN_USE：入队置位、obtain 清零、池内保持置位防误用；
// FLAG_ASYNCHRONOUS：异步消息（同步屏障后置）；FLAG_REMOVED：摘除标记）
constexpr int32_t kFlagInUse = 1 << 0;
constexpr int32_t kFlagAsynchronous = 1 << 1;
constexpr int32_t kFlagRemoved = 1 << 2;
constexpr uint32_t kMqMessages = 0;
constexpr uint32_t kMqQuitting = 8;
constexpr uint32_t kMqIdleHandlers = 16;  // [Landroid/os/MessageQueue$IdleHandler;（懒建）
constexpr uint32_t kMqIdleCount = 24;     // I：有效 idle handler 数（数组前缀）
constexpr uint32_t kLooperQueue = 0;
constexpr uint32_t kLooperThread = 8;
constexpr uint32_t kHandlerLooper = 0;
constexpr uint32_t kHandlerQueue = 8;
constexpr uint32_t kHandlerCallback = 16;
constexpr uint32_t kHtLooper = 104;  // Thread 实例区 104 字节之后（M4.4 Thread
                                     // 扩 parkBlocker/ThreadLocalRandom/ACC 槽位）
constexpr uint32_t kUhHandle = 0;

VObject* MsgNext(VObject* m) {
  return reinterpret_cast<VObject*>(GetRefField(m, kMsgNext));
}

void MsgSetNext(VObject* m, VObject* n) {
  SetRefField(m, kMsgNext, reinterpret_cast<ObjHeader*>(n));
}

void MsgReset(VObject* msg) {
  // obtain/构造语义：字段全零含 flags（AOSP obtain(): m.flags = 0——仅 obtain/
  // new Message 后可改写内容）
  std::memset(msg->field_data(), 0, kMsgSize);
}

// AOSP Message.clear()：字段清零 + flags=FLAG_IN_USE（池内哨兵：防 recycle 后误用）
void MsgClear(VObject* msg) {
  std::memset(msg->field_data(), 0, kMsgSize);
  SetIField(msg, kMsgFlags, kFlagInUse);
}

// AOSP recycleUnchecked：clear + 收池（内部回收面：泵 dispatch 后/摘除/quit 清队）。
// 不查 in-use（查面在公开 recycle()）。
void MsgRecycleUnchecked(VObject* msg) {
  if (msg == nullptr) return;
  MsgClear(msg);
  if (g_msg_pool_size < kMsgPoolMax) {
    MsgSetNext(msg, g_msg_pool);
    g_msg_pool = msg;
    g_msg_pool_size++;
  }
}

// 稳定按 when 升序插入（AOSP enqueueMessage 排序语义；同 when 追加其后）。
// 入队即 markInUse（AOSP：FLAG_IN_USE 置位至投递后回收，公开 recycle() 视为在用）。
void QueueInsert(VObject* q, VObject* msg, int64_t when) {
  SetLField(msg, kMsgWhen, when);
  MsgSetNext(msg, nullptr);
  SetIField(msg, kMsgFlags, GetIField(msg, kMsgFlags) | kFlagInUse);
  VObject* head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
  if (head == nullptr) {
    SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(msg));
    return;
  }
  VObject* prev = nullptr;
  VObject* cur = head;
  while (cur != nullptr && GetLField(cur, kMsgWhen) <= when) {
    prev = cur;
    cur = MsgNext(cur);
  }
  MsgSetNext(msg, cur);
  if (prev == nullptr) {
    SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(msg));
  } else {
    MsgSetNext(prev, msg);
  }
}

// sendMessageAtFrontOfQueue 语义：绝对队头（when=0）
void QueueInsertFront(VObject* q, VObject* msg) {
  SetLField(msg, kMsgWhen, 0);
  SetIField(msg, kMsgFlags, GetIField(msg, kMsgFlags) | kFlagInUse);  // AOSP markInUse
  VObject* head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
  MsgSetNext(msg, head);
  SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(msg));
}

// MessageQueue.next() 核心：队头出队。when=0 / <=now 立即；未来消息按虚拟
// 时间推进出队（"空等到下一 when" 的单线程投影）；空队列先走 AOSP idle 面
// （queueIdle 轮询，false 摘除；queueIdle 期间 post 的消息随即出队），仍空/
// quit 后返回 null（单线程 VM 不阻塞——coroutine deadline wait 后置）。
// ok=false = queueIdle 抛异常（pending exception 上抛，不吞）。
VObject* ArrGetRefAt(VArray* a, int32_t i) {
  VObject* o = nullptr;
  std::memcpy(&o, a->data() + static_cast<size_t>(i) * 8, sizeof(o));
  return o;
}
void ArrSetRefAt(VArray* a, int32_t i, VObject* o) {
  std::memcpy(a->data() + static_cast<size_t>(i) * 8, &o, sizeof(o));
}

// AOSP MessageQueue.next() 空档 idle 轮询：对每个 IdleHandler 调 queueIdle()，
// 返回 false 的摘除（保序压缩）；异常经 ok 上抛。
size_t g_idle_handler_runs = 0;  // 会话诊断：idle handler 执行轮数

bool QueueRunIdleHandlers(VMState& state, VObject* q, bool* ok) {
  *ok = true;
  if (GetIField(q, kMqQuitting) != 0) return true;
  VObject* arr = reinterpret_cast<VObject*>(GetRefField(q, kMqIdleHandlers));
  if (arr == nullptr) return true;
  g_idle_handler_runs++;
  int32_t count = GetIField(q, kMqIdleCount);
  VArray* a = reinterpret_cast<VArray*>(arr);
  int32_t out = 0;
  for (int32_t i = 0; i < count; i++) {
    VObject* h = ArrGetRefAt(a, i);
    if (h == nullptr) continue;
    Value r{};
    if (!CallVirtual(state, h, "queueIdle", {}, "Z", nullptr, 0, &r)) {
      *ok = false;
      return false;
    }
    if (r.i != 0) ArrSetRefAt(a, out++, h);  // keep（AOSP：true 留任）
  }
  for (int32_t i = out; i < count; i++) ArrSetRefAt(a, i, nullptr);  // 尾清（防残槽保活）
  SetIField(q, kMqIdleCount, out);
  return true;
}

// 实时语义出队（会话泵）：队首未到期 = 返回 null（不虚拟推进；IdleHandler 先跑）。
VObject* QueueNextDue(VMState& state, VObject* q, bool* ok) {
  *ok = true;
  VObject* head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
  int64_t now = UptimeNowMs();
  if (head == nullptr || GetLField(head, kMsgWhen) > now) {
    if (!QueueRunIdleHandlers(state, q, ok)) return nullptr;
    head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
    if (head == nullptr || GetLField(head, kMsgWhen) > UptimeNowMs()) return nullptr;
  }
  SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(MsgNext(head)));
  MsgSetNext(head, nullptr);
  return head;
}

VObject* QueueNext(VMState& state, VObject* q, bool* ok) {
  *ok = true;
  VObject* head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
  if (head == nullptr) {
    if (!QueueRunIdleHandlers(state, q, ok)) return nullptr;
    head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
    if (head == nullptr) return nullptr;
  } else if (GetLField(head, kMsgWhen) > UptimeNowMs()) {
    // AOSP next()：队首未到期（无 due 消息）先跑 IdleHandler——FragmentManager
    // 的 execPendingActions 等 idle 动作依赖此点；本 VM 泵随后按虚拟时间直接
    // 出队（保留 postDelayed 必达语义）。
    if (!QueueRunIdleHandlers(state, q, ok)) return nullptr;
    head = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
    if (head == nullptr) return nullptr;
  }
  SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(MsgNext(head)));
  MsgSetNext(head, nullptr);
  return head;
}

// quit：清空全部消息 + mQuitting（next() 即 null，loop 退出）。
// quitSafely：移除未来消息、保留已到期（AOSP removeAllFutureMessagesLocked），
// mQuitting 后剩余到期消息仍可出队。摘除消息一律 recycleUnchecked 收池（AOSP 同款）。
void QueueQuit(VObject* q, bool safe) {
  SetIField(q, kMqQuitting, 1);
  if (!safe) {
    VObject* cur = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
    SetRefField(q, kMqMessages, nullptr);
    while (cur != nullptr) {
      VObject* next = MsgNext(cur);
      MsgSetNext(cur, nullptr);
      MsgRecycleUnchecked(cur);
      cur = next;
    }
    return;
  }
  int64_t now = UptimeNowMs();
  VObject* prev = nullptr;
  VObject* cur = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
  while (cur != nullptr) {
    VObject* next = MsgNext(cur);
    if (GetLField(cur, kMsgWhen) > now) {
      MsgSetNext(cur, nullptr);
      if (prev == nullptr) {
        SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(next));
      } else {
        MsgSetNext(prev, next);
      }
      MsgRecycleUnchecked(cur);
    } else {
      prev = cur;
    }
    cur = next;
  }
}

// 按条件摘除消息（removeCallbacks/removeMessages/removeCallbacksAndMessages 语义）。
// callback==nullptr = 不限；filter_what/filter_obj 控制 what/obj 匹配。
void QueueRemoveMatching(VObject* q, VObject* target, VObject* callback, int32_t what,
                         bool filter_what, VObject* obj, bool filter_obj) {
  VObject* prev = nullptr;
  VObject* cur = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages));
  while (cur != nullptr) {
    VObject* next = MsgNext(cur);
    bool match =
        (target == nullptr || reinterpret_cast<VObject*>(GetRefField(cur, kMsgTarget)) == target);
    if (match && callback != nullptr) {
      match = reinterpret_cast<VObject*>(GetRefField(cur, kMsgCallback)) == callback;
    }
    if (match && filter_what) match = GetIField(cur, kMsgWhat) == what;
    if (match && filter_obj) {
      match = reinterpret_cast<VObject*>(GetRefField(cur, kMsgObj)) == obj;
    }
    if (match) {
      MsgSetNext(cur, nullptr);
      if (prev == nullptr) {
        SetRefField(q, kMqMessages, reinterpret_cast<ObjHeader*>(next));
      } else {
        MsgSetNext(prev, next);
      }
      MsgRecycleUnchecked(cur);  // AOSP 摘除即收池
    } else {
      prev = cur;
    }
    cur = next;
  }
}

// 当前执行线程（builtins Thread.currentThread 同款懒建；main 身份）
VObject* CurrentThread(VMState& state) {
  VClass* thr = state.linker->FindClass("Ljava/lang/Thread;");
  if (thr == nullptr || thr->static_storage.empty()) return nullptr;
  // 一律走 Thread.currentThread() intrinsic（按 GuestTask 身份返回）——静态槽
  // 只是"最后执行流"镜像，worker 执行后指向 worker，直接读槽会把当前身份判错
  // （主 Looper 被绑到工作线程的根因）。
  for (auto& m : thr->direct_methods) {
    if (m.name == "currentThread" && m.IsStatic() && m.return_type == "Ljava/lang/Thread;") {
      Value r{};
      if (InvokeMethod(state, &m, nullptr, 0, &r)) return reinterpret_cast<VObject*>(r.ref);
      state.ClearException();
      return nullptr;
    }
  }
  return reinterpret_cast<VObject*>(thr->static_storage[0].ref);
}

// 建 Looper 并按线程身份绑定（一人一 Looper 一队列；泵表登记）。
VObject* AllocLooper(VMState& state, VObject* thread) {
  VClass* looper_cls = state.linker->FindClass("Landroid/os/Looper;");
  VClass* queue_cls = state.linker->FindClass("Landroid/os/MessageQueue;");
  if (looper_cls == nullptr || queue_cls == nullptr) return nullptr;
  VObject* looper = state.linker->AllocObject(looper_cls);
  VObject* queue = state.linker->AllocObject(queue_cls);
  if (looper == nullptr || queue == nullptr) return nullptr;
  SetRefField(looper, kLooperQueue, reinterpret_cast<ObjHeader*>(queue));
  SetRefField(looper, kLooperThread, thread);
  if (thread != nullptr) g_thread_looper[thread] = looper;  // 线程身份键
  g_loopers.push_back(looper);
  return looper;
}

// 主 Looper 就绪（docs 19.1 Process State：ActivityThread.main 已
// prepareMainLooper，主业务码永远可见主 Looper）。主线程身份 = g_main_thread
// （引导约定首个进入消息循环面的线程；动态线程模型下 = main GuestTask 所在
// 线程）；已有 prepare() 绑定则收编（AOSP prepareMainLooper = prepare + 记账）。
VObject* EnsureMainLooper(VMState& state) {
  VClass* looper_cls = state.linker->FindClass("Landroid/os/Looper;");
  if (looper_cls == nullptr || looper_cls->static_storage.empty()) return nullptr;
  VObject* main = reinterpret_cast<VObject*>(looper_cls->static_storage[0].ref);
  if (main != nullptr) return main;
  // 主 Looper 绑定 = 进程主线程身份（确定性 main peer），不由首个调用者决定：
  // SDK 常在后台线程调 getMainLooper()（AOSP 返回已绑定的 sMainLooper，绝不把
  // 主 Looper 绑到调用者线程）。
  VObject* main_peer = MainThreadPeer(state);
  VObject* thread = main_peer != nullptr ? main_peer : CurrentThread(state);
  if (g_main_thread == nullptr) g_main_thread = thread;  // 主线程身份固化
  auto it = thread != nullptr ? g_thread_looper.find(thread) : g_thread_looper.end();
  if (it != g_thread_looper.end()) {
    main = it->second;  // 已 prepare：收编为 sMainLooper
  } else {
    main = AllocLooper(state, thread);
  }
  if (main != nullptr) looper_cls->static_storage[0].ref = main;
  return main;
}

// Handler 投递（AOSP sendMessageAtTime 收敛面）
bool HandlerSendAt(VMState& state, VObject* handler, VObject* msg, int64_t when, bool front) {
  if (handler == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Handler is null");
    return false;
  }
  VObject* q = reinterpret_cast<VObject*>(GetRefField(handler, kHandlerQueue));
  if (q == nullptr) {
    state.ThrowNew("Ljava/lang/RuntimeException;", "Handler has no MessageQueue");
    return false;
  }
  if (GetIField(q, kMqQuitting) != 0) return false;  // AOSP：quit 后投递失败
  SetRefField(msg, kMsgTarget, reinterpret_cast<ObjHeader*>(handler));
  if ((GetIField(msg, kMsgFlags) & kFlagInUse) != 0) {
    // AOSP enqueueMessage：在用（已入队/已投递/已回收）不可再投
    state.ThrowNew("Ljava/lang/IllegalStateException;", "This message is already in use.");
    return false;
  }
  if (front) {
    QueueInsertFront(q, msg);
  } else {
    QueueInsert(q, msg, when);
  }
  return true;
}

// dispatchMessage 语义（AOSP）：msg.callback.run() -> mCallback.handleMessage(msg)
// （返回 true 即止）-> handleMessage(msg)。
bool DispatchMessage(VMState& state, VObject* handler, VObject* msg) {
  if (msg == nullptr) return true;
  VObject* cb = reinterpret_cast<VObject*>(GetRefField(msg, kMsgCallback));
  if (cb != nullptr) {
    return CallVirtual(state, cb, "run", {}, "V", nullptr, 0, nullptr);
  }
  VObject* mcb = reinterpret_cast<VObject*>(GetRefField(handler, kHandlerCallback));
  if (mcb != nullptr) {
    Value mv{};
    mv.ref = msg;
    Value r{};
    if (!CallVirtual(state, mcb, "handleMessage", {"Landroid/os/Message;"}, "Z", &mv, 1, &r)) {
      return false;
    }
    if (r.i != 0) return true;  // Callback 消费
  }
  Value mv{};
  mv.ref = msg;
  return CallVirtual(state, handler, "handleMessage", {"Landroid/os/Message;"}, "V", &mv, 1,
                     nullptr);
}

// 当前执行流的 Thread 身份（Thread.currentThread intrinsic——per-GuestTask；
// 不复用 Thread.theCurrentThread 静态槽：槽是"最后执行流"镜像，worker 执行后
// 会指向 worker，泵会派发错线程队列）。
VObject* PumpCurrentThread(VMState& state) {
  VClass* thr = state.linker->FindClass("Ljava/lang/Thread;");
  if (thr == nullptr) return nullptr;
  for (auto& m : thr->direct_methods) {
    if (m.name != "currentThread" || !m.IsStatic() ||
        m.return_type != "Ljava/lang/Thread;") {
      continue;
    }
    Value r{};
    if (InvokeMethod(state, &m, nullptr, 0, &r)) return reinterpret_cast<VObject*>(r.ref);
    state.ClearException();
    return nullptr;
  }
  return nullptr;
}

// 单队列排空（when 升序；延迟消息虚拟时间出队）。收敛上限 kPumpDispatchLimit
// （应用重试自投递循环的排空边界；剩余消息下泵点续排）。
bool PumpOneQueue(VMState& state, VObject* q, size_t* dispatched, std::string* error) {
  while (true) {
    if (*dispatched >= kPumpDispatchLimit) break;  // 本轮封顶，下泵点续排
    bool qok = true;
    VObject* msg = QueueNext(state, q, &qok);
    if (!qok) {  // queueIdle 异常：rethrow 前中止（AOSP loopOnce 同款）
      if (error != nullptr) {
        *error = std::string("idle handler failed in pump: ") +
                 (state.HasPendingException() ? "(guest exception pending)" : "invoke failed");
      }
      return false;
    }
    if (msg == nullptr) break;
    VObject* target = reinterpret_cast<VObject*>(GetRefField(msg, kMsgTarget));
    if (target == nullptr) {
      MsgRecycleUnchecked(msg);  // 无 target（enqueueMessage 拒绝；防御丢弃收池）
      continue;
    }
    if (!DispatchMessage(state, target, msg)) {  // AOSP loopOnce：异常不回收
      if (error != nullptr) {
        *error = std::string("message dispatch failed in pump: ") +
                 (state.HasPendingException() ? "(guest exception pending)" : "invoke failed");
      }
      return false;
    }
    (*dispatched)++;
    MsgRecycleUnchecked(msg);  // AOSP loopOnce 尾：msg.recycleUnchecked()
  }
  return true;
}

// 消息泵核心：默认只排空【调用方线程自己的】Looper 队列。AOSP 线程亲和：
// Handler 消息只在所属线程的 loop 内派发——旧的全量泵会把后台线程的 Handler
// 消息挤到主线程生命周期链上执行（启动慢/主线程被后台工作占满的根因）。
// all_loopers=true 仅供宿主测试做"虚拟时间跨线程推进"（非 Android 语义面）。
}  // namespace
size_t IdleHandlerRuns() { return g_idle_handler_runs; }

// 实时泵（常驻会话）：只出队到期消息（postDelayed 按真实时间等待，不虚拟
// 推进——应用自投递重试不会形成消息洪水）；无到期消息即返回。
bool PumpDueQueue(VMState& state, VObject* q, size_t* dispatched, std::string* error) {
  while (true) {
    if (*dispatched >= kPumpDispatchLimit) break;
    bool qok = true;
    VObject* msg = QueueNextDue(state, q, &qok);
    if (!qok) {
      if (error != nullptr) {
        *error = std::string("idle handler failed in pump: ") +
                 (state.HasPendingException() ? "(guest exception pending)" : "invoke failed");
      }
      return false;
    }
    if (msg == nullptr) break;
    VObject* target = reinterpret_cast<VObject*>(GetRefField(msg, kMsgTarget));
    if (target == nullptr) {
      MsgRecycleUnchecked(msg);
      continue;
    }
    if (!DispatchMessage(state, target, msg)) {
      if (error != nullptr) {
        *error = std::string("message dispatch failed in pump: ") +
                 (state.HasPendingException() ? "(guest exception pending)" : "invoke failed");
      }
      return false;
    }
    (*dispatched)++;
    MsgRecycleUnchecked(msg);
  }
  return true;
}

bool PumpQueuesRealtime(VMState& state, std::string* error, size_t* dispatched_out) {
  if (g_pumping) return true;
  g_pumping = true;
  size_t dispatched = 0;
  VObject* thread = PumpCurrentThread(state);
  VObject* looper = nullptr;
  if (thread != nullptr) {
    auto it = g_thread_looper.find(thread);
    if (it != g_thread_looper.end()) looper = it->second;
  }
  bool ok = true;
  if (looper != nullptr) {
    VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
    if (q != nullptr) ok = PumpDueQueue(state, q, &dispatched, error);
  }
  g_pumping = false;
  if (dispatched_out != nullptr) *dispatched_out = dispatched;
  return ok;
}

bool PumpQueues(VMState& state, std::string* error, bool all_loopers, size_t* dispatched_out) {
  if (g_pumping) return true;  // 重入（dispatch 内触发 pump/loop）：外层继续排空
  g_pumping = true;
  size_t dispatched = 0;
  bool ok = true;
  if (all_loopers) {
    for (VObject* looper : g_loopers) {
      if (looper == nullptr) continue;
      VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
      if (q == nullptr) continue;
      if (!PumpOneQueue(state, q, &dispatched, error)) {
        ok = false;
        break;
      }
    }
  } else {
    VObject* thread = PumpCurrentThread(state);
    VObject* looper = nullptr;
    if (thread != nullptr) {
      auto it = g_thread_looper.find(thread);
      if (it != g_thread_looper.end()) looper = it->second;
    }
    if (looper != nullptr) {
      VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
      if (q != nullptr) ok = PumpOneQueue(state, q, &dispatched, error);
    }
    // 线程未 prepare Looper：无队列可泵（AOSP：无 looper 的线程没有消息循环）
  }
  g_pumping = false;
  if (dispatched_out != nullptr) *dispatched_out = dispatched;
  return ok;
}

// ---- android.os.Message ----

Value MsgInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  MsgReset(self);
  return Value{};
}

// obtain 核心（AOSP Message.obtain 重载面）：池优先复用（recycleUnchecked 收池，
// MAX_POOL_SIZE=50）；池空新建清零。每重载独立 IntrinsicFn（Value 联合体不携带
// int/ref 类型，按槽位定型）。
namespace {
VObject* ObtainMsgCore(VMState& state, bool* ok) {
  VObject* m = g_msg_pool;
  if (m != nullptr) {
    g_msg_pool = MsgNext(m);
    g_msg_pool_size--;
    MsgSetNext(m, nullptr);
    MsgReset(m);  // AOSP obtain()：m.flags = 0（池内字段已 clear，防御复清）
    return m;
  }
  VClass* msg_cls = state.linker->FindClass("Landroid/os/Message;");
  m = msg_cls != nullptr ? state.linker->AllocObject(msg_cls) : nullptr;
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "Message.obtain alloc failed");
    *ok = false;
  }
  return m;
}

}  // namespace
Value MsgObtain0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = ObtainMsgCore(state, ok);
  return r;
}

Value MsgObtainHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1) SetRefField(m, kMsgTarget, args[0].ref);
  r.ref = m;
  return r;
}

Value MsgObtainHandlerRunnable(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1) {
    SetRefField(m, kMsgTarget, args[0].ref);
    if (n >= 2) SetRefField(m, kMsgCallback, args[1].ref);
  }
  r.ref = m;
  return r;
}

Value MsgObtainHandlerWhat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1) {
    SetRefField(m, kMsgTarget, args[0].ref);
    if (n >= 2) SetIField(m, kMsgWhat, args[1].i);
  }
  r.ref = m;
  return r;
}

Value MsgObtainHandlerWhatObj(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1) {
    SetRefField(m, kMsgTarget, args[0].ref);
    if (n >= 2) SetIField(m, kMsgWhat, args[1].i);
    if (n >= 3) SetRefField(m, kMsgObj, args[2].ref);
  }
  r.ref = m;
  return r;
}

Value MsgObtainHandlerWhatArg1Arg2(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1) {
    SetRefField(m, kMsgTarget, args[0].ref);
    if (n >= 2) SetIField(m, kMsgWhat, args[1].i);
    if (n >= 3) SetIField(m, kMsgArg1, args[2].i);
    if (n >= 4) SetIField(m, kMsgArg2, args[3].i);
  }
  r.ref = m;
  return r;
}

Value MsgObtainHandlerWhatArg1Arg2Obj(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1) {
    SetRefField(m, kMsgTarget, args[0].ref);
    if (n >= 2) SetIField(m, kMsgWhat, args[1].i);
    if (n >= 3) SetIField(m, kMsgArg1, args[2].i);
    if (n >= 4) SetIField(m, kMsgArg2, args[3].i);
    if (n >= 5) SetRefField(m, kMsgObj, args[4].ref);
  }
  r.ref = m;
  return r;
}

// lib Bundle 新建/深拷贝缝（§31.7 B 档 POJO→lib：Bundle 本体在 runtime.dex）。
// from==nullptr -> new Bundle()；非空 -> new Bundle(from)（AOSP obtain(Message)/
// copyFrom 的 data 深拷贝语义，转调 lib 拷贝构造）。
namespace {
VObject* BundleNewCore(VMState& state, VObject* from) {
  VClass* b = state.linker->FindClass("Landroid/os/Bundle;");
  if (b == nullptr || !CheckClassDefinition(state, b) || !state.linker->InitializeClass(b, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/NoClassDefFoundError;", "Landroid/os/Bundle;");
    }
    return nullptr;
  }
  VObject* bundle = state.linker->AllocObject(b);
  if (bundle == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "Bundle alloc failed");
    return nullptr;
  }
  Value arg{};
  arg.ref = reinterpret_cast<ObjHeader*>(from);
  bool ok = from != nullptr
                ? CallVirtual(state, bundle, "<init>", {"Landroid/os/Bundle;"}, "V", &arg, 1, nullptr)
                : CallVirtual(state, bundle, "<init>", {}, "V", nullptr, 0, nullptr);
  return ok ? bundle : nullptr;
}

// obtain(Message orig)：AOSP——值拷贝 what/arg1/arg2/obj/target/callback +
// data 深拷贝（replyTo/sendingUid 面随后续 Parcelable 面后置）。
}  // namespace
Value MsgObtainOrig(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr && n >= 1 && args[0].ref != nullptr) {
    VObject* orig = reinterpret_cast<VObject*>(args[0].ref);
    SetIField(m, kMsgWhat, GetIField(orig, kMsgWhat));
    SetIField(m, kMsgArg1, GetIField(orig, kMsgArg1));
    SetIField(m, kMsgArg2, GetIField(orig, kMsgArg2));
    SetRefField(m, kMsgObj, GetRefField(orig, kMsgObj));
    SetRefField(m, kMsgTarget, GetRefField(orig, kMsgTarget));
    SetRefField(m, kMsgCallback, GetRefField(orig, kMsgCallback));
    VObject* data = reinterpret_cast<VObject*>(GetRefField(orig, kMsgData));
    if (data != nullptr) {
      VObject* copy = BundleNewCore(state, data);
      if (copy == nullptr) {
        *ok = false;
        return r;
      }
      SetRefField(m, kMsgData, reinterpret_cast<ObjHeader*>(copy));
    }
  }
  r.ref = m;
  return r;
}

// recycle()：AOSP——在用（入队/投递/已回收）抛 IllegalStateException；否则
// recycleUnchecked（clear + 收池，供 obtain 复用）。
Value MsgRecycle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  if ((GetIField(self, kMsgFlags) & kFlagInUse) != 0) {
    state.ThrowNew("Ljava/lang/IllegalStateException;",
                   "This message cannot be recycled because it is still in use.");
    *ok = false;
    return Value{};
  }
  MsgRecycleUnchecked(self);
  return Value{};
}

Value MsgSendToTarget(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  VObject* target = reinterpret_cast<VObject*>(GetRefField(self, kMsgTarget));
  if (target == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "sendToTarget: msg.target == null");
    *ok = false;
    return Value{};
  }
  Value mv{};
  mv.ref = self;
  Value r{};
  if (!CallVirtual(state, target, "sendMessage", {"Landroid/os/Message;"}, "Z", &mv, 1, &r)) {
    *ok = false;
  }
  return Value{};
}

Value MsgGetTarget(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kMsgTarget);
  return r;
}

Value MsgSetTarget(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetRefField(self, kMsgTarget, args[0].ref);
  return Value{};
}

// copyFrom(Message)：AOSP——拷贝 what/arg1/arg2/obj + data 深拷贝（不拷
// target/callback/when/next；flags 清 IN_USE|REMOVED 位）。
Value MsgCopyFrom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    VObject* o = reinterpret_cast<VObject*>(args[0].ref);
    SetIField(self, kMsgWhat, GetIField(o, kMsgWhat));
    SetIField(self, kMsgArg1, GetIField(o, kMsgArg1));
    SetIField(self, kMsgArg2, GetIField(o, kMsgArg2));
    SetRefField(self, kMsgObj, GetRefField(o, kMsgObj));
    SetIField(self, kMsgFlags, GetIField(o, kMsgFlags) & ~(kFlagInUse | kFlagRemoved));
    VObject* data = reinterpret_cast<VObject*>(GetRefField(o, kMsgData));
    if (data != nullptr) {
      VObject* copy = BundleNewCore(state, data);
      if (copy == nullptr) {
        *ok = false;
        return Value{};
      }
      SetRefField(self, kMsgData, reinterpret_cast<ObjHeader*>(copy));
    } else {
      SetRefField(self, kMsgData, nullptr);
    }
  }
  return Value{};
}

Value MsgGetWhen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.l = GetLField(self, kMsgWhen);
  return r;
}

Value MsgGetCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kMsgCallback);
  return r;
}

Value MsgIsAsynchronous(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = (GetIField(self, kMsgFlags) & kFlagAsynchronous) != 0 ? 1 : 0;
  return r;
}

Value MsgSetAsynchronous(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  int32_t flags = GetIField(self, kMsgFlags);
  if (n >= 1 && args[0].i != 0) {
    flags |= kFlagAsynchronous;
  } else {
    flags &= ~kFlagAsynchronous;
  }
  SetIField(self, kMsgFlags, flags);
  return Value{};
}

// getData()：AOSP——懒建 new Bundle()（lib 缝转调）并留存；peekData() 不建可空。
Value MsgGetData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* data = reinterpret_cast<VObject*>(GetRefField(self, kMsgData));
  if (data == nullptr) {
    data = BundleNewCore(state, nullptr);
    if (data == nullptr) {
      *ok = false;
      return r;
    }
    SetRefField(self, kMsgData, reinterpret_cast<ObjHeader*>(data));
  }
  r.ref = data;
  return r;
}

Value MsgPeekData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kMsgData);
  return r;
}

Value MsgSetData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetRefField(self, kMsgData, args[0].ref);
  return Value{};
}

// ---- android.os.MessageQueue ----

Value MQInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// enqueueMessage(Message, long)：AOSP 语义——msg.target 必须非空；quit 后拒绝
// （回收消息）；按 when 稳定入队。
Value MQEnqueueMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "enqueueMessage: msg == null");
    *ok = false;
    return r;
  }
  VObject* msg = reinterpret_cast<VObject*>(args[0].ref);
  if (GetRefField(msg, kMsgTarget) == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Message must have a target.");
    *ok = false;
    return r;
  }
  if ((GetIField(msg, kMsgFlags) & kFlagInUse) != 0) {
    // AOSP enqueueMessage：在用（已入队/已投递/已回收）不可入队
    state.ThrowNew("Ljava/lang/IllegalStateException;", "This message is already in use.");
    *ok = false;
    return r;
  }
  if (GetIField(self, kMqQuitting) != 0) {
    MsgRecycleUnchecked(msg);  // AOSP：拒绝后回收
    return r;
  }
  QueueInsert(self, msg, args[1].l);
  r.i = 1;
  return r;
}

Value MQNext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = QueueNext(state, self, ok);
  return r;
}

Value MQQuit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  QueueQuit(self, /*safe=*/false);
  return Value{};
}

Value MQQuitSafely(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  QueueQuit(self, /*safe=*/true);
  return Value{};
}

// addIdleHandler(IdleHandler)（AOSP：mIdleHandlers.add——append，不去重）。
// 存储 = 懒建 VArray 前缀 + 计数（GC 传递闭包面：mIdleHandlers 引用字段 +
// 数组元素全量标记）。
Value MQAddIdleHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "addIdleHandler: handler == null");
    *ok = false;
    return Value{};
  }
  VObject* handler = reinterpret_cast<VObject*>(args[0].ref);
  VObject* arr = reinterpret_cast<VObject*>(GetRefField(self, kMqIdleHandlers));
  int32_t count = GetIField(self, kMqIdleCount);
  if (arr == nullptr) {
    VClass* iface = state.linker->FindClass("Landroid/os/MessageQueue$IdleHandler;");
    VClass* arr_cls = iface != nullptr ? state.linker->FindArrayClass(iface) : nullptr;
    VArray* a = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, 4) : nullptr;
    if (a == nullptr) {
      state.ThrowNew("Ljava/lang/OutOfMemoryError;", "addIdleHandler alloc failed");
      *ok = false;
      return Value{};
    }
    arr = reinterpret_cast<VObject*>(a);
    SetRefField(self, kMqIdleHandlers, reinterpret_cast<ObjHeader*>(arr));
  }
  VArray* a = reinterpret_cast<VArray*>(arr);
  if (count >= a->length) {
    // 扩容翻倍（元素保序搬移；旧数组即弃——GC 回收）
    int32_t cap = a->length * 2;
    VClass* arr_cls = a->klass;
    VArray* grown = state.linker->AllocArray(arr_cls, cap);
    if (grown == nullptr) {
      state.ThrowNew("Ljava/lang/OutOfMemoryError;", "addIdleHandler grow failed");
      *ok = false;
      return Value{};
    }
    for (int32_t i = 0; i < count; i++) ArrSetRefAt(grown, i, ArrGetRefAt(a, i));
    a = grown;
    arr = reinterpret_cast<VObject*>(a);
    SetRefField(self, kMqIdleHandlers, reinterpret_cast<ObjHeader*>(arr));
  }
  ArrSetRefAt(a, count, handler);
  SetIField(self, kMqIdleCount, count + 1);
  return Value{};
}

// removeIdleHandler(IdleHandler)（AOSP ArrayList.remove(Object)：首个身份命中
// 摘除；无命中 no-op）。
Value MQRemoveIdleHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  VObject* handler = reinterpret_cast<VObject*>(args[0].ref);
  VObject* arr = reinterpret_cast<VObject*>(GetRefField(self, kMqIdleHandlers));
  if (arr == nullptr) return Value{};
  VArray* a = reinterpret_cast<VArray*>(arr);
  int32_t count = GetIField(self, kMqIdleCount);
  for (int32_t i = 0; i < count; i++) {
    if (ArrGetRefAt(a, i) != handler) continue;
    for (int32_t j = i; j + 1 < count; j++) ArrSetRefAt(a, j, ArrGetRefAt(a, j + 1));
    ArrSetRefAt(a, count - 1, nullptr);
    SetIField(self, kMqIdleCount, count - 1);
    break;
  }
  return Value{};
}

// ---- android.os.Looper ----

// prepare([boolean])：为当前线程建 Looper（AOSP：一线程一 Looper，重复抛
// RuntimeException）。
Value LooperPrepare(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  VObject* thread = CurrentThread(state);
  if (thread != nullptr && g_thread_looper.find(thread) != g_thread_looper.end()) {
    state.ThrowNew("Ljava/lang/RuntimeException;", "Only one Looper may be created per thread");
    *ok = false;
    return Value{};
  }
  if (AllocLooper(state, thread) == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "Looper.prepare alloc failed");
    *ok = false;
  }
  return Value{};
}

// prepareMainLooper()：prepare + sMainLooper（AOSP：已存在则 IllegalStateException）。
Value LooperPrepareMain(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  VClass* looper_cls = state.linker->FindClass("Landroid/os/Looper;");
  if (looper_cls == nullptr || looper_cls->static_storage.empty()) {
    state.ThrowNew("Ljava/lang/InternalError;", "Looper class missing");
    *ok = false;
    return Value{};
  }
  if (looper_cls->static_storage[0].ref != nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;",
                   "The main Looper has already been prepared.");
    *ok = false;
    return Value{};
  }
  VObject* thread = CurrentThread(state);
  if (thread != nullptr && g_thread_looper.find(thread) != g_thread_looper.end()) {
    state.ThrowNew("Ljava/lang/RuntimeException;", "Only one Looper may be created per thread");
    *ok = false;
    return Value{};
  }
  VObject* main = AllocLooper(state, thread);
  if (main == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "prepareMainLooper alloc failed");
    *ok = false;
    return Value{};
  }
  if (g_main_thread == nullptr) g_main_thread = thread;  // 主线程身份固化
  looper_cls->static_storage[0].ref = main;
  return Value{};
}

Value LooperMyLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* thread = CurrentThread(state);
  // 身份查找路径（线程身份 key 化；动态线程 GuestTask 1:1 就绪——新线程即新键）
  auto it = thread != nullptr ? g_thread_looper.find(thread) : g_thread_looper.end();
  if (it != g_thread_looper.end()) {
    r.ref = it->second;
    return r;
  }
  // 主线程进程态投影（docs 19.1）：主线程身份的 Looper 恒就绪（ActivityThread.main
  // 的 prepareMainLooper 语义）；其余线程按 AOSP 返回 null——Handler() 将抛
  // "thread has not called Looper.prepare()"，为动态线程留门。
  VObject* main_peer = MainThreadPeer(state);
  if (thread != nullptr && main_peer != nullptr && thread == main_peer) {
    r.ref = EnsureMainLooper(state);
  }
  return r;
}

Value LooperGetMainLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = EnsureMainLooper(state);
  return r;
}

// loop()：本线程 Looper 的消息泵（while active: next_message -> dispatch；
// 空/quit 即出——单线程 VM 投影，coroutine wait 后置）。
Value LooperLoop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value ml = LooperMyLooper(state, nullptr, nullptr, 0, ok);
  if (!*ok) return Value{};
  VObject* looper = reinterpret_cast<VObject*>(ml.ref);
  if (looper == nullptr) {
    state.ThrowNew("Ljava/lang/RuntimeException;",
                   "No Looper; Looper.prepare() wasn't called on this thread.");
    *ok = false;
    return Value{};
  }
  VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
  while (true) {
    VObject* msg = QueueNext(state, q, ok);
    if (!*ok) return Value{};
    if (msg == nullptr) break;
    VObject* target = reinterpret_cast<VObject*>(GetRefField(msg, kMsgTarget));
    if (target == nullptr) continue;
    if (!DispatchMessage(state, target, msg)) {
      *ok = false;
      return Value{};
    }
  }
  return Value{};
}

Value LooperQuit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kLooperQueue));
  if (q != nullptr) QueueQuit(q, /*safe=*/false);
  return Value{};
}

Value LooperQuitSafely(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kLooperQueue));
  if (q != nullptr) QueueQuit(q, /*safe=*/true);
  return Value{};
}

Value LooperGetThread(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kLooperThread);
  return r;
}

Value LooperGetQueue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kLooperQueue);
  return r;
}

// myQueue()（AOSP 静态隐藏面）：当前线程 Looper 的 MessageQueue——
// `myLooper().mQueue` 投影；无线程 Looper 时 AOSP 形态（.mQueue 解引用）
// NullPointerException。zhihu BaseLifecycleProvider.onCreate@40 消费面。
Value LooperMyQueue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value ml = LooperMyLooper(state, nullptr, nullptr, 0, ok);
  if (!*ok) return Value{};
  VObject* looper = reinterpret_cast<VObject*>(ml.ref);
  if (looper == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;",
                   "Looper.myQueue() on thread with no Looper");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = GetRefField(looper, kLooperQueue);
  return r;
}

// ---- android.os.Handler ----

// AOSP 构造收敛：(Looper[, Callback]) 绑定 looper 与其队列；()/(Callback) 绑定
// myLooper()（app 进程主 Looper 恒就绪；AOSP "thread has not called Looper.prepare"
// RuntimeException 被 Process State 语义吸收）。每重载独立 IntrinsicFn（Callback
// 实现类 descriptor 不等于接口，按槽位定型）。
namespace {
bool HandlerBind(VMState& state, VObject* self, VObject* looper, VObject* callback, bool* ok) {
  if (looper == nullptr) {
    Value ml = LooperMyLooper(state, nullptr, nullptr, 0, ok);
    if (!*ok) return false;
    looper = reinterpret_cast<VObject*>(ml.ref);
    if (looper == nullptr) {
      state.ThrowNew("Ljava/lang/RuntimeException;",
                     "Can't create handler inside thread that has not called Looper.prepare()");
      *ok = false;
      return false;
    }
  }
  SetRefField(self, kHandlerLooper, reinterpret_cast<ObjHeader*>(looper));
  SetRefField(self, kHandlerQueue, GetRefField(looper, kLooperQueue));
  if (callback != nullptr) {
    SetRefField(self, kHandlerCallback, reinterpret_cast<ObjHeader*>(callback));
  }
  return true;
}

}  // namespace
Value HandlerInit0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  HandlerBind(state, self, nullptr, nullptr, ok);
  return Value{};
}

Value HandlerInitCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  HandlerBind(state, self, nullptr, n >= 1 ? reinterpret_cast<VObject*>(args[0].ref) : nullptr,
              ok);
  return Value{};
}

Value HandlerInitLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  HandlerBind(state, self, n >= 1 ? reinterpret_cast<VObject*>(args[0].ref) : nullptr, nullptr,
              ok);
  return Value{};
}

Value HandlerInitLooperCallback(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  *ok = true;
  HandlerBind(state, self, n >= 1 ? reinterpret_cast<VObject*>(args[0].ref) : nullptr,
              n >= 2 ? reinterpret_cast<VObject*>(args[1].ref) : nullptr, ok);
  return Value{};
}

// Handler.createAsync(Looper[, Callback])（API 28+ 静态工厂）：本 VM 派发语义
// 不区分异步消息，等价 new Handler(looper[, callback])。androidx
// DefaultTaskExecutor.postToMainThread 在 SDK>=28 走此面；缺实现（stub 返回
// null）会让 LiveData.postValue 的 Handler 为 null，主线程投递 NPE，feed 状态
// 永不回灌（白屏根因）。
namespace {
Value HandlerCreateAsyncImpl(VMState& state, VObject* looper, VObject* callback, bool* ok) {
  *ok = true;
  Value r{};
  VClass* hk =
      state.linker != nullptr ? state.linker->FindClass("Landroid/os/Handler;") : nullptr;
  if (hk == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "android.os.Handler missing");
    *ok = false;
    return r;
  }
  VObject* h = state.linker->AllocObject(hk);
  if (h == nullptr) {
    ThrowOOM(state, "Handler.createAsync");
    *ok = false;
    return r;
  }
  if (!HandlerBind(state, h, looper, callback, ok)) return r;
  r.ref = h;
  return r;
}
}  // namespace

Value HandlerCreateAsync(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  return HandlerCreateAsyncImpl(state,
                                n >= 1 ? reinterpret_cast<VObject*>(args[0].ref) : nullptr,
                                nullptr, ok);
}

Value HandlerCreateAsyncCallback(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  (void)self;
  return HandlerCreateAsyncImpl(state,
                                n >= 1 ? reinterpret_cast<VObject*>(args[0].ref) : nullptr,
                                n >= 2 ? reinterpret_cast<VObject*>(args[1].ref) : nullptr, ok);
}

// post(Runnable) -> sendMessageDelayed(getPostMessage(r), 0)
// ---- obtainMessage 全家族（AOSP Handler.obtainMessage：一律 Message.obtain(this,...)）----
// target=handler 预置，字段同 Message.obtain 对应重载。(I,I,Object) 为任务面补全
// （AOSP 公开面无此重载；按 what/arg1/obj 语义落位、arg2=0）。
namespace {
VObject* HandlerObtainCore(VMState& state, VObject* handler, bool* ok) {
  VObject* m = ObtainMsgCore(state, ok);
  if (m != nullptr) SetRefField(m, kMsgTarget, reinterpret_cast<ObjHeader*>(handler));
  return m;
}

}  // namespace
Value HandlerObtainMessage0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = HandlerObtainCore(state, self, ok);
  return r;
}

Value HandlerObtainMessageWhat(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  *ok = true;
  Value r{};
  VObject* m = HandlerObtainCore(state, self, ok);
  if (m != nullptr && n >= 1) SetIField(m, kMsgWhat, args[0].i);
  r.ref = m;
  return r;
}

Value HandlerObtainMessageWhatObj(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  *ok = true;
  Value r{};
  VObject* m = HandlerObtainCore(state, self, ok);
  if (m != nullptr && n >= 1) {
    SetIField(m, kMsgWhat, args[0].i);
    if (n >= 2) SetRefField(m, kMsgObj, args[1].ref);
  }
  r.ref = m;
  return r;
}

Value HandlerObtainMessageWhatArg1Arg2(VMState& state, VObject* self, const Value* args, size_t n,
                                       bool* ok) {
  *ok = true;
  Value r{};
  VObject* m = HandlerObtainCore(state, self, ok);
  if (m != nullptr && n >= 1) {
    SetIField(m, kMsgWhat, args[0].i);
    if (n >= 2) SetIField(m, kMsgArg1, args[1].i);
    if (n >= 3) SetIField(m, kMsgArg2, args[2].i);
  }
  r.ref = m;
  return r;
}

Value HandlerObtainMessageWhatArg1Obj(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok) {
  *ok = true;
  Value r{};
  VObject* m = HandlerObtainCore(state, self, ok);
  if (m != nullptr && n >= 1) {
    SetIField(m, kMsgWhat, args[0].i);
    if (n >= 2) SetIField(m, kMsgArg1, args[1].i);
    if (n >= 3) SetRefField(m, kMsgObj, args[2].ref);
  }
  r.ref = m;
  return r;
}

Value HandlerObtainMessageWhatArg1Arg2Obj(VMState& state, VObject* self, const Value* args,
                                          size_t n, bool* ok) {
  *ok = true;
  Value r{};
  VObject* m = HandlerObtainCore(state, self, ok);
  if (m != nullptr && n >= 1) {
    SetIField(m, kMsgWhat, args[0].i);
    if (n >= 2) SetIField(m, kMsgArg1, args[1].i);
    if (n >= 3) SetIField(m, kMsgArg2, args[2].i);
    if (n >= 4) SetRefField(m, kMsgObj, args[3].ref);
  }
  r.ref = m;
  return r;
}

Value HandlerPost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "post: Runnable == null");
    *ok = false;
    return r;
  }
  VObject* msg = ObtainMsgCore(state, ok);
  if (msg == nullptr) return r;
  SetRefField(msg, kMsgCallback, args[0].ref);
  r.i = HandlerSendAt(state, self, msg, UptimeNowMs(), false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

// postAtTime(R[, token], when)：绝对 uptime 时刻
Value HandlerPostAtTime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "postAtTime: Runnable == null");
    *ok = false;
    return r;
  }
  VObject* msg = ObtainMsgCore(state, ok);
  if (msg == nullptr) return r;
  SetRefField(msg, kMsgCallback, args[0].ref);
  int64_t when;
  if (n == 2) {
    when = args[1].l;
  } else {
    SetRefField(msg, kMsgObj, args[1].ref);
    when = args[2].l;
  }
  r.i = HandlerSendAt(state, self, msg, when, false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

// postDelayed(R[, token], delayMillis)：相对延迟（when = uptimeMillis + delay）
Value HandlerPostDelayed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "postDelayed: Runnable == null");
    *ok = false;
    return r;
  }
  VObject* msg = ObtainMsgCore(state, ok);
  if (msg == nullptr) return r;
  SetRefField(msg, kMsgCallback, args[0].ref);
  int64_t when;
  if (n == 2) {
    when = UptimeNowMs() + args[1].l;
  } else {
    SetRefField(msg, kMsgObj, args[1].ref);
    when = UptimeNowMs() + args[2].l;
  }
  r.i = HandlerSendAt(state, self, msg, when, false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

Value HandlerPostAtFrontOfQueue(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "postAtFrontOfQueue: Runnable == null");
    *ok = false;
    return r;
  }
  VObject* msg = ObtainMsgCore(state, ok);
  if (msg == nullptr) return r;
  SetRefField(msg, kMsgCallback, args[0].ref);
  r.i = HandlerSendAt(state, self, msg, 0, /*front=*/true) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

Value HandlerSendMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "sendMessage: msg == null");
    *ok = false;
    return r;
  }
  VObject* msg = reinterpret_cast<VObject*>(args[0].ref);
  r.i = HandlerSendAt(state, self, msg, UptimeNowMs(), false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

Value HandlerSendMessageDelayed(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "sendMessageDelayed: msg == null");
    *ok = false;
    return r;
  }
  VObject* msg = reinterpret_cast<VObject*>(args[0].ref);
  r.i = HandlerSendAt(state, self, msg, UptimeNowMs() + args[1].l, false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

Value HandlerSendMessageAtTime(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "sendMessageAtTime: msg == null");
    *ok = false;
    return r;
  }
  VObject* msg = reinterpret_cast<VObject*>(args[0].ref);
  r.i = HandlerSendAt(state, self, msg, args[1].l, false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

Value HandlerSendMessageAtFrontOfQueue(VMState& state, VObject* self, const Value* args, size_t n,
                                       bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "sendMessageAtFrontOfQueue: msg == null");
    *ok = false;
    return r;
  }
  VObject* msg = reinterpret_cast<VObject*>(args[0].ref);
  r.i = HandlerSendAt(state, self, msg, 0, /*front=*/true) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

// sendEmptyMessage(i) / sendEmptyMessageDelayed(i, delay)
Value HandlerSendEmptyMessage(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  VObject* msg = ObtainMsgCore(state, ok);
  if (msg == nullptr) return r;
  if (n >= 1) SetIField(msg, kMsgWhat, args[0].i);
  int64_t when = UptimeNowMs();
  if (n >= 2) when += args[1].l;
  r.i = HandlerSendAt(state, self, msg, when, false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

// sendEmptyMessageAtTime(i, when)
Value HandlerSendEmptyMessageAtTime(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  VObject* msg = ObtainMsgCore(state, ok);
  if (msg == nullptr) return r;
  if (n >= 1) SetIField(msg, kMsgWhat, args[0].i);
  int64_t when = n >= 2 ? args[1].l : UptimeNowMs();
  r.i = HandlerSendAt(state, self, msg, when, false) ? 1 : 0;
  if (state.HasPendingException()) *ok = false;
  return r;
}

// handleMessage：骨架空实现（可覆写；AOSP 空方法体）
Value HandlerHandleMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

Value HandlerDispatchMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1) return Value{};
  if (!DispatchMessage(state, self, reinterpret_cast<VObject*>(args[0].ref))) *ok = false;
  return Value{};
}

Value HandlerGetLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kHandlerLooper);
  return r;
}

Value HandlerRemoveCallbacks(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kHandlerQueue));
  if (q != nullptr && n >= 1) {
    if (n == 1) {
      QueueRemoveMatching(q, self, reinterpret_cast<VObject*>(args[0].ref), 0, false, nullptr,
                          false);
    } else {
      QueueRemoveMatching(q, self, reinterpret_cast<VObject*>(args[0].ref), 0, false,
                          reinterpret_cast<VObject*>(args[1].ref), true);
    }
  }
  return Value{};
}

Value HandlerRemoveMessages(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kHandlerQueue));
  if (q != nullptr && n >= 1) {
    if (n == 1) {
      QueueRemoveMatching(q, self, nullptr, args[0].i, true, nullptr, false);
    } else {
      QueueRemoveMatching(q, self, nullptr, args[0].i, true,
                          reinterpret_cast<VObject*>(args[1].ref), true);
    }
  }
  return Value{};
}

// removeCallbacksAndMessages(Object token)：token==null 移除本 Handler 全部消息，
// 否则移除 obj==token 的消息（AOSP MessageQueue.removeCallbacksAndMessages）。
Value HandlerRemoveCallbacksAndMessages(VMState& state, VObject* self, const Value* args,
                                        size_t n, bool* ok) {
  (void)state;
  *ok = true;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kHandlerQueue));
  if (q != nullptr) {
    if (n == 0 || args[0].ref == nullptr) {
      QueueRemoveMatching(q, self, nullptr, 0, false, nullptr, false);
    } else {
      QueueRemoveMatching(q, self, nullptr, 0, false, reinterpret_cast<VObject*>(args[0].ref),
                          true);
    }
  }
  return Value{};
}

Value HandlerHasMessages(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 0;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kHandlerQueue));
  if (q != nullptr && n >= 1) {
    int32_t what = args[0].i;
    VObject* obj = n >= 2 ? reinterpret_cast<VObject*>(args[1].ref) : nullptr;
    bool filter_obj = n >= 2;
    for (VObject* cur = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages)); cur != nullptr;
         cur = MsgNext(cur)) {
      if (reinterpret_cast<VObject*>(GetRefField(cur, kMsgTarget)) != self) continue;
      if (GetIField(cur, kMsgWhat) != what) continue;
      if (filter_obj && reinterpret_cast<VObject*>(GetRefField(cur, kMsgObj)) != obj) continue;
      r.i = 1;
      break;
    }
  }
  return r;
}

Value HandlerHasCallbacks(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = 0;
  VObject* q = reinterpret_cast<VObject*>(GetRefField(self, kHandlerQueue));
  if (q != nullptr && n >= 1 && args[0].ref != nullptr) {
    for (VObject* cur = reinterpret_cast<VObject*>(GetRefField(q, kMqMessages)); cur != nullptr;
         cur = MsgNext(cur)) {
      if (reinterpret_cast<VObject*>(GetRefField(cur, kMsgTarget)) != self) continue;
      if (reinterpret_cast<VObject*>(GetRefField(cur, kMsgCallback)) == args[0].ref) {
        r.i = 1;
        break;
      }
    }
  }
  return r;
}

// ---- android.os.HandlerThread ----

// <init>(String[, priority])：Thread 语义（name/priority 字段），Looper 于 start 就绪。
Value HTInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    VField* nf = FindInstanceField(self->klass, "name");
    if (nf != nullptr) SetRefField(self, nf->offset, args[0].ref);
  }
  if (n >= 2) {
    VField* pf = FindInstanceField(self->klass, "priority");
    if (pf != nullptr) SetIField(self, pf->offset, args[1].i);
  } else {
    VField* pf = FindInstanceField(self->klass, "priority");
    if (pf != nullptr) SetIField(self, pf->offset, 5);
  }
  return Value{};
}

// start()：HandlerThread = 新 GuestTask + 自己的 Looper（线程身份 = 本
// HandlerThread 实例；独立 MessageQueue，非全局队列）。动态线程（GuestTask
// 1:1）就绪；当前单线程模型下执行由消息泵统一驱动（14.10：不占 OS 线程）。
Value HTStart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  VObject* existing = reinterpret_cast<VObject*>(GetRefField(self, kHtLooper));
  if (existing != nullptr) return Value{};  // 幂等
  VObject* looper = AllocLooper(state, self);
  if (looper == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "HandlerThread.start alloc failed");
    *ok = false;
    return Value{};
  }
  SetRefField(self, kHtLooper, reinterpret_cast<ObjHeader*>(looper));
  return Value{};
}

Value HTGetLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, kHtLooper);
  return r;
}

Value HTQuit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  VObject* looper = reinterpret_cast<VObject*>(GetRefField(self, kHtLooper));
  if (looper != nullptr) {
    VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
    if (q != nullptr) {
      QueueQuit(q, /*safe=*/false);
      r.i = 1;
    }
  }
  return r;
}

Value HTQuitSafely(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  VObject* looper = reinterpret_cast<VObject*>(GetRefField(self, kHtLooper));
  if (looper != nullptr) {
    VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
    if (q != nullptr) {
      QueueQuit(q, /*safe=*/true);
      r.i = 1;
    }
  }
  return r;
}

// run()：Looper.prepare + loop（AOSP HandlerThread.run；单线程 VM 下显式调用才执行）
Value HTRun(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  if (GetRefField(self, kHtLooper) == nullptr) {
    VObject* looper = AllocLooper(state, self);
    if (looper == nullptr) {
      state.ThrowNew("Ljava/lang/OutOfMemoryError;", "HandlerThread.run alloc failed");
      *ok = false;
      return Value{};
    }
    SetRefField(self, kHtLooper, reinterpret_cast<ObjHeader*>(looper));
  }
  VObject* looper = reinterpret_cast<VObject*>(GetRefField(self, kHtLooper));
  VObject* q = reinterpret_cast<VObject*>(GetRefField(looper, kLooperQueue));
  while (true) {
    VObject* msg = QueueNext(state, q, ok);
    if (!*ok) return Value{};
    if (msg == nullptr) break;
    VObject* target = reinterpret_cast<VObject*>(GetRefField(msg, kMsgTarget));
    if (target == nullptr) continue;
    if (!DispatchMessage(state, target, msg)) {
      *ok = false;
      return Value{};
    }
  }
  return Value{};
}

// ---- android.os.Process / UserHandle ----

Value ProcessMyPid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(::getpid());
  return r;
}

Value ProcessMyTid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // 单线程 VM 主执行流投影：Android 主线程 tid == pid
  return ProcessMyPid(state, self, args, n, ok);
}

Value ProcessMyUid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(::getuid());
  return r;
}

Value ProcessMyUserHandle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* uh = state.linker->FindClass("Landroid/os/UserHandle;");
  VObject* o = uh != nullptr ? state.linker->AllocObject(uh) : nullptr;
  if (o == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "Process.myUserHandle alloc failed");
    *ok = false;
    return r;
  }
  // UserHandle 语义：Android user id = uid / PER_USER_RANGE(100000)
  SetIField(o, kUhHandle, static_cast<int32_t>(::getuid() / 100000));
  r.ref = o;
  return r;
}

// ---- android.system.Os / ErrnoException（考卷链：Os.readlink 探测面）----
// 真实 syscall 语义（readlink 失败抛 ErrnoException(functionName, errno)）。
Value OsReadlink(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Os.readlink: path == null");
    *ok = false;
    return r;
  }
  std::string path = Mutf8Arg(args[0]);
  char buf[4096];
  ssize_t len = ::readlink(path.c_str(), buf, sizeof(buf) - 1);
  if (len < 0) {
    // ErrnoException(String functionName, int errno)
    VClass* ee = state.linker->FindClass("Landroid/system/ErrnoException;");
    VObject* ex = ee != nullptr ? state.linker->AllocObject(ee) : nullptr;
    if (ex == nullptr) {
      state.ThrowNew("Ljava/lang/RuntimeException;", "Os.readlink failed (no ErrnoException)");
      *ok = false;
      return r;
    }
    VField* fn_f = FindInstanceField(ee, "functionName");
    if (fn_f != nullptr) {
      SetRefField(ex, fn_f->offset,
                  reinterpret_cast<ObjHeader*>(state.linker->AllocString(u"readlink")));
    }
    VField* en_f = FindInstanceField(ee, "errno");
    if (en_f != nullptr) SetIField(ex, en_f->offset, errno);
    VField* msg_f = FindInstanceField(ee, "detailMessage");
    if (msg_f != nullptr) {
      std::string m = "readlink failed: errno " + std::to_string(errno);
      std::u16string u(m.begin(), m.end());
      SetRefField(ex, msg_f->offset, reinterpret_cast<ObjHeader*>(state.linker->AllocString(u)));
    }
    state.Throw(ex);
    *ok = false;
    return r;
  }
  buf[len] = '\0';
  std::string out(buf, static_cast<size_t>(len));
  std::u16string u(out.begin(), out.end());
  r.ref = state.linker->AllocString(u);
  return r;
}

Value OsMyPid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ProcessMyPid(state, self, args, n, ok);
}

Value OsMyUid(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ProcessMyUid(state, self, args, n, ok);
}

// ErrnoException(String functionName, int errno)：字段 + detailMessage（getMessage 继承面）
Value ErrnoExceptionInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 1) {
    VField* fn_f = FindInstanceField(self->klass, "functionName");
    if (fn_f != nullptr) SetRefField(self, fn_f->offset, args[0].ref);
  }
  if (n >= 2) {
    VField* en_f = FindInstanceField(self->klass, "errno");
    if (en_f != nullptr) SetIField(self, en_f->offset, args[1].i);
    std::string fn = n >= 1 && args[0].ref != nullptr
                         ? static_cast<VString*>(args[0].ref)->ToModifiedUtf8()
                         : "?";
    std::string m = fn + " failed: errno " + std::to_string(args[1].i);
    std::u16string u(m.begin(), m.end());
    VField* msg_f = FindInstanceField(self->klass, "detailMessage");
    if (msg_f != nullptr) {
      SetRefField(self, msg_f->offset,
                  reinterpret_cast<ObjHeader*>(state.linker->AllocString(u)));
    }
  }
  return Value{};
}

// ---- android.os.SystemClock ----

Value SCUptimeMillis(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.l = UptimeNowMs();
  return r;
}

Value SCElapsedRealtime(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  // Android elapsedRealtime ≈ 自开机（含深睡）；宿主墙钟为可观察投影
  r.l = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
  return r;
}

Value SCElapsedRealtimeNanos(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.l = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
  return r;
}

Value SCSleep(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int64_t ms = n >= 1 ? args[0].l : 0;
  if (ms < 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "sleep: negative");
    *ok = false;
    return Value{};
  }
  if (ms > 0) {
    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(ms / 1000);
    ts.tv_nsec = static_cast<long>((ms % 1000) * 1000000L);
    nanosleep(&ts, nullptr);
  }
  *ok = true;
  return Value{};
}


}  // namespace dexvm::runtime
