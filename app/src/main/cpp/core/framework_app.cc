// ActivityThread/Application/Activity/Context/Intent/Uri/ContentResolver/
// ContentProvider/PM/Resources/ProviderInfo 族。纯移动自
// core/framework_builtins.cc（零行为变更；注册仍在 registrar）。

#include "core/framework_builtins.h"

#include <unistd.h>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <dirent.h>
#include <sys/stat.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>
#include <new>
#include <set>
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

// ---- android.content.Context ----

// getPackageName：mBase 链向下取首个非空 mPackageName（AOSP ContextWrapper 委派）。
Value CtxGetPackageName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* c = self;
  while (c != nullptr) {
    VField* name_f = FindInstanceField(c->klass, "mPackageName");
    if (name_f != nullptr) {
      ObjHeader* s = GetRefField(c, name_f->offset);
      if (s != nullptr) {
        r.ref = s;
        return r;
      }
    }
    VField* base_f = FindInstanceField(c->klass, "mBase");
    c = base_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(c, base_f->offset)) : nullptr;
  }
  r.ref = state.linker->AllocString(u"");
  return r;
}

// getApplicationContext：mBase 链上首个非空 mApplicationContext，否则 self。
Value CtxGetApplicationContext(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* c = self;
  while (c != nullptr) {
    VField* app_f = FindInstanceField(c->klass, "mApplicationContext");
    if (app_f != nullptr) {
      ObjHeader* s = GetRefField(c, app_f->offset);
      if (s != nullptr) {
        r.ref = s;
        return r;
      }
    }
    VField* base_f = FindInstanceField(c->klass, "mBase");
    c = base_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(c, base_f->offset)) : nullptr;
  }
  r.ref = self;
  return r;
}

Value CtxGetBaseContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* base_f = FindInstanceField(self->klass, "mBase");
  if (base_f != nullptr) r.ref = GetRefField(self, base_f->offset);
  return r;
}

// ---- android.content.ContextWrapper ----

Value CWInitBase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    VField* base_f = FindInstanceField(self->klass, "mBase");
    if (base_f != nullptr) SetRefField(self, base_f->offset, args[0].ref);
  }
  return Value{};
}

// attachBaseContext(Context)：ContextWrapper 语义（mBase = base）。
Value CWAttachBaseContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* base_f = FindInstanceField(self->klass, "mBase");
    if (base_f != nullptr) SetRefField(self, base_f->offset, args[0].ref);
  }
  return Value{};
}

// ---- android.app.Activity ----

// attach(Context, ActivityThread, Instrumentation, Intent)：ActivityThread
// 驱动的最小 attach 语义（AOSP hidden attach 的收敛面，docs 19.3）。
Value ActAttach(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 1) {
    VField* base_f = FindInstanceField(self->klass, "mBase");
    if (base_f != nullptr) SetRefField(self, base_f->offset, args[0].ref);
    // AOSP Activity.attach -> attachBaseContext(base)：子类覆写（壳/SDK 初始化
    // 钩子）必须经虚分发执行（MainActivity 等在此构建 delegate）。
    VMethod probe;
    probe.name = "attachBaseContext";
    probe.param_types = {"Landroid/content/Context;"};
    probe.return_type = "V";
    if (VMethod* m = self->klass->FindInterfaceImpl(&probe)) {
      Value av[2] = {};
      av[0].ref = self;
      av[1].ref = args[0].ref;
      Value r{};
      if (!InvokeMethod(state, m, av, 2, &r)) {
        *ok = false;
        return Value{};
      }
    }
  }
  if (n >= 2) {
    VField* thread_f = FindInstanceField(self->klass, "mActivityThread");
    if (thread_f != nullptr) SetRefField(self, thread_f->offset, args[1].ref);
  }
  if (n >= 3) {
    VField* instr_f = FindInstanceField(self->klass, "mInstrumentation");
    if (instr_f != nullptr) SetRefField(self, instr_f->offset, args[2].ref);
  }
  if (n >= 4) {
    VField* intent_f = FindInstanceField(self->klass, "mIntent");
    if (intent_f != nullptr) SetRefField(self, intent_f->offset, args[3].ref);
  }
  return Value{};
}

Value ActSetContentView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* cv_f = FindInstanceField(self->klass, "mContentView");
    if (cv_f != nullptr) SetRefField(self, cv_f->offset, args[0].ref);
  }
  return Value{};
}

// Activity.getWindow()：窗口对象单例（lib android.view.Window 面；窗口系统
// 归桥接层，getDecorView/setContentView 由 VM 覆写承接——见 BindWellKnownNatives）
Value ActGetWindow(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* win_f = FindInstanceField(self->klass, "mWindow");
  if (win_f == nullptr) return r;
  VObject* existing = reinterpret_cast<VObject*>(GetRefField(self, win_f->offset));
  if (existing != nullptr) {
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DexVmWindow", "getWindow act=%p reuse win=%p",
                        (void*)self, (void*)existing);
#endif
    NoteWindowOwner(existing, self);
    if (state.heap != nullptr) state.heap->Pin(existing);  // 侧表键：GC 根
    r.ref = existing;
    return r;
  }
  VClass* window_cls = state.linker->FindClass("Landroid/view/Window;");
  if (window_cls == nullptr || !CheckClassDefinition(state, window_cls) ||
      !state.linker->InitializeClass(window_cls, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "android.view.Window missing");
    }
    *ok = false;
    return Value{};
  }
  VObject* window = state.linker->AllocObject(window_cls);
  if (window == nullptr) {
    ThrowOOM(state, "Activity.getWindow");
    *ok = false;
    return Value{};
  }
  SetRefField(self, win_f->offset, reinterpret_cast<ObjHeader*>(window));
  // AOSP Activity.attach：Window.setCallback(this)——Activity 即 Window.Callback
  NoteWindowOwner(window, self);
  // 侧表（g_window_callback/g_decor_by_window/g_content_by_window）只存裸
  // 指针，GC 不扫 C++ 容器；窗口对象必须钉住，否则键地址被复用时侧表命中
  // 错误条目，decor 也可能随窗口一起被回收（findViewById 遍历悬空树 → SEGV）。
  if (state.heap != nullptr) state.heap->Pin(window);
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmWindow", "getWindow act=%p NEW win=%p", (void*)self,
                      (void*)window);
#endif
  r.ref = window;
  return r;
}

// Window.getDecorView()：decor View（宿主桥接物化面）；每窗口惰性单例。
// Window decor（AOSP PhoneWindow 形制）：decor FrameLayout + 内容父容器
// （android.R.id.content=0x01020002）。AppCompat createSubDecor 依赖该内容父
// 容器存在（window.findViewById(android.R.id.content)），随后把 subDecor 作为
// 窗口内容（window.setContentView）——本 VM 把窗口内容落回 Activity.mContentView
// （桥接出画的内容根）。
namespace {
std::unordered_map<ObjHeader*, ObjHeader*> g_window_callback;
std::unordered_map<ObjHeader*, VObject*> g_decor_by_window;
std::unordered_map<ObjHeader*, VObject*> g_content_by_window;  // window -> content parent
constexpr int32_t kAndroidRIdContent = 0x01020002;

VObject* MakeGroup(VMState& state, const char* desc, VObject* context, int32_t id) {
  VClass* cls = state.linker->FindClass(desc);
  if (cls == nullptr || !CheckClassDefinition(state, cls) ||
      !state.linker->InitializeClass(cls, &state)) {
    state.ClearException();
    return nullptr;
  }
  VObject* v = state.linker->AllocObject(cls);
  if (v == nullptr) return nullptr;
  for (auto& m : cls->direct_methods) {
    if (m.name != "<init>") continue;
    Value cargs[3] = {};
    cargs[0].ref = v;
    size_t an = 1;
    bool match = m.param_types.empty();
    if (!match && m.param_types[0] == "Landroid/content/Context;") {
      cargs[1].ref = context;
      an = 2;
      match = m.param_types.size() == 1;
      if (!match && m.param_types.size() == 2 &&
          m.param_types[1] == "Landroid/util/AttributeSet;") {
        match = true;  // attrs null 面
      }
    }
    if (!match) continue;
    Value ret{};
    if (!InvokeMethod(state, &m, cargs, an, &ret)) {
      state.ClearException();
      continue;
    }
    if (id != 0) {
      if (VField* f = FindInstanceField(cls, "mId")) SetIField(v, f->offset, id);
    }
    return v;
  }
  return nullptr;
}

void AddChildV(VMState& state, VObject* parent, VObject* child) {
  VMethod probe;
  probe.name = "addView";
  probe.param_types = {"Landroid/view/View;"};
  probe.return_type = "V";
  if (parent == nullptr || child == nullptr) return;
  VMethod* m = parent->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return;
  Value av[2] = {};
  av[0].ref = parent;
  av[1].ref = reinterpret_cast<ObjHeader*>(child);
  Value r{};
  InvokeMethod(state, m, av, 2, &r);
}

VObject* WindowOwnerActivity(VObject* window) {
  auto it = g_window_callback.find(reinterpret_cast<ObjHeader*>(window));
  return it != g_window_callback.end() ? reinterpret_cast<VObject*>(it->second) : nullptr;
}

void SetActivityContentView(VObject* activity, VObject* view) {
  if (activity == nullptr) return;
  if (VField* f = FindInstanceField(activity->klass, "mContentView")) {
    SetRefField(activity, f->offset, reinterpret_cast<ObjHeader*>(view));
  }
}
}  // namespace

Value WindowGetDecorView(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_decor_by_window.find(reinterpret_cast<ObjHeader*>(self));
  if (it != g_decor_by_window.end()) {
    r.ref = it->second;
    return r;
  }
  VObject* activity = WindowOwnerActivity(self);
  VObject* decor = MakeGroup(state, "Landroid/widget/FrameLayout;", activity, 0);
  if (decor == nullptr) {
    decor = MakeGroup(state, "Landroid/view/View;", activity, 0);
  }
  if (decor == nullptr) {
    ThrowOOM(state, "Window.getDecorView");
    *ok = false;
    return Value{};
  }
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmWindow", "win=%p getDecorView build owner=%p",
                      (void*)self, (void*)WindowOwnerActivity(self));
#endif
  // 内容父容器（android.R.id.content）：AppCompat 从窗口搬移内容用
  VObject* content = MakeGroup(state, "Landroid/widget/FrameLayout;", activity, kAndroidRIdContent);
  if (content != nullptr) {
    AddChildV(state, decor, content);
    g_content_by_window[reinterpret_cast<ObjHeader*>(self)] = content;
    // decor/content 只被侧表与子树引用：钉住防 GC 回收后裸指针悬空。
    if (state.heap != nullptr) {
      state.heap->Pin(decor);
      state.heap->Pin(content);
    }
    // Activity 已有内容（非 AppCompat 路径先 setContentView）时挂入
    VObject* existing = nullptr;
    if (activity != nullptr) {
      if (VField* f = FindInstanceField(activity->klass, "mContentView")) {
        existing = reinterpret_cast<VObject*>(GetRefField(activity, f->offset));
      }
    }
    if (existing != nullptr) AddChildV(state, content, existing);
  }
  g_decor_by_window[reinterpret_cast<ObjHeader*>(self)] = decor;
  if (state.heap != nullptr) state.heap->Pin(decor);  // 侧表值：GC 根
  r.ref = reinterpret_cast<ObjHeader*>(decor);
  return r;
}

// Window.findViewById(id)：decor 树优先；未命中回落 owner Activity 内容树
// （AppCompat 在窗口内容父容器搬移完成后用新内容根查询的场景）。
Value WindowFindViewById(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  auto it = g_decor_by_window.find(reinterpret_cast<ObjHeader*>(self));
  if (it != g_decor_by_window.end()) {
    VObject* decor = it->second;
    Value found = ViewFindViewById(state, decor, args, 1, ok);
#if defined(__ANDROID__)

#endif
    if (found.ref != nullptr) return found;
  }
  VObject* activity = WindowOwnerActivity(self);
#if defined(__ANDROID__)

#endif
  if (activity != nullptr) {
    if (VField* f = FindInstanceField(activity->klass, "mContentView")) {
      VObject* content = reinterpret_cast<VObject*>(GetRefField(activity, f->offset));
#if defined(__ANDROID__)

#endif
      if (content != nullptr) {
        Value found = ViewFindViewById(state, content, args, 1, ok);
#if defined(__ANDROID__)

#endif
        return found;
      }
    }
  }
  return r;
}

Value WindowSetContentView(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  // AOSP Window.setContentView(View)：窗口内容 = view；本 VM 内容根 =
  // owner Activity.mContentView（桥接出画消费面）。AppCompat 路径传 subDecor。
  VObject* activity = WindowOwnerActivity(self);
  if (activity == nullptr) return Value{};
  VObject* view = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmWindow", "win=%p setContentView(%s) owner=%p",
                      (void*)self, view->klass != nullptr ? view->klass->descriptor.c_str() : "?",
                      (void*)activity);
#endif
  SetActivityContentView(activity, view);
  return Value{};
}

// Window callback 面（AOSP：Activity.attach 内 setCallback(this)；AppCompat 的
// WindowCallbackWrapper 断言 callback 非空）。Window 是 lib 类无实例槽——
// 按窗口对象地址侧表；ActGetWindow 落 activity 为默认 callback
// （g_window_callback 声明见上：decor/content 侧表层内共用）。

void NoteWindowOwner(VObject* window, VObject* activity) {
  if (window == nullptr || activity == nullptr) return;
  g_window_callback[reinterpret_cast<ObjHeader*>(window)] =
      reinterpret_cast<ObjHeader*>(activity);
}

void ResetWindowCallbacks() {
  g_window_callback.clear();
  g_decor_by_window.clear();
  g_content_by_window.clear();
}

Value WindowGetCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_window_callback.find(reinterpret_cast<ObjHeader*>(self));
  if (it != g_window_callback.end()) r.ref = it->second;
  return r;
}

// Window 装饰/系统栏/标志面：宿主窗口归桥接层，这些调用为语义空转
// （AOSP Window 状态面；AppCompat/inset 链只要求可调用）。
Value WindowSetDecorFitsSystemWindows(VMState& state, VObject* self, const Value* args,
                                      size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

Value WindowSetStatusBarColor(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

Value WindowSetCallback(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 1) {
    g_window_callback[reinterpret_cast<ObjHeader*>(self)] =
        const_cast<ObjHeader*>(args[0].ref);
    // callback（Activity/包装器）只被侧表引用：钉住防 GC 回收后悬空。
    if (state.heap != nullptr) {
      state.heap->Pin(self);
      if (args[0].ref != nullptr) state.heap->Pin(args[0].ref);
    }
  }
  return Value{};
}

// Resources.getConfiguration()：设备配置单例（lib Configuration 对象；
// 字段取宿主默认值——AOSP 每 Resources 一份）。
Value ResGetConfiguration(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* cfg_f = FindInstanceField(self->klass, "mConfiguration");
  if (cfg_f == nullptr) return r;
  VObject* existing = reinterpret_cast<VObject*>(GetRefField(self, cfg_f->offset));
  if (existing != nullptr) {
    r.ref = existing;
    return r;
  }
  VClass* cfg_cls = state.linker->FindClass("Landroid/content/res/Configuration;");
  if (cfg_cls == nullptr || !CheckClassDefinition(state, cfg_cls) ||
      !state.linker->InitializeClass(cfg_cls, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "Configuration missing");
    }
    *ok = false;
    return Value{};
  }
  VObject* cfg = state.linker->AllocObject(cfg_cls);
  if (cfg == nullptr) {
    ThrowOOM(state, "Resources.getConfiguration");
    *ok = false;
    return Value{};
  }
  VMethod* ctor = nullptr;
  for (auto& m : cfg_cls->direct_methods) {
    if (m.name == "<init>" && m.param_types.empty() && !m.IsStatic()) {
      ctor = &m;
      break;
    }
  }
  if (ctor != nullptr) {
    Value cargs[1] = {};
    cargs[0].ref = cfg;
    Value ignored{};
    if (!InvokeMethod(state, ctor, cargs, 1, &ignored)) {
      *ok = false;
      return Value{};
    }
  }
  SetRefField(self, cfg_f->offset, reinterpret_cast<ObjHeader*>(cfg));
  r.ref = cfg;
  return r;
}

Value ActGetIntent(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* intent_f = FindInstanceField(self->klass, "mIntent");
  if (intent_f != nullptr) r.ref = GetRefField(self, intent_f->offset);
  return r;
}

Value ActGetApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* thread_f = FindInstanceField(self->klass, "mActivityThread");
  VObject* thread =
      thread_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(self, thread_f->offset))
                          : nullptr;
  if (thread != nullptr) {
    VField* app_f = FindInstanceField(thread->klass, "mInitialApplication");
    if (app_f != nullptr) r.ref = GetRefField(thread, app_f->offset);
  }
  (void)state;
  return r;
}

// ---- android.app.Instrumentation ----

// newApplication(Class, Context)：实例化 + ctor + attachBaseContext（AOSP 语义）。
Value InstrNewApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "newApplication class == null");
    *ok = false;
    return r;
  }
  VClass* klass = reinterpret_cast<VClass*>(args[0].ref);
  if (!CheckClassDefinition(state, klass)) {
    *ok = false;
    return r;
  }
  if (!state.linker->InitializeClass(klass, &state)) {
    *ok = false;
    return r;
  }
  VObject* app = state.linker->AllocObject(klass);
  if (app == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "newApplication alloc failed");
    *ok = false;
    return r;
  }
  VMethod probe;
  probe.name = "<init>";
  probe.return_type = "V";
  VMethod* ctor = klass->FindInterfaceImpl(&probe);
  if (ctor != nullptr) {
    Value self_v{};
    self_v.ref = app;
    Value cr{};
    if (!InvokeMethod(state, ctor, &self_v, 1, &cr)) {
      *ok = false;
      return r;
    }
  }
  Value ctx{};
  ctx.ref = args[1].ref;
  if (!CallVirtual(state, app, "attachBaseContext", {"Landroid/content/Context;"}, "V", &ctx, 1,
                   nullptr)) {
    *ok = false;
    return r;
  }
  r.ref = app;
  return r;
}

Value InstrCallApplicationOnCreate(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)self;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    if (!CallVirtual(state, reinterpret_cast<VObject*>(args[0].ref), "onCreate", {}, "V", nullptr,
                     0, nullptr)) {
      *ok = false;
    }
  }
  return Value{};
}

Value InstrCallActivityOnCreate(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)self;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    Value bundle{};
    if (n >= 2) bundle = args[1];
    if (!CallVirtual(state, reinterpret_cast<VObject*>(args[0].ref), "onCreate",
                     {"Landroid/os/Bundle;"}, "V", &bundle, 1, nullptr)) {
      *ok = false;
    }
  }
  return Value{};
}

// ---- android.app.ActivityThread ----

namespace {
VObject* CurrentActivityThread(ClassLinker* linker) {
  VClass* at = linker->FindClass("Landroid/app/ActivityThread;");
  if (at == nullptr || at->static_fields.empty()) return nullptr;
  return reinterpret_cast<VObject*>(at->static_storage[0].ref);
}

}  // namespace
Value ATCurrentActivityThread(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = CurrentActivityThread(state.linker);
  return r;
}

// ---- android.content.ComponentName（AOSP 值对象；PM 查询/壳反射面）----
namespace {
void SetStrField(VMState& state, VObject* obj, const char* name, const std::string& v) {
  if (obj == nullptr) return;
  VField* f = FindInstanceField(obj->klass, name);
  if (f == nullptr) return;
  SetRefField(obj, f->offset,
              reinterpret_cast<ObjHeader*>(state.linker->AllocString(
                  std::u16string(v.begin(), v.end()))));
}
std::string GetStrField(VObject* obj, const char* name) {
  if (obj == nullptr) return "";
  VField* f = FindInstanceField(obj->klass, name);
  ObjHeader* s = f != nullptr ? GetRefField(obj, f->offset) : nullptr;
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : "";
}
}  // namespace

Value ComponentNameInit2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 2) {
    SetStrField(state, self, "mPackage", Mutf8Arg(args[0]));
    SetStrField(state, self, "mClass", Mutf8Arg(args[1]));
  }
  return Value{};
}

Value ComponentNameInitCtx(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 2) return Value{};
  std::string pkg;
  if (args[0].ref != nullptr) {
    VObject* ctx = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
    VField* f = FindInstanceField(ctx->klass, "mPackageName");
    ObjHeader* s = f != nullptr ? GetRefField(ctx, f->offset) : nullptr;
    if (s != nullptr) pkg = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  SetStrField(state, self, "mPackage", pkg);
  SetStrField(state, self, "mClass", Mutf8Arg(args[1]));
  return Value{};
}

Value ComponentNameInitCtxClass(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  *ok = true;
  if (n < 2 || args[1].ref == nullptr) return Value{};
  std::string pkg;
  if (args[0].ref != nullptr) {
    VObject* ctx = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
    VField* f = FindInstanceField(ctx->klass, "mPackageName");
    ObjHeader* s = f != nullptr ? GetRefField(ctx, f->offset) : nullptr;
    if (s != nullptr) pkg = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  VClass* target = reinterpret_cast<VClass*>(const_cast<ObjHeader*>(args[1].ref));
  std::string cls = target->descriptor;
  if (cls.size() >= 2 && cls.front() == 'L' && cls.back() == ';') {
    cls = cls.substr(1, cls.size() - 2);
  }
  for (char& c : cls) {
    if (c == '/') c = '.';
  }
  SetStrField(state, self, "mPackage", pkg);
  SetStrField(state, self, "mClass", cls);
  return Value{};
}

Value ComponentNameGetPackageName(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  std::string pkg = GetStrField(self, "mPackage");
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(pkg.begin(), pkg.end())));
  return r;
}

Value ComponentNameGetClassName(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  std::string cls = GetStrField(self, "mClass");
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(cls.begin(), cls.end())));
  return r;
}

Value ComponentNameFlattenToString(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  std::string s = GetStrField(self, "mPackage") + "/" + GetStrField(self, "mClass");
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(s.begin(), s.end())));
  return r;
}

Value ComponentNameEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  VObject* other = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  r.i = (GetStrField(self, "mPackage") == GetStrField(other, "mPackage") &&
         GetStrField(self, "mClass") == GetStrField(other, "mClass"))
            ? 1
            : 0;
  return r;
}

