// android.app.Fragment / FragmentManager / FragmentTransaction 面（平台
// Fragment；androidx 里 lifecycle/ReportFragment 等仍以 android.app.Fragment
// 为基类——壳/SDK 启动链常驻）。语义口径：
//   - FragmentManager = Activity 级单例（AOSP Activity.getFragmentManager 同源）；
//   - FragmentTransaction.commit() 立即应用已收集操作（AOSP commit 是主线程
//     异步，但 VM 单主线程模型下即时应用等价且可观测）；add 时按 AOSP 顺序
//     派发 fragment.onAttach -> onCreate -> onActivityCreated；
//   - Fragment 基类字段面（mActivity/mContext/mTag/mArguments/...）保留，
//     生命周期默认实现空转（子类覆写经虚分发命中）。
#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins_internal.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/interpreter.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"

namespace dexvm::runtime {
namespace {

constexpr uint32_t kPub = 0x1;
constexpr uint32_t kProt = 0x4;
constexpr uint32_t kStatic = 0x8;
constexpr uint32_t kCtor = 0x10000;

VClass* MakeClass(ClassLinker* linker, std::vector<VClass*>* all, const char* desc,
                  VClass* super, uint32_t bytes, uint32_t acc) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->descriptor = desc;
  k->super_class = super;
  k->access_flags = acc;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + bytes;
  k->status = ClassStatus::kInitialized;
  all->push_back(k);
  return k;
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

void AddMethod(VClass* k, const char* name, std::vector<std::string> params, const char* ret,
               uint32_t acc, IntrinsicFn fn) {
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
  m.access_flags = acc;
  m.code = nullptr;
  m.native_fn = ToIntrinsicSlot(fn);
  k->direct_methods.push_back(std::move(m));
}

// ---- 字段槽 ----

VField* Field(VClass* k, const char* name) { return FindInstanceField(k, name); }

ObjHeader* Ref(VObject* o, const char* name) {
  VField* f = Field(o->klass, name);
  return f != nullptr ? GetRefField(o, f->offset) : nullptr;
}

void SetRef(VObject* o, const char* name, ObjHeader* v) {
  VField* f = Field(o->klass, name);
  if (f != nullptr) SetRefField(o, f->offset, v);
}

int32_t Int(VObject* o, const char* name) {
  VField* f = Field(o->klass, name);
  return f != nullptr ? GetIField(o, f->offset) : 0;
}

void SetInt(VObject* o, const char* name, int32_t v) {
  VField* f = Field(o->klass, name);
  if (f != nullptr) SetIField(o, f->offset, v);
}

bool CallVirtual(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
                 const char* ret, Value* args, size_t n) {
  if (obj == nullptr || obj->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "call on null");
    return false;
  }
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return true;  // 未覆写：基类默认即空转
  Value self{};
  self.ref = obj;
  std::vector<Value> full;
  full.push_back(self);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  return InvokeMethod(state, m, full.data(), full.size(), &r);
}

VObject* NewObject(VMState& state, const char* desc) {
  VClass* k = state.linker->FindClass(desc);
  if (k == nullptr || !CheckClassDefinition(state, k) ||
      !state.linker->InitializeClass(k, &state)) {
    state.ClearException();
    return nullptr;
  }
  return state.linker->AllocObject(k);
}

// ---- 侧表（VM 会话生命周期；RegisterFrameworkBuiltins 复位） ----
std::map<VObject*, VObject*> g_fragment_manager_by_activity;  // Activity -> FragmentManager
std::map<VObject*, VObject*> g_fragment_manager_host;          // FragmentManager -> Activity
std::map<VObject*, std::vector<VObject*>> g_fragment_lists;    // FragmentManager -> Fragment[]
std::map<VObject*, std::vector<VObject*>> g_txn_adds;          // Transaction -> Fragment[]

VObject* GetFragmentManager(VMState& state, VObject* activity) {
  auto it = g_fragment_manager_by_activity.find(activity);
  if (it != g_fragment_manager_by_activity.end()) return it->second;
  VObject* fm = NewObject(state, "Landroid/app/FragmentManager;");
  if (fm == nullptr) return nullptr;
  g_fragment_manager_by_activity[activity] = fm;
  g_fragment_manager_host[fm] = activity;
  if (state.heap != nullptr) state.heap->Pin(fm);
  return fm;
}

}  // namespace

