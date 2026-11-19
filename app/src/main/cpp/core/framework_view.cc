// android.view.View/ViewGroup + TextView + android.util.Log 面。
// 纯移动自 core/framework_builtins.cc（零行为变更；注册仍在 registrar）。

#include "core/framework_builtins.h"

#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <dirent.h>
#include <sys/stat.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <atomic>
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

// 点击回灌缝契约（bridge/ui_bridge_input.h）：viewId -> guest listener 注入。
// 实现体在本 TU 尾部（dexvm::bridge 段）；登记表随 VM 会话存活。
#include "bridge/ui_bridge_input.h"

namespace dexvm::runtime {

// 浮点字段槽读取（8 字节槽；跨 TU 共享面，见 framework_builtins_internal.h）
float GetFField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  float v = 0;
  std::memcpy(&v, &slot, sizeof(v));
  return v;
}

// ---- android.view.View / TextView ----

Value ViewInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  // 字段零初始化已在分配点完成（Heap::AllocRaw 每次分配 memset 整块）。
  // 不可在此清整实例区：dex（035）允许 <init> 在 invoke-direct super 之前写
  // 字段（合成 this$0 即此形态），super 构造器整块清零会把它冲成 null
  // （知乎 TabLayout$SlidingTabIndicator.<init> 实锤）。
  // AOSP 默认面：mAlpha 默认 1.0f（View.mAlpha 同值；0 会把物化出的宿主
  // View 变全透明）。
  VField* alpha_f = FindInstanceField(self->klass, "mAlpha");
  if (alpha_f != nullptr) SetFField(self, alpha_f->offset, 1.0f);
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mContext");
    if (f != nullptr) SetRefField(self, f->offset, args[0].ref);
  }
  return Value{};
}

namespace {
bool g_window_focused = false;
bool g_view_attached = false;
// View -> lib ViewTreeObserver（监听器登记面；Pin 保活。声明提前：窗口焦点
// 派发也走 observer 面）。
std::map<VObject*, VObject*> g_vto_by_view;
// AOSP View.requestLayout/invalidate：标记树脏，下一次 choreographer 帧
// 重新 measure/layout/draw。会话帧循环按此脏位触发 traversal（异步 UI
// ——ViewPager/RecyclerView adapter 绑定、延迟 addView——全靠它）。
std::atomic<bool> g_layout_requested{false};
}  // namespace

void RequestGuestLayout() {
  g_layout_requested.store(true, std::memory_order_relaxed);
}

bool TakeGuestLayoutRequest() {
  return g_layout_requested.exchange(false, std::memory_order_relaxed);
}

void SetGuestWindowFocused(bool focused) {
  g_window_focused = focused;
}

void SetGuestViewAttached(bool attached) {
  g_view_attached = attached;
}

bool GuestViewAttached() {
  return g_view_attached;
}

// AOSP View.dispatchAttachedToWindow/onWindowVisibilityChanged 近似面：
// 父先子后，递归整树；缺省实现（内建 View NoOp）失败不中断。
bool DispatchViewAttached(VMState& state, VObject* view, bool attached) {
  if (view == nullptr) return true;
  Value out{};
  if (!CallVirtual(state, view, attached ? "onAttachedToWindow" : "onDetachedFromWindow", {}, "V",
                   nullptr, 0, &out)) {
    state.ClearException();
  }
  Value vis[1] = {};
  vis[0].i = attached ? 0 : 8;  // VISIBLE / GONE（窗口不可见）
  if (!CallVirtual(state, view, "onWindowVisibilityChanged", {"I"}, "V", vis, 1, &out)) {
    state.ClearException();
  }
  VField* f = FindInstanceField(view->klass, "mChildren");
  if (f == nullptr) return true;
  VArray* children = reinterpret_cast<VArray*>(GetRefField(view, f->offset));
  if (children == nullptr) return true;
  for (int32_t i = 0; i < children->length; i++) {
    ObjHeader* ref = nullptr;
    std::memcpy(&ref, children->data() + static_cast<size_t>(i) * 8, sizeof(ref));
    if (ref == nullptr) continue;
    DispatchViewAttached(state, reinterpret_cast<VObject*>(ref), attached);
  }
  return true;
}

bool GuestWindowFocused() {
  return g_window_focused;
}

Value ViewHasWindowFocus(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = g_window_focused ? 1 : 0;
  return r;
}

// 递归分发 onWindowFocusChanged（AOSP View.dispatchWindowFocusChanged 近似面：
// 父视图先收、再子视图）。
bool DispatchWindowFocusChanged(VMState& state, VObject* view, bool focused) {
  if (view == nullptr) return true;
  Value args[1] = {};
  args[0].i = focused ? 1 : 0;
  Value out{};
  if (!CallVirtual(state, view, "onWindowFocusChanged", {"Z"}, "V", args, 1, &out)) {
    // 基类缺省实现 = 空转；单个 View 失败不中断整树分发。
    state.ClearException();
  }
  // ViewTreeObserver.OnWindowFocusChangeListener 面（view 级 observer 单例）。
  auto vto_it = g_vto_by_view.find(view);
  if (vto_it != g_vto_by_view.end() && vto_it->second != nullptr) {
    Value obs_out{};
    if (!CallVirtual(state, vto_it->second, "dispatchOnWindowFocusChange", {"Z"}, "V", args, 1,
                     &obs_out)) {
      state.ClearException();
    }
  }
  VField* f = FindInstanceField(view->klass, "mChildren");
  if (f == nullptr) return true;
  VArray* children = reinterpret_cast<VArray*>(GetRefField(view, f->offset));
  if (children == nullptr) return true;
  for (int32_t i = 0; i < children->length; i++) {
    ObjHeader* ref = nullptr;
    std::memcpy(&ref, children->data() + static_cast<size_t>(i) * 8, sizeof(ref));
    if (ref == nullptr) continue;
    if (!DispatchWindowFocusChanged(state, reinterpret_cast<VObject*>(ref), focused)) return false;
  }
  return true;
}

Value ViewDispatchWindowFocusChanged(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok) {
  bool focused = n >= 1 && args[0].i != 0;
  g_window_focused = focused;
  *ok = true;
  if (!DispatchWindowFocusChanged(state, self, focused)) {
    *ok = false;
    return Value{};
  }
  return Value{};
}

// View.getViewTreeObserver：每 View 惰性单例（lib ViewTreeObserver；监听器
// 登记面）。自定义 Tab/Pager 常以 OnGlobalLayoutListener 作为内容容器创建
// 闸门（知乎首页 tab container 即此形态），必须返回真实实例并在首帧
// traversal 后派发（见 DispatchViewTreeGlobalLayout）。
Value ViewGetViewTreeObserver(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  if (self == nullptr || state.linker == nullptr) return r;
  auto it = g_vto_by_view.find(self);
  if (it != g_vto_by_view.end()) {
    r.ref = reinterpret_cast<ObjHeader*>(it->second);
    return r;
  }
  VClass* cls = state.linker->FindClass("Landroid/view/ViewTreeObserver;");
  if (cls == nullptr || !state.linker->InitializeClass(cls, &state)) {
    state.ClearException();
    return r;
  }
  VObject* vto = state.linker->AllocObject(cls);
  if (vto == nullptr) return r;
  VMethod ctor_probe;
  ctor_probe.name = "<init>";
  ctor_probe.return_type = "V";
  if (VMethod* ctor = cls->FindInterfaceImpl(&ctor_probe)) {
    Value a[1] = {};
    a[0].ref = vto;
    Value tmp{};
    if (!InvokeMethod(state, ctor, a, 1, &tmp)) state.ClearException();
  }
  g_vto_by_view[self] = vto;
  if (state.heap != nullptr) {
    state.heap->Pin(self);
    state.heap->Pin(vto);
  }
  r.ref = vto;
  return r;
}