Value ComponentNameUnflatten(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  std::string flat = Mutf8Arg(args[0]);
  size_t slash = flat.find('/');
  if (slash == std::string::npos) return r;
  std::string pkg = flat.substr(0, slash);
  std::string cls = flat.substr(slash + 1);
  if (!cls.empty() && cls[0] == '.') cls = pkg + cls;
  VClass* k = state.linker->FindClass("Landroid/content/ComponentName;");
  if (k == nullptr) return r;
  VObject* cn = state.linker->AllocObject(k);
  if (cn == nullptr) return r;
  SetStrField(state, cn, "mPackage", pkg);
  SetStrField(state, cn, "mClass", cls);
  r.ref = reinterpret_cast<ObjHeader*>(cn);
  return r;
}

// Activity.register/unregisterActivityLifecycleCallbacks：AOSP 委派 Application。
// 回调表当前为登记面（生命周期事件分发后置；壳/兼容链只关心调用成功）。
Value ActLifecycleCallbacks(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// Activity.startActivity 族：guest Intent 启动链的可观测落点。VM 单 Activity
// 生命周期（无 Activity 栈）：记录意图（进度日志），不切换生命周期阶段。
Value ActStartActivityImpl(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    VObject* intent = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
    std::string pkg;
    std::string cls;
    for (VClass* k = intent->klass; k != nullptr; k = k->super_class) {
      for (auto& f : k->instance_fields) {
        if (f.name != "mComponentClass" && f.name != "mComponentPackage" &&
            f.name != "mAction") {
          continue;
        }
        ObjHeader* s = GetRefField(intent, f.offset);
        if (s == nullptr) continue;
        std::string v = static_cast<VString*>(s)->ToModifiedUtf8();
        if (f.name == "mComponentClass") cls = v;
        else if (f.name == "mComponentPackage") pkg = v;
        else if (cls.empty() && pkg.empty()) cls = "action=" + v;
      }
    }
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DexVmIntent", "startActivity %s/%s",
                        pkg.c_str(), cls.c_str());
#else
    std::fprintf(stderr, "startActivity %s/%s\n", pkg.c_str(), cls.c_str());
#endif
    // AOSP：AMS 后续调度目标 Activity 生命周期。VM 单线程协程模型 = 请求排队，
    // 当前 Activity 生命周期阶段结束后由 LifecycleDriver 排空（见
    // TakePendingActivityLaunch）——等价 ActivityThread 的 launch 调度点。
    if (!cls.empty() && cls.rfind("action=", 0) != 0) {
      SetPendingActivityLaunch(pkg, cls, state, intent);
    }
  }
  return Value{};
}

// ---- 待启动 Activity 请求（startActivity -> 驱动排空；AOSP AMS 调度等价面）----
namespace {
std::string g_pending_pkg;
std::string g_pending_class;
ObjHeader* g_pending_intent = nullptr;
}  // namespace

void SetPendingActivityLaunch(const std::string& pkg, const std::string& cls, VMState& state,
                              VObject* intent) {
  g_pending_pkg = pkg;
  g_pending_class = cls;
  g_pending_intent = reinterpret_cast<ObjHeader*>(intent);
  if (state.heap != nullptr && intent != nullptr) state.heap->Pin(intent);  // 请求期 GC 根
}

bool TakePendingActivityLaunch(std::string* pkg, std::string* cls, ObjHeader** intent) {
  if (g_pending_class.empty()) return false;
  if (pkg != nullptr) *pkg = g_pending_pkg;
  if (cls != nullptr) *cls = g_pending_class;
  if (intent != nullptr) *intent = g_pending_intent;
  g_pending_pkg.clear();
  g_pending_class.clear();
  g_pending_intent = nullptr;
  return true;
}

void ClearPendingActivityLaunch() {
  g_pending_pkg.clear();
  g_pending_class.clear();
  g_pending_intent = nullptr;
}

// Activity.getComponentName / getLocalClassName：AOSP = ComponentName(this, 类名)。
Value ActGetComponentName(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* cn_cls = state.linker->FindClass("Landroid/content/ComponentName;");
  if (cn_cls == nullptr || !CheckClassDefinition(state, cn_cls) ||
      !state.linker->InitializeClass(cn_cls, &state)) {
    state.ClearException();
    *ok = false;
    return r;
  }
  VObject* cn = state.linker->AllocObject(cn_cls);
  if (cn == nullptr) {
    *ok = false;
    return r;
  }
  // ComponentName(Context, String)：Activity 的 mPackageName + 实例类名
  std::string pkg;
  for (VClass* k = self->klass; k != nullptr && pkg.empty(); k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == "mPackageName") {
        ObjHeader* s = GetRefField(self, f.offset);
        if (s != nullptr) pkg = static_cast<VString*>(s)->ToModifiedUtf8();
        break;
      }
    }
  }
  std::string cls = self->klass != nullptr ? self->klass->descriptor : "";
  if (cls.size() >= 2 && cls.front() == 'L' && cls.back() == ';') {
    cls = cls.substr(1, cls.size() - 2);
  }
  for (char& c : cls) {
    if (c == '/') c = '.';
  }
  VMethod* ctor = nullptr;
  for (auto& m : cn_cls->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 2 &&
        m.param_types[0] == "Ljava/lang/String;" &&
        m.param_types[1] == "Ljava/lang/String;") {
      ctor = &m;
      break;
    }
  }
  if (ctor != nullptr) {
    Value cargs[3] = {};
    cargs[0].ref = cn;
    cargs[1].ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(
        std::u16string(pkg.begin(), pkg.end())));
    cargs[2].ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(
        std::u16string(cls.begin(), cls.end())));
    Value ret{};
    if (!InvokeMethod(state, ctor, cargs, 3, &ret)) {
      *ok = false;
      return r;
    }
  }
  r.ref = reinterpret_cast<ObjHeader*>(cn);
  return r;
}

Value ActGetLocalClassName(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  std::string cls = self->klass != nullptr ? self->klass->descriptor : "";
  if (cls.size() >= 2 && cls.front() == 'L' && cls.back() == ';') {
    cls = cls.substr(1, cls.size() - 2);
  }
  for (char& c : cls) {
    if (c == '/') c = '.';
  }
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(cls.begin(), cls.end())));
  return r;
}

// Application.registerComponentCallbacks / unregisterComponentCallbacks
// （AOSP Application.mComponentCallbacks；当前 VM 无配置变更/低内存事件源，
// 注册表保留，事件分发后置）。
namespace {
std::map<VObject*, std::vector<VObject*>> g_component_callbacks;
// IntentFilter 值面（action/category 列表；广播派发源后置）
std::map<VObject*, std::vector<std::string>> g_filter_actions;
std::map<VObject*, std::vector<std::string>> g_filter_categories;
}

// 新 VM 引导复位（跨会话不留旧堆回调指针）
void ResetComponentCallbacks() {
  g_component_callbacks.clear();
  g_filter_actions.clear();
  g_filter_categories.clear();
}

Value IntentFilterInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  g_filter_actions[self].clear();
  g_filter_categories[self].clear();
  if (n >= 1 && args[0].ref != nullptr) {
    g_filter_actions[self].push_back(static_cast<VString*>(args[0].ref)->ToModifiedUtf8());
  }
  return Value{};
}

Value IntentFilterAddAction(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    g_filter_actions[self].push_back(static_cast<VString*>(args[0].ref)->ToModifiedUtf8());
  }
  return Value{};
}

Value IntentFilterAddCategory(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    g_filter_categories[self].push_back(static_cast<VString*>(args[0].ref)->ToModifiedUtf8());
  }
  return Value{};
}

Value IntentFilterCountActions(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = static_cast<int32_t>(g_filter_actions[self].size());
  return r;
}

Value IntentFilterGetAction(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  auto& list = g_filter_actions[self];
  int32_t i = args[0].i;
  if (i >= 0 && i < static_cast<int32_t>(list.size())) {
    std::u16string u16(list[i].begin(), list[i].end());
    r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(u16));
  }
  return r;
}

Value IntentFilterHasAction(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  std::string want = static_cast<VString*>(args[0].ref)->ToModifiedUtf8();
  for (const auto& a : g_filter_actions[self]) {
    if (a == want) {
      r.i = 1;
      break;
    }
  }
  return r;
}

Value AppRegisterComponentCallbacks(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "registerComponentCallbacks: null");
    *ok = false;
    return Value{};
  }
  g_component_callbacks[self].push_back(reinterpret_cast<VObject*>(args[0].ref));
  return Value{};
}

Value AppUnregisterComponentCallbacks(VMState& state, VObject* self, const Value* args, size_t n,
                                      bool* ok) {
  (void)state;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  auto& list = g_component_callbacks[self];
  for (auto it = list.begin(); it != list.end(); ++it) {
    if (*it == reinterpret_cast<VObject*>(args[0].ref)) {
      list.erase(it);
      break;
    }
  }
  return Value{};
}

// Application.getProcessName（API 28+）：Android 语义主进程 processName == packageName；
// 经 ActivityThread.mInitialApplication 的 mBase 链取 mPackageName（AOSP 同源语义）。
Value AppGetProcessName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* thread = CurrentActivityThread(state.linker);
  VObject* app = nullptr;
  if (thread != nullptr) {
    VField* app_f = FindInstanceField(thread->klass, "mInitialApplication");
    if (app_f != nullptr) app = static_cast<VObject*>(GetRefField(thread, app_f->offset));
  }
  if (app != nullptr) {
    return CtxGetPackageName(state, app, nullptr, 0, ok);
  }
  return r;  // Application 未就位：null（Java 语义无异常）
}
Value ATCurrentApplication(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* thread = CurrentActivityThread(state.linker);
  if (thread != nullptr) {
    VField* app_f = FindInstanceField(thread->klass, "mInitialApplication");
    if (app_f != nullptr) r.ref = GetRefField(thread, app_f->offset);
  }
  return r;
}

// currentPackageName()（AOSP ActivityThread：mBoundApplication.appInfo.
// packageName 的 VM 收敛面）：mInitialApplication 的 mPackageName 链（mBase
// 链语义同 CtxGetPackageName）。加固链（梆梆 get_pkgname -> check_package_name）
// 依赖此面；缺位时 GetStaticMethodID 失败 -> .so strchr(NULL) 段错误（实测）。
Value ATCurrentPackageName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VObject* thread = CurrentActivityThread(state.linker);
  if (thread == nullptr) return r;
  VField* app_f = FindInstanceField(thread->klass, "mInitialApplication");
  VObject* app =
      app_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(thread, app_f->offset)) : nullptr;
  for (VObject* c = app; c != nullptr;) {
    VField* name_f = FindInstanceField(c->klass, "mPackageName");
    if (name_f != nullptr) {
      ObjHeader* s = GetRefField(c, name_f->offset);
      if (s != nullptr) {
        r.ref = s;
        return r;
      }
    }
    VField* base_f = FindInstanceField(c->klass, "mBase");
    c = base_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(c, base_f->offset)) : nullptr;
  }
  return r;
}

// ---- android.content.Intent：lib 归位（§31.7 B 档 POJO→lib），内建面已删 ----

// ---- android.net.Uri ----

Value UriParse(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VClass* uri = state.linker->FindClass("Landroid/net/Uri;");
  if (uri == nullptr || n < 1) return r;
  VObject* u = state.linker->AllocObject(uri);
  if (u == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "Uri.parse alloc failed");
    *ok = false;
    return r;
  }
  VField* f = FindInstanceField(uri, "mUriString");
  if (f != nullptr) SetRefField(u, f->offset, args[0].ref);
  r.ref = u;
  return r;
}

Value UriToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* f = FindInstanceField(self->klass, "mUriString");
  if (f != nullptr) {
    ObjHeader* s = GetRefField(self, f->offset);
    if (s != nullptr) {
      r.ref = s;
      return r;
    }
  }
  r.ref = state.linker->AllocString(u"");
  return r;
}

// =====================================================================
// android.net.Uri 查询面（§31.7 判定：数据面 = mUriString 留 builtin；
// 查询面逐方法 intrinsic，RFC 2396 解析口径 = AOSP StringUri 语义）。
// 纯函数解析 mUriString（无缓存字段）；解码 = '%'-escape UTF-8（AOSP
// Uri.decode：非法八位组以 U+FFFD 代入；'+' 不转空格）；编码 = 百分号
// 转义非白名单字符（alphanumerics + "_-!.~'()*" + allow，AOSP Uri.encode）。
// =====================================================================

struct UriParts {
  bool has_scheme = false;
  std::u16string scheme;
  bool has_fragment = false;
  std::u16string fragment;  // raw（encoded 形）
  bool hierarchical = true;
  std::u16string ssp_raw;  // scheme: 之后（无 scheme = 全串；fragment 前）
  bool has_authority = false;
  std::u16string authority_raw;
  bool has_path = false;  // hierarchical 恒真（path 可空串）；opaque 恒假
  std::u16string path_raw;
  bool has_query = false;
  std::u16string query_raw;
};

namespace {
std::u16string UriSelfString(VObject* uri) {
  if (uri == nullptr) return std::u16string();
  VField* f = FindInstanceField(uri->klass, "mUriString");
  VString* s = f != nullptr ? reinterpret_cast<VString*>(GetRefField(uri, f->offset)) : nullptr;
  if (s == nullptr) return std::u16string();
  return std::u16string(reinterpret_cast<const char16_t*>(s->chars()),
                        static_cast<size_t>(s->length));
}

// String 参数直读（VString 布局；与 UriSelfString 分立——非 Uri 对象无字段）。
std::u16string ArgU16(const Value& v) {
  if (v.ref == nullptr) return std::u16string();
  VString* s = reinterpret_cast<VString*>(const_cast<ObjHeader*>(v.ref));
  return std::u16string(reinterpret_cast<const char16_t*>(s->chars()),
                        static_cast<size_t>(s->length));
}

// MUTF-8 编码（VString::ToModifiedUtf8 同口径：逐 UTF-16 code unit）。
std::string U16ToMutf8(const std::u16string& s) {
  std::string out;
  out.reserve(s.size());
  for (char16_t c : s) {
    if (c != 0 && c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

// AOSP StringUri 拆解：scheme = 首个 ':'（其前无 '/'、'?'、'#'）；
// fragment = '#' 后；opaque = 有 scheme 且 ssp 不以 '/' 起；
// hierarchical：'//' 后至 '/','?','#' 为 authority，'?' 前为 path。
void ParseUriString(const std::u16string& u, UriParts* p) {
  size_t scheme_sep = std::u16string::npos;
  for (size_t i = 0; i < u.size(); i++) {
    char16_t c = u[i];
    if (c == u':') {
      scheme_sep = i;
      break;
    }
    if (c == u'/' || c == u'?' || c == u'#') break;
  }
  size_t main_begin = 0;
  if (scheme_sep != std::u16string::npos) {
    p->has_scheme = true;
    p->scheme = u.substr(0, scheme_sep);
    main_begin = scheme_sep + 1;
  }
  size_t hash = u.find(u'#', main_begin);
  std::u16string main =
      hash == std::u16string::npos ? u.substr(main_begin) : u.substr(main_begin, hash - main_begin);
  if (hash != std::u16string::npos) {
    p->has_fragment = true;
    p->fragment = u.substr(hash + 1);
  }
  p->ssp_raw = main;
  p->hierarchical = !p->has_scheme || (!main.empty() && main[0] == u'/');
  if (!p->hierarchical) return;
  size_t pos = 0;
  if (main.size() >= 2 && main[0] == u'/' && main[1] == u'/') {
    size_t auth_end = 2;
    while (auth_end < main.size() && main[auth_end] != u'/' && main[auth_end] != u'?' &&
           main[auth_end] != u'#') {
      auth_end++;
    }
    p->has_authority = true;
    p->authority_raw = main.substr(2, auth_end - 2);
    pos = auth_end;
  }
  std::u16string rest = main.substr(pos);
  size_t qmark = rest.find(u'?');
  p->has_path = true;
  p->path_raw = qmark == std::u16string::npos ? rest : rest.substr(0, qmark);
  if (qmark != std::u16string::npos) {
    p->has_query = true;
    p->query_raw = rest.substr(qmark + 1);
  }
}

void CpToUtf8(uint32_t cp, std::string* out) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

// 严格 UTF-8 步进；非法序列返回 false（调用方以 U+FFFD 代入并前进 1 字节）。
bool Utf8Next(const std::string& in, size_t* i, uint32_t* cp) {
  const unsigned char c = static_cast<unsigned char>(in[*i]);
  size_t len = 1;
  uint32_t acc = 0;
  if (c < 0x80) {
    acc = c;
  } else if ((c & 0xE0) == 0xC0) {
    acc = c & 0x1F;
    len = 2;
  } else if ((c & 0xF0) == 0xE0) {
    acc = c & 0x0F;
    len = 3;
  } else if ((c & 0xF8) == 0xF0) {
    acc = c & 0x07;
    len = 4;
  } else {
    return false;
  }
  if (*i + len > in.size()) return false;
  for (size_t k = 1; k < len; k++) {
    const unsigned char cc = static_cast<unsigned char>(in[*i + k]);
    if ((cc & 0xC0) != 0x80) return false;
    acc = (acc << 6) | (cc & 0x3Fu);
  }
  if (len > 1 && acc < (len == 2 ? 0x80u : len == 3 ? 0x800u : 0x10000u)) return false;
  if (acc >= 0xD800 && acc <= 0xDFFF) return false;
  if (acc > 0x10FFFF) return false;
  *cp = acc;
  *i += len;
  return true;
}

int HexVal(char16_t c) {
  if (c >= u'0' && c <= u'9') return c - u'0';
  if (c >= u'a' && c <= u'f') return c - u'a' + 10;
  if (c >= u'A' && c <= u'F') return c - u'A' + 10;
  return -1;
}

// AOSP Uri.decode 口径：'%'-escape 八位组按 UTF-8 汇聚解码，非法八位组/序列
// 以 U+FFFD 代入；截断 '%' 与非 hex 转义按字面保留。
std::u16string PercentDecode(const std::u16string& s) {
  std::u16string out;
  std::string run;  // 待解码 %XX 字节串
  auto flush = [&]() {
    size_t i = 0;
    while (i < run.size()) {
      uint32_t cp = 0;
      if (!Utf8Next(run, &i, &cp)) {
        out.push_back(0xFFFD);
        i += 1;
      } else if (cp < 0x10000) {
        out.push_back(static_cast<char16_t>(cp));
      } else {
        cp -= 0x10000;
        out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
      }
    }
    run.clear();
  };
  for (size_t i = 0; i < s.size(); i++) {
    char16_t c = s[i];
    if (c == u'%' && i + 2 < s.size()) {
      int hi = HexVal(s[i + 1]);
      int lo = HexVal(s[i + 2]);
      if (hi >= 0 && lo >= 0) {
        run.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        continue;
      }
    }
    flush();
    out.push_back(c);
  }
  flush();
  return out;
}

// AOSP Uri.encode 口径：白名单（alphanumerics + "_-!.~'()*" + allow）原样，
// 其余按 UTF-8 八位组 %XX（大写 hex）。
std::u16string PercentEncode(const std::u16string& s, const std::u16string* allow) {
  static const char* kHex = "0123456789ABCDEF";
  std::u16string out;
  auto allowed = [&](char16_t c) {
    if ((c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z') || (c >= u'0' && c <= u'9')) return true;
    static const char16_t kSpecial[] = u"_-!.~'()*";
    for (const char16_t* p = kSpecial; *p; p++)
      if (*p == c) return true;
    if (allow != nullptr && allow->find(c) != std::u16string::npos) return true;
    return false;
  };
  for (size_t i = 0; i < s.size(); i++) {
    uint32_t cp = s[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 &&
        s[i + 1] <= 0xDFFF) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i + 1] - 0xDC00);
      i++;
    }
    if (cp < 0x80 && allowed(static_cast<char16_t>(cp))) {
      out.push_back(static_cast<char16_t>(cp));
      continue;
    }
    std::string bytes;
    CpToUtf8(cp, &bytes);
    for (unsigned char b : bytes) {
      out.push_back(u'%');
      out.push_back(static_cast<char16_t>(kHex[b >> 4]));
      out.push_back(static_cast<char16_t>(kHex[b & 0xF]));
    }
  }
  return out;
}

Value UriMakeStr(VMState& state, const std::u16string& s) {
  Value r{};
  r.ref = state.linker->AllocString(s);
  return r;
}

Value UriMakeMaybeStr(VMState& state, bool present, const std::u16string& s) {
  if (!present) return Value{};  // null（AOSP：缺分量返回 null）
  return UriMakeStr(state, s);
}

VObject* UriAlloc(VMState& state, const std::u16string& s) {
  VClass* uri = state.linker->FindClass("Landroid/net/Uri;");
  if (uri == nullptr) return nullptr;
  VObject* u = state.linker->AllocObject(uri);
  if (u == nullptr) return nullptr;
  VField* f = FindInstanceField(uri, "mUriString");
  if (f != nullptr) SetRefField(u, f->offset, reinterpret_cast<ObjHeader*>(state.linker->AllocString(s)));
  return u;
}

}  // namespace
Value UriGetScheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_scheme, p.scheme);
}

Value UriGetSchemeSpecificPart(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeStr(state, PercentDecode(p.ssp_raw));
}

Value UriGetAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_authority, PercentDecode(p.authority_raw));
}

Value UriGetEncodedAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_authority, p.authority_raw);
}

Value UriGetPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_path, PercentDecode(p.path_raw));
}

Value UriGetEncodedPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_path, p.path_raw);
}

Value UriGetQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_query, PercentDecode(p.query_raw));
}

Value UriGetEncodedQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_query, p.query_raw);
}

Value UriGetFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_fragment, PercentDecode(p.fragment));
}

Value UriGetEncodedFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  return UriMakeMaybeStr(state, p.has_fragment, p.fragment);
}

Value UriIsHierarchical(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  Value r{};
  r.i = p.hierarchical ? 1 : 0;
  return r;
}

Value UriIsOpaque(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  Value r{};
  r.i = p.hierarchical ? 0 : 1;
  return r;
}

Value UriIsAbsolute(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  Value r{};
  r.i = p.has_scheme ? 1 : 0;
  return r;
}

Value UriIsRelative(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  Value r{};
  r.i = p.has_scheme ? 0 : 1;
  return r;
}

