// Handler.obtainMessage 全家族 + Message 面（obtain 全重载/recycle 复用语义）冒烟
// （独立 TU，tools/handler_msg_tests.sh 选择性编译；不动既有 looper_smoke 断言）。
// 覆盖面（对照 AOSP android-37 Message/Handler 公开源语义）：
//   obtainMessage()/(I)/(I,Object)/(I,I,I)/(I,I,Object)/(I,I,I,Object) 全家族 +
//   target 正确（target=handler 预置）+ 复用语义（recycle 收池/obtain 复用同一实例、
//   dispatch 后 Looper 自动收池、在用 recycle/重发抛 IllegalStateException）
//   + Message.obtain 全重载（含 obtain(Message) 值拷贝）+ copyFrom/getWhen/getCallback
//   + isAsynchronous/setAsynchronous。
// 纯内建引导（无需 runtime.dex）。
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
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
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;

// Message 字段布局契约（core/framework_builtins.cc kMsg* 常量；8 字节槽）
constexpr uint32_t kMsgWhat = 0;
constexpr uint32_t kMsgArg1 = 8;
constexpr uint32_t kMsgArg2 = 16;
constexpr uint32_t kMsgObj = 24;
constexpr uint32_t kMsgTarget = 32;
constexpr uint32_t kMsgCallback = 40;
constexpr uint32_t kMsgWhen = 48;
constexpr uint32_t kMsgFlags = 64;
constexpr int32_t kFlagInUse = 1;

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
void SetL(VObject* o, uint32_t off, int64_t v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

ClassLinker* g_linker = nullptr;
VMState* g_state = nullptr;

// ---- 驱动辅助 ----

VMethod* FindStatic(VClass* k, const char* name, std::vector<std::string> params, const char* ret) {
  for (auto& m : k->direct_methods) {
    if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) return &m;
  }
  return nullptr;
}

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

// 期望抛出指定 guest 异常类（返回 true = 抛出且类型匹配）
bool CallVirtualExpectEx(VObject* obj, const char* name, std::vector<std::string> params,
                         const char* ret, const Value* args, size_t n, const char* ex_desc) {
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
  if (InvokeMethod(*g_state, m, full.data(), full.size(), &r)) return false;  // 未抛出
  ObjHeader* ex = g_state->pending_exception;
  bool match = ex != nullptr && ex->klass != nullptr && ex->klass->descriptor == ex_desc;
  if (!match) {
    std::printf("  (got %s want %s)\n",
                ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor.c_str() : "(none)",
                ex_desc);
  }
  g_state->ClearException();
  return match;
}

VObject* Alloc(VClass* k) { return g_linker->AllocObject(k); }

VObject* MakeHandler(VObject* looper) {
  VObject* h = Alloc(g_linker->FindClass("Landroid/os/Handler;"));
  if (h == nullptr) return nullptr;
  Value arg{};
  arg.ref = looper;
  if (!CallVirtualOn(h, "<init>", {"Landroid/os/Looper;"}, "V", &arg, 1)) return nullptr;
  return h;
}

void Pump() {
  std::string err;
  if (!dexvm::runtime::PumpAllLooperQueues(*g_state, &err)) {
    std::printf("  (pump failed: %s)\n", err.c_str());
    g_failures++;
  }
}

// ---- Bundle 桩面（lib 缝验证：getData 懒建/obtain(Message) data 深拷贝走
// BundleNewCore -> FindClass("Landroid/os/Bundle;")；纯内建引导无 runtime.dex，
// 以同名桩类承接转调）----
Value NoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

void AddFn(VClass* k, const char* name, std::vector<std::string> params, const char* ret,
           uint32_t access, dexvm::runtime::IntrinsicFn fn) {
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
  m.native_fn = dexvm::runtime::ToIntrinsicSlot(fn);
  k->direct_methods.push_back(std::move(m));
}