// 首帧 traversal 后按 AOSP performTraversals 口径派发 OnGlobalLayout（整树）。
// 每个 View 的 observer 独立登记；遍历全树保证任何子 View 上的监听器都收到
// 回调。监听器异常不失败首帧（ClearException 后继续）。
void DispatchViewTreeGlobalLayout(VMState& state, VObject* root) {
  std::vector<VObject*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    VObject* cur = stack.back();
    stack.pop_back();
    if (cur == nullptr) continue;
    if (state.heap == nullptr || !state.heap->IsHeapStart(cur) || cur->klass == nullptr) continue;
    auto it = g_vto_by_view.find(cur);
    if (it != g_vto_by_view.end() && it->second != nullptr) {
      Value out{};
      if (!CallVirtual(state, it->second, "dispatchOnGlobalLayout", {}, "V", nullptr, 0, &out)) {
        state.ClearException();
      }
    }
    VField* ch_f = FindInstanceField(cur->klass, "mChildren");
    if (ch_f != nullptr) {
      VArray* arr = reinterpret_cast<VArray*>(GetRefField(cur, ch_f->offset));
      if (arr != nullptr) {
        for (int32_t i = 0; i < arr->length; i++) {
          ObjHeader* ref = GetArrRef(arr, i);
          if (ref != nullptr) stack.push_back(reinterpret_cast<VObject*>(ref));
        }
      }
    }
  }
}

// OnPreDraw 派发（AOSP performTraversals：预绘制监听器）。返回 false =
// 本帧取消 draw；下一帧仍需重试（置脏）。
bool DispatchViewTreePreDraw(VMState& state, VObject* root) {
  bool proceed = true;
  std::vector<VObject*> stack;
  stack.push_back(root);
  while (!stack.empty()) {
    VObject* cur = stack.back();
    stack.pop_back();
    if (cur == nullptr) continue;
    if (state.heap == nullptr || !state.heap->IsHeapStart(cur) || cur->klass == nullptr) continue;
    auto it = g_vto_by_view.find(cur);
    if (it != g_vto_by_view.end() && it->second != nullptr) {
      Value out{};
      if (!CallVirtual(state, it->second, "dispatchOnPreDraw", {}, "Z", nullptr, 0, &out)) {
        state.ClearException();
      } else if (out.i == 0) {
        proceed = false;
      }
    }
    VField* ch_f = FindInstanceField(cur->klass, "mChildren");
    if (ch_f != nullptr) {
      VArray* arr = reinterpret_cast<VArray*>(GetRefField(cur, ch_f->offset));
      if (arr != nullptr) {
        for (int32_t i = 0; i < arr->length; i++) {
          ObjHeader* ref = GetArrRef(arr, i);
          if (ref != nullptr) stack.push_back(reinterpret_cast<VObject*>(ref));
        }
      }
    }
  }
  if (!proceed) RequestGuestLayout();
  return proceed;
}

Value ViewSetPadding(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  static const char* kNames[4] = {"mPaddingLeft", "mPaddingTop", "mPaddingRight", "mPaddingBottom"};
  for (size_t i = 0; i < 4 && i < n; i++) {
    VField* f = FindInstanceField(self->klass, kNames[i]);
    if (f != nullptr) SetIField(self, f->offset, args[i].i);
  }
  return Value{};
}

Value ViewGetContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mContext");
  if (f != nullptr) r.ref = GetRefField(self, f->offset);
  return r;
}

// ---- android.view.ViewGroup（M4.2：addView 存子 View，mChildren 数组字段） ----

// addView(View[, index])：AOSP 语义——index<0 或越界追加，否则插入。
namespace {
// 协变返回覆写查找（同 framework_inflate.cc FindCovariantMethod）：
// generateLayoutParams 子类覆写返回具体 LP 类型，精确签名查找漏配。
VMethod* FindCovariantMethodIn(VClass* klass, const char* name,
                               const std::vector<std::string>& params) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& m : k->virtual_methods)
      if (m.name == name && m.param_types == params) return &m;
    for (auto& m : k->direct_methods)
      if (!m.IsStatic() && m.name == name && m.param_types == params) return &m;
  }
  return nullptr;
}

// AOSP ViewGroup.addView 的 LayoutParams 协商：params==null →
// generateDefaultLayoutParams()；!checkLayoutParams(params) →
// generateLayoutParams(ViewGroup.LayoutParams)。否则子 View 会带着
// inflate 兜底的 LinearLayout.LayoutParams 进入 AppBarLayout/CoordinatorLayout
// 等强类型父容器 → guest (AppBarLayout.LayoutParams) 强转 CCE。
void EnsureLayoutParamsForAdd(VMState& state, VObject* parent, VObject* child) {
  if (parent == nullptr || child == nullptr) return;
  VField* lpf = FindInstanceField(child->klass, "mLayoutParams");
  if (lpf == nullptr) return;
  ObjHeader* lp = GetRefField(child, lpf->offset);
  auto invoke = [&](const char* name, const std::vector<std::string>& params,
                    ObjHeader* arg, bool* is_bool) -> ObjHeader* {
    VMethod* impl = FindCovariantMethodIn(parent->klass, name, params);
    if (impl == nullptr) return nullptr;
    Value av[2] = {};
    av[0].ref = parent;
    if (arg != nullptr) av[1].ref = arg;
    Value rv{};
    if (!InvokeMethod(state, impl, av, arg != nullptr ? 2 : 1, &rv)) {
      state.ClearException();
      return nullptr;
    }
    if (is_bool != nullptr) {
      *is_bool = rv.i != 0;
      return nullptr;
    }
    return rv.ref;
  };
  if (lp == nullptr) {
    ObjHeader* next = invoke("generateDefaultLayoutParams", {}, nullptr, nullptr);
    if (next != nullptr) SetRefField(child, lpf->offset, next);
    return;
  }
  bool fits = true;
  invoke("checkLayoutParams", {"Landroid/view/ViewGroup$LayoutParams;"}, lp, &fits);
  if (!fits) {
    ObjHeader* next = invoke("generateLayoutParams",
                             {"Landroid/view/ViewGroup$LayoutParams;"}, lp, nullptr);
    if (next != nullptr) SetRefField(child, lpf->offset, next);
  }
}
}  // namespace

