// M4.3 android.os 消息循环子系统冒烟（docs 04 章 14.10 / 10 章 19.1）：
// 纯内建引导（无需 runtime.dex）——Looper/Handler/MessageQueue/Message/
// HandlerThread/Process/SystemClock 语义面 + 消息泵（虚拟时间推进）：
//   消息 FIFO / 延迟出队（when 排序）/ dispatchMessage 顺序（callback ->
//   mCallback.handleMessage -> handleMessage）/ removeCallbacks(-AndMessages) /
//   quit vs quitSafely 语义 / HandlerThread 面 / Process·SystemClock 面 /
//   Message.obtain 全家桶 / enqueueMessage 拒绝无 target / loop() 泵出。
// 独立 TU（tools/looper_tests.sh 选择性编译，不动既有十套）。
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"
#include "memory/heap.h"

namespace {

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

using dexvm::runtime::ClassLinker;
using dexvm::runtime::ClassStatus;
using dexvm::runtime::IntrinsicFn;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::ToIntrinsicSlot;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

// 事件日志（测试内建面记录 dispatch 轨迹）
std::vector<std::string> g_events;

// Message 字段布局契约（core/framework_builtins.cc kMsg* 常量；8 字节槽）
constexpr uint32_t kMsgWhat = 0;
constexpr uint32_t kMsgObj = 24;
constexpr uint32_t kMsgTarget = 32;
constexpr uint32_t kMsgCallback = 40;
constexpr uint32_t kMsgWhen = 48;

ObjHeader* GetRef(VObject* o, uint32_t off) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}
void SetRef(VObject* o, uint32_t off, ObjHeader* v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}
int32_t GetI(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}
int64_t GetL(VObject* o, uint32_t off) {
  int64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return slot;
}
void SetI(VObject* o, uint32_t off, int32_t v) {
  uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
  std::memcpy(o->field_data() + off, &slot, sizeof(slot));
}

// ---- 测试内建面：TaggedRunnable / RecHandler / RecCallback ----

// run()：记录 "run:<tag>"
Value RecRun(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  VString* tag = reinterpret_cast<VString*>(GetRef(self, 0));
  g_events.push_back("run:" + (tag != nullptr ? tag->ToModifiedUtf8() : "?"));
  return Value{};
}

Value NoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// handleMessage：记录 "handle:<what>"
Value RecHandleMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    g_events.push_back(
        "handle:" + std::to_string(GetI(reinterpret_cast<VObject*>(args[0].ref), kMsgWhat)));
  }
  return Value{};
}

// Callback.handleMessage：记录 "cb:<what>"，返回 consume 字段
Value RecCallbackHandle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n >= 1 && args[0].ref != nullptr) {
    g_events.push_back(
        "cb:" + std::to_string(GetI(reinterpret_cast<VObject*>(args[0].ref), kMsgWhat)));
  }
  r.i = GetI(self, 0);  // consume
  return r;
}

void AddField(VClass* k, const char* name, const char* type, uint32_t off) {
  VField f;
  f.declaring = k;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = 0x2;
  f.offset = off;
  f.is_static = false;
  k->instance_fields.push_back(std::move(f));
}

void AddFn(VClass* k, const char* name, std::vector<std::string> params, const char* ret,
           uint32_t access, IntrinsicFn fn) {
  VMethod m;
  m.declaring = k;
  m.name = name;
  m.param_types = std::move(params);
  m.return_type = ret;
  std::string shorty;
  shorty.push_back(ret[0] == 'L' || ret[0] == '[' ? 'L' : ret[0]);
  for (const auto& p : m.param_types) {
    shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
  }
  m.shorty = shorty;
  m.access_flags = access;
  m.code = nullptr;
  m.native_fn = ToIntrinsicSlot(fn);
  k->direct_methods.push_back(std::move(m));
}

ClassLinker* g_linker = nullptr;
VMState* g_state = nullptr;

