// JNI 全面（M1 最小面 + M2 补全）实现（docs 05 章 15.1-15.15、§28.3；
// 对照 jni_internal.cc/java_vm_ext.cc）
#include "jni/jni_env.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/exception.h"
#include "core/interpreter.h"
#include "jni/native_manager.h"
#include "jni/reflection.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace dexvm::jni {

namespace {

using dexvm::runtime::ClassLinker;
using dexvm::runtime::ClassStatus;
using dexvm::runtime::IsClinitFailed;
using dexvm::runtime::NoteClinitFailure;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

// ---- 句柄 <-> 指针位型（jobject 是不透明句柄，非裸指针）----

inline Handle ToHandle(const void* p) {
  return static_cast<Handle>(reinterpret_cast<uintptr_t>(p));
}

template <typename T>
inline T FromHandle(Handle h) {
  return reinterpret_cast<T>(static_cast<uintptr_t>(h));
}

inline JNIEnvExt* Ext(JNIEnv* env) { return static_cast<JNIEnvExt*>(env); }

// ---- Decode / Encode（docs 15.3/15.4/15.13）----

ObjHeader* DecodeObj(JNIEnvExt* e, jobject ref) {
  if (ref == nullptr) return nullptr;
  Handle h = ToHandle(ref);
  switch (GetHandleKind(h)) {
    case RefKind::kLocal:
      return e->locals.Get(h);
    case RefKind::kGlobal:
    case RefKind::kWeakGlobal:
      return e->vm != nullptr ? e->vm->globals.Get(h) : nullptr;  // weak 清除 -> null
    default:
      e->Warn("DecodeJObject: invalid reference");
      return nullptr;
  }
}

jobject EncodeObj(JNIEnvExt* e, ObjHeader* obj) {
  if (obj == nullptr) return nullptr;
  Handle h = e->locals.Add(obj);
  if (h == kNullHandle) {
    e->Warn("EncodeJObject: local reference table overflow");
    // JNI 耗尽语义（docs 15.12）：返 null 且抛 OutOfMemoryError——调用方按
    // ExceptionCheck 收口，不静默 null 流入后续解引用（SecNeo 回调崩溃形态）。
    if (e->vmstate != nullptr && !e->vmstate->HasPendingException()) {
      e->vmstate->ThrowNew("Ljava/lang/OutOfMemoryError;",
                           "local reference table overflow (capacity " +
                               std::to_string(kMaxLocalRefs) + ")");
    }
    return nullptr;
  }
  return FromHandle<jobject>(h);
}

VClass* DecodeClass(JNIEnvExt* e, jclass clazz) {
  // jclass 句柄指向 java.lang.Class 元对象（本 VM 中即 VClass 本体）
  return reinterpret_cast<VClass*>(DecodeObj(e, clazz));
}

// ---- 窄化（8 字节元素/字段槽语义，对照 interpreter Narrow）----

enum class Narrow : uint8_t { kWord, kWide, kRef, kBool, kByte, kChar, kShort };

uint64_t NarrowRead(Narrow n, uint64_t slot) {
  switch (n) {
    case Narrow::kWide:
    case Narrow::kRef:
      return slot;
    case Narrow::kBool:
      return (slot & 0xffu) != 0 ? 1u : 0u;
    case Narrow::kByte:
      return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(slot)));
    case Narrow::kChar:
      return slot & 0xffffu;
    case Narrow::kShort:
      return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(slot)));
    case Narrow::kWord:
    default:
      return static_cast<uint32_t>(slot);
  }
}

uint64_t NarrowWrite(Narrow n, uint64_t bits) {
  switch (n) {
    case Narrow::kWide:
    case Narrow::kRef:
      return bits;
    case Narrow::kBool:
      return bits & 0xffu;
    case Narrow::kByte:
      return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int8_t>(bits)));
    case Narrow::kChar:
      return bits & 0xffffu;
    case Narrow::kShort:
      return static_cast<uint64_t>(static_cast<int64_t>(static_cast<int16_t>(bits)));
    case Narrow::kWord:
    default:
      return static_cast<uint32_t>(bits);
  }
}

Narrow PrimNarrow(char prim) {
  switch (prim) {
    case 'Z': return Narrow::kBool;
    case 'B': return Narrow::kByte;
    case 'C': return Narrow::kChar;
    case 'S': return Narrow::kShort;
    case 'J':
    case 'D': return Narrow::kWide;
    default: return Narrow::kWord;  // I/F
  }
}

size_t PrimElemSize(char prim) {
  switch (prim) {
    case 'Z':
    case 'B': return 1;
    case 'C':
    case 'S': return 2;
    case 'J':
    case 'D': return 8;
    default: return 4;  // I/F
  }
}

inline uint64_t LoadSlot(const void* p) {
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

inline void StoreSlot(void* p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }

// ---- MUTF-8 -> UTF-8 ----
// NewStringUTF 按 ART NewStringUTFVisitor：modified UTF-8（C0 80 = U+0000）、
// 常规 1-3 字节序列、历史 4 字节 UTF-8（转代理对）；非法序列 -> U+FFFD。
void Mutf8ToChars(const char* bytes, std::u16string* out) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(bytes);
  size_t i = 0;
  size_t n = std::strlen(bytes);
  while (i < n) {
    uint8_t b = p[i];
    if (b < 0x80) {
      out->push_back(static_cast<char16_t>(b));
      i++;
      continue;
    }
    if (b == 0xC0 && i + 1 < n && p[i + 1] == 0x80) {
      out->push_back(0);  // MUTF-8 内嵌 NUL
      i += 2;
      continue;
    }
    int extra;
    uint32_t cp;
    if ((b & 0xE0) == 0xC0) {
      extra = 1;
      cp = b & 0x1Fu;
    } else if ((b & 0xF0) == 0xE0) {
      extra = 2;
      cp = b & 0x0Fu;
    } else if ((b & 0xF8) == 0xF0) {
      extra = 3;  // 历史 4-byte UTF-8
      cp = b & 0x07u;
    } else {
      out->push_back(0xFFFD);
      i++;
      continue;
    }
    if (i + extra >= n) {  // 截断
      out->push_back(0xFFFD);
      i++;
      continue;
    }
    bool bad = false;
    for (int k = 1; k <= extra; k++) {
      uint8_t c = p[i + k];
      if ((c & 0xC0u) != 0x80u) {
        bad = true;
        break;
      }
      cp = (cp << 6) | (c & 0x3Fu);
    }
    if (bad) {
      out->push_back(0xFFFD);
      i++;
      continue;
    }
    i += extra + 1;
    if (cp >= 0x10000u) {
      cp -= 0x10000u;
      out->push_back(static_cast<char16_t>(0xD800u + (cp >> 10)));
      out->push_back(static_cast<char16_t>(0xDC00u + (cp & 0x3FFu)));
    } else {
      out->push_back(static_cast<char16_t>(cp));
    }
  }
}

// UTF-16 -> MUTF-8（与 VString::ToModifiedUtf8 同语义，供 GetStringUTFRegion 子串）
void CharsToMutf8(const uint16_t* chars, size_t n, std::string* out) {
  out->clear();
  out->reserve(n);
  for (size_t i = 0; i < n; i++) {
    uint16_t c = chars[i];
    if (c != 0 && c < 0x80) {
      out->push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out->push_back(static_cast<char>(0xC0 | (c >> 6)));
      out->push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out->push_back(static_cast<char>(0xE0 | (c >> 12)));
      out->push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out->push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
}

// ---- 类初始化（对照 interpreter EnsureClassInitialized，docs 03 章 12）----

bool EnsureInitialized(JNIEnvExt* e, VClass* klass) {
  if (klass == nullptr) {
    if (e->vmstate != nullptr) e->vmstate->ThrowNew("Ljava/lang/InternalError;", "null class");
    return false;
  }
  if (klass->status == ClassStatus::kInitialized) return true;
  // erroneous 语义（docs 03 章 12）：<clinit> 曾失败 -> 不再重跑（同
  // interpreter EnsureClassInitialized；重跑 = 字面量解密清单原地打转）。
  if (IsClinitFailed(klass)) {
    if (e->vmstate != nullptr) {
      e->vmstate->ThrowNew("Ljava/lang/NoClassDefFoundError;",
                           klass->descriptor + " (erroneous: <clinit> previously failed)");
    }
    return false;
  }
  if (e->vmstate == nullptr || e->vmstate->linker == nullptr) return true;
  if (e->vmstate->linker->InitializeClass(klass, e->vmstate)) return true;
  if (e->vmstate->HasPendingException()) NoteClinitFailure(klass);
  e->vmstate->ClearException();
  e->vmstate->ThrowNew("Ljava/lang/ExceptionInInitializerError;",
                       klass->descriptor + " <clinit> failed");
  return false;
}

// ---- ID 查找（签名 "(params)return" 精确匹配，沿继承链）----

std::string MethodSignature(const VMethod& m) {
  std::string s = "(";
  for (const auto& p : m.param_types) s += p;
  s += ")";
  s += m.return_type;
  return s;
}

VMethod* FindMethod(VClass* klass, const char* name, const char* sig, bool want_static) {
  if (klass == nullptr || name == nullptr || sig == nullptr) return nullptr;
  std::string want(sig);
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (std::vector<VMethod>* list : {&k->direct_methods, &k->virtual_methods}) {
      for (auto& m : *list) {
        if (m.name == name && m.IsStatic() == want_static && MethodSignature(m) == want) {
          return &m;
        }
      }
    }
  }
  return nullptr;
}

VField* FindField(VClass* klass, const char* name, const char* sig, bool want_static) {
  if (klass == nullptr || name == nullptr || sig == nullptr) return nullptr;
  std::string want(sig);
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (std::vector<VField>* list : {&k->instance_fields, &k->static_fields}) {
      for (auto& f : *list) {
        if (f.name == name && f.is_static == want_static && f.type_descriptor == want) return &f;
      }
    }
  }
  return nullptr;
}

// ---- 参数铺陈（jvalue / va_list -> Value；wide 一个 Value 装 64 位）----

bool BuildArgsVa(JNIEnvExt* e, VMethod* m, va_list ap, std::vector<Value>* out) {
  for (const auto& p : m->param_types) {
    Value v{};
    switch (p[0]) {
      case 'Z':
      case 'B':
      case 'C':
      case 'S':
      case 'I':
        v.i = va_arg(ap, jint);  // C 变参整型提升
        break;
      case 'J':
        v.l = va_arg(ap, jlong);
        break;
      case 'F': {
        jdouble d = va_arg(ap, jdouble);  // C 变参 float 提升为 double
        jfloat f = static_cast<jfloat>(d);
        uint32_t bits;
        std::memcpy(&bits, &f, sizeof(bits));
        v.i = static_cast<int32_t>(bits);
        break;
      }
      case 'D': {
        jdouble d = va_arg(ap, jdouble);
        uint64_t bits;
        std::memcpy(&bits, &d, sizeof(bits));
        v.l = static_cast<int64_t>(bits);
        break;
      }
      default:
        v.ref = DecodeObj(e, va_arg(ap, jobject));
        break;
    }
    out->push_back(v);
  }
  return true;
}

