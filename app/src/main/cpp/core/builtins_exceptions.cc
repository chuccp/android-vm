// 异常壳语义面（Throwable 构造/消息/抛出面/栈/抑制/printStackTrace）。
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

namespace dexvm::runtime {

// ---- Throwable 构造器/消息面（detailMessage 布局，docs 02 章 6.2）----

// 抛出面栈捕获（fillInStackTrace 语义；实现见本文件后部「Throwable 抛出面」节）
void ThrowableCaptureStack(VMState& state, VObject* self);

Value ThrowableInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  // OpenJDK：<init> 链隐式 fillInStackTrace（栈快照捕获）
  ThrowableCaptureStack(state, self);
  (void)ok;
  return Value{};  // detailMessage 保持 null
}

Value ThrowableInitMsg(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // java.lang.Throwable.<init>(String)：detailMessage = message（可空）
  ThrowableCaptureStack(state, self);
  VField* f = FindDetailMessage(self->klass);
  if (f != nullptr) {
    ObjHeader* msg = (n >= 1 && args[0].ref != nullptr) ? args[0].ref : nullptr;
    std::memcpy(self->field_data() + f->offset, &msg, sizeof(msg));
  }
  return Value{};
}

// Throwable.cause 字段查找（沿继承链；与 detailMessage 同法）
namespace {
VField* FindCauseField(VClass* k) {
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& f : c->instance_fields) {
      if (f.name == "cause") return &f;
    }
  }
  return nullptr;
}

}  // namespace
Value ThrowableInitCause(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // java.lang.Throwable.<init>(Throwable)：cause = t、detailMessage = t.toString()
  // （AOSP/Java 口径：message 缺省为 cause 的字符串形——被包装异常诊断可见根因）
  (void)ok;
  ThrowableCaptureStack(state, self);
  VField* cf = FindCauseField(self->klass);
  if (cf != nullptr) {
    ObjHeader* c = (n >= 1) ? args[0].ref : nullptr;
    std::memcpy(self->field_data() + cf->offset, &c, sizeof(c));
  }
  VField* f = FindDetailMessage(self->klass);
  if (f != nullptr && n >= 1 && args[0].ref != nullptr) {
    VObject* cause = reinterpret_cast<VObject*>(args[0].ref);
    // cause.toString() 手写形（Throwable.toString：类名[: message]）
    std::string desc = cause->klass != nullptr ? cause->klass->descriptor : "Ljava/lang/Throwable;";
    std::string name = desc;
    if (!name.empty() && name[0] == 'L') {
      name = name.substr(1);
      if (!name.empty() && name.back() == ';') name.pop_back();
      for (char& ch : name)
        if (ch == '/') ch = '.';
    }
    VField* cfm = FindDetailMessage(cause->klass);
    ObjHeader* cm = nullptr;
    if (cfm != nullptr) std::memcpy(&cm, cause->field_data() + cfm->offset, sizeof(cm));
    if (cm != nullptr) {
      name += ": ";
      name += static_cast<VString*>(cm)->ToModifiedUtf8();
    }
    ObjHeader* msg = reinterpret_cast<ObjHeader*>(
        state.linker->AllocString(std::u16string(name.begin(), name.end())));
    std::memcpy(self->field_data() + f->offset, &msg, sizeof(msg));
  }
  return Value{};
}