// authority 拆解（AOSP AbstractHierarchicalUri）：末 '@' 前 = userInfo；
// 括号外末 ':' 后纯数字 = port；host = 余下（decode；IPv6 括号保留）。
namespace {
void SplitAuthority(const std::u16string& auth, std::u16string* user_info, bool* has_user,
                    std::u16string* host, bool* has_host, int32_t* port) {
  *has_user = false;
  *has_host = false;
  *port = -1;
  std::u16string hostport = auth;
  size_t at = auth.rfind(u'@');
  if (at != std::u16string::npos) {
    *has_user = true;
    *user_info = auth.substr(0, at);
    hostport = auth.substr(at + 1);
  }
  if (hostport.empty()) return;
  size_t port_sep = std::u16string::npos;
  size_t bracket = hostport.find(u'[');
  if (bracket != std::u16string::npos) {
    size_t close = hostport.find(u']', bracket);
    if (close != std::u16string::npos) port_sep = hostport.find(u':', close);
  } else {
    port_sep = hostport.rfind(u':');
  }
  std::u16string host_part =
      port_sep == std::u16string::npos ? hostport : hostport.substr(0, port_sep);
  if (port_sep != std::u16string::npos) {
    std::u16string port_part = hostport.substr(port_sep + 1);
    if (!port_part.empty()) {
      bool digits = true;
      for (char16_t c : port_part)
        if (c < u'0' || c > u'9') digits = false;
      if (digits) {
        int64_t v = 0;
        for (char16_t c : port_part) v = v * 10 + (c - u'0');
        *port = v > 0x7FFFFFFF ? -1 : static_cast<int32_t>(v);
      }
    }
  }
  if (host_part.empty()) return;
  *has_host = true;
  *host = host_part;
}

}  // namespace
Value UriGetUserInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  if (!p.has_authority) return Value{};
  std::u16string user, host;
  bool has_user = false, has_host = false;
  int32_t port = -1;
  SplitAuthority(p.authority_raw, &user, &has_user, &host, &has_host, &port);
  return UriMakeMaybeStr(state, has_user, PercentDecode(user));
}

Value UriGetHost(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  if (!p.has_authority) return Value{};
  std::u16string user, host;
  bool has_user = false, has_host = false;
  int32_t port = -1;
  SplitAuthority(p.authority_raw, &user, &has_user, &host, &has_host, &port);
  return UriMakeMaybeStr(state, has_host, PercentDecode(host));
}

Value UriGetPort(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  int32_t port = -1;
  if (p.has_authority) {
    std::u16string user, host;
    bool has_user = false, has_host = false;
    SplitAuthority(p.authority_raw, &user, &has_user, &host, &has_host, &port);
  }
  Value r{};
  r.i = port;
  return r;
}

// getPathSegments：解码路径按 '/' 切分、空段省略（AOSP PathPart.splitSegments）。
Value UriGetPathSegments(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  bool tok = true;
  VObject* list = NewLibObject(state, "Ljava/util/ArrayList;", {}, nullptr, 0, &tok);
  if (!tok || list == nullptr) {
    *ok = false;
    return Value{};
  }
  if (p.has_path) {
    std::u16string path = PercentDecode(p.path_raw);
    size_t start = 0;
    while (start < path.size()) {
      size_t slash = path.find(u'/', start);
      size_t end = slash == std::u16string::npos ? path.size() : slash;
      if (start < end) {
        Value seg = UriMakeStr(state, path.substr(start, end - start));
        Value ignored{};
        if (!CallVirtual(state, list, "add", {"Ljava/lang/Object;"}, "Z", &seg, 1, &ignored)) {
          *ok = false;
          return Value{};
        }
      }
      if (slash == std::u16string::npos) break;
      start = slash + 1;
    }
  }
  r.ref = list;
  return r;
}

Value UriGetLastPathSegment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  if (!p.has_path) return Value{};
  std::u16string path = PercentDecode(p.path_raw);
  size_t end = path.size();
  while (end > 0 && path[end - 1] == u'/') end--;  // 尾 '/' 不成段
  if (end == 0) return Value{};
  size_t slash = path.rfind(u'/', end - 1);
  size_t begin = slash == std::u16string::npos ? 0 : slash + 1;
  if (begin >= end) return Value{};
  return UriMakeStr(state, path.substr(begin, end - begin));
}

// opaque 校验（AOSP getQueryParameter 家族：opaque 抛 UnsupportedOperationException）
namespace {
bool UriRequireHierarchical(VMState& state, const UriParts& p, bool* ok) {
  if (p.hierarchical) return true;
  state.ThrowNew("Ljava/lang/UnsupportedOperationException;", "This isn't a hierarchical URI.");
  *ok = false;
  return false;
}

// AOSP AbstractHierarchicalUri.getQueryParameter：encoded key 对 raw query
// 逐段 '&'/'=' 匹配（大小写敏感），命中返回解码值（无 '=' = 空串）。
}  // namespace
Value UriGetQueryParameter(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  if (!UriRequireHierarchical(state, p, ok)) return Value{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "key");
    *ok = false;
    return Value{};
  }
  std::u16string key = ArgU16(args[0]);
  std::u16string encoded_key = PercentEncode(key, nullptr);
  const std::u16string& query = p.query_raw;
  if (!p.has_query) return Value{};
  size_t length = query.size();
  size_t start = 0;
  do {
    size_t next_amp = query.find(u'&', start);
    size_t end = next_amp == std::u16string::npos ? length : next_amp;
    size_t sep = query.find(u'=', start);
    if (sep == std::u16string::npos || sep > end) sep = end;
    if (sep - start == encoded_key.size() &&
        query.compare(start, encoded_key.size(), encoded_key) == 0) {
      if (sep == end) return UriMakeStr(state, std::u16string());
      return UriMakeStr(state, PercentDecode(query.substr(sep + 1, end - sep - 1)));
    }
    start = end + 1;
  } while (start < length);
  return Value{};
}

// getQueryParameters(key)：全出现序解码值表（无 '=' = 空串）。
Value UriGetQueryParameters(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  if (!UriRequireHierarchical(state, p, ok)) return Value{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "key");
    *ok = false;
    return Value{};
  }
  Value r{};
  bool tok = true;
  VObject* list = NewLibObject(state, "Ljava/util/ArrayList;", {}, nullptr, 0, &tok);
  if (!tok || list == nullptr) {
    *ok = false;
    return Value{};
  }
  std::u16string key = ArgU16(args[0]);
  std::u16string encoded_key = PercentEncode(key, nullptr);
  const std::u16string& query = p.query_raw;
  if (p.has_query) {
    size_t length = query.size();
    size_t start = 0;
    while (start < length) {
      size_t next_amp = query.find(u'&', start);
      size_t end = next_amp == std::u16string::npos ? length : next_amp;
      size_t sep = query.find(u'=', start);
      if (sep == std::u16string::npos || sep > end) sep = end;
      if (sep - start == encoded_key.size() &&
          query.compare(start, encoded_key.size(), encoded_key) == 0) {
        Value v = sep == end ? UriMakeStr(state, std::u16string())
                             : UriMakeStr(state, PercentDecode(query.substr(sep + 1, end - sep - 1)));
        Value ignored{};
        if (!CallVirtual(state, list, "add", {"Ljava/lang/Object;"}, "Z", &v, 1, &ignored)) {
          *ok = false;
          return Value{};
        }
      }
      start = end + 1;
    }
  }
  r.ref = list;
  return r;
}

// getQueryParameterNames：出现序去重解码键集（AOSP LinkedHashSet 语义）。
Value UriGetQueryParameterNames(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  UriParts p;
  ParseUriString(UriSelfString(self), &p);
  if (!UriRequireHierarchical(state, p, ok)) return Value{};
  Value r{};
  bool tok = true;
  VObject* set = NewLibObject(state, "Ljava/util/LinkedHashSet;", {}, nullptr, 0, &tok);
  if (!tok || set == nullptr) {
    *ok = false;
    return Value{};
  }
  const std::u16string& query = p.query_raw;
  if (p.has_query) {
    size_t length = query.size();
    size_t start = 0;
    while (start < length) {
      size_t next_amp = query.find(u'&', start);
      size_t end = next_amp == std::u16string::npos ? length : next_amp;
      size_t sep = query.find(u'=', start);
      if (sep == std::u16string::npos || sep > end) sep = end;
      Value name = UriMakeStr(state, PercentDecode(query.substr(start, sep - start)));
      Value ignored{};
      if (!CallVirtual(state, set, "add", {"Ljava/lang/Object;"}, "Z", &name, 1, &ignored)) {
        *ok = false;
        return Value{};
      }
      start = end + 1;
    }
  }
  r.ref = set;
  return r;
}

// equals：AOSP Uri.equals——uri 字符串相等即等（toString 口径）。
Value UriEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)n;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  VObject* other = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  bool is_uri = false;
  for (VClass* k = other->klass; k != nullptr; k = k->super_class) {
    if (k->descriptor == "Landroid/net/Uri;") {
      is_uri = true;
      break;
    }
  }
  if (!is_uri) return r;
  r.i = UriSelfString(self) == UriSelfString(other) ? 1 : 0;
  return r;
}

// hashCode：AOSP String.hashCode 同法（31 进制滚动）。
Value UriHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  std::u16string s = UriSelfString(self);
  int32_t h = 0;
  for (char16_t c : s) h = 31 * h + c;
  Value r{};
  r.i = h;
  return r;
}

// normalizeScheme：scheme 小写（ASCII）重构；无 scheme 原样返回（AOSP）。
Value UriNormalizeScheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  UriParts p;
  std::u16string raw = UriSelfString(self);
  ParseUriString(raw, &p);
  if (!p.has_scheme) {
    r.ref = self;
    return r;
  }
  std::u16string scheme = p.scheme;
  for (char16_t& c : scheme)
    if (c >= u'A' && c <= u'Z') c = static_cast<char16_t>(c - u'A' + u'a');
  std::u16string rebuilt = scheme + u":" + p.ssp_raw;
  if (p.has_fragment) rebuilt += u"#" + p.fragment;
  VObject* u = UriAlloc(state, rebuilt);
  if (u == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "normalizeScheme alloc failed");
    *ok = false;
    return Value{};
  }
  r.ref = u;
  return r;
}

// ---- 静态面 ----

Value UriDecode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  return UriMakeStr(state, PercentDecode(ArgU16(args[0])));
}

Value UriEncode1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  return UriMakeStr(state, PercentEncode(ArgU16(args[0]), nullptr));
}

Value UriEncode2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) return Value{};
  std::u16string allow;
  if (n >= 2 && args[1].ref != nullptr) {
    allow = ArgU16(args[1]);
  }
  return UriMakeStr(state, PercentEncode(
                              ArgU16(args[0]),
                              n >= 2 ? &allow : nullptr));
}

// fromParts(scheme, ssp, fragment)：AOSP——scheme/ssp 非空（NPE），拼串解析。
Value UriFromParts(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "scheme and ssp");
    *ok = false;
    return r;
  }
  std::u16string s = ArgU16(args[0]);
  s += u":";
  s += ArgU16(args[1]);
  if (n >= 3 && args[2].ref != nullptr) {
    s += u"#";
    s += ArgU16(args[2]);
  }
  VObject* u = UriAlloc(state, s);
  if (u == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "fromParts alloc failed");
    *ok = false;
    return r;
  }
  r.ref = u;
  return r;
}

// withAppendedPath(base, segment)：AOSP Builder.appendPath 口径——段经
// Uri.encode 后拼接（base + "/" + encode(segment)）。
Value UriWithAppendedPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "baseUri");
    *ok = false;
    return r;
  }
  std::u16string s =
      UriSelfString(reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref)));
  s += u"/";
  if (args[1].ref != nullptr) {
    s += PercentEncode(ArgU16(args[1]), nullptr);
  }
  VObject* u = UriAlloc(state, s);
  if (u == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "withAppendedPath alloc failed");
    *ok = false;
    return r;
  }
  r.ref = u;
  return r;
}

// ---- android.os.Bundle：已按 §31.7 B 档 POJO→lib 归位（runtime-libs
// android/os/Bundle.java 随 runtime.dex 打包；内存 HashMap 数据载体）。----

// =====================================================================
// ContentResolver CRUD 路由（§31.7 B 档：内建只留钩子；authority →
// provider 实例分发 = AOSP ActivityThread.mProviderMap 等价）。
// 实例注册面：ContentProvider.attachInfo 落点（ProviderInfo.authority
// 分号多值逐键注册，GC 钉住 = ContentService 自身即根等价）。
// 未注册 authority：AOSP ContentResolver 家族口径 IllegalArgumentException
// "Unknown URI <uri>"（acquireProvider 无主抛面）。
// =====================================================================

std::map<std::string, VObject*> g_provider_instances;  // authority -> provider（GC 钉住）

namespace {
void RegisterProviderInstance(VMState& state, const std::string& authorities, VObject* provider) {
  if (provider == nullptr) return;
  size_t start = 0;
  while (start <= authorities.size()) {
    size_t semi = authorities.find(';', start);
    std::string one =
        authorities.substr(start, semi == std::string::npos ? std::string::npos : semi - start);
    if (!one.empty()) g_provider_instances[one] = provider;
    if (semi == std::string::npos) break;
    start = semi + 1;
  }
  if (state.heap != nullptr) state.heap->Pin(provider);
}

// authority 键（MUTF-8；注册/查找同口径——ProviderInfo.authority 经
// VString::ToModifiedUtf8 注册）。
std::string UriAuthKey(const UriParts& p) {
  return p.has_authority ? U16ToMutf8(p.authority_raw) : std::string();
}

// authority（MUTF-8）解析 provider 实例；未注册抛 IAE（AOSP ContentResolver
// 家族口径："Unknown URI <uri>"）。
VObject* ResolveProviderInstance(VMState& state, const std::string& display,
                                 const std::string& auth_utf8, bool* ok) {
  auto it = g_provider_instances.find(auth_utf8);
  if (it != g_provider_instances.end()) return it->second;
  state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Unknown URI " + display);
  *ok = false;
  return nullptr;
}

// CRUD 入口公共前置：uri 权威键 + 实例解析（失败挂异常，*ok=false）。
VObject* ResolveForUri(VMState& state, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "uri");
    *ok = false;
    return nullptr;
  }
  VObject* uri = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  UriParts p;
  ParseUriString(UriSelfString(uri), &p);
  return ResolveProviderInstance(state, U16ToMutf8(UriSelfString(uri)), UriAuthKey(p), ok);
}

// ContentResolver.query(Uri,String[],String,String[],String) -> provider.query 同形
}  // namespace
// ---- android.net.Uri$Builder（内建内嵌类；语义-lite 字段态装配）----
// 五槽字符串态（mScheme/mAuthority/mPath/mQuery/mFragment）；build/toString =
// AOSP 拼装口径（scheme://authority/path?query#fragment）。

Value UBInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  if (self->klass->object_size > sizeof(ObjHeader)) {
    std::memset(self->field_data(), 0, self->klass->object_size - sizeof(ObjHeader));
  }
  return Value{};
}

Value UBSetPart(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                const char* field) {
  (void)state;
  *ok = true;
  VField* f = FindInstanceField(self->klass, field);
  if (f != nullptr && n >= 1) SetRefField(self, f->offset, args[0].ref);
  return Value{};
}

Value UBScheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mScheme");
}
Value UBAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mAuthority");
}
Value UBEncodedAuthority(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mAuthority");
}
Value UBPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mPath");
}
Value UBEncodedPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mPath");
}
Value UBQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mQuery");
}
Value UBEncodedQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mQuery");
}
Value UBFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mFragment");
}
Value UBEncodedFragment(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return UBSetPart(state, self, args, n, ok, "mFragment");
}
Value UBClearQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  VField* f = FindInstanceField(self->klass, "mQuery");
  if (f != nullptr) SetRefField(self, f->offset, nullptr);
  return Value{};
}

Value UBAppendPath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    VField* f = FindInstanceField(self->klass, "mPath");
    if (f != nullptr) {
      ObjHeader* cur = GetRefField(self, f->offset);
      std::string path = cur != nullptr ? static_cast<VString*>(cur)->ToModifiedUtf8() : "";
      path += "/" + static_cast<VString*>(args[0].ref)->ToModifiedUtf8();
      VString* s = state.linker->AllocString(std::u16string(path.begin(), path.end()));
      SetRefField(self, f->offset, reinterpret_cast<ObjHeader*>(s));
    }
  }
  return Value{};
}

Value UBAppendQueryParameter(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 2 && args[0].ref != nullptr && args[1].ref != nullptr) {
    VField* f = FindInstanceField(self->klass, "mQuery");
    if (f != nullptr) {
      ObjHeader* cur = GetRefField(self, f->offset);
      std::string q = cur != nullptr ? static_cast<VString*>(cur)->ToModifiedUtf8() : "";
      if (!q.empty()) q += "&";
      q += static_cast<VString*>(args[0].ref)->ToModifiedUtf8();
      q += "=";
      q += static_cast<VString*>(args[1].ref)->ToModifiedUtf8();
      VString* s = state.linker->AllocString(std::u16string(q.begin(), q.end()));
      SetRefField(self, f->offset, reinterpret_cast<ObjHeader*>(s));
    }
  }
  return Value{};
}

std::string UBAssemble(VObject* self) {
  auto get = [&](const char* name) {
    VField* f = FindInstanceField(self->klass, name);
    ObjHeader* v = f != nullptr ? GetRefField(self, f->offset) : nullptr;
    return v != nullptr ? static_cast<VString*>(v)->ToModifiedUtf8() : std::string();
  };
  std::string scheme = get("mScheme");
  std::string auth = get("mAuthority");
  std::string path = get("mPath");
  std::string query = get("mQuery");
  std::string frag = get("mFragment");
  std::string out;
  if (!scheme.empty()) out += scheme + ":";
  if (!auth.empty()) out += "//" + auth;
  out += path;
  if (!query.empty()) out += "?" + query;
  if (!frag.empty()) out += "#" + frag;
  return out;
}

Value UBToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  std::string s = UBAssemble(self);
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(s.begin(), s.end())));
  return r;
}

Value UBBuild(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* uri_cls = state.linker->FindClass("Landroid/net/Uri;");
  if (uri_cls == nullptr) {
    *ok = false;
    return r;
  }
  VObject* uri = state.linker->AllocObject(uri_cls);
  if (uri == nullptr) {
    *ok = false;
    return r;
  }
  std::string s = UBAssemble(self);
  VField* f = FindInstanceField(uri_cls, "mUriString");
  if (f != nullptr) {
    SetRefField(uri, f->offset, reinterpret_cast<ObjHeader*>(
        state.linker->AllocString(std::u16string(s.begin(), s.end()))));
  }
  r.ref = uri;
  return r;
}

// Uri.buildUpon()：mUriString 解析 -> Builder 字段（scheme://auth/path?q#f；
// opaque = scheme:ssp 落 scheme/path 两槽）。
Value UriBuildUpon(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* bk = state.linker->FindClass("Landroid/net/Uri$Builder;");
  if (bk == nullptr) {
    *ok = false;
    return r;
  }
  VObject* b = state.linker->AllocObject(bk);
  if (b == nullptr) {
    *ok = false;
    return r;
  }
  std::string uri;
  if (VField* f = FindInstanceField(self->klass, "mUriString")) {
    ObjHeader* s = GetRefField(self, f->offset);
    if (s != nullptr) uri = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  auto setf = [&](const char* name, const std::string& v) {
    if (v.empty()) return;
    if (VField* f = FindInstanceField(b->klass, name)) {
      SetRefField(b, f->offset,
                  reinterpret_cast<ObjHeader*>(state.linker->AllocString(
                      std::u16string(v.begin(), v.end()))));
    }
  };
  std::string rest = uri;
  size_t hash = rest.find('#');
  if (hash != std::string::npos) {
    setf("mFragment", rest.substr(hash + 1));
    rest = rest.substr(0, hash);
  }
  size_t q = rest.find('?');
  if (q != std::string::npos) {
    setf("mQuery", rest.substr(q + 1));
    rest = rest.substr(0, q);
  }
  size_t colon = rest.find(':');
  if (colon != std::string::npos) {
    setf("mScheme", rest.substr(0, colon));
    std::string ssp = rest.substr(colon + 1);
    if (ssp.rfind("//", 0) == 0) {
      size_t slash = ssp.find('/', 2);
      setf("mAuthority", ssp.substr(2, slash == std::string::npos ? std::string::npos
                                                                  : slash - 2));
      if (slash != std::string::npos) setf("mPath", ssp.substr(slash));
    } else {
      setf("mPath", ssp);
    }
  } else if (!rest.empty()) {
    setf("mPath", rest);
  }
  r.ref = reinterpret_cast<ObjHeader*>(b);
  return r;
}

Value UBBuildUpon(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  // Builder 为内建类：直接 Alloc + 槽拷贝
  VClass* bk = state.linker->FindClass("Landroid/net/Uri$Builder;");
  if (bk == nullptr) {
    *ok = false;
    return r;
  }
  VObject* nb = state.linker->AllocObject(bk);
  if (nb == nullptr) {
    *ok = false;
    return r;
  }
  static const char* kFields[5] = {"mScheme", "mAuthority", "mPath", "mQuery", "mFragment"};
  for (const char* name : kFields) {
    VField* f = FindInstanceField(self->klass, name);
    VField* nf = FindInstanceField(nb->klass, name);
    if (f != nullptr && nf != nullptr) {
      SetRefField(nb, nf->offset, GetRefField(self, f->offset));
    }
  }
  r.ref = nb;
  return r;
}

Value CRQuery(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  // 未知 authority 的"探测型"查询（华为 referrer/厂商渠道等）返回 null 游标，
  // 不抛 IllegalArgumentException：AOSP 抛面会打断加固 app 的设备信息/账户
  // 初始化链（实测 CloudID -> 华为 referrer 探测在非华为设备上异常上抛，
  // guest 账户请求中止，currentAccount 恒 null）。null = "无数据"。
  bool resolved_ok = true;
  VObject* provider = ResolveForUri(state, args, n, &resolved_ok);
  if (provider == nullptr) {
    if (state.HasPendingException()) state.ClearException();
#if defined(__ANDROID__)
    // TEMP DIAG: 安静探测路径（事后移除）
    __android_log_print(ANDROID_LOG_INFO, "DexVmQuery", "quiet miss pending=%d",
                        state.HasPendingException() ? 1 : 0);
#endif
    *ok = true;
    return r;
  }
  if (!CallVirtual(state, provider, "query",
                   {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                    "[Ljava/lang/String;", "Ljava/lang/String;"},
                   "Landroid/database/Cursor;", args, 5, &r)) {
    *ok = false;
  }
  return r;
}

// 6 号 query 形（+CancellationSignal）：路由至 provider 经典 5 号形（AOSP
// ContentProvider 基类委派同构；信号面后置）。
Value CRQuerySignal(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 5) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "query arity");
    *ok = false;
    return Value{};
  }
  return CRQuery(state, self, args, 5, ok);
}