bool BuildArgsJValue(JNIEnvExt* e, VMethod* m, const jvalue* args, std::vector<Value>* out) {
  size_t i = 0;
  for (const auto& p : m->param_types) {
    Value v{};
    const jvalue& av = args[i++];
    switch (p[0]) {
      case 'Z':
        v.i = av.z != 0 ? 1 : 0;
        break;
      case 'B':
        v.i = av.b;
        break;
      case 'C':
        v.i = av.c;
        break;
      case 'S':
        v.i = av.s;
        break;
      case 'I':
        v.i = av.i;
        break;
      case 'J':
        v.l = av.j;
        break;
      case 'F': {
        uint32_t bits;
        std::memcpy(&bits, &av.f, sizeof(bits));
        v.i = static_cast<int32_t>(bits);
        break;
      }
      case 'D': {
        uint64_t bits;
        std::memcpy(&bits, &av.d, sizeof(bits));
        v.l = static_cast<int64_t>(bits);
        break;
      }
      default:
        v.ref = DecodeObj(e, av.l);
        break;
    }
    out->push_back(v);
  }
  return true;
}

// ---- 调用（docs 15.8：validate receiver -> EnsureInitialized -> invoke）----
// do_virtual=false：CallNonvirtual 语义（methodID 本体直调，跳过覆盖实现）。
// native_fn 已登记：走 native trampoline（SetEntryPointFromJni 等价桩）。

bool InvokeJni(JNIEnvExt* e, VMethod* target, jobject receiver, const std::vector<Value>& args,
               Value* result, bool do_virtual) {
  // 返回值卫生（D2 根因面）：进门先全宽清零（Value NSDMI 约定，见 vm_types.h）。
  // CallCore 任何失败/空返回路径不得残留旧值——上一次调用的 result.l 残留
  // 会让 RetObject 把陈旧 ObjHeader* 编码成"新句柄指向错对象"。
  if (result != nullptr) result->l = 0;
  VMState& st = *e->vmstate;
  if (target == nullptr) {
    st.ThrowNew("Ljava/lang/NoSuchMethodError;", "null methodID");
    return false;
  }
  if (!target->IsStatic()) {
    ObjHeader* recv = DecodeObj(e, receiver);
    if (recv == nullptr) {
      dexvm::runtime::ThrowNullPointerException(st, "JNI call on null");
      return false;
    }
    if (recv->klass == nullptr) {
      st.ThrowNew("Ljava/lang/InternalError;", "receiver has no class");
      return false;
    }
    // Call<Type>Method：虚派发（direct 直调；对照 DoCall virtual/interface）
    if (do_virtual && !target->IsDirect()) {
      VMethod* impl = recv->klass->FindInterfaceImpl(target);
      if (impl == nullptr) {
        st.ThrowNew("Ljava/lang/NoSuchMethodError;", target->name);
        return false;
      }
      target = impl;
    }
    if (target->jni_native_fn != nullptr) {
#if defined(__ANDROID__)
      // TEMP DIAG（H.d 经 JNI 回调的解密结果归因；事后移除）。
      const bool is_hd = target->declaring != nullptr &&
                         target->declaring->descriptor == "Lcom/secneo/apkwrapper/H;" &&
                         target->name == "d";
      std::string hd_in;
      if (is_hd && !args.empty() && args[0].ref != nullptr && args[0].ref->klass != nullptr &&
          args[0].ref->klass->descriptor == "Ljava/lang/String;") {
        hd_in = reinterpret_cast<VString*>(args[0].ref)->ToModifiedUtf8();
      }
      bool hd_ok = InvokeNativeTrampoline(e, target, recv, args.data(), args.size(), result);
      if (is_hd) {
        std::string hd_out = "?";
        if (hd_ok && result != nullptr && result->ref != nullptr &&
            result->ref->klass != nullptr &&
            result->ref->klass->descriptor == "Ljava/lang/String;") {
          hd_out = reinterpret_cast<VString*>(result->ref)->ToModifiedUtf8();
        }
        // 降噪：H.d 每秒数百次调用，全量打印曾累计百万行 logcat（同步写拖慢
        // 启动）。仅前 300 次打印（解密链诊断仍可见），其余静默。
        static std::atomic<int> hd_logs{0};
        if (hd_logs.fetch_add(1) < 300) {
          __android_log_print(ANDROID_LOG_INFO, "DexVmJniStr", "jni H.d(%s) -> ok=%d \"%s\"",
                              hd_in.c_str(), hd_ok ? 1 : 0, hd_out.c_str());
        }
      }
      return hd_ok;
#else
      return InvokeNativeTrampoline(e, target, recv, args.data(), args.size(), result);
#endif
    }
    if ((target->access_flags & 0x100u) != 0 && !target->HasCode()) {
      // native 未登记：UnsatisfiedLinkError（UnregisterNatives 后调用面）
      st.ThrowNew("Ljava/lang/UnsatisfiedLinkError;", target->declaring->descriptor + "." +
                                                          target->name);
      return false;
    }
    if (!EnsureInitialized(e, target->declaring)) return false;
    std::vector<Value> full;
    full.reserve(args.size() + 1);
    Value rv{};
    rv.ref = recv;
    full.push_back(rv);
    full.insert(full.end(), args.begin(), args.end());
    return dexvm::runtime::InvokeMethod(st, target, full.data(), full.size(), result);
  }
  if (target->jni_native_fn != nullptr) {
#if defined(__ANDROID__)
    // TEMP DIAG（H.d 经 JNI 静态回调的解密结果归因；事后移除）。
    const bool is_hd = target->declaring != nullptr &&
                       target->declaring->descriptor == "Lcom/secneo/apkwrapper/H;" &&
                       target->name == "d";
    std::string hd_in;
    if (is_hd && !args.empty() && args[0].ref != nullptr && args[0].ref->klass != nullptr &&
        args[0].ref->klass->descriptor == "Ljava/lang/String;") {
      hd_in = reinterpret_cast<VString*>(args[0].ref)->ToModifiedUtf8();
    }
    bool hd_ok = InvokeNativeTrampoline(e, target, nullptr, args.data(), args.size(), result);
    if (is_hd) {
      std::string hd_out = "?";
      if (hd_ok && result != nullptr && result->ref != nullptr &&
          result->ref->klass != nullptr &&
          result->ref->klass->descriptor == "Ljava/lang/String;") {
        hd_out = reinterpret_cast<VString*>(result->ref)->ToModifiedUtf8();
      }
      __android_log_print(ANDROID_LOG_INFO, "DexVmJniStr", "jniS H.d(%s) -> ok=%d \"%s\"",
                          hd_in.c_str(), hd_ok ? 1 : 0, hd_out.c_str());
    }
    return hd_ok;
#else
    return InvokeNativeTrampoline(e, target, nullptr, args.data(), args.size(), result);
#endif
  }
  if ((target->access_flags & 0x100u) != 0 && !target->HasCode()) {
    st.ThrowNew("Ljava/lang/UnsatisfiedLinkError;",
                target->declaring->descriptor + "." + target->name);
    return false;
  }
  if (!EnsureInitialized(e, target->declaring)) return false;
  return dexvm::runtime::InvokeMethod(st, target, args.data(), args.size(), result);
}

// ---- 返回值转换（object -> local ref；异常保留在 Guest Thread）----

jobject RetObject(JNIEnvExt* e, const Value& r) {
  return EncodeObj(e, reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(r.l)));
}
jint RetInt(const Value& r) { return static_cast<jint>(r.l); }
jboolean RetBoolean(const Value& r) { return (r.l & 0xffu) != 0 ? JNI_TRUE : JNI_FALSE; }
jbyte RetByte(const Value& r) { return static_cast<jbyte>(r.l); }
jchar RetChar(const Value& r) { return static_cast<jchar>(r.l & 0xffffu); }
jshort RetShort(const Value& r) { return static_cast<jshort>(r.l); }
jlong RetLong(const Value& r) { return static_cast<jlong>(r.l); }
jfloat RetFloat(const Value& r) {
  uint32_t bits = static_cast<uint32_t>(r.l);
  jfloat out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}
jdouble RetDouble(const Value& r) {
  uint64_t bits = static_cast<uint64_t>(r.l);
  jdouble out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

// ---- 对象分配（String 特判 -> AllocEmptyString 语义，docs 15.5/28.3）----

VObject* AllocInstance(JNIEnvExt* e, VClass* klass) {
  if (klass->descriptor == "Ljava/lang/String;") {
    // AllocObject(String) -> AllocEmptyString：直接给空 VString
    VString* s = e->vmstate->linker->AllocString(std::u16string_view());
    return reinterpret_cast<VObject*>(s);
  }
  return e->vmstate->linker->AllocObject(klass);
}

// ---- 字段槽（8 字节槽语义与 core 一致）----

uint8_t* InstanceSlot(JNIEnvExt* e, jobject obj, VField* f) {
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "JNI field access on null");
    return nullptr;
  }
  return static_cast<VObject*>(o)->field_data() + f->offset;
}

Value* StaticSlot(JNIEnvExt* e, VField* f) {
  if (!EnsureInitialized(e, f->declaring)) return nullptr;
  VClass* decl = f->declaring;
  if (decl == nullptr || f->offset >= decl->static_storage.size()) {
    e->vmstate->ThrowNew("Ljava/lang/NoSuchFieldError;", f->name + " static storage missing");
    return nullptr;
  }
  return &decl->static_storage[f->offset];
}

// static final 写入 EnsureModifiable（docs 15.7）
bool EnsureModifiable(JNIEnvExt* e, VField* f) {
  if (f->is_static && (f->access_flags & 0x10u) != 0) {  // kAccFinal
    e->vmstate->ThrowNew("Ljava/lang/IllegalAccessError;",
                         "Illegal static final field write: " + f->name);
    return false;
  }
  return true;
}

// ---- 数组槽助手（8 字节元素槽）----

VArray* DecodeArray(JNIEnvExt* e, jarray arr, const char* what) {
  ObjHeader* o = DecodeObj(e, arr);
  if (o == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, std::string(what) + " on null");
    return nullptr;
  }
  return static_cast<VArray*>(o);
}

bool CheckRegion(JNIEnvExt* e, VArray* arr, jsize start, jsize len) {
  if (start < 0 || len < 0 || start > arr->length || len > arr->length - start) {
    dexvm::runtime::ThrowArrayIndexOutOfBoundsException(*e->vmstate, start + len, arr->length);
    return false;
  }
  return true;
}

template <typename T, Narrow K>
T* GetArrayElementsT(JNIEnvExt* e, VArray* arr, jboolean* isCopy) {
  (void)e;
  if (isCopy != nullptr) *isCopy = JNI_TRUE;
  size_t n = static_cast<size_t>(arr->length);
  T* buf = static_cast<T*>(std::malloc(n * sizeof(T) + 1));  // +1：0 长度可判非空
  if (buf == nullptr) return nullptr;
  for (size_t i = 0; i < n; i++) {
    uint64_t slot = LoadSlot(arr->data() + i * 8);
    uint64_t v = NarrowRead(K, slot);
    T out;
    std::memcpy(&out, &v, sizeof(T));
    buf[i] = out;
  }
  return buf;
}

template <typename T, Narrow K>
void ReleaseArrayElementsT(VArray* arr, T* elems, jint mode) {
  if (mode != JNI_ABORT) {
    size_t n = static_cast<size_t>(arr->length);
    for (size_t i = 0; i < n; i++) {
      uint64_t v = 0;
      std::memcpy(&v, &elems[i], sizeof(T));
      StoreSlot(arr->data() + i * 8, NarrowWrite(K, v));
    }
  }
  if (mode != JNI_COMMIT) std::free(elems);
}