Value AssertionErrorInitObject(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  // OpenJDK/libcore AssertionError(Object)：
  //   detailMessage = String.valueOf(detailMessage)；若实参是 Throwable 则 initCause 之。
  // 走 assert 面最常见的实参是被包装异常（okhttp NamedRunnable 等）。
  ThrowableCaptureStack(state, self);
  VObject* value = (n >= 1) ? reinterpret_cast<VObject*>(args[0].ref) : nullptr;
  bool value_is_throwable = false;
  for (VClass* c = value != nullptr ? value->klass : nullptr; c != nullptr;
       c = c->super_class) {
    if (c->descriptor == "Ljava/lang/Throwable;") {
      value_is_throwable = true;
      break;
    }
  }
  VField* f = FindDetailMessage(self->klass);
  if (f != nullptr) {
    std::string text;
    if (value == nullptr) {
      text = "null";
    } else if (value_is_throwable) {
      // Throwable.toString 形：类名[: message]
      std::string name = value->klass->descriptor;
      if (!name.empty() && name[0] == 'L') {
        name = name.substr(1);
        if (!name.empty() && name.back() == ';') name.pop_back();
        for (char& ch : name)
          if (ch == '/') ch = '.';
      }
      VField* vfm = FindDetailMessage(value->klass);
      ObjHeader* vm = nullptr;
      if (vfm != nullptr) std::memcpy(&vm, value->field_data() + vfm->offset, sizeof(vm));
      if (vm != nullptr) {
        name += ": ";
        name += static_cast<VString*>(vm)->ToModifiedUtf8();
      }
      text = name;
    } else {
      // 非 Throwable：Object.toString 粗略形（诊断面足够）
      std::string name = value->klass != nullptr ? value->klass->descriptor : "Ljava/lang/Object;";
      if (!name.empty() && name[0] == 'L') {
        name = name.substr(1);
        if (!name.empty() && name.back() == ';') name.pop_back();
        for (char& ch : name)
          if (ch == '/') ch = '.';
      }
      text = name;
    }
    ObjHeader* msg = reinterpret_cast<ObjHeader*>(
        state.linker->AllocString(std::u16string(text.begin(), text.end())));
    std::memcpy(self->field_data() + f->offset, &msg, sizeof(msg));
  }
  if (value_is_throwable) {
    VField* cf = FindCauseField(self->klass);
    if (cf != nullptr) {
      ObjHeader* c = reinterpret_cast<ObjHeader*>(value);
      std::memcpy(self->field_data() + cf->offset, &c, sizeof(c));
    }
  }
  (void)state;
  (void)ok;
  return Value{};
}

Value ThrowableInitMsgCause(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  // <init>(String, Throwable)：detailMessage = msg、cause = t（lib 异常壳构造链落点）
  ThrowableCaptureStack(state, self);
  VField* f = FindDetailMessage(self->klass);
  if (f != nullptr) {
    ObjHeader* msg = (n >= 1 && args[0].ref != nullptr) ? args[0].ref : nullptr;
    std::memcpy(self->field_data() + f->offset, &msg, sizeof(msg));
  }
  VField* cf = FindCauseField(self->klass);
  if (cf != nullptr) {
    ObjHeader* c = (n >= 2) ? args[1].ref : nullptr;
    std::memcpy(self->field_data() + cf->offset, &c, sizeof(c));
  }
  return Value{};
}

Value ThrowableGetCause(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  Value r{};
  VField* f = FindCauseField(self->klass);
  if (f != nullptr) {
    ObjHeader* c = nullptr;
    std::memcpy(&c, self->field_data() + f->offset, sizeof(c));
    r.ref = c;
  }
  return r;
}

Value ThrowableInitCauseMethod(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  // initCause(Throwable)：cause 置值（已置抛 IllegalStateException——最小面：覆盖置值）
  (void)state;
  VField* f = FindCauseField(self->klass);
  if (f != nullptr) {
    ObjHeader* c = (n >= 1) ? args[0].ref : nullptr;
    std::memcpy(self->field_data() + f->offset, &c, sizeof(c));
  }
  Value r{};
  r.ref = self;
  return r;
}

Value ThrowableGetMessage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  Value r{};
  VField* f = self->klass != nullptr ? FindDetailMessage(self->klass) : nullptr;
  if (f != nullptr) {
    ObjHeader* msg = nullptr;
    std::memcpy(&msg, self->field_data() + f->offset, sizeof(msg));
    r.ref = msg;
  }
  return r;
}