// 测试类登记（builtin 落点；记录 dispatch 轨迹）
void RegisterTestFaces(ClassLinker& linker) {
  VClass* object = linker.FindClass("Ljava/lang/Object;");
  VClass* class_class = linker.FindClass("Ljava/lang/Class;");
  VClass* handler = linker.FindClass("Landroid/os/Handler;");
  if (object == nullptr || class_class == nullptr || handler == nullptr) return;
  auto make = [&](const char* desc, VClass* super, uint32_t extra) {
    void* mem = linker.heap()->Alloc(sizeof(VClass));
    if (mem == nullptr) return static_cast<VClass*>(nullptr);
    VClass* k = new (mem) VClass();
    k->descriptor = desc;
    k->super_class = super;
    k->status = ClassStatus::kInitialized;
    k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + extra;
    k->klass = class_class;
    linker.RegisterBuiltinClass(k);
    return k;
  };
  const uint32_t ctor = 0x1 | 0x10000;  // public constructor
  const uint32_t pub = 0x1;
  // TaggedRunnable：tag@0 + run()V
  VClass* run_cls = make("Ltest/TaggedRunnable;", object, 8);
  if (run_cls != nullptr) {
    AddField(run_cls, "tag", "Ljava/lang/String;", 0);
    AddFn(run_cls, "<init>", {}, "V", ctor, NoOp);  // 测试直构（Alloc + 字段直写）
    AddFn(run_cls, "run", {}, "V", pub, RecRun);
  }
  // RecHandler：Handler 子类覆写 handleMessage
  VClass* h_cls = make("Ltest/RecHandler;", handler, 24);
  if (h_cls != nullptr) {
    AddFn(h_cls, "handleMessage", {"Landroid/os/Message;"}, "V", pub, RecHandleMessage);
  }
  // RecCallback：Handler$Callback 面（handleMessage -> Z；consume@0）
  VClass* c_cls = make("Ltest/RecCallback;", object, 8);
  if (c_cls != nullptr) {
    AddField(c_cls, "consume", "I", 0);
    AddFn(c_cls, "handleMessage", {"Landroid/os/Message;"}, "Z", pub, RecCallbackHandle);
  }
}

// ---- 驱动辅助 ----

VMethod* FindStatic(VClass* k, const char* name, std::vector<std::string> params, const char* ret) {
  for (auto& m : k->direct_methods) {
    if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) return &m;
  }
  return nullptr;
}

// 静态调用（成功 true）
bool CallStatic(const char* cls, const char* name, std::vector<std::string> params, const char* ret,
                const Value* args, size_t n, Value* out) {
  VClass* k = g_linker->FindClass(cls);
  VMethod* m = k != nullptr ? FindStatic(k, name, std::move(params), ret) : nullptr;
  if (m == nullptr) {
    std::printf("  (missing static %s.%s)\n", cls, name);
    return false;
  }
  Value r{};
  if (!InvokeMethod(*g_state, m, args, n, &r)) {
    g_state->ClearException();
    return false;
  }
  if (out != nullptr) *out = r;
  return true;
}

// 实例调用（成功 true）
bool CallVirtualOn(VObject* obj, const char* name, std::vector<std::string> params, const char* ret,
                   const Value* args, size_t n, Value* out = nullptr) {
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    std::printf("  (missing virtual %s on %s)\n", name, obj->klass->descriptor.c_str());
    return false;
  }
  std::vector<Value> full;
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!InvokeMethod(*g_state, m, full.data(), full.size(), &r)) {
    g_state->ClearException();
    return false;
  }
  if (out != nullptr) *out = r;
  return true;
}

VObject* Alloc(VClass* k) { return g_linker->AllocObject(k); }

VObject* MakeRunnable(const char16_t* tag) {
  VObject* r = Alloc(g_linker->FindClass("Ltest/TaggedRunnable;"));
  if (r != nullptr) SetRef(r, 0, reinterpret_cast<ObjHeader*>(g_linker->AllocString(tag)));
  return r;
}

VObject* MakeHandler(VObject* looper, VObject* callback = nullptr) {
  VClass* hk = g_linker->FindClass("Ltest/RecHandler;");
  VObject* h = hk != nullptr ? Alloc(hk) : nullptr;
  if (h == nullptr) return nullptr;
  Value args[2] = {};
  args[0].ref = looper;
  args[1].ref = callback;
  if (callback != nullptr) {
    CallVirtualOn(h, "<init>", {"Landroid/os/Looper;", "Landroid/os/Handler$Callback;"}, "V", args,
                  2);
  } else {
    CallVirtualOn(h, "<init>", {"Landroid/os/Looper;"}, "V", args, 1);
  }
  return h;
}