template <typename T, Narrow K>
void GetArrayRegionT(JNIEnvExt* e, VArray* arr, jsize start, jsize len, T* buf) {
  if (!CheckRegion(e, arr, start, len)) return;
  for (jsize i = 0; i < len; i++) {
    uint64_t slot = LoadSlot(arr->data() + static_cast<size_t>(start + i) * 8);
    uint64_t v = NarrowRead(K, slot);
    T out;
    std::memcpy(&out, &v, sizeof(T));
    buf[i] = out;
  }
}

template <typename T, Narrow K>
void SetArrayRegionT(JNIEnvExt* e, VArray* arr, jsize start, jsize len, const T* buf) {
  if (!CheckRegion(e, arr, start, len)) return;
  for (jsize i = 0; i < len; i++) {
    uint64_t v = 0;
    std::memcpy(&v, &buf[i], sizeof(T));
    StoreSlot(arr->data() + static_cast<size_t>(start + i) * 8, NarrowWrite(K, v));
  }
}

template <char Prim>
jarray NewPrimArray(JNIEnvExt* e, jsize length) {
  if (length < 0) {
    e->vmstate->ThrowNew("Ljava/lang/NegativeArraySizeException;", std::to_string(length));
    return nullptr;
  }
  ClassLinker* linker = e->vmstate->linker;
  VClass* comp = linker->PrimitiveClass(Prim);
  VClass* ac = (comp != nullptr) ? linker->FindArrayClass(comp) : nullptr;
  VArray* arr = (ac != nullptr) ? linker->AllocArray(ac, length) : nullptr;
  if (arr == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "NewArray");
    return nullptr;
  }
  return FromHandle<jarray>(e->locals.Add(arr));
}

// ---- 异常对象 detailMessage（对照 exception.cc SetDetailMessage）----

void SetDetailMessage(JNIEnvExt* e, ObjHeader* ex, const char* msg) {
  if (ex == nullptr || msg == nullptr || ex->klass == nullptr) return;
  for (VClass* k = ex->klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == "detailMessage") {
        std::u16string chars;
        for (const char* p = msg; *p != 0; p++) {
          chars.push_back(static_cast<char16_t>(static_cast<unsigned char>(*p)));
        }
        VString* s = e->vmstate->linker->AllocString(chars);
        ObjHeader** slot = reinterpret_cast<ObjHeader**>(static_cast<VObject*>(ex)->field_data() +
                                                        f.offset);
        *slot = reinterpret_cast<ObjHeader*>(s);
        return;
      }
    }
  }
}

// =====================================================================
// JNIEnv 函数表实现
// =====================================================================

jint JniGetVersion(JNIEnv*) { return JNI_VERSION_1_6; }

jclass JniDefineClass(JNIEnv* env, const char*, jobject, const jbyte*, jsize) {
  // docs 15.6 / jni_internal.cc:707-710：不支持，log warning + nullptr
  std::fprintf(stderr, "WARNING: JNI DefineClass is not supported\n");
  (void)env;
  return nullptr;
}

jclass JniFindClass(JNIEnv* env, const char* name) {
  JNIEnvExt* e = Ext(env);
  if (name == nullptr) return nullptr;
  // docs 15.6 / jni_internal.cc:712-729：NormalizeJniClassDescriptor +
  // GetClassLoader 优先级（override -> 当前帧声明类 loader -> system -> boot）
  std::string desc = NormalizeJniClassDescriptor(name);
  VClass* k = e->vmstate->linker->FindClass(GetEffectiveClassLoader(e), desc);
  if (k == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NoClassDefFoundError;", name);
    return nullptr;
  }
  return FromHandle<jclass>(e->locals.Add(reinterpret_cast<ObjHeader*>(k)));
}

jclass JniGetSuperclass(JNIEnv* env, jclass clazz) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr) return nullptr;
  if (k->IsInterface()) return nullptr;  // docs 15.5：interface ? null : super
  if (k->super_class == nullptr) return nullptr;
  return FromHandle<jclass>(e->locals.Add(reinterpret_cast<ObjHeader*>(k->super_class)));
}

jboolean JniIsAssignableFrom(JNIEnv* env, jclass clazz1, jclass clazz2) {
  JNIEnvExt* e = Ext(env);
  VClass* c1 = DecodeClass(e, clazz1);
  VClass* c2 = DecodeClass(e, clazz2);
  if (c1 == nullptr || c2 == nullptr) return JNI_FALSE;  // jni_internal.cc:782-785
  // ART：java_class1 应可安全转换为 java_class2（jni_internal.cc:780-789）
  return c2->IsAssignableFrom(c1) ? JNI_TRUE : JNI_FALSE;
}

jint JniThrow(JNIEnv* env, jthrowable obj) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* ex = DecodeObj(e, obj);
  if (ex == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NullPointerException;", "throw null");
    return JNI_ERR;
  }
  e->vmstate->Throw(ex);
  return 0;
}

jint JniThrowNew(JNIEnv* env, jclass clazz, const char* msg) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NullPointerException;", "ThrowNew null class");
    return JNI_ERR;
  }
  VObject* ex = e->vmstate->linker->AllocObject(k);
  if (ex == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "ThrowNew");
    return JNI_ERR;
  }
  SetDetailMessage(e, ex, msg != nullptr ? msg : "");
  e->vmstate->Throw(ex);
  return 0;
}

jthrowable JniExceptionOccurred(JNIEnv* env) {
  JNIEnvExt* e = Ext(env);
  return FromHandle<jthrowable>(
      e->locals.Add(e->vmstate->pending_exception));  // null -> kNullHandle
}

jboolean JniExceptionCheck(JNIEnv* env) {
  return Ext(env)->vmstate->HasPendingException() ? JNI_TRUE : JNI_FALSE;
}

void JniExceptionDescribe(JNIEnv* env) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* ex = e->vmstate->pending_exception;
  if (ex == nullptr) return;
  const char* cls = (ex->klass != nullptr) ? ex->klass->descriptor.c_str() : "<unknown>";
  // detailMessage
  std::string msg;
  if (ex->klass != nullptr) {
    for (VClass* k = ex->klass; k != nullptr && msg.empty(); k = k->super_class) {
      for (auto& f : k->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* s =
              *reinterpret_cast<ObjHeader**>(static_cast<VObject*>(ex)->field_data() + f.offset);
          if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
          break;
        }
      }
    }
  }
  std::fprintf(stderr, "Exception: %s: %s\n", cls, msg.c_str());
}

void JniExceptionClear(JNIEnv* env) { Ext(env)->vmstate->ClearException(); }

void JniFatalError(JNIEnv* env, const char* msg) {
  (void)env;
  std::fprintf(stderr, "FATAL ERROR: %s\n", msg != nullptr ? msg : "(null)");
  std::fflush(stderr);
  std::abort();
}

jint JniPushLocalFrame(JNIEnv* env, jint capacity) {
  JNIEnvExt* e = Ext(env);
  // docs 15.12：容量核算（不足返回 JNI_ERR，不入帧）
  if (!e->locals.EnsureCapacity(capacity)) {
    e->Warn("PushLocalFrame: insufficient local reference capacity");
    return JNI_ERR;
  }
  e->locals.PushFrame();
  return 0;
}

jobject JniPopLocalFrame(JNIEnv* env, jobject result) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* survivor = DecodeObj(e, result);  // 解码后随帧释放
  if (!e->locals.PopFrame()) {
    e->Warn("PopLocalFrame: unbalanced pop");
    return nullptr;
  }
  return EncodeObj(e, survivor);  // survivor 转外层 frame 的 local reference
}

jobject JniNewGlobalRef(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) return nullptr;
  return FromHandle<jobject>(e->vm->globals.Add(o));
}

void JniDeleteGlobalRef(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  if (obj == nullptr) return;
  if (!e->vm->globals.Remove(ToHandle(obj))) e->Warn("DeleteGlobalRef: stale reference");
}

void JniDeleteLocalRef(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  if (obj == nullptr) return;
  // docs 15.15：非当前 frame 的 entry no-op + WARNING，不抛异常
  if (!e->locals.Remove(ToHandle(obj))) e->Warn("DeleteLocalRef: not in current frame");
}

jboolean JniIsSameObject(JNIEnv* env, jobject a, jobject b) {
  JNIEnvExt* e = Ext(env);
  return DecodeObj(e, a) == DecodeObj(e, b) ? JNI_TRUE : JNI_FALSE;
}

jobject JniNewLocalRef(JNIEnv* env, jobject ref) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, ref);  // weak -> read barrier/weak clear check（15.15）
  return EncodeObj(e, o);
}

jint JniEnsureLocalCapacity(JNIEnv* env, jint capacity) {
  // docs 15.12：真实容量核算（kMaxLocalRefs 余量）
  JNIEnvExt* e = Ext(env);
  if (!e->locals.EnsureCapacity(capacity)) {
    e->Warn("EnsureLocalCapacity: insufficient local reference capacity");
    return JNI_ERR;
  }
  return 0;
}

jobject JniAllocObject(JNIEnv* env, jclass clazz) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "AllocObject null class");
    return nullptr;
  }
  if (!EnsureInitialized(e, k)) return nullptr;
  VObject* obj = AllocInstance(e, k);
  if (obj == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "AllocObject");
    return nullptr;
  }
  return EncodeObj(e, obj);
}

jobject NewObjectCommon(JNIEnvExt* e, jclass clazz, jmethodID methodID,
                        const std::vector<Value>& vargs) {
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "NewObject null class");
    return nullptr;
  }
  if (!EnsureInitialized(e, k)) return nullptr;
  VObject* obj = AllocInstance(e, k);  // String -> 空 VString（AllocEmptyString 语义）
  if (obj == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "NewObject");
    return nullptr;
  }
  VMethod* ctor = DecodeMethodId(methodID);
  if (ctor != nullptr) {
    std::vector<Value> args;
    args.reserve(vargs.size() + 1);
    Value rv{};
    rv.ref = obj;
    args.push_back(rv);
    args.insert(args.end(), vargs.begin(), vargs.end());
    Value dummy{};
    if (ctor->jni_native_fn != nullptr) {
      if (!InvokeNativeTrampoline(e, ctor, obj, vargs.data(), vargs.size(), &dummy)) return nullptr;
    } else if (!dexvm::runtime::InvokeMethod(*e->vmstate, ctor, args.data(), args.size(), &dummy)) {
      return nullptr;
    }
  }
  return EncodeObj(e, obj);
}

jobject JniNewObject(JNIEnv* env, jclass clazz, jmethodID methodID, ...) {
  JNIEnvExt* e = Ext(env);
  VMethod* ctor = DecodeMethodId(methodID);
  std::vector<Value> args;
  if (ctor != nullptr) {
    va_list ap;
    va_start(ap, methodID);
    BuildArgsVa(e, ctor, ap, &args);
    va_end(ap);
  }
  return NewObjectCommon(e, clazz, methodID, args);
}

jobject JniNewObjectV(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args) {
  JNIEnvExt* e = Ext(env);
  VMethod* ctor = DecodeMethodId(methodID);
  std::vector<Value> vargs;
  if (ctor != nullptr) BuildArgsVa(e, ctor, args, &vargs);
  return NewObjectCommon(e, clazz, methodID, vargs);
}