Value ThrowableGetLocalizedMessage(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  return ThrowableGetMessage(state, self, args, n, ok);
}

Value ThrowableToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  std::string name = ClassName(self->klass);
  std::u16string msg;
  VField* f = self->klass != nullptr ? FindDetailMessage(self->klass) : nullptr;
  if (f != nullptr) {
    ObjHeader* m = nullptr;
    std::memcpy(&m, self->field_data() + f->offset, sizeof(m));
    if (m != nullptr) msg = CopyChars(AsString(m));
  }
  std::u16string out;
  AppendUtf16(&out, name.c_str());
  if (!msg.empty()) {
    out.push_back(u':');
    out.push_back(u' ');
    out += msg;
  }
  return MakeStr(state, out, ok);
}

Value ThrowableFillInStackTrace(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)args; (void)n; (void)ok;
  ThrowableCaptureStack(state, self);
  Value r{};
  r.ref = self;
  return r;
}

// ---- Throwable 抛出面补全（栈/抑制/printStackTrace；A 档异常壳，§31.4）----
// 栈语义（OpenJDK fillInStackTrace 同构）：构造链隐式捕获 guest 栈快照到
// stackTrace 隐藏槽；getStackTrace 返回克隆（防御性拷贝）；行号来源是 dex_pc
// 而非行号表 → lineNumber = -1（Unknown Source 口径，不伪造行号）。帧文本
// 对齐 Java 面（"at pkg.Cls.method(File:line)" / "Caused by:" / "Suppressed:"；
// 不做 "... N more" 帧折叠——全量帧诊断不丢语义）。

namespace {
void SetRefSlot(VObject* o, VClass* k, const char* name, ObjHeader* v) {
  VField* f = FindInstField(k, name);
  if (f != nullptr) std::memcpy(o->field_data() + f->offset, &v, sizeof(v));
}

}  // namespace
ObjHeader* GetRefSlot(VObject* o, VClass* k, const char* name) {
  VField* f = FindInstField(k, name);
  if (f == nullptr) return nullptr;
  ObjHeader* v = nullptr;
  std::memcpy(&v, o->field_data() + f->offset, sizeof(v));
  return v;
}

namespace {
int32_t GetIntSlot(VObject* o, VClass* k, const char* name) {
  VField* f = FindInstField(k, name);
  if (f == nullptr) return 0;
  uint64_t slot = 0;
  std::memcpy(&slot, o->field_data() + f->offset, sizeof(slot));
  return static_cast<int32_t>(slot);  // Narrow::kWord 低 32
}

}  // namespace
VObject* MakeStackTraceElement(VMState& state, VClass* ste_cls, const std::string& cls,
                              const std::string& method, bool* ok) {
  VObject* ste = state.linker->AllocObject(ste_cls);
  if (ste == nullptr) {
    ThrowOOM(state, "StackTraceElement");
    *ok = false;
    return nullptr;
  }
  Value cv{};
  std::u16string tmp;
  AppendUtf16(&tmp, cls.c_str());
  cv = MakeStr(state, tmp, ok);
  if (!*ok) return nullptr;
  SetRefSlot(ste, ste_cls, "declaringClass", cv.ref);
  tmp.clear();
  AppendUtf16(&tmp, method.c_str());
  cv = MakeStr(state, tmp, ok);
  if (!*ok) return nullptr;
  SetRefSlot(ste, ste_cls, "methodName", cv.ref);
  SetRefSlot(ste, ste_cls, "fileName", nullptr);  // 行号表未落 → 未知源
  VField* line = FindInstField(ste_cls, "lineNumber");
  if (line != nullptr) {
    uint64_t slot = static_cast<uint64_t>(static_cast<int64_t>(-1));  // Unknown Source
    std::memcpy(ste->field_data() + line->offset, &slot, sizeof(slot));
  }
  return ste;
}