// Bundle queryArgs 形（API 26+）：路由至经典 5 号形（selection 面按 null；
// queryArgs 键面后置）。
Value CRQueryBundle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value classic[5] = {};
  if (n >= 1) classic[0] = args[0];
  if (n >= 2) classic[1] = args[1];
  return CRQuery(state, self, classic, 5, ok);
}

Value CRInsert(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* provider = ResolveForUri(state, args, n, ok);
  if (!*ok || provider == nullptr) return r;
  Value call[2] = {};
  if (n >= 1) call[0] = args[0];
  if (n >= 2) call[1] = args[1];
  if (!CallVirtual(state, provider, "insert",
                   {"Landroid/net/Uri;", "Landroid/content/ContentValues;"}, "Landroid/net/Uri;",
                   call, 2, &r)) {
    *ok = false;
  }
  return r;
}

// insert(Uri, ContentValues, Bundle)：extras 面后置，路由经典 2 号形。
Value CRInsertExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return CRInsert(state, self, args, n >= 2 ? 2 : n, ok);
}

Value CRUpdate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* provider = ResolveForUri(state, args, n, ok);
  if (!*ok || provider == nullptr) return r;
  Value call[4] = {};
  if (n >= 1) call[0] = args[0];
  if (n >= 2) call[1] = args[1];
  if (n >= 3) call[2] = args[2];
  if (n >= 4) call[3] = args[3];
  if (!CallVirtual(state, provider, "update",
                   {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Ljava/lang/String;",
                    "[Ljava/lang/String;"},
                   "I", call, 4, &r)) {
    *ok = false;
  }
  return r;
}

// update(Uri, ContentValues, Bundle)：路由经典 4 号形（selection 面按 null）。
Value CRUpdateExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value classic[4] = {};
  if (n >= 1) classic[0] = args[0];
  if (n >= 2) classic[1] = args[1];
  return CRUpdate(state, self, classic, 4, ok);
}

Value CRDelete(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* provider = ResolveForUri(state, args, n, ok);
  if (!*ok || provider == nullptr) return r;
  Value call[3] = {};
  if (n >= 1) call[0] = args[0];
  if (n >= 2) call[1] = args[1];
  if (n >= 3) call[2] = args[2];
  if (!CallVirtual(state, provider, "delete",
                   {"Landroid/net/Uri;", "Ljava/lang/String;", "[Ljava/lang/String;"}, "I", call, 3,
                   &r)) {
    *ok = false;
  }
  return r;
}

// delete(Uri, Bundle)：路由经典 3 号形（selection 面按 null）。
Value CRDeleteExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value classic[3] = {};
  if (n >= 1) classic[0] = args[0];
  return CRDelete(state, self, classic, 3, ok);
}

Value CRGetType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* provider = ResolveForUri(state, args, n, ok);
  if (!*ok || provider == nullptr) return r;
  if (!CallVirtual(state, provider, "getType", {"Landroid/net/Uri;"}, "Ljava/lang/String;", args, 1,
                   &r)) {
    *ok = false;
  }
  return r;
}

// bulkInsert：AOSP Transport.bulkInsert 同构——逐条 insert 计成功数。
Value CRBulkInsert(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "uri or values");
    *ok = false;
    return r;
  }
  VArray* arr = reinterpret_cast<VArray*>(const_cast<ObjHeader*>(args[1].ref));
  int32_t count = 0;
  for (int32_t i = 0; i < arr->length; i++) {
    Value one[2] = {};
    one[0] = args[0];
    one[1].ref = GetArrRef(arr, i);
    Value out = CRInsert(state, self, one, 2, ok);
    if (!*ok) return Value{};
    if (out.ref != nullptr) count++;
  }
  r.i = count;
  return r;
}

// call(Uri, method, arg, extras)：provider.call(method, arg, extras) 路由。
Value CRCallUri(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* provider = ResolveForUri(state, args, n, ok);
  if (!*ok || provider == nullptr) return r;
  Value call[3] = {};
  if (n >= 2) call[0] = args[1];
  if (n >= 3) call[1] = args[2];
  if (n >= 4) call[2] = args[3];
  if (!CallVirtual(state, provider, "call",
                   {"Ljava/lang/String;", "Ljava/lang/String;", "Landroid/os/Bundle;"},
                   "Landroid/os/Bundle;", call, 3, &r)) {
    *ok = false;
  }
  return r;
}

// call(authority, method, arg, extras)：authority 字符串直接解析路由。
Value CRCallAuth(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "authority");
    *ok = false;
    return r;
  }
  std::string auth = Mutf8Arg(args[0]);
  VObject* provider = ResolveProviderInstance(state, auth, auth, ok);
  if (!*ok || provider == nullptr) return r;
  Value call[3] = {};
  if (n >= 2) call[0] = args[1];
  if (n >= 3) call[1] = args[2];
  if (n >= 4) call[2] = args[3];
  if (!CallVirtual(state, provider, "call",
                   {"Ljava/lang/String;", "Ljava/lang/String;", "Landroid/os/Bundle;"},
                   "Landroid/os/Bundle;", call, 3, &r)) {
    *ok = false;
  }
  return r;
}

// ---- ContentProvider 基类 overload 委派（AOSP 具体形同构）：全形归一经典形 ----

Value CPSelfQuerySignal(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value call[5] = {};
  for (size_t i = 0; i < 5 && i < n; i++) call[i] = args[i];
  Value r{};
  if (!CallVirtual(state, self, "query",
                   {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                    "[Ljava/lang/String;", "Ljava/lang/String;"},
                   "Landroid/database/Cursor;", call, 5, &r)) {
    *ok = false;
    return r;
  }
  *ok = true;
  return r;
}

Value CPSelfQueryBundle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value call[5] = {};
  if (n >= 1) call[0] = args[0];
  if (n >= 2) call[1] = args[1];
  Value r{};
  if (!CallVirtual(state, self, "query",
                   {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                    "[Ljava/lang/String;", "Ljava/lang/String;"},
                   "Landroid/database/Cursor;", call, 5, &r)) {
    *ok = false;
    return r;
  }
  *ok = true;
  return r;
}

Value CPSelfInsertExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value call[2] = {};
  if (n >= 1) call[0] = args[0];
  if (n >= 2) call[1] = args[1];
  Value r{};
  if (!CallVirtual(state, self, "insert",
                   {"Landroid/net/Uri;", "Landroid/content/ContentValues;"}, "Landroid/net/Uri;",
                   call, 2, &r)) {
    *ok = false;
    return r;
  }
  *ok = true;
  return r;
}

Value CPSelfUpdateExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value call[4] = {};
  if (n >= 1) call[0] = args[0];
  if (n >= 2) call[1] = args[1];
  Value r{};
  if (!CallVirtual(state, self, "update",
                   {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Ljava/lang/String;",
                    "[Ljava/lang/String;"},
                   "I", call, 4, &r)) {
    *ok = false;
    return r;
  }
  *ok = true;
  return r;
}

Value CPSelfDeleteExtras(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value call[3] = {};
  if (n >= 1) call[0] = args[0];
  Value r{};
  if (!CallVirtual(state, self, "delete",
                   {"Landroid/net/Uri;", "Ljava/lang/String;", "[Ljava/lang/String;"}, "I", call, 3,
                   &r)) {
    *ok = false;
    return r;
  }
  *ok = true;
  return r;
}

Value CPSelfCall4(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value call[3] = {};
  if (n >= 2) call[0] = args[1];
  if (n >= 3) call[1] = args[2];
  if (n >= 4) call[2] = args[3];
  Value r{};
  if (!CallVirtual(state, self, "call",
                   {"Ljava/lang/String;", "Ljava/lang/String;", "Landroid/os/Bundle;"},
                   "Landroid/os/Bundle;", call, 3, &r)) {
    *ok = false;
    return r;
  }
  *ok = true;
  return r;
}

// ContentResolver 单例（AOSP ContextImpl.mResolver 每 Context 单例；VM 单
// 服务桥口径——同 PM/PackageManager 单例语义）。
VObject* g_content_resolver = nullptr;

Value CtxGetContentResolver(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  if (g_content_resolver == nullptr) {
    VClass* k = state.linker->FindClass("Landroid/content/ContentResolver;");
    VObject* cr = k != nullptr ? state.linker->AllocObject(k) : nullptr;
    if (cr == nullptr) {
      state.ThrowNew("Ljava/lang/IllegalStateException;", "ContentResolver unavailable");
      *ok = false;
      return r;
    }
    g_content_resolver = cr;
    if (state.heap != nullptr) state.heap->Pin(cr);
  }
  r.ref = g_content_resolver;
  return r;
}

// ---- android.content.ContentProvider ----

Value CPAttachInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mContext");
    if (f != nullptr) SetRefField(self, f->offset, args[0].ref);
  }
  // AOSP ActivityThread.installProvider：实例入 provider map（authority 键，
  // 分号多值逐键）——ContentResolver CRUD 路由的分发源。
  if (n >= 2 && args[1].ref != nullptr) {
    VObject* info = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[1].ref));
    VField* auth_f = FindInstanceField(info->klass, "authority");
    if (auth_f != nullptr) {
      ObjHeader* s = GetRefField(info, auth_f->offset);
      if (s != nullptr) {
        RegisterProviderInstance(state, static_cast<VString*>(s)->ToModifiedUtf8(), self);
      }
    }
  }
  return Value{};
}

// AOSP ContentProvider.getContext()（final：返回 attachInfo 挂载的 mContext）
Value CPGetContext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  if (self != nullptr) {
    VField* f = FindInstanceField(self->klass, "mContext");
    if (f != nullptr) r.ref = GetRefField(self, f->offset);
  }
  return r;
}

// =====================================================================
// M4.3 android.os 消息循环子系统（docs 04 章 14.10 Guest main Looper、
// 10 章 19.1 Process State；AOSP Looper/Handler/MessageQueue/Message/HandlerThread
// 公开语义）。消息队列 = 对象字段链（MessageQueue.mMessages -> Message.next，
// 按 when 升序稳定排序）；消息泵 = 单线程 VM 虚拟时间推进（延迟消息在 pump
// 时按虚拟时钟出队，不占 OS 线程——coroutine wait/wake 接线后置）。
//
// 线程模型（用户确认）：① 执行按单线程 + 协程（当前验收路径）；② Looper 一律
// 按线程身份 key 化——myLooper/prepare/Handler 默认绑定走 Thread*->Looper*
// 身份查找（g_thread_looper），每个 Looper 独立 MessageQueue（无全局单例队列）；
// HandlerThread = 新 GuestTask + 自己的 Looper（执行暂由单线程泵驱动）。
// 动态线程（GuestTask 1:1、Carrier 池扩容）就绪：新线程=新身份键，语义不变。
// =====================================================================

// ---- android.content.Context 设备保护存储面 ----
// AOSP createDeviceProtectedStorageContext 返回存储 API 落 DE 区的 Context；
// VM 存储面单一（无 CE/DE 分区），返回等价 Context（this）——语义等价不伪造。
Value CtxDeviceProtectedContext(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = self;
  return r;
}

Value CtxIsDeviceProtected(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 0;
  return r;
}

// AOSP moveSharedPreferencesFrom：SP 名字键全局单存储面（lib SharedPreferencesImpl
// per-name 单例）——跨 Context 迁移为空操作，恒等完成（true）。
Value CtxMoveSharedPreferencesFrom(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

// AOSP moveDatabaseFrom 同口径（VM 单存储面：数据库名键全局唯一，跨 Context
// 迁移为空操作，恒等完成 true）。
Value CtxMoveDatabaseFrom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

// ---- android.content.Context 服务桥（§31.7 B 档）：系统服务单例 ----
// AOSP：所有 Context 的 getPackageManager() 返回同一系统 PM；getApplicationInfo()
// 返回本 app 身份。VM 口径 = 全局单例（设备考 semface 装配后落点；宿主测试
// 自设）。ContextImpl 等子类可另有更具体实现（虚分发优先）。
VObject* g_context_pm = nullptr;
VObject* g_context_app_info = nullptr;

Value CtxGetPackageManager(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  if (g_context_pm == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "no PackageManager installed");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = g_context_pm;
  *ok = true;
  return r;
}

Value CtxGetApplicationInfo(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  if (g_context_app_info == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "no ApplicationInfo installed");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = g_context_app_info;
  *ok = true;
  return r;
}

// ---- android.content.Context 文件/存储面（AOSP ContextImpl 语义；B 档方法面）----
// 数据根 = app data dir（SetContextDataDir 落点；设备考语境 = 宿主 app files
// 目录投影，真实路径自洽、可读写）。路径口径（AOSP ContextImpl）：
//   getDataDir()               = <root>
//   getFilesDir()              = <root>/files
//   getCacheDir()              = <root>/cache
//   getCodeCacheDir()          = <root>/code_cache
//   getNoBackupFilesDir()      = <root>/no_backup
//   getDir(name, mode)         = <root>/<name>
//   getDatabasePath(name)      = <root>/databases/<name>（绝对名直用）
//   getSharedPreferencesPath() = <root>/shared_prefs/<name>.xml
// 目录创建语义（AOSP ensurePrivateDirExists）：取用即建（含缺失父目录）；
// File 产出 = java.io.File 构造经 lib（<init>(String)，失败回落直写 path 字段，
// 同 runtime_contract semface 先例）。

std::string g_ctx_data_dir;  // 显式落点（设备/宿主测试各自生命周期设置）

const std::string& CtxDataRoot() {
  if (g_ctx_data_dir.empty()) {
    const char* env = std::getenv("DEXVM_DATA_DIR");
    std::string base = (env != nullptr && env[0] != '\0') ? env : "";
    if (base.empty()) {
      const char* tmp = std::getenv("TMPDIR");
      base = (tmp != nullptr && tmp[0] != '\0') ? tmp : "/tmp";
      if (!base.empty() && base.back() == '/') base.pop_back();
      base += "/dexvm-ctx-data";
    }
    if (!base.empty() && base.back() == '/' && base.size() > 1) base.pop_back();
    g_ctx_data_dir = base;
  }
  return g_ctx_data_dir;
}

// mkdir -p（逐段创建；已有目录视为成功）
namespace {
bool EnsureDirPath(const std::string& path) {
  if (path.empty()) return false;
  std::string cur;
  size_t pos = 0;
  while (pos <= path.size()) {
    size_t next = path.find('/', pos);
    if (next == std::string::npos) next = path.size();
    if (next > 0) {
      cur.assign(path, 0, next);
      if (::mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST) {
        struct stat st;
        if (::stat(cur.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return false;
      }
    }
    if (next == path.size()) break;
    pos = next + 1;
  }
  struct stat st;
  return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// java.io.File 产出（构造经 lib：guest <init>(String)；失败回落直写 path 字段）
VObject* CtxMakeFile(VMState& state, const std::string& path, bool* ok) {
  VClass* file_cls = state.linker->FindClass("Ljava/io/File;");
  if (file_cls == nullptr || !CheckClassDefinition(state, file_cls) ||
      !state.linker->InitializeClass(file_cls, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/NoClassDefFoundError;", "Ljava/io/File;");
    }
    *ok = false;
    return nullptr;
  }
  VObject* file = state.linker->AllocObject(file_cls);
  if (file == nullptr) {
    ThrowOOM(state, "Context file face");
    *ok = false;
    return nullptr;
  }
  std::u16string u(path.begin(), path.end());
  ObjHeader* pstr = reinterpret_cast<ObjHeader*>(state.linker->AllocString(u));
  for (auto& m : file_cls->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 1 &&
        m.param_types[0] == "Ljava/lang/String;") {
      Value args[2] = {};
      args[0].ref = file;
      args[1].ref = pstr;
      Value ret{};
      if (InvokeMethod(state, &m, args, 2, &ret) && !state.HasPendingException()) {
        *ok = true;
        return file;
      }
      state.ClearException();
      break;
    }
  }
  for (const auto& f : file_cls->instance_fields) {
    if (f.name == "path") {
      SetRefField(file, f.offset, pstr);
      *ok = true;
      return file;
    }
  }
  state.ThrowNew("Ljava/lang/InternalError;", "java.io.File path field missing");
  *ok = false;
  return nullptr;
}

// 存储目录取用（AOSP ensurePrivateDirExists：取用即建；失败 IllegalStateException）
Value CtxDirFace(VMState& state, const std::string& rel, bool create, bool* ok) {
  std::string path = CtxDataRoot();
  if (!rel.empty()) {
    if (!path.empty() && path.back() != '/') path += '/';
    path += rel;
  }
  if (create && !EnsureDirPath(path)) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "cannot create dir " + path);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = CtxMakeFile(state, path, ok);
  return r;
}

// ---- Context 外部存储面（AOSP ContextImpl.getExternal*Dirs + Environment
// buildExternalStorageAppDirs 同构）：基 = <ext-root>/Android/data/<pkg>/{files,
// cache}、<ext-root>/Android/media/<pkg>；files 带 type 子目录。建目录
// best-effort（AOSP ensurePrivateExternalDirsExist：失败仅记日志不抛——
// FileProvider paths 装配只取路径不写入）。----
}  // namespace
std::string g_ctx_external_root;  // 空 = "/storage/emulated/0"（真机语义缺省）

std::string CtxExternalRoot() {
  if (g_ctx_external_root.empty()) return "/storage/emulated/0";
  return g_ctx_external_root;
}

// mBase 链包名 std::string 形（CtxGetPackageName 同链）
namespace {
std::string CtxGetPackageNameString(VObject* self) {
  VObject* c = self;
  while (c != nullptr) {
    VField* name_f = FindInstanceField(c->klass, "mPackageName");
    if (name_f != nullptr) {
      ObjHeader* s = GetRefField(c, name_f->offset);
      if (s != nullptr) return static_cast<VString*>(s)->ToModifiedUtf8();
    }
    VField* base_f = FindInstanceField(c->klass, "mBase");
    c = base_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(c, base_f->offset)) : nullptr;
  }
  return "";
}

// pkg = mBase 链包名（虚拟身份）；AOSP buildExternalStorageAppDirs 路径形
VObject* CtxExternalDirFace(VMState& state, VObject* self, const std::string& kind,
                            const std::string& type, bool* ok) {
  std::string root = CtxExternalRoot();
  if (!root.empty() && root.back() == '/') root.pop_back();
  std::string pkg = CtxGetPackageNameString(self);
  std::string path;
  if (kind == "media") {
    path = root + "/Android/media/" + pkg;
  } else {
    path = root + "/Android/data/" + pkg + "/" + kind;
    if (!type.empty()) {
      if (type.find('/') != std::string::npos || type.find('\0') != std::string::npos) {
        state.ThrowNew("Ljava/lang/IllegalArgumentException;", "bad external dir type");
        *ok = false;
        return nullptr;
      }
      path += "/" + type;
    }
  }
  EnsureDirPath(path);  // best-effort（AOSP：失败仅日志）
  return CtxMakeFile(state, path, ok);
}

}  // namespace
Value CtxGetExternalFilesDir(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  std::string type = n > 0 ? Mutf8Arg(args[0]) : "";
  Value r{};
  r.ref = CtxExternalDirFace(state, self, "files", type, ok);
  return r;
}

Value CtxGetExternalCacheDir(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)args;
  (void)n;
  Value r{};
  r.ref = CtxExternalDirFace(state, self, "cache", "", ok);
  return r;
}

namespace {
Value CtxGetExternalMediaDir(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)args;
  (void)n;
  Value r{};
  r.ref = CtxExternalDirFace(state, self, "media", "", ok);
  return r;
}

// File[] 单元素装配（AOSP getExternal*Dirs：单存储面投影一档，多卷后置）
Value CtxDirArray(VMState& state, ObjHeader* dir, bool* ok) {
  VClass* file_cls = state.linker->FindClass("Ljava/io/File;");
  VClass* arr_cls = file_cls != nullptr ? state.linker->FindArrayClass(file_cls) : nullptr;
  VArray* arr = arr_cls != nullptr ? state.linker->AllocArray(arr_cls, 1) : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "getExternal*Dirs");
    *ok = false;
    return Value{};
  }
  SetArrRef(arr, 0, dir);
  Value r{};
  r.ref = arr;
  return r;
}

}  // namespace
Value CtxGetExternalFilesDirs(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  Value dir = CtxGetExternalFilesDir(state, self, args, n, ok);
  if (!*ok) return Value{};
  return CtxDirArray(state, dir.ref, ok);
}

Value CtxGetExternalCacheDirs(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)args;
  (void)n;
  Value dir = CtxGetExternalCacheDir(state, self, nullptr, 0, ok);
  if (!*ok) return Value{};
  return CtxDirArray(state, dir.ref, ok);
}

Value CtxGetExternalMediaDirs(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)args;
  (void)n;
  Value dir = CtxGetExternalMediaDir(state, self, nullptr, 0, ok);
  if (!*ok) return Value{};
  return CtxDirArray(state, dir.ref, ok);
}

// AOSP makeFilename：名内路径分隔符非法（IllegalArgumentException）
namespace {
bool CtxSimpleName(VMState& state, const std::string& name, const char* what) {
  if (!name.empty() && name.find('/') == std::string::npos) return true;
  state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                 std::string("Bad ") + what + ": " + name);
  return false;
}

}  // namespace
Value CtxGetDataDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  return CtxDirFace(state, "", true, ok);
}

Value CtxGetFilesDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  return CtxDirFace(state, "files", true, ok);
}