jobject JniNewObjectA(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args) {
  JNIEnvExt* e = Ext(env);
  VMethod* ctor = DecodeMethodId(methodID);
  std::vector<Value> vargs;
  if (ctor != nullptr) BuildArgsJValue(e, ctor, args, &vargs);
  return NewObjectCommon(e, clazz, methodID, vargs);
}

jclass JniGetObjectClass(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "GetObjectClass null");
    return nullptr;
  }
  return FromHandle<jclass>(e->locals.Add(reinterpret_cast<ObjHeader*>(o->klass)));
}

jboolean JniIsInstanceOf(JNIEnv* env, jobject obj, jclass clazz) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) return JNI_TRUE;  // docs 15.5
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr || o->klass == nullptr) return JNI_FALSE;
  return k->IsAssignableFrom(o->klass) ? JNI_TRUE : JNI_FALSE;
}

jmethodID JniGetMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr || !EnsureInitialized(e, k)) return nullptr;
  VMethod* m = FindMethod(k, name, sig, /*want_static=*/false);
  if (m == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NoSuchMethodError;",
                         std::string(name != nullptr ? name : "") + " " +
                             std::string(sig != nullptr ? sig : ""));
    return nullptr;
  }
  return EncodeMethodId(m);
}

jmethodID JniGetStaticMethodID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr || !EnsureInitialized(e, k)) return nullptr;
  VMethod* m = FindMethod(k, name, sig, /*want_static=*/true);
  if (m == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NoSuchMethodError;",
                         std::string(name != nullptr ? name : "") + " " +
                             std::string(sig != nullptr ? sig : ""));
    return nullptr;
  }
  return EncodeMethodId(m);
}

// ---- Call 族共核 ----
bool CallCore(JNIEnvExt* e, jobject obj, jmethodID methodID, const std::vector<Value>& args,
              Value* result, bool do_virtual) {
  return InvokeJni(e, DecodeMethodId(methodID), obj, args, result, do_virtual);
}

// Call 族宏（虚派发）：FailRet = 失败（异常挂起）返回值；RetExpr = 成功返回值表达式
#define DEFINE_CALL_FAMILY(Name, RetType, FailRet, RetExpr)                                    \
  RetType JniCall##Name##Method(JNIEnv* env, jobject obj, jmethodID mid, ...) {                \
    JNIEnvExt* e = Ext(env);                                                                   \
    VMethod* m = DecodeMethodId(mid);                                                          \
    std::vector<Value> args;                                                                   \
    if (m != nullptr) {                                                                        \
      va_list ap;                                                                              \
      va_start(ap, mid);                                                                       \
      BuildArgsVa(e, m, ap, &args);                                                            \
      va_end(ap);                                                                              \
    }                                                                                          \
    Value result{};                                                                            \
    if (!CallCore(e, obj, mid, args, &result, true)) return (FailRet);                         \
    return (RetExpr);                                                                          \
  }                                                                                            \
  RetType JniCall##Name##MethodV(JNIEnv* env, jobject obj, jmethodID mid, va_list args) {      \
    JNIEnvExt* e = Ext(env);                                                                   \
    VMethod* m = DecodeMethodId(mid);                                                          \
    std::vector<Value> vargs;                                                                  \
    if (m != nullptr) BuildArgsVa(e, m, args, &vargs);                                         \
    Value result{};                                                                            \
    if (!CallCore(e, obj, mid, vargs, &result, true)) return (FailRet);                        \
    return (RetExpr);                                                                          \
  }                                                                                            \
  RetType JniCall##Name##MethodA(JNIEnv* env, jobject obj, jmethodID mid, const jvalue* args) { \
    JNIEnvExt* e = Ext(env);                                                                   \
    VMethod* m = DecodeMethodId(mid);                                                          \
    std::vector<Value> vargs;                                                                  \
    if (m != nullptr) BuildArgsJValue(e, m, args, &vargs);                                     \
    Value result{};                                                                            \
    if (!CallCore(e, obj, mid, vargs, &result, true)) return (FailRet);                        \
    return (RetExpr);                                                                          \
  }

// CallNonvirtual 族宏（methodID 本体直调，无虚派发）
#define DEFINE_CALL_NONVIRTUAL_FAMILY(Name, RetType, FailRet, RetExpr)                         \
  RetType JniCallNonvirtual##Name##Method(JNIEnv* env, jobject obj, jclass, jmethodID mid,     \
                                          ...) {                                               \
    JNIEnvExt* e = Ext(env);                                                                   \
    VMethod* m = DecodeMethodId(mid);                                                          \
    std::vector<Value> args;                                                                   \
    if (m != nullptr) {                                                                        \
      va_list ap;                                                                              \
      va_start(ap, mid);                                                                       \
      BuildArgsVa(e, m, ap, &args);                                                            \
      va_end(ap);                                                                              \
    }                                                                                          \
    Value result{};                                                                            \
    if (!CallCore(e, obj, mid, args, &result, false)) return (FailRet);                        \
    return (RetExpr);                                                                          \
  }                                                                                            \
  RetType JniCallNonvirtual##Name##MethodV(JNIEnv* env, jobject obj, jclass, jmethodID mid,    \
                                           va_list args) {                                     \
    JNIEnvExt* e = Ext(env);                                                                   \
    VMethod* m = DecodeMethodId(mid);                                                          \
    std::vector<Value> vargs;                                                                  \
    if (m != nullptr) BuildArgsVa(e, m, args, &vargs);                                         \
    Value result{};                                                                            \
    if (!CallCore(e, obj, mid, vargs, &result, false)) return (FailRet);                       \
    return (RetExpr);                                                                          \
  }                                                                                            \
  RetType JniCallNonvirtual##Name##MethodA(JNIEnv* env, jobject obj, jclass, jmethodID mid,    \
                                           const jvalue* args) {                               \
    JNIEnvExt* e = Ext(env);                                                                   \
    VMethod* m = DecodeMethodId(mid);                                                          \
    std::vector<Value> vargs;                                                                  \
    if (m != nullptr) BuildArgsJValue(e, m, args, &vargs);                                     \
    Value result{};                                                                            \
    if (!CallCore(e, obj, mid, vargs, &result, false)) return (FailRet);                       \
    return (RetExpr);                                                                          \
  }

// 静态 Call 族共核（receiver 忽略）
bool CallStaticCore(JNIEnvExt* e, jmethodID methodID, const std::vector<Value>& args,
                    Value* result) {
  return InvokeJni(e, DecodeMethodId(methodID), nullptr, args, result, false);
}

#define DEFINE_CALL_STATIC_FAMILY(Name, RetType, FailRet, RetExpr)                              \
  RetType JniCallStatic##Name##Method(JNIEnv* env, jclass, jmethodID mid, ...) {                \
    JNIEnvExt* e = Ext(env);                                                                    \
    VMethod* m = DecodeMethodId(mid);                                                           \
    std::vector<Value> args;                                                                    \
    if (m != nullptr) {                                                                         \
      va_list ap;                                                                               \
      va_start(ap, mid);                                                                        \
      BuildArgsVa(e, m, ap, &args);                                                             \
      va_end(ap);                                                                               \
    }                                                                                           \
    Value result{};                                                                             \
    if (!CallStaticCore(e, mid, args, &result)) return (FailRet);                               \
    return (RetExpr);                                                                           \
  }                                                                                             \
  RetType JniCallStatic##Name##MethodV(JNIEnv* env, jclass, jmethodID mid, va_list args) {      \
    JNIEnvExt* e = Ext(env);                                                                    \
    VMethod* m = DecodeMethodId(mid);                                                           \
    std::vector<Value> vargs;                                                                   \
    if (m != nullptr) BuildArgsVa(e, m, args, &vargs);                                          \
    Value result{};                                                                             \
    if (!CallStaticCore(e, mid, vargs, &result)) return (FailRet);                              \
    return (RetExpr);                                                                           \
  }                                                                                             \
  RetType JniCallStatic##Name##MethodA(JNIEnv* env, jclass, jmethodID mid, const jvalue* args) { \
    JNIEnvExt* e = Ext(env);                                                                    \
    VMethod* m = DecodeMethodId(mid);                                                           \
    std::vector<Value> vargs;                                                                   \
    if (m != nullptr) BuildArgsJValue(e, m, args, &vargs);                                      \
    Value result{};                                                                             \
    if (!CallStaticCore(e, mid, vargs, &result)) return (FailRet);                              \
    return (RetExpr);                                                                           \
  }

// 全 9 返回类型族 + Void（Object/Boolean/Byte/Char/Short/Int/Long/Float/Double/Void）
#define DEFINE_ALL_CALL_FAMILIES(Prefix, Macro)                    \
  Macro(Object, jobject, nullptr, RetObject(e, result))            \
  Macro(Boolean, jboolean, JNI_FALSE, RetBoolean(result))          \
  Macro(Byte, jbyte, 0, RetByte(result))                           \
  Macro(Char, jchar, 0, RetChar(result))                           \
  Macro(Short, jshort, 0, RetShort(result))                        \
  Macro(Int, jint, 0, RetInt(result))                              \
  Macro(Long, jlong, 0, RetLong(result))                           \
  Macro(Float, jfloat, 0, RetFloat(result))                        \
  Macro(Double, jdouble, 0, RetDouble(result))                     \
  Macro(Void, void, (void) 0, (void) result)

DEFINE_ALL_CALL_FAMILIES(Call, DEFINE_CALL_FAMILY)
DEFINE_ALL_CALL_FAMILIES(CallNonvirtual, DEFINE_CALL_NONVIRTUAL_FAMILY)
DEFINE_ALL_CALL_FAMILIES(CallStatic, DEFINE_CALL_STATIC_FAMILY)

// ---- 字段 ----

jfieldID JniGetFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr || !EnsureInitialized(e, k)) return nullptr;
  VField* f = FindField(k, name, sig, /*want_static=*/false);
  if (f == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NoSuchFieldError;",
                         std::string(name != nullptr ? name : "") + " " +
                             std::string(sig != nullptr ? sig : ""));
    return nullptr;
  }
  return EncodeFieldId(f);
}

// 对象字段（引用 = 完整指针）
jobject JniGetObjectField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot == nullptr) return nullptr;
  return EncodeObj(e, reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(LoadSlot(slot))));
}

void JniSetObjectField(JNIEnv* env, jobject obj, jfieldID fid, jobject value) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot == nullptr) return;
  StoreSlot(slot, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(DecodeObj(e, value))));
}

// 窄类型字段（Boolean/Byte/Char/Short：8 字节槽低半位型，与 Narrow 一致）
jboolean JniGetBooleanField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  return slot == nullptr ? JNI_FALSE : ((LoadSlot(slot) & 0xffu) != 0 ? JNI_TRUE : JNI_FALSE);
}
void JniSetBooleanField(JNIEnv* env, jobject obj, jfieldID fid, jboolean v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) StoreSlot(slot, NarrowWrite(Narrow::kBool, v));
}
jbyte JniGetByteField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jbyte>(NarrowRead(Narrow::kByte, LoadSlot(slot)));
}
void JniSetByteField(JNIEnv* env, jobject obj, jfieldID fid, jbyte v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) StoreSlot(slot, NarrowWrite(Narrow::kByte, static_cast<uint8_t>(v)));
}
jchar JniGetCharField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jchar>(LoadSlot(slot) & 0xffffu);
}
void JniSetCharField(JNIEnv* env, jobject obj, jfieldID fid, jchar v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) StoreSlot(slot, NarrowWrite(Narrow::kChar, v));
}
jshort JniGetShortField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jshort>(NarrowRead(Narrow::kShort, LoadSlot(slot)));
}
void JniSetShortField(JNIEnv* env, jobject obj, jfieldID fid, jshort v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) StoreSlot(slot, NarrowWrite(Narrow::kShort, static_cast<uint16_t>(v)));
}