void ThrowableCaptureStack(VMState& state, VObject* self) {
  if (self == nullptr || self->klass == nullptr) return;
  VClass* arr_k = state.linker->FindClass("[Ljava/lang/StackTraceElement;");
  VClass* ste_cls = state.linker->FindClass("Ljava/lang/StackTraceElement;");
  if (arr_k == nullptr || ste_cls == nullptr) {
    state.ClearException();
    return;  // 栈面不可用时静默降级（不阻断抛出路径）
  }
  size_t count = 0;
  for (const Frame* f : state.frame_stack) {
    if (f != nullptr && f->method != nullptr) count++;
    if (count >= 32) break;  // 与 CaptureThrowBacktrace 同上限
  }
  VArray* arr = state.linker->AllocArray(arr_k, static_cast<int32_t>(count));
  if (arr == nullptr) {
    state.ClearException();
    return;
  }
  int32_t i = 0;
  for (auto it = state.frame_stack.rbegin(); it != state.frame_stack.rend(); ++it) {
    if (i >= static_cast<int32_t>(count)) break;
    const Frame* f = *it;
    if (f == nullptr || f->method == nullptr) continue;
    const VMethod* m = f->method;
    std::string cls = m->declaring != nullptr ? ClassName(m->declaring) : "?";
    bool ok = true;
    VObject* ste = MakeStackTraceElement(state, ste_cls, cls, m->name, &ok);
    if (ste == nullptr) {
      state.ClearException();
      break;
    }
    ObjHeader* slot = ste;
    std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &slot, sizeof(slot));
    i++;
  }
  SetRefSlot(self, self->klass, "stackTrace", arr);
}

// Thread.getStackTrace()（A 档 Thread 接缝面；OpenJDK 语义）：当前线程 = live
// guest 栈快照（innermost 先，与 ThrowableCaptureStack 同口径/同上限 32）；
Value ThrowableGetStackTrace(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)args; (void)n;
  VClass* arr_k = state.linker->FindClass("[Ljava/lang/StackTraceElement;");
  if (arr_k == nullptr) {
    *ok = false;
    return Value{};
  }
  VArray* stored = reinterpret_cast<VArray*>(
      GetRefSlot(self, self->klass != nullptr ? self->klass : arr_k, "stackTrace"));
  int32_t len = stored != nullptr ? stored->length : 0;
  VArray* copy = state.linker->AllocArray(arr_k, len);  // OpenJDK：返回克隆
  if (copy == nullptr) {
    ThrowOOM(state, "getStackTrace");
    *ok = false;
    return Value{};
  }
  if (stored != nullptr && len > 0) {
    std::memcpy(copy->data(), stored->data(), static_cast<size_t>(len) * 8);
  }
  Value r{};
  r.ref = copy;
  return r;
}

Value ThrowableSetStackTrace(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "setStackTrace");
    *ok = false;
    return Value{};
  }
  VArray* in = reinterpret_cast<VArray*>(args[0].ref);
  VClass* arr_k = state.linker->FindClass("[Ljava/lang/StackTraceElement;");
  if (arr_k == nullptr) {
    *ok = false;
    return Value{};
  }
  VArray* copy = state.linker->AllocArray(arr_k, in->length);
  if (copy == nullptr) {
    ThrowOOM(state, "setStackTrace");
    *ok = false;
    return Value{};
  }
  std::memcpy(copy->data(), in->data(), static_cast<size_t>(in->length) * 8);
  SetRefSlot(self, self->klass, "stackTrace", copy);
  return Value{};
}