// Context.openFileOutput(name, mode)：<filesDir>/<name> FileOutputStream
// （AOSP ContextImpl.openFileOutput；MODE_APPEND=0x8000 映射 append 构造）。
Value CtxOpenFileOutput(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 2) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "openFileOutput args");
    *ok = false;
    return r;
  }
  std::string name = Mutf8Arg(args[0]);
  int32_t mode = args[1].i;
  if (!CtxSimpleName(state, name, "file name")) {
    *ok = false;
    return r;
  }
  bool dir_ok = true;
  Value dirv = CtxDirFace(state, "files", true, &dir_ok);
  if (!dir_ok || dirv.ref == nullptr) {
    *ok = false;
    return r;
  }
  VObject* dir_obj = reinterpret_cast<VObject*>(dirv.ref);
  VField* pf = FindInstanceField(dir_obj->klass, "path");
  ObjHeader* ps = pf != nullptr ? GetRefField(dir_obj, pf->offset) : nullptr;
  std::string dir = ps != nullptr ? static_cast<VString*>(ps)->ToModifiedUtf8() : "";
  std::string path = dir.empty() ? name : dir + "/" + name;
  bool append = (mode & 0x8000) != 0;  // MODE_APPEND
  bool lib_ok = true;
  Value sarg{};
  sarg.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(std::u16string(
      path.begin(), path.end())));
  if (append) {
    Value cargs[2] = {};
    cargs[0] = sarg;
    cargs[1].i = 1;
    VObject* fos = NewLibObject(state, "Ljava/io/FileOutputStream;",
                                {"Ljava/lang/String;", "Z"}, cargs, 2, &lib_ok);
    r.ref = reinterpret_cast<ObjHeader*>(fos);
  } else {
    VObject* fos = NewLibObject(state, "Ljava/io/FileOutputStream;", {"Ljava/lang/String;"},
                                &sarg, 1, &lib_ok);
    r.ref = reinterpret_cast<ObjHeader*>(fos);
  }
  if (!lib_ok) {
    state.ClearException();
    state.ThrowNew("Ljava/io/FileNotFoundException;", path);
    *ok = false;
    return r;
  }
  return r;
}

// Context.openFileInput(name)：<filesDir>/<name> FileInputStream（缺文件
// 抛 FileNotFoundException——AOSP openFileInput 语义）。
Value CtxOpenFileInput(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "openFileInput args");
    *ok = false;
    return r;
  }
  std::string name = Mutf8Arg(args[0]);
  if (!CtxSimpleName(state, name, "file name")) {
    *ok = false;
    return r;
  }
  bool dir_ok = true;
  Value dirv = CtxDirFace(state, "files", true, &dir_ok);
  if (!dir_ok || dirv.ref == nullptr) {
    *ok = false;
    return r;
  }
  VObject* dir_obj = reinterpret_cast<VObject*>(dirv.ref);
  VField* pf = FindInstanceField(dir_obj->klass, "path");
  ObjHeader* ps = pf != nullptr ? GetRefField(dir_obj, pf->offset) : nullptr;
  std::string dir = ps != nullptr ? static_cast<VString*>(ps)->ToModifiedUtf8() : "";
  std::string path = dir + "/" + name;
  bool lib_ok = true;
  Value sarg{};
  sarg.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(path.begin(), path.end())));
  VObject* fis = NewLibObject(state, "Ljava/io/FileInputStream;", {"Ljava/lang/String;"}, &sarg,
                              1, &lib_ok);
  if (!lib_ok) {
    state.ClearException();
    state.ThrowNew("Ljava/io/FileNotFoundException;", path);
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(fis);
  return r;
}

Value CtxGetCacheDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  return CtxDirFace(state, "cache", true, ok);
}

Value CtxGetCodeCacheDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  return CtxDirFace(state, "code_cache", true, ok);
}

Value CtxGetNoBackupFilesDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  return CtxDirFace(state, "no_backup", true, ok);
}

// AOSP getDir(name, mode)：<root>/<name> 取用即建（mode 忽略 = MODE_PRIVATE）
Value CtxGetDir(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : std::string();
  if (!CtxSimpleName(state, name, "directory name")) {
    *ok = false;
    return Value{};
  }
  return CtxDirFace(state, name, true, ok);
}

// AOSP getDatabasePath(name)：相对名落 <root>/databases/（databases 目录取用即建）；
// 绝对名按 AOSP 拆父目录 + basename（保证 basename 无目录穿越，直用原路径）。
Value CtxGetDatabasePath(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : std::string();
  if (name.empty()) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "empty database name");
    *ok = false;
    return Value{};
  }
  if (name[0] == '/') {
    Value r{};
    r.ref = CtxMakeFile(state, name, ok);
    return r;
  }
  if (!CtxSimpleName(state, name, "database name")) {
    *ok = false;
    return Value{};
  }
  if (!EnsureDirPath(CtxDataRoot() + "/databases")) {
    state.ThrowNew("Ljava/lang/IllegalStateException;",
                   "cannot create dir " + CtxDataRoot() + "/databases");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = CtxMakeFile(state, CtxDataRoot() + "/databases/" + name, ok);
  return r;
}

// 数据库目录项（getDatabaseNames/databaseList 共用口径）：缺失目录 = 空集。
namespace {
std::vector<std::string> CtxDatabaseEntries() {
  std::vector<std::string> names;
  std::string dir = CtxDataRoot() + "/databases";
  DIR* d = ::opendir(dir.c_str());
  if (d == nullptr) return names;
  while (struct dirent* e = ::readdir(d)) {
    std::string nm = e->d_name;
    if (nm == "." || nm == "..") continue;
    names.push_back(nm);
  }
  ::closedir(d);
  return names;
}

// getDatabaseNames()：数据库目录项名集（String[]）。
}  // namespace
Value CtxGetDatabaseNames(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self; (void)args; (void)n;
  std::vector<std::string> names = CtxDatabaseEntries();
  VClass* str_cls = state.linker->FindClass("Ljava/lang/String;");
  VClass* sarr_cls = str_cls != nullptr ? state.linker->FindArrayClass(str_cls) : nullptr;
  VArray* arr = sarr_cls != nullptr
                    ? state.linker->AllocArray(sarr_cls, static_cast<int32_t>(names.size()))
                    : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "getDatabaseNames");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < names.size(); i++) {
    std::u16string u(names[i].begin(), names[i].end());
    SetArrRef(arr, static_cast<int32_t>(i),
              reinterpret_cast<ObjHeader*>(state.linker->AllocString(u)));
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

// deleteDatabase(name)：AOSP 口径（主文件删除即成，缺失即真）；伴随日志
// （-journal/-wal/-shm/-mj，SQLiteDatabase.deleteDatabase 语义）一并清除。
Value CtxDeleteDatabase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : std::string();
  if (name.empty()) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "empty database name");
    *ok = false;
    return Value{};
  }
  std::string path;
  if (name[0] == '/') {
    path = name;
  } else {
    if (!CtxSimpleName(state, name, "database name")) {
      *ok = false;
      return Value{};
    }
    path = CtxDataRoot() + "/databases/" + name;
  }
  static const char* kCompanions[] = {"", "-journal", "-wal", "-shm", "-mj"};
  for (const char* suffix : kCompanions) {
    ::unlink((path + suffix).c_str());
  }
  struct stat st;
  Value r{};
  r.i = (::stat(path.c_str(), &st) != 0) ? 1 : 0;
  *ok = true;
  return r;
}

// AOSP getSharedPreferencesPath(name)：<root>/shared_prefs/<name>.xml
// （preferences 目录取用即建，AOSP getPreferencesDir 口径）。
Value CtxGetSharedPreferencesPath(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  (void)self;
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : std::string();
  if (!CtxSimpleName(state, name, "preferences name")) {
    *ok = false;
    return Value{};
  }
  if (!EnsureDirPath(CtxDataRoot() + "/shared_prefs")) {
    state.ThrowNew("Ljava/lang/IllegalStateException;",
                   "cannot create dir " + CtxDataRoot() + "/shared_prefs");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = CtxMakeFile(state, CtxDataRoot() + "/shared_prefs/" + name + ".xml", ok);
  return r;
}

// ---- android.content.Context.getSharedPreferences（lib 缝，§31.7 B 档）----
// SP 面整体在 lib（runtime-libs android.content.SharedPreferences*，随
// runtime.dex 打包；纯 Java 零 native 面/零布局需求）。本钩子仅转调 lib
// SharedPreferencesImpl.getInstance(name)（per-name 单例内存 Map；mode 忽略
// =MODE_PRIVATE 语义），除此之外无内建面。
namespace {
VMethod* SpFindStatic(VClass* k, const char* name, std::vector<std::string> params,
                      const char* ret) {
  if (k == nullptr) return nullptr;
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& m : c->direct_methods) {
      if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) {
        return &m;
      }
    }
    for (auto& m : c->virtual_methods) {
      if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) {
        return &m;
      }
    }
  }
  return nullptr;
}

}  // namespace
Value CtxGetSharedPreferences(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self;
  VClass* impl = state.linker->FindClass("Landroid/content/SharedPreferencesImpl;");
  if (impl == nullptr || !CheckClassDefinition(state, impl) ||
      !state.linker->InitializeClass(impl, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/NoClassDefFoundError;",
                     "Landroid/content/SharedPreferencesImpl;");
    }
    *ok = false;
    return Value{};
  }
  VMethod* get = SpFindStatic(impl, "getInstance", {"Ljava/lang/String;"},
                              "Landroid/content/SharedPreferences;");
  if (get == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;",
                   "Landroid/content/SharedPreferencesImpl;.getInstance(Ljava/lang/String;)");
    *ok = false;
    return Value{};
  }
  Value arg{};
  if (n >= 1) arg = args[0];  // guest String 透传（lib 侧 null -> ""）
  Value r{};
  if (!InvokeMethod(state, get, &arg, 1, &r)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return r;
}

// ---- android.content.VmPrefsStore：SharedPreferencesImpl 落盘钩子 ----
// lib SP 面（android.content.SharedPreferencesImpl）是进程内 Map；本钩子把
// 键值表落到 <ctxDataRoot>/shared_prefs/<name>.xml（与 AOSP 路径同源），
// 让 guest 账户/配置跨会话持久化——实测账户初始化依赖本地 guest 账户，不落盘
// 则每次启动 currentAccount 为空，Zonfig 配置拉取失败（无首页 tab）。
namespace {
std::string PrefsStorePath(const std::string& name) {
  return CtxDataRoot() + "/shared_prefs/" + name + ".xml";
}
}  // namespace

namespace {
// AOSP 不校验 prefs 名（空名合法）；VM 仅拒绝路径分隔符防越界写。
bool PrefsNameSafe(const std::string& name) {
  return name.find('/') == std::string::npos && name.find('\0') == std::string::npos;
}
}  // namespace

Value VmPrefsRead(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  std::string name = Mutf8Arg(args[0]);
  if (!PrefsNameSafe(name)) {
    *ok = false;
    return r;
  }
  std::ifstream in(PrefsStorePath(name), std::ios::binary);
  if (!in) return r;
  std::string data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  // 内容为 ASCII 线格式（base64 编码键值），逐字节宽化即可
  std::u16string u(data.begin(), data.end());
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(u));
  return r;
}

Value VmPrefsWrite(VMState& state, VObject*, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 2 || args[0].ref == nullptr) return Value{};
  std::string name = Mutf8Arg(args[0]);
  if (!PrefsNameSafe(name)) {
    *ok = false;
    return Value{};
  }
  EnsureDirPath(CtxDataRoot() + "/shared_prefs");
  std::ofstream out(PrefsStorePath(name), std::ios::binary | std::ios::trunc);
  if (out) {
    std::string data = Mutf8Arg(args[1]);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
  }
  return Value{};
}

// ---- Resources 面（docs 20 章；首个消费方 = FileProvider meta-data）----
// §31.7 分界：arsc/zip/AXML 字节解析 = VM native 缝（session/），本层只做
// guest 类面 + 事件流摊平（对象字段态 = SBState 同款 GC 安全模式）。
// 消费链（androidx FileProvider 真实字节码）：attachInfo -> getPathStrategy ->
// pm.resolveContentProvider -> info.loadXmlMetaData(pm, key) ->
// pm.getResourcesForApplication(ai).getXml(resId) -> XmlResourceParser
// next/getName/getAttributeValue(null, "name"/"path")。

ResourceSeam g_resource_seam = {};
VObject* g_context_resources = nullptr;
std::map<std::string, VObject*> g_resources_by_apk;   // apk -> Resources（GC 钉住）
std::map<std::string, VObject*> g_provider_registry;  // authority -> ProviderInfo（GC 钉住）

// UTF-8 -> UTF-16（AXML 字符串面保真；非法序列 U+FFFD，同 lifecycle_driver 口径）
namespace {
std::u16string ResToU16(const std::string& s) {
  std::u16string out;
  out.reserve(s.size());
  size_t i = 0;
  while (i < s.size()) {
    uint32_t cp = 0;
    size_t len = 1;
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      cp = c;
    } else if ((c & 0xE0) == 0xC0) {
      cp = c & 0x1Fu;
      len = 2;
    } else if ((c & 0xF0) == 0xE0) {
      cp = c & 0x0Fu;
      len = 3;
    } else if ((c & 0xF8) == 0xF0) {
      cp = c & 0x07u;
      len = 4;
    } else {
      out.push_back(0xFFFD);
      i++;
      continue;
    }
    if (i + len > s.size()) {
      out.push_back(0xFFFD);
      break;
    }
    bool bad = false;
    for (size_t k = 1; k < len; k++) {
      const unsigned char cc = static_cast<unsigned char>(s[i + k]);
      if ((cc & 0xC0) != 0x80) {
        bad = true;
        break;
      }
      cp = (cp << 6) | (cc & 0x3Fu);
    }
    if (bad) {
      out.push_back(0xFFFD);
      i++;
      continue;
    }
    if (cp >= 0x10000) {
      cp -= 0x10000;
      out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
    } else {
      out.push_back(static_cast<char16_t>(cp));
    }
    i += len;
  }
  return out;
}

VString* ResAllocStr(VMState& state, const std::string& s) {
  return state.linker->AllocString(ResToU16(s));
}

// XmlBlock$Parser 装配：AXML 事件流摊平入对象字段（GC 传递闭包安全）。
// 布局：mTypes[I]@0 mNames[Ljava/lang/String;]@8 mAttrKeys[..]@16 mAttrVals[..]@24
// mSpan[I]@32（每事件 start,count 两槽）mPos[I]@40（下一个事件游标，0 起）。
VObject* MakeXmlParserObject(VMState& state, const std::vector<session::XmlResEvent>& events) {
  ClassLinker* linker = state.linker;
  VClass* klass = linker->FindClass("Landroid/content/res/XmlBlock$Parser;");
  VClass* str_cls = linker->FindClass("Ljava/lang/String;");
  VClass* int_arr_cls = linker->FindArrayClass(linker->PrimitiveClass('I'));
  VClass* str_arr_cls = str_cls != nullptr ? linker->FindArrayClass(str_cls) : nullptr;
  if (klass == nullptr || int_arr_cls == nullptr || str_arr_cls == nullptr) return nullptr;
  size_t attr_total = 0;
  for (const auto& e : events) attr_total += e.attrs.size();
  VObject* obj = linker->AllocObject(klass);
  VArray* types = linker->AllocArray(int_arr_cls, static_cast<int32_t>(events.size()));
  VArray* names = linker->AllocArray(str_arr_cls, static_cast<int32_t>(events.size()));
  VArray* keys = linker->AllocArray(str_arr_cls, static_cast<int32_t>(attr_total));
  VArray* vals = linker->AllocArray(str_arr_cls, static_cast<int32_t>(attr_total));
  VArray* span = linker->AllocArray(int_arr_cls, static_cast<int32_t>(events.size() * 2));
  if (obj == nullptr || types == nullptr || names == nullptr || keys == nullptr ||
      vals == nullptr || span == nullptr) {
    return nullptr;
  }
  int32_t attr_cursor = 0;
  for (size_t i = 0; i < events.size(); i++) {
    const session::XmlResEvent& e = events[i];
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(e.type));
    std::memcpy(types->data() + i * 8, &slot, sizeof(slot));
    if (!e.name.empty()) {
      SetArrRef(names, static_cast<int32_t>(i),
                reinterpret_cast<ObjHeader*>(ResAllocStr(state, e.name)));
    }
    slot = static_cast<uint64_t>(static_cast<uint32_t>(attr_cursor));
    std::memcpy(span->data() + (i * 2) * 8, &slot, sizeof(slot));
    slot = static_cast<uint64_t>(static_cast<uint32_t>(e.attrs.size()));
    std::memcpy(span->data() + (i * 2 + 1) * 8, &slot, sizeof(slot));
    for (const auto& a : e.attrs) {
      SetArrRef(keys, attr_cursor, reinterpret_cast<ObjHeader*>(ResAllocStr(state, a.name)));
      SetArrRef(vals, attr_cursor, reinterpret_cast<ObjHeader*>(ResAllocStr(state, a.value)));
      attr_cursor++;
    }
  }
  SetRefField(obj, 0, reinterpret_cast<ObjHeader*>(types));
  SetRefField(obj, 8, reinterpret_cast<ObjHeader*>(names));
  SetRefField(obj, 16, reinterpret_cast<ObjHeader*>(keys));
  SetRefField(obj, 24, reinterpret_cast<ObjHeader*>(vals));
  SetRefField(obj, 32, reinterpret_cast<ObjHeader*>(span));
  SetIField(obj, 40, 0);
  return obj;
}

// Resources 实例（每 APK 一张表；缓存单例 = AOSP AssetManager 每包单例同型）
}  // namespace
VObject* MakeResourcesObject(VMState& state, const std::string& apk_path,
                             const std::string& package_name) {
  auto it = g_resources_by_apk.find(apk_path);
  if (it != g_resources_by_apk.end()) return it->second;
  ClassLinker* linker = state.linker;
  VClass* klass = linker->FindClass("Landroid/content/res/Resources;");
  if (klass == nullptr) return nullptr;
  VObject* obj = linker->AllocObject(klass);
  if (obj == nullptr) return nullptr;
  SetRefField(obj, 0, reinterpret_cast<ObjHeader*>(ResAllocStr(state, apk_path)));
  if (!package_name.empty()) {
    SetRefField(obj, 8, reinterpret_cast<ObjHeader*>(ResAllocStr(state, package_name)));
  }
  g_resources_by_apk[apk_path] = obj;
  if (state.heap != nullptr) state.heap->Pin(obj);  // 缓存根（同 Looper 单例口径）
  return obj;
}

namespace {
std::string ResObjApkPath(VObject* obj) {
  if (obj == nullptr) return "";
  VField* f = FindInstanceField(obj->klass, "mApkPath");
  ObjHeader* s = f != nullptr ? GetRefField(obj, f->offset) : nullptr;
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : "";
}

std::string ResObjPackageName(VObject* obj) {
  if (obj == nullptr) return "";
  VField* f = FindInstanceField(obj->klass, "mPackageName");
  ObjHeader* s = f != nullptr ? GetRefField(obj, f->offset) : nullptr;
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : "";
}

std::string ResObjStringField(VObject* obj, const char* name) {
  if (obj == nullptr) return "";
  VField* f = FindInstanceField(obj->klass, name);
  ObjHeader* s = f != nullptr ? GetRefField(obj, f->offset) : nullptr;
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : "";
}

// Resources.getIdentifier(name, defType, defPackage)：未命中 = 0（AOSP 口径，不抛）
}  // namespace
Value ResGetIdentifier(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  *ok = true;
  Value r{};
  if (g_resource_seam.lookup_resource_id == nullptr) return r;
  std::string pkg = Mutf8Arg(args[2]);
  if (pkg.empty()) pkg = ResObjPackageName(self);
  r.i = g_resource_seam.lookup_resource_id(ResObjApkPath(self), pkg, Mutf8Arg(args[1]),
                                           Mutf8Arg(args[0]));
  return r;
}

// Resources.getResourcePackageName(int)：资源 id 归属包名。单 APK 安装面：
// 0x01 段 = framework（"android"），其余（0x7f 应用资源）= 本 Resources 包名。
Value ResGetResourcePackageName(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)n;
  *ok = true;
  Value r{};
  uint32_t res_id = static_cast<uint32_t>(args[0].i);
  std::string pkg = (res_id >> 24) == 0x01 ? "android" : ResObjPackageName(self);
  if (pkg.empty()) pkg = "android";
  r.ref = ResAllocStr(state, pkg);
  return r;
}

// Resources.getXml/openXmlResourceParser：resId -> 文件字节 -> 事件流 -> parser。
// 查无 = NotFoundException（AOSP Resources.NotFoundException 语义）。
Value ResOpenXmlParser(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  *ok = true;
  Value r{};
  int32_t res_id = static_cast<int32_t>(args[0].l);
  std::string apk = ResObjApkPath(self);
  if (g_resource_seam.open_resource_file == nullptr ||
      g_resource_seam.parse_xml_events == nullptr) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   "resource seam not installed (resId)");
    *ok = false;
    return r;
  }
  std::string entry;
  std::vector<uint8_t> bytes;
  std::string err;
  if (!g_resource_seam.open_resource_file(apk, static_cast<uint32_t>(res_id), &entry, &bytes,
                                          &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   err.empty() ? "resource not found" : err);
    *ok = false;
    return r;
  }
  std::vector<session::XmlResEvent> events;
  if (!g_resource_seam.parse_xml_events(bytes, &events, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   err.empty() ? "bad resource xml" : err);
    *ok = false;
    return r;
  }
  VObject* parser = MakeXmlParserObject(state, events);
  if (parser == nullptr) {
    ThrowOOM(state, "XmlResourceParser");
    *ok = false;
    return r;
  }
  r.ref = parser;
  return r;
}

// Resources.getString/getText：resId -> arsc 字符串值（值面缝）。查无 =
// NotFoundException（AOSP Resources.NotFoundException 语义）。
Value ResGetString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  *ok = true;
  Value r{};
  int32_t res_id = static_cast<int32_t>(args[0].l);
  std::string apk = ResObjApkPath(self);
  if (g_resource_seam.lookup_string == nullptr) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   "resource seam not installed (string)");
    *ok = false;
    return r;
  }
  std::string out;
  std::string err;
  if (!g_resource_seam.lookup_string(apk, static_cast<uint32_t>(res_id), &out, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   err.empty() ? "String resource ID not found" : err);
    *ok = false;
    return r;
  }
  r.ref = ResAllocStr(state, out);
  return r;
}

// Resources.getStringArray/getTextArray：resId -> arsc 数组值 -> String[]。
Value ResGetStringArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  *ok = true;
  Value r{};
  int32_t res_id = static_cast<int32_t>(args[0].l);
  std::string apk = ResObjApkPath(self);
  if (g_resource_seam.lookup_string_array == nullptr) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   "resource seam not installed (string-array)");
    *ok = false;
    return r;
  }
  std::vector<std::string> out;
  std::string err;
  if (!g_resource_seam.lookup_string_array(apk, static_cast<uint32_t>(res_id), &out, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;",
                   err.empty() ? "Array resource ID not found" : err);
    *ok = false;
    return r;
  }
  VClass* str_cls = state.linker->FindClass("Ljava/lang/String;");