// 整/浮点字段（8 字节槽：Int 取低 32，Long/Double 全 64，Float 低 32 位型）
jint JniGetIntField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jint>(LoadSlot(slot));
}
void JniSetIntField(JNIEnv* env, jobject obj, jfieldID fid, jint v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) StoreSlot(slot, static_cast<uint32_t>(v));
}
jlong JniGetLongField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jlong>(LoadSlot(slot));
}
void JniSetLongField(JNIEnv* env, jobject obj, jfieldID fid, jlong v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) StoreSlot(slot, static_cast<uint64_t>(v));
}
jfloat JniGetFloatField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot == nullptr) return 0.0f;
  uint32_t bits = static_cast<uint32_t>(LoadSlot(slot));
  jfloat out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}
void JniSetFloatField(JNIEnv* env, jobject obj, jfieldID fid, jfloat v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) {
    uint32_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    StoreSlot(slot, bits);
  }
}
jdouble JniGetDoubleField(JNIEnv* env, jobject obj, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot == nullptr) return 0.0;
  uint64_t bits = LoadSlot(slot);
  jdouble out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}
void JniSetDoubleField(JNIEnv* env, jobject obj, jfieldID fid, jdouble v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  uint8_t* slot = (f != nullptr) ? InstanceSlot(e, obj, f) : nullptr;
  if (slot != nullptr) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    StoreSlot(slot, bits);
  }
}

// 静态字段
jobject JniGetStaticObjectField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  if (slot == nullptr) return nullptr;
  return EncodeObj(e, reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(slot->l)));
}
void JniSetStaticObjectField(JNIEnv* env, jclass, jfieldID fid, jobject value) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->ref = DecodeObj(e, value);
}
jboolean JniGetStaticBooleanField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  return slot == nullptr ? JNI_FALSE : ((slot->l & 0xffu) != 0 ? JNI_TRUE : JNI_FALSE);
}
void JniSetStaticBooleanField(JNIEnv* env, jclass, jfieldID fid, jboolean v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->l = (v != 0) ? 1 : 0;
}
jbyte JniGetStaticByteField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jbyte>(slot->l);
}
void JniSetStaticByteField(JNIEnv* env, jclass, jfieldID fid, jbyte v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->l = v;
}
jchar JniGetStaticCharField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jchar>(slot->l & 0xffffu);
}
void JniSetStaticCharField(JNIEnv* env, jclass, jfieldID fid, jchar v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->l = v;
}
jshort JniGetStaticShortField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jshort>(slot->l);
}
void JniSetStaticShortField(JNIEnv* env, jclass, jfieldID fid, jshort v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->l = v;
}
jint JniGetStaticIntField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  return slot == nullptr ? 0 : static_cast<jint>(slot->l);
}
void JniSetStaticIntField(JNIEnv* env, jclass, jfieldID fid, jint v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->l = static_cast<uint32_t>(v);
}
jlong JniGetStaticLongField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  return slot == nullptr ? 0 : slot->l;
}
void JniSetStaticLongField(JNIEnv* env, jclass, jfieldID fid, jlong v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  slot->l = v;
}
jfloat JniGetStaticFloatField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  if (slot == nullptr) return 0.0f;
  uint32_t bits = static_cast<uint32_t>(slot->l);
  jfloat out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}
void JniSetStaticFloatField(JNIEnv* env, jclass, jfieldID fid, jfloat v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  uint32_t bits;
  std::memcpy(&bits, &v, sizeof(bits));
  slot->l = static_cast<int64_t>(bits);
}
jdouble JniGetStaticDoubleField(JNIEnv* env, jclass, jfieldID fid) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  Value* slot = (f != nullptr) ? StaticSlot(e, f) : nullptr;
  if (slot == nullptr) return 0.0;
  uint64_t bits = static_cast<uint64_t>(slot->l);
  jdouble out;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}
void JniSetStaticDoubleField(JNIEnv* env, jclass, jfieldID fid, jdouble v) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fid);
  if (f == nullptr) return;
  Value* slot = StaticSlot(e, f);
  if (slot == nullptr || !EnsureModifiable(e, f)) return;
  uint64_t bits;
  std::memcpy(&bits, &v, sizeof(bits));
  slot->l = static_cast<int64_t>(bits);
}

jfieldID JniGetStaticFieldID(JNIEnv* env, jclass clazz, const char* name, const char* sig) {
  JNIEnvExt* e = Ext(env);
  VClass* k = DecodeClass(e, clazz);
  if (k == nullptr || !EnsureInitialized(e, k)) return nullptr;
  VField* f = FindField(k, name, sig, /*want_static=*/true);
  if (f == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/NoSuchFieldError;",
                         std::string(name != nullptr ? name : "") + " " +
                             std::string(sig != nullptr ? sig : ""));
    return nullptr;
  }
  return EncodeFieldId(f);
}

// ---- 字符串 ----

jstring JniNewStringUTF(JNIEnv* env, const char* bytes) {
  JNIEnvExt* e = Ext(env);
  if (bytes == nullptr) return nullptr;
  std::u16string chars;
  Mutf8ToChars(bytes, &chars);
#if defined(__ANDROID__)
  // TEMP DIAG（NetworkMonitorDecorator 空串来源；事后移除）。
  if (chars.empty()) {
    char hex[3 * 24 + 1];
    int hn = 0;
    for (size_t i = 0; i < 24 && bytes[i] != '\0'; i++) {
      hn += std::snprintf(hex + hn, sizeof(hex) - hn, "%02x ", static_cast<unsigned char>(bytes[i]));
    }
    hex[hn] = '\0';
    __android_log_print(ANDROID_LOG_INFO, "DexVmJniStr", "NewStringUTF empty input len=%zu [%s]",
                        std::strlen(bytes), hex);
  }
#endif
  VString* s = e->vmstate->linker->AllocString(chars);
  if (s == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "NewStringUTF");
    return nullptr;
  }
  return FromHandle<jstring>(e->locals.Add(s));
}

jstring JniNewString(JNIEnv* env, const jchar* unicode, jsize len) {
  JNIEnvExt* e = Ext(env);
  if (unicode == nullptr && len != 0) return nullptr;
  if (len < 0) len = 0;
#if defined(__ANDROID__)
  // TEMP DIAG（NetworkMonitorDecorator 空串来源；事后移除）。
  if (len == 0) {
    __android_log_print(ANDROID_LOG_INFO, "DexVmJniStr", "NewString len=0 ptr=%p",
                        static_cast<const void*>(unicode));
  }
#endif
  std::u16string chars(reinterpret_cast<const char16_t*>(unicode),
                       static_cast<size_t>(len));
  VString* s = e->vmstate->linker->AllocString(chars);
  if (s == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "NewString");
    return nullptr;
  }
  return FromHandle<jstring>(e->locals.Add(s));
}

VString* DecodeString(JNIEnvExt* e, jstring str) {
  ObjHeader* o = DecodeObj(e, str);
  if (o == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "String op on null");
    return nullptr;
  }
  return static_cast<VString*>(o);
}

jsize JniGetStringLength(JNIEnv* env, jstring str) {
  VString* s = DecodeString(Ext(env), str);
  return s == nullptr ? 0 : s->length;
}

const jchar* JniGetStringChars(JNIEnv* env, jstring str, jboolean* isCopy) {
  VString* s = DecodeString(Ext(env), str);
  if (s == nullptr) return nullptr;
  if (isCopy != nullptr) *isCopy = JNI_TRUE;
  size_t n = static_cast<size_t>(s->length);
  jchar* buf = static_cast<jchar*>(std::malloc((n + 1) * sizeof(jchar)));
  if (buf == nullptr) return nullptr;
  std::memcpy(buf, s->chars(), n * sizeof(jchar));
  buf[n] = 0;
  return buf;
}

void JniReleaseStringChars(JNIEnv*, jstring, const jchar* chars) {
  std::free(const_cast<jchar*>(chars));
}

jsize JniGetStringUTFLength(JNIEnv* env, jstring str) {
  VString* s = DecodeString(Ext(env), str);
  return s == nullptr ? 0 : static_cast<jsize>(s->ToModifiedUtf8().size());
}

const char* JniGetStringUTFChars(JNIEnv* env, jstring str, jboolean* isCopy) {
  VString* s = DecodeString(Ext(env), str);
  if (s == nullptr) return nullptr;
  if (isCopy != nullptr) *isCopy = JNI_TRUE;
  std::string utf8 = s->ToModifiedUtf8();  // MUTF-8（内嵌 NUL = C0 80）
  char* buf = static_cast<char*>(std::malloc(utf8.size() + 1));
  if (buf == nullptr) return nullptr;
  std::memcpy(buf, utf8.data(), utf8.size());
  buf[utf8.size()] = 0;
  return buf;
}

void JniReleaseStringUTFChars(JNIEnv*, jstring, const char* chars) {
  std::free(const_cast<char*>(chars));
}

// 字符串区域（jni_internal.cc:2216-2262）：越界抛 StringIndexOutOfBoundsException
bool CheckStringRegion(JNIEnvExt* e, VString* s, jsize start, jsize len) {
  if (start < 0 || len < 0 || start > s->length || len > s->length - start) {
    e->vmstate->ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                         "start=" + std::to_string(start) + ", length=" + std::to_string(len) +
                             ", string length=" + std::to_string(s->length));
    return false;
  }
  return true;
}

void JniGetStringRegion(JNIEnv* env, jstring str, jsize start, jsize len, jchar* buf) {
  JNIEnvExt* e = Ext(env);
  VString* s = DecodeString(e, str);
  if (s == nullptr || buf == nullptr) return;
  if (!CheckStringRegion(e, s, start, len)) return;
  std::memcpy(buf, s->chars() + start, static_cast<size_t>(len) * sizeof(jchar));
}

void JniGetStringUTFRegion(JNIEnv* env, jstring str, jsize start, jsize len, char* buf) {
  JNIEnvExt* e = Ext(env);
  VString* s = DecodeString(e, str);
  if (s == nullptr) return;
  if (!CheckStringRegion(e, s, start, len)) return;
  if (len == 0 && buf == nullptr) return;  // jni_internal.cc:2246-2248
  std::string mutf8;
  CharsToMutf8(s->chars() + start, static_cast<size_t>(len), &mutf8);
  if (buf == nullptr) return;
  std::memcpy(buf, mutf8.data(), mutf8.size());
  buf[mutf8.size()] = '\0';  // ART：NUL 终止（jni_internal.cc:2255）
}

// ---- 数组 ----

jsize JniGetArrayLength(JNIEnv* env, jarray array) {
  VArray* a = DecodeArray(Ext(env), array, "GetArrayLength");
  return a == nullptr ? 0 : a->length;
}