void ResetFragmentState() {
  g_fragment_manager_by_activity.clear();
  g_fragment_manager_host.clear();
  g_fragment_lists.clear();
  g_txn_adds.clear();
}

// ---- Fragment 生命周期默认实现（空转；子类覆写经虚分发） ----
Value FragNoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

Value FragInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  SetInt(self, "mUserVisibleHint", 1);
  return Value{};
}

Value FragGetActivity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = Ref(self, "mActivity");
  return r;
}

Value FragGetContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  ObjHeader* ctx = Ref(self, "mContext");
  if (ctx == nullptr) ctx = Ref(self, "mActivity");
  r.ref = ctx;
  return r;
}

Value FragGetView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = Ref(self, "mView");
  return r;
}

Value FragGetId(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mFragmentId");
  return r;
}

Value FragGetTag(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = Ref(self, "mTag");
  return r;
}

Value FragSetArguments(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetRef(self, "mArguments", const_cast<ObjHeader*>(args[0].ref));
  return Value{};
}

Value FragGetArguments(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = Ref(self, "mArguments");
  return r;
}

Value FragSetRetain(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetInt(self, "mRetainInstance", args[0].i != 0 ? 1 : 0);
  return Value{};
}

Value FragGetRetain(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mRetainInstance");
  return r;
}

Value FragIsAdded(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mAdded");
  return r;
}

Value FragIsHidden(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mHidden");
  return r;
}

Value FragIsDetached(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mDetached");
  return r;
}

Value FragIsRemoving(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mRemoving");
  return r;
}

Value FragIsVisible(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = (Int(self, "mAdded") != 0 && Int(self, "mHidden") == 0 &&
         Ref(self, "mView") != nullptr)
            ? 1
            : 0;
  return r;
}

Value FragGetFragmentManager(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  ObjHeader* fm = Ref(self, "mFragmentManager");
  if (fm == nullptr) {
    VObject* activity = reinterpret_cast<VObject*>(Ref(self, "mActivity"));
    if (activity != nullptr) {
      VObject* created = GetFragmentManager(state, activity);
      if (created != nullptr) {
        SetRef(self, "mFragmentManager", reinterpret_cast<ObjHeader*>(created));
        fm = reinterpret_cast<ObjHeader*>(created);
      }
    }
  }
  r.ref = fm;
  return r;
}

Value FragGetChildFragmentManager(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  ObjHeader* fm = Ref(self, "mChildFragmentManager");
  if (fm == nullptr) {
    VObject* created = NewObject(state, "Landroid/app/FragmentManager;");
    if (created != nullptr) {
      SetRef(self, "mChildFragmentManager", reinterpret_cast<ObjHeader*>(created));
      fm = reinterpret_cast<ObjHeader*>(created);
    }
  }
  r.ref = fm;
  return r;
}

Value FragGetParentFragment(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = Ref(self, "mParentFragment");
  return r;
}

Value FragGetResources(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  ObjHeader* ctx = Ref(self, "mContext");
  if (ctx == nullptr) ctx = Ref(self, "mActivity");
  if (ctx != nullptr) {
    VObject* c = reinterpret_cast<VObject*>(ctx);
    if (CallVirtual(state, c, "getResources", {}, "Landroid/content/res/Resources;", nullptr,
                    0)) {
      VMethod probe;
      probe.name = "getResources";
      probe.return_type = "Landroid/content/res/Resources;";
      if (VMethod* m = c->klass->FindInterfaceImpl(&probe)) {
        Value rv{};
        Value self_v{};
        self_v.ref = c;
        if (InvokeMethod(state, m, &self_v, 1, &rv)) r.ref = rv.ref;
      }
    }
  }
  return r;
}