#if defined(__ANDROID__)
  // TEMP DIAG（NetworkMonitorDecorator 空串集合归因；事后移除）。
  if (out.size() == 4) {
    bool all_empty = true;
    for (const auto& s : out) {
      if (!s.empty()) all_empty = false;
    }
    if (all_empty) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmNet",
                          "getStringArray res=0x%08x n=4 ALL EMPTY", res_id);
    }
  }
#endif
  VClass* sarr_cls = str_cls != nullptr ? state.linker->FindArrayClass(str_cls) : nullptr;
  VArray* arr =
      sarr_cls != nullptr ? state.linker->AllocArray(sarr_cls, static_cast<int32_t>(out.size()))
                          : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "getStringArray");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < out.size(); i++) {
    SetArrRef(arr, static_cast<int32_t>(i),
              reinterpret_cast<ObjHeader*>(ResAllocStr(state, out[i])));
  }
  r.ref = arr;
  return r;
}

// XmlBlock$Parser.next()：XmlPullParser 事件流（1=END_DOCUMENT 2=START_TAG 3=END_TAG）
Value XmlNext(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* pos_f = FindInstanceField(self->klass, "mPos");
  VField* types_f = FindInstanceField(self->klass, "mTypes");
  int32_t pos = pos_f != nullptr ? GetIField(self, pos_f->offset) : 0;
  ObjHeader* arr = types_f != nullptr ? GetRefField(self, types_f->offset) : nullptr;
  VArray* types = reinterpret_cast<VArray*>(arr);
  if (types == nullptr || pos >= types->length) {
    r.i = 1;  // END_DOCUMENT（到尾恒定）
    return r;
  }
  uint64_t slot = 0;
  std::memcpy(&slot, types->data() + static_cast<size_t>(pos) * 8, sizeof(slot));
  r.i = static_cast<int32_t>(slot);
  if (pos_f != nullptr) SetIField(self, pos_f->offset, pos + 1);
  return r;
}

// 当前事件（mPos-1）属性游标辅助
namespace {
bool XmlCurrentSpan(VObject* self, int32_t* ev_idx, int32_t* start, int32_t* count) {
  VField* pos_f = FindInstanceField(self->klass, "mPos");
  VField* span_f = FindInstanceField(self->klass, "mSpan");
  ObjHeader* span_arr = span_f != nullptr ? GetRefField(self, span_f->offset) : nullptr;
  int32_t pos = pos_f != nullptr ? GetIField(self, pos_f->offset) : 0;
  VArray* span = reinterpret_cast<VArray*>(span_arr);
  if (span == nullptr || pos <= 0 || pos * 2 > span->length) return false;
  int32_t idx = pos - 1;
  uint64_t s0 = 0;
  uint64_t s1 = 0;
  std::memcpy(&s0, span->data() + static_cast<size_t>(idx) * 2 * 8, sizeof(s0));
  std::memcpy(&s1, span->data() + static_cast<size_t>(idx) * 2 * 8 + 8, sizeof(s1));
  *ev_idx = idx;
  *start = static_cast<int32_t>(s0);
  *count = static_cast<int32_t>(s1);
  return true;
}

}  // namespace
Value XmlGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  int32_t idx = 0, start = 0, count = 0;
  if (!XmlCurrentSpan(self, &idx, &start, &count)) return r;
  VField* names_f = FindInstanceField(self->klass, "mNames");
  ObjHeader* arr = names_f != nullptr ? GetRefField(self, names_f->offset) : nullptr;
  VArray* names = reinterpret_cast<VArray*>(arr);
  if (names == nullptr || idx >= names->length) return r;
  r.ref = GetArrRef(names, idx);
  if (r.ref == nullptr) {
    r.ref = ResAllocStr(state, "");  // 无名事件（END_DOCUMENT）= 空串非 null（最小面）
  }
  return r;
}

Value XmlGetAttributeValue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)n;
  *ok = true;
  Value r{};
  std::string want = Mutf8Arg(args[1]);
  int32_t idx = 0, start = 0, count = 0;
  if (!XmlCurrentSpan(self, &idx, &start, &count)) return r;
  VField* keys_f = FindInstanceField(self->klass, "mAttrKeys");
  VField* vals_f = FindInstanceField(self->klass, "mAttrVals");
  VArray* keys = reinterpret_cast<VArray*>(keys_f != nullptr ? GetRefField(self, keys_f->offset)
                                                             : nullptr);
  VArray* vals = reinterpret_cast<VArray*>(vals_f != nullptr ? GetRefField(self, vals_f->offset)
                                                             : nullptr);
  if (keys == nullptr || vals == nullptr) return r;
  // namespace 最小面：paths XML 属性无命名空间，按名匹配（AOSP getAttributeValue(ns,
  // name) 的 ns 参数在本消费面为 null；带前缀属性后置）
  for (int32_t i = 0; i < count; i++) {
    ObjHeader* k = GetArrRef(keys, start + i);
    if (k == nullptr) continue;
    if (static_cast<VString*>(k)->ToModifiedUtf8() == want) {
      r.ref = GetArrRef(vals, start + i);  // AOSP：未命中返回 null
      return r;
    }
  }
  return r;
}

// ProviderInfo.loadXmlMetaData(pm, name)（AOSP PackageItemInfo 语义）：
// meta-data int 资源 id -> pm.getResourcesForApplication(this.applicationInfo)
// -> Resources.getXml(resId)；无 meta-data/id = null。
Value PILoadXmlMetaData(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  Value r{};
  VField* md_f = FindInstanceField(self->klass, "metaData");
  ObjHeader* md = md_f != nullptr ? GetRefField(self, md_f->offset) : nullptr;
  if (md == nullptr) {
    *ok = true;
    return r;
  }
  Value k{};
  k.ref = args[1].ref;
  Value iv{};
  if (!CallVirtual(state, reinterpret_cast<VObject*>(md), "getInt", {"Ljava/lang/String;"}, "I",
                   &k, 1, &iv)) {
    *ok = false;
    return r;
  }
  int32_t res_id = static_cast<int32_t>(iv.l);
  if (res_id == 0) {
    *ok = true;
    return r;
  }
  VField* ai_f = FindInstanceField(self->klass, "applicationInfo");
  ObjHeader* ai = ai_f != nullptr ? GetRefField(self, ai_f->offset) : nullptr;
  if (args[0].ref == nullptr || ai == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "loadXmlMetaData: pm/applicationInfo");
    *ok = false;
    return r;
  }
  Value a{};
  a.ref = ai;
  Value rv{};
  if (!CallVirtual(state, reinterpret_cast<VObject*>(args[0].ref), "getResourcesForApplication",
                   {"Landroid/content/pm/ApplicationInfo;"}, "Landroid/content/res/Resources;",
                   &a, 1, &rv)) {
    *ok = false;
    return r;
  }
  if (rv.ref == nullptr) {
    *ok = true;
    return r;
  }
  Value x{};
  x.l = res_id;
  if (!CallVirtual(state, reinterpret_cast<VObject*>(rv.ref), "getXml", {"I"},
                   "Landroid/content/res/XmlResourceParser;", &x, 1, &r)) {
    *ok = false;
    return r;
  }
  return r;
}

// PackageManager.resolveContentProvider(authority, flags)（AOSP PMS 查询面 VM
// 等价：manifest 值投影注册表；flags 最小面忽略 GET_META_DATA 位——ProviderInfo
// 恒带 metaData 全量装配）
Value PMResolveContentProvider(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)state;
  (void)self;
  (void)n;
  *ok = true;
  Value r{};
  auto it = g_provider_registry.find(Mutf8Arg(args[0]));
  if (it != g_provider_registry.end()) r.ref = it->second;
  return r;
}

// PackageManager.getProviderInfo(ComponentName, flags)（AOSP PMS 查询面 VM
// 等价：manifest 值投影注册表按组件名匹配；flags 最小面同上）。未命中 =
// NameNotFoundException（AOSP ApplicationPackageManager.getProviderInfo 抛面，
// message = ComponentName.flattenToString()）。
Value PMGetProviderInfo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "component");
    *ok = false;
    return r;
  }
  VObject* comp = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
  std::string pkg = ResObjStringField(comp, "mPackage");
  std::string cls = ResObjStringField(comp, "mClass");
  for (const auto& kv : g_provider_registry) {
    VObject* info = kv.second;
    std::string name = ResObjStringField(info, "name");
    std::string ipkg = ResObjStringField(info, "packageName");
    bool name_ok = name == cls || (!pkg.empty() && name == pkg + "." + cls);
    bool pkg_ok = pkg.empty() || ipkg.empty() || ipkg == pkg;
    if (name_ok && pkg_ok) {
      r.ref = info;
      return r;
    }
  }
  state.ThrowNew("Ljava/lang/content/pm/PackageManager$NameNotFoundException;",
                 pkg + "/" + cls);
  *ok = false;
  return r;
}

// PackageManager.getResourcesForApplication(ApplicationInfo)：该包资源表
// （Resources(app) 语义：sourceDir 的 APK 资源面）
Value PMGetResourcesForApplication(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)self;
  (void)n;
  Value r{};
  VObject* ai = reinterpret_cast<VObject*>(args[0].ref);
  if (ai == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getResourcesForApplication: null info");
    *ok = false;
    return r;
  }
  std::string apk = ResObjStringField(ai, "sourceDir");
  std::string pkg = ResObjStringField(ai, "packageName");
  if (apk.empty()) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                   "getResourcesForApplication: no sourceDir");
    *ok = false;
    return r;
  }
  VObject* res = MakeResourcesObject(state, apk, pkg);
  if (res == nullptr) {
    ThrowOOM(state, "Resources");
    *ok = false;
    return r;
  }
  r.ref = res;
  *ok = true;
  return r;
}

// Resources.getSystem()：AOSP 系统资源单例。本 VM 不承载独立系统资源包，
// 以已装配的 app Resources 承担（display metrics/configuration 语义同源）；
// 未装配时抛 IllegalStateException（不能返回 null——Morph Dimensions.<clinit>
// 依赖 getDisplayMetrics，null 会连带 clinit 失败）。
Value ResGetSystem(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  if (g_context_resources == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "no Resources installed");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = g_context_resources;
  *ok = true;
  return r;
}

// Resources.getAssets()/Context.getAssets()：资产管理器单例（AOSP 每包一张
// AssetManager）。本 VM 资产读取归 Resources 资源缝；AssetManager 骨架实例恒在，
// lib open() 抛 IOException —— OkHttp PublicSuffixDatabase.readTheListUninterruptibly
// 捕获并降级（"Failed to read public suffix list"），但不接受 null 接收者。
namespace {
VObject* g_context_assets = nullptr;
}  // namespace

Value ResGetAssets(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  if (g_context_assets == nullptr) {
    VObject* am =
        NewLibObject(state, "Landroid/content/res/AssetManager;", {}, nullptr, 0, ok);
    if (am == nullptr) return Value{};
    g_context_assets = am;
  }
  Value r{};
  r.ref = g_context_assets;
  *ok = true;
  return r;
}

// Context.getResources()：app Resources 单例（AOSP ContextImpl.getResources 委派）
Value CtxGetResources(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  if (g_context_resources == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "no Resources installed");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = g_context_resources;
  *ok = true;
  return r;
}

// Context 资源面取用（getResources 同源；未装时按 ApplicationInfo.sourceDir
// 装配——AOSP ContextImpl.mResources 同构，semface/宿主测试装配优先）。
namespace {
VObject* CtxResolveResources(VMState& state, bool* ok) {
  if (g_context_resources != nullptr) return g_context_resources;
  if (g_context_app_info != nullptr) {
    std::string apk = ResObjStringField(g_context_app_info, "sourceDir");
    std::string pkg = ResObjStringField(g_context_app_info, "packageName");
    if (!apk.empty()) {
      VObject* res = MakeResourcesObject(state, apk, pkg);
      if (res != nullptr) {
        g_context_resources = res;
        return res;
      }
    }
  }
  state.ThrowNew("Ljava/lang/IllegalStateException;", "no Resources installed");
  *ok = false;
  return nullptr;
}

// Context.getMainLooper（AOSP ContextImpl：Looper.getMainLooper 静态委派）
}  // namespace

Value CtxGetMainLooper(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* looper_cls = state.linker->FindClass("Landroid/os/Looper;");
  if (looper_cls == nullptr) return r;
  for (auto& m : looper_cls->direct_methods) {
    if (m.name != "getMainLooper" || !m.IsStatic()) continue;
    Value rv{};
    if (InvokeMethod(state, &m, nullptr, 0, &rv)) r.ref = rv.ref;
    break;
  }
  return r;
}

// Context.getString/getText(int, Object[])：委派 Resources 带参串
Value CtxGetStringArgs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VObject* res = g_context_resources;
  if (res == nullptr || n < 2) return r;
  return ResGetStringArgs(state, res, args, n, ok);
}

// Context.obtainStyledAttributes 族：AOSP = getTheme().obtainStyledAttributes。
// 主题/样式表层（theme -> defStyleAttr -> defStyleRes 解析链）后置；当前最小面 =
// 空值 TypedArray（所有 getter 回落 defValue —— AOSP「主题未声明该属性」同值），
// 结构面齐备（getIndexCount/hasValue/getType/peekValue 全部按零值走）。
Value CtxObtainStyledAttributes(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  *ok = true;
  Value r{};
  // AOSP ContextImpl.obtainStyledAttributes：set == null 时落 Theme 解析链
  // （getTheme().obtainStyledAttributes(attrs)）；set 非 null = XML 属性集面。
  // 参数形：([I) / (AttributeSet,[I) / (AttributeSet,[I,I,I)；attrs 恒在 index 1
  // （含 set 重载）或 0（纯 [I）。
  size_t attrs_idx = 0;
  if (n >= 2 && args[1].ref != nullptr && args[1].ref->klass != nullptr &&
      args[1].ref->klass->descriptor == "[I") {
    attrs_idx = 1;
  } else {
    for (size_t i = 0; i < n; i++) {
      if (args[i].ref != nullptr && args[i].ref->klass != nullptr &&
          args[i].ref->klass->descriptor == "[I") {
        attrs_idx = i;
        break;
      }
    }
  }
  bool set_null = true;
  if (n >= 2) {
    // 带 AttributeSet 重载：set 为 args[0]，null = 落主题链
    if (args[0].ref != nullptr && args[0].ref->klass != nullptr &&
        args[0].ref->klass->descriptor != "[I") {
      set_null = false;
    }
  }
  if (set_null) {
    bool tok = true;
    Value theme = CtxGetTheme(state, self, nullptr, 0, &tok);
    if (tok && theme.ref != nullptr) {
      Value targs[1] = {};
      targs[0] = args[attrs_idx];
      bool t_ok = true;
      return ThemeObtainStyledSelf(state, reinterpret_cast<VObject*>(theme.ref), targs, 1,
                                   &t_ok);
    }
    *ok = false;
    return r;
  }
  // attrs 数组 = 最后一个 [I 参数（各重载一致）
  ObjHeader* attrs_ref = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (args[i].ref != nullptr && args[i].ref->klass != nullptr &&
        args[i].ref->klass->descriptor == "[I") {
      attrs_ref = args[i].ref;
    }
  }
  VClass* int_arr = state.linker->FindClass("[I");
  if (int_arr == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "int[] missing");
    *ok = false;
    return r;
  }
  int32_t length = 0;
  if (attrs_ref != nullptr) {
    length = reinterpret_cast<VArray*>(attrs_ref)->length;
  }
  // rsrcs = 请求的 attr id 表（零值 = 未解析）
  VArray* rsrcs = state.linker->AllocArray(int_arr, length);
  VArray* data = state.linker->AllocArray(int_arr, length * 6);
  if (rsrcs == nullptr || data == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "obtainStyledAttributes");
    *ok = false;
    return r;
  }
  VClass* ta_cls = state.linker->FindClass("Landroid/content/res/TypedArray;");
  if (ta_cls == nullptr || !CheckClassDefinition(state, ta_cls) ||
      !state.linker->InitializeClass(ta_cls, &state)) {
    state.ClearException();
    state.ThrowNew("Ljava/lang/InternalError;", "TypedArray missing");
    *ok = false;
    return r;
  }
  VObject* ta = state.linker->AllocObject(ta_cls);
  if (ta == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "TypedArray alloc");
    *ok = false;
    return r;
  }
  VMethod* ctor = nullptr;
  for (auto& m : ta_cls->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 4) {
      ctor = &m;
      break;
    }
  }
  if (ctor == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", "TypedArray.<init>");
    *ok = false;
    return r;
  }
  Value cargs[5] = {};
  cargs[0].ref = ta;
  cargs[1].ref = reinterpret_cast<ObjHeader*>(g_context_resources);
  cargs[2].ref = reinterpret_cast<ObjHeader*>(rsrcs);
  cargs[3].ref = reinterpret_cast<ObjHeader*>(data);
  cargs[4].i = length;
  Value ret{};
  if (!InvokeMethod(state, ctor, cargs, 5, &ret)) {
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(ta);
  return r;
}

// Context.obtainStyledAttributes(int resid, int[] attrs)：AOSP ContextImpl 口径
// = getTheme().obtainStyledAttributes(resid, attrs, 0, 0)。不可复用
// CtxObtainStyledAttributes：后者首参按引用解，int 实参会被当成指针解引用。
Value CtxObtainStyledAttrsRes(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  *ok = true;
  Value r{};
  if (n < 2 || args[1].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "attrs == null");
    *ok = false;
    return r;
  }
  bool tok = true;
  Value theme = CtxGetTheme(state, self, nullptr, 0, &tok);
  if (!tok || theme.ref == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "context has no theme");
    *ok = false;
    return r;
  }
  return ThemeObtainStyled28(state, reinterpret_cast<VObject*>(theme.ref), args, n, ok);
}

// ---- 资源主题面（Resources$Theme / Activity.getTheme）----
// 主题解析链：style 资源 -> parent 链上溯（LookupResourceStyle），attr id 命中
// 取 Res_value；TYPE_ATTRIBUTE 一跳再解析；TYPE_REFERENCE 经 lookup_value 收口。
// 产物 = lib TypedArray（mRsrcs/mData/mStrings 布局，getter 走真实值面）。

namespace {
int32_t g_app_theme_res_id = 0;

std::string ThemeApkPath(VObject* theme) {
  VObject* res = reinterpret_cast<VObject*>(GetRefField(theme, 0));
  if (res == nullptr) return "";
  VField* f = FindInstanceField(res->klass, "mApkPath");
  ObjHeader* s = f != nullptr ? GetRefField(res, f->offset) : nullptr;
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : "";
}

bool ResolveThemeItem(const std::string& apk, std::vector<uint32_t>* chain,
                      uint32_t attr_id, uint8_t* out_type, uint32_t* out_data,
                      uint32_t* out_rsrc, std::string* out_string) {
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_style == nullptr) return false;
  // attr -> 首个命中（近者优先：chain 从派生到基类）
  for (uint32_t style_id : *chain) {
    uint32_t cur = style_id;
    for (int depth = 0; depth < 32 && cur != 0; depth++) {
      uint32_t parent = 0;
      std::vector<session::StyleItem> items;
      std::string err;
      if (!seam->lookup_style(apk, cur, &parent, &items, &err)) break;
      for (const auto& it : items) {
        if (it.name != attr_id) continue;
        uint8_t t = it.type;
        uint32_t d = it.data;
        if (out_rsrc != nullptr) *out_rsrc = 0;
        if (t == 0x01 || t == 0x07) {  // reference：记 rsrc，解析终值
          if (out_rsrc != nullptr) *out_rsrc = d;
          uint8_t rt = 0;
          uint32_t rd = 0;
          std::string rs;
          std::string verr;
          if (seam->lookup_value != nullptr &&
              seam->lookup_value(apk, d, &rt, &rd, &rs, &verr)) {
            t = rt;
            d = rd;
            if (out_string != nullptr) *out_string = rs;
          }
        } else if (t == 0x02) {  // ?attr 主题引用（一跳）
          uint8_t rt = 0;
          uint32_t rd = 0;
          uint32_t rr = 0;
          std::string rs;
          if (ResolveThemeItem(apk, chain, d, &rt, &rd, &rr, &rs)) {
            t = rt;
            d = rd;
            if (out_string != nullptr) *out_string = rs;
          }
        } else if (t == 0x03 && out_string != nullptr) {  // TYPE_STRING
          *out_string = it.string;
        }
        *out_type = t;
        *out_data = d;
        return true;
      }
      cur = parent;
    }
  }
  return false;
}

// 主题对象懒建（Activity.getTheme / Context.getTheme 落点）
VObject* MakeThemeFor(VMState& state, VObject* resources, int32_t theme_res_id) {
  VClass* theme_cls = state.linker->FindClass("Landroid/content/res/Resources$Theme;");
  if (theme_cls == nullptr || !CheckClassDefinition(state, theme_cls) ||
      !state.linker->InitializeClass(theme_cls, &state)) {
    state.ClearException();
    return nullptr;
  }
  VObject* theme = state.linker->AllocObject(theme_cls);
  if (theme == nullptr) return nullptr;
  SetRefField(theme, 0, reinterpret_cast<ObjHeader*>(resources));
  VField* id_f = FindInstanceField(theme_cls, "mThemeResId");
  if (id_f != nullptr) SetIField(theme, id_f->offset, theme_res_id);
  return theme;
}