void RegisterBundleStub(ClassLinker& linker) {
  VClass* object = linker.FindClass("Ljava/lang/Object;");
  VClass* class_class = linker.FindClass("Ljava/lang/Class;");
  if (object == nullptr || class_class == nullptr) return;
  void* mem = linker.heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return;
  VClass* k = new (mem) VClass();
  k->descriptor = "Landroid/os/Bundle;";
  k->super_class = object;
  k->status = ClassStatus::kInitialized;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + 8;
  k->klass = class_class;
  linker.RegisterBuiltinClass(k);
  const uint32_t ctor = 0x1 | 0x10000;  // public constructor
  AddFn(k, "<init>", {}, "V", ctor, NoOp);
  AddFn(k, "<init>", {"Landroid/os/Bundle;"}, "V", ctor, NoOp);
}

}  // namespace

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  std::printf("== handler/msg smoke (obtainMessage 全家族 / Message.obtain 全重载 / 复用语义) ==\n");

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);
  RegisterBundleStub(linker);  // lib Bundle 桩（RegisterFrameworkBuiltins 断言后登记，不入 def 面）
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;
  g_linker = &linker;
  g_state = &state;

  Value ml{};
  CHECK(CallStatic("Landroid/os/Looper;", "getMainLooper", {}, "Landroid/os/Looper;", nullptr, 0,
                   &ml) &&
            ml.ref != nullptr,
        "Looper.getMainLooper() 就绪");
  VObject* h = MakeHandler(reinterpret_cast<VObject*>(ml.ref));
  CHECK(h != nullptr, "new Handler(mainLooper)");

  // ---- 1. Handler.obtainMessage 全家族（target=handler 预置）----
  std::printf("\n-- 1. Handler.obtainMessage family --\n");
  {
    Value m{};
    CHECK(CallVirtualOn(h, "obtainMessage", {}, "Landroid/os/Message;", nullptr, 0, &m) &&
              m.ref != nullptr,
          "obtainMessage()");
    VObject* msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h) && GetI(msg, kMsgWhat) == 0 &&
              GetRef(msg, kMsgObj) == nullptr && GetL(msg, kMsgWhen) == 0,
          "obtainMessage()：target=handler，字段清零");

    Value a1{};
    a1.i = 7;
    CHECK(CallVirtualOn(h, "obtainMessage", {"I"}, "Landroid/os/Message;", &a1, 1, &m) &&
              m.ref != nullptr,
          "obtainMessage(7)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 7 && GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h) &&
              GetI(msg, kMsgArg1) == 0 && GetI(msg, kMsgArg2) == 0,
          "obtainMessage(what)：what/target 就位");

    Value a2[2] = {};
    a2[0].i = 7;
    a2[1].ref = g_linker->AllocString(u"o1");
    CHECK(CallVirtualOn(h, "obtainMessage", {"I", "Ljava/lang/Object;"}, "Landroid/os/Message;", a2,
                        2, &m) &&
              m.ref != nullptr,
          "obtainMessage(7, obj)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 7 && GetRef(msg, kMsgObj) == a2[1].ref &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtainMessage(what, obj)：what/obj/target 就位");

    Value a3[3] = {};
    a3[0].i = 7;
    a3[1].i = 1;
    a3[2].i = 2;
    CHECK(CallVirtualOn(h, "obtainMessage", {"I", "I", "I"}, "Landroid/os/Message;", a3, 3, &m) &&
              m.ref != nullptr,
          "obtainMessage(7, 1, 2)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 7 && GetI(msg, kMsgArg1) == 1 && GetI(msg, kMsgArg2) == 2 &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtainMessage(what, arg1, arg2)：what/arg1/arg2/target 就位");

    // 任务面补全（AOSP 公开面无 (I,I,Object)；what/arg1/obj 语义，arg2=0）
    Value a4[3] = {};
    a4[0].i = 7;
    a4[1].i = 1;
    a4[2].ref = g_linker->AllocString(u"o2");
    CHECK(CallVirtualOn(h, "obtainMessage", {"I", "I", "Ljava/lang/Object;"},
                        "Landroid/os/Message;", a4, 3, &m) &&
              m.ref != nullptr,
          "obtainMessage(7, 1, obj)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 7 && GetI(msg, kMsgArg1) == 1 && GetI(msg, kMsgArg2) == 0 &&
              GetRef(msg, kMsgObj) == a4[2].ref &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtainMessage(what, arg1, obj)：what/arg1/obj/target 就位（arg2=0）");

    Value a5[4] = {};
    a5[0].i = 7;
    a5[1].i = 1;
    a5[2].i = 2;
    a5[3].ref = g_linker->AllocString(u"o3");
    CHECK(CallVirtualOn(h, "obtainMessage", {"I", "I", "I", "Ljava/lang/Object;"},
                        "Landroid/os/Message;", a5, 4, &m) &&
              m.ref != nullptr,
          "obtainMessage(7, 1, 2, obj)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 7 && GetI(msg, kMsgArg1) == 1 && GetI(msg, kMsgArg2) == 2 &&
              GetRef(msg, kMsgObj) == a5[3].ref &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtainMessage(what, arg1, arg2, obj)：全字段 + target 就位");

    // target 正确（getTarget 面）
    Value tgt{};
    CHECK(CallVirtualOn(msg, "getTarget", {}, "Landroid/os/Handler;", nullptr, 0, &tgt) &&
              tgt.ref == reinterpret_cast<ObjHeader*>(h),
          "msg.getTarget() == handler（target 正确）");
  }

  // ---- 2. Message.obtain 全重载（AOSP 8 个）----
  std::printf("\n-- 2. Message.obtain overloads --\n");
  {
    Value m{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m) &&
              m.ref != nullptr,
          "Message.obtain()");
    VObject* msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 0 && GetRef(msg, kMsgTarget) == nullptr &&
              GetRef(msg, kMsgCallback) == nullptr && GetL(msg, kMsgWhen) == 0 &&
              (GetI(msg, kMsgFlags) & kFlagInUse) == 0,
          "obtain()：字段清零、非 in-use");

    // obtain(Message orig)：值拷贝 what/arg1/arg2/obj/target/callback（when 不拷）
    Value a5[5] = {};
    a5[0].ref = h;
    a5[1].i = 5;
    a5[2].i = 11;
    a5[3].i = 22;
    a5[4].ref = g_linker->AllocString(u"ox");
    Value orig{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "I", "I", "I", "Ljava/lang/Object;"},
                     "Landroid/os/Message;", a5, 5, &orig) &&
              orig.ref != nullptr,
          "Message.obtain(h, 5, 11, 22, ox)");
    VObject* o = reinterpret_cast<VObject*>(orig.ref);
    // callback 槽直写（值拷贝断言不涉 dispatch；Runnable 面用 Object 占位）
    ObjHeader* cbx = reinterpret_cast<ObjHeader*>(Alloc(g_linker->FindClass("Ljava/lang/Object;")));
    SetRef(o, kMsgCallback, cbx);
    SetL(o, kMsgWhen, 999);  // when 不应被拷贝
    Value om[1] = {};
    om[0] = orig;
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Message;"},
                     "Landroid/os/Message;", om, 1, &m) &&
              m.ref != nullptr,
          "Message.obtain(orig)");
    VObject* c = reinterpret_cast<VObject*>(m.ref);
    CHECK(c != o && GetI(c, kMsgWhat) == 5 && GetI(c, kMsgArg1) == 11 && GetI(c, kMsgArg2) == 22 &&
              GetRef(c, kMsgObj) == a5[4].ref && GetRef(c, kMsgTarget) == reinterpret_cast<ObjHeader*>(h) &&
              GetRef(c, kMsgCallback) == cbx,
          "obtain(orig)：what/arg1/arg2/obj/target/callback 值拷贝");
    CHECK(GetL(c, kMsgWhen) == 0, "obtain(orig)：when 不拷贝");

    Value h1[1] = {};
    h1[0].ref = h;
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Handler;"},
                     "Landroid/os/Message;", h1, 1, &m) &&
              m.ref != nullptr,
          "Message.obtain(h)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h) && GetI(msg, kMsgWhat) == 0,
          "obtain(h)：仅 target");

    Value h2[2] = {};
    h2[0].ref = h;
    h2[1].ref = Alloc(g_linker->FindClass("Ljava/lang/Object;"));  // Runnable 槽占位
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "Ljava/lang/Runnable;"}, "Landroid/os/Message;", h2,
                     2, &m) &&
              m.ref != nullptr,
          "Message.obtain(h, runnable)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h) &&
              GetRef(msg, kMsgCallback) == h2[1].ref,
          "obtain(h, runnable)：target/callback");

    Value h3[2] = {};
    h3[0].ref = h;
    h3[1].i = 9;
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Handler;", "I"},
                     "Landroid/os/Message;", h3, 2, &m) &&
              m.ref != nullptr,
          "Message.obtain(h, 9)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 9 && GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtain(h, what)：what/target");

    Value h4[3] = {};
    h4[0].ref = h;
    h4[1].i = 9;
    h4[2].ref = g_linker->AllocString(u"oo");
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "I", "Ljava/lang/Object;"}, "Landroid/os/Message;",
                     h4, 3, &m) &&
              m.ref != nullptr,
          "Message.obtain(h, 9, oo)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 9 && GetRef(msg, kMsgObj) == h4[2].ref &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtain(h, what, obj)：what/obj/target");

    Value h5[4] = {};
    h5[0].ref = h;
    h5[1].i = 9;
    h5[2].i = 3;
    h5[3].i = 4;
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Handler;", "I", "I", "I"},
                     "Landroid/os/Message;", h5, 4, &m) &&
              m.ref != nullptr,
          "Message.obtain(h, 9, 3, 4)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 9 && GetI(msg, kMsgArg1) == 3 && GetI(msg, kMsgArg2) == 4 &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtain(h, what, arg1, arg2)：全字段");

    Value h6[5] = {};
    h6[0].ref = h;
    h6[1].i = 9;
    h6[2].i = 3;
    h6[3].i = 4;
    h6[4].ref = g_linker->AllocString(u"op");
    CHECK(CallStatic("Landroid/os/Message;", "obtain",
                     {"Landroid/os/Handler;", "I", "I", "I", "Ljava/lang/Object;"},
                     "Landroid/os/Message;", h6, 5, &m) &&
              m.ref != nullptr,
          "Message.obtain(h, 9, 3, 4, op)");
    msg = reinterpret_cast<VObject*>(m.ref);
    CHECK(GetI(msg, kMsgWhat) == 9 && GetI(msg, kMsgArg1) == 3 && GetI(msg, kMsgArg2) == 4 &&
              GetRef(msg, kMsgObj) == h6[4].ref &&
              GetRef(msg, kMsgTarget) == reinterpret_cast<ObjHeader*>(h),
          "obtain(h, what, arg1, arg2, obj)：全字段");
  }

  // ---- 3. copyFrom / getWhen / getCallback / async 面 ----
  std::printf("\n-- 3. copyFrom / getWhen / getCallback / asynchrony --\n");
  {
    Value a5[5] = {};
    a5[0].ref = h;
    a5[1].i = 5;
    a5[2].i = 11;
    a5[3].i = 22;
    a5[4].ref = g_linker->AllocString(u"co");
    Value orig{};
    CallStatic("Landroid/os/Message;", "obtain",
               {"Landroid/os/Handler;", "I", "I", "I", "Ljava/lang/Object;"}, "Landroid/os/Message;",
               a5, 5, &orig);
    VObject* o = reinterpret_cast<VObject*>(orig.ref);
    ObjHeader* cbx = reinterpret_cast<ObjHeader*>(Alloc(g_linker->FindClass("Ljava/lang/Object;")));
    SetRef(o, kMsgCallback, cbx);
    SetL(o, kMsgWhen, 777);
    Value m{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m),
          "obtain() 作 copyFrom 目标");
    VObject* dst = reinterpret_cast<VObject*>(m.ref);
    Value cf[1] = {};
    cf[0] = orig;
    CHECK(CallVirtualOn(dst, "copyFrom", {"Landroid/os/Message;"}, "V", cf, 1), "copyFrom(orig)");
    CHECK(GetI(dst, kMsgWhat) == 5 && GetI(dst, kMsgArg1) == 11 && GetI(dst, kMsgArg2) == 22 &&
              GetRef(dst, kMsgObj) == a5[4].ref,
          "copyFrom：what/arg1/arg2/obj 拷贝");
    CHECK(GetRef(dst, kMsgTarget) == nullptr && GetRef(dst, kMsgCallback) == nullptr &&
              GetL(dst, kMsgWhen) == 0,
          "copyFrom：target/callback/when 不拷贝");

    Value w{};
    CHECK(CallVirtualOn(o, "getWhen", {}, "J", nullptr, 0, &w) && w.l == 777, "getWhen() == 777");
    Value cb{};
    CHECK(CallVirtualOn(o, "getCallback", {}, "Ljava/lang/Runnable;", nullptr, 0, &cb) &&
              cb.ref == cbx,
          "getCallback() == callback");

    Value as{};
    CHECK(CallVirtualOn(dst, "isAsynchronous", {}, "Z", nullptr, 0, &as) && as.i == 0,
          "isAsynchronous() 默认 false");
    Value one{};
    one.i = 1;
    CHECK(CallVirtualOn(dst, "setAsynchronous", {"Z"}, "V", &one, 1), "setAsynchronous(true)");
    CHECK(CallVirtualOn(dst, "isAsynchronous", {}, "Z", nullptr, 0, &as) && as.i != 0,
          "isAsynchronous() == true");
    Value zero{};
    CHECK(CallVirtualOn(dst, "setAsynchronous", {"Z"}, "V", &zero, 1), "setAsynchronous(false)");
    CHECK(CallVirtualOn(dst, "isAsynchronous", {}, "Z", nullptr, 0, &as) && as.i == 0,
          "isAsynchronous() 回到 false");
  }

  // ---- 4. 复用语义（recycle 收池 / obtain 复用 / 在用护栏）----
  std::printf("\n-- 4. reuse semantics (pool) --\n");
  {
    // 4a. recycle() -> obtain() 同一实例（池复用）
    Value m{};
    CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m);
    VObject* p = reinterpret_cast<VObject*>(m.ref);
    CHECK(CallVirtualOn(p, "recycle", {}, "V", nullptr, 0), "recycle()（未投递）");
    CHECK(GetI(p, kMsgFlags) & kFlagInUse, "recycle 后 in-use 位（池内哨兵）");
    Value m2{};
    CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m2);
    CHECK(m2.ref == reinterpret_cast<ObjHeader*>(p),
          "obtain() 复用回收实例（obtain -> recycle -> obtain 同一对象）");

    // 4b. obtainMessage 同池（obtainMessage -> recycle -> obtainMessage 同一对象）
    Value m3{};
    CHECK(CallVirtualOn(h, "obtainMessage", {}, "Landroid/os/Message;", nullptr, 0, &m3),
          "obtainMessage() 取件");
    VObject* p3 = reinterpret_cast<VObject*>(m3.ref);
    CHECK(CallVirtualOn(p3, "recycle", {}, "V", nullptr, 0), "recycle()");
    Value m4{};
    CHECK(CallVirtualOn(h, "obtainMessage", {}, "Landroid/os/Message;", nullptr, 0, &m4),
          "obtainMessage() 再取件");
    CHECK(m4.ref == reinterpret_cast<ObjHeader*>(p3), "obtainMessage 复用回收实例");

    // 4c. 二次 recycle（连续两次，中间不 obtain——obtain 会清 in-use 复位可用态）：
    // IllegalStateException（AOSP：池内哨兵仍在用）
    Value m5c{};
    CHECK(CallVirtualOn(h, "obtainMessage", {}, "Landroid/os/Message;", nullptr, 0, &m5c) &&
              m5c.ref != nullptr,
          "obtainMessage() 供二次 recycle 断言");
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(m5c.ref), "recycle", {}, "V", nullptr, 0),
          "recycle() 第一次");
    CHECK(CallVirtualExpectEx(reinterpret_cast<VObject*>(m5c.ref), "recycle", {}, "V", nullptr, 0,
                              "Ljava/lang/IllegalStateException;"),
          "二次 recycle()：IllegalStateException");

    // 4d. 在用（已入队）不可 recycle / 不可重发
    Value om{};
    Value wh{};
    wh.i = 1;
    CHECK(CallVirtualOn(h, "obtainMessage", {"I"}, "Landroid/os/Message;", &wh, 1, &om) &&
              om.ref != nullptr,
          "obtainMessage(1) 供在用断言");
    VObject* p5 = reinterpret_cast<VObject*>(om.ref);
    Value sv[1] = {};
    sv[0] = om;
    CHECK(CallVirtualOn(h, "sendMessage", {"Landroid/os/Message;"}, "Z", sv, 1),
          "sendMessage(msg)");
    CHECK(CallVirtualExpectEx(p5, "recycle", {}, "V", nullptr, 0,
                              "Ljava/lang/IllegalStateException;"),
          "在用 recycle()：IllegalStateException");
    CHECK(CallVirtualExpectEx(h, "sendMessage", {"Landroid/os/Message;"}, "Z", sv, 1,
                              "Ljava/lang/IllegalStateException;"),
          "重发在用消息：IllegalStateException");
    Pump();  // 清理（dispatch 后自动收池）

    // 4e. dispatch 后 Looper 自动收池（AOSP loopOnce 尾 recycleUnchecked）：
    // 字段清零 + obtain() 复用同一实例
    Value om2{};
    Value wh2{};
    wh2.i = 2;
    CHECK(CallVirtualOn(h, "obtainMessage", {"I"}, "Landroid/os/Message;", &wh2, 1, &om2) &&
              om2.ref != nullptr,
          "obtainMessage(2) 供 dispatch 断言");
    VObject* p6 = reinterpret_cast<VObject*>(om2.ref);
    sv[0] = om2;
    CHECK(CallVirtualOn(h, "sendMessage", {"Landroid/os/Message;"}, "Z", sv, 1), "sendMessage");
    Pump();
    CHECK(GetI(p6, kMsgWhat) == 0 && (GetI(p6, kMsgFlags) & kFlagInUse) != 0,
          "dispatch 后自动收池：字段清零 + in-use（池内）");
    Value m7{};
    CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m7);
    CHECK(m7.ref == reinterpret_cast<ObjHeader*>(p6), "dispatch 后 obtain() 复用被收池实例");
  }

  // ---- 5. Message data 面（getData 懒建 / peekData / setData；lib Bundle 缝）----
  std::printf("\n-- 5. Message data face (Bundle seam) --\n");
  {
    Value m{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m),
          "obtain() 供 data 面");
    VObject* msg = reinterpret_cast<VObject*>(m.ref);
    Value p0{};
    CHECK(CallVirtualOn(msg, "peekData", {}, "Landroid/os/Bundle;", nullptr, 0, &p0) &&
              p0.ref == nullptr,
          "peekData() 无 data：null（不懒建）");
    Value d1{}, d2{};
    CHECK(CallVirtualOn(msg, "getData", {}, "Landroid/os/Bundle;", nullptr, 0, &d1) &&
              d1.ref != nullptr,
          "getData() 懒建非空（lib Bundle 缝转调）");
    CHECK(CallVirtualOn(msg, "getData", {}, "Landroid/os/Bundle;", nullptr, 0, &d2) &&
              d2.ref == d1.ref,
          "getData() 稳定同一实例");
    CHECK(CallVirtualOn(msg, "peekData", {}, "Landroid/os/Bundle;", nullptr, 0, &p0) &&
              p0.ref == d1.ref,
          "peekData() == getData() 留存");

    // setData/peekData 往返
    Value m2{};
    CallStatic("Landroid/os/Message;", "obtain", {}, "Landroid/os/Message;", nullptr, 0, &m2);
    VObject* msg2 = reinterpret_cast<VObject*>(m2.ref);
    Value b{};
    b.ref = Alloc(g_linker->FindClass("Landroid/os/Bundle;"));
    Value sa[1] = {};
    sa[0] = b;
    CHECK(CallVirtualOn(msg2, "setData", {"Landroid/os/Bundle;"}, "V", sa, 1), "setData(b)");
    CHECK(CallVirtualOn(msg2, "peekData", {}, "Landroid/os/Bundle;", nullptr, 0, &p0) &&
              p0.ref == b.ref,
          "peekData() == b（setData 往返）");

    // obtain(Message orig)：data 深拷贝（新 Bundle 实例，非共享）
    Value om[1] = {};
    om[0] = m2;
    Value m3{};
    CHECK(CallStatic("Landroid/os/Message;", "obtain", {"Landroid/os/Message;"},
                     "Landroid/os/Message;", om, 1, &m3) &&
              m3.ref != nullptr,
          "obtain(orig)（orig 带 data）");
    CHECK(CallVirtualOn(reinterpret_cast<VObject*>(m3.ref), "peekData", {},
                        "Landroid/os/Bundle;", nullptr, 0, &p0) &&
              p0.ref != nullptr && p0.ref != b.ref,
          "obtain(orig)：data 深拷贝（新 Bundle，不共享）");
  }

  std::printf("\n== handler/msg smoke: %s (%d failures) ==\n", g_failures == 0 ? "PASS" : "FAIL",
              g_failures);
  return g_failures == 0 ? 0 : 1;
}