VObject* MakeCallback(int32_t consume) {
  VObject* c = Alloc(g_linker->FindClass("Ltest/RecCallback;"));
  if (c != nullptr) SetI(c, 0, consume);
  return c;
}

bool EventsEq(const std::vector<std::string>& want) {
  if (g_events.size() != want.size()) {
    std::printf("  (events: got %zu want %zu:", g_events.size(), want.size());
    for (const auto& e : g_events) std::printf(" %s", e.c_str());
    std::printf(")\n");
    return false;
  }
  for (size_t i = 0; i < want.size(); i++) {
    if (g_events[i] != want[i]) {
      std::printf("  (events[%zu]: got %s want %s)\n", i, g_events[i].c_str(), want[i].c_str());
      return false;
    }
  }
  return true;
}

void Pump(const char* what) {
  std::string err;
  if (!dexvm::runtime::PumpAllLooperQueues(*g_state, &err)) {
    std::printf("  (pump failed during %s: %s)\n", what, err.c_str());
    g_events.push_back("pump-fail");
  }
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  std::printf("== looper smoke (M4.3 android.os 消息循环子系统) ==\n");

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;
  g_linker = &linker;
  g_state = &state;
  RegisterTestFaces(linker);

  std::printf("\n-- 0. test faces registered --\n");
  CHECK(linker.FindClass("Ltest/TaggedRunnable;") != nullptr, "TaggedRunnable registered");
  CHECK(linker.FindClass("Ltest/RecHandler;") != nullptr, "RecHandler registered");
  CHECK(linker.FindClass("Ltest/RecCallback;") != nullptr, "RecCallback registered");

  // ---- 1. Looper/Process/SystemClock 面 ----
  std::printf("\n-- 1. Looper / Process / SystemClock faces --\n");
  Value ml{};
  CHECK(CallStatic("Landroid/os/Looper;", "getMainLooper", {}, "Landroid/os/Looper;", nullptr, 0,
                   &ml) &&
            ml.ref != nullptr,
        "Looper.getMainLooper() != null");
  VObject* main_looper = reinterpret_cast<VObject*>(ml.ref);
  Value my{};
  CHECK(CallStatic("Landroid/os/Looper;", "myLooper", {}, "Landroid/os/Looper;", nullptr, 0, &my) &&
            my.ref == ml.ref,
        "Looper.myLooper() == getMainLooper()（主线程绑定）");
  Value thr{};
  CHECK(CallVirtualOn(main_looper, "getThread", {}, "Ljava/lang/Thread;", nullptr, 0, &thr) &&
            thr.ref != nullptr,
        "Looper.getThread() != null");
  Value cur{};
  CHECK(CallStatic("Ljava/lang/Thread;", "currentThread", {}, "Ljava/lang/Thread;", nullptr, 0,
                   &cur) &&
            cur.ref == thr.ref,
        "Looper.getThread() == Thread.currentThread()");
  Value pid{};
  CHECK(CallStatic("Landroid/os/Process;", "myPid", {}, "I", nullptr, 0, &pid) && pid.i > 0,
        "Process.myPid() > 0");
  Value tid{};
  CHECK(CallStatic("Landroid/os/Process;", "myTid", {}, "I", nullptr, 0, &tid) && tid.i > 0,
        "Process.myTid() > 0");
  Value uid{};
  CHECK(CallStatic("Landroid/os/Process;", "myUid", {}, "I", nullptr, 0, &uid) && uid.i >= 0,
        "Process.myUid() >= 0");
  Value uh{};
  CHECK(CallStatic("Landroid/os/Process;", "myUserHandle", {}, "Landroid/os/UserHandle;", nullptr,
                   0, &uh) &&
            uh.ref != nullptr,
        "Process.myUserHandle() != null");
  Value up1{}, up2{};
  CHECK(CallStatic("Landroid/os/SystemClock;", "uptimeMillis", {}, "J", nullptr, 0, &up1),
        "SystemClock.uptimeMillis() 调用");
  Value er{};
  CHECK(CallStatic("Landroid/os/SystemClock;", "elapsedRealtime", {}, "J", nullptr, 0, &er) &&
            er.l > 0,
        "SystemClock.elapsedRealtime() > 0");
  Value slp_arg{};
  slp_arg.l = 1;
  CHECK(CallStatic("Landroid/os/SystemClock;", "sleep", {"J"}, "V", &slp_arg, 1, nullptr),
        "SystemClock.sleep(1)");
  CHECK(CallStatic("Landroid/os/SystemClock;", "uptimeMillis", {}, "J", nullptr, 0, &up2) &&
            up2.l >= up1.l,
        "uptimeMillis 单调");

  // ---- 2. 消息 FIFO ----
  std::printf("\n-- 2. message FIFO --\n");
  VObject* h = MakeHandler(main_looper);
  CHECK(h != nullptr, "new Handler(mainLooper)");
  g_events.clear();
  Value rv{};
  rv.ref = MakeRunnable(u"a");
  CHECK(CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &rv, 1), "post(a)");
  rv.ref = MakeRunnable(u"b");
  CHECK(CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &rv, 1), "post(b)");
  rv.ref = MakeRunnable(u"c");
  CHECK(CallVirtualOn(h, "postAtFrontOfQueue", {"Ljava/lang/Runnable;"}, "Z", &rv, 1),
        "postAtFrontOfQueue(c)");
  Pump("fifo");
  CHECK(EventsEq({"run:c", "run:a", "run:b"}), "FIFO：post 顺序 + front-of-queue 队头");

  // ---- 3. 延迟出队（when 排序；虚拟时间推进必须出队）----
  std::printf("\n-- 3. delayed dequeue (when ordering) --\n");
  g_events.clear();
  {
    Value args2[2] = {};
    args2[0].ref = MakeRunnable(u"d50");
    args2[1].l = 50;
    CHECK(CallVirtualOn(h, "postDelayed", {"Ljava/lang/Runnable;", "J"}, "Z", args2, 2),
          "postDelayed(d50, 50)");
    args2[0].ref = MakeRunnable(u"d10");
    args2[1].l = 10;
    CHECK(CallVirtualOn(h, "postDelayed", {"Ljava/lang/Runnable;", "J"}, "Z", args2, 2),
          "postDelayed(d10, 10)");
    args2[0].ref = MakeRunnable(u"now");
    args2[1].l = 0;
    CHECK(CallVirtualOn(h, "postDelayed", {"Ljava/lang/Runnable;", "J"}, "Z", args2, 2),
          "postDelayed(now, 0)");
  }
  Pump("delayed");
  CHECK(g_events.size() == 3 && g_events[0] == "run:now" && g_events[1] == "run:d10" &&
            g_events[2] == "run:d50",
        "when 升序出队：now(0) < d10(10) < d50(50)（延迟消息 pump 必达）");

  // ---- 4. dispatchMessage 顺序 ----
  std::printf("\n-- 4. dispatchMessage order --\n");
  // 4a：msg.callback 优先（不经 handleMessage）
  g_events.clear();
  {
    Value args[2] = {};
    args[0].ref = h;
    args[1].ref = MakeRunnable(u"cb");
    Value msg{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "Ljava/lang/Runnable;"}, "Landroid/os/Message;",
                     args, 2, &msg) &&
              msg.ref != nullptr,
          "Message.obtain(handler, runnable)");
    Value dargs[1] = {};
    dargs[0] = msg;
    CHECK(CallVirtualOn(h, "dispatchMessage", {"Landroid/os/Message;"}, "V", dargs, 1),
          "dispatchMessage(callback msg)");
    CHECK(EventsEq({"run:cb"}), "callback 优先：只 run，不 handleMessage");
  }
  // 4b：mCallback.handleMessage 返回 false -> 继续 handleMessage
  g_events.clear();
  {
    VObject* cb = MakeCallback(0);
    VObject* h2 = MakeHandler(main_looper, cb);
    CHECK(h2 != nullptr, "new Handler(mainLooper, callback(consume=0))");
    Value args[1] = {};
    args[0].i = 7;
    CHECK(CallVirtualOn(h2, "sendEmptyMessage", {"I"}, "Z", args, 1), "sendEmptyMessage(7)");
    Pump("dispatch b");
    CHECK(EventsEq({"cb:7", "handle:7"}), "mCallback 返回 false：cb 后仍 handleMessage");
  }
  // 4c：mCallback.handleMessage 返回 true -> 消费
  g_events.clear();
  {
    VObject* cb = MakeCallback(1);
    VObject* h3 = MakeHandler(main_looper, cb);
    Value args[1] = {};
    args[0].i = 9;
    CHECK(CallVirtualOn(h3, "sendEmptyMessage", {"I"}, "Z", args, 1), "sendEmptyMessage(9)");
    Pump("dispatch c");
    CHECK(EventsEq({"cb:9"}), "mCallback 返回 true：消费即止");
  }
  // 4d：无 callback：handleMessage
  g_events.clear();
  {
    Value args[1] = {};
    args[0].i = 3;
    CHECK(CallVirtualOn(h, "sendEmptyMessage", {"I"}, "Z", args, 1), "sendEmptyMessage(3)");
    Pump("dispatch d");
    CHECK(EventsEq({"handle:3"}), "无 callback：handleMessage");
  }

  // ---- 5. removeCallbacks / removeCallbacksAndMessages / removeMessages ----
  std::printf("\n-- 5. remove semantics --\n");
  g_events.clear();
  {
    VObject* r1 = MakeRunnable(u"r1");
    VObject* r2 = MakeRunnable(u"r2");
    Value a1{};
    a1.ref = r1;
    CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    a1.ref = r2;
    CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    a1.ref = r1;
    CHECK(CallVirtualOn(h, "removeCallbacks", {"Ljava/lang/Runnable;"}, "V", &a1, 1),
          "removeCallbacks(r1)");
    Pump("remove");
    CHECK(EventsEq({"run:r2"}), "removeCallbacks：r1 摘除，r2 执行");
  }
  g_events.clear();
  {
    // token 语义：postDelayed(R, token, 0) 的消息 obj==token
    VObject* token = reinterpret_cast<VObject*>(g_linker->AllocString(u"tok"));
    VObject* rt = MakeRunnable(u"rt");
    VObject* ro = MakeRunnable(u"ro");
    Value args[3] = {};
    args[0].ref = rt;
    args[1].ref = token;
    args[2].l = 0;
    CHECK(CallVirtualOn(h, "postDelayed", {"Ljava/lang/Runnable;", "Ljava/lang/Object;", "J"}, "Z",
                        args, 3),
          "postDelayed(rt, token, 0)");
    Value a1{};
    a1.ref = ro;
    CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    Value tok_arg{};
    tok_arg.ref = token;
    CHECK(CallVirtualOn(h, "removeCallbacksAndMessages", {"Ljava/lang/Object;"}, "V", &tok_arg, 1),
          "removeCallbacksAndMessages(token)");
    Pump("remove token");
    CHECK(EventsEq({"run:ro"}), "token 摘除：rt 移除，ro 执行");
  }
  g_events.clear();
  {
    // token==null：移除本 Handler 全部消息
    VObject* ra = MakeRunnable(u"ra");
    VObject* rb = MakeRunnable(u"rb");
    Value a1{};
    a1.ref = ra;
    CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    a1.ref = rb;
    CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    Value null_arg{};
    CHECK(CallVirtualOn(h, "removeCallbacksAndMessages", {"Ljava/lang/Object;"}, "V", &null_arg, 1),
          "removeCallbacksAndMessages(null)");
    Pump("remove all");
    CHECK(g_events.empty(), "removeCallbacksAndMessages(null)：全部移除");
  }
  g_events.clear();
  {
    Value a1{};
    a1.i = 5;
    CallVirtualOn(h, "sendEmptyMessage", {"I"}, "Z", &a1, 1);
    CallVirtualOn(h, "sendEmptyMessage", {"I"}, "Z", &a1, 1);
    a1.i = 6;
    CallVirtualOn(h, "sendEmptyMessage", {"I"}, "Z", &a1, 1);
    a1.i = 5;
    Value has{};
    CHECK(CallVirtualOn(h, "hasMessages", {"I"}, "Z", &a1, 1, &has) && has.i == 1,
          "hasMessages(5) == true");
    CHECK(CallVirtualOn(h, "removeMessages", {"I"}, "V", &a1, 1), "removeMessages(5)");
    Pump("remove what");
    CHECK(EventsEq({"handle:6"}), "removeMessages(what)：只余 handle:6");
  }

  // ---- 6. quit vs quitSafely ----
  std::printf("\n-- 6. quit semantics --\n");
  {
    // quit()：清空全部（即时+延迟均不出队）
    VClass* ht_cls = linker.FindClass("Landroid/os/HandlerThread;");
    VObject* ht = ht_cls != nullptr ? Alloc(ht_cls) : nullptr;
    Value narg{};
    narg.ref = linker.AllocString(u"ht1");
    CHECK(ht != nullptr && CallVirtualOn(ht, "<init>", {"Ljava/lang/String;"}, "V", &narg, 1),
          "new HandlerThread(ht1)");
    CHECK(CallVirtualOn(ht, "start", {}, "V", nullptr, 0), "ht1.start()");
    Value lop{};
    CHECK(CallVirtualOn(ht, "getLooper", {}, "Landroid/os/Looper;", nullptr, 0, &lop) &&
              lop.ref != nullptr,
          "ht1.getLooper() != null");
    VObject* hh = MakeHandler(reinterpret_cast<VObject*>(lop.ref));
    g_events.clear();
    Value a1{};
    a1.ref = MakeRunnable(u"q1");
    CallVirtualOn(hh, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(lop.ref), "quit", {}, "V", nullptr, 0),
          "looper.quit()");
    Pump("quit");
    CHECK(g_events.empty(), "quit()：消息清空不出队");
    Value n3{};
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(lop.ref), "getQueue", {},
                        "Landroid/os/MessageQueue;", nullptr, 0, &n3) &&
              CallVirtualOn(reinterpret_cast<VObject*>(n3.ref), "next", {}, "Landroid/os/Message;",
                            nullptr, 0, &n3) &&
              n3.ref == nullptr,
          "quit 后 next() 返回 null（loop 退出语义）");
  }
  {
    // quitSafely()：保留已到期、移除未来
    VClass* ht_cls = linker.FindClass("Landroid/os/HandlerThread;");
    VObject* ht = ht_cls != nullptr ? Alloc(ht_cls) : nullptr;
    Value narg{};
    narg.ref = linker.AllocString(u"ht2");
    CallVirtualOn(ht, "<init>", {"Ljava/lang/String;"}, "V", &narg, 1);
    CallVirtualOn(ht, "start", {}, "V", nullptr, 0);
    Value lop{};
    CallVirtualOn(ht, "getLooper", {}, "Landroid/os/Looper;", nullptr, 0, &lop);
    VObject* hh = MakeHandler(reinterpret_cast<VObject*>(lop.ref));
    g_events.clear();
    Value a1{};
    a1.ref = MakeRunnable(u"due");
    CallVirtualOn(hh, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    Value args2[2] = {};
    args2[0].ref = MakeRunnable(u"future");
    args2[1].l = 3600000;  // 1h 后：quitSafely 应移除
    CallVirtualOn(hh, "postDelayed", {"Ljava/lang/Runnable;", "J"}, "Z", args2, 2);
    Value qret{};
    CHECK(CallVirtualOn(ht, "quitSafely", {}, "Z", nullptr, 0, &qret) && qret.i == 1,
          "ht2.quitSafely() == true");
    Pump("quitSafely");
    CHECK(EventsEq({"run:due"}), "quitSafely()：到期执行、未来移除");
  }

  // ---- 7. HandlerThread 面 ----
  std::printf("\n-- 7. HandlerThread face --\n");
  {
    VClass* ht_cls = linker.FindClass("Landroid/os/HandlerThread;");
    VObject* ht = ht_cls != nullptr ? Alloc(ht_cls) : nullptr;
    Value narg{};
    narg.ref = linker.AllocString(u"worker");
    CHECK(ht != nullptr && CallVirtualOn(ht, "<init>", {"Ljava/lang/String;"}, "V", &narg, 1),
          "new HandlerThread(worker)");
    CHECK(CallVirtualOn(ht, "start", {}, "V", nullptr, 0), "worker.start()");
    Value lop{};
    CHECK(CallVirtualOn(ht, "getLooper", {}, "Landroid/os/Looper;", nullptr, 0, &lop) &&
              lop.ref != nullptr,
          "worker.getLooper() != null");
    Value lthread{};
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(lop.ref), "getThread", {}, "Ljava/lang/Thread;",
                        nullptr, 0, &lthread) &&
              lthread.ref == ht,
          "looper.getThread() == HandlerThread 实例");
    CHECK(my.ref != lop.ref, "myLooper() != worker looper（一线程一 Looper）");
    // Handler() 默认绑定创建线程（主线程）的 Looper——身份查找路径（动态线程留门）
    {
      VClass* hk = linker.FindClass("Ltest/RecHandler;");
      VObject* h_def = hk != nullptr ? Alloc(hk) : nullptr;
      CHECK(h_def != nullptr && CallVirtualOn(h_def, "<init>", {}, "V", nullptr, 0),
            "new Handler()（无参）");
      Value lop_def{};
      CHECK(CallVirtualOn(h_def, "getLooper", {}, "Landroid/os/Looper;", nullptr, 0, &lop_def) &&
                lop_def.ref == my.ref,
            "Handler() 默认绑定 = 创建线程 myLooper()（身份键）");
    }
    VObject* hh = MakeHandler(reinterpret_cast<VObject*>(lop.ref));
    g_events.clear();
    Value a1{};
    a1.ref = MakeRunnable(u"w");
    CHECK(CallVirtualOn(hh, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1),
          "post 到 worker handler");
    Pump("worker");
    CHECK(EventsEq({"run:w"}), "HandlerThread 队列由消息泵执行");
  }

  // ---- 8. Message 面 ----
  std::printf("\n-- 8. Message face --\n");
  {
    Value msg{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0,
                     &msg) &&
              msg.ref != nullptr,
          "Message.obtain()");
    VObject* m = reinterpret_cast<VObject*>(msg.ref);
    CHECK(GetI(m, kMsgWhat) == 0 && GetRef(m, kMsgTarget) == nullptr && GetL(m, kMsgWhen) == 0,
          "obtain()：字段清零");
    Value args[3] = {};
    args[0].ref = h;
    args[1].i = 11;
    args[2].ref = linker.AllocString(u"objx");
    Value msg2{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "I", "Ljava/lang/Object;"}, "Landroid/os/Message;",
                     args, 3, &msg2) &&
              msg2.ref != nullptr,
          "Message.obtain(h, 11, obj)");
    VObject* m2 = reinterpret_cast<VObject*>(msg2.ref);
    CHECK(GetI(m2, kMsgWhat) == 11 && GetRef(m2, kMsgTarget) == h &&
              GetRef(m2, kMsgObj) == args[2].ref,
          "obtain(h, what, obj)：what/target/obj 就位");
    g_events.clear();
    CHECK(CallVirtualOn(m2, "sendToTarget", {}, "V", nullptr, 0), "msg.sendToTarget()");
    Pump("sendToTarget");
    CHECK(EventsEq({"handle:11"}), "sendToTarget -> handleMessage(11)");
    // obtain(Handler,Runnable) + getTarget
    Value ar[2] = {};
    ar[0].ref = h;
    ar[1].ref = MakeRunnable(u"ob");
    Value msg3{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "Ljava/lang/Runnable;"}, "Landroid/os/Message;", ar,
                     2, &msg3) &&
              msg3.ref != nullptr,
          "Message.obtain(h, runnable)");
    Value tgt{};
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(msg3.ref), "getTarget", {},
                        "Landroid/os/Handler;", nullptr, 0, &tgt) &&
              tgt.ref == h,
          "msg.getTarget() == handler");
    // recycle 清字段
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(msg3.ref), "recycle", {}, "V", nullptr, 0),
          "msg.recycle()");
    CHECK(GetRef(reinterpret_cast<VObject*>(msg3.ref), kMsgTarget) == nullptr &&
              GetRef(reinterpret_cast<VObject*>(msg3.ref), kMsgCallback) == nullptr,
          "recycle()：字段清零");
  }

  // ---- 9. MessageQueue 面 ----
  std::printf("\n-- 9. MessageQueue face --\n");
  {
    VClass* mq_cls = linker.FindClass("Landroid/os/MessageQueue;");
    VObject* q = mq_cls != nullptr ? Alloc(mq_cls) : nullptr;
    CHECK(q != nullptr, "new MessageQueue()");
    // 无 target 拒绝（IllegalArgumentException）
    Value msg{};
    CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &msg);
    Value eargs[2] = {};
    eargs[0] = msg;
    eargs[1].l = 0;
    CHECK(!CallVirtualOn(q, "enqueueMessage", {"Landroid/os/Message;", "J"}, "Z", eargs, 2),
          "enqueueMessage 无 target：抛 IllegalArgumentException");
    // 手工入队排序：when=100 先于 when=200 出队
    Value hargs[1] = {};
    hargs[0].ref = h;
    Value m_late{}, m_early{};
    CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Handler;"}, "Landroid/os/Message;",
               hargs, 1, &m_late);
    CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Handler;"}, "Landroid/os/Message;",
               hargs, 1, &m_early);
    eargs[0] = m_late;
    eargs[1].l = 200;
    CHECK(CallVirtualOn(q, "enqueueMessage", {"Landroid/os/Message;", "J"}, "Z", eargs, 2),
          "enqueueMessage(late, 200)");
    eargs[0] = m_early;
    eargs[1].l = 100;
    CHECK(CallVirtualOn(q, "enqueueMessage", {"Landroid/os/Message;", "J"}, "Z", eargs, 2),
          "enqueueMessage(early, 100)");
    Value n1{};
    CHECK(CallVirtualOn(q, "next", {}, "Landroid/os/Message;", nullptr, 0, &n1) &&
              n1.ref == m_early.ref,
          "next()：when 升序（early 先）");
    Value n2{};
    CHECK(CallVirtualOn(q, "next", {}, "Landroid/os/Message;", nullptr, 0, &n2) &&
              n2.ref == m_late.ref,
          "next()：late 随后");
    Value n3{};
    CHECK(CallVirtualOn(q, "next", {}, "Landroid/os/Message;", nullptr, 0, &n3) &&
              n3.ref == nullptr,
          "next()：空队列返回 null（单线程投影）");
  }

  // ---- 10. prepare 一线程一 Looper / loop() 泵出 ----
  std::printf("\n-- 10. prepare / loop --\n");
  {
    // 当前线程已有（自动准备的）主 Looper：prepare 应抛 RuntimeException
    CHECK(!CallStatic("Landroid/os/Looper;", "prepare", {}, "V", nullptr, 0, nullptr),
          "Looper.prepare() 重复：抛 RuntimeException");
    // loop()：泵出已投递消息后退出（空队列即出，单线程投影）
    g_events.clear();
    Value a1{};
    a1.ref = MakeRunnable(u"L1");
    CallVirtualOn(h, "post", {"Ljava/lang/Runnable;"}, "Z", &a1, 1);
    CHECK(CallStatic("Landroid/os/Looper;", "loop", {}, "V", nullptr, 0, nullptr),
          "Looper.loop()");
    CHECK(EventsEq({"run:L1"}), "loop()：出队 dispatch 后退出");
    g_events.clear();
    CHECK(CallStatic("Landroid/os/Looper;", "loop", {}, "V", nullptr, 0, nullptr),
          "Looper.loop() 空转");
    CHECK(g_events.empty(), "loop() 空队列无 dispatch");
  }

  std::printf("\n== looper smoke: %s (%d failures) ==\n", g_failures == 0 ? "PASS" : "FAIL",
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