jobjectArray JniNewObjectArray(JNIEnv* env, jsize length, jclass elementClass,
                               jobject initialElement) {
  JNIEnvExt* e = Ext(env);
  if (length < 0) {
    e->vmstate->ThrowNew("Ljava/lang/NegativeArraySizeException;", std::to_string(length));
    return nullptr;
  }
  VClass* comp = DecodeClass(e, elementClass);
  ClassLinker* linker = e->vmstate->linker;
  VClass* ac = (comp != nullptr) ? linker->FindArrayClass(comp) : nullptr;
  VArray* arr = (ac != nullptr) ? linker->AllocArray(ac, length) : nullptr;
  if (arr == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "NewObjectArray");
    return nullptr;
  }
  ObjHeader* init = DecodeObj(e, initialElement);
  for (jsize i = 0; i < length; i++) {
    StoreSlot(arr->data() + static_cast<size_t>(i) * 8,
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(init)));
  }
  return FromHandle<jobjectArray>(e->locals.Add(arr));
}

jobject JniGetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index) {
  JNIEnvExt* e = Ext(env);
  VArray* arr = DecodeArray(e, array, "GetObjectArrayElement");
  if (arr == nullptr) return nullptr;
  if (index < 0 || index >= arr->length) {
    dexvm::runtime::ThrowArrayIndexOutOfBoundsException(*e->vmstate, index, arr->length);
    return nullptr;
  }
  return EncodeObj(e, reinterpret_cast<ObjHeader*>(
                          static_cast<uintptr_t>(LoadSlot(arr->data() + index * 8))));
}

void JniSetObjectArrayElement(JNIEnv* env, jobjectArray array, jsize index, jobject value) {
  JNIEnvExt* e = Ext(env);
  VArray* arr = DecodeArray(e, array, "SetObjectArrayElement");
  if (arr == nullptr) return;
  if (index < 0 || index >= arr->length) {
    dexvm::runtime::ThrowArrayIndexOutOfBoundsException(*e->vmstate, index, arr->length);
    return;
  }
  ObjHeader* val = DecodeObj(e, value);
  if (val != nullptr && arr->klass != nullptr && arr->klass->component_type != nullptr &&
      val->klass != nullptr &&
      !arr->klass->component_type->IsAssignableFrom(val->klass)) {
    e->vmstate->ThrowNew("Ljava/lang/ArrayStoreException;",
                         val->klass->descriptor + " into " + arr->klass->descriptor);
    return;
  }
  StoreSlot(arr->data() + index * 8, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(val)));
}

#define DEFINE_ARRAY_FAMILY(Type, Name, Prim, NarrowKind)                                     \
  Type##Array JniNew##Name##Array(JNIEnv* env, jsize length) {                                 \
    return (Type##Array) NewPrimArray<Prim>(Ext(env), length);                                 \
  }                                                                                           \
  Type* JniGet##Name##ArrayElements(JNIEnv* env, Type##Array array, jboolean* isCopy) {        \
    JNIEnvExt* e = Ext(env);                                                                  \
    VArray* a = DecodeArray(e, array, "Get" #Name "ArrayElements");                           \
    return a == nullptr ? nullptr : GetArrayElementsT<Type, NarrowKind>(e, a, isCopy);         \
  }                                                                                           \
  void JniRelease##Name##ArrayElements(JNIEnv* env, Type##Array array, Type* elems, jint mode) { \
    JNIEnvExt* e = Ext(env);                                                                  \
    VArray* a = DecodeArray(e, array, "Release" #Name "ArrayElements");                        \
    if (a != nullptr) ReleaseArrayElementsT<Type, NarrowKind>(a, elems, mode);                 \
    else std::free(elems);                                                                    \
  }                                                                                           \
  void JniGet##Name##ArrayRegion(JNIEnv* env, Type##Array array, jsize start, jsize len,       \
                                 Type* buf) {                                                 \
    JNIEnvExt* e = Ext(env);                                                                  \
    VArray* a = DecodeArray(e, array, "Get" #Name "ArrayRegion");                             \
    if (a != nullptr) GetArrayRegionT<Type, NarrowKind>(e, a, start, len, buf);                \
  }                                                                                           \
  void JniSet##Name##ArrayRegion(JNIEnv* env, Type##Array array, jsize start, jsize len,       \
                                 const Type* buf) {                                           \
    JNIEnvExt* e = Ext(env);                                                                  \
    VArray* a = DecodeArray(e, array, "Set" #Name "ArrayRegion");                             \
    if (a != nullptr) SetArrayRegionT<Type, NarrowKind>(e, a, start, len, buf);                \
  }

DEFINE_ARRAY_FAMILY(jboolean, Boolean, 'Z', Narrow::kBool)
DEFINE_ARRAY_FAMILY(jbyte, Byte, 'B', Narrow::kByte)
DEFINE_ARRAY_FAMILY(jchar, Char, 'C', Narrow::kChar)
DEFINE_ARRAY_FAMILY(jshort, Short, 'S', Narrow::kShort)
DEFINE_ARRAY_FAMILY(jint, Int, 'I', Narrow::kWord)
DEFINE_ARRAY_FAMILY(jlong, Long, 'J', Narrow::kWide)
DEFINE_ARRAY_FAMILY(jfloat, Float, 'F', Narrow::kWord)
DEFINE_ARRAY_FAMILY(jdouble, Double, 'D', Narrow::kWide)

// ---- Critical（docs 15.9/15.11 恒 pin；jni_internal.cc:2501-2560）----
// VM 数组为 8 字节元素槽：恒返回按元素宽度打包的 pin 视图（is_copy=JNI_FALSE、
// 无复制回退语义上的"pin"），Release 时同步修改（mode 语义同 Release<Type>ArrayElements）。

void* JniGetPrimitiveArrayCritical(JNIEnv* env, jarray array, jboolean* isCopy) {
  JNIEnvExt* e = Ext(env);
  VArray* a = DecodeArray(e, array, "GetPrimitiveArrayCritical");
  if (a == nullptr) return nullptr;
  VClass* ac = a->klass;
  if (ac == nullptr || !ac->is_array || ac->component_type == nullptr ||
      !ac->component_type->is_primitive) {
    e->Warn("GetPrimitiveArrayCritical: expected primitive array");
    return nullptr;
  }
  if (isCopy != nullptr) *isCopy = JNI_FALSE;  // 恒 pin
  for (auto& pin : e->critical_pins) {
    if (pin.array == a && pin.packed != nullptr) {
      pin.refs++;
      return pin.packed;
    }
  }
  const char prim = ac->component_type->primitive_type;
  const Narrow k = PrimNarrow(prim);
  const size_t elem = PrimElemSize(prim);
  const size_t n = static_cast<size_t>(a->length);
  void* buf = std::malloc(n * elem + 1);
  if (buf == nullptr) return nullptr;
  uint8_t* out = static_cast<uint8_t*>(buf);
  for (size_t i = 0; i < n; i++) {
    uint64_t v = NarrowRead(k, LoadSlot(a->data() + i * 8));
    std::memcpy(out + i * elem, &v, elem);
  }
  CriticalPin pin;
  pin.array = a;
  pin.packed = buf;
  pin.length = n;
  pin.primitive = prim;
  pin.refs = 1;
  e->critical_pins.push_back(pin);
  return buf;
}

void JniReleasePrimitiveArrayCritical(JNIEnv* env, jarray array, void* carray, jint mode) {
  JNIEnvExt* e = Ext(env);
  VArray* a = DecodeArray(e, array, "ReleasePrimitiveArrayCritical");
  if (a == nullptr) return;
  for (size_t pi = 0; pi < e->critical_pins.size(); pi++) {
    CriticalPin& pin = e->critical_pins[pi];
    if (pin.array != a || pin.packed == nullptr) continue;
    if (carray != nullptr && carray != pin.packed) {
      e->Warn("ReleasePrimitiveArrayCritical: unexpected pointer");
      return;
    }
    const Narrow k = PrimNarrow(pin.primitive);
    const size_t elem = PrimElemSize(pin.primitive);
    const size_t n = pin.length < static_cast<size_t>(a->length) ? pin.length
                                                                : static_cast<size_t>(a->length);
    if (mode != JNI_ABORT) {  // 同步修改（docs 15.9）
      const uint8_t* in = static_cast<const uint8_t*>(pin.packed);
      for (size_t i = 0; i < n; i++) {
        uint64_t v = 0;
        std::memcpy(&v, in + i * elem, elem);
        StoreSlot(a->data() + i * 8, NarrowWrite(k, v));
      }
    }
    if (mode != JNI_COMMIT) {
      if (pin.refs > 0) pin.refs--;
      if (pin.refs == 0) {
        std::free(pin.packed);
        e->critical_pins.erase(e->critical_pins.begin() + static_cast<long>(pi));
      }
    }
    return;
  }
  e->Warn("ReleasePrimitiveArrayCritical: no matching pin");
}

const jchar* JniGetStringCritical(JNIEnv* env, jstring str, jboolean* isCopy) {
  JNIEnvExt* e = Ext(env);
  VString* s = DecodeString(e, str);
  if (s == nullptr) return nullptr;
  // 恒 pin：非移动堆 + chars 内联对象（docs 15.9 "恒返回 backing pointer"）
  if (isCopy != nullptr) *isCopy = JNI_FALSE;
  return s->chars();
}

void JniReleaseStringCritical(JNIEnv* env, jstring str, const jchar* chars) {
  JNIEnvExt* e = Ext(env);
  VString* s = DecodeString(e, str);
  if (s == nullptr) return;
  if (chars != s->chars()) e->Warn("ReleaseStringCritical: unexpected pointer");
}

// ---- Monitor（与解释器 monitor-enter/exit 一致，M1 重入计数）----

jint JniMonitorEnter(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "monitor-enter on null");
    return JNI_ERR;
  }
  o->monitor += 1;
  return 0;
}

jint JniMonitorExit(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) {
    dexvm::runtime::ThrowNullPointerException(*e->vmstate, "monitor-exit on null");
    return JNI_ERR;
  }
  if (o->monitor == 0) {
    e->vmstate->ThrowNew("Ljava/lang/IllegalMonitorStateException;", "monitor-exit count=0");
    return JNI_ERR;
  }
  o->monitor -= 1;
  return 0;
}

// ---- RegisterNatives / UnregisterNatives（native_manager.cc 实现）----

jint JniRegisterNatives(JNIEnv* env, jclass clazz, const JNINativeMethod* methods,
                        jint nMethods) {
  JNIEnvExt* e = Ext(env);
  return RegisterNativesImpl(e, DecodeClass(e, clazz), methods, nMethods);
}

jint JniUnregisterNatives(JNIEnv* env, jclass clazz) {
  JNIEnvExt* e = Ext(env);
  return UnregisterNativesImpl(e, DecodeClass(e, clazz));
}

// ---- VM ----

jint JniGetJavaVM(JNIEnv* env, JavaVM** vm) {
  if (vm != nullptr) *vm = Ext(env)->vm;
  return JNI_OK;
}