Value FragStartActivity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  ObjHeader* ctx = Ref(self, "mContext");
  if (ctx == nullptr) ctx = Ref(self, "mActivity");
  if (ctx == nullptr || n < 1) return Value{};
  VObject* c = reinterpret_cast<VObject*>(ctx);
  VMethod probe;
  probe.name = "startActivity";
  probe.param_types = {"Landroid/content/Intent;"};
  probe.return_type = "V";
  if (VMethod* m = c->klass->FindInterfaceImpl(&probe)) {
    Value av[2] = {};
    av[0].ref = c;
    av[1].ref = args[0].ref;
    Value r{};
    InvokeMethod(state, m, av, 2, &r);
  }
  return Value{};
}

Value FragGetStringRes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  ObjHeader* ctx = Ref(self, "mContext");
  if (ctx == nullptr) ctx = Ref(self, "mActivity");
  if (ctx == nullptr || n < 1) return Value{};
  VObject* c = reinterpret_cast<VObject*>(ctx);
  VMethod probe;
  probe.name = "getString";
  probe.param_types = {"I"};
  probe.return_type = "Ljava/lang/String;";
  if (VMethod* m = c->klass->FindInterfaceImpl(&probe)) {
    Value av[2] = {};
    av[0].ref = c;
    av[1].i = args[0].i;
    Value r{};
    if (InvokeMethod(state, m, av, 2, &r)) return r;
  }
  return Value{};
}

Value FragGetTextRes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  ObjHeader* ctx = Ref(self, "mContext");
  if (ctx == nullptr) ctx = Ref(self, "mActivity");
  if (ctx == nullptr || n < 1) return Value{};
  VObject* c = reinterpret_cast<VObject*>(ctx);
  VMethod probe;
  probe.name = "getText";
  probe.param_types = {"I"};
  probe.return_type = "Ljava/lang/CharSequence;";
  if (VMethod* m = c->klass->FindInterfaceImpl(&probe)) {
    Value av[2] = {};
    av[0].ref = c;
    av[1].i = args[0].i;
    Value r{};
    if (InvokeMethod(state, m, av, 2, &r)) return r;
  }
  return Value{};
}

Value FragGetLayoutInflater(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  ObjHeader* ctx = Ref(self, "mContext");
  if (ctx == nullptr) ctx = Ref(self, "mActivity");
  if (ctx != nullptr) {
    VObject* c = reinterpret_cast<VObject*>(ctx);
    VMethod probe;
    probe.name = "getSystemService";
    probe.param_types = {"Ljava/lang/String;"};
    probe.return_type = "Ljava/lang/Object;";
    if (VMethod* m = c->klass->FindInterfaceImpl(&probe)) {
      Value av[2] = {};
      av[0].ref = c;
      av[1].ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(u"layout_inflater"));
      Value rv{};
      if (InvokeMethod(state, m, av, 2, &rv)) r.ref = rv.ref;
    }
  }
  return r;
}

Value FragUserVisibleHint(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetInt(self, "mUserVisibleHint", args[0].i != 0 ? 1 : 0);
  return Value{};
}

Value FragGetUserVisibleHint(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = Int(self, "mUserVisibleHint");
  return r;
}

Value FragSetMenuVisibility(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) SetInt(self, "mMenuVisible", args[0].i != 0 ? 1 : 0);
  return Value{};
}

Value FragReturnFalse(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  return r;
}

// ---- FragmentManager / FragmentTransaction ----

Value FMGetFragmentManagerForActivity(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(GetFragmentManager(state, self));
  return r;
}

Value FMBeginTransaction(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* txn = NewObject(state, "Landroid/app/FragmentTransaction;");
  if (txn == nullptr) return r;
  SetRef(txn, "mManager", reinterpret_cast<ObjHeader*>(self));
  g_txn_adds[txn].clear();
  r.ref = reinterpret_cast<ObjHeader*>(txn);
  return r;
}

Value FMFindByTag(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  std::string tag = Mutf8Arg(args[0]);
  for (VObject* f : g_fragment_lists[self]) {
    ObjHeader* t = Ref(f, "mTag");
    if (t != nullptr && static_cast<VString*>(t)->ToModifiedUtf8() == tag) {
      r.ref = reinterpret_cast<ObjHeader*>(f);
      break;
    }
  }
  return r;
}