Value VGAddView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "addView child == null");
    *ok = false;
    return Value{};
  }
  EnsureLayoutParamsForAdd(state, self,
                           reinterpret_cast<VObject*>(args[0].ref));
  // AOSP ViewGroup.addView：child.mParent = this（getParent 语义的基本不变量；
  // 缺回填 → ViewPager2 的 FragmentStateAdapter enforcer 沿 getParent 找宿主
  // 失败（"Expected ViewPager2 instance. Got: null"））。
  if (VObject* child = reinterpret_cast<VObject*>(args[0].ref)) {
    if (VField* pf = FindInstanceField(child->klass, "mParent")) {
      SetRefField(child, pf->offset, reinterpret_cast<ObjHeader*>(self));
    }
  }
  VField* f = FindInstanceField(self->klass, "mChildren");
  if (f == nullptr) return Value{};
  VArray* old = reinterpret_cast<VArray*>(GetRefField(self, f->offset));
  int32_t count = old != nullptr ? old->length : 0;
  int32_t index = n >= 2 ? args[1].i : -1;
  if (index < 0 || index > count) index = count;
  VClass* view_cls = state.linker->FindClass("Landroid/view/View;");
  VClass* arr_cls = view_cls != nullptr ? state.linker->FindArrayClass(view_cls) : nullptr;
  VArray* arr = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, count + 1) : nullptr;
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "addView children alloc failed");
    *ok = false;
    return Value{};
  }
  for (int32_t i = 0; i < index; i++) SetArrRef(arr, i, GetArrRef(old, i));
  SetArrRef(arr, index, args[0].ref);
  for (int32_t i = index; i < count; i++) SetArrRef(arr, i + 1, GetArrRef(old, i));
  SetRefField(self, f->offset, reinterpret_cast<ObjHeader*>(arr));
  // AOSP ViewGroup.addViewInner：onViewAdded(child) 钩子（ConstraintLayout
  // 在此维护 mChildrenByIds——缺失会导致约束目标查不到、整棵约束树按
  // MATCH_PARENT 兜底，TopNavigator 等高度吃满）。
  {
    Value added{};
    added.ref = args[0].ref;
    Value ignored{};
    bool hook_ok = CallVirtual(state, self, "onViewAdded", {"Landroid/view/View;"}, "V", &added, 1,
                               &ignored);
    if (!hook_ok) {
      // VM 语义缺口（如 ViewPager2 内部 RecyclerView 识别）不应让 addView
      // 整体失败：钩子异常按诊断面吞掉（异常已由 DexVmThrow 记录）。
      state.ClearException();
    }
  }
  // 已附着树内新增子 View：AOSP 立即分发 attach/窗口可见性（BottomNav 等
  // 自定义导航在 onAttachedToWindow/onWindowVisibilityChanged 里启动控制器）。
  if (GuestViewAttached() && args[0].ref != nullptr) {
    DispatchViewAttached(state, reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref)),
                         true);
  }
  return Value{};
}

// addView(View, LayoutParams) / addView(View, int, LayoutParams)：先登记布局参数
// （AOSP addView 的 params 形），再走索引形。
Value VGAddView2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "addView child == null");
    *ok = false;
    return Value{};
  }
  VObject* child = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  int32_t index = -1;
  ObjHeader* params = nullptr;
  for (size_t i = 1; i < n; i++) {
    if (args[i].ref != nullptr) {
      params = const_cast<ObjHeader*>(args[i].ref);
    } else {
      index = args[i].i;
    }
  }
  if (params != nullptr) {
    if (VField* f = FindInstanceField(child->klass, "mLayoutParams")) {
      SetRefField(child, f->offset, params);
    }
  }
  Value av[2] = {};
  av[0].ref = args[0].ref;
  av[1].i = index;
  return VGAddView(state, self, av, index >= 0 ? 2 : 1, ok);
}

// attachViewToParent(View,int,LayoutParams) / detachViewFromParent(View)：
// AOSP ViewGroup 隐藏附着面（ViewPager2/RecyclerView 等 androidx 使用）——
// 附着 = 设 LP + 入子表 + mParent；分离 = 出子表 + mParent 置空。
Value VGAttachViewToParent(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  VObject* child = reinterpret_cast<VObject*>(args[0].ref);
  if (n >= 3 && args[2].ref != nullptr) {
    if (VField* pf = FindInstanceField(child->klass, "mLayoutParams")) {
      SetRefField(child, pf->offset, const_cast<ObjHeader*>(args[2].ref));
    }
  }
  Value av[2] = {};
  av[0].ref = args[0].ref;
  av[1].i = n >= 2 ? args[1].i : -1;
  return VGAddView(state, self, av, av[1].i >= 0 ? 2 : 1, ok);
}

Value VGDetachViewFromParent(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  return VGRemoveView(state, self, args, n, ok);
}

Value VGDetachViewFromParentIdx(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  // detachViewFromParent(int index)（AOSP 隐藏面）：按索引移除 1 个。
  if (n < 1) {
    *ok = true;
    return Value{};
  }
  Value av[2] = {};
  av[0].i = args[0].i;
  av[1].i = 1;
  return VGRemoveViewAt(state, self, av, 2, ok);
}

// removeViewAt(int) / removeViews(int,int)：AOSP 子序移除（区间形取首参起 1 个）
Value VGRemoveViewAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 1) return Value{};
  VField* f = FindInstanceField(self->klass, "mChildren");
  if (f == nullptr) return Value{};
  VArray* old = reinterpret_cast<VArray*>(GetRefField(self, f->offset));
  int32_t count = old != nullptr ? old->length : 0;
  int32_t start = args[0].i;
  int32_t remove_count = n >= 2 ? args[1].i : 1;
  if (start < 0 || start >= count || remove_count <= 0) return Value{};
  if (start + remove_count > count) remove_count = count - start;
  VClass* view_cls = state.linker->FindClass("Landroid/view/View;");
  VClass* arr_cls = view_cls != nullptr ? state.linker->FindArrayClass(view_cls) : nullptr;
  VArray* arr =
      arr_cls != nullptr ? state.linker->AllocArray(arr_cls, count - remove_count) : nullptr;
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "removeViewAt");
    *ok = false;
    return Value{};
  }
  int32_t out = 0;
  for (int32_t i = 0; i < count; i++) {
    if (i >= start && i < start + remove_count) {
      // AOSP：被移除子 View 的 mParent 置空。
      if (VObject* rc = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(GetArrRef(old, i)))) {
        if (VField* pf = FindInstanceField(rc->klass, "mParent")) {
          SetRefField(rc, pf->offset, nullptr);
        }
      }
      continue;
    }
    SetArrRef(arr, out++, GetArrRef(old, i));
  }
  SetRefField(self, f->offset, reinterpret_cast<ObjHeader*>(arr));
  return Value{};
}

Value VGRemoveAllViews(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  VField* f = FindInstanceField(self->klass, "mChildren");
  if (f != nullptr) {
    // AOSP：全部移除时逐子清 mParent。
    if (VArray* children = reinterpret_cast<VArray*>(GetRefField(self, f->offset))) {
      for (int32_t i = 0; i < children->length; i++) {
        if (VObject* rc = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(GetArrRef(children, i)))) {
          if (VField* pf = FindInstanceField(rc->klass, "mParent")) {
            SetRefField(rc, pf->offset, nullptr);
          }
        }
      }
    }
    SetRefField(self, f->offset, nullptr);
  }
  return Value{};
}

Value VGGetChildCount(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mChildren");
  VArray* arr = f != nullptr ? reinterpret_cast<VArray*>(GetRefField(self, f->offset)) : nullptr;
  r.i = arr != nullptr ? arr->length : 0;
  return r;
}

Value VGGetChildAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mChildren");
  VArray* arr = f != nullptr ? reinterpret_cast<VArray*>(GetRefField(self, f->offset)) : nullptr;
  if (arr != nullptr && n >= 1 && args[0].i >= 0 && args[0].i < arr->length) {
    r.ref = GetArrRef(arr, args[0].i);
  }
  return r;  // 越界返回 null（AOSP ViewGroup.getChildAt）
}

// ---- android.widget.TextView ----

// TextView.<init>(Context)：View 构造语义 + AOSP 默认文字面（15sp / 不透明黑）。
Value TVInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  ViewInit(state, self, args, n, ok);
  if (!*ok) return Value{};
  VField* size_f = FindInstanceField(self->klass, "mTextSize");
  if (size_f != nullptr) SetFField(self, size_f->offset, 15.0f);
  VField* color_f = FindInstanceField(self->klass, "mTextColor");
  if (color_f != nullptr) SetIField(self, color_f->offset, static_cast<int32_t>(0xFF000000));
  return Value{};
}