Value ThrowableAddSuppressed(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "addSuppressed");
    *ok = false;
    return Value{};
  }
  ObjHeader* ex = args[0].ref;
  if (ex == self) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Self-suppression not permitted");
    *ok = false;
    return Value{};
  }
  VClass* arr_k = state.linker->FindClass("[Ljava/lang/Throwable;");
  if (arr_k == nullptr) {
    *ok = false;
    return Value{};
  }
  VArray* cur = reinterpret_cast<VArray*>(
      GetRefSlot(self, self->klass != nullptr ? self->klass : arr_k, "suppressedExceptions"));
  int32_t len = cur != nullptr ? cur->length : 0;
  VArray* grown = state.linker->AllocArray(arr_k, len + 1);
  if (grown == nullptr) {
    ThrowOOM(state, "addSuppressed");
    *ok = false;
    return Value{};
  }
  if (cur != nullptr && len > 0) {
    std::memcpy(grown->data(), cur->data(), static_cast<size_t>(len) * 8);
  }
  std::memcpy(grown->data() + static_cast<size_t>(len) * 8, &ex, sizeof(ex));
  SetRefSlot(self, self->klass, "suppressedExceptions", grown);
  return Value{};
}

Value ThrowableGetSuppressed(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)args; (void)n;
  VClass* arr_k = state.linker->FindClass("[Ljava/lang/Throwable;");
  if (arr_k == nullptr) {
    *ok = false;
    return Value{};
  }
  VArray* cur = reinterpret_cast<VArray*>(
      GetRefSlot(self, self->klass != nullptr ? self->klass : arr_k, "suppressedExceptions"));
  int32_t len = cur != nullptr ? cur->length : 0;
  VArray* copy = state.linker->AllocArray(arr_k, len);
  if (copy == nullptr) {
    ThrowOOM(state, "getSuppressed");
    *ok = false;
    return Value{};
  }
  if (cur != nullptr && len > 0) {
    std::memcpy(copy->data(), cur->data(), static_cast<size_t>(len) * 8);
  }
  Value r{};
  r.ref = copy;
  return r;
}

// ---- printStackTrace 族（println(String) 派发到 PrintStream/PrintWriter）----

namespace {
std::string ThrowableFrameLine(VObject* ste) {
  VClass* k = ste->klass;
  std::string cls_obj = "?";
  std::string method = "?";
  std::string file;
  ObjHeader* cls_s = GetRefSlot(ste, k, "declaringClass");
  if (cls_s != nullptr) cls_obj = AsString(cls_s)->ToModifiedUtf8();
  ObjHeader* m_s = GetRefSlot(ste, k, "methodName");
  if (m_s != nullptr) method = AsString(m_s)->ToModifiedUtf8();
  ObjHeader* f_s = GetRefSlot(ste, k, "fileName");
  if (f_s != nullptr) file = AsString(f_s)->ToModifiedUtf8();
  int32_t line = GetIntSlot(ste, k, "lineNumber");
  std::string loc;
  if (line == -2) {
    loc = "(Native Method)";
  } else if (file.empty()) {
    loc = "(Unknown Source)";
  } else if (line >= 0) {
    loc = "(" + file + ":" + std::to_string(line) + ")";
  } else {
    loc = "(" + file + ")";
  }
  return "\tat " + cls_obj + "." + method + loc;
}

bool ThrowablePrintLine(VMState& state, VObject* target, const std::string& line, bool* ok) {
  if (target == nullptr || target->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "printStackTrace stream");
    *ok = false;
    return false;
  }
  VMethod* println = LookupDeclaredMethod(target->klass, "println", {"Ljava/lang/String;"},
                                          true);
  if (println == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", "println(String) on " +
                    target->klass->descriptor);
    *ok = false;
    return false;
  }
  Value a[2];
  a[0].ref = target;
  std::u16string tmp;
  AppendUtf16(&tmp, line.c_str());
  a[1] = MakeStr(state, tmp, ok);
  if (!*ok) return false;
  Value r{};
  return InvokeMethod(state, println, a, 2, &r);
}

}  // namespace
bool ThrowablePrintRec(VMState& state, VObject* self, VObject* target,
                       std::vector<VObject*>* seen, const char* prefix, bool* ok);

