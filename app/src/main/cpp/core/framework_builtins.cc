// Guest framework 内建类 registrar（瘦身后）：共享助手 + RegisterFrameworkBuiltins
// 注册聚合 + Context/Resources seam 公开面 + 消息泵入口。各语义面实现按 A 档分组
// 拆分到 core/framework_os.cc / framework_app.cc / framework_view.cc（纯移动重构，
// 行为不变）。跨 TU 共享面声明见 core/framework_builtins_internal.h。

#include "core/framework_builtins.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <unistd.h>

#include <dirent.h>
#include <sys/stat.h>

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
#include "core/framework_dialog.h"
#include "core/toast_bridge.h"

namespace dexvm::runtime {

// Window callback 侧表复位（framework_app.cc 实现体；builtins_internal.h 同名
// 声明不重复引入——kAcc* 常量会二义）
void ResetWindowCallbacks();


namespace {
constexpr uint32_t kAccPublic = 0x1;
constexpr uint32_t kAccPrivate = 0x2;
constexpr uint32_t kAccProtected = 0x4;
constexpr uint32_t kAccStatic = 0x8;
constexpr uint32_t kAccFinal = 0x10;
constexpr uint32_t kAccConstructor = 0x10000;
constexpr uint32_t kAccInterface = 0x200;
constexpr uint32_t kAccAbstract = 0x400;

VClass* AllocClass(ClassLinker* linker) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->status = ClassStatus::kInitialized;
  return k;
}

// instance_bytes：实例字段区字节数（对象头之外）；klass 元指针随后统一填。
VClass* MakeClass(ClassLinker* linker, std::vector<VClass*>* all, const char* descriptor,
                  VClass* super, uint32_t instance_bytes, uint32_t access) {
  VClass* k = AllocClass(linker);
  if (k == nullptr) return nullptr;
  k->descriptor = descriptor;
  k->super_class = super;
  k->access_flags = access;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + instance_bytes;
  all->push_back(k);
  return k;
}

void AddMethodSkeleton(VClass* klass, const char* name, std::vector<std::string> param_types,
                       const char* return_type, uint32_t access) {
  VMethod m;
  m.declaring = klass;
  m.name = name;
  m.param_types = std::move(param_types);
  m.return_type = return_type;
  std::string shorty;
  shorty.push_back(return_type[0] == 'L' || return_type[0] == '[' ? 'L' : return_type[0]);
  for (const auto& p : m.param_types) {
    shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
  }
  m.shorty = shorty;
  m.access_flags = access;
  m.code = nullptr;
  klass->direct_methods.push_back(std::move(m));
}

void AddIntrinsicMethod(VClass* klass, const char* name, std::vector<std::string> param_types,
                        const char* return_type, uint32_t access, IntrinsicFn fn) {
  AddMethodSkeleton(klass, name, std::move(param_types), return_type, access);
  klass->direct_methods.back().native_fn = ToIntrinsicSlot(fn);
}

// 实例字段（内置类不走 LinkClass：offset 手工布局，8 字节槽与链接器一致）。
void AddInstanceField(VClass* klass, const char* name, const char* type, uint32_t offset) {
  VField f;
  f.declaring = klass;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = kAccPrivate;
  f.offset = offset;
  f.is_static = false;
  klass->instance_fields.push_back(std::move(f));
}

void AddStaticField(VClass* klass, const char* name, const char* type) {
  VField f;
  f.declaring = klass;
  f.name = name;
  f.type_descriptor = type;
  f.access_flags = kAccPrivate | kAccStatic;
  f.offset = static_cast<uint32_t>(klass->static_fields.size());
  f.is_static = true;
  klass->static_fields.push_back(std::move(f));
  // 全宽清零（Value{} 聚合初始化只清首成员 4 字节，ref 槽高位不可脏）
  Value zero;
  std::memset(&zero, 0, sizeof(zero));
  klass->static_storage.resize(klass->static_fields.size(), zero);
}

// ---- 对象字段槽辅助（8 字节槽） ----

}  // namespace
VField* FindInstanceField(VClass* klass, const char* name) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == name) return &f;
    }
  }
  return nullptr;
}

ObjHeader* GetRefField(VObject* o, uint32_t off) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}

void SetRefField(VObject* o, uint32_t off, ObjHeader* v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

namespace {
int32_t GetIField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  return static_cast<int32_t>(slot);
}

void SetIField(VObject* o, uint32_t off, int32_t v) {
  uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
  std::memcpy(o->field_data() + off, &slot, sizeof(slot));
}

}  // namespace
void SetFField(VObject* o, uint32_t off, float v) {
  uint64_t slot = 0;
  std::memcpy(&slot, &v, sizeof(v));
  std::memcpy(o->field_data() + off, &slot, sizeof(slot));
}

namespace {
float GetFField(VObject* o, uint32_t off) {
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + off, sizeof(slot));
  float v = 0;
  std::memcpy(&v, &slot, sizeof(v));
  return v;
}

// 数组元素（8 字节槽；引用按原始指针存取，同 AllocArray 布局）。
}  // namespace
ObjHeader* GetArrRef(VArray* a, int32_t i) {
  ObjHeader* v = nullptr;
  std::memcpy(&v, a->data() + static_cast<size_t>(i) * 8, sizeof(v));
  return v;
}

void SetArrRef(VArray* a, int32_t i, ObjHeader* v) {
  std::memcpy(a->data() + static_cast<size_t>(i) * 8, &v, sizeof(v));
}

namespace {
std::string Mutf8Arg(const Value& v) {
  if (v.ref == nullptr) return "";
  return static_cast<VString*>(v.ref)->ToModifiedUtf8();
}

// ---- 通用 intrinsic ----

// 空实现（void 生命周期方法：可覆写、super 可调用）。
Value NoOp(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// ---- API 面批量补桩（api_stubs.inc：android.jar 差集；返回值默认语义）----
Value ReturnZeroLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.l = 0;
  return r;
}

Value ReturnZeroFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.f = 0.0f;
  return r;
}

Value ReturnZeroDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.d = 0.0;
  return r;
}

Value CtxReturnSelf(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(self);
  return r;
}

// Context.createConfigurationContext(Configuration)：独立配置上下文（资源表 =
// 全局资源表副本 + 覆盖配置）。AOSP 返回新 ContextImpl；AppCompat
// ContextThemeWrapper.getResources() 会 createConfigurationContext(cfg)
// .getResources()——返回 self 会让其 getResources <-> b() 无限递归。
Value CtxCreateConfigurationContext(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VClass* cls = state.linker->FindClass("Landroid/content/VmConfigurationContext;");
  if (cls == nullptr) {
    state.ClearException();
    return r;
  }
  VObject* obj = state.linker->AllocObject(cls);
  if (obj == nullptr) return r;
  std::memset(obj->field_data(), 0, 8);
  VObject* res = nullptr;
  if (g_context_resources != nullptr && g_context_resources->klass != nullptr) {
    VClass* res_cls = g_context_resources->klass;
    res = state.linker->AllocObject(res_cls);
    if (res != nullptr) {
      uint32_t bytes = res_cls->object_size > sizeof(ObjHeader)
                           ? res_cls->object_size - sizeof(ObjHeader)
                           : 0;
      std::memcpy(res->field_data(), g_context_resources->field_data(), bytes);
      // Resources.mConfiguration @16 <- 入参 Configuration 副本（浅拷贝）
      if (n >= 1 && args[0].ref != nullptr && bytes >= 24) {
        VClass* cfg_cls = args[0].ref->klass;
        if (cfg_cls != nullptr) {
          VObject* cfg = state.linker->AllocObject(cfg_cls);
          if (cfg != nullptr) {
            uint32_t cbytes = cfg_cls->object_size > sizeof(ObjHeader)
                                  ? cfg_cls->object_size - sizeof(ObjHeader)
                                  : 0;
            std::memcpy(cfg->field_data(),
                        reinterpret_cast<VObject*>(args[0].ref)->field_data(), cbytes);
            ObjHeader* cref = reinterpret_cast<ObjHeader*>(cfg);
            std::memcpy(res->field_data() + 16, &cref, sizeof(cref));
          }
        }
      }
    }
  }
  ObjHeader* res_ref = reinterpret_cast<ObjHeader*>(res);
  std::memcpy(obj->field_data(), &res_ref, sizeof(res_ref));
  r.ref = obj;
  return r;
}

// VmConfigurationContext.getResources()：返回携带覆盖配置的资源表副本。
Value VmConfigCtxGetResources(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  if (self != nullptr) std::memcpy(&r.ref, self->field_data(), sizeof(r.ref));
  if (r.ref == nullptr) r.ref = reinterpret_cast<ObjHeader*>(g_context_resources);
  return r;
}

Value ReturnOneInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

Value ReturnZeroInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};  // 零 Value（int 0）
}

Value ThrowUnsupported(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  ThrowUnsupportedOperationException(state, "not supported in dexvm framework skeleton");
  *ok = false;
  return Value{};
}

Value ReturnNull(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// android.widget.Toast（宿主 Toast 桥：文案/时长入队，宿主 ToastBridge 用
// 真 Toast.makeText().show() 呈现）。
Value ToastMakeText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VClass* k = state.linker->FindClass("Landroid/widget/Toast;");
  if (k == nullptr) return r;
  VObject* toast = state.linker->AllocObject(k);
  if (toast == nullptr) return r;
  if (n > 1) {
    std::memcpy(toast->field_data() + 0, &args[1].ref, sizeof(args[1].ref));
  }
  if (n > 2) {
    std::memcpy(toast->field_data() + 8, &args[2].i, sizeof(args[2].i));
  }
  r.ref = toast;
  return r;
}

// makeText(Context, int resId, int)：int 参数不是引用，mText 置 null。
Value ToastMakeTextRes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  VClass* k = state.linker->FindClass("Landroid/widget/Toast;");
  if (k == nullptr) return r;
  VObject* toast = state.linker->AllocObject(k);
  if (toast == nullptr) return r;
  if (n > 2) {
    std::memcpy(toast->field_data() + 8, &args[2].i, sizeof(args[2].i));
  }
  r.ref = toast;
  return r;
}

// Toast.show()：文案/时长入宿主 Toast 桥队列（宿主 ToastBridge 用真
// Toast.makeText().show() 呈现；guest 侧 show/cancel 不再空转）。
Value ToastShow(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  HostToastRequest req;
  if (self != nullptr) {
    if (VField* f = FindInstanceField(self->klass, "mText")) {
      ObjHeader* text = GetRefField(self, f->offset);
      if (text != nullptr) req.text = static_cast<VString*>(text)->ToModifiedUtf8();
    }
    if (VField* f = FindInstanceField(self->klass, "mDuration")) {
      req.duration = GetIField(self, f->offset);
    }
  }
  PushHostToastRequest(std::move(req));
  return Value{};
}

// Toast.setText(CharSequence)：更新实例文案（show 时读取）。
Value ToastSetText(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  if (self != nullptr && n > 0) {
    if (VField* f = FindInstanceField(self->klass, "mText")) {
      SetRefField(self, f->offset, args[0].ref);
    }
  }
  return Value{};
}

// Toast.setDuration(int)：更新实例时长并返回 this（AOSP 链式语义）。
Value ToastSetDuration(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  r.ref = self;
  if (self != nullptr && n > 0) {
    if (VField* f = FindInstanceField(self->klass, "mDuration")) {
      SetIField(self, f->offset, args[0].i);
    }
  }
  return r;
}

Value ToastGetDuration(VMState& state, VObject* self, const Value*, size_t, bool* ok) {
  (void)state;
  *ok = true;
  Value r{};
  if (self != nullptr) {
    if (VField* f = FindInstanceField(self->klass, "mDuration")) {
      r.i = GetIField(self, f->offset);
    }
  }
  return r;
}

Value ReturnTrue(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  r.i = 1;
  return r;
}

// 虚分发一次（Intrinsic 内再入解释器：对照 CallJavaLoadClass 先例）。
bool CallVirtual(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
                 const char* ret, const Value* args, size_t n, Value* out) {
  if (obj == nullptr || obj->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "call virtual on null");
    return false;
  }
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", name);
    return false;
  }
  std::vector<Value> full;
  full.reserve(n + 1);
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!InvokeMethod(state, m, full.data(), full.size(), &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

// Context.startService/startForegroundService：同进程本地服务投影。
// AOSP 语义最小面：单例服务对象 + onCreate 一次 + 每次 start 派发
// onStartCommand；返回解析到的 ComponentName（未解析 = null）。
namespace {
std::map<std::string, VObject*>& LocalServiceRegistry() {
  static std::map<std::string, VObject*> services;
  return services;
}

Value StartLocalService(VMState& state, VObject* self, const Value* args, bool foreground,
                        bool* ok) {
  *ok = true;
  Value r{};
  if (args == nullptr || args[0].ref == nullptr || state.linker == nullptr) return r;
  VObject* intent = static_cast<VObject*>(args[0].ref);
  Value comp{};
  if (!CallVirtual(state, intent, "getComponent", {}, "Landroid/content/ComponentName;", nullptr,
                   0, &comp) ||
      comp.ref == nullptr) {
    return r;
  }
  Value cls_name{};
  if (!CallVirtual(state, static_cast<VObject*>(comp.ref), "getClassName", {},
                   "Ljava/lang/String;", nullptr, 0, &cls_name) ||
      cls_name.ref == nullptr) {
    return r;
  }
  std::string name = Mutf8Arg(cls_name);
  if (name.empty()) return r;
  std::string desc = "L";
  for (char c : name) desc.push_back(c == '.' ? '/' : c);
  desc.push_back(';');
  auto it = LocalServiceRegistry().find(desc);
  VObject* svc = it != LocalServiceRegistry().end() ? it->second : nullptr;
  if (svc == nullptr) {
    VClass* sc = state.linker->FindClass(desc);
    if (sc == nullptr) {
#if defined(__ANDROID__)
      __android_log_print(ANDROID_LOG_INFO, "DexVmService",
                          "startService class not found: %s fg=%d", desc.c_str(),
                          foreground ? 1 : 0);
#endif
      return r;
    }
    state.linker->InitializeClass(sc, &state);
    if (state.pending_exception != nullptr) return r;
    svc = state.linker->AllocObject(sc);
    if (svc == nullptr) return r;
    VMethod* ctor_m = nullptr;
    for (auto& m : sc->direct_methods) {
      if (m.name == "<init>" && m.param_types.empty()) {
        ctor_m = &m;
        break;
      }
    }
    if (ctor_m == nullptr) return r;
    Value ctor_args[1];
    ctor_args[0].ref = svc;
    Value ignored{};
    if (!InvokeMethod(state, ctor_m, ctor_args, 1, &ignored)) return r;
    for (VClass* k = sc; k != nullptr; k = k->super_class) {
      bool found = false;
      for (auto& f : k->instance_fields) {
        if (f.name == "mBase" && f.type_descriptor == "Landroid/content/Context;") {
          std::memcpy(svc->field_data() + f.offset, &self, sizeof(self));
          found = true;
          break;
        }
      }
      if (found) break;
    }
    Value life{};
    if (!CallVirtual(state, svc, "onCreate", {}, "V", nullptr, 0, &life)) return r;
    LocalServiceRegistry()[desc] = svc;
    if (state.heap != nullptr) state.heap->Pin(svc);  // 运行中服务根
  }
  Value sc_args[3];
  sc_args[0].ref = intent;
  sc_args[1].i = 0;  // flags
  static std::atomic<int> next_start_id{1};
  sc_args[2].i = next_start_id.fetch_add(1);
  Value sc_ret{};
  if (!CallVirtual(state, svc, "onStartCommand",
                   {"Landroid/content/Intent;", "I", "I"}, "I", sc_args, 3, &sc_ret)) {
    return r;
  }
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmService", "startService ok %s fg=%d",
                      desc.c_str(), foreground ? 1 : 0);
#endif
  r.ref = comp.ref;
  return r;
}
}  // namespace

Value CtxStartService(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  return StartLocalService(state, self, args, false, ok);
}

Value CtxStartForegroundService(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)n;
  return StartLocalService(state, self, args, true, ok);
}

// Context.bindService：同进程本地服务投影（AOSP bindService 语义最小面）。
// 步骤：Intent.component 解析类名 → 实例化 → mBase=Context → onCreate →
// onBind → ServiceConnection.onServiceConnected（AIDL 本地接口短路直调）。
// 返回 false = 组件/类不可解析/实例化失败（调用方按 AOSP 失败处理）。
Value CtxBindService(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  Value r{};
  r.i = 0;
  if (n < 2 || args[0].ref == nullptr || args[1].ref == nullptr || state.linker == nullptr) {
    return r;
  }
  VObject* intent = static_cast<VObject*>(args[0].ref);
  VObject* conn = static_cast<VObject*>(args[1].ref);
  Value comp{};
  if (!CallVirtual(state, intent, "getComponent", {}, "Landroid/content/ComponentName;", nullptr,
                   0, &comp) ||
      comp.ref == nullptr) {
    return r;
  }
  Value cls_name{};
  if (!CallVirtual(state, static_cast<VObject*>(comp.ref), "getClassName", {},
                   "Ljava/lang/String;", nullptr, 0, &cls_name) ||
      cls_name.ref == nullptr) {
    return r;
  }
  std::string name = Mutf8Arg(cls_name);
#if defined(__ANDROID__)
  // TEMP DIAG（bindService 本地投影归因；事后移除）。
  __android_log_print(ANDROID_LOG_INFO, "DexVmService", "bindService component=%s",
                      name.c_str());
#endif
  if (name.empty()) return r;
  std::string desc = "L";
  for (char c : name) desc.push_back(c == '.' ? '/' : c);
  desc.push_back(';');
  VClass* sc = state.linker->FindClass(desc);
  if (sc == nullptr) {
    // 不是本 APK 内可解析的服务（系统服务/其它进程）——本地投影不可达。
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DexVmService", "bindService class not found: %s",
                        desc.c_str());
#endif
    return r;
  }
  state.linker->InitializeClass(sc, &state);
  if (state.pending_exception != nullptr) return r;
  VObject* svc = state.linker->AllocObject(sc);
  if (svc == nullptr) return r;
  // 默认构造器（AOSP Service 子类必须有无参构造）。
  VMethod* ctor_m = nullptr;
  for (auto& m : sc->direct_methods) {
    if (m.name == "<init>" && m.param_types.empty()) {
      ctor_m = &m;
      break;
    }
  }
  if (ctor_m == nullptr) return r;
  Value ctor_args[1];
  ctor_args[0].ref = svc;
  Value ignored{};
  if (!InvokeMethod(state, ctor_m, ctor_args, 1, &ignored)) return r;
  // ContextWrapper.mBase = 绑定方 Context（Service.getApplicationContext 等
  // 继承面走 mBase；VM 内建 Context 方法亦以全局态兜底）。
  for (VClass* k = sc; k != nullptr; k = k->super_class) {
    bool found = false;
    for (auto& f : k->instance_fields) {
      if (f.name == "mBase" && f.type_descriptor == "Landroid/content/Context;") {
        std::memcpy(svc->field_data() + f.offset, &self, sizeof(self));
        found = true;
        break;
      }
    }
    if (found) break;
  }
  Value life{};
  if (!CallVirtual(state, svc, "onCreate", {}, "V", nullptr, 0, &life)) return r;
  Value intent_arg[1];
  intent_arg[0].ref = intent;
  Value binder{};
  if (!CallVirtual(state, svc, "onBind", {"Landroid/content/Intent;"},
                   "Landroid/os/IBinder;", intent_arg, 1, &binder)) {
    return r;
  }
  Value cb[2];
  cb[0].ref = comp.ref;
  cb[1].ref = binder.ref;
  Value cb_r{};
  if (!CallVirtual(state, conn, "onServiceConnected",
                   {"Landroid/content/ComponentName;", "Landroid/os/IBinder;"}, "V", cb, 2,
                   &cb_r)) {
    return r;
  }
  r.i = 1;
#if defined(__ANDROID__)
  __android_log_print(ANDROID_LOG_INFO, "DexVmService", "bindService ok %s binder=%p",
                      desc.c_str(), static_cast<void*>(binder.ref));
#endif
  return r;
}

// 数据根读写（framework_builtins.h 公开面；内部态在匿名域）
}  // namespace
void SetContextDataDir(const std::string& dir) { g_ctx_data_dir = dir; }
std::string GetContextDataDir() { return CtxDataRoot(); }
void SetContextExternalRoot(const std::string& root) { g_ctx_external_root = root; }
std::string GetContextExternalRoot() { return CtxExternalRoot(); }
void SetContextPackageManager(VObject* pm) { g_context_pm = pm; }
void SetContextApplicationInfo(VObject* app_info) { g_context_app_info = app_info; }
VObject* GetContextApplicationInfo() { return g_context_app_info; }

// Resources 面公开落点（实现态在匿名域）
void SetResourceSeam(const ResourceSeam& seam) { g_resource_seam = seam; }
const ResourceSeam* GetResourceSeam() { return &g_resource_seam; }
void SetContextResources(VObject* resources) { g_context_resources = resources; }
VObject* AllocResources(VMState& state, const std::string& apk_path,
                        const std::string& package_name) {
  return MakeResourcesObject(state, apk_path, package_name);
}
void RegisterProviderInfo(VMState& state, const std::string& authority, VObject* info) {
  if (authority.empty() || info == nullptr) return;
  g_provider_registry[authority] = info;
  if (state.heap != nullptr) state.heap->Pin(info);  // 注册表根（PMS 自身即根等价）
}