// Button 构造面：AOSP Theme.Material Button 默认样式的最小等价——内边距
// 16/12（控件高度 = 文字高 + 24，近似 48dp minHeight 观感）。XML padding
// 属性随后覆写（inflate 在 ctor 之后应用属性）。
Value ButtonInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  TVInit(state, self, args, n, ok);
  if (!*ok) return Value{};
  VField* l = FindInstanceField(self->klass, "mPaddingLeft");
  VField* t = FindInstanceField(self->klass, "mPaddingTop");
  VField* r = FindInstanceField(self->klass, "mPaddingRight");
  VField* b = FindInstanceField(self->klass, "mPaddingBottom");
  if (l != nullptr) SetIField(self, l->offset, 16);
  if (t != nullptr) SetIField(self, t->offset, 12);
  if (r != nullptr) SetIField(self, r->offset, 16);
  if (b != nullptr) SetIField(self, b->offset, 12);
  return Value{};
}

// EditText 构造面：AOSP EditText 默认内边距 8/8（下划线由宿主控件自绘）。
Value EditTextInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  TVInit(state, self, args, n, ok);
  if (!*ok) return Value{};
  VField* l = FindInstanceField(self->klass, "mPaddingLeft");
  VField* t = FindInstanceField(self->klass, "mPaddingTop");
  VField* r = FindInstanceField(self->klass, "mPaddingRight");
  VField* b = FindInstanceField(self->klass, "mPaddingBottom");
  if (l != nullptr) SetIField(self, l->offset, 8);
  if (t != nullptr) SetIField(self, t->offset, 8);
  if (r != nullptr) SetIField(self, r->offset, 8);
  if (b != nullptr) SetIField(self, b->offset, 8);
  return Value{};
}

Value TVSetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mText");
    if (f != nullptr) SetRefField(self, f->offset, args[0].ref);
  }
  return Value{};
}

// LinearLayout 方向面（AOSP：HORIZONTAL=0 / VERTICAL=1；越界参数忽略）
Value LLSetOrientation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    if (args[0].i != 0 && args[0].i != 1) return Value{};
    VField* f = FindInstanceField(self->klass, "mOrientation");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value LLSetGravity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mGravity");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value LLGetGravity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mGravity");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value LLGetOrientation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mOrientation");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

// ---- 属性侧表（selected/activated/rotation/contentDescription）----
// View 实例布局已满（无空闲槽），这些展示态属性走侧表（VM 会话生命周期，
// 引导时由 ResetViewStateRegistries 清空）。

namespace {
std::map<VObject*, int> g_view_selected;
std::map<VObject*, int> g_view_activated;
std::map<VObject*, float> g_view_rotation;
std::map<VObject*, ObjHeader*> g_view_content_desc;
}  // namespace

void ResetViewStateRegistries() {
  g_view_selected.clear();
  g_view_activated.clear();
  g_view_rotation.clear();
  g_view_content_desc.clear();
}

Value ViewGroupIndexOfChild(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.i = -1;
  if (n < 1 || args[0].ref == nullptr) return r;
  VField* f = FindInstanceField(self->klass, "mChildren");
  VArray* arr = f != nullptr ? reinterpret_cast<VArray*>(GetRefField(self, f->offset)) : nullptr;
  if (arr == nullptr) return r;
  for (int32_t i = 0; i < arr->length; i++) {
    if (GetArrRef(arr, i) == args[0].ref) {
      r.i = i;
      break;
    }
  }
  return r;
}

Value ViewSetSelected(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) g_view_selected[self] = args[0].i != 0 ? 1 : 0;
  return Value{};
}

Value ViewIsSelected(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_view_selected.find(self);
  r.i = it != g_view_selected.end() ? it->second : 0;
  return r;
}

Value ViewSetActivated(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) g_view_activated[self] = args[0].i != 0 ? 1 : 0;
  return Value{};
}

Value ViewIsActivated(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_view_activated.find(self);
  r.i = it != g_view_activated.end() ? it->second : 0;
  return r;
}

Value ViewSetRotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) g_view_rotation[self] = args[0].f;
  return Value{};
}

Value ViewGetRotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_view_rotation.find(self);
  r.f = it != g_view_rotation.end() ? it->second : 0.0f;
  return r;
}

Value ViewSetContentDescription(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) g_view_content_desc[self] = args[0].ref;
  return Value{};
}

Value ViewGetContentDescription(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_view_content_desc.find(self);
  if (it != g_view_content_desc.end()) r.ref = it->second;
  return r;
}

// ---- widget 字段缝（资源布局面：Switch/ProgressBar/ImageView）----

Value SwitchSetChecked(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mChecked");
    if (f != nullptr) SetIField(self, f->offset, args[0].i != 0 ? 1 : 0);
  }
  return Value{};
}

Value SwitchIsChecked(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mChecked");
  if (f != nullptr) r.i = GetIField(self, f->offset) != 0 ? 1 : 0;
  return r;
}

Value ProgressBarSetProgress(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mProgress");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value ProgressBarGetProgress(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mProgress");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ImageViewSetImageResource(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mImageResource");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value TVGetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mText");
  if (f != nullptr) r.ref = GetRefField(self, f->offset);
  return r;
}

Value TVSetTextSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mTextSize");
    if (f != nullptr) SetFField(self, f->offset, args[0].f);
  }
  return Value{};
}

// ---- android.view.View 长尾面（需求清单批补充；framework 面批量补充轮）----
// 取值/存值全走字段缝（FindInstanceField by name；布局见 registrar）。

Value ViewGetId(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mId");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewGetLeft(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mLeft");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewGetTop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mTop");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewGetRight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mRight");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewGetBottom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mBottom");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

// AOSP getBackground 非 null 契约：material 组件（SwipeRefreshLayout 的
// CircleImageView）会直接 getBackground().setAlpha(...)；本 VM 的背景
// drawable 尚未做对象字段面，返回一个临时 ColorDrawable 占位，避免 NPE。
Value ViewGetBackground(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  if (state.linker == nullptr) return r;
  VClass* k = state.linker->FindClass("Landroid/graphics/drawable/ColorDrawable;");
  if (k == nullptr) return r;
  r.ref = state.linker->AllocObject(k);
  return r;
}

Value ViewOnScrollChanged(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

Value ViewGetVisibility(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mVisibility");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewSetVisibility(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mVisibility");
    if (f != nullptr) {
      int32_t old = GetIField(self, f->offset);
      SetIField(self, f->offset, args[0].i);
      // AOSP setVisibility：值变化 -> requestLayout/invalidate（下一帧
      // traversal）。缺此钩子时 XML 里 GONE、运行期转 VISIBLE 的控件
      // （TopNavigator tab、延迟加载区块）永远保持 0x0。
      if (old != args[0].i) RequestGuestLayout();
    }
  }
  return Value{};
}

Value ViewGetLayoutParams(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mLayoutParams");
  if (f != nullptr) r.ref = GetRefField(self, f->offset);
  return r;
}

Value ViewSetLayoutParams(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mLayoutParams");
    if (f != nullptr) SetRefField(self, f->offset, args[0].ref);
  }
  return Value{};
}

Value ViewGetParent(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mParent");
  if (f != nullptr) r.ref = GetRefField(self, f->offset);
  return r;
}

Value ViewGetTag0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mTag");
  if (f != nullptr) r.ref = GetRefField(self, f->offset);
  return r;
}

Value ViewSetTag0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mTag");
    if (f != nullptr) SetRefField(self, f->offset, args[0].ref);
  }
  return Value{};
}