jobjectRefType JniGetObjectRefType(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  if (obj == nullptr) return JNIInvalidRefType;
  Handle h = ToHandle(obj);
  switch (GetHandleKind(h)) {
    case RefKind::kLocal:
      return e->locals.Contains(h) ? JNILocalRefType : JNIInvalidRefType;
    case RefKind::kGlobal: {
      RefKind k = e->vm->globals.Classify(h);
      return k == RefKind::kGlobal ? JNIGlobalRefType : JNIInvalidRefType;
    }
    case RefKind::kWeakGlobal: {
      // 清除后的 weak 仍是 weak-global entry（docs 15.15）
      RefKind k = e->vm->globals.Classify(h);
      return k == RefKind::kWeakGlobal ? JNIWeakGlobalRefType : JNIInvalidRefType;
    }
    default:
      return JNIInvalidRefType;
  }
}

jobject JniNewWeakGlobalRef(JNIEnv* env, jobject obj) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeObj(e, obj);
  if (o == nullptr) return nullptr;
  return FromHandle<jobject>(e->vm->globals.AddWeak(o));
}

void JniDeleteWeakGlobalRef(JNIEnv* env, jobject ref) {
  JNIEnvExt* e = Ext(env);
  if (ref == nullptr) return;
  if (!e->vm->globals.Remove(ToHandle(ref))) e->Warn("DeleteWeakGlobalRef: stale reference");
}

// ---- Direct buffer（docs 15.9；M2 后置：告警桩）----

jobject JniNewDirectByteBuffer(JNIEnv* env, void*, jlong) {
  Ext(env)->Warn("NewDirectByteBuffer: not supported (M2 后置)");
  return nullptr;
}
void* JniGetDirectBufferAddress(JNIEnv* env, jobject) {
  Ext(env)->Warn("GetDirectBufferAddress: not supported (M2 后置)");
  return nullptr;
}
jlong JniGetDirectBufferCapacity(JNIEnv* env, jobject) {
  Ext(env)->Warn("GetDirectBufferCapacity: not supported (M2 后置)");
  return -1;
}

// ---- JavaVM 函数表（M1 简化：单 VM / 主 Env）----

jint JniDestroyJavaVM(JavaVM*) { return JNI_OK; }

jint JniAttachCurrentThread(JavaVM* vm, JNIEnv** p_env, void*) {
  JavaVMExt* v = static_cast<JavaVMExt*>(vm);
  if (v->attached.empty()) return JNI_ERR;  // M1：不隐式造 Env（M2：GuestTask peer）
  if (p_env != nullptr) *p_env = v->attached.front();
  return JNI_OK;
}

jint JniDetachCurrentThread(JavaVM*) { return JNI_OK; }  // M2：ThreadList 注销

jint JniGetEnv(JavaVM* vm, void** p_env, jint version) {
  JavaVMExt* v = static_cast<JavaVMExt*>(vm);
  if (version > JNI_VERSION_1_8) return JNI_EVERSION;
  if (v->attached.empty()) return JNI_EDETACHED;
  if (p_env != nullptr) *p_env = v->attached.front();
  return JNI_OK;
}

jint JniAttachCurrentThreadAsDaemon(JavaVM* vm, JNIEnv** p_env, void* thr_args) {
  return JniAttachCurrentThread(vm, p_env, thr_args);
}

// ---- 函数表（C++20 指定初始化：与真实 jni.h 槽位顺序一致）----

const JNINativeInterface kJniTable = {
    .reserved0 = nullptr,
    .reserved1 = nullptr,
    .reserved2 = nullptr,
    .reserved3 = nullptr,
    .GetVersion = JniGetVersion,
    .DefineClass = JniDefineClass,
    .FindClass = JniFindClass,
    .FromReflectedMethod = JniFromReflectedMethod,
    .FromReflectedField = JniFromReflectedField,
    .ToReflectedMethod = JniToReflectedMethod,
    .GetSuperclass = JniGetSuperclass,
    .IsAssignableFrom = JniIsAssignableFrom,
    .ToReflectedField = JniToReflectedField,
    .Throw = JniThrow,
    .ThrowNew = JniThrowNew,
    .ExceptionOccurred = JniExceptionOccurred,
    .ExceptionDescribe = JniExceptionDescribe,
    .ExceptionClear = JniExceptionClear,
    .FatalError = JniFatalError,
    .PushLocalFrame = JniPushLocalFrame,
    .PopLocalFrame = JniPopLocalFrame,
    .NewGlobalRef = JniNewGlobalRef,
    .DeleteGlobalRef = JniDeleteGlobalRef,
    .DeleteLocalRef = JniDeleteLocalRef,
    .IsSameObject = JniIsSameObject,
    .NewLocalRef = JniNewLocalRef,
    .EnsureLocalCapacity = JniEnsureLocalCapacity,
    .AllocObject = JniAllocObject,
    .NewObject = JniNewObject,
    .NewObjectV = JniNewObjectV,
    .NewObjectA = JniNewObjectA,
    .GetObjectClass = JniGetObjectClass,
    .IsInstanceOf = JniIsInstanceOf,
    .GetMethodID = JniGetMethodID,
    .CallObjectMethod = JniCallObjectMethod,
    .CallObjectMethodV = JniCallObjectMethodV,
    .CallObjectMethodA = JniCallObjectMethodA,
    .CallBooleanMethod = JniCallBooleanMethod,
    .CallBooleanMethodV = JniCallBooleanMethodV,
    .CallBooleanMethodA = JniCallBooleanMethodA,
    .CallByteMethod = JniCallByteMethod,
    .CallByteMethodV = JniCallByteMethodV,
    .CallByteMethodA = JniCallByteMethodA,
    .CallCharMethod = JniCallCharMethod,
    .CallCharMethodV = JniCallCharMethodV,
    .CallCharMethodA = JniCallCharMethodA,
    .CallShortMethod = JniCallShortMethod,
    .CallShortMethodV = JniCallShortMethodV,
    .CallShortMethodA = JniCallShortMethodA,
    .CallIntMethod = JniCallIntMethod,
    .CallIntMethodV = JniCallIntMethodV,
    .CallIntMethodA = JniCallIntMethodA,
    .CallLongMethod = JniCallLongMethod,
    .CallLongMethodV = JniCallLongMethodV,
    .CallLongMethodA = JniCallLongMethodA,
    .CallFloatMethod = JniCallFloatMethod,
    .CallFloatMethodV = JniCallFloatMethodV,
    .CallFloatMethodA = JniCallFloatMethodA,
    .CallDoubleMethod = JniCallDoubleMethod,
    .CallDoubleMethodV = JniCallDoubleMethodV,
    .CallDoubleMethodA = JniCallDoubleMethodA,
    .CallVoidMethod = JniCallVoidMethod,
    .CallVoidMethodV = JniCallVoidMethodV,
    .CallVoidMethodA = JniCallVoidMethodA,
    .CallNonvirtualObjectMethod = JniCallNonvirtualObjectMethod,
    .CallNonvirtualObjectMethodV = JniCallNonvirtualObjectMethodV,
    .CallNonvirtualObjectMethodA = JniCallNonvirtualObjectMethodA,
    .CallNonvirtualBooleanMethod = JniCallNonvirtualBooleanMethod,
    .CallNonvirtualBooleanMethodV = JniCallNonvirtualBooleanMethodV,
    .CallNonvirtualBooleanMethodA = JniCallNonvirtualBooleanMethodA,
    .CallNonvirtualByteMethod = JniCallNonvirtualByteMethod,
    .CallNonvirtualByteMethodV = JniCallNonvirtualByteMethodV,
    .CallNonvirtualByteMethodA = JniCallNonvirtualByteMethodA,
    .CallNonvirtualCharMethod = JniCallNonvirtualCharMethod,
    .CallNonvirtualCharMethodV = JniCallNonvirtualCharMethodV,
    .CallNonvirtualCharMethodA = JniCallNonvirtualCharMethodA,
    .CallNonvirtualShortMethod = JniCallNonvirtualShortMethod,
    .CallNonvirtualShortMethodV = JniCallNonvirtualShortMethodV,
    .CallNonvirtualShortMethodA = JniCallNonvirtualShortMethodA,
    .CallNonvirtualIntMethod = JniCallNonvirtualIntMethod,
    .CallNonvirtualIntMethodV = JniCallNonvirtualIntMethodV,
    .CallNonvirtualIntMethodA = JniCallNonvirtualIntMethodA,
    .CallNonvirtualLongMethod = JniCallNonvirtualLongMethod,
    .CallNonvirtualLongMethodV = JniCallNonvirtualLongMethodV,
    .CallNonvirtualLongMethodA = JniCallNonvirtualLongMethodA,
    .CallNonvirtualFloatMethod = JniCallNonvirtualFloatMethod,
    .CallNonvirtualFloatMethodV = JniCallNonvirtualFloatMethodV,
    .CallNonvirtualFloatMethodA = JniCallNonvirtualFloatMethodA,
    .CallNonvirtualDoubleMethod = JniCallNonvirtualDoubleMethod,
    .CallNonvirtualDoubleMethodV = JniCallNonvirtualDoubleMethodV,
    .CallNonvirtualDoubleMethodA = JniCallNonvirtualDoubleMethodA,
    .CallNonvirtualVoidMethod = JniCallNonvirtualVoidMethod,
    .CallNonvirtualVoidMethodV = JniCallNonvirtualVoidMethodV,
    .CallNonvirtualVoidMethodA = JniCallNonvirtualVoidMethodA,
    .GetFieldID = JniGetFieldID,
    .GetObjectField = JniGetObjectField,
    .GetBooleanField = JniGetBooleanField,
    .GetByteField = JniGetByteField,
    .GetCharField = JniGetCharField,
    .GetShortField = JniGetShortField,
    .GetIntField = JniGetIntField,
    .GetLongField = JniGetLongField,
    .GetFloatField = JniGetFloatField,
    .GetDoubleField = JniGetDoubleField,
    .SetObjectField = JniSetObjectField,
    .SetBooleanField = JniSetBooleanField,
    .SetByteField = JniSetByteField,
    .SetCharField = JniSetCharField,
    .SetShortField = JniSetShortField,
    .SetIntField = JniSetIntField,
    .SetLongField = JniSetLongField,
    .SetFloatField = JniSetFloatField,
    .SetDoubleField = JniSetDoubleField,
    .GetStaticMethodID = JniGetStaticMethodID,
    .CallStaticObjectMethod = JniCallStaticObjectMethod,
    .CallStaticObjectMethodV = JniCallStaticObjectMethodV,
    .CallStaticObjectMethodA = JniCallStaticObjectMethodA,
    .CallStaticBooleanMethod = JniCallStaticBooleanMethod,
    .CallStaticBooleanMethodV = JniCallStaticBooleanMethodV,
    .CallStaticBooleanMethodA = JniCallStaticBooleanMethodA,
    .CallStaticByteMethod = JniCallStaticByteMethod,
    .CallStaticByteMethodV = JniCallStaticByteMethodV,
    .CallStaticByteMethodA = JniCallStaticByteMethodA,
    .CallStaticCharMethod = JniCallStaticCharMethod,
    .CallStaticCharMethodV = JniCallStaticCharMethodV,
    .CallStaticCharMethodA = JniCallStaticCharMethodA,
    .CallStaticShortMethod = JniCallStaticShortMethod,
    .CallStaticShortMethodV = JniCallStaticShortMethodV,
    .CallStaticShortMethodA = JniCallStaticShortMethodA,
    .CallStaticIntMethod = JniCallStaticIntMethod,
    .CallStaticIntMethodV = JniCallStaticIntMethodV,
    .CallStaticIntMethodA = JniCallStaticIntMethodA,
    .CallStaticLongMethod = JniCallStaticLongMethod,
    .CallStaticLongMethodV = JniCallStaticLongMethodV,
    .CallStaticLongMethodA = JniCallStaticLongMethodA,
    .CallStaticFloatMethod = JniCallStaticFloatMethod,
    .CallStaticFloatMethodV = JniCallStaticFloatMethodV,
    .CallStaticFloatMethodA = JniCallStaticFloatMethodA,
    .CallStaticDoubleMethod = JniCallStaticDoubleMethod,
    .CallStaticDoubleMethodV = JniCallStaticDoubleMethodV,
    .CallStaticDoubleMethodA = JniCallStaticDoubleMethodA,
    .CallStaticVoidMethod = JniCallStaticVoidMethod,
    .CallStaticVoidMethodV = JniCallStaticVoidMethodV,
    .CallStaticVoidMethodA = JniCallStaticVoidMethodA,
    .GetStaticFieldID = JniGetStaticFieldID,
    .GetStaticObjectField = JniGetStaticObjectField,
    .GetStaticBooleanField = JniGetStaticBooleanField,
    .GetStaticByteField = JniGetStaticByteField,
    .GetStaticCharField = JniGetStaticCharField,
    .GetStaticShortField = JniGetStaticShortField,
    .GetStaticIntField = JniGetStaticIntField,
    .GetStaticLongField = JniGetStaticLongField,
    .GetStaticFloatField = JniGetStaticFloatField,
    .GetStaticDoubleField = JniGetStaticDoubleField,
    .SetStaticObjectField = JniSetStaticObjectField,
    .SetStaticBooleanField = JniSetStaticBooleanField,
    .SetStaticByteField = JniSetStaticByteField,
    .SetStaticCharField = JniSetStaticCharField,
    .SetStaticShortField = JniSetStaticShortField,
    .SetStaticIntField = JniSetStaticIntField,
    .SetStaticLongField = JniSetStaticLongField,
    .SetStaticFloatField = JniSetStaticFloatField,
    .SetStaticDoubleField = JniSetStaticDoubleField,
    .NewString = JniNewString,
    .GetStringLength = JniGetStringLength,
    .GetStringChars = JniGetStringChars,
    .ReleaseStringChars = JniReleaseStringChars,
    .NewStringUTF = JniNewStringUTF,
    .GetStringUTFLength = JniGetStringUTFLength,
    .GetStringUTFChars = JniGetStringUTFChars,
    .ReleaseStringUTFChars = JniReleaseStringUTFChars,
    .GetArrayLength = JniGetArrayLength,
    .NewObjectArray = JniNewObjectArray,
    .GetObjectArrayElement = JniGetObjectArrayElement,
    .SetObjectArrayElement = JniSetObjectArrayElement,
    .NewBooleanArray = JniNewBooleanArray,
    .NewByteArray = JniNewByteArray,
    .NewCharArray = JniNewCharArray,
    .NewShortArray = JniNewShortArray,
    .NewIntArray = JniNewIntArray,
    .NewLongArray = JniNewLongArray,
    .NewFloatArray = JniNewFloatArray,
    .NewDoubleArray = JniNewDoubleArray,
    .GetBooleanArrayElements = JniGetBooleanArrayElements,
    .GetByteArrayElements = JniGetByteArrayElements,
    .GetCharArrayElements = JniGetCharArrayElements,
    .GetShortArrayElements = JniGetShortArrayElements,
    .GetIntArrayElements = JniGetIntArrayElements,
    .GetLongArrayElements = JniGetLongArrayElements,
    .GetFloatArrayElements = JniGetFloatArrayElements,
    .GetDoubleArrayElements = JniGetDoubleArrayElements,
    .ReleaseBooleanArrayElements = JniReleaseBooleanArrayElements,
    .ReleaseByteArrayElements = JniReleaseByteArrayElements,
    .ReleaseCharArrayElements = JniReleaseCharArrayElements,
    .ReleaseShortArrayElements = JniReleaseShortArrayElements,
    .ReleaseIntArrayElements = JniReleaseIntArrayElements,
    .ReleaseLongArrayElements = JniReleaseLongArrayElements,
    .ReleaseFloatArrayElements = JniReleaseFloatArrayElements,
    .ReleaseDoubleArrayElements = JniReleaseDoubleArrayElements,
    .GetBooleanArrayRegion = JniGetBooleanArrayRegion,
    .GetByteArrayRegion = JniGetByteArrayRegion,
    .GetCharArrayRegion = JniGetCharArrayRegion,
    .GetShortArrayRegion = JniGetShortArrayRegion,
    .GetIntArrayRegion = JniGetIntArrayRegion,
    .GetLongArrayRegion = JniGetLongArrayRegion,
    .GetFloatArrayRegion = JniGetFloatArrayRegion,
    .GetDoubleArrayRegion = JniGetDoubleArrayRegion,
    .SetBooleanArrayRegion = JniSetBooleanArrayRegion,
    .SetByteArrayRegion = JniSetByteArrayRegion,
    .SetCharArrayRegion = JniSetCharArrayRegion,
    .SetShortArrayRegion = JniSetShortArrayRegion,
    .SetIntArrayRegion = JniSetIntArrayRegion,
    .SetLongArrayRegion = JniSetLongArrayRegion,
    .SetFloatArrayRegion = JniSetFloatArrayRegion,
    .SetDoubleArrayRegion = JniSetDoubleArrayRegion,
    .RegisterNatives = JniRegisterNatives,
    .UnregisterNatives = JniUnregisterNatives,
    .MonitorEnter = JniMonitorEnter,
    .MonitorExit = JniMonitorExit,
    .GetJavaVM = JniGetJavaVM,
    .GetStringRegion = JniGetStringRegion,
    .GetStringUTFRegion = JniGetStringUTFRegion,
    .GetPrimitiveArrayCritical = JniGetPrimitiveArrayCritical,
    .ReleasePrimitiveArrayCritical = JniReleasePrimitiveArrayCritical,
    .GetStringCritical = JniGetStringCritical,
    .ReleaseStringCritical = JniReleaseStringCritical,
    .NewWeakGlobalRef = JniNewWeakGlobalRef,
    .DeleteWeakGlobalRef = JniDeleteWeakGlobalRef,
    .ExceptionCheck = JniExceptionCheck,
    .NewDirectByteBuffer = JniNewDirectByteBuffer,
    .GetDirectBufferAddress = JniGetDirectBufferAddress,
    .GetDirectBufferCapacity = JniGetDirectBufferCapacity,
    .GetObjectRefType = JniGetObjectRefType,
};