Value FMFindById(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  for (VObject* f : g_fragment_lists[self]) {
    if (Int(f, "mFragmentId") == args[0].i) {
      r.ref = reinterpret_cast<ObjHeader*>(f);
      break;
    }
  }
  return r;
}

Value FMExecutePending(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

Value FMIsDestroyed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  return r;
}

Value FMFragments(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  VObject* list = NewObject(state, "Ljava/util/ArrayList;");
  if (list == nullptr) return r;
  for (VObject* f : g_fragment_lists[self]) {
    VMethod probe;
    probe.name = "add";
    probe.param_types = {"Ljava/lang/Object;"};
    probe.return_type = "Z";
    if (VMethod* m = list->klass->FindInterfaceImpl(&probe)) {
      Value av[2] = {};
      av[0].ref = list;
      av[1].ref = reinterpret_cast<ObjHeader*>(f);
      Value rv{};
      InvokeMethod(state, m, av, 2, &rv);
    }
  }
  r.ref = reinterpret_cast<ObjHeader*>(list);
  return r;
}

Value TxnAdd(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  // add(int containerViewId, Fragment[, tag]) / add(Fragment, tag)
  VObject* frag = nullptr;
  int32_t id = 0;
  ObjHeader* tag = nullptr;
  VClass* frag_cls = state.linker->FindClass("Landroid/app/Fragment;");
  for (size_t i = 0; i < n; i++) {
    if (args[i].ref == nullptr) {
      id = args[i].i;  // 数值参数槽（I）
      continue;
    }
    if (args[i].ref->klass == nullptr) continue;
    if (frag_cls != nullptr && frag_cls->IsAssignableFrom(args[i].ref->klass)) {
      frag = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[i].ref));
    } else if (std::strcmp(args[i].ref->klass->descriptor.c_str(),
                           "Ljava/lang/String;") == 0) {
      tag = args[i].ref;
    }
  }
  if (frag == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "FragmentTransaction.add: null fragment");
    *ok = false;
    return Value{};
  }
  if (id != 0) SetInt(frag, "mContainerId", id);
  if (tag != nullptr) SetRef(frag, "mTag", tag);
  g_txn_adds[self].push_back(frag);
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(self);  // AOSP 链式：返回 this
  return r;
}

Value TxnNoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(self);  // AOSP 链式：返回 this
  return r;
}

Value TxnCommit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  static int32_t s_txn_id = 0;
  auto it = g_txn_adds.find(self);
  if (it == g_txn_adds.end()) {
    Value r{};
    r.i = ++s_txn_id;
    return r;
  }
  ObjHeader* fm_ref = Ref(self, "mManager");
  for (VObject* frag : it->second) {
    if (fm_ref != nullptr) {
      SetRef(frag, "mFragmentManager", fm_ref);
      VObject* fm = reinterpret_cast<VObject*>(fm_ref);
      auto host = g_fragment_manager_host.find(fm);
      if (host != g_fragment_manager_host.end()) {
        SetRef(frag, "mActivity", reinterpret_cast<ObjHeader*>(host->second));
        SetRef(frag, "mContext", reinterpret_cast<ObjHeader*>(host->second));
      }
      g_fragment_lists[fm].push_back(frag);
    }
    SetInt(frag, "mAdded", 1);
    // AOSP 派发顺序：onAttach(Context) -> onCreate(Bundle) -> onActivityCreated
    Value ctx{};
    ctx.ref = Ref(frag, "mContext");
    if (!CallVirtual(state, frag, "onAttach", {"Landroid/content/Context;"}, "V", &ctx, 1)) {
      *ok = false;
      return Value{};
    }
    if (!CallVirtual(state, frag, "onCreate", {"Landroid/os/Bundle;"}, "V", nullptr, 0)) {
      *ok = false;
      return Value{};
    }
    if (!CallVirtual(state, frag, "onActivityCreated", {"Landroid/os/Bundle;"}, "V", nullptr, 0)) {
      *ok = false;
      return Value{};
    }
  }
  it->second.clear();
  Value r{};
  r.i = ++s_txn_id;  // AOSP commit 返回事务 id（>0）
  return r;
}