// getTag(int)/setTag(int, Object)：AOSP keyed-tag 语义——单槽退化（key 忽略，
// 进程内常用单 tag；keyed map 后置）。
Value ViewGetTagKeyed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ViewGetTag0(state, self, args, n, ok);
}

Value ViewSetTagKeyed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 2) {
    VField* f = FindInstanceField(self->klass, "mTag");
    if (f != nullptr) SetRefField(self, f->offset, args[1].ref);
  }
  return Value{};
}

Value ViewSetAlpha(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mAlpha");
    if (f != nullptr) SetFField(self, f->offset, args[0].f);
  }
  return Value{};
}

Value ViewGetAlpha(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mAlpha");
  if (f != nullptr) r.f = GetFField(self, f->offset);
  return r;
}

Value ViewSetTranslationX(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mTranslationX");
    if (f != nullptr) SetFField(self, f->offset, args[0].f);
  }
  return Value{};
}

Value ViewSetTranslationY(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mTranslationY");
    if (f != nullptr) SetFField(self, f->offset, args[0].f);
  }
  return Value{};
}

Value ViewGetScrollX(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mScrollX");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewGetScrollY(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mScrollY");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value ViewSetScrollX(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mScrollX");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value ViewSetScrollY(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mScrollY");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value ViewRequestLayout(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  // AOSP：标记树脏 -> 下一帧 traversal（异步 UI 的驱动面，不能是空实现）。
  RequestGuestLayout();
  return Value{};
}

Value ViewInvalidate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  // invalidate 只要求重绘；本 VM 重绘 = 一次完整 traversal（draw 命令流记录）。
  RequestGuestLayout();
  return Value{};
}

// ---- 点击回灌缝（viewId -> guest listener 注入；bridge/ui_bridge_input.h 契约）----
// 登记表（view -> listener）随 VM 会话存活；RegisterRenderPipeline 每 VM 引导
// 清表（防跨会话悬挂）。list entry 的 listener 可空（NoteGuestView 只登记视图）。

namespace {

struct ClickEntry {
  VObject* view = nullptr;
  VObject* listener = nullptr;
};

std::vector<ClickEntry>& ClickRegistry() {
  static std::vector<ClickEntry> registry;
  return registry;
}

ClickEntry* FindClickEntry(VObject* view) {
  for (auto& e : ClickRegistry()) {
    if (e.view == view) return &e;
  }
  return nullptr;
}

VObject* FindClickListener(VObject* view) {
  ClickEntry* e = FindClickEntry(view);
  return e != nullptr ? e->listener : nullptr;
}

}  // namespace

// setOnClickListener(OnClickListener)：桥接缝存储（RegisterRenderPipeline 覆写
// NoOp 登记到此；listener==null 为清除）。
Value ViewSetOnClickListener(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  *ok = true;
  VObject* listener = nullptr;
  if (n >= 1 && args[0].ref != nullptr) listener = reinterpret_cast<VObject*>(args[0].ref);
  ClickEntry* e = FindClickEntry(self);
  if (e != nullptr) {
    e->listener = listener;
  } else {
    ClickRegistry().push_back(ClickEntry{self, listener});
  }
  return Value{};
}

// performClick()：点击语义——桥接层派发登记的 guest listener.onClick(View)
// （docs 设计："监听器派发归桥接层"）；无监听器走 AOSP 常量语义返回 true。
Value ViewPerformClick(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  VObject* listener = FindClickListener(self);
  if (listener != nullptr) {
    Value av{};
    av.ref = self;
    if (!CallVirtual(state, listener, "onClick", {"Landroid/view/View;"}, "V", &av, 1, nullptr)) {
      *ok = false;
      return Value{};
    }
  }
  Value r{};
  r.i = 1;
  return r;
}

Value ViewRemoveCallbacks(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

Value ViewPost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "post: Runnable == null");
    *ok = false;
    return r;
  }
  // AOSP View.post：未 attach 走 RunQueue（attach 时执行），已 attach 投
  // AttachInfo.mHandler。本 VM 未实现 RunQueue，统一投主线程 Handler——
  // 语义可观察结果一致（UI 线程执行），且不会像旧实现那样静默丢弃。
  VClass* handler_cls =
      state.linker != nullptr ? state.linker->FindClass("Landroid/os/Handler;") : nullptr;
  if (handler_cls == nullptr) {
    *ok = false;
    return r;
  }
  VObject* handler = state.linker->AllocObject(handler_cls);
  if (handler == nullptr) {
    ThrowOOM(state, "View.post handler");
    *ok = false;
    return r;
  }
  bool lok = true;
  Value looper = LooperGetMainLooper(state, nullptr, nullptr, 0, &lok);
  if (!lok || looper.ref == nullptr) {
    *ok = false;
    return r;
  }
  bool hok = true;
  Value larg[1] = {};
  larg[0].ref = looper.ref;
  HandlerInitLooper(state, handler, larg, 1, &hok);
  if (!hok) {
    *ok = false;
    return r;
  }
  if (n >= 2) {
    Value pr = HandlerPostDelayed(state, handler, args, n, ok);
    return pr;
  }
  return HandlerPost(state, handler, args, n, ok);
}

// animate()：返回 lib ViewPropertyAnimator（NewLibObject；链式语义由 lib 承担）。
Value ViewAnimate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  bool tok = true;
  VObject* anim = NewLibObject(state, "Landroid/view/ViewPropertyAnimator;", {}, nullptr, 0, &tok);
  if (!tok) {
    *ok = false;
    return Value{};
  }
  r.ref = anim;
  return r;
}

// findViewById(int)：本 View 及子树（mChildren 递归）按 mId 匹配。
Value ViewFindViewById(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  int32_t target = args[0].i;
#if defined(__ANDROID__)
  if (target == 0x01020002) {
    std::vector<VObject*> st;
    st.push_back(self);
    while (!st.empty()) {
      VObject* cur = st.back();
      st.pop_back();
      if (cur == nullptr) continue;
      int32_t idv = 0;
      if (VField* f = FindInstanceField(cur->klass, "mId")) idv = GetIField(cur, f->offset);
      __android_log_print(ANDROID_LOG_INFO, "DexVmFind", "  node %s id=0x%08x",
                          cur->klass->descriptor.c_str(), idv);
      if (VField* ch = FindInstanceField(cur->klass, "mChildren")) {
        VArray* arr = reinterpret_cast<VArray*>(GetRefField(cur, ch->offset));
        if (arr != nullptr) {
          for (int32_t i = 0; i < arr->length; i++) {
            st.push_back(reinterpret_cast<VObject*>(GetArrRef(arr, i)));
          }
        }
      }
    }
  }
#endif
  std::vector<VObject*> stack;
  stack.push_back(self);
  while (!stack.empty()) {
    VObject* cur = stack.back();
    stack.pop_back();
    if (cur == nullptr) continue;
    // 侧表/子树里可能出现已回收对象（裸指针悬空）：解引用 klass 前先验堆起点，
    // 否则 klass 读到复用内存脏值（实证 klass=0x1 → FindInstanceField SEGV）。
    if (state.heap == nullptr || !state.heap->IsHeapStart(cur) || cur->klass == nullptr) {
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "DexVmFind",
                          "BAD node cur=%p on %s target=0x%08x", static_cast<void*>(cur),
                          self != nullptr && self->klass != nullptr
                              ? self->klass->descriptor.c_str()
                              : "?",
                          target);
#endif
      continue;
    }
    VField* id_f = FindInstanceField(cur->klass, "mId");
    if (id_f != nullptr && target != -1 && GetIField(cur, id_f->offset) == target) {
      r.ref = cur;
#if defined(__ANDROID__)
      if (target == 0x01020002) {
        __android_log_print(ANDROID_LOG_INFO, "DexVmFind", "hit 0x01020002 -> %s",
                            cur->klass->descriptor.c_str());
      }
#endif
      return r;
    }
    VField* ch_f = FindInstanceField(cur->klass, "mChildren");
    if (ch_f != nullptr) {
      VArray* arr = reinterpret_cast<VArray*>(GetRefField(cur, ch_f->offset));
      if (arr != nullptr) {
        for (int32_t i = arr->length - 1; i >= 0; i--) {
          ObjHeader* ref = GetArrRef(arr, i);
          if (ref != nullptr) stack.push_back(reinterpret_cast<VObject*>(ref));
        }
      }
    }
  }