namespace {
bool ThrowablePrintBody(VMState& state, VObject* self, VObject* target,
                        std::vector<VObject*>* seen, const char* prefix, bool* ok) {
  std::string head = prefix;
  Value ts = ThrowableToString(state, self, nullptr, 0, ok);
  if (!*ok) return false;
  head += ts.ref != nullptr ? AsString(ts.ref)->ToModifiedUtf8() : "?";
  if (!ThrowablePrintLine(state, target, head, ok)) return false;
  Value st = ThrowableGetStackTrace(state, self, nullptr, 0, ok);
  if (!*ok) return false;
  VArray* frames = reinterpret_cast<VArray*>(st.ref);
  for (int32_t i = 0; frames != nullptr && i < frames->length; i++) {
    ObjHeader* ste = nullptr;
    std::memcpy(&ste, frames->data() + static_cast<size_t>(i) * 8, sizeof(ste));
    if (ste == nullptr) continue;
    if (!ThrowablePrintLine(state, target, ThrowableFrameLine(static_cast<VObject*>(ste)), ok)) {
      return false;
    }
  }
  // 抑制分支（OpenJDK："Suppressed: " 前缀，随宿主缩进）
  VArray* sup = reinterpret_cast<VArray*>(
      GetRefSlot(self, self->klass, "suppressedExceptions"));
  for (int32_t i = 0; sup != nullptr && i < sup->length; i++) {
    ObjHeader* s = nullptr;
    std::memcpy(&s, sup->data() + static_cast<size_t>(i) * 8, sizeof(s));
    if (s == nullptr) continue;
    if (!ThrowablePrintRec(state, static_cast<VObject*>(s), target, seen,
                           (std::string(prefix) + "Suppressed: ").c_str(), ok)) {
      return false;
    }
  }
  // 因果链（防 initCause 环）
  VObject* cause = reinterpret_cast<VObject*>(GetRefSlot(self, self->klass, "cause"));
  if (cause != nullptr && cause != self) {
    for (VObject* s : *seen) {
      if (s == cause) return true;
    }
    seen->push_back(cause);
    std::string cprefix = "Caused by: ";
    if (!ThrowablePrintRec(state, cause, target, seen, cprefix.c_str(), ok)) return false;
  }
  return true;
}

}  // namespace
bool ThrowablePrintRec(VMState& state, VObject* self, VObject* target,
                       std::vector<VObject*>* seen, const char* prefix, bool* ok) {
  return ThrowablePrintBody(state, self, target, seen, prefix, ok);
}

bool ThrowablePrintTo(VMState& state, VObject* self, VObject* target, bool* ok) {
  std::vector<VObject*> seen;
  seen.push_back(self);
  return ThrowablePrintBody(state, self, target, &seen, "", ok);
}

VObject* SystemErrStream(VMState& state) {
  VClass* sys = state.linker->FindClass("Ljava/lang/System;");
  VField* err_f = sys != nullptr ? LookupField(sys, "err", false) : nullptr;
  if (err_f != nullptr && err_f->is_static && err_f->offset < sys->static_storage.size()) {
    return static_cast<VObject*>(sys->static_storage[err_f->offset].ref);
  }
  return nullptr;
}

Value ThrowablePrintStackTrace(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  (void)args; (void)n;
  VObject* err = SystemErrStream(state);
  if (err == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "System.err");
    *ok = false;
    return Value{};
  }
  ThrowablePrintTo(state, self, err, ok);
  return Value{};
}

Value ThrowablePrintStackTraceStream(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "printStackTrace");
    *ok = false;
    return Value{};
  }
  ThrowablePrintTo(state, self, static_cast<VObject*>(args[0].ref), ok);
  return Value{};
}

Value ThrowablePrintStackTraceWriter(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok) {
  return ThrowablePrintStackTraceStream(state, self, args, n, ok);
}

// ---- ThreadGroup.uncaughtException（OpenJDK 语义）----
// 父组递归 → 默认 UEH →（非 ThreadDeath）stderr "Exception in thread \"name\" "
// + printStackTrace(System.err)（Java 8 形：print 头 + printStackTrace 续行）。

}  // namespace dexvm::runtime