// TypedArray 装配（attrs -> 解析值；getBoolean/getInt/getString 真实可用）
VObject* BuildTypedArray(VMState& state, VObject* resources, const std::string& apk,
                         const std::vector<uint32_t>& chain, ObjHeader* attrs_ref) {
  VArray* attrs = reinterpret_cast<VArray*>(attrs_ref);
  int32_t length = attrs != nullptr ? attrs->length : 0;
  VClass* int_arr = state.linker->FindClass("[I");
  VClass* ta_cls = state.linker->FindClass("Landroid/content/res/TypedArray;");
  if (int_arr == nullptr || ta_cls == nullptr || !CheckClassDefinition(state, ta_cls) ||
      !state.linker->InitializeClass(ta_cls, &state)) {
    state.ClearException();
    return nullptr;
  }
  VArray* rsrcs = state.linker->AllocArray(int_arr, length);
  VArray* data = state.linker->AllocArray(int_arr, length * 6);
  if (rsrcs == nullptr || data == nullptr) return nullptr;
  std::vector<std::string> strings;
  int32_t resolved = 0;
  auto set_i = [&](VArray* a, int32_t idx, int32_t v) {
    std::memcpy(a->data() + static_cast<size_t>(idx) * 8, &v, sizeof(v));
  };
  for (int32_t i = 0; i < length; i++) {
    int32_t attr_id = 0;
    std::memcpy(&attr_id, attrs->data() + static_cast<size_t>(i) * 8, sizeof(attr_id));
    set_i(rsrcs, i, attr_id);
    uint8_t type = 0;
    uint32_t value = 0;
    uint32_t rsrc = 0;
    std::string str;
    if (!ResolveThemeItem(apk, const_cast<std::vector<uint32_t>*>(&chain),
                          static_cast<uint32_t>(attr_id), &type, &value, &rsrc, &str)) {
      continue;  // 未声明：TYPE_NULL（getter 回落 defValue）
    }
    resolved++;
    if (rsrc != 0) set_i(rsrcs, i, static_cast<int32_t>(rsrc));
    if (type == 0x03) {  // TYPE_STRING：data = mStrings 下标
      strings.push_back(str);
      set_i(data, i * 6, static_cast<int32_t>(type));
      set_i(data, i * 6 + 1, static_cast<int32_t>(strings.size() - 1));
    } else {
      set_i(data, i * 6, static_cast<int32_t>(type));
      set_i(data, i * 6 + 1, static_cast<int32_t>(value));
    }
  }
#if defined(__ANDROID__)
  {
    int32_t a0 = 0, a1 = 0;
    if (length > 0) std::memcpy(&a0, attrs->data(), 4);
    if (length > 1) std::memcpy(&a1, attrs->data() + 8, 4);
    std::string chain_ids;
    for (uint32_t id : chain) {
      char b[16];
      std::snprintf(b, sizeof(b), "0x%08x ", id);
      chain_ids += b;
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmTheme",
                        "obtainStyledAttributes len=%d resolved=%d chain=[%s] a0=0x%08x a1=0x%08x",
                        length, resolved, chain_ids.c_str(), a0, a1);
  }
#endif
  VObject* ta = state.linker->AllocObject(ta_cls);
  if (ta == nullptr) return nullptr;
  VMethod* ctor = nullptr;
  for (auto& m : ta_cls->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == 4) {
      ctor = &m;
      break;
    }
  }
  if (ctor == nullptr) return nullptr;
  Value cargs[5] = {};
  cargs[0].ref = ta;
  cargs[1].ref = reinterpret_cast<ObjHeader*>(resources);
  cargs[2].ref = reinterpret_cast<ObjHeader*>(rsrcs);
  cargs[3].ref = reinterpret_cast<ObjHeader*>(data);
  cargs[4].i = length;
  Value ret{};
  if (!InvokeMethod(state, ctor, cargs, 5, &ret)) return nullptr;
  if (!strings.empty()) {
    VClass* str_cls = state.linker->FindClass("Ljava/lang/String;");
    VClass* str_arr = str_cls != nullptr ? state.linker->FindArrayClass(str_cls) : nullptr;
    VArray* arr = str_arr != nullptr
                      ? state.linker->AllocArray(str_arr, static_cast<int32_t>(strings.size()))
                      : nullptr;
    if (arr != nullptr) {
      for (size_t i = 0; i < strings.size(); i++) {
        ObjHeader* s = reinterpret_cast<ObjHeader*>(state.linker->AllocString(
            std::u16string(strings[i].begin(), strings[i].end())));
        std::memcpy(arr->data() + i * 8, &s, sizeof(s));
      }
      if (VField* f = FindInstanceField(ta_cls, "mStrings")) {
        SetRefField(ta, f->offset, reinterpret_cast<ObjHeader*>(arr));
      }
    }
  }
  return ta;
}

std::vector<uint32_t> ThemeChain(int32_t theme_res_id) {
  std::vector<uint32_t> chain;
  if (theme_res_id != 0) chain.push_back(static_cast<uint32_t>(theme_res_id));
  return chain;
}

}  // namespace

void SetAppThemeResId(int32_t res_id) { g_app_theme_res_id = res_id; }
int32_t GetAppThemeResId() { return g_app_theme_res_id; }

// AOSP starting window 语义：Activity 主题的 android:windowBackground
// （WindowManager 在首帧前画的预览层）。主题选择 = activity theme 优先、
// application theme 兜底（AOSP ActivityInfo.getThemeResource()）。解析出
// 背景色 / 居中 bitmap（layer-list 的常见形制：item(color)+item(bitmap)）。
// 纯 VM 侧（Resources/Theme 同一套 seam），宿主只做显示。
bool ResolveStartingWindowTheme(const std::string& apk, int32_t theme_res_id,
                                uint32_t* out_color, std::vector<uint8_t>* out_bitmap,
                                int32_t* out_gravity) {
  if (out_color != nullptr) *out_color = 0xFFFFFFFFu;
  if (out_gravity != nullptr) *out_gravity = 0x11;  // Gravity.CENTER
  if (out_bitmap != nullptr) out_bitmap->clear();
  if (theme_res_id == 0) return false;
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_style == nullptr) return false;
  std::vector<uint32_t> chain = ThemeChain(theme_res_id);
  if (chain.empty()) return false;
  uint8_t type = 0;
  uint32_t data = 0;
  uint32_t rsrc = 0;
  std::string str;
  if (!ResolveThemeItem(apk, &chain, 0x01010054 /*android:windowBackground*/, &type, &data,
                        &rsrc, &str)) {
    return false;
  }
  // 颜色终值（TYPE_INT_COLOR_* 0x1c..0x1f）
  if (type >= 0x1c && type <= 0x1f) {
    if (out_color != nullptr) *out_color = data;
    return true;
  }
  uint32_t res = rsrc;
  if (res == 0 && (type == 0x01 || type == 0x07)) res = data;
  if (res == 0 || seam->open_resource_file == nullptr) return false;
  std::string entry;
  std::vector<uint8_t> bytes;
  std::string err;
  if (!seam->open_resource_file(apk, res, &entry, &bytes, &err) || bytes.empty()) return false;
  auto is_axml = [](const std::vector<uint8_t>& b) {
    return b.size() > 8 && b[0] == 0x03 && b[1] == 0x00 && b[2] == 0x08;
  };
  if (!is_axml(bytes)) {
    // 直接位图资源（PNG/WEBP）：整图作为居中开屏图。
    if (out_bitmap != nullptr) *out_bitmap = std::move(bytes);
    return out_bitmap != nullptr && !out_bitmap->empty();
  }
  if (seam->parse_xml_events == nullptr) return false;
  std::vector<session::XmlResEvent> events;
  if (!seam->parse_xml_events(bytes, &events, &err)) return false;
  bool got = false;
  // layer-list / bitmap drawable：android:drawable(0x01010199) 颜色项；
  // android:src(0x01010119) 位图项；android:gravity(0x010100af) 摆放。
  for (const auto& e : events) {
    if (e.type != session::XmlResEvent::kStartTag) continue;
    for (const auto& a : e.attrs) {
      if (a.res_id == 0x010100af) {
        if (out_gravity != nullptr) *out_gravity = static_cast<int32_t>(a.value_data);
        continue;
      }
      uint32_t ref = a.value_data;
      bool is_ref = a.value_type == 0x01 || a.value_type == 0x07;
      if (a.res_id == 0x01010199 && is_ref) {
        // item drawable：颜色 → 背景色；否则按资源文件取位图
        uint8_t vt = 0;
        uint32_t vd = 0;
        std::string vs;
        if (seam->lookup_value != nullptr &&
            seam->lookup_value(apk, ref, &vt, &vd, &vs, &err) && vt >= 0x1c && vt <= 0x1f) {
          if (out_color != nullptr) *out_color = vd;
          got = true;
        } else if (seam->open_resource_file != nullptr) {
          std::string e2;
          std::vector<uint8_t> b2;
          if (seam->open_resource_file(apk, ref, &e2, &b2, &err) && !b2.empty() &&
              !is_axml(b2) && out_bitmap != nullptr) {
            *out_bitmap = std::move(b2);
            got = true;
          }
        }
      } else if (a.res_id == 0x01010119 && is_ref) {
        // bitmap src：取位图文件字节
        std::string e2;
        std::vector<uint8_t> b2;
        if (seam->open_resource_file != nullptr &&
            seam->open_resource_file(apk, ref, &e2, &b2, &err) && !b2.empty() &&
            !is_axml(b2) && out_bitmap != nullptr) {
          *out_bitmap = std::move(b2);
          got = true;
        }
      }
    }
  }
  return got;
}

// 包装 Context 主题面（AOSP ContextThemeWrapper）：mThemeResId 字段优先于
// app 主题（wrapContext 的 defStyle 语义）；字段不存在回落 app 主题。
int32_t ContextOwnThemeResId(VObject* self) {
  if (self != nullptr) {
    if (VField* f = FindInstanceField(self->klass, "mThemeResId")) {
      int32_t id = GetIField(self, f->offset);
      if (id != 0) return id;
    }
  }
  return g_app_theme_res_id;
}

Value ContextThemeWrapperInitRes(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  *ok = true;
  if (n >= 1) {
    VField* base_f = FindInstanceField(self->klass, "mBase");
    if (base_f != nullptr) SetRefField(self, base_f->offset, args[0].ref);
  }
  if (n >= 2) {
    if (VField* f = FindInstanceField(self->klass, "mThemeResId")) {
      SetIField(self, f->offset, args[1].i);
    }
  }
  (void)state;
  return Value{};
}

Value ContextThemeWrapperInitTheme(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  *ok = true;
  if (n >= 1) {
    VField* base_f = FindInstanceField(self->klass, "mBase");
    if (base_f != nullptr) SetRefField(self, base_f->offset, args[0].ref);
  }
  (void)state;
  return Value{};
}

Value ContextThemeWrapperSetTheme(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    if (VField* f = FindInstanceField(self->klass, "mThemeResId")) {
      SetIField(self, f->offset, args[0].i);
    }
  }
  return Value{};
}

// Activity.getTheme()/Context.getTheme()：mTheme 懒建（app 主题 = manifest
// application/activity theme；未声明 = 0 = 全缺省）
Value CtxGetTheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VField* th_f = FindInstanceField(self->klass, "mTheme");
  VObject* theme = th_f != nullptr ? reinterpret_cast<VObject*>(GetRefField(self, th_f->offset))
                                   : nullptr;
  if (theme != nullptr) {
    r.ref = reinterpret_cast<ObjHeader*>(theme);
    return r;
  }
  VObject* res = g_context_resources;
  theme = MakeThemeFor(state, res, ContextOwnThemeResId(self));
  if (theme == nullptr) {
    *ok = false;
    return r;
  }
  if (th_f == nullptr) {
    // 类未预留 mTheme 槽：侧表（Activity 侧后续补槽前兼容）
    static std::map<VObject*, VObject*> s_themes;
    theme = s_themes[self] = theme;
  } else {
    SetRefField(self, th_f->offset, reinterpret_cast<ObjHeader*>(theme));
  }
  r.ref = reinterpret_cast<ObjHeader*>(theme);
  return r;
}

Value ActSetTheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    g_app_theme_res_id = args[0].i;  // 简化：单主题面（per-activity 后置）
  }
  (void)self;
  return Value{};
}

Value ThemeSetTo(VMState& state, VObject* state_src, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1 && args[0].ref != nullptr) {
    VObject* src = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[0].ref));
    SetRefField(state_src, 0, GetRefField(src, 0));
    VField* f = FindInstanceField(src->klass, "mThemeResId");
    if (f != nullptr) {
      VField* df = FindInstanceField(state_src->klass, "mThemeResId");
      if (df != nullptr) SetIField(state_src, df->offset, GetIField(src, f->offset));
    }
  }
  return Value{};
}

Value ThemeApplyStyle(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (n >= 1) {
    VField* f = FindInstanceField(self->klass, "mThemeResId");
    if (f != nullptr) SetIField(self, f->offset, args[0].i);
  }
  return Value{};
}

Value ThemeObtainStyledSelf(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  VField* f = FindInstanceField(self->klass, "mThemeResId");
  int32_t theme_id = f != nullptr ? GetIField(self, f->offset) : 0;
  std::string apk = ThemeApkPath(self);
  VObject* res = reinterpret_cast<VObject*>(GetRefField(self, 0));
  VObject* ta = BuildTypedArray(state, res, apk, ThemeChain(theme_id), args[0].ref);
  if (ta == nullptr) {
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(ta);
  return r;
}

// Theme.obtainStyledAttributes(int resid, int[] attrs[, int defStyleAttr, int defStyleRes])
Value ThemeObtainStyled28(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 2 || args[1].ref == nullptr) return r;
  std::string apk = ThemeApkPath(self);
  VObject* res = reinterpret_cast<VObject*>(GetRefField(self, 0));
  std::vector<uint32_t> chain;
  if (args[0].i != 0) chain.push_back(static_cast<uint32_t>(args[0].i));
  VField* f = FindInstanceField(self->klass, "mThemeResId");
  if (f != nullptr) {
    int32_t theme_id = GetIField(self, f->offset);
    if (theme_id != 0) chain.push_back(static_cast<uint32_t>(theme_id));
  }
  VObject* ta = BuildTypedArray(state, res, apk, chain, args[1].ref);
  if (ta == nullptr) {
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(ta);
  return r;
}

// Theme.obtainStyledAttributes(AttributeSet, int[]...)：XML 属性集面后置——
// 空值 TypedArray（与 Context 同口径，getter 走 defValue）。
Value ThemeObtainStyledSet(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  ObjHeader* attrs_ref = nullptr;
  for (size_t i = 0; i < n; i++) {
    if (args[i].ref != nullptr && args[i].ref->klass != nullptr &&
        args[i].ref->klass->descriptor == "[I") {
      attrs_ref = args[i].ref;
    }
  }
  if (attrs_ref == nullptr) return r;
  // 复用 Context 空值口径（attrs 数组即参数）
  return CtxObtainStyledAttributes(state, self, args, n, ok);
}

Value ThemeResolveAttribute(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 2 || args[1].ref == nullptr) return r;
  VField* f = FindInstanceField(self->klass, "mThemeResId");
  int32_t theme_id = f != nullptr ? GetIField(self, f->offset) : 0;
  std::string apk = ThemeApkPath(self);
  uint8_t type = 0;
  uint32_t data = 0;
  uint32_t rsrc = 0;
  std::string str;
  std::vector<uint32_t> chain = ThemeChain(theme_id);
  if (!ResolveThemeItem(apk, &chain, static_cast<uint32_t>(args[0].i), &type, &data, &rsrc,
                        &str)) {
    return r;  // false = 未解析（AOSP 同）
  }
  // TypedValue 字段：type@0, data@8（lib TypedValue 布局）
  VObject* tv = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[1].ref));
  VField* tf = FindInstanceField(tv->klass, "type");
  VField* df = FindInstanceField(tv->klass, "data");
  if (tf != nullptr) SetIField(tv, tf->offset, type);
  if (df != nullptr) SetIField(tv, df->offset, static_cast<int32_t>(data));
  VField* rf = FindInstanceField(tv->klass, "resourceId");
  if (rf != nullptr) SetIField(tv, rf->offset, static_cast<int32_t>(rsrc));
  r.i = 1;
  return r;
}

// ---- Resources 值面批（AOSP Resources 常用 getter 组）----
namespace {
// themed 资源解析：reference 链 + dimension complex 数值换算
bool LookupDimPx(const std::string& apk, uint32_t res_id, float* out, std::string* err) {
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_value == nullptr) return false;
  uint8_t t = 0;
  uint32_t d = 0;
  std::string s;
  if (!seam->lookup_value(apk, res_id, &t, &d, &s, err)) return false;
  auto complex_to_float = [](uint32_t data) {
    // AOSP TypedValue.complexToFloat：24 位尾数 × RADIX_MULTS[radix]
    // （radix 3 = 0.5dp 的 0x40000031 形态；忽略 radix 会放大 2^23 倍）。
    static constexpr float kRadixMults[4] = {
        1.0f / 256.0f,
        1.0f / (256.0f * 128.0f),
        1.0f / (256.0f * 32768.0f),
        1.0f / (256.0f * 8388608.0f),
    };
    const int32_t mantissa = static_cast<int32_t>(data & 0xFFFFFF00u);
    const int radix = static_cast<int>((data >> 4) & 0x3);
    return static_cast<float>(mantissa) * kRadixMults[radix];
  };
  switch (t) {
    case 0x05: {  // TYPE_DIMENSION
      const int unit = static_cast<int>(d & 0xF);
      float v = complex_to_float(d);
      // 与 ResDisplayMetrics（density/scaledDensity=2、dpi=320）同口径。
      switch (unit) {
        case 1: *out = v * 2.0f; break;          // dp
        case 2: *out = v * 2.0f; break;          // sp
        case 3: *out = v * 320.0f / 72.0f; break;
        case 4: *out = v * 320.0f; break;
        case 5: *out = v * 320.0f / 25.4f; break;
        default: *out = v; break;                // px
      }
      return true;
    }
    case 0x06:  // TYPE_FRACTION（以 100% = 1.0 口径近似，base 由调用方乘）
      *out = complex_to_float(d);
      return true;
    case 0x04: {  // TYPE_FLOAT
      float f = 0;
      std::memcpy(&f, &d, sizeof(f));
      *out = f;
      return true;
    }
    case 0x10:
    case 0x11:
      *out = static_cast<float>(static_cast<int32_t>(d));
      return true;
    default:
      return false;
  }
}

bool LookupColorValue(const std::string& apk, uint32_t res_id, int32_t* out, std::string* err) {
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_value == nullptr) return false;
  uint8_t t = 0;
  uint32_t d = 0;
  std::string s;
  if (!seam->lookup_value(apk, res_id, &t, &d, &s, err)) return false;
  switch (t) {
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x10:
    case 0x11:
      *out = static_cast<int32_t>(d);
      return true;
    case 0x03: {
      // "#RRGGBB" 字符串色值
      if (s.size() >= 2 && s[0] == '#') {
        char* end = nullptr;
        unsigned long v = std::strtoul(s.c_str() + 1, &end, 16);
        if (s.size() == 7) v |= 0xFF000000ul;
        *out = static_cast<int32_t>(v);
        return true;
      }
      return false;
    }
    default:
      return false;
  }
}

std::string ResApkPath(VObject* res) {
  if (res == nullptr) return "";
  VField* f = FindInstanceField(res->klass, "mApkPath");
  ObjHeader* s = f != nullptr ? GetRefField(res, f->offset) : nullptr;
  return s != nullptr ? static_cast<VString*>(s)->ToModifiedUtf8() : "";
}

// DisplayMetrics 单例（guest 窗口 540x960 / density 2.0：与桥接口径一致）
VObject* ResDisplayMetrics(VMState& state, VObject* res) {
  VField* f = FindInstanceField(res->klass, "mDisplayMetrics");
  if (f != nullptr) {
    VObject* existing = reinterpret_cast<VObject*>(GetRefField(res, f->offset));
    if (existing != nullptr) return existing;
  }
  static VObject* fallback = nullptr;
  bool dm_ok = true;
  VObject* dm = NewLibObject(state, "Landroid/util/DisplayMetrics;", {}, nullptr, 0, &dm_ok);
  if (dm == nullptr) dm = fallback;
  if (dm == nullptr) return nullptr;
  fallback = dm;
  auto seti = [&](const char* n, int32_t v) {
    if (VField* ff = FindInstanceField(dm->klass, n)) SetIField(dm, ff->offset, v);
  };
  auto setf = [&](const char* n, float v) {
    if (VField* ff = FindInstanceField(dm->klass, n)) SetFField(dm, ff->offset, v);
  };
  seti("widthPixels", 540);
  seti("heightPixels", 960);
  setf("density", 2.0f);
  seti("densityDpi", 320);
  setf("scaledDensity", 2.0f);
  setf("xdpi", 320.0f);
  setf("ydpi", 320.0f);
  if (f != nullptr) SetRefField(res, f->offset, reinterpret_cast<ObjHeader*>(dm));
  return dm;
}
}  // namespace

Value ResGetDisplayMetrics(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(ResDisplayMetrics(state, self));
  if (r.ref == nullptr) *ok = false;
  return r;
}

Value ResGetDimensionPx(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  float px = 0;
  std::string err;
  if (LookupDimPx(ResApkPath(self), static_cast<uint32_t>(args[0].i), &px, &err)) {
    r.i = static_cast<int32_t>(std::lround(px));
  } else {
    r.i = n >= 2 ? args[1].i : 0;
  }
  return r;
}

Value ResGetDimension(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  float px = 0;
  std::string err;
  if (LookupDimPx(ResApkPath(self), static_cast<uint32_t>(args[0].i), &px, &err)) {
    r.f = px;
  } else {
    r.f = n >= 2 ? args[1].f : 0.0f;
  }
  return r;
}

Value ResGetColorStateList(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  int32_t color = 0;
  std::string err;
  if (!LookupColorValue(ResApkPath(self), static_cast<uint32_t>(args[0].i), &color, &err)) {
    return r;  // 查无：null（AOSP theme-less 且非 color = 抛 NotFound；桥接面取容忍）
  }
  // lib ColorStateList 无 (I) 构造器：走公开工厂 valueOf(int)（单色态表）。
  VClass* csl_cls = state.linker != nullptr
                        ? state.linker->FindClass("Landroid/content/res/ColorStateList;")
                        : nullptr;
  VMethod* value_of = csl_cls != nullptr
                          ? FindStaticM(csl_cls, "valueOf", {"I"},
                                        "Landroid/content/res/ColorStateList;")
                          : nullptr;
  if (value_of != nullptr) {
    Value cargs[1] = {};
    cargs[0].i = color;
    Value out{};
    if (InvokeMethod(state, value_of, cargs, 1, &out)) {
      r.ref = out.ref;
    } else {
      state.ClearException();  // 颜色值面容忍：缺省回落 null
    }
  }
  return r;
}