const JNIInvokeInterface kVmTable = {
    .reserved0 = nullptr,
    .reserved1 = nullptr,
    .reserved2 = nullptr,
    .DestroyJavaVM = JniDestroyJavaVM,
    .AttachCurrentThread = JniAttachCurrentThread,
    .DetachCurrentThread = JniDetachCurrentThread,
    .GetEnv = JniGetEnv,
    .AttachCurrentThreadAsDaemon = JniAttachCurrentThreadAsDaemon,
};

}  // namespace

// =====================================================================
// JNIEnvExt / JavaVMExt + 编解码 + 公共辅助
// =====================================================================

void JNIEnvExt::Warn(const std::string& msg) {
  warning_sink += msg;
  warning_sink += '\n';
  std::fprintf(stderr, "JNI WARNING: %s\n", msg.c_str());  // ART：日志 + dump 栈
}

void JavaVMExt::Attach(JNIEnvExt* env) {
  env->vm = this;
  attached.push_back(env);
}

void JavaVMExt::Detach(JNIEnvExt* env) {
  for (auto it = attached.begin(); it != attached.end(); ++it) {
    if (*it == env) {
      attached.erase(it);
      return;
    }
  }
}

void InitJavaVMExt(JavaVMExt* vm) { vm->functions = &kVmTable; }

void InitJNIEnvExt(JNIEnvExt* env, JavaVMExt* vm, VMState* state) {
  env->functions = &kJniTable;
  env->vmstate = state;
  env->vm = vm;
  if (vm != nullptr) vm->Attach(env);
  RegisterEnvForState(state, env);  // 解释器 hook：VMState -> JNIEnvExt
  // 汇合点：解释器对 jni_native_fn 方法的派发经此钩子走 JNI trampoline
  runtime::SetNativeCallHook(static_cast<runtime::NativeCallHook>(&InvokeNativeTrampoline));
}

jobject EncodeJObject(JNIEnvExt* env, ObjHeader* obj) { return EncodeObj(env, obj); }

ObjHeader* DecodeJObject(JNIEnvExt* env, jobject ref) { return DecodeObj(env, ref); }

// method/field ID = VMethod*/VField* 的不透明编码（EncodeArtMethod/EncodeArtField 等价物）
jmethodID EncodeMethodId(VMethod* m) { return reinterpret_cast<jmethodID>(m); }

VMethod* DecodeMethodId(jmethodID id) { return reinterpret_cast<VMethod*>(id); }

jfieldID EncodeFieldId(VField* f) { return reinterpret_cast<jfieldID>(f); }

VField* DecodeFieldId(jfieldID id) { return reinterpret_cast<VField*>(id); }

// FindClass loader 优先级（jni_internal.cc:393-420 GetClassLoader）
VObject* GetEffectiveClassLoader(JNIEnvExt* e) {
  // 1. nativeLoad/Runtime.nativeLoad 的 ClassLoader override（JNI_OnLoad 期间）
  if (e->class_loader != nullptr) return e->class_loader;
  // 2. 当前帧方法声明类的 loader
  if (e->vmstate != nullptr && !e->vmstate->frame_stack.empty()) {
    const dexvm::runtime::Frame* f = e->vmstate->frame_stack.back();
    if (f != nullptr && f->method != nullptr && f->method->declaring != nullptr) {
      return f->method->declaring->class_loader;
    }
  }
  // 3. system loader（M2 无）-> 4. boot（nullptr）
  return nullptr;
}

// NormalizeJniClassDescriptor（docs 15.6）：dot -> slash；数组/基本类型直通
std::string NormalizeJniClassDescriptor(const char* name) {
  std::string desc;
  if (name == nullptr) return desc;
  if (name[0] == '[' || name[0] == 'L') {
    desc = name;
  } else if (std::strchr("VZBSCIJFD", name[0]) != nullptr && name[1] == 0) {
    desc = name;
  } else {
    desc.reserve(std::strlen(name) + 2);
    for (const char* p = name; *p != 0; p++) desc.push_back(*p == '.' ? '/' : *p);
    if (desc == "int") desc = "I";
    else if (desc == "long") desc = "J";
    else if (desc == "float") desc = "F";
    else if (desc == "double") desc = "D";
    else if (desc == "boolean") desc = "Z";
    else if (desc == "byte") desc = "B";
    else if (desc == "short") desc = "S";
    else if (desc == "char") desc = "C";
    else if (desc == "void") desc = "V";
    else desc = "L" + desc + ";";
  }
  return desc;
}

NativeCallScope::NativeCallScope(JNIEnvExt* env, jint capacity) : env_(env) {
  (void)capacity;
  env_->locals.PushFrame();
}

NativeCallScope::~NativeCallScope() { env_->locals.PopFrame(); }

}  // namespace dexvm::jni