#if defined(__ANDROID__)
  if (target == 0x01020002) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmFind", "miss 0x01020002 on %s",
                        self->klass->descriptor.c_str());
  } else if (target != -1) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmFind", "MISS 0x%08x on %s", target,
                        self->klass->descriptor.c_str());
  }
#endif
  return r;
}

// combineMeasuredStates(int,int)：静态合并（AOSP：位或）。
Value ViewCombineMeasuredStates(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  r.i = (n >= 2) ? (args[0].i | args[1].i) : 0;
  return r;
}

// resolveSize(int,int)：静态 MeasureSpec 收敛（AOSP View.resolveSize 同口径）。
Value ViewResolveSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2) return r;
  int32_t size = args[0].i;
  int32_t spec = args[1].i;
  int32_t spec_mode = spec & ~0x3fffffff;
  int32_t spec_size = spec & 0x3fffffff;
  switch (spec_mode) {
    case 0x40000000:  // EXACTLY
      r.i = spec_size;
      break;
    case static_cast<int32_t>(0x80000000u):  // AT_MOST
      r.i = size < spec_size ? size : spec_size;
      break;
    default:
      r.i = size;
      break;
  }
  return r;
}

// ---- ViewGroup 长尾面 ----

Value VGGetPaddingLeft(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mPaddingLeft");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value VGGetPaddingTop(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mPaddingTop");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value VGGetPaddingRight(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mPaddingRight");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

Value VGGetPaddingBottom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mPaddingBottom");
  if (f != nullptr) r.i = GetIField(self, f->offset);
  return r;
}

// removeView(View)：从 mChildren 移除（保留序）。
Value VGRemoveView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  VField* f = FindInstanceField(self->klass, "mChildren");
  if (f == nullptr) return Value{};
  VArray* old = reinterpret_cast<VArray*>(GetRefField(self, f->offset));
  if (old == nullptr) return Value{};
  int32_t count = old->length;
  int32_t idx = -1;
  for (int32_t i = 0; i < count; i++) {
    if (GetArrRef(old, i) == args[0].ref) {
      idx = i;
      break;
    }
  }
  if (idx < 0) return Value{};
  // AOSP：移除后 child.mParent = null。
  if (VObject* rc = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref))) {
    if (VField* pf = FindInstanceField(rc->klass, "mParent")) SetRefField(rc, pf->offset, nullptr);
  }
  VClass* view_cls = state.linker->FindClass("Landroid/view/View;");
  VClass* arr_cls = view_cls != nullptr ? state.linker->FindArrayClass(view_cls) : nullptr;
  VArray* arr = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, count - 1) : nullptr;
  if (arr == nullptr) return Value{};
  for (int32_t i = 0, j = 0; i < count; i++) {
    if (i != idx) SetArrRef(arr, j++, GetArrRef(old, i));
  }
  SetRefField(self, f->offset, reinterpret_cast<ObjHeader*>(arr));
  return Value{};
}

// getChildMeasureSpec(int,int,int)：静态（AOSP 同口径）。
Value VGGetChildMeasureSpec(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n < 3) return r;
  int32_t spec = args[0].i;
  int32_t padding = args[1].i;
  int32_t child_dim = args[2].i;
  int32_t spec_mode = spec & ~0x3fffffff;
  int32_t spec_size = spec & 0x3fffffff;
  int32_t size = spec_size - padding;
  if (size < 0) size = 0;
  if (child_dim >= 0) {
    r.i = (child_dim & 0x3fffffff) | 0x40000000;  // EXACTLY
    return r;
  }
  if (spec_mode == 0) {
    // AOSP：父 UNSPECIFIED 时子 MATCH_PARENT/WRAP_CONTENT 均为 UNSPECIFIED(0)
    r.i = 0;
    return r;
  }
  if (child_dim == -1) {
    r.i = size | 0x40000000;  // MATCH_PARENT -> EXACTLY(剩余)
    return r;
  }
  r.i = size | static_cast<int32_t>(0x80000000u);  // WRAP_CONTENT -> AT_MOST(剩余)
  return r;
}

// measureChild(View,int,int)：AOSP ViewGroup.measureChild（无 margin 版）。
Value VGMeasureChild(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 3 || args[0].ref == nullptr) return Value{};
  VObject* child = reinterpret_cast<VObject*>(args[0].ref);
  int32_t pad_h = 0;
  int32_t pad_v = 0;
  VField* f = FindInstanceField(self->klass, "mPaddingLeft");
  if (f != nullptr) pad_h += GetIField(self, f->offset);
  f = FindInstanceField(self->klass, "mPaddingRight");
  if (f != nullptr) pad_h += GetIField(self, f->offset);
  f = FindInstanceField(self->klass, "mPaddingTop");
  if (f != nullptr) pad_v += GetIField(self, f->offset);
  f = FindInstanceField(self->klass, "mPaddingBottom");
  if (f != nullptr) pad_v += GetIField(self, f->offset);
  int32_t lw = -1;
  int32_t lh = -1;
  VField* lp_f = FindInstanceField(child->klass, "mLayoutParams");
  if (lp_f != nullptr) {
    VObject* lp = reinterpret_cast<VObject*>(GetRefField(child, lp_f->offset));
    if (lp != nullptr) {
      VField* wf = FindInstanceField(lp->klass, "width");
      VField* hf = FindInstanceField(lp->klass, "height");
      if (wf != nullptr) lw = GetIField(lp, wf->offset);
      if (hf != nullptr) lh = GetIField(lp, hf->offset);
    }
  }
  Value ms_args[3];
  ms_args[0].i = args[1].i;
  ms_args[1].i = pad_h;
  ms_args[2].i = lw;
  Value cw{};
  {
    Value a3[3] = {ms_args[0], ms_args[1], ms_args[2]};
    bool tok = true;
    cw = VGGetChildMeasureSpec(state, nullptr, a3, 3, &tok);
    if (!tok) {
      *ok = false;
      return Value{};
    }
  }
  ms_args[0].i = args[2].i;
  ms_args[1].i = pad_v;
  ms_args[2].i = lh;
  Value ch{};
  {
    Value a3[3] = {ms_args[0], ms_args[1], ms_args[2]};
    bool tok = true;
    ch = VGGetChildMeasureSpec(state, nullptr, a3, 3, &tok);
    if (!tok) {
      *ok = false;
      return Value{};
    }
  }
  Value call[2];
  call[0] = cw;
  call[1] = ch;
  if (!CallVirtual(state, child, "measure", {"I", "I"}, "V", call, 2, nullptr)) {
    *ok = false;
  }
  return Value{};
}

// ---- android.view.View$MeasureSpec（内建内嵌类）----

Value MSMakeMeasureSpec(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n >= 2) {
    r.i = (args[0].i & 0x3fffffff) | args[1].i;
  }
  return r;
}

Value MSGetMode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n >= 1) r.i = args[0].i & ~0x3fffffff;
  return r;
}