// 字符串格式化（String.format 最小面：%s/%d/%1$s 顺序替换）
std::u16string FormatArgs(VMState& state, const std::string& fmt, ObjHeader* args_arr) {
  VArray* arr = reinterpret_cast<VArray*>(args_arr);
  int32_t argi = 0;
  auto next_arg = [&]() -> ObjHeader* {
    if (arr == nullptr || argi >= arr->length) return nullptr;
    ObjHeader* v = nullptr;
    std::memcpy(&v, arr->data() + static_cast<size_t>(argi) * 8, sizeof(v));
    argi++;
    return v;
  };
  std::u16string out;
  for (size_t i = 0; i < fmt.size(); i++) {
    if (fmt[i] != '%' || i + 1 >= fmt.size()) {
      out.push_back(static_cast<char16_t>(static_cast<unsigned char>(fmt[i])));
      continue;
    }
    char c = fmt[i + 1];
    if (c == '%') {
      out.push_back(u'%');
      i++;
      continue;
    }
    if (c == 's' || c == 'd' || c == 'f') {
      ObjHeader* a = next_arg();
      i++;
      if (a == nullptr) continue;
      if (a->klass != nullptr && a->klass->descriptor == "Ljava/lang/String;") {
        VString* vs = static_cast<VString*>(a);
        for (int32_t i = 0; i < vs->length; i++) {
          out.push_back(static_cast<char16_t>(vs->chars()[i]));
        }
      } else {
        // 数值近似：经 Object.toString 虚拟派发
        Value recv{};
        recv.ref = a;
        if (VMState* st = &state; st != nullptr) {
          VMethod probe;
          probe.name = "toString";
          probe.return_type = "Ljava/lang/String;";
          if (VMethod* m = a->klass != nullptr ? a->klass->FindInterfaceImpl(&probe) : nullptr) {
            Value rv{};
            if (InvokeMethod(state, m, &recv, 1, &rv) && rv.ref != nullptr) {
              VString* rs = static_cast<VString*>(rv.ref);
              for (int32_t i = 0; i < rs->length; i++) {
                out.push_back(static_cast<char16_t>(rs->chars()[i]));
              }
            }
          }
        }
      }
      continue;
    }
    out.push_back(static_cast<char16_t>(static_cast<unsigned char>(fmt[i])));
  }
  return out;
}
// Resources.getText/getString(int, Object[])：资源串 + %s/%d 顺序替换
Value ResGetStringArgs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 2) return r;
  const ResourceSeam* seam = GetResourceSeam();
  std::string text;
  std::string err;
  if (seam == nullptr || seam->lookup_string == nullptr ||
      !seam->lookup_string(ResApkPath(self), static_cast<uint32_t>(args[0].i), &text, &err)) {
    return r;
  }
  std::u16string formatted = FormatArgs(state, text, args[1].ref);
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(formatted));
  return r;
}

Value ResGetQuantityString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 2) return r;
  const ResourceSeam* seam = GetResourceSeam();
  std::string text;
  std::string err;
  if (seam == nullptr || seam->lookup_string == nullptr ||
      !seam->lookup_string(ResApkPath(self), static_cast<uint32_t>(args[0].i), &text, &err)) {
    return r;
  }
  // 数量型：无 plural 表，按 %d 替换 quantity（AOSP 单复数由 arsc plurals 决定）
  ObjHeader* arr = n >= 3 ? args[2].ref : nullptr;
  size_t pos = text.find("%d");
  if (pos != std::string::npos) {
    text = text.substr(0, pos) + std::to_string(args[1].i) + text.substr(pos + 2);
  }
  std::u16string formatted = FormatArgs(*&state, text, arr);
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocString(formatted));
  return r;
}

Value ResGetInteger(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const ResourceSeam* seam = GetResourceSeam();
  uint8_t t = 0;
  uint32_t d = 0;
  std::string s;
  std::string err;
  if (seam != nullptr && seam->lookup_value != nullptr &&
      seam->lookup_value(ResApkPath(self), static_cast<uint32_t>(args[0].i), &t, &d, &s, &err)) {
    r.i = static_cast<int32_t>(d);
  } else {
    r.i = n >= 2 ? args[1].i : 0;  // getInteger(id, defValue) 缺省面
  }
  return r;
}

Value ResGetBoolean(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const ResourceSeam* seam = GetResourceSeam();
  uint8_t t = 0;
  uint32_t d = 0;
  std::string s;
  std::string err;
  if (seam != nullptr && seam->lookup_value != nullptr &&
      seam->lookup_value(ResApkPath(self), static_cast<uint32_t>(args[0].i), &t, &d, &s, &err)) {
    r.i = d != 0 ? 1 : 0;
  } else {
    r.i = n >= 2 ? args[1].i : 0;
  }
  return r;
}

// android.content.res.VmAssets.open(String)：lib AssetManager.open 的 VM 钩子
// （资产缝：assets/<name> 从 APK zip 读原始字节，stored/deflate 同源）。
// 未命中返回 null —— lib 侧转 IOException（AOSP FileNotFoundException 同族）。
Value VmAssetsOpen(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  const ResourceSeam* seam = GetResourceSeam();
  std::string apk = ResApkPath(g_context_resources);
  if (seam == nullptr || seam->open_asset_file == nullptr || apk.empty()) return r;
  std::string name = Mutf8Arg(args[0]);
  std::vector<uint8_t> bytes;
  std::string err;
  if (!seam->open_asset_file(apk, name, &bytes, &err)) {
#if defined(__ANDROID__)
    // 诊断（临时）：资产未命中清单（Tab 配置等动态资产名归因）。
    __android_log_print(ANDROID_LOG_INFO, "DexVmAsset", "miss %s", name.c_str());
#endif
    return r;
  }
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmAsset", "open %s (%zu bytes)", name.c_str(),
                      bytes.size());
#endif
  VClass* byte_arr = state.linker->FindClass("[B");
  if (byte_arr == nullptr) return r;
  VArray* arr = state.linker->AllocArray(byte_arr, static_cast<int32_t>(bytes.size()));
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "VmAssets.open");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < bytes.size(); i++) {
    std::memcpy(arr->data() + i * 8, &bytes[i], 1);
  }
  r.ref = arr;
  return r;
}

Value ResOpenRawResource(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const ResourceSeam* seam = GetResourceSeam();
  std::string entry_path;
  std::vector<uint8_t> bytes;
  std::string err;
  VClass* byte_arr = state.linker->FindClass("[B");
  if (seam == nullptr || seam->open_resource_file == nullptr || byte_arr == nullptr) {
    *ok = false;
    return r;
  }
  if (!seam->open_resource_file(ResApkPath(self), static_cast<uint32_t>(args[0].i), &entry_path,
                                &bytes, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", err);
    *ok = false;
    return r;
  }
  VArray* arr = state.linker->AllocArray(byte_arr, static_cast<int32_t>(bytes.size()));
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "openRawResource");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < bytes.size(); i++) {
    std::memcpy(arr->data() + i * 8, &bytes[i], 1);  // 字节低槽写入（8 字节槽约定）
  }
  bool lib_ok = true;
  Value cargs[1] = {};
  cargs[0].ref = reinterpret_cast<ObjHeader*>(arr);
  VObject* stream = NewLibObject(state, "Ljava/io/ByteArrayInputStream;", {"[B"}, cargs, 1,
                                 &lib_ok);
  if (!lib_ok) {
    state.ClearException();
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(stream);
  return r;
}

Value ResResourceName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_name == nullptr) return r;
  std::string pkg, type, entry, err;
  if (!seam->lookup_name(ResApkPath(self), static_cast<uint32_t>(args[0].i), &pkg, &type,
                         &entry, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", err);
    *ok = false;
    return r;
  }
  std::string out = pkg + ":" + type + "/" + entry;
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(out.begin(), out.end())));
  return r;
}

Value ResResourceEntryName(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_name == nullptr) return r;
  std::string pkg, type, entry, err;
  if (!seam->lookup_name(ResApkPath(self), static_cast<uint32_t>(args[0].i), &pkg, &type,
                         &entry, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", err);
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(entry.begin(), entry.end())));
  return r;
}

Value ResResourceTypeName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_name == nullptr) return r;
  std::string pkg, type, entry, err;
  if (!seam->lookup_name(ResApkPath(self), static_cast<uint32_t>(args[0].i), &pkg, &type,
                         &entry, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", err);
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(
      state.linker->AllocString(std::u16string(type.begin(), type.end())));
  return r;
}

// Resources.getValue(int id, TypedValue outValue, boolean resolveRefs)：arsc
// 终值填 TypedValue（AppCompatDrawableManager 等消费面）。
Value ResGetValue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 2 || args[1].ref == nullptr) return r;
  std::string apk;
  VField* pf = FindInstanceField(self->klass, "mApkPath");
  ObjHeader* ps = pf != nullptr ? GetRefField(self, pf->offset) : nullptr;
  if (ps != nullptr) apk = static_cast<VString*>(ps)->ToModifiedUtf8();
  const ResourceSeam* seam = GetResourceSeam();
  uint8_t type = 0;
  uint32_t data = 0;
  std::string str;
  std::string err;
  bool found = seam != nullptr && seam->lookup_value != nullptr &&
               seam->lookup_value(apk, static_cast<uint32_t>(args[0].i), &type, &data, &str,
                                  &err);
  VObject* tv = reinterpret_cast<VObject*>(const_cast<ObjHeader*>(args[1].ref));
  if (!found) {
    r.i = 0;  // AOSP：查无 = false（outValue 不变）
    return r;
  }
  if (VField* f = FindInstanceField(tv->klass, "type")) SetIField(tv, f->offset, type);
  if (VField* f = FindInstanceField(tv->klass, "data")) {
    SetIField(tv, f->offset, static_cast<int32_t>(data));
  }
  if (type == 0x03) {
    if (VField* f = FindInstanceField(tv->klass, "string")) {
      SetRefField(tv, f->offset,
                  reinterpret_cast<ObjHeader*>(state.linker->AllocString(
                      std::u16string(str.begin(), str.end()))));
    }
  }
  r.i = 1;
  return r;
}

// VectorDrawableCompat.mutate 覆写：桥接占位 drawable 的 state 对象已登记
// resId；mutate 会深度复制 state，克隆后反查丢失（底部导航图标即此路径）。
// mutate 返回 self（占位无可变像素状态），保持 state 身份稳定。
Value VectorDrawableMutateSelf(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = self;
  return r;
}

// ---- Drawable 资源登记（桥接快照按 ImageView.mDrawable 反查 resId）----
namespace {
std::mutex g_drawable_res_mu;
std::map<ObjHeader*, uint32_t> g_drawable_res;

void RegisterDrawableOne(VMState& state, ObjHeader* o, uint32_t res_id) {
  if (o == nullptr) return;
  std::lock_guard<std::mutex> lk(g_drawable_res_mu);
  if (g_drawable_res.count(o) != 0) return;
  g_drawable_res.emplace(o, res_id);
  if (state.heap != nullptr) state.heap->Pin(o);  // 侧表键：GC 根
}

void RegisterDrawableRefs(VMState& state, ObjHeader* o, uint32_t res_id, int depth) {
  RegisterDrawableOne(state, o, res_id);
  if (o == nullptr || o->klass == nullptr || depth >= 1) return;
  VObject* v = reinterpret_cast<VObject*>(o);
  uint8_t* data = v->field_data();
  for (auto& f : o->klass->instance_fields) {
    if (f.type_descriptor.empty() || f.type_descriptor[0] != 'L') continue;
    ObjHeader* r = nullptr;
    std::memcpy(&r, data + f.offset, sizeof(r));
    if (r != nullptr) RegisterDrawableOne(state, r, res_id);
  }
}
}  // namespace

void RegisterGuestDrawableResource(VMState& state, VObject* drawable, uint32_t res_id,
                                   bool include_state_refs) {
  if (drawable == nullptr || res_id == 0) return;
  if (include_state_refs) {
    RegisterDrawableRefs(state, reinterpret_cast<ObjHeader*>(drawable), res_id, 0);
  } else {
    RegisterDrawableOne(state, reinterpret_cast<ObjHeader*>(drawable), res_id);
  }
}

uint32_t ResolveGuestDrawableResource(VObject* drawable) {
  if (drawable == nullptr) return 0;
  std::lock_guard<std::mutex> lk(g_drawable_res_mu);
  std::set<ObjHeader*> seen;
  std::vector<ObjHeader*> queue{reinterpret_cast<ObjHeader*>(drawable)};
  for (int depth = 0; depth < 4 && !queue.empty(); depth++) {
    std::vector<ObjHeader*> next;
    for (ObjHeader* o : queue) {
      if (o == nullptr || !seen.insert(o).second) continue;
      auto it = g_drawable_res.find(o);
      if (it != g_drawable_res.end()) return it->second;
      if (o->klass == nullptr) continue;
      VObject* v = reinterpret_cast<VObject*>(o);
      uint8_t* data = v->field_data();
      for (auto& f : o->klass->instance_fields) {
        if (f.type_descriptor.empty() || f.type_descriptor[0] != 'L') continue;
        ObjHeader* r = nullptr;
        std::memcpy(&r, data + f.offset, sizeof(r));
        if (r != nullptr) next.push_back(r);
      }
    }
    queue.swap(next);
  }
  return 0;
}

void ResetGuestDrawableResources() {
  std::lock_guard<std::mutex> lk(g_drawable_res_mu);
  g_drawable_res.clear();
}

// Resources.getDrawable(int)：资源为颜色值 -> ColorDrawable（真颜色可绘制）；
// 其它类型（文件位图/XML drawable）桥接图元面后置 -> null（AOSP 无资源同形）。
Value ResGetDrawable(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) return r;
  std::string apk;
  VField* pf = FindInstanceField(self->klass, "mApkPath");
  ObjHeader* ps = pf != nullptr ? GetRefField(self, pf->offset) : nullptr;
  if (ps != nullptr) apk = static_cast<VString*>(ps)->ToModifiedUtf8();
  const ResourceSeam* seam = GetResourceSeam();
  uint8_t type = 0;
  uint32_t data = 0;
  std::string str;
  std::string err;
  if (seam == nullptr || seam->lookup_value == nullptr ||
      !seam->lookup_value(apk, static_cast<uint32_t>(args[0].i), &type, &data, &str, &err)) {
    return r;
  }
  bool is_color = type == 0x1c || type == 0x1d || type == 0x1e || type == 0x1f ||
                  type == 0x10 || type == 0x11;
  if (!is_color) {
    // XML drawable（vector/layer-list 等文件资源）：返回 VectorDrawableCompat
    // 实例（AOSP 路径 = Resources 装载 XML drawable —— 类型面等价；像素绘制归
    // 桥接，本层不消费）。类名按 androidx 混淆/非混淆两形探测（单一实现仍为
    // 该库自身类，非 VM 伪造）。
    bool xml = type == 0x03 && str.size() >= 4 &&
               str.compare(str.size() - 4, 4, ".xml") == 0;
    if (!xml) return r;
    for (const char* desc : {"Landroidx/vectordrawable/graphics/drawable/VectorDrawableCompat;",
                             "Landroidx/vectordrawable/a/a/i;"}) {
      VClass* vd = state.linker->FindClass(desc);
      if (vd == nullptr) continue;
      if (!CheckClassDefinition(state, vd) || !state.linker->InitializeClass(vd, &state)) {
        state.ClearException();
        continue;
      }
      // mutate() 不得复制 state（否则 drawable→resId 侧表在克隆后失效）。
      for (auto& m : vd->virtual_methods) {
        if (m.name == "mutate" && m.param_types.empty()) {
          m.native_fn = ToIntrinsicSlot(VectorDrawableMutateSelf);
        }
      }
      VObject* obj = state.linker->AllocObject(vd);
      if (obj == nullptr) continue;
      bool ctor_ok = false;
      for (auto& m : vd->direct_methods) {
        if (m.name != "<init>" || !m.param_types.empty()) continue;  // 无参构造
        Value cargs[1] = {};
        cargs[0].ref = obj;
        Value ret{};
        ctor_ok = InvokeMethod(state, &m, cargs, 1, &ret);
        if (!ctor_ok) state.ClearException();
        break;
      }
      if (ctor_ok) {
        r.ref = reinterpret_cast<ObjHeader*>(obj);
        // 占位 drawable 本身没有 XML 内容；登记 resId 供桥接快照转交宿主
        // 用 guest APK 真实资源表装载（图标等 XML drawable 出画通路）。
        // 连状态对象一起登记：调用方常 getConstantState().newDrawable() 克隆。
        RegisterGuestDrawableResource(state, obj, static_cast<uint32_t>(args[0].i), true);
        return r;
      }
    }
    return r;
  }
  VClass* cd_cls = state.linker->FindClass("Landroid/graphics/drawable/ColorDrawable;");
  if (cd_cls == nullptr || !CheckClassDefinition(state, cd_cls) ||
      !state.linker->InitializeClass(cd_cls, &state)) {
    state.ClearException();
    return r;
  }
  VObject* cd = state.linker->AllocObject(cd_cls);
  if (cd == nullptr) return r;
  for (auto& m : cd_cls->direct_methods) {
    if (m.name != "<init>" || m.param_types.size() != 1 || m.param_types[0] != "I") continue;
    Value cargs[2] = {};
    cargs[0].ref = cd;
    cargs[1].i = static_cast<int32_t>(data);
    Value ret{};
    if (InvokeMethod(state, &m, cargs, 2, &ret)) {
      r.ref = reinterpret_cast<ObjHeader*>(cd);
      RegisterGuestDrawableResource(state, cd, static_cast<uint32_t>(args[0].i), false);
    }
    break;
  }
  return r;
}

// Resources.newTheme()：AOSP 新建空主题（绑定本 Resources；ContextThemeWrapper
// 包装链消费——随后 setTo/applyStyle 注入主题资源）。
Value ResNewTheme(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(MakeThemeFor(state, self, 0));
  if (r.ref == nullptr) *ok = false;
  return r;
}

Value ResGetResourcesSelf(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = GetRefField(self, 0);
  return r;
}

// 资源 color 解析（arsc 终值：color/int 位型）；失败 = NotFound 语义。
namespace {
bool ResolveColorRes(const std::string& apk_path, uint32_t res_id, int32_t* out,
                     std::string* err) {
  const ResourceSeam* seam = GetResourceSeam();
  if (seam == nullptr || seam->lookup_value == nullptr) return false;
  uint8_t t = 0;
  uint32_t d = 0;
  std::string s;
  if (!seam->lookup_value(apk_path, res_id, &t, &d, &s, err)) return false;
  switch (t) {
    case 0x1c:
    case 0x1d:
    case 0x1e:
    case 0x1f:
    case 0x10:
    case 0x11:
      *out = static_cast<int32_t>(d);
      return true;
    default:
      return false;
  }
}
}  // namespace

// Context.getColor(int resId)：AOSP ContextImpl.getColor -> Resources.getColor。
Value CtxGetColor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "getColor: missing id");
    *ok = false;
    return r;
  }
  VObject* res = g_context_resources;
  std::string apk;
  if (res != nullptr) {
    VField* f = FindInstanceField(res->klass, "mApkPath");
    ObjHeader* s = f != nullptr ? GetRefField(res, f->offset) : nullptr;
    if (s != nullptr) apk = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  int32_t color = 0;
  std::string err;
  if (!ResolveColorRes(apk, static_cast<uint32_t>(args[0].i), &color, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", "getColor: " + err);
    *ok = false;
    return r;
  }
  r.i = color;
  return r;
}

// Resources.getColor(int)：资源表单例自身 mApkPath 解析（AppCompat 消费面）。
Value ResGetColor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  if (n < 1) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "getColor: missing id");
    *ok = false;
    return r;
  }
  std::string apk;
  if (self != nullptr) {
    VField* f = FindInstanceField(self->klass, "mApkPath");
    ObjHeader* s = f != nullptr ? GetRefField(self, f->offset) : nullptr;
    if (s != nullptr) apk = static_cast<VString*>(s)->ToModifiedUtf8();
  }
  int32_t color = 0;
  std::string err;
  if (!ResolveColorRes(apk, static_cast<uint32_t>(args[0].i), &color, &err)) {
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", "getColor: " + err);
    *ok = false;
    return r;
  }
  r.i = color;
  return r;
}

// TextView.setText(int resId)：AOSP TextView.setText(int) =
// getContext().getResources().getText(id)。Context 资源单例未装 =
// IllegalStateException（同 getString 语义）；查无 = Resources.NotFoundException。
Value TVSetTextRes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1) return Value{};
  VObject* res = g_context_resources;
  if (res == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "no Resources installed");
    *ok = false;
    return Value{};
  }
  std::string apk = ResObjStringField(res, "mApkPath");
  const ResourceSeam* seam = GetResourceSeam();
  std::string text;
  std::string err;
  bool got = seam != nullptr && seam->lookup_string != nullptr &&
             seam->lookup_string(apk, static_cast<uint32_t>(args[0].i), &text, &err);
  if (!got) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "setText: resource 0x%08x not found", args[0].i);
    state.ThrowNew("Landroid/content/res/Resources$NotFoundException;", buf);
    *ok = false;
    return Value{};
  }
  VField* f = FindInstanceField(self->klass, "mText");
  if (f != nullptr) {
    SetRefField(self, f->offset,
                reinterpret_cast<ObjHeader*>(state.linker->AllocString(ResToU16(text))));
  }
  return Value{};
}

Value CtxGetString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  VObject* res = CtxResolveResources(state, ok);
  if (res == nullptr) return Value{};
  Value r{};
  if (!CallVirtual(state, res, "getString", {"I"}, "Ljava/lang/String;", args, 1, &r)) {
    *ok = false;
  }
  return r;
}

Value CtxGetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  VObject* res = CtxResolveResources(state, ok);
  if (res == nullptr) return Value{};
  Value r{};
  if (!CallVirtual(state, res, "getString", {"I"}, "Ljava/lang/String;", args, 1, &r)) {
    *ok = false;
  }
  return r;
}

Value CtxGetStringArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  VObject* res = CtxResolveResources(state, ok);
  if (res == nullptr) return Value{};
  Value r{};
  if (!CallVirtual(state, res, "getStringArray", {"I"}, "[Ljava/lang/String;", args, 1, &r)) {
    *ok = false;
  }
  return r;
}

Value CtxGetTextArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  VObject* res = CtxResolveResources(state, ok);
  if (res == nullptr) return Value{};
  Value r{};
  if (!CallVirtual(state, res, "getStringArray", {"I"}, "[Ljava/lang/String;", args, 1, &r)) {
    *ok = false;
  }
  return r;
}


}  // namespace dexvm::runtime