// ---- 注册 ----

void RegisterFragmentBuiltins(ClassLinker* linker, std::vector<VClass*>* all,
                              VClass* activity) {
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  if (object == nullptr) return;
  VClass* ctx_wrapper = linker->FindClass("Landroid/content/ContextWrapper;");

  // Fragment
  VClass* fragment =
      MakeClass(linker, all, "Landroid/app/Fragment;", object, 152, kPub);
  if (fragment != nullptr) {
    AddField(fragment, "mActivity", "Landroid/app/Activity;", 0);
    AddField(fragment, "mContext", "Landroid/content/Context;", 8);
    AddField(fragment, "mFragmentId", "I", 16);
    AddField(fragment, "mContainerId", "I", 24);
    AddField(fragment, "mTag", "Ljava/lang/String;", 32);
    AddField(fragment, "mArguments", "Landroid/os/Bundle;", 40);
    AddField(fragment, "mParentFragment", "Landroid/app/Fragment;", 48);
    AddField(fragment, "mChildFragmentManager", "Landroid/app/FragmentManager;", 56);
    AddField(fragment, "mFragmentManager", "Landroid/app/FragmentManager;", 64);
    AddField(fragment, "mView", "Landroid/view/View;", 72);
    AddField(fragment, "mRetainInstance", "Z", 80);
    AddField(fragment, "mHidden", "Z", 88);
    AddField(fragment, "mAdded", "Z", 96);
    AddField(fragment, "mDetached", "Z", 104);
    AddField(fragment, "mRemoving", "Z", 112);
    AddField(fragment, "mMenuVisible", "Z", 120);
    AddField(fragment, "mUserVisibleHint", "Z", 128);
    AddField(fragment, "mSavedFragmentState", "Landroid/os/Bundle;", 136);
    AddField(fragment, "mSavedViewState", "Landroid/os/Parcelable;", 144);
    AddMethod(fragment, "<init>", {}, "V", kPub | kCtor, FragInit);
    AddMethod(fragment, "getActivity", {}, "Landroid/app/Activity;", kPub, FragGetActivity);
    AddMethod(fragment, "getContext", {}, "Landroid/content/Context;", kPub, FragGetContext);
    AddMethod(fragment, "getView", {}, "Landroid/view/View;", kPub, FragGetView);
    AddMethod(fragment, "getId", {}, "I", kPub, FragGetId);
    AddMethod(fragment, "getTag", {}, "Ljava/lang/String;", kPub, FragGetTag);
    AddMethod(fragment, "setArguments", {"Landroid/os/Bundle;"}, "V", kPub, FragSetArguments);
    AddMethod(fragment, "getArguments", {}, "Landroid/os/Bundle;", kPub, FragGetArguments);
    AddMethod(fragment, "setRetainInstance", {"Z"}, "V", kPub, FragSetRetain);
    AddMethod(fragment, "getRetainInstance", {}, "Z", kPub, FragGetRetain);
    AddMethod(fragment, "isAdded", {}, "Z", kPub, FragIsAdded);
    AddMethod(fragment, "isHidden", {}, "Z", kPub, FragIsHidden);
    AddMethod(fragment, "isDetached", {}, "Z", kPub, FragIsDetached);
    AddMethod(fragment, "isRemoving", {}, "Z", kPub, FragIsRemoving);
    AddMethod(fragment, "isVisible", {}, "Z", kPub, FragIsVisible);
    AddMethod(fragment, "isResumed", {}, "Z", kPub, FragReturnFalse);
    AddMethod(fragment, "isInLayout", {}, "Z", kPub, FragReturnFalse);
    AddMethod(fragment, "getFragmentManager", {}, "Landroid/app/FragmentManager;", kPub,
              FragGetFragmentManager);
    AddMethod(fragment, "getChildFragmentManager", {}, "Landroid/app/FragmentManager;", kPub,
              FragGetChildFragmentManager);
    AddMethod(fragment, "getParentFragment", {}, "Landroid/app/Fragment;", kPub,
              FragGetParentFragment);
    AddMethod(fragment, "getResources", {}, "Landroid/content/res/Resources;", kPub,
              FragGetResources);
    AddMethod(fragment, "startActivity", {"Landroid/content/Intent;"}, "V", kPub,
              FragStartActivity);
    AddMethod(fragment, "getString", {"I"}, "Ljava/lang/String;", kPub, FragGetStringRes);
    AddMethod(fragment, "getText", {"I"}, "Ljava/lang/CharSequence;", kPub, FragGetTextRes);
    AddMethod(fragment, "getLayoutInflater", {"Landroid/os/Bundle;"}, "Landroid/view/LayoutInflater;",
              kPub, FragGetLayoutInflater);
    AddMethod(fragment, "setUserVisibleHint", {"Z"}, "V", kPub, FragUserVisibleHint);
    AddMethod(fragment, "getUserVisibleHint", {}, "Z", kPub, FragGetUserVisibleHint);
    AddMethod(fragment, "setMenuVisibility", {"Z"}, "V", kPub, FragSetMenuVisibility);
    AddMethod(fragment, "registerForContextMenu", {"Landroid/view/View;"}, "V", kPub, FragNoOp);
    AddMethod(fragment, "unregisterForContextMenu", {"Landroid/view/View;"}, "V", kPub, FragNoOp);
    // 生命周期默认实现（子类覆写走虚分发）
    const char* kBundle = "Landroid/os/Bundle;";
    AddMethod(fragment, "onAttach", {"Landroid/content/Context;"}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onAttach", {"Landroid/app/Activity;"}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onCreate", {kBundle}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onCreateView",
              {"Landroid/view/LayoutInflater;", "Landroid/view/ViewGroup;", kBundle},
              "Landroid/view/View;", kPub, FragGetView);  // 默认 null（无视图 Fragment）
    AddMethod(fragment, "onViewCreated", {"Landroid/view/View;", kBundle}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onActivityCreated", {kBundle}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onViewStateRestored", {kBundle}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onStart", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onResume", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onPause", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onStop", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onDestroyView", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onDestroy", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onDetach", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onSaveInstanceState", {kBundle}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onConfigurationChanged",
              {"Landroid/content/res/Configuration;"}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onLowMemory", {}, "V", kPub, FragNoOp);
    AddMethod(fragment, "onRequestPermissionsResult",
              {"I", "[Ljava/lang/String;", "[I"}, "V", kPub, FragNoOp);
    AddMethod(fragment, "dump", {"Ljava/lang/String;", "Ljava/io/FileDescriptor;",
                                 "Ljava/io/PrintWriter;", "[Ljava/lang/String;"},
              "V", kPub, FragNoOp);
    AddMethod(fragment, "toString", {}, "Ljava/lang/String;", kPub, FragNoOp);
  }
  VClass* fragment_cls = fragment;

  // FragmentManager
  VClass* fm = MakeClass(linker, all, "Landroid/app/FragmentManager;", object, 8, kPub);
  if (fm != nullptr) {
    AddMethod(fm, "beginTransaction", {}, "Landroid/app/FragmentTransaction;", kPub,
              FMBeginTransaction);
    AddMethod(fm, "findFragmentByTag", {"Ljava/lang/String;"}, "Landroid/app/Fragment;", kPub,
              FMFindByTag);
    AddMethod(fm, "findFragmentById", {"I"}, "Landroid/app/Fragment;", kPub, FMFindById);
    AddMethod(fm, "executePendingTransactions", {}, "Z", kPub, FMExecutePending);
    AddMethod(fm, "isDestroyed", {}, "Z", kPub, FMIsDestroyed);
    AddMethod(fm, "getFragments", {}, "Ljava/util/List;", kPub, FMFragments);
    AddMethod(fm, "popBackStack", {}, "V", kPub, FragNoOp);
    AddMethod(fm, "popBackStackImmediate", {}, "Z", kPub, FragReturnFalse);
    AddMethod(fm, "addOnBackStackChangedListener",
              {"Landroid/app/FragmentManager$OnBackStackChangedListener;"}, "V", kPub, FragNoOp);
    AddMethod(fm, "removeOnBackStackChangedListener",
              {"Landroid/app/FragmentManager$OnBackStackChangedListener;"}, "V", kPub, FragNoOp);
  }
  if (fm != nullptr) {
    VClass* listener = MakeClass(
        linker, all, "Landroid/app/FragmentManager$OnBackStackChangedListener;", object, 0,
        kPub | 0x200 | 0x400);
    (void)listener;
  }

  // FragmentTransaction
  VClass* txn = MakeClass(linker, all, "Landroid/app/FragmentTransaction;", object, 24, kPub);
  if (txn != nullptr && fragment_cls != nullptr) {
    AddField(txn, "mManager", "Landroid/app/FragmentManager;", 0);
    AddMethod(txn, "add",
              {"I", "Landroid/app/Fragment;", "Ljava/lang/String;"},
              "Landroid/app/FragmentTransaction;", kPub, TxnAdd);
    AddMethod(txn, "add", {"I", "Landroid/app/Fragment;"},
              "Landroid/app/FragmentTransaction;", kPub, TxnAdd);
    AddMethod(txn, "add", {"Landroid/app/Fragment;", "Ljava/lang/String;"},
              "Landroid/app/FragmentTransaction;", kPub, TxnAdd);
    AddMethod(txn, "remove", {"Landroid/app/Fragment;"}, "Landroid/app/FragmentTransaction;",
              kPub, TxnNoOp);
    AddMethod(txn, "replace",
              {"I", "Landroid/app/Fragment;", "Ljava/lang/String;"},
              "Landroid/app/FragmentTransaction;", kPub, TxnNoOp);
    AddMethod(txn, "replace", {"I", "Landroid/app/Fragment;"},
              "Landroid/app/FragmentTransaction;", kPub, TxnNoOp);
    AddMethod(txn, "hide", {"Landroid/app/Fragment;"}, "Landroid/app/FragmentTransaction;", kPub,
              TxnNoOp);
    AddMethod(txn, "show", {"Landroid/app/Fragment;"}, "Landroid/app/FragmentTransaction;", kPub,
              TxnNoOp);
    AddMethod(txn, "detach", {"Landroid/app/Fragment;"}, "Landroid/app/FragmentTransaction;",
              kPub, TxnNoOp);
    AddMethod(txn, "attach", {"Landroid/app/Fragment;"}, "Landroid/app/FragmentTransaction;",
              kPub, TxnNoOp);
    AddMethod(txn, "addToBackStack", {"Ljava/lang/String;"},
              "Landroid/app/FragmentTransaction;", kPub, TxnNoOp);
    AddMethod(txn, "setReorderingAllowed", {"Z"}, "Landroid/app/FragmentTransaction;", kPub,
              TxnNoOp);
    AddMethod(txn, "setCustomAnimations", {"I", "I", "I", "I"},
              "Landroid/app/FragmentTransaction;", kPub, TxnNoOp);
    AddMethod(txn, "setCustomAnimations", {"I", "I"}, "Landroid/app/FragmentTransaction;", kPub,
              TxnNoOp);
    AddMethod(txn, "runOnCommit", {"Ljava/lang/Runnable;"}, "V", kPub, FragNoOp);
    AddMethod(txn, "commit", {}, "I", kPub, TxnCommit);  // AOSP 返回事务 id
    AddMethod(txn, "commitAllowingStateLoss", {}, "I", kPub, TxnCommit);
    AddMethod(txn, "commitNow", {}, "V", kPub, FragNoOp);
    AddMethod(txn, "commitNowAllowingStateLoss", {}, "V", kPub, FragNoOp);
    AddMethod(txn, "isEmpty", {}, "Z", kPub, FragReturnFalse);
  }
  (void)ctx_wrapper;
}

}  // namespace dexvm::runtime