Value MSGetSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  *ok = true;
  Value r{};
  if (n >= 1) r.i = args[0].i & 0x3fffffff;
  return r;
}

// ---- ViewGroup$LayoutParams / $MarginLayoutParams / LinearLayout$LayoutParams ----

Value LPInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  // 字段零初始化由分配点负责；构造器不得清整实例区（见 ViewInit 注记）。
  if (n >= 2) {
    VField* wf = FindInstanceField(self->klass, "width");
    VField* hf = FindInstanceField(self->klass, "height");
    if (wf != nullptr) SetIField(self, wf->offset, args[0].i);
    if (hf != nullptr) SetIField(self, hf->offset, args[1].i);
  }
  return Value{};
}

Value LPInitAttrs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  // 字段零初始化由分配点负责；构造器不得清整实例区（见 ViewInit 注记）。
  VField* wf = FindInstanceField(self->klass, "width");
  VField* hf = FindInstanceField(self->klass, "height");
  if (wf != nullptr) SetIField(self, wf->offset, -2);  // WRAP_CONTENT
  if (hf != nullptr) SetIField(self, hf->offset, -2);
  return Value{};
}

// View.getWindowInsetsController()（API 30）：androidx WindowInsetsControllerCompat
// Impl30 直接 .show()——恒非 null（每次新 no-op 控制器）。
Value ViewGetWindowInsetsController(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* k = state.linker != nullptr
                  ? state.linker->FindClass("Landroid/view/WindowInsetsControllerImpl;")
                  : nullptr;
  if (k == nullptr) return r;
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocObject(k));
  return r;
}

// TextView.getFilters()：AOSP 恒非 null（EMPTY_FILTERS）——androidx
// AppCompatTextView 合并过滤器时直接 .length，null 即 NPE（工厂创建失败实证）。
Value TVGetFilters(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* iface = state.linker != nullptr ? state.linker->FindClass("Landroid/text/InputFilter;")
                                          : nullptr;
  if (iface == nullptr) return r;
  VClass* arr_cls = state.linker->FindArrayClass(iface);
  if (arr_cls == nullptr) return r;
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocArray(arr_cls, 0));
  return r;
}

Value VGGenerateLayoutParams(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  *ok = true;
  Value r{};
  if (self == nullptr || self->klass == nullptr) return r;
  // LP 类：沿类链找最近存在的「容器类$LayoutParams」（TabView 等无自有 LP
  // 的子类回落到 LinearLayout$LayoutParams——直落 ViewGroup$LayoutParams 会丢
  // Margin 面，guest (MarginLayoutParams) 强转 CCE（TabLayout$TabView 实证））。
  VClass* lp = nullptr;
  for (VClass* k = self->klass; k != nullptr && lp == nullptr; k = k->super_class) {
    const std::string& kd = k->descriptor;
    if (kd.empty() || kd.back() != ';') continue;
    lp = state.linker->FindClass(kd.substr(0, kd.size() - 1) + "$LayoutParams;");
  }
  if (lp == nullptr) lp = state.linker->FindClass("Landroid/view/ViewGroup$LayoutParams;");
  if (lp == nullptr) return r;
  VObject* o = state.linker->AllocObject(lp);
  if (o == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "generateLayoutParams");
    *ok = false;
    return r;
  }
  bool copied = false;
  for (size_t i = 0; i < n; i++) {
    if (args[i].ref == nullptr) continue;
    VObject* src = reinterpret_cast<VObject*>(args[i].ref);
    static const char* kNames[6] = {"width", "height", "leftMargin", "topMargin",
                                    "rightMargin", "bottomMargin"};
    for (const char* name : kNames) {
      VField* mf = FindInstanceField(o->klass, name);
      VField* smf = FindInstanceField(src->klass, name);
      if (mf != nullptr && smf != nullptr) {
        SetIField(o, mf->offset, GetIField(src, smf->offset));
        copied = true;
      }
    }
  }
  if (!copied) {
    // 无参形（generateDefaultLayoutParams）：AOSP 容器默认 WRAP_CONTENT。
    VField* wf = FindInstanceField(o->klass, "width");
    VField* hf = FindInstanceField(o->klass, "height");
    if (wf != nullptr) SetIField(o, wf->offset, -2);
    if (hf != nullptr) SetIField(o, hf->offset, -2);
  }
  r.ref = o;
  return r;
}

Value VGCheckLayoutParams(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (self == nullptr || self->klass == nullptr || n < 1 || args[0].ref == nullptr) {
    r.i = 0;
    return r;
  }
  // AOSP checkLayoutParams：p 是否为容器的 LP 类型——沿类链找最近的
  // 「容器类$LayoutParams」（与 VGGenerateLayoutParams 同口径），并接受其子类。
  VObject* p = reinterpret_cast<VObject*>(args[0].ref);
  VClass* expected = nullptr;
  for (VClass* k = self->klass; k != nullptr && expected == nullptr; k = k->super_class) {
    const std::string& kd = k->descriptor;
    if (kd.empty() || kd.back() != ';') continue;
    expected = state.linker->FindClass(kd.substr(0, kd.size() - 1) + "$LayoutParams;");
  }
  bool fits = false;
  if (p != nullptr && p->klass != nullptr) {
    if (expected == nullptr) {
      fits = true;  // 无约定类型：宽容
    } else {
      for (VClass* c = p->klass; c != nullptr; c = c->super_class) {
        if (c == expected) {
          fits = true;
          break;
        }
      }
    }
  }
  r.i = fits ? 1 : 0;
  return r;
}

Value LPInitCopy(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  // 字段零初始化由分配点负责；构造器不得清整实例区（见 ViewInit 注记）。
  if (n >= 1 && args[0].ref != nullptr) {
    VObject* src = reinterpret_cast<VObject*>(args[0].ref);
    static const char* kNames[6] = {"width", "height", "leftMargin", "topMargin",
                                    "rightMargin", "bottomMargin"};
    for (const char* name : kNames) {
      VField* mf = FindInstanceField(self->klass, name);
      VField* smf = FindInstanceField(src->klass, name);
      if (mf != nullptr && smf != nullptr) {
        SetIField(self, mf->offset, GetIField(src, smf->offset));
      }
    }
  }
  return Value{};
}

// ---- android.util.Log ----

// View.postOnAnimation：主线程帧回调（AOSP Choreographer 面）。VM 单协程泵下
// 等价 post 到主 Looper（view.post 语义；帧边界由泵点推进）。
Value ViewPostOnAnimation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ViewPost(state, self, args, n, ok);
}

namespace {
Value LogPrint(VMState& state, VObject* self, const Value* args, size_t n, bool* ok, char level) {
  (void)state;
  (void)self;
  *ok = true;
  std::string tag = n >= 1 ? Mutf8Arg(args[0]) : "";
  std::string msg = n >= 2 ? Mutf8Arg(args[1]) : "";
  std::fprintf(stderr, "%c/%s: %s\n", level, tag.c_str(), msg.c_str());
  std::fflush(stderr);
#if defined(__ANDROID__)
  // 设备诊断面：guest Log.* 同步落 logcat（stderr 在 Android app 进程被丢弃，
  // 缺口归因需要 guest 侧日志可见）。
  int prio = level == 'V' ? ANDROID_LOG_VERBOSE
             : level == 'D' ? ANDROID_LOG_DEBUG
             : level == 'I' ? ANDROID_LOG_INFO
             : level == 'W' ? ANDROID_LOG_WARN
                            : ANDROID_LOG_ERROR;
  __android_log_print(prio, tag.c_str(), "%s", msg.c_str());
#endif
  Value r{};
  r.i = 0;
  return r;
}

}  // namespace
Value LogD(VMState& s, VObject* o, const Value* a, size_t n, bool* ok) {
  return LogPrint(s, o, a, n, ok, 'D');
}
Value LogI(VMState& s, VObject* o, const Value* a, size_t n, bool* ok) {
  return LogPrint(s, o, a, n, ok, 'I');
}
Value LogW(VMState& s, VObject* o, const Value* a, size_t n, bool* ok) {
  return LogPrint(s, o, a, n, ok, 'W');
}
Value LogE(VMState& s, VObject* o, const Value* a, size_t n, bool* ok) {
  return LogPrint(s, o, a, n, ok, 'E');
}
Value LogV(VMState& s, VObject* o, const Value* a, size_t n, bool* ok) {
  return LogPrint(s, o, a, n, ok, 'V');
}