namespace {
// API 面批量补桩（android.jar 公开方法面差集；tools/api_stub_gen.py 生成）。
// 语义：缺失方法 = 可调用面（V=NoOp / 数值 0 / 引用 null）；已有实现（含继承
// 链）绝不覆盖；接口类不加桩（派发语义归实现类）。
// dex_classes_only=true：只补 lib 类（boot dex 装载后的第二遍；内建类已在
// RegisterFrameworkBuiltins 首遍补过，避免重复扫描/重复登记）。
void ApplyApiStubs(ClassLinker* linker, bool dex_classes_only) {
  auto add_api_stub = [&](const char* desc, const char* name, const char* params_pipe,
                          const char* ret, bool is_static) {
    VClass* klass = linker->FindClass(desc);
    if (klass == nullptr || klass->IsInterface()) return;
    if (dex_classes_only && klass->dex_file == nullptr) return;
    std::vector<std::string> params;
    if (params_pipe != nullptr && params_pipe[0] != 0) {
      std::string all(params_pipe);
      size_t start = 0;
      while (start <= all.size()) {
        size_t bar = all.find('|', start);
        params.push_back(all.substr(start, bar == std::string::npos ? std::string::npos
                                                                    : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
      }
    }
    VMethod probe;
    probe.name = name;
    probe.param_types = params;
    probe.return_type = ret;
    if (klass->FindInterfaceImpl(&probe) != nullptr) return;  // 已有（含继承）
    IntrinsicFn fn = ReturnNull;
    switch (ret[0]) {
      case 'V':
        fn = NoOp;
        break;
      case 'J':
        fn = ReturnZeroLong;
        break;
      case 'F':
        fn = ReturnZeroFloat;
        break;
      case 'D':
        fn = ReturnZeroDouble;
        break;
      case 'Z':
      case 'B':
      case 'S':
      case 'C':
      case 'I':
        fn = ReturnZeroInt;
        break;
      default:
        fn = ReturnNull;
        break;
    }
    AddIntrinsicMethod(klass, name, std::move(params), ret,
                       kAccPublic | (is_static ? kAccStatic : 0), fn);
  };
#define DEXVM_STUB(desc, name, params, ret, is_static) \
  add_api_stub(desc, name, params, ret, is_static);
#include "core/api_stubs.inc"
#undef DEXVM_STUB
}
}  // namespace

void ApplyFrameworkApiStubs(ClassLinker* linker) {
  ApplyApiStubs(linker, /*dex_classes_only=*/true);
}

void RegisterFrameworkBuiltins(ClassLinker* linker) {
  if (linker == nullptr) return;
  // 幂等：framework 内建已登记
  if (linker->FindClass("Landroid/app/ActivityThread;") != nullptr) return;

  // 新 VM 引导：消息循环 VM 侧状态复位（跨考试/测试运行不残留）
  g_loopers.clear();
  g_thread_looper.clear();
  g_main_thread = nullptr;
  g_pumping = false;
  g_uptime_anchor_us = 0;
  g_msg_pool = nullptr;
  g_msg_pool_size = 0;
  // ContentResolver 路由面同口径复位（provider 实例注册表 + resolver 单例）
  g_provider_instances.clear();
  g_content_resolver = nullptr;
  // PMS 注册表/资源表缓存同口径复位（根因修复：注册表值 = GC 钉住对象，
  // 跨 VM 残留 = 旧堆销毁后悬挂指针——PMGetProviderInfo/resolveContentProvider
  // 解引用 SEGV。新 VM 引导全量重建，不残留）
  g_provider_registry.clear();
  g_resources_by_apk.clear();
  ResetInflateState();  // 资源布局覆盖层登记（addContentView）复位
  ResetMediaState();    // AudioManager 单例复位（旧堆对象不得跨 VM 复用）
  ResetComponentCallbacks();  // Application.ComponentCallbacks 回调表复位
  ResetWindowCallbacks();     // Window.callback 侧表复位（旧堆对象不得跨 VM）
  ResetFragmentState();       // Fragment/FragmentManager 侧表复位
  // Context 服务桥单例（PM/ApplicationInfo/Resources）同口径复位：新 VM 由
  // semface/测试装配重新落点，避免悬挂旧堆指针
  SetContextResources(nullptr);
  SetContextPackageManager(nullptr);
  SetContextApplicationInfo(nullptr);
  // Thread.sleep 泵点接线（builtins MessagePumpHook -> 本 TU 消息泵）
  SetMessagePumpHook([](VMState& s, std::string* e) { return PumpLooperQueues(s, e); });

  std::vector<VClass*> all;
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  VClass* class_class = linker->FindClass("Ljava/lang/Class;");
  if (object == nullptr || class_class == nullptr) return;

  const uint32_t ctor = kAccPublic | kAccConstructor;
  const uint32_t pctor = kAccProtected | kAccConstructor;
  const uint32_t pub = kAccPublic;
  const uint32_t prot = kAccProtected;
  const uint32_t stat = kAccPublic | kAccStatic;
  const uint32_t pub_final = kAccPublic | kAccFinal;
  (void)pctor;
  (void)prot;
  (void)pub_final;

  // ---- android.content.Context（骨架；mPackageName/mApplicationContext） ----
  VClass* ctx = MakeClass(linker, &all, "Landroid/content/Context;", object, 16,
                          kAccPublic | kAccAbstract);
  if (ctx != nullptr) {
    AddInstanceField(ctx, "mPackageName", "Ljava/lang/String;", 0);
    AddInstanceField(ctx, "mApplicationContext", "Landroid/content/Context;", 8);
    AddIntrinsicMethod(ctx, "getPackageName", {}, "Ljava/lang/String;", pub, CtxGetPackageName);
    AddIntrinsicMethod(ctx, "getApplicationContext", {}, "Landroid/content/Context;", pub,
                       CtxGetApplicationContext);
    AddIntrinsicMethod(ctx, "getBaseContext", {}, "Landroid/content/Context;", pub,
                       CtxGetBaseContext);
    // 系统服务面：audio -> AudioManager 单例（AOSP ActivityThread 服务缓存同形）
    AddIntrinsicMethod(ctx, "getSystemService", {"Ljava/lang/String;"}, "Ljava/lang/Object;", pub,
                       CtxGetSystemService);
    AddIntrinsicMethod(ctx, "getSystemService", {"Ljava/lang/Class;"}, "Ljava/lang/Object;", pub,
                       CtxGetSystemServiceClass);
    // 主 Looper/执行器与权限面（AOSP Context 常用组；权限为已授权投影）
    AddIntrinsicMethod(ctx, "getMainLooper", {}, "Landroid/os/Looper;", pub, CtxGetMainLooper);
    AddIntrinsicMethod(ctx, "getMainExecutor", {}, "Ljava/util/concurrent/Executor;", pub,
                       ReturnNull);
    AddIntrinsicMethod(ctx, "checkSelfPermission", {"Ljava/lang/String;"}, "I", pub,
                       ReturnZeroInt);  // PERMISSION_GRANTED
    AddIntrinsicMethod(ctx, "checkCallingOrSelfPermission", {"Ljava/lang/String;"}, "I", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(ctx, "checkCallingPermission", {"Ljava/lang/String;"}, "I", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(ctx, "enforceCallingOrSelfPermission",
                       {"Ljava/lang/String;", "Ljava/lang/String;"}, "V", pub, NoOp);
    AddIntrinsicMethod(ctx, "enforcePermission",
                       {"Ljava/lang/String;", "I", "I", "Ljava/lang/String;"}, "V", pub, NoOp);
    AddIntrinsicMethod(ctx, "getObbDir", {}, "Ljava/io/File;", pub, CtxGetCacheDir);
    AddIntrinsicMethod(ctx, "getExternalMediaDirs", {}, "[Ljava/io/File;", pub,
                       CtxGetExternalMediaDirs);
    // String 资源带参形态（Context.getString(id, args...) = getResources().getString 委派）
    AddIntrinsicMethod(ctx, "getString", {"I", "[Ljava/lang/Object;"}, "Ljava/lang/String;", pub,
                       CtxGetStringArgs);
    AddIntrinsicMethod(ctx, "getText", {"I", "[Ljava/lang/CharSequence;"},
                       "Ljava/lang/CharSequence;", pub, CtxGetStringArgs);
    // 广播注册面（无广播派发源：注册成功语义 = 返回 null sticky Intent；
    // unregister 安全空转——壳/SDK 启动链消费）
    AddIntrinsicMethod(ctx, "registerReceiver",
                       {"Landroid/content/BroadcastReceiver;", "Landroid/content/IntentFilter;"},
                       "Landroid/content/Intent;", pub, ReturnNull);
    AddIntrinsicMethod(ctx, "registerReceiver",
                       {"Landroid/content/BroadcastReceiver;", "Landroid/content/IntentFilter;",
                        "I"},
                       "Landroid/content/Intent;", pub, ReturnNull);
    AddIntrinsicMethod(ctx, "registerReceiver",
                       {"Landroid/content/BroadcastReceiver;", "Landroid/content/IntentFilter;",
                        "Ljava/lang/String;", "Landroid/os/Handler;"},
                       "Landroid/content/Intent;", pub, ReturnNull);
    AddIntrinsicMethod(ctx, "registerReceiver",
                       {"Landroid/content/BroadcastReceiver;", "Landroid/content/IntentFilter;",
                        "Ljava/lang/String;", "Landroid/os/Handler;", "I"},
                       "Landroid/content/Intent;", pub, ReturnNull);
    AddIntrinsicMethod(ctx, "unregisterReceiver", {"Landroid/content/BroadcastReceiver;"}, "V",
                       pub, NoOp);
    // Service 启动面：VM 不承载系统 Service 管理（宿主 AMS 归桥接）；AOSP
    // startService 返回待启动 ComponentName，未解析到组件 = null（不伪造）。
    AddIntrinsicMethod(ctx, "startService", {"Landroid/content/Intent;"},
                       "Landroid/content/ComponentName;", pub, CtxStartService);
    AddIntrinsicMethod(ctx, "startForegroundService", {"Landroid/content/Intent;"},
                       "Landroid/content/ComponentName;", pub, CtxStartForegroundService);
    // Service 绑定面（同进程本地服务；FileDownloader 等 SDK 下载链依赖）：
    // 实例化 + attach 上下文 + onCreate/onBind + onServiceConnected 回调。
    // binder 缝走 lib Binder.queryLocalInterface 本地短路（AIDL Stub.asInterface
    // 同进程直调，不经 Parcel.transact）。
    AddIntrinsicMethod(ctx, "bindService",
                       {"Landroid/content/Intent;", "Landroid/content/ServiceConnection;", "I"},
                       "Z", pub, CtxBindService);
    AddIntrinsicMethod(ctx, "unbindService", {"Landroid/content/ServiceConnection;"}, "V", pub,
                       NoOp);
    AddIntrinsicMethod(ctx, "createPackageContext", {"Ljava/lang/String;", "I"},
                       "Landroid/content/Context;", pub, ThrowUnsupported);
    AddIntrinsicMethod(ctx, "getAssets", {}, "Landroid/content/res/AssetManager;", pub,
                       ResGetAssets);
    // Resources 面（docs 20 章）：app 资源表单例（SetContextResources 装配）
    AddIntrinsicMethod(ctx, "getResources", {}, "Landroid/content/res/Resources;", pub,
                       CtxGetResources);
    // 主题面（AOSP ContextThemeWrapper/Activity.getTheme；侧表懒建）
    AddIntrinsicMethod(ctx, "getTheme", {}, "Landroid/content/res/Resources$Theme;", pub,
                       CtxGetTheme);
    AddIntrinsicMethod(ctx, "setTheme", {"I"}, "V", pub, ActSetTheme);
    AddIntrinsicMethod(ctx, "getThemeResId", {}, "I", pub,
                       [](VMState& s, VObject*, const Value*, size_t, bool* ok) {
                         (void)s;
                         *ok = true;
                         Value r{};
                         r.i = GetAppThemeResId();
                         return r;
                       });
    // 资源值面（AOSP ContextImpl：getResources().get* 委派；arsc 值缝）
    AddIntrinsicMethod(ctx, "getString", {"I"}, "Ljava/lang/String;", pub, CtxGetString);
    AddIntrinsicMethod(ctx, "getText", {"I"}, "Ljava/lang/CharSequence;", pub, CtxGetText);
    AddIntrinsicMethod(ctx, "getStringArray", {"I"}, "[Ljava/lang/String;", pub, CtxGetStringArray);
    AddIntrinsicMethod(ctx, "getTextArray", {"I"}, "[Ljava/lang/CharSequence;", pub,
                       CtxGetTextArray);
    // SharedPreferences 面（§31.7 B 档 lib 缝：转调 lib SharedPreferencesImpl.getInstance(name)）
    AddIntrinsicMethod(ctx, "getSharedPreferences", {"Ljava/lang/String;", "I"},
                       "Landroid/content/SharedPreferences;", pub, CtxGetSharedPreferences);
    // 设备保护存储面（VM 单存储面等价语义）
    AddIntrinsicMethod(ctx, "createDeviceProtectedStorageContext", {},
                       "Landroid/content/Context;", pub, CtxDeviceProtectedContext);
    AddIntrinsicMethod(ctx, "isDeviceProtectedStorage", {}, "Z", pub, CtxIsDeviceProtected);
    AddIntrinsicMethod(ctx, "moveSharedPreferencesFrom",
                       {"Landroid/content/Context;", "Ljava/lang/String;"}, "Z", pub,
                       CtxMoveSharedPreferencesFrom);
    // 文件/存储面（AOSP ContextImpl 语义；数据根 = SetContextDataDir 落点）
    AddIntrinsicMethod(ctx, "getDataDir", {}, "Ljava/io/File;", pub, CtxGetDataDir);
    AddIntrinsicMethod(ctx, "getFilesDir", {}, "Ljava/io/File;", pub, CtxGetFilesDir);
    AddIntrinsicMethod(ctx, "openFileOutput", {"Ljava/lang/String;", "I"},
                       "Ljava/io/FileOutputStream;", pub, CtxOpenFileOutput);
    AddIntrinsicMethod(ctx, "openFileInput", {"Ljava/lang/String;"},
                       "Ljava/io/FileInputStream;", pub, CtxOpenFileInput);
    AddIntrinsicMethod(ctx, "getColor", {"I"}, "I", pub, CtxGetColor);
    // createConfigurationContext：返回独立配置上下文（资源表 = 全局资源表副本
    // + 覆盖配置）。AOSP 返回新 ContextImpl；AppCompat ContextThemeWrapper 的
    // getResources() 会 createConfigurationContext(cfg).getResources()——返回
    // self 会让其进入 getResources <-> b() 无限递归（实测 activity.attach
    // StackOverflowError）。VM 资源表单一，副本仅覆盖 Configuration 字段。
    AddIntrinsicMethod(ctx, "createConfigurationContext",
                       {"Landroid/content/res/Configuration;"},
                       "Landroid/content/Context;", pub, CtxCreateConfigurationContext);
    AddIntrinsicMethod(ctx, "createDisplayContext", {"Landroid/view/Display;"},
                       "Landroid/content/Context;", pub, CtxReturnSelf);
    // 独立配置上下文（createConfigurationContext 落点；资源表字段态）
    VClass* vm_config_ctx =
        MakeClass(linker, &all, "Landroid/content/VmConfigurationContext;", ctx, 8, pub);
    if (vm_config_ctx != nullptr) {
      AddInstanceField(vm_config_ctx, "mResources", "Landroid/content/res/Resources;", 0);
      AddIntrinsicMethod(vm_config_ctx, "getResources", {},
                         "Landroid/content/res/Resources;", pub, VmConfigCtxGetResources);
    }
    // android.content.VmPrefsStore：lib SharedPreferencesImpl 落盘钩子
    VClass* vm_prefs =
        MakeClass(linker, &all, "Landroid/content/VmPrefsStore;", object, 0, pub);
    if (vm_prefs != nullptr) {
      AddIntrinsicMethod(vm_prefs, "read", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                         VmPrefsRead);
      AddIntrinsicMethod(vm_prefs, "write", {"Ljava/lang/String;", "Ljava/lang/String;"}, "V",
                         stat, VmPrefsWrite);
    }
    // Context.getDrawable(int) = getResources().getDrawable(id)（AOSP ContextImpl）
    AddIntrinsicMethod(ctx, "getDrawable", {"I"}, "Landroid/graphics/drawable/Drawable;", pub,
                       [](VMState& s, VObject* self, const Value* a, size_t n, bool* ok) {
                         (void)self;
                         *ok = true;
                         Value r{};
                         VObject* res = g_context_resources;
                         if (res != nullptr) return ResGetDrawable(s, res, a, n, ok);
                         return r;
                       });
    // obtainStyledAttributes 三形态（主题值面后置；空值 TypedArray，getter 走默认）
    AddIntrinsicMethod(ctx, "obtainStyledAttributes",
                       {"Landroid/util/AttributeSet;", "[I"},
                       "Landroid/content/res/TypedArray;", pub, CtxObtainStyledAttributes);
    AddIntrinsicMethod(ctx, "obtainStyledAttributes",
                       {"Landroid/util/AttributeSet;", "[I", "I", "I"},
                       "Landroid/content/res/TypedArray;", pub, CtxObtainStyledAttributes);
    AddIntrinsicMethod(ctx, "obtainStyledAttributes", {"[I"},
                       "Landroid/content/res/TypedArray;", pub, CtxObtainStyledAttributes);
    AddIntrinsicMethod(ctx, "obtainStyledAttributes", {"I", "[I"},
                       "Landroid/content/res/TypedArray;", pub, CtxObtainStyledAttrsRes);
    AddIntrinsicMethod(ctx, "getCacheDir", {}, "Ljava/io/File;", pub, CtxGetCacheDir);
    // 外部存储面（AOSP ContextImpl.getExternal*Dirs；FileProvider paths 的
    // external-files-path/external-cache-path/external-media-path 消费面）
    AddIntrinsicMethod(ctx, "getExternalFilesDir", {"Ljava/lang/String;"}, "Ljava/io/File;", pub,
                       CtxGetExternalFilesDir);
    AddIntrinsicMethod(ctx, "getExternalFilesDirs", {"Ljava/lang/String;"},
                       "[Ljava/io/File;", pub, CtxGetExternalFilesDirs);
    AddIntrinsicMethod(ctx, "getExternalCacheDir", {}, "Ljava/io/File;", pub,
                       CtxGetExternalCacheDir);
    AddIntrinsicMethod(ctx, "getExternalCacheDirs", {}, "[Ljava/io/File;", pub,
                       CtxGetExternalCacheDirs);
    AddIntrinsicMethod(ctx, "getExternalMediaDirs", {}, "[Ljava/io/File;", pub,
                       CtxGetExternalMediaDirs);
    AddIntrinsicMethod(ctx, "getCodeCacheDir", {}, "Ljava/io/File;", pub, CtxGetCodeCacheDir);
    AddIntrinsicMethod(ctx, "getNoBackupFilesDir", {}, "Ljava/io/File;", pub,
                       CtxGetNoBackupFilesDir);
    AddIntrinsicMethod(ctx, "getDir", {"Ljava/lang/String;", "I"}, "Ljava/io/File;", pub,
                       CtxGetDir);
    AddIntrinsicMethod(ctx, "getDatabasePath", {"Ljava/lang/String;"}, "Ljava/io/File;", pub,
                       CtxGetDatabasePath);
    AddIntrinsicMethod(ctx, "getDatabaseNames", {}, "[Ljava/lang/String;", pub,
                       CtxGetDatabaseNames);
    AddIntrinsicMethod(ctx, "deleteDatabase", {"Ljava/lang/String;"}, "Z", pub,
                       CtxDeleteDatabase);
    AddIntrinsicMethod(ctx, "getSharedPreferencesPath", {"Ljava/lang/String;"},
                       "Ljava/io/File;", pub, CtxGetSharedPreferencesPath);
    AddIntrinsicMethod(ctx, "moveDatabaseFrom",
                       {"Landroid/content/Context;", "Ljava/lang/String;"}, "Z", pub,
                       CtxMoveDatabaseFrom);
    // 服务桥（系统服务单例；AOSP 同 PM 全 Context 可得）
    AddIntrinsicMethod(ctx, "getPackageManager", {}, "Landroid/content/pm/PackageManager;", pub,
                       CtxGetPackageManager);
    AddIntrinsicMethod(ctx, "getApplicationInfo", {}, "Landroid/content/pm/ApplicationInfo;", pub,
                       CtxGetApplicationInfo);
    // ContentResolver 面（§31.7 B 档：CRUD 路由钩子单例，AOSP ContextImpl.mResolver）
    AddIntrinsicMethod(ctx, "getContentResolver", {}, "Landroid/content/ContentResolver;", pub,
                       CtxGetContentResolver);
  }

  // ---- android.content.ContextWrapper（mBase；attachBaseContext 语义落点） ----
  VClass* ctx_wrapper = nullptr;
  if (ctx != nullptr) {
    ctx_wrapper = MakeClass(linker, &all, "Landroid/content/ContextWrapper;", ctx, 24, kAccPublic);
    AddInstanceField(ctx_wrapper, "mBase", "Landroid/content/Context;", 16);
    AddMethodSkeleton(ctx_wrapper, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(ctx_wrapper, "<init>", {"Landroid/content/Context;"}, "V", ctor, CWInitBase);
    AddIntrinsicMethod(ctx_wrapper, "attachBaseContext", {"Landroid/content/Context;"}, "V",
                       kAccProtected, CWAttachBaseContext);
  }

  VClass* ctx_theme = nullptr;
  if (ctx_wrapper != nullptr) {
    ctx_theme =
        MakeClass(linker, &all, "Landroid/content/ContextThemeWrapper;", ctx_wrapper, 32, kAccPublic);
    // ContextWrapper.mBase@16 之后（勿与 mBase 重叠：0x..90 指针被读成 int 事故）
    AddInstanceField(ctx_theme, "mThemeResId", "I", 24);
    AddMethodSkeleton(ctx_theme, "<init>", {}, "V", ctor);
    AddMethodSkeleton(ctx_theme, "<init>", {"Landroid/content/Context;"}, "V", ctor);
    // AOSP android.view.ContextThemeWrapper（平台侧同名类，AppCompat wrapContext
    // 构造面）：extends content.ContextThemeWrapper。(Context,int)/(Context,Theme)
    VClass* view_ctx_theme =
        MakeClass(linker, &all, "Landroid/view/ContextThemeWrapper;", ctx_theme, 32, kAccPublic);
    if (view_ctx_theme != nullptr) {
      AddIntrinsicMethod(view_ctx_theme, "<init>", {"Landroid/content/Context;"}, "V", ctor,
                         CWInitBase);
      AddIntrinsicMethod(view_ctx_theme, "<init>", {"Landroid/content/Context;", "I"}, "V",
                         ctor, ContextThemeWrapperInitRes);
      AddIntrinsicMethod(view_ctx_theme, "<init>",
                         {"Landroid/content/Context;", "Landroid/content/res/Resources$Theme;"},
                         "V", ctor, ContextThemeWrapperInitTheme);
      AddIntrinsicMethod(view_ctx_theme, "getTheme", {},
                         "Landroid/content/res/Resources$Theme;", pub, CtxGetTheme);
      AddIntrinsicMethod(view_ctx_theme, "setTheme", {"I"}, "V", pub, ContextThemeWrapperSetTheme);
      AddIntrinsicMethod(view_ctx_theme, "applyOverrideConfiguration",
                         {"Landroid/content/res/Configuration;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_ctx_theme, "getResources", {},
                         "Landroid/content/res/Resources;", pub, CtxGetResources);
      AddIntrinsicMethod(view_ctx_theme, "getAssets", {},
                         "Landroid/content/res/AssetManager;", pub, ResGetAssets);
    }
  }

  // ---- android.app.Application ----
  VClass* application = nullptr;
  if (ctx_wrapper != nullptr) {
    application = MakeClass(linker, &all, "Landroid/app/Application;", ctx_wrapper, 24, kAccPublic);
    AddMethodSkeleton(application, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(application, "onCreate", {}, "V", pub, NoOp);
    AddIntrinsicMethod(application, "onTerminate", {}, "V", pub, NoOp);
    AddIntrinsicMethod(application, "getProcessName", {}, "Ljava/lang/String;", stat,
                       AppGetProcessName);
    AddIntrinsicMethod(application, "registerActivityLifecycleCallbacks",
                       {"Landroid/app/Application$ActivityLifecycleCallbacks;"}, "V", pub, NoOp);
    AddIntrinsicMethod(application, "unregisterActivityLifecycleCallbacks",
                       {"Landroid/app/Application$ActivityLifecycleCallbacks;"}, "V", pub, NoOp);
    // ComponentCallbacks（AOSP Application.registerComponentCallbacks）：
    // 注册表保留（配置变更/低内存事件面后置；当前 VM 无这两类事件源），
    // 接口类形态齐全（壳/SDK 反射面）。
    VClass* component_callbacks = MakeClass(
        linker, &all, "Landroid/content/ComponentCallbacks;", object, 0,
        kAccPublic | kAccInterface | kAccAbstract);
    if (component_callbacks != nullptr) {
      AddMethodSkeleton(component_callbacks, "onConfigurationChanged",
                        {"Landroid/content/res/Configuration;"}, "V", pub | kAccAbstract);
      AddMethodSkeleton(component_callbacks, "onLowMemory", {}, "V", pub | kAccAbstract);
    }
    AddIntrinsicMethod(application, "registerComponentCallbacks",
                       {"Landroid/content/ComponentCallbacks;"}, "V", pub,
                       AppRegisterComponentCallbacks);
    AddIntrinsicMethod(application, "unregisterComponentCallbacks",
                       {"Landroid/content/ComponentCallbacks;"}, "V", pub,
                       AppUnregisterComponentCallbacks);
    // ActivityLifecycleCallbacks 回调面（19.3：pre/post 穿插，骨架接口）
    VClass* alc = MakeClass(linker, &all, "Landroid/app/Application$ActivityLifecycleCallbacks;",
                            object, 0, kAccPublic | kAccInterface | kAccAbstract);
    (void)alc;
  }

  // ---- android.app.Service（bindService 同进程本地服务基类） ----
  // AOSP Service 生命周期面：子类覆写；VM 的 bindService 走本地投影直调
  // onCreate/onBind（不经 AMS）。binder 缝由 lib Binder 本地接口短路承接。
  VClass* service = nullptr;
  if (ctx_wrapper != nullptr) {
    service = MakeClass(linker, &all, "Landroid/app/Service;", ctx_wrapper, 24,
                        kAccPublic | kAccAbstract);
    AddMethodSkeleton(service, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(service, "onCreate", {}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "onStartCommand", {"Landroid/content/Intent;", "I", "I"}, "I",
                       pub, ReturnZeroInt);
    AddIntrinsicMethod(service, "onBind", {"Landroid/content/Intent;"}, "Landroid/os/IBinder;",
                       pub, ReturnNull);
    AddIntrinsicMethod(service, "onUnbind", {"Landroid/content/Intent;"}, "Z", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(service, "onRebind", {"Landroid/content/Intent;"}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "onDestroy", {}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "onConfigurationChanged",
                       {"Landroid/content/res/Configuration;"}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "onLowMemory", {}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "onTrimMemory", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "onTaskRemoved", {"Landroid/content/Intent;"}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "getApplication", {}, "Landroid/app/Application;", pub,
                       ReturnNull);
    AddIntrinsicMethod(service, "stopSelf", {}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "stopSelf", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "stopSelfResult", {"I"}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(service, "startForeground", {"I", "Landroid/app/Notification;"}, "V",
                       pub, NoOp);
    AddIntrinsicMethod(service, "stopForeground", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(service, "isForeground", {}, "Z", pub, ReturnZeroInt);
  }
  (void)service;

  // ---- android.content.Intent：lib 归位（§31.7 B 档 POJO→lib——Intent 数据面
  // putExtra/getXxxExtra 族随 runtime.dex android/content/Intent.java 承接），
  // 不再内建 ----

  // ---- android.os.Bundle：lib 归位（§31.7 B 档 POJO→lib），不再内建 ----

  // ---- android.app.Instrumentation ----
  VClass* instrumentation =
      MakeClass(linker, &all, "Landroid/app/Instrumentation;", object, 0, kAccPublic);
  if (instrumentation != nullptr) {
    AddMethodSkeleton(instrumentation, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(instrumentation, "newApplication",
                       {"Ljava/lang/Class;", "Landroid/content/Context;"}, "Landroid/app/Application;",
                       stat, InstrNewApplication);
    AddIntrinsicMethod(instrumentation, "callApplicationOnCreate", {"Landroid/app/Application;"},
                       "V", pub, InstrCallApplicationOnCreate);
    AddIntrinsicMethod(instrumentation, "callActivityOnCreate",
                       {"Landroid/app/Activity;", "Landroid/os/Bundle;"}, "V", pub,
                       InstrCallActivityOnCreate);
    AddIntrinsicMethod(instrumentation, "callActivityOnResume", {"Landroid/app/Activity;"}, "V",
                       pub, NoOp);
  }

  // ---- android.app.ActivityThread（壳反射面：mInitialApplication/mAllApplications） ----
  VClass* activity_thread = nullptr;
  if (instrumentation != nullptr && application != nullptr) {
    activity_thread = MakeClass(linker, &all, "Landroid/app/ActivityThread;", object, 24, kAccPublic);
    AddInstanceField(activity_thread, "mInitialApplication", "Landroid/app/Application;", 0);
    AddInstanceField(activity_thread, "mAllApplications", "Ljava/util/ArrayList;", 8);
    AddInstanceField(activity_thread, "mInstrumentation", "Landroid/app/Instrumentation;", 16);
    AddStaticField(activity_thread, "sCurrentActivityThread", "Landroid/app/ActivityThread;");
    AddMethodSkeleton(activity_thread, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(activity_thread, "currentActivityThread", {}, "Landroid/app/ActivityThread;",
                       stat, ATCurrentActivityThread);
    AddIntrinsicMethod(activity_thread, "currentApplication", {}, "Landroid/app/Application;", stat,
                       ATCurrentApplication);
    AddIntrinsicMethod(activity_thread, "currentPackageName", {}, "Ljava/lang/String;", stat,
                       ATCurrentPackageName);
    AddIntrinsicMethod(activity_thread, "getApplicationThread", {},
                       "Landroid/app/ActivityThread;", pub, ReturnNull);
    (void)instrumentation;
  }

  // ---- android.app.Activity ----
  VClass* activity = nullptr;
  if (ctx_theme != nullptr && activity_thread != nullptr) {
    activity = MakeClass(linker, &all, "Landroid/app/Activity;", ctx_theme, 72, kAccPublic);
    AddInstanceField(activity, "mIntent", "Landroid/content/Intent;", 24);
    AddInstanceField(activity, "mActivityThread", "Landroid/app/ActivityThread;", 32);
    AddInstanceField(activity, "mInstrumentation", "Landroid/app/Instrumentation;", 40);
    AddInstanceField(activity, "mContentView", "Landroid/view/View;", 48);
    AddInstanceField(activity, "mWindow", "Landroid/view/Window;", 56);
    // 主题资源 id（继承 ContextThemeWrapper.mThemeResId@24 会与 mIntent 撞槽；
    // Activity 子布局追加在 64，ContextOwnThemeResId 近者优先命中）
    AddInstanceField(activity, "mThemeResId", "I", 64);
    AddMethodSkeleton(activity, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(activity, "attach",
                       {"Landroid/content/Context;", "Landroid/app/ActivityThread;",
                        "Landroid/app/Instrumentation;", "Landroid/content/Intent;"},
                       "V", prot, ActAttach);
    AddIntrinsicMethod(activity, "onCreate", {"Landroid/os/Bundle;"}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "onStart", {}, "V", prot, NoOp);
    // AOSP Activity.onPostCreate(savedInstanceState)：AppCompat/FragmentActivity
    // super 调用面（app 的一次性建立动作挂此：知乎 SetupTabsOnCreate → 首页
    // Tab 容器）。缺注册即 NoSuchMethodError，后续 onResume 链中断。
    AddIntrinsicMethod(activity, "onPostCreate", {"Landroid/os/Bundle;"}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "onResume", {}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "onPostResume", {}, "V", prot, NoOp);
    // Activity.getLayoutInflater（AOSP PhoneWindow.getLayoutInflater 等价面）：
    // api_stubs 的 null 桩会让 FragmentActivity host callback 的
    // LayoutInflater.cloneInContext 收到 null receiver（androidx Fragment
    // 创建链 NPE）。返回本 VM 平台 inflater（lib VmLayoutInflater 单例）。
    AddIntrinsicMethod(activity, "getLayoutInflater", {}, "Landroid/view/LayoutInflater;", pub,
                       [](VMState& st, VObject* self, const Value*, size_t, bool* ok) {
                         *ok = true;
                         Value r{};
                         VObject* inflater = AllocPlatformLayoutInflater(st, self);
                         if (inflater == nullptr) {
                           st.ThrowNew("Ljava/lang/OutOfMemoryError;",
                                       "Activity.getLayoutInflater");
                           *ok = false;
                           return r;
                         }
                         r.ref = inflater;
                         return r;
                       });
    // onPause/onStop：AppCompat/FragmentActivity super 调用面（Activity 为
    // framework builtin，lib Activity 被剔除；缺注册即 NoSuchMethodError，
    // 旧 Activity 退前台链中断）
    AddIntrinsicMethod(activity, "onPause", {}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "onStop", {}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "onRestart", {}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "onDestroy", {}, "V", prot, NoOp);
    AddIntrinsicMethod(activity, "setContentView", {"Landroid/view/View;"}, "V", pub,
                       ActSetContentView);
    // 资源布局面（docs 20 章）：AXML 膨胀 -> guest View 树（framework_inflate.cc）
    AddIntrinsicMethod(activity, "setContentView", {"I"}, "V", pub, ActSetContentViewInt);
    AddIntrinsicMethod(activity, "findViewById", {"I"}, "Landroid/view/View;", pub,
                       ActFindViewById);
    AddIntrinsicMethod(activity, "addContentView",
                       {"Landroid/view/View;", "Landroid/view/ViewGroup$LayoutParams;"}, "V", pub,
                       ActAddContentView);
    AddIntrinsicMethod(activity, "getIntent", {}, "Landroid/content/Intent;", pub, ActGetIntent);
    AddIntrinsicMethod(activity, "getWindow", {}, "Landroid/view/Window;", pub, ActGetWindow);
    AddIntrinsicMethod(activity, "getWindowManager", {}, "Landroid/view/WindowManager;", pub,
                       [](VMState& s, VObject*, const Value*, size_t, bool* ok) {
                         *ok = true;
                         Value r{};
                         bool good = true;
                         VObject* wm = NewLibObject(s, "Landroid/view/WindowManagerImpl;", {},
                                                    nullptr, 0, &good);
                         if (wm == nullptr) s.ClearException();
                         r.ref = reinterpret_cast<ObjHeader*>(wm);
                         return r;
                       });
    AddIntrinsicMethod(activity, "getApplication", {}, "Landroid/app/Application;", pub,
                       ActGetApplication);
    AddIntrinsicMethod(activity, "getTitle", {}, "Ljava/lang/CharSequence;", pub, ReturnNull);
    AddIntrinsicMethod(activity, "getComponentName", {}, "Landroid/content/ComponentName;", pub,
                       ActGetComponentName);
    AddIntrinsicMethod(activity, "getLocalClassName", {}, "Ljava/lang/String;", pub,
                       ActGetLocalClassName);
    AddIntrinsicMethod(activity, "getCallingActivity", {}, "Landroid/content/ComponentName;", pub,
                       ReturnNull);
    AddIntrinsicMethod(activity, "getFragmentManager", {}, "Landroid/app/FragmentManager;", pub,
                       FMGetFragmentManagerForActivity);
    AddIntrinsicMethod(activity, "getCallingPackage", {}, "Ljava/lang/String;", pub, ReturnNull);
    AddIntrinsicMethod(activity, "isTaskRoot", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(activity, "isDestroyed", {}, "Z", pub, ReturnZeroInt);
    // 任务/父级/方向面（单任务 VM：taskId 恒 1，AOSP 单 Activity task 形制）
    AddIntrinsicMethod(activity, "getTaskId", {}, "I", pub, ReturnOneInt);
    AddIntrinsicMethod(activity, "getParent", {}, "Landroid/app/Activity;", pub, ReturnNull);
    AddIntrinsicMethod(activity, "getParentActivityIntent", {}, "Landroid/content/Intent;", pub,
                       ReturnNull);
    AddIntrinsicMethod(activity, "getRequestedOrientation", {}, "I", pub,
                       [](VMState& st, VObject*, const Value*, size_t, bool* ok) {
                         (void)st;
                         *ok = true;
                         Value r{};
                         r.i = -1;  // AOSP SCREEN_ORIENTATION_UNSPECIFIED
                         return r;
                       });
    AddIntrinsicMethod(activity, "setRequestedOrientation", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "getChangingConfigurations", {}, "I", pub, ReturnZeroInt);
    // 组件启动/结束面（AOSP Activity->Instrumentation->AMS 链；VM 单 Activity
    // 生命周期模型下为可调用空转——多 Activity 栈后置）
    AddIntrinsicMethod(activity, "startActivity", {"Landroid/content/Intent;"}, "V", pub,
                       ActStartActivityImpl);
    AddIntrinsicMethod(activity, "startActivity", {"Landroid/content/Intent;",
                                                   "Landroid/os/Bundle;"}, "V", pub,
                       ActStartActivityImpl);
    AddIntrinsicMethod(activity, "startActivityForResult",
                       {"Landroid/content/Intent;", "I"}, "V", pub, ActStartActivityImpl);
    AddIntrinsicMethod(activity, "startActivityForResult",
                       {"Landroid/content/Intent;", "I", "Landroid/os/Bundle;"}, "V", pub,
                       ActStartActivityImpl);
    AddIntrinsicMethod(activity, "startActivityIfNeeded",
                       {"Landroid/content/Intent;", "I"}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(activity, "startActivityIfNeeded",
                       {"Landroid/content/Intent;", "I", "Landroid/os/Bundle;"}, "Z", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(activity, "finish", {}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "finishAffinity", {}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "finishAndRemoveTask", {}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "overridePendingTransition", {"I", "I"}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "shouldUpRecreateTask", {"Landroid/content/Intent;"}, "Z", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(activity, "navigateUpTo", {"Landroid/content/Intent;"}, "Z", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(activity, "setResult", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "setResult", {"I", "Landroid/content/Intent;"}, "V", pub, NoOp);
    AddIntrinsicMethod(activity, "isChangingConfigurations", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(activity, "isChild", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(activity, "moveTaskToBack", {"Z"}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(activity, "registerActivityLifecycleCallbacks",
                       {"Landroid/app/Application$ActivityLifecycleCallbacks;"}, "V", pub,
                       ActLifecycleCallbacks);
    AddIntrinsicMethod(activity, "unregisterActivityLifecycleCallbacks",
                       {"Landroid/app/Application$ActivityLifecycleCallbacks;"}, "V", pub,
                       ActLifecycleCallbacks);
    AddIntrinsicMethod(activity, "isFinishing", {}, "Z", pub, ReturnNull);  // 骨架：未 finish
    // 非配置实例保留面（AOSP：无重建 = null；onRetainNonConfigurationInstance 默认 null）
    AddIntrinsicMethod(activity, "getLastNonConfigurationInstance", {}, "Ljava/lang/Object;",
                       pub, ReturnNull);
    AddIntrinsicMethod(activity, "onRetainNonConfigurationInstance", {}, "Ljava/lang/Object;",
                       prot, ReturnNull);
    AddIntrinsicMethod(activity, "getLastCustomNonConfigurationInstance", {},
                       "Ljava/lang/Object;", pub, ReturnNull);
    // ---- Window.Callback（AOSP Activity implements Window.Callback；窗口回调默认面）----
    if (VClass* wcb = linker->FindClass("Landroid/view/Window$Callback;")) {
      activity->interfaces.push_back(wcb);
      const char* kEv = "Landroid/view/KeyEvent;";
      const char* kMe = "Landroid/view/MotionEvent;";
      const char* kMenu = "Landroid/view/Menu;";
      AddIntrinsicMethod(activity, "dispatchKeyEvent", {kEv}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "dispatchKeyShortcutEvent", {kEv}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "dispatchTouchEvent", {kMe}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "dispatchTrackballEvent", {kMe}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "dispatchGenericMotionEvent", {kMe}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "dispatchPopulateAccessibilityEvent",
                         {"Landroid/view/accessibility/AccessibilityEvent;"}, "Z", pub,
                         ReturnZeroInt);
      AddIntrinsicMethod(activity, "onCreatePanelView", {"I"}, "Landroid/view/View;", pub,
                         ReturnNull);
      AddIntrinsicMethod(activity, "onCreatePanelMenu", {"I", kMenu}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "onPreparePanel",
                         {"I", "Landroid/view/View;", kMenu}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "onMenuOpened", {"I", kMenu}, "Z", pub, ReturnTrue);
      AddIntrinsicMethod(activity, "onMenuItemSelected",
                         {"I", "Landroid/view/MenuItem;"}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "onWindowAttributesChanged",
                         {"Landroid/view/WindowManager$LayoutParams;"}, "V", pub, NoOp);
      AddIntrinsicMethod(activity, "onContentChanged", {}, "V", pub, NoOp);
      AddIntrinsicMethod(activity, "onWindowFocusChanged", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(activity, "onAttachedToWindow", {}, "V", pub, NoOp);
      AddIntrinsicMethod(activity, "onDetachedFromWindow", {}, "V", pub, NoOp);
      AddIntrinsicMethod(activity, "onPanelClosed", {"I", kMenu}, "V", pub, NoOp);
      AddIntrinsicMethod(activity, "onSearchRequested", {}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(activity, "onSearchRequested",
                         {"Landroid/view/accessibility/AccessibilityEvent;"}, "Z", pub,
                         ReturnZeroInt);
      AddIntrinsicMethod(activity, "onWindowStartingActionMode",
                         {"Landroid/view/ActionMode$Callback;"}, "Landroid/view/ActionMode;", pub,
                         ReturnNull);
      AddIntrinsicMethod(activity, "onWindowStartingActionMode",
                         {"Landroid/view/ActionMode$Callback;", "I"}, "Landroid/view/ActionMode;",
                         pub, ReturnNull);
      AddIntrinsicMethod(activity, "onActionModeStarted", {"Landroid/view/ActionMode;"}, "V", pub,
                         NoOp);
      AddIntrinsicMethod(activity, "onActionModeFinished", {"Landroid/view/ActionMode;"}, "V",
                         pub, NoOp);
    }
  }

  // ---- android.content.ContentProvider ----
  VClass* provider = MakeClass(linker, &all, "Landroid/content/ContentProvider;", object, 8,
                               kAccPublic | kAccAbstract);
  if (provider != nullptr) {
    AddInstanceField(provider, "mContext", "Landroid/content/Context;", 0);
    AddMethodSkeleton(provider, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(provider, "attachInfo",
                       {"Landroid/content/Context;", "Landroid/content/pm/ProviderInfo;"}, "V",
                       pub, CPAttachInfo);
    AddIntrinsicMethod(provider, "getContext", {}, "Landroid/content/Context;", pub,
                       CPGetContext);
    AddIntrinsicMethod(provider, "onCreate", {}, "Z", pub, ReturnTrue);
    // CRUD 抽象面（AOSP abstract；无覆写调用按 AbstractMethodError 语义落骨架）
    AddMethodSkeleton(provider, "query",
                      {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                       "[Ljava/lang/String;", "Ljava/lang/String;"},
                      "Landroid/database/Cursor;", pub);
    AddMethodSkeleton(provider, "getType", {"Landroid/net/Uri;"}, "Ljava/lang/String;", pub);
    AddMethodSkeleton(provider, "insert", {"Landroid/net/Uri;", "Landroid/content/ContentValues;"},
                      "Landroid/net/Uri;", pub);
    AddMethodSkeleton(provider, "delete", {"Landroid/net/Uri;", "Ljava/lang/String;",
                                           "[Ljava/lang/String;"},
                      "I", pub);
    AddMethodSkeleton(provider, "update",
                      {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Ljava/lang/String;",
                       "[Ljava/lang/String;"},
                      "I", pub);
    // overload 归一面（AOSP ContentProvider 具体委派形）：6 号 query（+信号）、
    // Bundle queryArgs、insert/update/delete extras 形、call 族——全部委派经典形，
    // 子类只覆写经典形即全形可达。
    AddIntrinsicMethod(provider, "query",
                       {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                        "[Ljava/lang/String;", "Ljava/lang/String;",
                        "Landroid/os/CancellationSignal;"},
                       "Landroid/database/Cursor;", pub, CPSelfQuerySignal);
    AddIntrinsicMethod(provider, "query",
                       {"Landroid/net/Uri;", "[Ljava/lang/String;", "Landroid/os/Bundle;",
                        "Landroid/os/CancellationSignal;"},
                       "Landroid/database/Cursor;", pub, CPSelfQueryBundle);
    AddIntrinsicMethod(provider, "insert",
                       {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Landroid/os/Bundle;"},
                       "Landroid/net/Uri;", pub, CPSelfInsertExtras);
    AddIntrinsicMethod(provider, "update",
                       {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Landroid/os/Bundle;"},
                       "I", pub, CPSelfUpdateExtras);
    AddIntrinsicMethod(provider, "delete", {"Landroid/net/Uri;", "Landroid/os/Bundle;"}, "I", pub,
                       CPSelfDeleteExtras);
    // AOSP ContentProvider.call：具体默认 null（可覆写；ContentResolver.call 路由面）
    AddIntrinsicMethod(provider, "call",
                       {"Ljava/lang/String;", "Ljava/lang/String;", "Landroid/os/Bundle;"},
                       "Landroid/os/Bundle;", pub, ReturnNull);
    AddIntrinsicMethod(provider, "call",
                       {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;",
                        "Landroid/os/Bundle;"},
                       "Landroid/os/Bundle;", pub, CPSelfCall4);
  }

  // ---- ContentProvider 签名类型面（ContentValues/Cursor/Uri/ProviderInfo） ----
  // ContentValues 已按 §31.7 POJO→lib 归位（runtime-libs android/content/
  // ContentValues.java 随 runtime.dex 打包；HashMap 数据载体），不再内建。
  //
  // Cursor（AOSP android.database 接口全形骨架：方法面按公开集登记，实现类
  // = dex（MatrixCursor 等 lib/dex 形），无覆写调用按 AbstractMethodError）。
  VClass* cursor = MakeClass(linker, &all, "Landroid/database/Cursor;", object, 0,
                             kAccPublic | kAccInterface | kAccAbstract);
  if (cursor != nullptr) {
    AddMethodSkeleton(cursor, "close", {}, "V", pub);
    AddMethodSkeleton(cursor, "getCount", {}, "I", pub);
    AddMethodSkeleton(cursor, "getPosition", {}, "I", pub);
    AddMethodSkeleton(cursor, "move", {"I"}, "Z", pub);
    AddMethodSkeleton(cursor, "moveToPosition", {"I"}, "Z", pub);
    AddMethodSkeleton(cursor, "moveToFirst", {}, "Z", pub);
    AddMethodSkeleton(cursor, "moveToLast", {}, "Z", pub);
    AddMethodSkeleton(cursor, "moveToNext", {}, "Z", pub);
    AddMethodSkeleton(cursor, "moveToPrevious", {}, "Z", pub);
    AddMethodSkeleton(cursor, "isFirst", {}, "Z", pub);
    AddMethodSkeleton(cursor, "isLast", {}, "Z", pub);
    AddMethodSkeleton(cursor, "isBeforeFirst", {}, "Z", pub);
    AddMethodSkeleton(cursor, "isAfterLast", {}, "Z", pub);
    AddMethodSkeleton(cursor, "getColumnIndex", {"Ljava/lang/String;"}, "I", pub);
    AddMethodSkeleton(cursor, "getColumnIndexOrThrow", {"Ljava/lang/String;"}, "I", pub);
    AddMethodSkeleton(cursor, "getColumnName", {"I"}, "Ljava/lang/String;", pub);
    AddMethodSkeleton(cursor, "getColumnNames", {}, "[Ljava/lang/String;", pub);
    AddMethodSkeleton(cursor, "getColumnCount", {}, "I", pub);
    AddMethodSkeleton(cursor, "getBlob", {"I"}, "[B", pub);
    AddMethodSkeleton(cursor, "getString", {"I"}, "Ljava/lang/String;", pub);
    AddMethodSkeleton(cursor, "copyStringToBuffer",
                      {"I", "Landroid/database/CharArrayBuffer;"}, "V", pub);
    AddMethodSkeleton(cursor, "getShort", {"I"}, "S", pub);
    AddMethodSkeleton(cursor, "getInt", {"I"}, "I", pub);
    AddMethodSkeleton(cursor, "getLong", {"I"}, "J", pub);
    AddMethodSkeleton(cursor, "getFloat", {"I"}, "F", pub);
    AddMethodSkeleton(cursor, "getDouble", {"I"}, "D", pub);
    AddMethodSkeleton(cursor, "getType", {"I"}, "I", pub);
    AddMethodSkeleton(cursor, "isNull", {"I"}, "Z", pub);
    AddMethodSkeleton(cursor, "deactivate", {}, "V", pub);
    AddMethodSkeleton(cursor, "requery", {}, "Z", pub);
    AddMethodSkeleton(cursor, "isClosed", {}, "Z", pub);
    AddMethodSkeleton(cursor, "registerContentObserver",
                      {"Landroid/database/ContentObserver;"}, "V", pub);
    AddMethodSkeleton(cursor, "unregisterContentObserver",
                      {"Landroid/database/ContentObserver;"}, "V", pub);
    AddMethodSkeleton(cursor, "registerDataSetObserver",
                      {"Landroid/database/DataSetObserver;"}, "V", pub);
    AddMethodSkeleton(cursor, "unregisterDataSetObserver",
                      {"Landroid/database/DataSetObserver;"}, "V", pub);
    AddMethodSkeleton(cursor, "setNotificationUri",
                      {"Landroid/content/ContentResolver;", "Landroid/net/Uri;"}, "V", pub);
    AddMethodSkeleton(cursor, "setNotificationUris",
                      {"Landroid/content/ContentResolver;", "Ljava/util/List;"}, "V", pub);
    AddMethodSkeleton(cursor, "getNotificationUri", {}, "Landroid/net/Uri;", pub);
    AddMethodSkeleton(cursor, "getNotificationUris", {}, "Ljava/util/List;", pub);
    AddMethodSkeleton(cursor, "getWantsAllOnMoveCalls", {}, "Z", pub);
    AddMethodSkeleton(cursor, "setExtras", {"Landroid/os/Bundle;"}, "V", pub);
    AddMethodSkeleton(cursor, "getExtras", {}, "Landroid/os/Bundle;", pub);
    AddMethodSkeleton(cursor, "respond", {"Landroid/os/Bundle;"}, "Landroid/os/Bundle;", pub);
  }
  // ProviderInfo 类链（AOSP android.content.pm 继承链原样）：
  //   PackageItemInfo <- ComponentInfo <- ProviderInfo。
  // 字段按 AOSP 归属分布（offset 全局布局不变，8 字节槽约定）：
  //   PackageItemInfo.name/packageName/metaData + ComponentInfo.processName/
  //   exported/enabled/applicationInfo + ProviderInfo.authority/initOrder/
  //   readPermission/writePermission/multiprocess/grantUriPermissions/
  //   pathPermissions。方法引用声明类（GDTFileProvider 等直指
  //   PackageItemInfo.loadXmlMetaData）经 ResolveMethod 超链 + FindInterfaceImpl
  //   超链双命中。值装配 = 生命周期驱动侧
  // framework/lifecycle_driver.cc AssembleProviderInfo，manifest 值投影源 =
  // session::ProviderEntry。
  VClass* package_item_info =
      MakeClass(linker, &all, "Landroid/content/pm/PackageItemInfo;", object, 64, kAccPublic);
  if (package_item_info != nullptr) {
    AddInstanceField(package_item_info, "name", "Ljava/lang/String;", 0);
    AddInstanceField(package_item_info, "packageName", "Ljava/lang/String;", 8);
    AddInstanceField(package_item_info, "metaData", "Landroid/os/Bundle;", 56);
    // AOSP PackageItemInfo.loadXmlMetaData：meta-data 资源 id -> pm.
    // getResourcesForApplication(this.applicationInfo).getXml(resId)（FileProvider
    // FILE_PROVIDER_PATHS 消费面；无 meta-data/id = null）。AOSP 声明类即本类。
    AddIntrinsicMethod(package_item_info, "loadXmlMetaData",
                       {"Landroid/content/pm/PackageManager;", "Ljava/lang/String;"},
                       "Landroid/content/res/XmlResourceParser;", pub, PILoadXmlMetaData);
  }
  VClass* component_info =
      MakeClass(linker, &all, "Landroid/content/pm/ComponentInfo;", package_item_info, 80,
                kAccPublic);
  if (component_info != nullptr) {
    AddInstanceField(component_info, "processName", "Ljava/lang/String;", 16);
    AddInstanceField(component_info, "applicationInfo", "Landroid/content/pm/ApplicationInfo;", 48);
    AddInstanceField(component_info, "exported", "Z", 64);
    AddInstanceField(component_info, "enabled", "Z", 72);
  }
  VClass* provider_info =
      MakeClass(linker, &all, "Landroid/content/pm/ProviderInfo;", component_info, 112,
                kAccPublic);
  if (provider_info != nullptr) {
    AddInstanceField(provider_info, "authority", "Ljava/lang/String;", 24);
    AddInstanceField(provider_info, "readPermission", "Ljava/lang/String;", 32);
    AddInstanceField(provider_info, "writePermission", "Ljava/lang/String;", 40);
    AddInstanceField(provider_info, "multiprocess", "Z", 80);
    AddInstanceField(provider_info, "initOrder", "I", 88);
    AddInstanceField(provider_info, "pathPermissions", "[Landroid/content/pm/PathPermission;", 96);
    AddInstanceField(provider_info, "grantUriPermissions", "Z", 104);
  }
  // pm 信息族续（ServiceInfo/ActivityInfo/ApplicationInfo/PackageInfo）按既有
  // 分工归 semface（jni/runtime_contract.cpp Complete：同 ApplicationInfo 形态
  // 值投影；framework_builtins 不重复登记防双 VClass 换布局红线）。

  // ---- android.content.BroadcastReceiver（壳/SDK 广播接收器基类）----
  VClass* broadcast_receiver = MakeClass(
      linker, &all, "Landroid/content/BroadcastReceiver;", object, 0, pub | kAccAbstract);
  if (broadcast_receiver != nullptr) {
    AddMethodSkeleton(broadcast_receiver, "<init>", {}, "V", ctor);
    AddMethodSkeleton(broadcast_receiver, "onReceive",
                      {"Landroid/content/Context;", "Landroid/content/Intent;"}, "V",
                      pub | kAccAbstract);
    // 结果/中止面（AOSP 默认实现；无广播派发源时安全空转）
    AddIntrinsicMethod(broadcast_receiver, "abortBroadcast", {}, "V", pub, NoOp);
    AddIntrinsicMethod(broadcast_receiver, "getAbortBroadcast", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(broadcast_receiver, "clearAbortBroadcast", {}, "V", pub, NoOp);
    AddIntrinsicMethod(broadcast_receiver, "getResultCode", {}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(broadcast_receiver, "setResultCode", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(broadcast_receiver, "getResultData", {}, "Ljava/lang/String;", pub,
                       ReturnNull);
    AddIntrinsicMethod(broadcast_receiver, "setResultData", {"Ljava/lang/String;"}, "V", pub,
                       NoOp);
  }

  // ---- android.content.res / pm.PackageManager（docs 20 章 Resources 面）----
  // Resources（每 APK 资源表；mApkPath/mPackageName = 资源表身份）。
  // getXml/openXmlResourceParser：resId -> AXML 事件流 -> XmlBlock$Parser。
  VClass* resources = MakeClass(linker, &all, "Landroid/content/res/Resources;", object, 32, pub);
  if (resources != nullptr) {
    AddInstanceField(resources, "mApkPath", "Ljava/lang/String;", 0);
    AddInstanceField(resources, "mPackageName", "Ljava/lang/String;", 8);
    AddInstanceField(resources, "mConfiguration", "Landroid/content/res/Configuration;", 16);
    AddIntrinsicMethod(resources, "getConfiguration", {}, "Landroid/content/res/Configuration;",
                       pub, ResGetConfiguration);
    AddMethodSkeleton(resources, "<init>", {}, "V", ctor);
    // android.content.res.Resources$Theme（内建内嵌类：主题面骨架）
    VClass* theme = MakeClass(linker, &all, "Landroid/content/res/Resources$Theme;", object, 16,
                              kAccPublic);
    if (theme != nullptr) {
      AddInstanceField(theme, "mResources", "Landroid/content/res/Resources;", 0);
      AddInstanceField(theme, "mThemeResId", "I", 8);
      AddIntrinsicMethod(theme, "<init>", {}, "V", ctor, NoOp);
      AddIntrinsicMethod(theme, "setTo", {"Landroid/content/res/Resources$Theme;"}, "V", pub,
                         ThemeSetTo);
      AddIntrinsicMethod(theme, "applyStyle", {"I", "Z"}, "V", pub, ThemeApplyStyle);
      AddIntrinsicMethod(theme, "obtainStyledAttributes", {"I", "[I"},
                         "Landroid/content/res/TypedArray;", pub, ThemeObtainStyled28);
      AddIntrinsicMethod(theme, "obtainStyledAttributes", {"I", "[I", "I", "I"},
                         "Landroid/content/res/TypedArray;", pub, ThemeObtainStyled28);
      AddIntrinsicMethod(theme, "obtainStyledAttributes",
                         {"Landroid/util/AttributeSet;", "[I"}, "Landroid/content/res/TypedArray;",
                         pub, ThemeObtainStyledSet);
      AddIntrinsicMethod(theme, "obtainStyledAttributes",
                         {"Landroid/util/AttributeSet;", "[I", "I", "I"},
                         "Landroid/content/res/TypedArray;", pub, ThemeObtainStyledSet);
      AddIntrinsicMethod(theme, "obtainStyledAttributes", {"[I"},
                         "Landroid/content/res/TypedArray;", pub, ThemeObtainStyledSelf);
      AddIntrinsicMethod(theme, "resolveAttribute", {"I", "Landroid/util/TypedValue;", "Z"}, "Z",
                         pub, ThemeResolveAttribute);
      AddIntrinsicMethod(theme, "getResources", {}, "Landroid/content/res/Resources;", pub,
                         ResGetResourcesSelf);
      AddIntrinsicMethod(theme, "rebase", {}, "V", pub, NoOp);
    }
    AddIntrinsicMethod(resources,
                       "getIdentifier",
                       {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;"},
                       "I", pub, ResGetIdentifier);
    AddIntrinsicMethod(resources, "getXml", {"I"}, "Landroid/content/res/XmlResourceParser;", pub,
                       ResOpenXmlParser);
    // Resources.getLayout(int)：AOSP 返回布局资源的 XmlResourceParser（与
    // getXml 同面；app 直接取布局解析器的调用点必需）。
    AddIntrinsicMethod(resources, "getLayout", {"I"},
                       "Landroid/content/res/XmlResourceParser;", pub, ResOpenXmlParser);
    AddIntrinsicMethod(resources, "openXmlResourceParser", {"I"},
                       "Landroid/content/res/XmlResourceParser;", pub, ResOpenXmlParser);
    // 值面（docs 20 章：arsc string/array 值缝）
    AddIntrinsicMethod(resources, "getString", {"I"}, "Ljava/lang/String;", pub, ResGetString);
    AddIntrinsicMethod(resources, "getText", {"I"}, "Ljava/lang/CharSequence;", pub, ResGetString);
    AddIntrinsicMethod(resources, "getResourcePackageName", {"I"}, "Ljava/lang/String;", pub,
                       ResGetResourcePackageName);
    AddIntrinsicMethod(resources, "getColor", {"I"}, "I", pub, ResGetColor);
    AddIntrinsicMethod(resources, "newTheme", {}, "Landroid/content/res/Resources$Theme;", pub,
                       ResNewTheme);
    // ---- Resources 值面批（常用 getter 组；真实 arsc 值 + 缺省回落）----
    AddIntrinsicMethod(resources, "getDisplayMetrics", {}, "Landroid/util/DisplayMetrics;", pub,
                       ResGetDisplayMetrics);
    AddIntrinsicMethod(resources, "getDimensionPixelSize", {"I"}, "I", pub, ResGetDimensionPx);
    AddIntrinsicMethod(resources, "getDimensionPixelOffset", {"I"}, "I", pub, ResGetDimensionPx);
    AddIntrinsicMethod(resources, "getDimension", {"I"}, "F", pub, ResGetDimension);
    AddIntrinsicMethod(resources, "getFraction", {"I", "I", "I"}, "F", pub, ResGetDimension);
    AddIntrinsicMethod(resources, "getColorStateList", {"I"},
                       "Landroid/content/res/ColorStateList;", pub, ResGetColorStateList);
    AddIntrinsicMethod(resources, "getColorStateList",
                       {"I", "Landroid/content/res/Resources$Theme;"},
                       "Landroid/content/res/ColorStateList;", pub, ResGetColorStateList);
    AddIntrinsicMethod(resources, "getDrawable",
                       {"I", "Landroid/content/res/Resources$Theme;"},
                       "Landroid/graphics/drawable/Drawable;", pub, ResGetDrawable);
    AddIntrinsicMethod(resources, "getDrawableForDensity",
                       {"I", "I", "Landroid/content/res/Resources$Theme;"},
                       "Landroid/graphics/drawable/Drawable;", pub, ResGetDrawable);
    AddIntrinsicMethod(resources, "getDrawableForDensity", {"I", "I"},
                       "Landroid/graphics/drawable/Drawable;", pub, ResGetDrawable);
    AddIntrinsicMethod(resources, "getColor",
                       {"I", "Landroid/content/res/Resources$Theme;"}, "I", pub, ResGetColor);
    AddIntrinsicMethod(resources, "getValue",
                       {"Ljava/lang/String;", "Landroid/util/TypedValue;", "Z"}, "V", pub,
                       ResGetValue);
    AddIntrinsicMethod(resources, "getString", {"I", "[Ljava/lang/Object;"},
                       "Ljava/lang/String;", pub, ResGetStringArgs);
    AddIntrinsicMethod(resources, "getText", {"I", "[Ljava/lang/CharSequence;"},
                       "Ljava/lang/CharSequence;", pub, ResGetStringArgs);
    AddIntrinsicMethod(resources, "getQuantityString", {"I", "I"}, "Ljava/lang/String;", pub,
                       ResGetQuantityString);
    AddIntrinsicMethod(resources, "getQuantityString",
                       {"I", "I", "[Ljava/lang/Object;"}, "Ljava/lang/String;", pub,
                       ResGetQuantityString);
    AddIntrinsicMethod(resources, "getQuantityText", {"I", "I"},
                       "Ljava/lang/CharSequence;", pub, ResGetQuantityString);
    AddIntrinsicMethod(resources, "getBoolean", {"I"}, "Z", pub, ResGetBoolean);
    AddIntrinsicMethod(resources, "getInteger", {"I"}, "I", pub, ResGetInteger);
    AddIntrinsicMethod(resources, "getInteger", {"I", "I"}, "I", pub, ResGetInteger);
    AddIntrinsicMethod(resources, "openRawResource", {"I"}, "Ljava/io/InputStream;", pub,
                       ResOpenRawResource);
    AddIntrinsicMethod(resources, "getResourceName", {"I"}, "Ljava/lang/String;", pub,
                       ResResourceName);
    AddIntrinsicMethod(resources, "getResourceEntryName", {"I"}, "Ljava/lang/String;", pub,
                       ResResourceEntryName);
    AddIntrinsicMethod(resources, "getResourceTypeName", {"I"}, "Ljava/lang/String;", pub,
                       ResResourceTypeName);
    AddIntrinsicMethod(resources, "getAssets", {}, "Landroid/content/res/AssetManager;", pub,
                       ResGetAssets);
    AddIntrinsicMethod(resources, "getSystem", {}, "Landroid/content/res/Resources;",
                       pub | kAccStatic, ResGetSystem);
    AddIntrinsicMethod(resources, "getValue",
                       {"I", "Landroid/util/TypedValue;", "Z"}, "V", pub, ResGetValue);
    AddIntrinsicMethod(resources, "getDrawable", {"I"}, "Landroid/graphics/drawable/Drawable;",
                       pub, ResGetDrawable);
    AddIntrinsicMethod(resources, "getValue", {"Ljava/lang/String;", "Landroid/util/TypedValue;", "Z"},
                       "V", pub, ResGetValue);
    AddIntrinsicMethod(resources, "getStringArray", {"I"}, "[Ljava/lang/String;", pub,
                       ResGetStringArray);
    AddIntrinsicMethod(resources, "getTextArray", {"I"}, "[Ljava/lang/CharSequence;", pub,
                       ResGetStringArray);
  }
  // Resources.NotFoundException（AOSP：extends RuntimeException）
  MakeClass(linker, &all, "Landroid/content/res/Resources$NotFoundException;",
            linker->FindClass("Ljava/lang/RuntimeException;"), 16, pub);
  // org.xmlpull.v1.XmlPullParser（事件常量 + 最小方法面；XmlResourceParser 超接口。
  // 常量 = AOSP XmlPullParser 同值：START_DOCUMENT=0 END_DOCUMENT=1 START_TAG=2
  // END_TAG=3 TEXT=4）
  VClass* pull = MakeClass(linker, &all, "Lorg/xmlpull/v1/XmlPullParser;", object, 0,
                           pub | kAccInterface | kAccAbstract);
  if (pull != nullptr) {
    AddStaticField(pull, "START_DOCUMENT", "I");
    AddStaticField(pull, "END_DOCUMENT", "I");
    AddStaticField(pull, "START_TAG", "I");
    AddStaticField(pull, "END_TAG", "I");
    AddStaticField(pull, "TEXT", "I");
    pull->static_storage[0].i = 0;
    pull->static_storage[1].i = 1;
    pull->static_storage[2].i = 2;
    pull->static_storage[3].i = 3;
    pull->static_storage[4].i = 4;
    AddMethodSkeleton(pull, "next", {}, "I", pub | kAccAbstract);
    AddMethodSkeleton(pull, "getName", {}, "Ljava/lang/String;", pub | kAccAbstract);
    AddMethodSkeleton(pull, "getAttributeValue", {"Ljava/lang/String;", "Ljava/lang/String;"},
                      "Ljava/lang/String;", pub | kAccAbstract);
  }
  // XmlResourceParser（AOSP：extends XmlPullParser + AttributeSet + AutoCloseable；
  // 最小面 = 标签/属性遍历 + close；AttributeSet 全面后置）
  VClass* xml_parser = MakeClass(linker, &all, "Landroid/content/res/XmlResourceParser;",
                                 pull != nullptr ? pull : object, 0,
                                 pub | kAccInterface | kAccAbstract);
  if (xml_parser != nullptr) {
    AddMethodSkeleton(xml_parser, "next", {}, "I", pub | kAccAbstract);
    AddMethodSkeleton(xml_parser, "getName", {}, "Ljava/lang/String;", pub | kAccAbstract);
    AddMethodSkeleton(xml_parser, "getAttributeValue",
                      {"Ljava/lang/String;", "Ljava/lang/String;"}, "Ljava/lang/String;",
                      pub | kAccAbstract);
    AddMethodSkeleton(xml_parser, "close", {}, "V", pub | kAccAbstract);
  }
  // XmlBlock$Parser（AXML 事件流游标；状态=对象字段，GC 安全）
  VClass* xml_block_parser =
      MakeClass(linker, &all, "Landroid/content/res/XmlBlock$Parser;", object, 48, pub);
  if (xml_block_parser != nullptr) {
    AddInstanceField(xml_block_parser, "mTypes", "[I", 0);
    AddInstanceField(xml_block_parser, "mNames", "[Ljava/lang/String;", 8);
    AddInstanceField(xml_block_parser, "mAttrKeys", "[Ljava/lang/String;", 16);
    AddInstanceField(xml_block_parser, "mAttrVals", "[Ljava/lang/String;", 24);
    AddInstanceField(xml_block_parser, "mSpan", "[I", 32);
    AddInstanceField(xml_block_parser, "mPos", "I", 40);
    AddMethodSkeleton(xml_block_parser, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(xml_block_parser, "next", {}, "I", pub, XmlNext);
    AddIntrinsicMethod(xml_block_parser, "getName", {}, "Ljava/lang/String;", pub, XmlGetName);
    AddIntrinsicMethod(xml_block_parser, "getAttributeValue",
                       {"Ljava/lang/String;", "Ljava/lang/String;"}, "Ljava/lang/String;", pub,
                       XmlGetAttributeValue);
    AddIntrinsicMethod(xml_block_parser, "close", {}, "V", pub, NoOp);
    if (xml_parser != nullptr) xml_block_parser->interfaces.push_back(xml_parser);
  }
  // android.content.pm.PackageManager（AOSP 系统服务桥类；设备/宿主装配单例）。
  // resolveContentProvider/getResourcesForApplication = manifest/资源表查询面
  // （RegisterProviderInfo/AllocResources 落点）；getPackageInfo/getApplicationInfo
  // 等身份面由 semface/宿主测试按需补（方法骨架不预置防重复登记）。
  VClass* pm_cls = MakeClass(linker, &all, "Landroid/content/pm/PackageManager;", object, 0, pub);
  if (pm_cls != nullptr) {
    AddMethodSkeleton(pm_cls, "<init>", {}, "V", ctor);
    AddIntrinsicMethod(pm_cls, "resolveContentProvider", {"Ljava/lang/String;", "I"},
                       "Landroid/content/pm/ProviderInfo;", pub, PMResolveContentProvider);
    AddIntrinsicMethod(pm_cls, "getProviderInfo", {"Landroid/content/ComponentName;", "I"},
                       "Landroid/content/pm/ProviderInfo;", pub, PMGetProviderInfo);
    AddIntrinsicMethod(pm_cls, "getResourcesForApplication",
                       {"Landroid/content/pm/ApplicationInfo;"},
                       "Landroid/content/res/Resources;", pub, PMGetResourcesForApplication);
    // 组件启用态面（AOSP PMS；VM 无 PackageManager 写入面——默认态/空转）
    AddIntrinsicMethod(pm_cls, "getComponentEnabledSetting",
                       {"Landroid/content/ComponentName;"}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(pm_cls, "setComponentEnabledSetting",
                       {"Landroid/content/ComponentName;", "I", "I"}, "V", pub, NoOp);
    AddIntrinsicMethod(pm_cls, "getApplicationEnabledSetting", {"Ljava/lang/String;"}, "I", pub,
                       ReturnZeroInt);
    AddIntrinsicMethod(pm_cls, "setApplicationEnabledSetting",
                       {"Ljava/lang/String;", "I", "I"}, "V", pub, NoOp);
  }
  // ActivityInfo（AOSP 类链 PackageItemInfo <- ComponentInfo <- ActivityInfo；
  // PM.getActivityInfo 返回面——ActivityInfo.name/packageName/applicationInfo/
  // metaData/theme/launchMode/configChanges 等字段在父链上）
  VClass* activity_info =
      MakeClass(linker, &all, "Landroid/content/pm/ActivityInfo;", component_info, 104,
                kAccPublic);
  if (activity_info != nullptr) {
    AddInstanceField(activity_info, "theme", "I", 80);
    AddInstanceField(activity_info, "launchMode", "I", 88);
    AddInstanceField(activity_info, "configChanges", "I", 92);
    // androidx NavUtils.getParentActivityName 直接读 parentActivityName 字段；
    // 缺字段会让 Activity.onCreate 抛 NoSuchFieldError（LauncherActivity 实锤）。
    AddInstanceField(activity_info, "parentActivityName", "Ljava/lang/String;", 96);
  }

  // com.dexvm.runtime.Host：宿主桥接值面（telephony.* 等系统服务真值快照；
  // 宿主 Java 经 nativeSetHostValues 推入）。lib 同名源仅为 Java 可见入口。
  VClass* host_bridge =
      MakeClass(linker, &all, "Lcom/dexvm/runtime/Host;", object, 0, pub | kAccFinal);
  if (host_bridge != nullptr) {
    AddIntrinsicMethod(host_bridge, "get", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                       HostGet);
  }

  // android.view.VmLayoutHost：lib VmLayoutInflater 的平台钩子（资源布局引擎
  // core/framework_inflate.cc 入口；类先于 lib 定义，静态面 intrinsic）。
  VClass* vm_layout_host =
      MakeClass(linker, &all, "Landroid/view/VmLayoutHost;", object, 0, pub);
  if (vm_layout_host != nullptr) {
    AddIntrinsicMethod(vm_layout_host, "inflate",
                       {"Landroid/view/LayoutInflater;", "I", "Landroid/view/ViewGroup;", "Z"},
                       "Landroid/view/View;", stat, VmLayoutHostInflate);
    // Dialog 窗口面：内容树登记为 VM 窗口根（快照叠加导出；Dialog 对象不出 guest）。
    AddIntrinsicMethod(
        vm_layout_host, "showDialog",
        {"Ljava/lang/Object;", "Landroid/view/View;", "I", "I", "I", "F", "Z"}, "V", stat,
        VmDialogShow);
    AddIntrinsicMethod(vm_layout_host, "dismissDialog", {"Ljava/lang/Object;"}, "V", stat,
                       VmDialogDismiss);
    // 窗口类弹层：PopupMenu（宿主真弹层菜单）/ PopupWindow（叠加浮层卡片）。
    AddIntrinsicMethod(vm_layout_host, "showPopupMenu",
                       {"Ljava/lang/Object;", "Landroid/view/View;", "[Ljava/lang/String;"},
                       "V", stat, VmPopupShowMenu);
    AddIntrinsicMethod(vm_layout_host, "dismissPopupMenu", {"Ljava/lang/Object;"}, "V", stat,
                       VmPopupDismissMenu);
    AddIntrinsicMethod(vm_layout_host, "showPopupWindow",
                       {"Ljava/lang/Object;", "Landroid/view/View;", "I", "I", "I", "I"}, "V",
                       stat, VmPopupShowWindow);
    AddIntrinsicMethod(vm_layout_host, "dismissPopupWindow", {"Ljava/lang/Object;"}, "V", stat,
                       VmPopupDismissWindow);
  }

  // android.content.res.VmAssets：lib AssetManager.open 的资产读取钩子
  // （assets/<name> -> APK zip 原始字节；查无返回 null）。
  VClass* vm_assets =
      MakeClass(linker, &all, "Landroid/content/res/VmAssets;", object, 0, pub);
  if (vm_assets != nullptr) {
    AddIntrinsicMethod(vm_assets, "open", {"Ljava/lang/String;"}, "[B", stat, VmAssetsOpen);
  }

  // java.security.VmDigest：lib sun.security.provider 摘要 SPI 的原生桥
  // （启动期 JarVerifier 全 APK 摘要校验热点；AOSP 口径 MessageDigest 由
  // native Conscrypt/BoringSSL 承接，这里由 libdexvm C++ 摘要实现承接）。
  VClass* vm_digest = MakeClass(linker, &all, "Ljava/security/VmDigest;", object, 0, pub);
  if (vm_digest != nullptr) {
    AddIntrinsicMethod(vm_digest, "create", {"Ljava/lang/String;"}, "J", stat, VmDigestCreate);
    AddIntrinsicMethod(vm_digest, "update", {"J", "[B", "I", "I"}, "V", stat, VmDigestUpdate);
    AddIntrinsicMethod(vm_digest, "digest", {"J"}, "[B", stat, VmDigestDigest);
    AddIntrinsicMethod(vm_digest, "reset", {"J"}, "V", stat, VmDigestReset);
    AddIntrinsicMethod(vm_digest, "close", {"J"}, "V", stat, VmDigestClose);
  }

  // java.util.VmLinkedList：lib LinkedList 节点遍历原生桥（启动期 app 模块
  // 追踪按 index 反复取节点，解释器下是 O(n²) 调用开销热点）。
  // android.graphics.VmFonts：lib Typeface/FontFamily/Paint 的宿主字体桥
  // （真 Typeface 创建 + 文本度量；AOSP：字体解析与 shaping 在平台 native）。
  VClass* vm_fonts = MakeClass(linker, &all, "Landroid/graphics/VmFonts;", object, 0, pub);
  if (vm_fonts != nullptr) {
    AddIntrinsicMethod(vm_fonts, "available", {}, "Z", stat, VmFontsAvailable);
    AddIntrinsicMethod(vm_fonts, "createFromFamily", {"Ljava/lang/String;", "I"}, "J", stat,
                       VmFontsCreateFromFamily);
    AddIntrinsicMethod(vm_fonts, "createFromFile", {"Ljava/lang/String;", "I"}, "J", stat,
                       VmFontsCreateFromFile);
    AddIntrinsicMethod(vm_fonts, "restyle", {"J", "I"}, "J", stat, VmFontsRestyle);
    AddIntrinsicMethod(vm_fonts, "createFromResource", {"I", "I"}, "J", stat,
                       VmFontsCreateFromResource);
    AddIntrinsicMethod(vm_fonts, "measureText", {"J", "Ljava/lang/String;", "F"}, "F", stat,
                       VmFontsMeasureText);
    AddIntrinsicMethod(vm_fonts, "fontMetrics", {"J", "F", "[I"}, "Z", stat,
                       VmFontsFontMetrics);
  }

  VClass* vm_linked_list =
      MakeClass(linker, &all, "Ljava/util/VmLinkedList;", object, 0, pub);
  if (vm_linked_list != nullptr) {
    AddIntrinsicMethod(vm_linked_list, "get",
                       {"Ljava/util/LinkedList;", "I"}, "Ljava/lang/Object;", stat,
                       VmLinkedListGet);
    AddIntrinsicMethod(vm_linked_list, "node",
                       {"Ljava/util/LinkedList;", "I"}, "Ljava/lang/Object;", stat,
                       VmLinkedListNode);
  }

  // android.hardware.VmSensors：lib SensorManager 的宿主传感器桥钩子
  // （list() 描述表、pollEvent([F[J)I 事件轮询；宿主 SensorBridge.java 推送）。
  VClass* vm_sensors =
      MakeClass(linker, &all, "Landroid/hardware/VmSensors;", object, 0, pub);
  if (vm_sensors != nullptr) {
    AddIntrinsicMethod(vm_sensors, "list", {}, "Ljava/lang/String;", stat, VmSensorsList);
    AddIntrinsicMethod(vm_sensors, "pollEvent", {"[F", "[J"}, "I", stat, VmSensorsPoll);
  }

  // android.app.VmNotifications：lib NotificationManager 的宿主通知桥钩子
  // （请求入队列 -> 宿主 NotificationBridge.java 真通知下发）。
  VClass* vm_notifications =
      MakeClass(linker, &all, "Landroid/app/VmNotifications;", object, 0, pub);
  if (vm_notifications != nullptr) {
    AddIntrinsicMethod(vm_notifications, "post",
                       {"I", "Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;",
                        "Ljava/lang/String;", "I", "I"},
                       "V", stat, VmNotificationsPost);
    AddIntrinsicMethod(vm_notifications, "cancel", {"I", "Ljava/lang/String;"}, "V", stat,
                       VmNotificationsCancel);
    AddIntrinsicMethod(vm_notifications, "cancelAll", {}, "V", stat, VmNotificationsCancelAll);
    AddIntrinsicMethod(vm_notifications, "createChannel",
                       {"Ljava/lang/String;", "Ljava/lang/String;", "I"}, "V", stat,
                       VmNotificationsCreateChannel);
    AddIntrinsicMethod(vm_notifications, "deleteChannel", {"Ljava/lang/String;"}, "V", stat,
                       VmNotificationsDeleteChannel);
  }
  // PackageManager.NameNotFoundException（AOSP：getProviderInfo 无主抛面；
  // extends AndroidException——VM 侧归 Exception 族）
  MakeClass(linker, &all, "Landroid/content/pm/PackageManager$NameNotFoundException;",
            linker->FindClass("Ljava/lang/Exception;"), 16, pub);
  // android.net.Uri / Uri$Builder 已归位 lib（android/net/Uri.java：纯字符串
  // 解析/构造逻辑，无 VM 布局/native 需求——§31.7 C 档）。

  // ---- android.content.ContentResolver（§31.7 B 档：CRUD 路由钩子；
  //      authority -> provider 实例分发，AOSP ContentService 语义面）----
  VClass* cr_cls = MakeClass(linker, &all, "Landroid/content/ContentResolver;", object, 0,
                             kAccPublic | kAccAbstract);
  if (cr_cls != nullptr) {
    AddMethodSkeleton(cr_cls, "<init>", {"Landroid/content/Context;"}, "V", ctor);
    // CRUD 路由（query/insert/update/delete/getType -> provider 实例方法）
    AddIntrinsicMethod(cr_cls, "query",
                       {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                        "[Ljava/lang/String;", "Ljava/lang/String;"},
                       "Landroid/database/Cursor;", pub, CRQuery);
    AddIntrinsicMethod(cr_cls, "query",
                       {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                        "[Ljava/lang/String;", "Ljava/lang/String;",
                        "Landroid/os/CancellationSignal;"},
                       "Landroid/database/Cursor;", pub, CRQuerySignal);
    AddIntrinsicMethod(cr_cls, "query",
                       {"Landroid/net/Uri;", "[Ljava/lang/String;", "Landroid/os/Bundle;",
                        "Landroid/os/CancellationSignal;"},
                       "Landroid/database/Cursor;", pub, CRQueryBundle);
    AddIntrinsicMethod(cr_cls, "insert", {"Landroid/net/Uri;", "Landroid/content/ContentValues;"},
                       "Landroid/net/Uri;", pub, CRInsert);
    AddIntrinsicMethod(cr_cls, "insert",
                       {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Landroid/os/Bundle;"},
                       "Landroid/net/Uri;", pub, CRInsertExtras);
    AddIntrinsicMethod(cr_cls, "update",
                       {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Ljava/lang/String;",
                        "[Ljava/lang/String;"},
                       "I", pub, CRUpdate);
    AddIntrinsicMethod(cr_cls, "update",
                       {"Landroid/net/Uri;", "Landroid/content/ContentValues;", "Landroid/os/Bundle;"},
                       "I", pub, CRUpdateExtras);
    AddIntrinsicMethod(cr_cls, "delete", {"Landroid/net/Uri;", "Ljava/lang/String;",
                                          "[Ljava/lang/String;"},
                       "I", pub, CRDelete);
    AddIntrinsicMethod(cr_cls, "delete", {"Landroid/net/Uri;", "Landroid/os/Bundle;"}, "I", pub,
                       CRDeleteExtras);
    AddIntrinsicMethod(cr_cls, "getType", {"Landroid/net/Uri;"}, "Ljava/lang/String;", pub,
                       CRGetType);
    AddIntrinsicMethod(cr_cls, "bulkInsert", {"Landroid/net/Uri;", "[Landroid/content/ContentValues;"},
                       "I", pub, CRBulkInsert);
    AddIntrinsicMethod(cr_cls, "call",
                       {"Landroid/net/Uri;", "Ljava/lang/String;", "Ljava/lang/String;",
                        "Landroid/os/Bundle;"},
                       "Landroid/os/Bundle;", pub, CRCallUri);
    AddIntrinsicMethod(cr_cls, "call",
                       {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;",
                        "Landroid/os/Bundle;"},
                       "Landroid/os/Bundle;", pub, CRCallAuth);
    // 观察者/通知面：无变更泵语义 = 登记空操作（AOSP 无监听者时的同效口径）
    AddIntrinsicMethod(cr_cls, "notifyChange",
                       {"Landroid/net/Uri;", "Landroid/database/ContentObserver;"}, "V", pub, NoOp);
    AddIntrinsicMethod(cr_cls, "notifyChange",
                       {"Landroid/net/Uri;", "Landroid/database/ContentObserver;", "Z"}, "V", pub,
                       NoOp);
    AddIntrinsicMethod(cr_cls, "notifyChange",
                       {"Landroid/net/Uri;", "Landroid/database/ContentObserver;", "I"}, "V", pub,
                       NoOp);
    AddIntrinsicMethod(cr_cls, "notifyChange",
                       {"Ljava/util/Collection;", "Landroid/database/ContentObserver;", "I"}, "V",
                       pub, NoOp);
    AddIntrinsicMethod(cr_cls, "registerContentObserver",
                       {"Landroid/net/Uri;", "Z", "Landroid/database/ContentObserver;"}, "V", pub,
                       NoOp);
    AddIntrinsicMethod(cr_cls, "unregisterContentObserver",
                       {"Landroid/database/ContentObserver;"}, "V", pub, NoOp);
    AddIntrinsicMethod(cr_cls, "getStreamTypes", {"Landroid/net/Uri;", "Ljava/lang/String;"},
                       "[Ljava/lang/String;", pub, ReturnNull);
    // Client 包装面后置（AOSP acquire*Client 无主返回 null 同值口径）
    AddIntrinsicMethod(cr_cls, "acquireContentProviderClient", {"Landroid/net/Uri;"},
                       "Landroid/content/ContentProviderClient;", pub, ReturnNull);
    AddIntrinsicMethod(cr_cls, "acquireUnstableContentProviderClient", {"Landroid/net/Uri;"},
                       "Landroid/content/ContentProviderClient;", pub, ReturnNull);
    // fd/流面 + 事务面：方法骨架（解析可达；调用 = AbstractMethodError 显式缺口）
    AddMethodSkeleton(cr_cls, "openInputStream", {"Landroid/net/Uri;"}, "Ljava/io/InputStream;",
                      pub);
    AddMethodSkeleton(cr_cls, "openOutputStream", {"Landroid/net/Uri;"}, "Ljava/io/OutputStream;",
                      pub);
    AddMethodSkeleton(cr_cls, "openOutputStream", {"Landroid/net/Uri;", "Ljava/lang/String;"},
                      "Ljava/io/OutputStream;", pub);
    AddMethodSkeleton(cr_cls, "openFileDescriptor", {"Landroid/net/Uri;", "Ljava/lang/String;"},
                      "Landroid/os/ParcelFileDescriptor;", pub);
    AddMethodSkeleton(cr_cls, "openFileDescriptor",
                      {"Landroid/net/Uri;", "Ljava/lang/String;", "Landroid/os/CancellationSignal;"},
                      "Landroid/os/ParcelFileDescriptor;", pub);
    AddMethodSkeleton(cr_cls, "openAssetFileDescriptor", {"Landroid/net/Uri;", "Ljava/lang/String;"},
                      "Landroid/content/res/AssetFileDescriptor;", pub);
    AddMethodSkeleton(cr_cls, "openTypedAssetFileDescriptor",
                      {"Landroid/net/Uri;", "Ljava/lang/String;", "Landroid/os/Bundle;"},
                      "Landroid/content/res/AssetFileDescriptor;", pub);
    AddMethodSkeleton(cr_cls, "openTypedAssetFile",
                      {"Landroid/net/Uri;", "Ljava/lang/String;", "Landroid/os/Bundle;",
                       "Landroid/os/CancellationSignal;"},
                      "Landroid/content/res/AssetFileDescriptor;", pub);
    AddMethodSkeleton(cr_cls, "applyBatch", {"Ljava/lang/String;", "Ljava/util/ArrayList;"},
                      "[Landroid/content/ContentProviderResult;", pub);
    AddMethodSkeleton(cr_cls, "getPersistedUriPermissions", {}, "Ljava/util/List;", pub);
    AddMethodSkeleton(cr_cls, "getOutgoingPersistedUriPermissions", {}, "Ljava/util/List;", pub);
    AddMethodSkeleton(cr_cls, "loadThumbnail",
                      {"Landroid/net/Uri;", "Landroid/util/Size;", "Landroid/os/CancellationSignal;"},
                      "Landroid/graphics/Bitmap;", pub);
  }

  // ---- android.view.View / ViewGroup / TextView / LinearLayout ----
  // 布局态字段（M4.2 渲染管线落点，framework/render.cc 消费）：
  // mLeft/mTop/mRight/mBottom、mMeasuredWidth/mMeasuredHeight、mBackgroundColor。
  // 长尾面字段（批量补充轮）：mVisibility/mLayoutParams/mParent/mTag/
  // mAlpha/mTranslationX/mTranslationY/mScrollX/mScrollY（104..168）。
  VClass* view = MakeClass(linker, &all, "Landroid/view/View;", object, 176, kAccPublic);
  if (view != nullptr) {
    AddInstanceField(view, "mContext", "Landroid/content/Context;", 0);
    AddInstanceField(view, "mId", "I", 8);
    AddInstanceField(view, "mPaddingLeft", "I", 16);
    AddInstanceField(view, "mPaddingTop", "I", 24);
    AddInstanceField(view, "mPaddingRight", "I", 32);
    AddInstanceField(view, "mPaddingBottom", "I", 40);
    AddInstanceField(view, "mLeft", "I", 48);
    AddInstanceField(view, "mTop", "I", 56);
    AddInstanceField(view, "mRight", "I", 64);
    AddInstanceField(view, "mBottom", "I", 72);
    AddInstanceField(view, "mMeasuredWidth", "I", 80);
    AddInstanceField(view, "mMeasuredHeight", "I", 88);
    AddInstanceField(view, "mBackgroundColor", "I", 96);
    AddInstanceField(view, "mVisibility", "I", 104);
    AddInstanceField(view, "mLayoutParams", "Landroid/view/ViewGroup$LayoutParams;", 112);
    AddInstanceField(view, "mParent", "Landroid/view/ViewParent;", 120);
    AddInstanceField(view, "mTag", "Ljava/lang/Object;", 128);
    AddInstanceField(view, "mAlpha", "F", 136);
    AddInstanceField(view, "mTranslationX", "F", 144);
    AddInstanceField(view, "mTranslationY", "F", 152);
    AddInstanceField(view, "mScrollX", "I", 160);
    AddInstanceField(view, "mScrollY", "I", 168);
    AddStaticField(view, "VISIBLE", "I");
    AddStaticField(view, "INVISIBLE", "I");
    AddStaticField(view, "GONE", "I");
    AddStaticField(view, "NO_ID", "I");
    view->static_storage[0].i = 0;
    view->static_storage[1].i = 4;
    view->static_storage[2].i = 8;
    view->static_storage[3].i = -1;
    // AOSP View 状态集静态字段：ColorStateList/StateListDrawable 的 state 数组
    // （TabLayout 构造期 sget View.SELECTED_STATE_SET/EMPTY_STATE_SET；Material
    // 组件 <clinit> 同族引用）。attr id 取 android.R.attr（16842908...）。
    {
      VClass* int_arr_class = linker->FindArrayClass(linker->PrimitiveClass('I'));
      auto add_state_set = [&](const char* name, const std::vector<int32_t>& ids) {
        AddStaticField(view, name, "[I");
        if (int_arr_class == nullptr) return;
        VArray* arr = linker->AllocArray(int_arr_class, static_cast<int32_t>(ids.size()));
        if (arr == nullptr) return;
        for (size_t i = 0; i < ids.size(); i++) {
          uint64_t slot = static_cast<uint32_t>(ids[i]);
          std::memcpy(arr->data() + i * 8, &slot, sizeof(slot));
        }
        view->static_storage.back().ref = reinterpret_cast<ObjHeader*>(arr);
      };
      const int32_t kFocused = 16842908;
      const int32_t kWindowFocused = 16842909;
      const int32_t kEnabled = 16842910;
      const int32_t kSelected = 16842913;
      const int32_t kPressed = 16842919;
      const int32_t kActivated = 16843518;
      const int32_t kHovered = 16843623;
      const int32_t kDragAccepted = 16843624;
      add_state_set("EMPTY_STATE_SET", {});
      add_state_set("WINDOW_FOCUSED_STATE_SET", {kWindowFocused});
      add_state_set("SELECTED_STATE_SET", {kSelected});
      add_state_set("SELECTED_WINDOW_FOCUSED_STATE_SET", {kWindowFocused, kSelected});
      add_state_set("FOCUSED_STATE_SET", {kFocused});
      add_state_set("FOCUSED_WINDOW_FOCUSED_STATE_SET", {kWindowFocused, kFocused});
      add_state_set("FOCUSED_SELECTED_STATE_SET", {kFocused, kSelected});
      add_state_set("FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kFocused, kSelected});
      add_state_set("ENABLED_STATE_SET", {kEnabled});
      add_state_set("ENABLED_FOCUSED_STATE_SET", {kEnabled, kFocused});
      add_state_set("ENABLED_SELECTED_STATE_SET", {kEnabled, kSelected});
      add_state_set("ENABLED_FOCUSED_SELECTED_STATE_SET", {kEnabled, kFocused, kSelected});
      add_state_set("ENABLED_WINDOW_FOCUSED_STATE_SET", {kWindowFocused, kEnabled});
      add_state_set("ENABLED_FOCUSED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kEnabled, kFocused});
      add_state_set("ENABLED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kEnabled, kSelected});
      add_state_set("ENABLED_FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kEnabled, kFocused, kSelected});
      add_state_set("PRESSED_STATE_SET", {kPressed});
      add_state_set("PRESSED_WINDOW_FOCUSED_STATE_SET", {kWindowFocused, kPressed});
      add_state_set("PRESSED_SELECTED_STATE_SET", {kPressed, kSelected});
      add_state_set("PRESSED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kSelected});
      add_state_set("PRESSED_FOCUSED_STATE_SET", {kPressed, kFocused});
      add_state_set("PRESSED_FOCUSED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kFocused});
      add_state_set("PRESSED_FOCUSED_SELECTED_STATE_SET", {kPressed, kFocused, kSelected});
      add_state_set("PRESSED_FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kFocused, kSelected});
      add_state_set("PRESSED_ENABLED_STATE_SET", {kPressed, kEnabled});
      add_state_set("PRESSED_ENABLED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kEnabled});
      add_state_set("PRESSED_ENABLED_SELECTED_STATE_SET", {kPressed, kEnabled, kSelected});
      add_state_set("PRESSED_ENABLED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kEnabled, kSelected});
      add_state_set("PRESSED_ENABLED_FOCUSED_STATE_SET", {kPressed, kEnabled, kFocused});
      add_state_set("PRESSED_ENABLED_FOCUSED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kEnabled, kFocused});
      add_state_set("PRESSED_ENABLED_FOCUSED_SELECTED_STATE_SET",
                    {kPressed, kEnabled, kFocused, kSelected});
      add_state_set("PRESSED_ENABLED_FOCUSED_SELECTED_WINDOW_FOCUSED_STATE_SET",
                    {kWindowFocused, kPressed, kEnabled, kFocused, kSelected});
      add_state_set("ACTIVATED_STATE_SET", {kActivated});
      add_state_set("ACTIVATED_SELECTED_STATE_SET", {kActivated, kSelected});
      add_state_set("HOVERED_STATE_SET", {kHovered});
      add_state_set("HOVERED_FOCUSED_STATE_SET", {kHovered, kFocused});
      add_state_set("HOVERED_ENABLED_STATE_SET", {kHovered, kEnabled});
      add_state_set("HOVERED_FOCUSED_ENABLED_STATE_SET", {kHovered, kFocused, kEnabled});
      add_state_set("SELECTED_HOVERED_STATE_SET", {kSelected, kHovered});
      add_state_set("SELECTED_HOVERED_FOCUSED_STATE_SET", {kSelected, kHovered, kFocused});
      add_state_set("SELECTED_PRESSED_STATE_SET", {kSelected, kPressed});
      add_state_set("SELECTED_FOCUSED_STATE_SET", {kSelected, kFocused});
      add_state_set("DRAG_ACCEPTED_STATE_SET", {kDragAccepted});
    }
    AddIntrinsicMethod(view, "<init>", {"Landroid/content/Context;"}, "V", ctor, ViewInit);
    // 属性集构造面（guest 子类 super(context, attrs[, defStyleAttr]) 链；属性值
    // 由 inflate 属性映射落字段，构造期 attrs 不消费）
    AddIntrinsicMethod(view, "<init>",
                       {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, "V", ctor,
                       ViewInit);
    AddIntrinsicMethod(view, "<init>",
                       {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}, "V",
                       ctor, ViewInit);
    AddIntrinsicMethod(view, "<init>",
                       {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I", "I"},
                       "V", ctor, ViewInit);
    // 系统 UI 可见性面（AOSP 常量 0x2000 = LIGHT_STATUS_BAR；窗口系统归宿主）
    AddStaticField(view, "SYSTEM_UI_FLAG_LIGHT_STATUS_BAR", "I");
    if (!view->static_fields.empty()) {
      view->static_storage[view->static_fields.size() - 1].i = 0x2000;
    }
    // 布局/文本方向常量（lib Gravity 等静态初始化依赖）
    struct StaticInt {
      const char* name;
      int32_t value;
    };
    const StaticInt kViewStatics[] = {
        {"LAYOUT_DIRECTION_LTR", 0},     {"LAYOUT_DIRECTION_RTL", 1},
        {"LAYOUT_DIRECTION_INHERIT", 2}, {"LAYOUT_DIRECTION_LOCALE", 3},
        {"TEXT_DIRECTION_INHERIT", 0},   {"TEXT_DIRECTION_FIRST_STRONG", 1},
        {"TEXT_DIRECTION_ANY_RTL", 2},   {"TEXT_DIRECTION_LTR", 3},
        {"TEXT_DIRECTION_RTL", 4},       {"TEXT_DIRECTION_LOCALE", 5},
        {"TEXT_ALIGNMENT_INHERIT", 0},   {"TEXT_ALIGNMENT_GRAVITY", 1},
        {"TEXT_ALIGNMENT_TEXT_START", 2}, {"TEXT_ALIGNMENT_TEXT_END", 3},
        {"TEXT_ALIGNMENT_CENTER", 4},    {"TEXT_ALIGNMENT_VIEW_START", 5},
        {"TEXT_ALIGNMENT_VIEW_END", 6},  {"SCROLL_AXIS_NONE", 0},
        {"SCROLL_AXIS_HORIZONTAL", 1},   {"SCROLL_AXIS_VERTICAL", 2},
        {"FOCUS_BACKWARD", 0x1},         {"FOCUS_FORWARD", 0x2},
        {"FOCUS_LEFT", 0x11},            {"FOCUS_RIGHT", 0x42},
        {"FOCUS_UP", 0x21},              {"FOCUS_DOWN", 0x82},
        {"IMPORTANT_FOR_ACCESSIBILITY_AUTO", 0},
        {"IMPORTANT_FOR_ACCESSIBILITY_YES", 1},
        {"IMPORTANT_FOR_ACCESSIBILITY_NO", 2},
        {"IMPORTANT_FOR_ACCESSIBILITY_NO_HIDE_DESCENDANTS", 4},
        {"OVER_SCROLL_ALWAYS", 0},       {"OVER_SCROLL_IF_CONTENT_SCROLLS", 1},
        {"OVER_SCROLL_NEVER", 2},        {"DRAWING_CACHE_QUALITY_AUTO", 0},
        {"DRAWING_CACHE_QUALITY_LOW", 1}, {"DRAWING_CACHE_QUALITY_HIGH", 2},
        {"KEEP_SCREEN_ON", 26},
    };
    for (const StaticInt& si : kViewStatics) {
      AddStaticField(view, si.name, "I");
      view->static_storage[view->static_fields.size() - 1].i = si.value;
    }
    AddIntrinsicMethod(view, "setSystemUiVisibility", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "getSystemUiVisibility", {}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "setPadding", {"I", "I", "I", "I"}, "V", pub, ViewSetPadding);
    AddIntrinsicMethod(view, "getPaddingLeft", {}, "I", pub, VGGetPaddingLeft);
    AddIntrinsicMethod(view, "getPaddingTop", {}, "I", pub, VGGetPaddingTop);
    AddIntrinsicMethod(view, "getPaddingRight", {}, "I", pub, VGGetPaddingRight);
    AddIntrinsicMethod(view, "getPaddingBottom", {}, "I", pub, VGGetPaddingBottom);
    AddIntrinsicMethod(view, "getPaddingStart", {}, "I", pub, VGGetPaddingLeft);
    AddIntrinsicMethod(view, "getPaddingEnd", {}, "I", pub, VGGetPaddingRight);
    AddIntrinsicMethod(view, "setPaddingRelative", {"I", "I", "I", "I"}, "V", pub,
                       ViewSetPadding);
    AddIntrinsicMethod(view, "getContext", {}, "Landroid/content/Context;", pub, ViewGetContext);
    AddIntrinsicMethod(view, "getId", {}, "I", pub, ViewGetId);
    AddIntrinsicMethod(view, "setId", {"I"}, "V", pub,
                       [](VMState& s, VObject* self, const Value* a, size_t n, bool* ok) {
                         (void)s;
                         *ok = true;
                         if (n >= 1) SetIField(self, 8 /* mId */, a[0].i);
                         return Value{};
                       });
    AddIntrinsicMethod(view, "setVisibility", {"I"}, "V", pub, ViewSetVisibility);
    AddIntrinsicMethod(view, "getVisibility", {}, "I", pub, ViewGetVisibility);
    AddIntrinsicMethod(view, "getBackground", {}, "Landroid/graphics/drawable/Drawable;", pub,
                       ViewGetBackground);
    AddIntrinsicMethod(view, "onScrollChanged", {"I", "I", "I", "I"}, "V", pub,
                       ViewOnScrollChanged);
    // AOSP View.setBackgroundColor：字段态落 mBackgroundColor（宿主物化器按
    // 该字段出画；程序化设置的骨架/占位背景因此可显示）。
    AddIntrinsicMethod(view, "setBackgroundColor", {"I"}, "V", pub,
                       [](VMState& s, VObject* self, const Value* a, size_t n, bool* ok) {
                         (void)s;
                         *ok = true;
                         if (n >= 1) {
                           VField* f = FindInstanceField(self->klass, "mBackgroundColor");
                           if (f != nullptr) SetIField(self, f->offset, a[0].i);
                         }
                         return Value{};
                       });
    AddIntrinsicMethod(view, "setOnClickListener", {"Landroid/view/View$OnClickListener;"}, "V",
                       pub, NoOp);
    // 长尾面（需求清单批补充）
    AddIntrinsicMethod(view, "getLeft", {}, "I", pub, ViewGetLeft);
    AddIntrinsicMethod(view, "getTop", {}, "I", pub, ViewGetTop);
    AddIntrinsicMethod(view, "getRight", {}, "I", pub, ViewGetRight);
    AddIntrinsicMethod(view, "getBottom", {}, "I", pub, ViewGetBottom);
    AddIntrinsicMethod(view, "getLayoutParams", {}, "Landroid/view/ViewGroup$LayoutParams;", pub,
                       ViewGetLayoutParams);
    AddIntrinsicMethod(view, "setLayoutParams", {"Landroid/view/ViewGroup$LayoutParams;"}, "V",
                       pub, ViewSetLayoutParams);
    AddIntrinsicMethod(view, "getParent", {}, "Landroid/view/ViewParent;", pub, ViewGetParent);
    AddIntrinsicMethod(view, "getTag", {}, "Ljava/lang/Object;", pub, ViewGetTag0);
    AddIntrinsicMethod(view, "setTag", {"Ljava/lang/Object;"}, "V", pub, ViewSetTag0);
    AddIntrinsicMethod(view, "getTag", {"I"}, "Ljava/lang/Object;", pub, ViewGetTagKeyed);
    AddIntrinsicMethod(view, "setTag", {"I", "Ljava/lang/Object;"}, "V", pub, ViewSetTagKeyed);
    AddIntrinsicMethod(view, "setAlpha", {"F"}, "V", pub, ViewSetAlpha);
    AddIntrinsicMethod(view, "getAlpha", {}, "F", pub, ViewGetAlpha);
    AddIntrinsicMethod(view, "setTranslationX", {"F"}, "V", pub, ViewSetTranslationX);
    AddIntrinsicMethod(view, "setTranslationY", {"F"}, "V", pub, ViewSetTranslationY);
    AddIntrinsicMethod(view, "getScrollX", {}, "I", pub, ViewGetScrollX);
    AddIntrinsicMethod(view, "getScrollY", {}, "I", pub, ViewGetScrollY);
    AddIntrinsicMethod(view, "setScrollX", {"I"}, "V", pub, ViewSetScrollX);
    AddIntrinsicMethod(view, "setScrollY", {"I"}, "V", pub, ViewSetScrollY);
    // 展示态侧表（AOSP View 属性面；实例布局无槽，走会话侧表）
    AddIntrinsicMethod(view, "setSelected", {"Z"}, "V", pub, ViewSetSelected);
    AddIntrinsicMethod(view, "isSelected", {}, "Z", pub, ViewIsSelected);
    AddIntrinsicMethod(view, "setActivated", {"Z"}, "V", pub, ViewSetActivated);
    AddIntrinsicMethod(view, "isActivated", {}, "Z", pub, ViewIsActivated);
    AddIntrinsicMethod(view, "setRotation", {"F"}, "V", pub, ViewSetRotation);
    AddIntrinsicMethod(view, "getRotation", {}, "F", pub, ViewGetRotation);
    AddIntrinsicMethod(view, "setContentDescription", {"Ljava/lang/CharSequence;"}, "V", pub,
                       ViewSetContentDescription);
    AddIntrinsicMethod(view, "getContentDescription", {}, "Ljava/lang/CharSequence;", pub,
                       ViewGetContentDescription);
    AddIntrinsicMethod(view, "requestLayout", {}, "V", pub, ViewRequestLayout);
    AddIntrinsicMethod(view, "isLayoutRequested", {}, "Z", pub, ViewIsLayoutRequested);
    // 布局/附着态谓词（AOSP View 状态位面的可观测投影）
    AddIntrinsicMethod(view, "isLaidOut", {}, "Z", pub,
                       [](VMState& st, VObject* self, const Value*, size_t, bool* ok) {
                         (void)st;
                         *ok = true;
                         Value r{};
                         int32_t l = 0, t = 0, rr = 0, b = 0;
                         if (VField* f = FindInstanceField(self->klass, "mLeft")) l = GetIField(self, f->offset);
                         if (VField* f = FindInstanceField(self->klass, "mTop")) t = GetIField(self, f->offset);
                         if (VField* f = FindInstanceField(self->klass, "mRight")) rr = GetIField(self, f->offset);
                         if (VField* f = FindInstanceField(self->klass, "mBottom")) b = GetIField(self, f->offset);
                         r.i = (rr > l || b > t) ? 1 : 0;
                         return r;
                       });
    AddIntrinsicMethod(view, "isAttachedToWindow", {}, "Z", pub,
                       [](VMState& st, VObject* self, const Value*, size_t, bool* ok) {
                         (void)st;
                         *ok = true;
                         Value r{};
                         if (VField* f = FindInstanceField(self->klass, "mParent")) {
                           r.i = GetRefField(self, f->offset) != nullptr ? 1 : 0;
                         } else {
                           r.i = 1;  // 树内视图默认已附着（桥接出画语义）
                         }
                         return r;
                       });
    AddIntrinsicMethod(view, "isShown", {}, "Z", pub,
                       [](VMState& st, VObject* self, const Value*, size_t, bool* ok) {
                         (void)st;
                         *ok = true;
                         Value r{};
                         int32_t vis = 0;
                         if (VField* f = FindInstanceField(self->klass, "mVisibility")) {
                           vis = GetIField(self, f->offset);
                         }
                         r.i = vis == 0 ? 1 : 0;
                         return r;
                       });
    AddIntrinsicMethod(view, "isInEditMode", {}, "Z", pub, ReturnZeroInt);
    // 属性数据留档（AOSP View.saveAttributeDataForStyleable：调试面空转）
    AddIntrinsicMethod(view, "saveAttributeDataForStyleable",
                       {"Landroid/content/Context;", "[I", "Landroid/util/AttributeSet;",
                        "Landroid/content/res/TypedArray;", "I", "I"},
                       "V", pub, NoOp);
    // Context 资源面（AOSP View.getResources = mContext.getResources）
    AddIntrinsicMethod(view, "getResources", {}, "Landroid/content/res/Resources;", pub,
                       [](VMState& st, VObject* self, const Value*, size_t, bool* ok) {
                         (void)st;
                         *ok = true;
                         Value r{};
                         if (r.ref == nullptr && g_context_resources != nullptr) {
                           r.ref = reinterpret_cast<ObjHeader*>(g_context_resources);
                         }
                         (void)self;
                         return r;
                       });
    AddIntrinsicMethod(view, "setWillNotDraw", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "setWillNotCacheDrawing", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "setMotionEventSplittingEnabled", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "setDescendantFocusability", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "getDescendantFocusability", {}, "I", pub, ReturnZeroInt);
    // ---- View 长尾面批（AOSP View 公开方法组；交互/动画/无障碍/绘制属性——
    //      桥接渲染只消费核心字段，其余为语义可调用面）----
    {
      const char* kVoidBoolSetters[] = {
          "setFocusable", "setFocusableInTouchMode", "setClickable", "setLongClickable",
          "setEnabled", "setSoundEffectsEnabled", "setHapticFeedbackEnabled", "setSaveEnabled",
          "setSaveFromParentEnabled", "setFilterTouchesWhenObscured",
          "setDuplicateParentStateEnabled", "setNestedScrollingEnabled", "setKeepScreenOn",
          "setDrawingCacheEnabled", "setClipToOutline", "setHovered", "setPressed",
          "setVerticalScrollBarEnabled", "setHorizontalScrollBarEnabled",
          "setScrollbarFadingEnabled", "setWillNotDraw", "setWillNotCacheDrawing"};
      for (const char* name : kVoidBoolSetters) {
        AddIntrinsicMethod(view, name, {"Z"}, "V", pub, NoOp);
      }
      AddIntrinsicMethod(view, "isEnabled", {}, "Z", pub, ReturnTrue);  // 默认可用
      const char* kBoolGetters[] = {"isClickable", "isLongClickable", "isPressed",
                                    "isSoundEffectsEnabled", "isHapticFeedbackEnabled",
                                    "isSaveEnabled", "isNestedScrollingEnabled", "isKeepScreenOn",
                                    "isDrawingCacheEnabled", "isClipToOutline", "isHovered",
                                    "getFilterTouchesWhenObscured", "getFitsSystemWindows",
                                    "isDuplicateParentStateEnabled"};
      for (const char* name : kBoolGetters) {
        AddIntrinsicMethod(view, name, {}, "Z", pub, ReturnZeroInt);
      }
      AddIntrinsicMethod(view, "setFitsSystemWindows", {"Z"}, "V", pub, NoOp);
      // 无障碍/Autofill/工具提示等单参组（属性面空转，getter 缺省）
      AddIntrinsicMethod(view, "setImportantForAccessibility", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "getImportantForAccessibility", {}, "I", pub, ReturnZeroInt);
      AddIntrinsicMethod(view, "setAccessibilityDelegate",
                         {"Landroid/view/View$AccessibilityDelegate;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "setAccessibilityLiveRegion", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "getAccessibilityLiveRegion", {}, "I", pub, ReturnZeroInt);
      AddIntrinsicMethod(view, "announceForAccessibility", {"Ljava/lang/CharSequence;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view, "sendAccessibilityEvent", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "sendAccessibilityEventUnchecked",
                         {"Landroid/view/accessibility/AccessibilityEvent;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "setAccessibilityPaneTitle", {"Ljava/lang/CharSequence;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view, "setTooltipText", {"Ljava/lang/CharSequence;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "getTooltipText", {}, "Ljava/lang/CharSequence;", pub, ReturnNull);
      AddIntrinsicMethod(view, "setAutofillHints", {"[Ljava/lang/String;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "getAutofillHints", {}, "[Ljava/lang/String;", pub, ReturnNull);
      AddIntrinsicMethod(view, "setImportantForAutofill", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "getImportantForAutofill", {}, "I", pub, ReturnZeroInt);
      AddIntrinsicMethod(view, "setLabelFor", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "getLabelFor", {}, "I", pub, ReturnZeroInt);
      const char* kFloatSetters[] = {"setElevation", "setTranslationZ", "setZ", "setRotationX",
                                     "setRotationY", "setScaleX", "setScaleY", "setPivotX",
                                     "setPivotY", "setCameraDistance"};
      for (const char* name : kFloatSetters) {
        AddIntrinsicMethod(view, name, {"F"}, "V", pub, NoOp);
      }
      auto return_one_float = [](VMState& st, VObject*, const Value*, size_t, bool* ok) {
        (void)st;
        *ok = true;
        Value r{};
        r.f = 1.0f;  // 默认缩放 1.0
        return r;
      };
      auto return_zero_float = [](VMState& st, VObject*, const Value*, size_t, bool* ok) {
        (void)st;
        *ok = true;
        return Value{};
      };
      AddIntrinsicMethod(view, "getScaleX", {}, "F", pub, return_one_float);
      AddIntrinsicMethod(view, "getScaleY", {}, "F", pub, return_one_float);
      const char* kFloatZeroGetters[] = {"getElevation", "getTranslationZ", "getZ",
                                         "getRotationX", "getRotationY", "getPivotX",
                                         "getPivotY", "getCameraDistance"};
      for (const char* name : kFloatZeroGetters) {
        AddIntrinsicMethod(view, name, {}, "F", pub, return_zero_float);
      }
    }

    // RTL/布局方向面（AOSP View：LTR 默认，无 RTL 资源覆盖）
    AddIntrinsicMethod(view, "getLayoutDirection", {}, "I", pub, ReturnZeroInt);      // LTR=0
    AddIntrinsicMethod(view, "getTextDirection", {}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "getRawLayoutDirection", {}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "getRawTextDirection", {}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "setLayoutDirection", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "setTextDirection", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "isLayoutDirectionResolved", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "isTextDirectionResolved", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "canResolveLayoutDirection", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "isHardwareAccelerated", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "isFocusable", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "isFocusableInTouchMode", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "requestFocus", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "requestFocus", {"I"}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(view, "clearFocus", {}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "getWindowToken", {}, "Landroid/os/IBinder;", pub, ReturnNull);
    AddIntrinsicMethod(view, "getApplicationWindowToken", {}, "Landroid/os/IBinder;", pub,
                       ReturnNull);
    AddIntrinsicMethod(view, "hasWindowFocus", {}, "Z", pub, ViewHasWindowFocus);
    AddIntrinsicMethod(view, "onWindowFocusChanged", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "onAttachedToWindow", {}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "onDetachedFromWindow", {}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "onWindowVisibilityChanged", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "onVisibilityChanged", {"Landroid/view/View;", "I"}, "V", pub,
                       NoOp);
    AddIntrinsicMethod(view, "getViewTreeObserver", {}, "Landroid/view/ViewTreeObserver;", pub,
                       ViewGetViewTreeObserver);
    AddIntrinsicMethod(view, "dispatchWindowFocusChanged", {"Z"}, "V", pub,
                       ViewDispatchWindowFocusChanged);
    AddIntrinsicMethod(view, "getLocationInWindow", {"[I"}, "V", pub,
                       [](VMState& st, VObject* self, const Value* a, size_t n, bool* ok) {
                         (void)st;
                         *ok = true;
                         if (n < 1 || a[0].ref == nullptr) return Value{};
                         auto* arr = reinterpret_cast<VArray*>(a[0].ref);
                         if (arr->length < 2) return Value{};
                         int32_t l = 0, t = 0;
                         for (VObject* v = self; v != nullptr;) {
                           if (VField* f = FindInstanceField(v->klass, "mLeft")) {
                             l += GetIField(v, f->offset);
                           }
                           if (VField* f = FindInstanceField(v->klass, "mTop")) {
                             t += GetIField(v, f->offset);
                           }
                           VField* pf = FindInstanceField(v->klass, "mParent");
                           v = pf != nullptr
                                   ? reinterpret_cast<VObject*>(GetRefField(v, pf->offset))
                                   : nullptr;
                         }
                         std::memcpy(arr->data(), &l, sizeof(l));
                         std::memcpy(arr->data() + 8, &t, sizeof(t));
                         return Value{};
                       });
    AddIntrinsicMethod(view, "getLocationOnScreen", {"[I"}, "V", pub,
                       [](VMState& st, VObject* self, const Value* a, size_t n, bool* ok) {
                         (void)st;
                         *ok = true;
                         if (n < 1 || a[0].ref == nullptr) return Value{};
                         auto* arr = reinterpret_cast<VArray*>(a[0].ref);
                         if (arr->length < 2) return Value{};
                         int32_t l = 0, t = 0;
                         for (VObject* v = self; v != nullptr;) {
                           if (VField* f = FindInstanceField(v->klass, "mLeft")) {
                             l += GetIField(v, f->offset);
                           }
                           if (VField* f = FindInstanceField(v->klass, "mTop")) {
                             t += GetIField(v, f->offset);
                           }
                           VField* pf = FindInstanceField(v->klass, "mParent");
                           v = pf != nullptr
                                   ? reinterpret_cast<VObject*>(GetRefField(v, pf->offset))
                                   : nullptr;
                         }
                         std::memcpy(arr->data(), &l, sizeof(l));
                         std::memcpy(arr->data() + 8, &t, sizeof(t));
                         return Value{};
                       });
    AddIntrinsicMethod(view, "invalidate", {}, "V", pub, ViewInvalidate);
    AddIntrinsicMethod(view, "invalidate", {"IIII"}, "V", pub, ViewInvalidate);
    AddIntrinsicMethod(view, "invalidate", {"Landroid/graphics/Rect;"}, "V", pub, ViewInvalidate);
    AddIntrinsicMethod(view, "removeCallbacks", {"Ljava/lang/Runnable;"}, "Z", pub,
                       ViewRemoveCallbacks);
    AddIntrinsicMethod(view, "post", {"Ljava/lang/Runnable;"}, "Z", pub, ViewPost);
    AddIntrinsicMethod(view, "postDelayed", {"Ljava/lang/Runnable;", "J"}, "Z", pub, ViewPost);
    AddIntrinsicMethod(view, "postOnAnimation", {"Ljava/lang/Runnable;"}, "V", pub,
                       ViewPostOnAnimation);
    AddIntrinsicMethod(view, "postOnAnimationDelayed", {"Ljava/lang/Runnable;", "J"}, "V", pub,
                       ViewPostOnAnimation);
    AddIntrinsicMethod(view, "animate", {}, "Landroid/view/ViewPropertyAnimator;", pub,
                       ViewAnimate);
    AddIntrinsicMethod(view, "performClick", {}, "Z", pub, ViewPerformClick);
    AddIntrinsicMethod(view, "performLongClick", {}, "Z", pub, ViewPerformLongClick);
    AddIntrinsicMethod(view, "findViewById", {"I"}, "Landroid/view/View;", pub, ViewFindViewById);
    AddIntrinsicMethod(view, "combineMeasuredStates", {"I", "I"}, "I", stat,
                       ViewCombineMeasuredStates);
    // View.generateViewId：AOSP 0x00FFFFFF 递增（与 android:id 资源区不重叠）
    AddIntrinsicMethod(view, "generateViewId", {}, "I", stat,
                       [](VMState& s, VObject*, const Value*, size_t, bool* ok) {
                         (void)s;
                         *ok = true;
                         Value r{};
                         static int32_t s_next_id = 0x00FFFFFF;
                         r.i = --s_next_id;
                         return r;
                       });
    AddIntrinsicMethod(view, "resolveSize", {"I", "I"}, "I", stat, ViewResolveSize);
    AddIntrinsicMethod(view, "resolveSizeAndState", {"I", "I", "I"}, "I", stat, ViewResolveSize);
    VClass* click_listener = MakeClass(linker, &all, "Landroid/view/View$OnClickListener;",
                                       object, 0, kAccPublic | kAccInterface | kAccAbstract);
    if (click_listener != nullptr) {
      // onClick 接口骨架：guest 字节码直接 invoke-interface 派发
      // listener.onClick（lib 控件覆写 performClick 等）需要可解析的接口方法；
      // 只有 native 派发（CallVirtual）时不声明也能跑，但 invoke-interface
      // 会因接口无该方法报 NoSuchMethodError。
      AddMethodSkeleton(click_listener, "onClick", {"Landroid/view/View;"}, "V",
                        pub | kAccAbstract);
    }
    // View.OnCreateContextMenuListener：androidx.fragment.app.Fragment 等实现的
    // 公开内嵌接口（缺注册即 interface not found -> Fragment 链接失败）。
    VClass* ctx_menu_listener = MakeClass(
        linker, &all, "Landroid/view/View$OnCreateContextMenuListener;", object, 0,
        kAccPublic | kAccInterface | kAccAbstract);
    if (ctx_menu_listener != nullptr) {
      AddMethodSkeleton(
          ctx_menu_listener, "onCreateContextMenu",
          {"Landroid/view/ContextMenu;", "Landroid/view/View;",
           "Landroid/view/ContextMenu$ContextMenuInfo;"},
          "V", pub | kAccAbstract);
    }
    VClass* insets_listener = MakeClass(
        linker, &all, "Landroid/view/View$OnApplyWindowInsetsListener;", object, 0,
        kAccPublic | kAccInterface | kAccAbstract);
    if (insets_listener != nullptr) {
      AddMethodSkeleton(insets_listener, "onApplyWindowInsets",
                        {"Landroid/view/View;", "Landroid/view/WindowInsets;"},
                        "Landroid/view/WindowInsets;", pub | kAccAbstract);
    }
    // 平台公开接口批量面（按 APK 全量 dex 扫描的高频 guest 实现面排序；
    // 纯 AOSP 签名，缺注册即 interface not found → 定义失败级联）。
    {
      struct IfaceSig {
        const char* desc;
        const char* method;
        const char* ret;
        std::vector<std::string> params;
      };
      static const IfaceSig kIfaceBatch[] = {
          // SeekBar$OnSeekBarChangeListener / RadioGroup$OnCheckedChangeListener
          // 属纯接口（Q4）→ lib 源随 runtime.dex 打包（§31.7 默认档），不再内建。
          {"Landroid/widget/TextView$OnEditorActionListener;", "onEditorAction", "Z",
           {"Landroid/widget/TextView;", "I", "Landroid/view/KeyEvent;"}},
          {"Landroid/view/GestureDetector$OnGestureListener;", "onDown", "Z",
           {"Landroid/view/MotionEvent;"}},
          {"Landroid/view/GestureDetector$OnGestureListener;", "onShowPress", "V",
           {"Landroid/view/MotionEvent;"}},
          {"Landroid/view/GestureDetector$OnGestureListener;", "onSingleTapUp", "Z",
           {"Landroid/view/MotionEvent;"}},
          {"Landroid/view/GestureDetector$OnGestureListener;", "onScroll", "Z",
           {"Landroid/view/MotionEvent;", "Landroid/view/MotionEvent;", "F", "F"}},
          {"Landroid/view/GestureDetector$OnGestureListener;", "onLongPress", "V",
           {"Landroid/view/MotionEvent;"}},
          {"Landroid/view/GestureDetector$OnGestureListener;", "onFling", "Z",
           {"Landroid/view/MotionEvent;", "Landroid/view/MotionEvent;", "F", "F"}},
          {"Landroid/webkit/ValueCallback;", "onReceiveValue", "V", {"Ljava/lang/Object;"}},
          {"Landroid/location/LocationListener;", "onLocationChanged", "V",
           {"Landroid/location/Location;"}},
          {"Landroid/location/LocationListener;", "onStatusChanged", "V",
           {"Ljava/lang/String;", "I", "Landroid/os/Bundle;"}},
          {"Landroid/location/LocationListener;", "onProviderEnabled", "V",
           {"Ljava/lang/String;"}},
          {"Landroid/location/LocationListener;", "onProviderDisabled", "V",
           {"Ljava/lang/String;"}},
          {"Landroid/view/Choreographer$FrameCallback;", "doFrame", "V", {"J"}},
          {"Landroid/text/InputFilter;", "filter", "Ljava/lang/CharSequence;",
           {"Ljava/lang/CharSequence;", "I", "I", "Landroid/text/Spanned;", "I", "I"}},
          {"Landroid/graphics/drawable/Animatable;", "start", "V", {}},
          {"Landroid/graphics/drawable/Animatable;", "stop", "V", {}},
          {"Landroid/graphics/drawable/Animatable;", "isRunning", "Z", {}},
          {"Landroid/view/SurfaceHolder$Callback;", "surfaceCreated", "V",
           {"Landroid/view/SurfaceHolder;"}},
          {"Landroid/view/SurfaceHolder$Callback;", "surfaceChanged", "V",
           {"Landroid/view/SurfaceHolder;", "I", "I", "I"}},
          {"Landroid/view/SurfaceHolder$Callback;", "surfaceDestroyed", "V",
           {"Landroid/view/SurfaceHolder;"}},
          {"Landroid/animation/TypeEvaluator;", "evaluate", "Ljava/lang/Object;",
           {"F", "Ljava/lang/Object;", "Ljava/lang/Object;"}},
          {"Landroid/util/Printer;", "println", "V", {"Ljava/lang/String;"}},
          {"Landroid/media/ImageReader$OnImageAvailableListener;", "onImageAvailable", "V",
           {"Landroid/media/ImageReader;"}},
          {"Landroid/media/MediaPlayer$OnInfoListener;", "onInfo", "Z",
           {"Landroid/media/MediaPlayer;", "I", "I"}},
          {"Landroid/view/ScaleGestureDetector$OnScaleGestureListener;", "onScale", "Z",
           {"Landroid/view/ScaleGestureDetector;"}},
          {"Landroid/view/ScaleGestureDetector$OnScaleGestureListener;", "onScaleBegin", "Z",
           {"Landroid/view/ScaleGestureDetector;"}},
          {"Landroid/view/ScaleGestureDetector$OnScaleGestureListener;", "onScaleEnd", "V",
           {"Landroid/view/ScaleGestureDetector;"}},
          {"Landroid/view/ViewGroup$OnHierarchyChangeListener;", "onChildViewAdded", "V",
           {"Landroid/view/View;", "Landroid/view/View;"}},
          {"Landroid/view/ViewGroup$OnHierarchyChangeListener;", "onChildViewRemoved", "V",
           {"Landroid/view/View;", "Landroid/view/View;"}},
          {"Landroid/view/View$OnSystemUiVisibilityChangeListener;",
           "onSystemUiVisibilityChange", "V", {"I"}},
          {"Landroid/widget/Checkable;", "setChecked", "V", {"Z"}},
          {"Landroid/widget/Checkable;", "isChecked", "Z", {}},
          {"Landroid/widget/Checkable;", "toggle", "V", {}},
          {"Landroid/widget/ViewSwitcher$ViewFactory;", "makeView", "Landroid/view/View;", {}},
      };
      // 同描述符多条方法：首次遇到时建类，后续追加入同表。
      std::vector<VClass*> created;
      for (const auto& s : kIfaceBatch) {
        VClass* k = nullptr;
        for (VClass* c : created) {
          if (c->descriptor == s.desc) {
            k = c;
            break;
          }
        }
        if (k == nullptr) {
          k = MakeClass(linker, &all, s.desc, object, 0,
                        kAccPublic | kAccInterface | kAccAbstract);
          if (k != nullptr) created.push_back(k);
        }
        if (k != nullptr) AddMethodSkeleton(k, s.method, s.params, s.ret, pub | kAccAbstract);
      }
    }
    // View 事件监听接口族（AOSP 公开内嵌接口；guest 类实现面缺注册即
    // interface not found → 定义失败级联）。setter 暂 NoOp（除点击已由
    // render.cc 覆写为真实登记）。
    {
      struct {
        const char* desc;
        const char* method;
        const char* ret;
        std::vector<std::string> params;
      } kVListeners[] = {
          {"Landroid/view/View$OnTouchListener;", "onTouch", "Z",
           {"Landroid/view/View;", "Landroid/view/MotionEvent;"}},
          {"Landroid/view/View$OnLongClickListener;", "onLongClick", "Z",
           {"Landroid/view/View;"}},
          {"Landroid/view/View$OnFocusChangeListener;", "onFocusChange", "V",
           {"Landroid/view/View;", "Z"}},
          {"Landroid/view/View$OnKeyListener;", "onKey", "Z",
           {"Landroid/view/View;", "I", "Landroid/view/KeyEvent;"}},
          {"Landroid/view/View$OnHoverListener;", "onHover", "Z",
           {"Landroid/view/View;", "Landroid/view/MotionEvent;"}},
          {"Landroid/view/View$OnGenericMotionListener;", "onGenericMotion", "Z",
           {"Landroid/view/View;", "Landroid/view/MotionEvent;"}},
          {"Landroid/view/View$OnScrollChangeListener;", "onScrollChange", "V",
           {"Landroid/view/View;", "I", "I", "I", "I"}},
      };
      for (const auto& l : kVListeners) {
        VClass* lk = MakeClass(linker, &all, l.desc, object, 0,
                               kAccPublic | kAccInterface | kAccAbstract);
        if (lk != nullptr) {
          AddMethodSkeleton(lk, l.method, l.params, l.ret, pub | kAccAbstract);
        }
      }
      AddIntrinsicMethod(view, "setOnTouchListener", {"Landroid/view/View$OnTouchListener;"},
                         "V", pub, NoOp);
      AddIntrinsicMethod(view, "setOnLongClickListener",
                         {"Landroid/view/View$OnLongClickListener;"}, "V", pub,
                         ViewSetOnLongClickListener);
      AddIntrinsicMethod(view, "setOnFocusChangeListener",
                         {"Landroid/view/View$OnFocusChangeListener;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "setOnKeyListener", {"Landroid/view/View$OnKeyListener;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view, "setOnHoverListener", {"Landroid/view/View$OnHoverListener;"},
                         "V", pub, NoOp);
      AddIntrinsicMethod(view, "setOnGenericMotionListener",
                         {"Landroid/view/View$OnGenericMotionListener;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view, "setOnScrollChangeListener",
                         {"Landroid/view/View$OnScrollChangeListener;"}, "V", pub, NoOp);
    }
    // android.webkit.DownloadListener：AOSP 公开接口（WebView 下载回调）。
    // 知乎 WebViewFragment2 实现它——缺注册即 interface not found → 定义失败
    // 级联（UserLevelFragment super not found → router p3$a.<clinit> 失败）。
    VClass* dl_listener = MakeClass(
        linker, &all, "Landroid/webkit/DownloadListener;", object, 0,
        kAccPublic | kAccInterface | kAccAbstract);
    if (dl_listener != nullptr) {
      AddMethodSkeleton(dl_listener, "onDownloadStart",
                        {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;",
                         "Ljava/lang/String;", "J"},
                        "V", pub | kAccAbstract);
    }
    // View.OnLayoutChangeListener：material TabLayout$TabView$1 等实现的公开
    // 内嵌接口（缺注册 -> interface not found -> 链接失败）。
    VClass* layout_change_listener = MakeClass(
        linker, &all, "Landroid/view/View$OnLayoutChangeListener;", object, 0,
        kAccPublic | kAccInterface | kAccAbstract);
    if (layout_change_listener != nullptr) {
      AddMethodSkeleton(layout_change_listener, "onLayoutChange",
                        {"Landroid/view/View;", "I", "I", "I", "I", "I", "I", "I", "I"}, "V",
                        pub | kAccAbstract);
    }
    AddIntrinsicMethod(view, "addOnLayoutChangeListener",
                       {"Landroid/view/View$OnLayoutChangeListener;"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "removeOnLayoutChangeListener",
                       {"Landroid/view/View$OnLayoutChangeListener;"}, "V", pub, NoOp);
    VClass* attach_listener = MakeClass(
        linker, &all, "Landroid/view/View$OnAttachStateChangeListener;", object, 0,
        kAccPublic | kAccInterface | kAccAbstract);
    if (attach_listener != nullptr) {
      AddMethodSkeleton(attach_listener, "onViewAttachedToWindow", {"Landroid/view/View;"}, "V",
                        pub | kAccAbstract);
      AddMethodSkeleton(attach_listener, "onViewDetachedFromWindow", {"Landroid/view/View;"}, "V",
                        pub | kAccAbstract);
    }
    AddIntrinsicMethod(view, "addOnAttachStateChangeListener",
                       {"Landroid/view/View$OnAttachStateChangeListener;"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "removeOnAttachStateChangeListener",
                       {"Landroid/view/View$OnAttachStateChangeListener;"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "setOnApplyWindowInsetsListener",
                       {"Landroid/view/View$OnApplyWindowInsetsListener;"}, "V", pub, NoOp);
    AddIntrinsicMethod(view, "requestApplyInsets", {}, "V", pub, NoOp);
    // 滚动条动画面（ScrollView/ViewGroup 滚动路径调用；VM 无滚动条，恒 true）
    AddIntrinsicMethod(view, "awakenScrollBars", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "awakenScrollBars", {"I"}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "awakenScrollBars", {"I", "Z"}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view, "getRootWindowInsets", {}, "Landroid/view/WindowInsets;", pub,
                       ReturnNull);
    // API 30 WindowInsetsController 面（androidx Impl30 直接调用，恒非 null）
    AddIntrinsicMethod(view, "getWindowInsetsController", {},
                       "Landroid/view/WindowInsetsController;", pub,
                       ViewGetWindowInsetsController);
    // AOSP 默认：onApplyWindowInsets 原样返回入参
    Value (*insets_pass)(VMState&, VObject*, const Value*, size_t, bool*) =
        [](VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
          (void)state;
          (void)self;
          (void)n;
          *ok = true;
          Value r{};
          if (n >= 1) r = args[0];
          return r;
        };
    AddIntrinsicMethod(view, "onApplyWindowInsets",
                       {"Landroid/view/WindowInsets;"}, "Landroid/view/WindowInsets;", prot,
                       insets_pass);
    // android.view.View$AccessibilityDelegate（内建内嵌类：无障碍委派面骨架）
    VClass* a11y_del = MakeClass(linker, &all, "Landroid/view/View$AccessibilityDelegate;", object,
                                 0, kAccPublic);
    if (a11y_del != nullptr) {
      AddIntrinsicMethod(a11y_del, "<init>", {}, "V", ctor, NoOp);
      AddMethodSkeleton(a11y_del, "dispatchPopulateAccessibilityEvent",
                        {"Landroid/view/View;", "Landroid/view/accessibility/AccessibilityEvent;"},
                        "Z", pub);
      AddMethodSkeleton(a11y_del, "onInitializeAccessibilityNodeInfo",
                        {"Landroid/view/View;",
                         "Landroid/view/accessibility/AccessibilityNodeInfo;"},
                        "V", pub);
      AddMethodSkeleton(a11y_del, "onRequestSendAccessibilityEvent",
                        {"Landroid/view/ViewGroup;", "Landroid/view/View;",
                         "Landroid/view/accessibility/AccessibilityEvent;"},
                        "Z", pub);
      AddMethodSkeleton(a11y_del, "getAccessibilityNodeProvider", {"Landroid/view/View;"},
                        "Landroid/view/accessibility/AccessibilityNodeProvider;", pub);
      AddMethodSkeleton(a11y_del, "performAccessibilityAction",
                        {"Landroid/view/View;", "I", "Landroid/os/Bundle;"}, "Z", pub);
    }

    // android.view.View$MeasureSpec（内建内嵌类：静态打包面）
    VClass* ms = MakeClass(linker, &all, "Landroid/view/View$MeasureSpec;", object, 0,
                           kAccPublic | kAccFinal);
    if (ms != nullptr) {
      AddStaticField(ms, "UNSPECIFIED", "I");
      AddStaticField(ms, "EXACTLY", "I");
      AddStaticField(ms, "AT_MOST", "I");
      ms->static_storage[0].i = 0;
      ms->static_storage[1].i = 0x40000000;
      ms->static_storage[2].i = static_cast<int32_t>(0x80000000u);
      AddIntrinsicMethod(ms, "makeMeasureSpec", {"I", "I"}, "I", stat, MSMakeMeasureSpec);
      AddIntrinsicMethod(ms, "getMode", {"I"}, "I", stat, MSGetMode);
      AddIntrinsicMethod(ms, "getSize", {"I"}, "I", stat, MSGetSize);
    }
  }
  VClass* view_group = nullptr;
  VClass* layout_params = nullptr;
  VClass* margin_params = nullptr;
  if (view != nullptr) {
    view_group = MakeClass(linker, &all, "Landroid/view/ViewGroup;", view, 184, kAccPublic);
    // mChildren："[Landroid/view/View;" 数组字段（addView 存子 View，M4.2）
    AddInstanceField(view_group, "mChildren", "[Landroid/view/View;", 176);
    AddIntrinsicMethod(view_group, "addView", {"Landroid/view/View;"}, "V", pub, VGAddView);
    AddIntrinsicMethod(view_group, "addView", {"Landroid/view/View;", "I"}, "V", pub, VGAddView);
    AddIntrinsicMethod(view_group, "removeAllViews", {}, "V", pub, VGRemoveAllViews);
    AddIntrinsicMethod(view_group, "getChildCount", {}, "I", pub, VGGetChildCount);
    AddIntrinsicMethod(view_group, "getChildAt", {"I"}, "Landroid/view/View;", pub, VGGetChildAt);
    AddIntrinsicMethod(view_group, "indexOfChild", {"Landroid/view/View;"}, "I", pub,
                       ViewGroupIndexOfChild);
    // ---- ViewGroup 长尾面批（addView LP 重载/移除/裁剪/焦点分配组）----
    AddIntrinsicMethod(view_group, "addView",
                       {"Landroid/view/View;", "Landroid/view/ViewGroup$LayoutParams;"}, "V", pub,
                       VGAddView2);
    AddIntrinsicMethod(view_group, "addView",
                       {"Landroid/view/View;", "I", "Landroid/view/ViewGroup$LayoutParams;"}, "V",
                       pub, VGAddView2);
    AddIntrinsicMethod(view_group, "addView", {"Landroid/view/View;", "I", "I"}, "V", pub,
                       VGAddView);
    AddIntrinsicMethod(view_group, "removeViewAt", {"I"}, "V", pub, VGRemoveViewAt);
    AddIntrinsicMethod(view_group, "removeViews", {"I", "I"}, "V", pub, VGRemoveViewAt);
    AddIntrinsicMethod(view_group, "removeAllViewsInLayout", {}, "V", pub, VGRemoveAllViews);
    AddIntrinsicMethod(view_group, "removeViewInLayout", {"Landroid/view/View;"}, "V", pub,
                       VGRemoveView);
    AddIntrinsicMethod(view_group, "bringChildToFront", {"Landroid/view/View;"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "setClipChildren", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "getClipChildren", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view_group, "setClipToPadding", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "getClipToPadding", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view_group, "setLayoutAnimation", {"Landroid/view/animation/LayoutAnimationController;"},
                       "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "startLayoutAnimation", {}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "scheduleLayoutAnimation", {}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "setLayoutTransition", {"Landroid/animation/LayoutTransition;"},
                       "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "getLayoutTransition", {}, "Landroid/animation/LayoutTransition;",
                       pub, ReturnNull);
    AddIntrinsicMethod(view_group, "setTransitioning", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "setAddStatesFromChildren", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "addStatesFromChildren", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(view_group, "setTouchscreenBlocksFocus", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "getTouchscreenBlocksFocus", {}, "Z", pub, ReturnZeroInt);
    AddIntrinsicMethod(view_group, "setAlwaysDrawnWithCacheEnabled", {"Z"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "setLayoutMode", {"I"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "getLayoutMode", {}, "I", pub, ReturnZeroInt);
    AddIntrinsicMethod(view_group, "setOnHierarchyChangeListener",
                       {"Landroid/view/ViewGroup$OnHierarchyChangeListener;"}, "V", pub, NoOp);
    AddIntrinsicMethod(view_group, "shouldDelayChildPressedState", {}, "Z", pub, ReturnTrue);
    AddIntrinsicMethod(view_group, "checkLayoutParams",
                       {"Landroid/view/ViewGroup$LayoutParams;"}, "Z", pub, VGCheckLayoutParams);
    // 容器 LP 工厂：此前被 API stub 补成 ReturnNull → inflater 拿不到强类型
    // LayoutParams（AppBarLayout/CoordinatorLayout 强转 CCE 根因之一）。
    AddIntrinsicMethod(view_group, "generateLayoutParams",
                       {"Landroid/util/AttributeSet;"},
                       "Landroid/view/ViewGroup$LayoutParams;", pub, VGGenerateLayoutParams);
    AddIntrinsicMethod(view_group, "generateLayoutParams",
                       {"Landroid/view/ViewGroup$LayoutParams;"},
                       "Landroid/view/ViewGroup$LayoutParams;", pub, VGGenerateLayoutParams);
    AddIntrinsicMethod(view_group, "generateDefaultLayoutParams", {},
                       "Landroid/view/ViewGroup$LayoutParams;", pub, VGGenerateLayoutParams);
    AddIntrinsicMethod(view_group, "getChildDrawingOrder", {"I", "I"}, "I", pub,
                       [](VMState& st, VObject*, const Value* a, size_t n, bool* ok) {
                         (void)st;
                         *ok = true;
                         Value r{};
                         r.i = n >= 2 ? a[1].i : 0;  // 默认绘制序 = 子序
                         return r;
                       });
    AddIntrinsicMethod(view_group, "removeView", {"Landroid/view/View;"}, "V", pub, VGRemoveView);
    // AOSP 隐藏附着面（androidx ViewPager2/RecyclerView 消费）
    AddIntrinsicMethod(view_group, "attachViewToParent",
                       {"Landroid/view/View;", "I", "Landroid/view/ViewGroup$LayoutParams;"}, "V",
                       pub, VGAttachViewToParent);
    AddIntrinsicMethod(view_group, "detachViewFromParent", {"Landroid/view/View;"}, "V", pub,
                       VGDetachViewFromParent);
    AddIntrinsicMethod(view_group, "detachViewFromParent", {"I"}, "V", pub, VGDetachViewFromParentIdx);
    AddIntrinsicMethod(view_group, "removeDetachedView", {"Landroid/view/View;", "Z"}, "V", pub,
                       VGRemoveDetachedView);
    AddIntrinsicMethod(view_group, "getPaddingLeft", {}, "I", pub, VGGetPaddingLeft);
    AddIntrinsicMethod(view_group, "getPaddingTop", {}, "I", pub, VGGetPaddingTop);
    AddIntrinsicMethod(view_group, "getPaddingRight", {}, "I", pub, VGGetPaddingRight);
    AddIntrinsicMethod(view_group, "getPaddingBottom", {}, "I", pub, VGGetPaddingBottom);
    AddIntrinsicMethod(view_group, "getChildMeasureSpec", {"I", "I", "I"}, "I", stat,
                       VGGetChildMeasureSpec);
    AddIntrinsicMethod(view_group, "measureChild", {"Landroid/view/View;", "I", "I"}, "V", pub,
                       VGMeasureChild);
    // ViewParent 接口面（内建接口；ViewGroup 实体实现空转/字段缝）——invoke-interface
    // 分发要求接收者类声明同签名方法（FindInterfaceImpl）。
    VClass* vp = MakeClass(linker, &all, "Landroid/view/ViewParent;", object, 0,
                           kAccPublic | kAccInterface | kAccAbstract);
    if (vp != nullptr) {
      AddMethodSkeleton(vp, "requestLayout", {}, "V", pub);
      AddMethodSkeleton(vp, "isLayoutRequested", {}, "Z", pub);
      AddMethodSkeleton(vp, "requestTransparentRegion", {"Landroid/view/View;"}, "V", pub);
      AddMethodSkeleton(vp, "invalidateChild", {"Landroid/view/View;", "Landroid/graphics/Rect;"},
                        "V", pub);
      AddMethodSkeleton(vp, "getParent", {}, "Landroid/view/ViewParent;", pub);
      AddMethodSkeleton(vp, "requestChildFocus", {"Landroid/view/View;", "Landroid/view/View;"},
                        "V", pub);
      AddMethodSkeleton(vp, "recomputeViewAttributes", {"Landroid/view/View;"}, "V", pub);
      AddMethodSkeleton(vp, "clearChildFocus", {"Landroid/view/View;"}, "V", pub);
      AddMethodSkeleton(vp, "getRootView", {}, "Landroid/view/View;", pub);
      AddMethodSkeleton(vp, "childDrawableStateChanged", {"Landroid/view/View;"}, "V", pub);
      AddMethodSkeleton(vp, "focusableViewAvailable", {"Landroid/view/View;"}, "Z", pub);
      AddMethodSkeleton(vp, "showContextMenuForChild", {"Landroid/view/View;"}, "Z", pub);
      AddMethodSkeleton(vp, "createContextMenu", {"Landroid/view/ContextMenu;"}, "V", pub);
      AddMethodSkeleton(vp, "sendSubtreeAccessibilityEvent",
                        {"Landroid/view/View;", "I"}, "V", pub);
      AddMethodSkeleton(vp, "invalidateChildInParent", {"[I", "Landroid/graphics/Rect;"},
                        "Landroid/view/ViewParent;", pub);
      // ViewGroup 实体实现（空转面；字段缝面复用 View 长尾）
      AddIntrinsicMethod(view_group, "requestLayout", {}, "V", pub, ViewRequestLayout);
      AddIntrinsicMethod(view_group, "isLayoutRequested", {}, "Z", pub, ViewIsLayoutRequested);
      AddIntrinsicMethod(view_group, "requestTransparentRegion", {"Landroid/view/View;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view_group, "invalidateChild",
                         {"Landroid/view/View;", "Landroid/graphics/Rect;"}, "V", pub,
                         ViewInvalidate);
      AddIntrinsicMethod(view_group, "requestChildFocus",
                         {"Landroid/view/View;", "Landroid/view/View;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "recomputeViewAttributes", {"Landroid/view/View;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view_group, "clearChildFocus", {"Landroid/view/View;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getRootView", {}, "Landroid/view/View;", pub, ViewGetParent);
      AddIntrinsicMethod(view_group, "childDrawableStateChanged", {"Landroid/view/View;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view_group, "focusableViewAvailable", {"Landroid/view/View;"}, "Z", pub,
                         NoOp);
      AddIntrinsicMethod(view_group, "showContextMenuForChild", {"Landroid/view/View;"}, "Z", pub,
                         NoOp);
      AddIntrinsicMethod(view_group, "createContextMenu", {"Landroid/view/ContextMenu;"}, "V",
                         pub, NoOp);
      AddIntrinsicMethod(view_group, "sendSubtreeAccessibilityEvent", {"Landroid/view/View;", "I"},
                         "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "invalidateChildInParent", {"[I", "Landroid/graphics/Rect;"},
                         "Landroid/view/ViewParent;", pub, ReturnNull);
      AddIntrinsicMethod(view_group, "getParentForAccessibility", {}, "Landroid/view/ViewParent;",
                         pub, ViewGetParent);
      // ViewGroup 绘制/布局开关长尾面：guest 布局与 RecyclerView 初始化会调用
      // （缺注册 = NoSuchMethodError，measure 整链中止）。这些开关不改变 VM
      // 的 measure/layout 结果，按 AOSP 默认值空转。
      AddIntrinsicMethod(view_group, "setChildrenDrawingOrderEnabled", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getChildrenDrawingOrderEnabled", {}, "Z", pub, NoOp);
      AddIntrinsicMethod(view_group, "setClipChildren", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getClipChildren", {}, "Z", pub, ReturnTrue);
      AddIntrinsicMethod(view_group, "setClipToPadding", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getClipToPadding", {}, "Z", pub, ReturnTrue);
      AddIntrinsicMethod(view_group, "setAddStatesFromChildren", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "addStatesFromChildren", {}, "Z", pub, NoOp);
      AddIntrinsicMethod(view_group, "setAlwaysDrawnWithCacheEnabled", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "isAlwaysDrawnWithCacheEnabled", {}, "Z", pub, ReturnTrue);
      AddIntrinsicMethod(view_group, "setAnimationCacheEnabled", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "isAnimationCacheEnabled", {}, "Z", pub, ReturnTrue);
      AddIntrinsicMethod(view_group, "setPersistentDrawingCache", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getPersistentDrawingCache", {}, "I", pub, NoOp);
      AddIntrinsicMethod(view_group, "setLayoutMode", {"I"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getLayoutMode", {}, "I", pub, NoOp);
      AddIntrinsicMethod(view_group, "setLayoutTransition",
                         {"Landroid/animation/LayoutTransition;"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getLayoutTransition", {},
                         "Landroid/animation/LayoutTransition;", pub, ReturnNull);
      AddIntrinsicMethod(view_group, "setTransitionGroup", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "isTransitionGroup", {}, "Z", pub, NoOp);
      AddIntrinsicMethod(view_group, "setTouchscreenBlocksFocus", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "getTouchscreenBlocksFocus", {}, "Z", pub, NoOp);
      AddIntrinsicMethod(view_group, "setKeyboardNavigationCluster", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(view_group, "isKeyboardNavigationCluster", {}, "Z", pub, NoOp);
      AddIntrinsicMethod(view_group, "setMotionEventSplittingEnabled", {"Z"}, "V", pub, NoOp);
      // AOSP ViewGroup.onScrollChanged：滚动容器 layout 期回调（缺注册 =
      // NoSuchMethodError，traversal layout 中止）。
      AddIntrinsicMethod(view_group, "onScrollChanged", {"I", "I", "I", "I"}, "V", pub,
                         ViewOnScrollChanged);
    }
    // ViewGroup$LayoutParams（width/height 公开字段 + 构造面）
    layout_params =
        MakeClass(linker, &all, "Landroid/view/ViewGroup$LayoutParams;", object, 16, kAccPublic);
    if (layout_params != nullptr) {
      AddInstanceField(layout_params, "width", "I", 0);
      AddInstanceField(layout_params, "height", "I", 8);
      // AOSP ViewGroup.LayoutParams 公开常量（guest sget 面；缺注册 =
      // NoSuchFieldError，横跨所有布局代码）。
      AddStaticField(layout_params, "MATCH_PARENT", "I");
      AddStaticField(layout_params, "FILL_PARENT", "I");
      AddStaticField(layout_params, "WRAP_CONTENT", "I");
      layout_params->static_storage[0].i = -1;  // MATCH_PARENT
      layout_params->static_storage[1].i = -1;  // FILL_PARENT（deprecated alias）
      layout_params->static_storage[2].i = -2;  // WRAP_CONTENT
      AddIntrinsicMethod(layout_params, "<init>", {"I", "I"}, "V", ctor, LPInit);
      AddIntrinsicMethod(layout_params, "<init>", {"Landroid/content/Context;",
                                                   "Landroid/util/AttributeSet;"},
                         "V", ctor, LPInitAttrs);
      AddIntrinsicMethod(layout_params, "<init>", {"Landroid/view/ViewGroup$LayoutParams;"}, "V",
                         ctor, LPInitCopy);
    }
    // ViewGroup$MarginLayoutParams（+ 四向 margin）
    margin_params = MakeClass(linker, &all, "Landroid/view/ViewGroup$MarginLayoutParams;",
                              layout_params, 48, kAccPublic);
    if (margin_params != nullptr) {
      AddInstanceField(margin_params, "leftMargin", "I", 16);
      AddInstanceField(margin_params, "topMargin", "I", 24);
      AddInstanceField(margin_params, "rightMargin", "I", 32);
      AddInstanceField(margin_params, "bottomMargin", "I", 40);
      AddIntrinsicMethod(margin_params, "<init>", {"I", "I"}, "V", ctor, LPInit);
      AddIntrinsicMethod(margin_params, "<init>", {"Landroid/content/Context;",
                                                   "Landroid/util/AttributeSet;"},
                         "V", ctor, LPInitAttrs);
      AddIntrinsicMethod(margin_params, "<init>",
                         {"Landroid/view/ViewGroup$MarginLayoutParams;"}, "V", ctor, LPInitCopy);
      AddIntrinsicMethod(margin_params, "<init>", {"Landroid/view/ViewGroup$LayoutParams;"}, "V",
                         ctor, LPInitCopy);
    }
  }
  if (view != nullptr) {
    VClass* text_view = MakeClass(linker, &all, "Landroid/widget/TextView;", view, 208, kAccPublic);
    AddInstanceField(text_view, "mText", "Ljava/lang/CharSequence;", 176);
    AddInstanceField(text_view, "mTextSize", "F", 184);
    AddInstanceField(text_view, "mTextColor", "I", 192);
    AddInstanceField(text_view, "mGravity", "I", 200);
    AddIntrinsicMethod(text_view, "<init>", {"Landroid/content/Context;"}, "V", ctor, TVInit);
    AddIntrinsicMethod(text_view, "<init>",
                       {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, "V", ctor,
                       TVInit);
    AddIntrinsicMethod(text_view, "<init>",
                       {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}, "V",
                       ctor, TVInit);
    AddIntrinsicMethod(text_view, "setGravity", {"I"}, "V", pub, LLSetGravity);
    AddIntrinsicMethod(text_view, "getGravity", {}, "I", pub, LLGetGravity);
    // InputFilter/TextWatcher 面（AOSP getFilters 恒非 null；androidx
    // AppCompatTextView 合并过滤器直接 .length——null 即 NPE，工厂创建失败实证）。
    AddIntrinsicMethod(text_view, "getFilters", {}, "[Landroid/text/InputFilter;", pub,
                       TVGetFilters);
    AddIntrinsicMethod(text_view, "setFilters", {"[Landroid/text/InputFilter;"}, "V", pub, NoOp);
    AddIntrinsicMethod(text_view, "addTextChangedListener", {"Landroid/text/TextWatcher;"}, "V",
                       pub, TextViewAddTextWatcher);
    AddIntrinsicMethod(text_view, "removeTextChangedListener",
                       {"Landroid/text/TextWatcher;"}, "V", pub, TextViewRemoveTextWatcher);
    AddIntrinsicMethod(text_view, "setText", {"Ljava/lang/CharSequence;"}, "V", pub, TVSetText);
    AddIntrinsicMethod(text_view, "setText", {"Ljava/lang/String;"}, "V", pub, TVSetText);
    // AOSP TextView.setText(int resId)：资源串 -> mText（framework_app.cc）
    AddIntrinsicMethod(text_view, "setText", {"I"}, "V", pub, TVSetTextRes);
    AddIntrinsicMethod(text_view, "getText", {}, "Ljava/lang/CharSequence;", pub, TVGetText);
    AddIntrinsicMethod(text_view, "setTextSize", {"F"}, "V", pub, TVSetTextSize);
    AddIntrinsicMethod(text_view, "setTextSize", {"I", "F"}, "V", pub, NoOp);
    // android.widget.HorizontalScrollView（ViewGroup 语义承接；Zhihu bottomnav /
    // androidx NestedScrollView 基类——缺失会让整段导航布局链接失败）
    VClass* hscroll =
        MakeClass(linker, &all, "Landroid/widget/HorizontalScrollView;", view_group, 200,
                  kAccPublic);
    if (hscroll != nullptr) {
      AddIntrinsicMethod(hscroll, "<init>", {"Landroid/content/Context;"}, "V", ctor, ViewInit);
      AddIntrinsicMethod(hscroll, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, "V", ctor,
                         ViewInit);
      AddIntrinsicMethod(hscroll, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}, "V",
                         ctor, ViewInit);
      AddIntrinsicMethod(hscroll, "setFillViewport", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(hscroll, "setSmoothScrollingEnabled", {"Z"}, "V", pub, NoOp);
      AddIntrinsicMethod(hscroll, "smoothScrollTo", {"I", "I"}, "V", pub, NoOp);
      AddIntrinsicMethod(hscroll, "smoothScrollBy", {"I", "I"}, "V", pub, NoOp);
      AddIntrinsicMethod(hscroll, "fullScroll", {"I"}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(hscroll, "arrowScroll", {"I"}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(hscroll, "pageScroll", {"I"}, "Z", pub, ReturnZeroInt);
      AddIntrinsicMethod(hscroll, "getMaxScrollAmount", {}, "I", pub, ReturnZeroInt);
    }
    // android.widget.Toast（应用提示面：文案入实例，显示空转；宿主无 UI 通道）
    VClass* toast = MakeClass(linker, &all, "Landroid/widget/Toast;", object, 16, kAccPublic);
    if (toast != nullptr) {
      AddInstanceField(toast, "mText", "Ljava/lang/CharSequence;", 0);
      AddInstanceField(toast, "mDuration", "I", 8);
      AddStaticField(toast, "LENGTH_SHORT", "I");
      AddStaticField(toast, "LENGTH_LONG", "I");
      toast->static_storage[0].i = 0;
      toast->static_storage[1].i = 1;
      AddIntrinsicMethod(toast, "<init>", {"Landroid/content/Context;"}, "V", kAccPublic, NoOp);
      AddIntrinsicMethod(toast, "makeText",
                         {"Landroid/content/Context;", "Ljava/lang/CharSequence;", "I"},
                         "Landroid/widget/Toast;", pub | kAccStatic, ToastMakeText);
      AddIntrinsicMethod(toast, "makeText", {"Landroid/content/Context;", "I", "I"},
                         "Landroid/widget/Toast;", pub | kAccStatic, ToastMakeTextRes);
      AddIntrinsicMethod(toast, "show", {}, "V", pub, ToastShow);
      AddIntrinsicMethod(toast, "cancel", {}, "V", pub, NoOp);
      AddIntrinsicMethod(toast, "setDuration", {"I"}, "Landroid/widget/Toast;", pub,
                         ToastSetDuration);
      AddIntrinsicMethod(toast, "setText", {"Ljava/lang/CharSequence;"}, "V", pub,
                         ToastSetText);
      AddIntrinsicMethod(toast, "setGravity", {"I", "I", "I"}, "V", pub, NoOp);
      AddIntrinsicMethod(toast, "setView", {"Landroid/view/View;"}, "V", pub, NoOp);
      AddIntrinsicMethod(toast, "getView", {}, "Landroid/view/View;", pub, ReturnNull);
      AddIntrinsicMethod(toast, "getDuration", {}, "I", pub, ToastGetDuration);
    }
    (void)view_group;
    VClass* linear_layout =
        MakeClass(linker, &all, "Landroid/widget/LinearLayout;", view_group, 200, kAccPublic);
    if (linear_layout != nullptr) {
      AddInstanceField(linear_layout, "mOrientation", "I", 184);
      AddInstanceField(linear_layout, "mGravity", "I", 192);
      AddIntrinsicMethod(linear_layout, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, "V", ctor,
                         ViewInit);
      AddIntrinsicMethod(linear_layout, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}, "V",
                         ctor, ViewInit);
      AddIntrinsicMethod(linear_layout, "setOrientation", {"I"}, "V", pub, LLSetOrientation);
      AddIntrinsicMethod(linear_layout, "getOrientation", {}, "I", pub, LLGetOrientation);
      AddIntrinsicMethod(linear_layout, "setGravity", {"I"}, "V", pub, LLSetGravity);
      AddIntrinsicMethod(linear_layout, "getGravity", {}, "I", pub, LLGetGravity);
      // LP 工厂（协变返回 LinearLayout$LayoutParams；api stub 兜底会导致
      // 子级 LP 类型错误 → AppBarLayout 等强转 CCE）。
      AddIntrinsicMethod(linear_layout, "generateLayoutParams",
                         {"Landroid/util/AttributeSet;"},
                         "Landroid/widget/LinearLayout$LayoutParams;", pub,
                         VGGenerateLayoutParams);
      AddIntrinsicMethod(linear_layout, "generateLayoutParams",
                         {"Landroid/view/ViewGroup$LayoutParams;"},
                         "Landroid/widget/LinearLayout$LayoutParams;", pub,
                         VGGenerateLayoutParams);
      AddIntrinsicMethod(linear_layout, "generateDefaultLayoutParams", {},
                         "Landroid/widget/LinearLayout$LayoutParams;", pub,
                         VGGenerateLayoutParams);
    }
    // LinearLayout$LayoutParams（+ gravity）
    if (linear_layout != nullptr && margin_params != nullptr) {
      VClass* ll_lp = MakeClass(linker, &all, "Landroid/widget/LinearLayout$LayoutParams;",
                                margin_params, 64, kAccPublic);
      if (ll_lp != nullptr) {
        AddInstanceField(ll_lp, "gravity", "I", 48);
        // AOSP LinearLayout.LayoutParams.weight（纵向/横向权重；abc 内容容器
        // weight=1 依赖此字段，缺失 = 内容高度 0）
        AddInstanceField(ll_lp, "weight", "F", 56);
        AddIntrinsicMethod(ll_lp, "<init>", {"I", "I"}, "V", ctor, LPInit);
        AddIntrinsicMethod(ll_lp, "<init>", {"Landroid/content/Context;",
                                             "Landroid/util/AttributeSet;"},
                           "V", ctor, LPInitAttrs);
        AddIntrinsicMethod(ll_lp, "<init>", {"I", "I", "F"}, "V", ctor, LPInit);
      }
    }

    // ---- 资源布局 widget 族（LayoutInflater/XML 面最小可执行骨架）----
    // 层级按 AOSP（Button/EditText extends TextView；Switch extends Button；
    // 容器 extends ViewGroup），保证资源布局里的类型转换语义成立。
    VClass* button = MakeClass(linker, &all, "Landroid/widget/Button;", text_view, 208, kAccPublic);
    if (button != nullptr) {
      AddIntrinsicMethod(button, "<init>", {"Landroid/content/Context;"}, "V", ctor, ButtonInit);
    }
    VClass* edit_text =
        MakeClass(linker, &all, "Landroid/widget/EditText;", text_view, 208, kAccPublic);
    if (edit_text != nullptr) {
      AddIntrinsicMethod(edit_text, "<init>", {"Landroid/content/Context;"}, "V", ctor,
                         EditTextInit);
    }
    VClass* switch_cls =
        MakeClass(linker, &all, "Landroid/widget/Switch;", button != nullptr ? button : text_view,
                  216, kAccPublic);
    if (switch_cls != nullptr) {
      AddInstanceField(switch_cls, "mChecked", "Z", 208);
      AddIntrinsicMethod(switch_cls, "<init>", {"Landroid/content/Context;"}, "V", ctor, TVInit);
      AddIntrinsicMethod(switch_cls, "setChecked", {"Z"}, "V", pub, SwitchSetChecked);
      AddIntrinsicMethod(switch_cls, "isChecked", {}, "Z", pub, SwitchIsChecked);
      AddIntrinsicMethod(switch_cls, "toggle", {}, "V", pub, NoOp);
    }
    VClass* progress_bar =
        MakeClass(linker, &all, "Landroid/widget/ProgressBar;", view, 184, kAccPublic);
    if (progress_bar != nullptr) {
      AddInstanceField(progress_bar, "mProgress", "I", 176);
      AddIntrinsicMethod(progress_bar, "<init>", {"Landroid/content/Context;"}, "V", ctor,
                         ViewInit);
      AddIntrinsicMethod(progress_bar, "setProgress", {"I"}, "V", pub, ProgressBarSetProgress);
      AddIntrinsicMethod(progress_bar, "getProgress", {}, "I", pub, ProgressBarGetProgress);
    }
    // android.widget.ImageView：lib 源（runtime-libs android/widget/ImageView.java，
    // C 档）提供类本体 + ImageView$ScaleType 枚举；不在此注册内建（lib 优先）。
    VClass* video_view =
        MakeClass(linker, &all, "Landroid/widget/VideoView;", view, 200, kAccPublic);
    if (video_view != nullptr) {
      // 宿主桥接出画面：路径/seek/播放态 -> bridge props（materializer 真播放）
      AddInstanceField(video_view, "mVideoPath", "Ljava/lang/String;", 176);
      AddInstanceField(video_view, "mVideoSeek", "I", 184);
      AddInstanceField(video_view, "mVideoPlaying", "Z", 192);
      AddIntrinsicMethod(video_view, "<init>", {"Landroid/content/Context;"}, "V", ctor, ViewInit);
      AddIntrinsicMethod(video_view, "setVideoPath", {"Ljava/lang/String;"}, "V", pub,
                         VideoViewSetVideoPath);
      AddIntrinsicMethod(video_view, "setVideoURI", {"Landroid/net/Uri;"}, "V", pub, NoOp);
      AddIntrinsicMethod(video_view, "start", {}, "V", pub, VideoViewStart);
      AddIntrinsicMethod(video_view, "stopPlayback", {}, "V", pub, VideoViewStop);
      AddIntrinsicMethod(video_view, "pause", {}, "V", pub, VideoViewStop);
      AddIntrinsicMethod(video_view, "stop", {}, "V", pub, VideoViewStop);
      AddIntrinsicMethod(video_view, "seekTo", {"I"}, "V", pub, VideoViewSeekTo);
      AddIntrinsicMethod(video_view, "isPlaying", {}, "Z", pub, VideoViewIsPlaying);
    }
    // android.widget.FrameLayout：lib 源（runtime-libs android/widget/FrameLayout.java，
    // C 档）提供类本体 + $LayoutParams；不在此注册内建（lib 优先，见 builtin_classes.def）。
    VClass* relative_layout =
        MakeClass(linker, &all, "Landroid/widget/RelativeLayout;", view_group, 184, kAccPublic);
    if (relative_layout != nullptr) {
      AddIntrinsicMethod(relative_layout, "<init>", {"Landroid/content/Context;"}, "V", ctor,
                         ViewInit);
      AddIntrinsicMethod(relative_layout, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, "V", ctor,
                         ViewInit);
      AddIntrinsicMethod(relative_layout, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}, "V",
                         ctor, ViewInit);
    }
    VClass* scroll_view = MakeClass(linker, &all, "Landroid/widget/ScrollView;",
                                    view_group, 184, kAccPublic);
    if (scroll_view != nullptr) {
      AddIntrinsicMethod(scroll_view, "<init>", {"Landroid/content/Context;"}, "V", ctor, ViewInit);
      AddIntrinsicMethod(scroll_view, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;"}, "V", ctor,
                         ViewInit);
      AddIntrinsicMethod(scroll_view, "<init>",
                         {"Landroid/content/Context;", "Landroid/util/AttributeSet;", "I"}, "V",
                         ctor, ViewInit);
    }
  }

  // ---- android.util.Log（stderr 落点） ----
  VClass* log = MakeClass(linker, &all, "Landroid/util/Log;", object, 0, kAccPublic | kAccFinal);
  if (log != nullptr) {
    AddIntrinsicMethod(log, "d", {"Ljava/lang/String;", "Ljava/lang/String;"}, "I", stat, LogD);
    AddIntrinsicMethod(log, "i", {"Ljava/lang/String;", "Ljava/lang/String;"}, "I", stat, LogI);
    AddIntrinsicMethod(log, "w", {"Ljava/lang/String;", "Ljava/lang/String;"}, "I", stat, LogW);
    AddIntrinsicMethod(log, "e", {"Ljava/lang/String;", "Ljava/lang/String;"}, "I", stat, LogE);
    AddIntrinsicMethod(log, "v", {"Ljava/lang/String;", "Ljava/lang/String;"}, "I", stat, LogV);
    AddIntrinsicMethod(log, "println", {"I", "Ljava/lang/String;", "Ljava/lang/String;"}, "I",
                       stat, LogI);
    AddIntrinsicMethod(log, "getStackTraceString", {"Ljava/lang/Throwable;"},
                       "Ljava/lang/String;", stat, LogGetStackTraceString);
  }

  // ---- android.os.Build / Build$VERSION（平台身份面；业务码 <clinit> 常读 SDK_INT）----
  // 值 = VM 宿主设备真实身份（emulator arm64，getprop 实测）；不伪造推断值。
  VClass* build = MakeClass(linker, &all, "Landroid/os/Build;", object, 0, kAccPublic);
  VClass* build_ver = MakeClass(linker, &all, "Landroid/os/Build$VERSION;", object, 0, kAccPublic);
  if (build != nullptr && build_ver != nullptr) {
    auto set_str = [&](VClass* k, const char* name, const char16_t* v) {
      for (auto& f : k->static_fields) {
        if (f.name == name) {
          Value slot;
          std::memset(&slot, 0, sizeof(slot));
          slot.ref = reinterpret_cast<ObjHeader*>(linker->AllocString(v));
          k->static_storage[f.offset] = slot;
          return;
        }
      }
    };
    auto set_int = [&](VClass* k, const char* name, int32_t v) {
      for (auto& f : k->static_fields) {
        if (f.name == name) {
          Value slot;
          std::memset(&slot, 0, sizeof(slot));
          slot.i = v;
          k->static_storage[f.offset] = slot;
          return;
        }
      }
    };
    const char* kBuildStrs[] = {"MANUFACTURER", "MODEL",     "BRAND",      "DEVICE",
                                "PRODUCT",      "HARDWARE",  "FINGERPRINT", "ID",
                                "DISPLAY",      "TYPE",      "TAGS",       "BOARD",
                                "BOOTLOADER",   "HOST",      "USER",       "SERIAL"};
    for (const char* n : kBuildStrs) AddStaticField(build, n, "Ljava/lang/String;");
    // ABI 数组面（Baidu Map/SoLoader 等 <clinit> 直接读 SUPPORTED_ABIS；
    // 缺失即 NoSuchFieldError → clinit 失败 → messagePump 中止）
    AddStaticField(build, "SUPPORTED_ABIS", "[Ljava/lang/String;");
    AddStaticField(build, "SUPPORTED_32_BIT_ABIS", "[Ljava/lang/String;");
    AddStaticField(build, "SUPPORTED_64_BIT_ABIS", "[Ljava/lang/String;");
    const char* kVerStrs[] = {"SDK", "RELEASE", "CODENAME", "INCREMENTAL", "SECURITY_PATCH",
                              "BASE_OS"};
    for (const char* n : kVerStrs) AddStaticField(build_ver, n, "Ljava/lang/String;");
    AddStaticField(build_ver, "SDK_INT", "I");
    AddStaticField(build_ver, "PREVIEW_SDK_INT", "I");
    set_str(build, "MANUFACTURER", u"Google");
    set_str(build, "MODEL", u"sdk_gphone64_arm64");
    set_str(build, "BRAND", u"google");
    set_str(build, "DEVICE", u"emu64a");
    set_str(build, "PRODUCT", u"sdk_gphone64_arm64");
    set_str(build, "HARDWARE", u"ranchu");
    set_str(build, "FINGERPRINT",
            u"google/sdk_gphone64_arm64/emu64a:17/CE2A.260420.051.A2/16418371:user/release-keys");
    set_str(build, "ID", u"CE2A.260420.051.A2");
    set_str(build, "DISPLAY", u"CE2A.260420.051.A2");
    set_str(build, "TYPE", u"user");
    set_str(build, "TAGS", u"release-keys");
    set_str(build, "BOARD", u"goldfish_arm64");
    set_str(build, "BOOTLOADER", u"unknown");
    set_str(build, "HOST", u"aacf82aa7fb9");
    set_str(build, "USER", u"android-build");
    set_str(build, "SERIAL", u"unknown");
    set_str(build_ver, "SDK", u"37");
    set_str(build_ver, "RELEASE", u"17");
    set_str(build_ver, "CODENAME", u"REL");
    set_str(build_ver, "INCREMENTAL", u"16418371");
    set_str(build_ver, "SECURITY_PATCH", u"2026-08-05");
    set_str(build_ver, "BASE_OS", u"");
    set_int(build_ver, "SDK_INT", 37);
    set_int(build_ver, "PREVIEW_SDK_INT", 0);
    auto set_strs = [&](VClass* k, const char* name,
                        std::initializer_list<const char16_t*> vals) {
      VClass* str_cls = linker->FindClass("Ljava/lang/String;");
      VClass* arr_cls = str_cls != nullptr ? linker->FindArrayClass(str_cls) : nullptr;
      if (arr_cls == nullptr) return;
      VArray* arr = linker->AllocArray(arr_cls, static_cast<int32_t>(vals.size()));
      if (arr == nullptr) return;
      int32_t i = 0;
      for (const char16_t* v : vals) {
        SetArrRef(arr, i, reinterpret_cast<ObjHeader*>(linker->AllocString(v)));
        i++;
      }
      for (auto& f : k->static_fields) {
        if (f.name == name) {
          Value slot;
          std::memset(&slot, 0, sizeof(slot));
          slot.ref = reinterpret_cast<ObjHeader*>(arr);
          k->static_storage[f.offset] = slot;
          return;
        }
      }
    };
    set_strs(build, "SUPPORTED_ABIS", {u"arm64-v8a", u"armeabi-v7a", u"armeabi"});
    set_strs(build, "SUPPORTED_32_BIT_ABIS", {u"armeabi-v7a", u"armeabi"});
    set_strs(build, "SUPPORTED_64_BIT_ABIS", {u"arm64-v8a"});
  }

  // ---- android.os 消息循环子系统（M4.3，docs 04 章 14.10 / 10 章 19.1）----
  // 布局与本 TU 顶部 kMsg*/kMq*/kLooper*/kHandler*/kHt* 常量一致。
  // 同步屏障（postSyncBarrier）后置；Message 已池化（recycleUnchecked 收池 /
  // obtain 复用，MAX_POOL_SIZE=50；flags@64 = AOSP IN_USE/ASYNCHRONOUS 位）。
  VClass* message = MakeClass(linker, &all, "Landroid/os/Message;", object, 80, pub);
  if (message != nullptr) {
    AddInstanceField(message, "what", "I", 0);
    AddInstanceField(message, "arg1", "I", 8);
    AddInstanceField(message, "arg2", "I", 16);
    AddInstanceField(message, "obj", "Ljava/lang/Object;", 24);
    AddInstanceField(message, "target", "Landroid/os/Handler;", 32);
    AddInstanceField(message, "callback", "Ljava/lang/Runnable;", 40);
    AddInstanceField(message, "when", "J", 48);
    AddInstanceField(message, "next", "Landroid/os/Message;", 56);
    AddInstanceField(message, "flags", "I", 64);
    AddInstanceField(message, "data", "Landroid/os/Bundle;", 72);
    AddIntrinsicMethod(message, "<init>", {}, "V", ctor, MsgInit);
    AddIntrinsicMethod(message, "obtain", {}, "Landroid/os/Message;", stat, MsgObtain0);
    AddIntrinsicMethod(message, "obtain", {"Landroid/os/Message;"}, "Landroid/os/Message;", stat,
                       MsgObtainOrig);
    AddIntrinsicMethod(message, "obtain", {"Landroid/os/Handler;"}, "Landroid/os/Message;", stat,
                       MsgObtainHandler);
    AddIntrinsicMethod(message, "obtain", {"Landroid/os/Handler;", "Ljava/lang/Runnable;"},
                       "Landroid/os/Message;", stat, MsgObtainHandlerRunnable);
    AddIntrinsicMethod(message, "obtain", {"Landroid/os/Handler;", "I"}, "Landroid/os/Message;",
                       stat, MsgObtainHandlerWhat);
    AddIntrinsicMethod(message, "obtain", {"Landroid/os/Handler;", "I", "Ljava/lang/Object;"},
                       "Landroid/os/Message;", stat, MsgObtainHandlerWhatObj);
    AddIntrinsicMethod(message, "obtain", {"Landroid/os/Handler;", "I", "I", "I"},
                       "Landroid/os/Message;", stat, MsgObtainHandlerWhatArg1Arg2);
    AddIntrinsicMethod(message,
                       "obtain", {"Landroid/os/Handler;", "I", "I", "I", "Ljava/lang/Object;"},
                       "Landroid/os/Message;", stat, MsgObtainHandlerWhatArg1Arg2Obj);
    AddIntrinsicMethod(message, "recycle", {}, "V", pub, MsgRecycle);
    AddIntrinsicMethod(message, "copyFrom", {"Landroid/os/Message;"}, "V", pub, MsgCopyFrom);
    AddIntrinsicMethod(message, "sendToTarget", {}, "V", pub, MsgSendToTarget);
    AddIntrinsicMethod(message, "getTarget", {}, "Landroid/os/Handler;", pub, MsgGetTarget);
    AddIntrinsicMethod(message, "setTarget", {"Landroid/os/Handler;"}, "V", pub, MsgSetTarget);
    AddIntrinsicMethod(message, "getWhen", {}, "J", pub, MsgGetWhen);
    AddIntrinsicMethod(message, "getCallback", {}, "Ljava/lang/Runnable;", pub, MsgGetCallback);
    AddIntrinsicMethod(message, "isAsynchronous", {}, "Z", pub, MsgIsAsynchronous);
    AddIntrinsicMethod(message, "setAsynchronous", {"Z"}, "V", pub, MsgSetAsynchronous);
    AddIntrinsicMethod(message, "getData", {}, "Landroid/os/Bundle;", pub, MsgGetData);
    AddIntrinsicMethod(message, "peekData", {}, "Landroid/os/Bundle;", pub, MsgPeekData);
    AddIntrinsicMethod(message, "setData", {"Landroid/os/Bundle;"}, "V", pub, MsgSetData);
  }

  // MessageQueue$IdleHandler（AOSP 嵌套接口）：queueIdle()Z 抽象面。
  // app 实现类（如 zhihu lifecycleProvider/a implements IdleHandler）链接
  // 依赖此接口定义；派发经 CallVirtual 名字面命中实现类 queueIdle。
  VClass* idle_handler =
      MakeClass(linker, &all, "Landroid/os/MessageQueue$IdleHandler;", object, 0,
                pub | kAccInterface | kAccAbstract);
  if (idle_handler != nullptr) {
    AddMethodSkeleton(idle_handler, "queueIdle", {}, "Z", pub | kAccAbstract);
  }

  VClass* mq = MakeClass(linker, &all, "Landroid/os/MessageQueue;", object, 32, pub);
  if (mq != nullptr) {
    AddInstanceField(mq, "mMessages", "Landroid/os/Message;", 0);
    AddInstanceField(mq, "mQuitting", "Z", 8);
    AddInstanceField(mq, "mIdleHandlers", "[Landroid/os/MessageQueue$IdleHandler;", 16);
    AddInstanceField(mq, "mIdleCount", "I", 24);
    AddIntrinsicMethod(mq, "<init>", {}, "V", ctor, MQInit);
    AddIntrinsicMethod(mq, "enqueueMessage", {"Landroid/os/Message;", "J"}, "Z", pub,
                       MQEnqueueMessage);
    AddIntrinsicMethod(mq, "next", {}, "Landroid/os/Message;", pub, MQNext);
    AddIntrinsicMethod(mq, "quit", {}, "V", pub, MQQuit);
    AddIntrinsicMethod(mq, "quitSafely", {}, "V", pub, MQQuitSafely);
    AddIntrinsicMethod(mq, "addIdleHandler",
                       {"Landroid/os/MessageQueue$IdleHandler;"}, "V", pub, MQAddIdleHandler);
    AddIntrinsicMethod(mq, "removeIdleHandler",
                       {"Landroid/os/MessageQueue$IdleHandler;"}, "V", pub, MQRemoveIdleHandler);
  }

  VClass* looper = MakeClass(linker, &all, "Landroid/os/Looper;", object, 16,
                             pub | kAccFinal);
  if (looper != nullptr) {
    AddInstanceField(looper, "mQueue", "Landroid/os/MessageQueue;", 0);
    AddInstanceField(looper, "mThread", "Ljava/lang/Thread;", 8);
    AddStaticField(looper, "sMainLooper", "Landroid/os/Looper;");
    AddIntrinsicMethod(looper, "prepare", {}, "V", stat, LooperPrepare);
    AddIntrinsicMethod(looper, "prepare", {"Z"}, "V", stat, LooperPrepare);
    AddIntrinsicMethod(looper, "prepareMainLooper", {}, "V", stat, LooperPrepareMain);
    AddIntrinsicMethod(looper, "loop", {}, "V", stat, LooperLoop);
    AddIntrinsicMethod(looper, "myLooper", {}, "Landroid/os/Looper;", stat, LooperMyLooper);
    AddIntrinsicMethod(looper, "getMainLooper", {}, "Landroid/os/Looper;", stat,
                       LooperGetMainLooper);
    AddIntrinsicMethod(looper, "quit", {}, "V", pub, LooperQuit);
    AddIntrinsicMethod(looper, "quitSafely", {}, "V", pub, LooperQuitSafely);
    AddIntrinsicMethod(looper, "getThread", {}, "Ljava/lang/Thread;", pub, LooperGetThread);
    AddIntrinsicMethod(looper, "getQueue", {}, "Landroid/os/MessageQueue;", pub, LooperGetQueue);
    AddIntrinsicMethod(looper, "myQueue", {}, "Landroid/os/MessageQueue;", stat, LooperMyQueue);
  }

  MakeClass(linker, &all, "Landroid/os/Handler$Callback;", object, 0,
            pub | kAccInterface | kAccAbstract);
  VClass* handler = MakeClass(linker, &all, "Landroid/os/Handler;", object, 24, pub);
  if (handler != nullptr) {
    AddInstanceField(handler, "mLooper", "Landroid/os/Looper;", 0);
    AddInstanceField(handler, "mQueue", "Landroid/os/MessageQueue;", 8);
    AddInstanceField(handler, "mCallback", "Landroid/os/Handler$Callback;", 16);
    AddIntrinsicMethod(handler, "<init>", {}, "V", ctor, HandlerInit0);
    AddIntrinsicMethod(handler, "<init>", {"Landroid/os/Handler$Callback;"}, "V", ctor,
                       HandlerInitCallback);
    AddIntrinsicMethod(handler, "<init>", {"Landroid/os/Looper;"}, "V", ctor, HandlerInitLooper);
    AddIntrinsicMethod(handler, "<init>", {"Landroid/os/Looper;", "Landroid/os/Handler$Callback;"},
                       "V", ctor, HandlerInitLooperCallback);
    // createAsync 静态工厂（API 28+；androidx DefaultTaskExecutor 主线程投递面）。
    AddIntrinsicMethod(handler, "createAsync", {"Landroid/os/Looper;"}, "Landroid/os/Handler;",
                       stat, HandlerCreateAsync);
    AddIntrinsicMethod(handler, "createAsync",
                       {"Landroid/os/Looper;", "Landroid/os/Handler$Callback;"},
                       "Landroid/os/Handler;", stat, HandlerCreateAsyncCallback);
    // obtainMessage 全家族（AOSP public final；(I,I,Object) 任务面补全，防 dex 引用缺口）
    AddIntrinsicMethod(handler, "obtainMessage", {}, "Landroid/os/Message;", pub_final,
                       HandlerObtainMessage0);
    AddIntrinsicMethod(handler, "obtainMessage", {"I"}, "Landroid/os/Message;", pub_final,
                       HandlerObtainMessageWhat);
    AddIntrinsicMethod(handler, "obtainMessage", {"I", "Ljava/lang/Object;"},
                       "Landroid/os/Message;", pub_final, HandlerObtainMessageWhatObj);
    AddIntrinsicMethod(handler, "obtainMessage", {"I", "I", "I"}, "Landroid/os/Message;", pub_final,
                       HandlerObtainMessageWhatArg1Arg2);
    AddIntrinsicMethod(handler, "obtainMessage", {"I", "I", "Ljava/lang/Object;"},
                       "Landroid/os/Message;", pub_final, HandlerObtainMessageWhatArg1Obj);
    AddIntrinsicMethod(handler, "obtainMessage", {"I", "I", "I", "Ljava/lang/Object;"},
                       "Landroid/os/Message;", pub_final, HandlerObtainMessageWhatArg1Arg2Obj);
    AddIntrinsicMethod(handler, "post", {"Ljava/lang/Runnable;"}, "Z", pub, HandlerPost);
    AddIntrinsicMethod(handler, "postAtTime", {"Ljava/lang/Runnable;", "J"}, "Z", pub,
                       HandlerPostAtTime);
    AddIntrinsicMethod(handler, "postAtTime",
                       {"Ljava/lang/Runnable;", "Ljava/lang/Object;", "J"}, "Z", pub,
                       HandlerPostAtTime);
    AddIntrinsicMethod(handler, "postDelayed", {"Ljava/lang/Runnable;", "J"}, "Z", pub,
                       HandlerPostDelayed);
    AddIntrinsicMethod(handler, "postDelayed",
                       {"Ljava/lang/Runnable;", "Ljava/lang/Object;", "J"}, "Z", pub,
                       HandlerPostDelayed);
    AddIntrinsicMethod(handler, "postAtFrontOfQueue", {"Ljava/lang/Runnable;"}, "Z", pub,
                       HandlerPostAtFrontOfQueue);
    AddIntrinsicMethod(handler, "sendMessage", {"Landroid/os/Message;"}, "Z", pub,
                       HandlerSendMessage);
    AddIntrinsicMethod(handler, "sendMessageDelayed", {"Landroid/os/Message;", "J"}, "Z", pub,
                       HandlerSendMessageDelayed);
    AddIntrinsicMethod(handler, "sendMessageAtTime", {"Landroid/os/Message;", "J"}, "Z", pub,
                       HandlerSendMessageAtTime);
    AddIntrinsicMethod(handler, "sendMessageAtFrontOfQueue", {"Landroid/os/Message;"}, "Z", pub,
                       HandlerSendMessageAtFrontOfQueue);
    AddIntrinsicMethod(handler, "sendEmptyMessage", {"I"}, "Z", pub, HandlerSendEmptyMessage);
    AddIntrinsicMethod(handler, "sendEmptyMessageDelayed", {"I", "J"}, "Z", pub,
                       HandlerSendEmptyMessage);
    AddIntrinsicMethod(handler, "sendEmptyMessageAtTime", {"I", "J"}, "Z", pub,
                       HandlerSendEmptyMessageAtTime);
    AddIntrinsicMethod(handler, "handleMessage", {"Landroid/os/Message;"}, "V", pub,
                       HandlerHandleMessage);
    AddIntrinsicMethod(handler, "dispatchMessage", {"Landroid/os/Message;"}, "V", pub,
                       HandlerDispatchMessage);
    AddIntrinsicMethod(handler, "getLooper", {}, "Landroid/os/Looper;", pub, HandlerGetLooper);
    AddIntrinsicMethod(handler, "removeCallbacks", {"Ljava/lang/Runnable;"}, "V", pub,
                       HandlerRemoveCallbacks);
    AddIntrinsicMethod(handler, "removeCallbacks", {"Ljava/lang/Runnable;", "Ljava/lang/Object;"},
                       "V", pub, HandlerRemoveCallbacks);
    AddIntrinsicMethod(handler, "removeMessages", {"I"}, "V", pub, HandlerRemoveMessages);
    AddIntrinsicMethod(handler, "removeMessages", {"I", "Ljava/lang/Object;"}, "V", pub,
                       HandlerRemoveMessages);
    AddIntrinsicMethod(handler, "removeCallbacksAndMessages", {"Ljava/lang/Object;"}, "V", pub,
                       HandlerRemoveCallbacksAndMessages);
    AddIntrinsicMethod(handler, "hasMessages", {"I"}, "Z", pub, HandlerHasMessages);
    AddIntrinsicMethod(handler, "hasMessages", {"I", "Ljava/lang/Object;"}, "Z", pub,
                       HandlerHasMessages);
    AddIntrinsicMethod(handler, "hasCallbacks", {"Ljava/lang/Runnable;"}, "Z", pub,
                       HandlerHasCallbacks);
  }

  // HandlerThread（extends Thread；start = 单线程投影建 Looper，见 HTStart 注释）
  // 实例布局：Thread 104 字节 + mLooper@104（M4.4 Thread 扩槽后移）
  VClass* thread_cls = linker->FindClass("Ljava/lang/Thread;");
  VClass* ht =
      thread_cls != nullptr
          ? MakeClass(linker, &all, "Landroid/os/HandlerThread;", thread_cls, 112, pub)
          : nullptr;
  if (ht != nullptr) {
    AddInstanceField(ht, "mLooper", "Landroid/os/Looper;", 104);
    AddIntrinsicMethod(ht, "<init>", {"Ljava/lang/String;"}, "V", ctor, HTInit);
    AddIntrinsicMethod(ht, "<init>", {"Ljava/lang/String;", "I"}, "V", ctor, HTInit);
    AddIntrinsicMethod(ht, "start", {}, "V", pub, HTStart);
    AddIntrinsicMethod(ht, "run", {}, "V", pub, HTRun);
    AddIntrinsicMethod(ht, "getLooper", {}, "Landroid/os/Looper;", pub, HTGetLooper);
    AddIntrinsicMethod(ht, "quit", {}, "Z", pub, HTQuit);
    AddIntrinsicMethod(ht, "quitSafely", {}, "Z", pub, HTQuitSafely);
    (void)thread_cls;
  }

  // android.os.Process / UserHandle（虚拟身份：真实 pid/uid，docs 29.2① 同源）
  VClass* process = MakeClass(linker, &all, "Landroid/os/Process;", object, 0,
                              pub | kAccFinal);
  if (process != nullptr) {
    AddIntrinsicMethod(process, "myPid", {}, "I", stat, ProcessMyPid);
    AddIntrinsicMethod(process, "myTid", {}, "I", stat, ProcessMyTid);
    AddIntrinsicMethod(process, "myUid", {}, "I", stat, ProcessMyUid);
    AddIntrinsicMethod(process, "myUserHandle", {}, "Landroid/os/UserHandle;", stat,
                       ProcessMyUserHandle);
    // 线程优先级面：协程调度优先级由 GuestTask 承担（主线程 P1/后台 P2），
    // guest setThreadPriority 为语义可调用面（不映射 OS nice）。
    AddIntrinsicMethod(process, "setThreadPriority", {"I", "I"}, "V", stat, NoOp);
    AddIntrinsicMethod(process, "setThreadPriority", {"I"}, "V", stat, NoOp);
    AddIntrinsicMethod(process, "getThreadPriority", {"I"}, "I", stat, ReturnZeroInt);
    (void)process;
  }
  VClass* uh = MakeClass(linker, &all, "Landroid/os/UserHandle;", object, 8, pub);
  if (uh != nullptr) {
    AddInstanceField(uh, "mHandle", "I", 0);
    AddMethodSkeleton(uh, "<init>", {}, "V", ctor);
  }

  // android.os.SystemClock（uptimeMillis=真实单调时钟；elapsedRealtime=墙钟投影）
  VClass* sys_clock = MakeClass(linker, &all, "Landroid/os/SystemClock;", object, 0,
                                pub | kAccFinal);
  if (sys_clock != nullptr) {
    AddIntrinsicMethod(sys_clock, "uptimeMillis", {}, "J", stat, SCUptimeMillis);
    AddIntrinsicMethod(sys_clock, "elapsedRealtime", {}, "J", stat, SCElapsedRealtime);
    AddIntrinsicMethod(sys_clock, "elapsedRealtimeNanos", {}, "J", stat, SCElapsedRealtimeNanos);
    AddIntrinsicMethod(sys_clock, "sleep", {"J"}, "V", stat, SCSleep);
  }

  // android.system.Os / ErrnoException（考卷链 Os.readlink 探测面；真实 syscall）
  VClass* os_cls = MakeClass(linker, &all, "Landroid/system/Os;", object, 0, pub | kAccFinal);
  if (os_cls != nullptr) {
    AddIntrinsicMethod(os_cls, "readlink", {"Ljava/lang/String;"}, "Ljava/lang/String;", stat,
                       OsReadlink);
    AddIntrinsicMethod(os_cls, "getpid", {}, "I", stat, OsMyPid);
    AddIntrinsicMethod(os_cls, "getuid", {}, "I", stat, OsMyUid);
    AddIntrinsicMethod(os_cls, "geteuid", {}, "I", stat, OsMyUid);
    AddIntrinsicMethod(os_cls, "gettid", {}, "I", stat, OsMyPid);
    AddIntrinsicMethod(os_cls, "getppid", {}, "I", stat, OsMyPid);
  }
  VClass* errno_ex = MakeClass(linker, &all, "Landroid/system/ErrnoException;",
                               linker->FindClass("Ljava/lang/Exception;"), 32, pub);
  if (errno_ex != nullptr) {
    // Throwable 布局：detailMessage@0 + cause@8（异常壳最小 <init> 面）→ 本类字段续排
    AddInstanceField(errno_ex, "functionName", "Ljava/lang/String;", 16);
    AddInstanceField(errno_ex, "errno", "I", 24);
    AddIntrinsicMethod(errno_ex, "<init>", {"Ljava/lang/String;", "I"}, "V", ctor,
                       ErrnoExceptionInit);
  }

  // Fragment 族（android.app.Fragment/FragmentManager/FragmentTransaction；
  // androidx lifecycle ReportFragment 等以 android.app.Fragment 为基类）
  RegisterFragmentBuiltins(linker, &all, activity);
  // android.net.* 连接性面（ConnectivityManager/NetworkInfo/Wifi*；宿主在线
  // 投影——TLS 上游网络初始化依赖，见 framework_net.cc）
  RegisterNetworkBuiltins(linker, &all);

  for (VClass* k : all) {
    if (k != nullptr && class_class != nullptr) k->klass = class_class;
    linker->RegisterBuiltinClass(k);
  }
  // 内建注册集 == 派生集（builtin_classes.def）一致性断言（防再犯核心）
  AssertBuiltinRegistration(all, FrameworkBuiltinClassManifest(), "RegisterFrameworkBuiltins");

  // 内建类首遍补桩（lib 类第二遍在 boot dex 装载后 ApplyFrameworkApiStubs）。
  ApplyApiStubs(linker, /*dex_classes_only=*/false);
}

bool PumpLooperQueues(VMState& state, std::string* error) {
  return PumpLooperQueuesCount(state, error, nullptr);
}

bool PumpLooperQueuesCount(VMState& state, std::string* error, size_t* dispatched) {
  std::string local;
  if (PumpQueues(state, &local, false, dispatched)) return true;
  // dispatch 抛出的 guest 异常 -> 诊断串（"descriptor: message"），pending 清除。
  std::string diag = local;
  if (state.HasPendingException()) {
    ObjHeader* ex = state.pending_exception;
    std::string desc =
        ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor : "Ljava/lang/Throwable;";
    std::string msg;
    if (ex != nullptr && ex->klass != nullptr) {
      VObject* obj = reinterpret_cast<VObject*>(ex);
      for (VClass* k = obj->klass; k != nullptr && msg.empty(); k = k->super_class) {
        for (const auto& f : k->instance_fields) {
          if (f.name == "detailMessage") {
            ObjHeader* s = GetRefField(obj, f.offset);
            if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
            break;
          }
        }
      }
    }
    diag = "guest exception " + desc + ": " + msg;
    state.ClearException();
  }
  if (error != nullptr) *error = diag;
  return false;
}

// 实时语义泵（常驻会话用；postDelayed 不虚拟推进，防应用重试自投递洪水）
bool PumpLooperQueuesRealtime(VMState& state, std::string* error, size_t* dispatched) {
  std::string local;
  if (PumpQueuesRealtime(state, &local, dispatched)) return true;
  std::string diag = local;
  if (state.HasPendingException()) {
    ObjHeader* ex = state.pending_exception;
    std::string desc =
        ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor : "Ljava/lang/Throwable;";
    std::string msg;
    if (ex != nullptr && ex->klass != nullptr) {
      VObject* obj = reinterpret_cast<VObject*>(ex);
      for (VClass* k = obj->klass; k != nullptr && msg.empty(); k = k->super_class) {
        for (const auto& f : k->instance_fields) {
          if (f.name == "detailMessage") {
            ObjHeader* s = GetRefField(obj, f.offset);
            if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
            break;
          }
        }
      }
    }
    diag = "guest exception " + desc + (msg.empty() ? "" : ": " + msg);
#if defined(__ANDROID__)
    __android_log_print(ANDROID_LOG_INFO, "DexVmPump", "%s", diag.c_str());
    for (size_t i = 0; i < state.last_throw_backtrace.size() && i < 12; i++) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmPump", "  at %s",
                          state.last_throw_backtrace[i].c_str());
    }
#endif
    state.ClearException();
  }
  if (error != nullptr) *error = diag;
  return false;
}

bool PumpAllLooperQueues(VMState& state, std::string* error) {
  std::string local;
  if (PumpQueues(state, &local, /*all_loopers=*/true)) return true;
  std::string diag = local;
  if (state.HasPendingException()) {
    ObjHeader* ex = state.pending_exception;
    std::string desc =
        ex != nullptr && ex->klass != nullptr ? ex->klass->descriptor : "Ljava/lang/Throwable;";
    std::string msg;
    if (ex != nullptr && ex->klass != nullptr) {
      VObject* obj = reinterpret_cast<VObject*>(ex);
      for (VClass* k = obj->klass; k != nullptr && msg.empty(); k = k->super_class) {
        for (const auto& f : k->instance_fields) {
          if (f.name == "detailMessage") {
            ObjHeader* s = GetRefField(obj, f.offset);
            if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
            break;
          }
        }
      }
    }
    diag = "guest exception " + desc + ": " + msg;
    state.ClearException();
  }
  if (error != nullptr) *error = diag;
  return false;
}

}  // namespace dexvm::runtime