// lib 类实例化（ctor 经 InvokeMethod；供 lib 缝组合语义用）
VObject* NewLibObject(VMState& state, const char* desc, std::vector<std::string> params,
                      const Value* args, size_t n, bool* ok) {
  bool sink = true;
  if (ok == nullptr) ok = &sink;  // 调用方可不关心状态（防御空指针解引用）
  VClass* k = state.linker->FindClass(desc);
  if (k == nullptr || !CheckClassDefinition(state, k) ||
      !state.linker->InitializeClass(k, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/NoClassDefFoundError;", desc);
    }
    *ok = false;
    return nullptr;
  }
  VObject* o = state.linker->AllocObject(k);
  if (o == nullptr) {
    ThrowOOM(state, desc);
    *ok = false;
    return nullptr;
  }
  for (auto& m : k->direct_methods) {
    if (m.name == "<init>" && m.param_types == params) {
      std::vector<Value> full;
      Value self_v{};
      self_v.ref = o;
      full.push_back(self_v);
      for (size_t i = 0; i < n; i++) full.push_back(args[i]);
      Value ret{};
      if (!InvokeMethod(state, &m, full.data(), full.size(), &ret) || state.HasPendingException()) {
        *ok = false;
        return nullptr;
      }
      *ok = true;
      return o;
    }
  }
  state.ThrowNew("Ljava/lang/NoSuchMethodError;", std::string(desc) + ".<init>");
  *ok = false;
  return nullptr;
}

// AOSP Log.getStackTraceString(Throwable)：printStackTrace 输出收集为字符串
// （StringWriter + PrintWriter lib 缝，AOSP 实现形状）；null -> ""。
Value LogGetStackTraceString(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    r.ref = state.linker->AllocString(u"");
    *ok = true;
    return r;
  }
  VObject* tr = reinterpret_cast<VObject*>(args[0].ref);
  bool tok = true;
  Value cap{};
  cap.i = 256;
  VObject* sw = NewLibObject(state, "Ljava/io/StringWriter;", {"I"}, &cap, 1, &tok);
  if (!tok || sw == nullptr) {
    *ok = false;
    return Value{};
  }
  Value w_arg{};
  w_arg.ref = sw;
  VObject* pw =
      NewLibObject(state, "Ljava/io/PrintWriter;", {"Ljava/io/Writer;"}, &w_arg, 1, &tok);
  if (!tok || pw == nullptr) {
    *ok = false;
    return Value{};
  }
  Value pw_arg{};
  pw_arg.ref = pw;
  if (!CallVirtual(state, tr, "printStackTrace", {"Ljava/io/PrintWriter;"}, "V", &pw_arg, 1,
                   nullptr)) {
    *ok = false;
    return Value{};
  }
  if (!CallVirtual(state, pw, "flush", {}, "V", nullptr, 0, nullptr)) {
    *ok = false;
    return Value{};
  }
  Value out{};
  if (!CallVirtual(state, sw, "toString", {}, "Ljava/lang/String;", nullptr, 0, &out)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return out;
}


}  // namespace dexvm::runtime

// ---- 点击回灌通路（bridge/ui_bridge_input.h 实现体）----
// host 点击 -> viewId -> guest listener 注入 -> onClick(View) 派发。
// 注：登记表在本 TU 的匿名段（dexvm::runtime 之上）；活会话才触 guest 堆。
namespace dexvm::bridge {

using dexvm::runtime::CallVirtual;
using dexvm::runtime::ClickEntry;
using dexvm::runtime::ClickRegistry;
using dexvm::runtime::FindClickEntry;
using dexvm::runtime::FindClickListener;
using dexvm::runtime::FindInstanceField;
using dexvm::runtime::GetIField;
using dexvm::runtime::Value;
using dexvm::runtime::VField;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;

void NoteGuestView(VObject* view) {
  if (view == nullptr) return;
  if (FindClickEntry(view) != nullptr) return;
  ClickRegistry().push_back(ClickEntry{view, nullptr});
}

bool HasGuestClickListener(VObject* view) { return FindClickListener(view) != nullptr; }

size_t GuestClickListenerCount() {
  size_t count = 0;
  for (const auto& e : ClickRegistry()) {
    if (e.listener != nullptr) count++;
  }
  return count;
}

void ClearGuestClickListeners() {
  ClickRegistry().clear();
  dexvm::runtime::ResetViewStateRegistries();  // selected/activated/rotation/desc 侧表
}

std::string InjectGuestClick(VMState* vm_state, int32_t view_id) {
  // 会话外（设备侧宿主点击时 guest VM 通常已收场）：只回通路状态，不触 guest 堆。
  if (vm_state == nullptr) {
    return "channel-ok viewId=" + std::to_string(view_id) + " guest-session=absent";
  }
  // viewId -> 登记的 guest view（mId 实时匹配）
  VObject* target = nullptr;
  for (auto& e : ClickRegistry()) {
    if (e.view == nullptr) continue;
    VField* f = FindInstanceField(e.view->klass, "mId");
    if (f != nullptr && GetIField(e.view, f->offset) == view_id) {
      target = e.view;
      break;
    }
  }
  if (target == nullptr) {
    return "no-view viewId=" + std::to_string(view_id);
  }
  VObject* listener = FindClickListener(target);
  if (listener == nullptr) {
    return "dispatched viewId=" + std::to_string(view_id) + " listener=0";
  }
  // guest listener 注入派发：onClick(View)
  Value av{};
  av.ref = target;
  if (!CallVirtual(*vm_state, listener, "onClick", {"Landroid/view/View;"}, "V", &av, 1,
                   nullptr)) {
    std::string diag = "dispatch-error viewId=" + std::to_string(view_id);
    if (vm_state->HasPendingException() && vm_state->pending_exception != nullptr) {
      dexvm::runtime::ObjHeader* ex = vm_state->pending_exception;
      if (ex->klass != nullptr) {
        diag += " " + ex->klass->descriptor;
        for (dexvm::runtime::VClass* k = ex->klass; k != nullptr; k = k->super_class) {
          bool found = false;
          for (auto& f : k->instance_fields) {
            if (f.name != "detailMessage") continue;
            dexvm::runtime::ObjHeader* msg = nullptr;
            std::memcpy(&msg, reinterpret_cast<dexvm::runtime::VObject*>(ex)->field_data() +
                                 f.offset,
                        sizeof(msg));
            if (msg != nullptr) {
              diag += ": " + static_cast<dexvm::runtime::VString*>(msg)->ToModifiedUtf8();
            }
            found = true;
            break;
          }
          if (found) break;
        }
      }
      vm_state->ClearException();
    }
    return diag;
  }
  return "dispatched viewId=" + std::to_string(view_id) + " listener=1";
}

}  // namespace dexvm::bridge
