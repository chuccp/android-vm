// 反射 ID 面实现（docs 05 章 15.8；对照 jni_internal.cc:727-780 与
// mirror::Method/Constructor/Field 的 artMethod/artField 槽）
//
// java/lang/reflect/{Method,Constructor,Field} 骨架：
//   实例区 16 字节：artMethod/artField:J @0（VMethod*/VField* 指针位型）、
//   declaringClass:Ljava/lang/Class; @8。经 ClassLinker::RegisterBuiltinClass
//   登记（kInitialized），不动 core/builtins.*。
#include "jni/reflection.h"

#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "core/class_linker.h"
#include "core/exception.h"

namespace dexvm::jni {

namespace {

using dexvm::runtime::ClassLinker;
using dexvm::runtime::ClassStatus;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;

constexpr uint32_t kAccPublic = 0x1;

inline JNIEnvExt* Ext(JNIEnv* env) { return static_cast<JNIEnvExt*>(env); }

inline uint64_t LoadSlot(const void* p) {
  uint64_t v;
  std::memcpy(&v, p, sizeof(v));
  return v;
}

inline void StoreSlot(void* p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }

VClass* MakeMirror(ClassLinker* linker, VClass* class_class, VClass* object, const char* descriptor) {
  void* mem = linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->status = ClassStatus::kInitialized;
  k->descriptor = descriptor;
  k->super_class = object;
  k->access_flags = kAccPublic;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + 16;  // artX @0 + declaringClass @8
  k->klass = class_class;

  VField art;
  art.declaring = k;
  // Field 镜像存 artField，Method/Constructor 镜像存 artMethod
  art.name = (std::strstr(descriptor, "Field;") != nullptr) ? "artField" : "artMethod";
  art.type_descriptor = "J";
  art.access_flags = kAccPublic;
  art.offset = 0;
  k->instance_fields.push_back(std::move(art));

  VField decl;
  decl.declaring = k;
  decl.name = "declaringClass";
  decl.type_descriptor = "Ljava/lang/Class;";
  decl.access_flags = kAccPublic;
  decl.offset = 8;
  k->instance_fields.push_back(std::move(decl));

  linker->RegisterBuiltinClass(k);
  return k;
}

// 反射对象槽读写（8 字节槽）
void* ReflSlot(VObject* obj, uint32_t offset) { return obj->field_data() + offset; }

bool IsMethodMirror(const VClass* k) {
  return k != nullptr && (k->descriptor == "Ljava/lang/reflect/Method;" ||
                          k->descriptor == "Ljava/lang/reflect/Constructor;");
}

bool IsFieldMirror(const VClass* k) {
  return k != nullptr && k->descriptor == "Ljava/lang/reflect/Field;";
}

}  // namespace

void EnsureReflectionClasses(ClassLinker* linker) {
  if (linker == nullptr) return;
  if (linker->FindClass("Ljava/lang/reflect/Method;") != nullptr) return;  // 幂等
  VClass* class_class = linker->FindClass("Ljava/lang/Class;");
  VClass* object = linker->FindClass("Ljava/lang/Object;");
  MakeMirror(linker, class_class, object, "Ljava/lang/reflect/Method;");
  MakeMirror(linker, class_class, object, "Ljava/lang/reflect/Constructor;");
  MakeMirror(linker, class_class, object, "Ljava/lang/reflect/Field;");
}

// ---- FromReflectedMethod / ToReflectedMethod（jni_internal.cc:727/753）----

jmethodID JniFromReflectedMethod(JNIEnv* env, jobject jlr_method) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeJObject(e, jlr_method);
  if (o == nullptr || o->klass == nullptr) {
    e->Warn("FromReflectedMethod: null reflection object");
    return nullptr;
  }
  if (!IsMethodMirror(o->klass)) {
    e->Warn("FromReflectedMethod: not a java.lang.reflect.Method/Constructor: " +
            o->klass->descriptor);
    return nullptr;
  }
  VMethod* m = reinterpret_cast<VMethod*>(
      static_cast<uintptr_t>(LoadSlot(ReflSlot(static_cast<VObject*>(o), 0))));
  return EncodeMethodId(m);
}

jobject JniToReflectedMethod(JNIEnv* env, jclass, jmethodID methodID, jboolean) {
  JNIEnvExt* e = Ext(env);
  VMethod* m = DecodeMethodId(methodID);
  if (m == nullptr) {
    e->Warn("ToReflectedMethod: null methodID");
    return nullptr;
  }
  ClassLinker* linker = e->vmstate->linker;
  EnsureReflectionClasses(linker);
  // <init> -> Constructor，其余 -> Method（mirror::Constructor::CreateFromArtMethod）
  const char* desc = (m->name == "<init>") ? "Ljava/lang/reflect/Constructor;"
                                           : "Ljava/lang/reflect/Method;";
  VClass* k = linker->FindClass(desc);
  if (k == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/InternalError;", "reflection mirror missing");
    return nullptr;
  }
  VObject* obj = linker->AllocObject(k);
  if (obj == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "ToReflectedMethod");
    return nullptr;
  }
  StoreSlot(ReflSlot(obj, 0), static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m)));
  if (m->declaring != nullptr) {
    StoreSlot(ReflSlot(obj, 8),
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(m->declaring)));
  }
  return EncodeJObject(e, obj);
}

// ---- FromReflectedField / ToReflectedField（jni_internal.cc:738/777）----

jfieldID JniFromReflectedField(JNIEnv* env, jobject jlr_field) {
  JNIEnvExt* e = Ext(env);
  ObjHeader* o = DecodeJObject(e, jlr_field);
  if (o == nullptr || o->klass == nullptr) {
    e->Warn("FromReflectedField: null reflection object");
    return nullptr;
  }
  // ART：非 java.lang.reflect.Field 返回 null
  if (!IsFieldMirror(o->klass)) {
    e->Warn("FromReflectedField: not a java.lang.reflect.Field: " + o->klass->descriptor);
    return nullptr;
  }
  VField* f = reinterpret_cast<VField*>(
      static_cast<uintptr_t>(LoadSlot(ReflSlot(static_cast<VObject*>(o), 0))));
  return EncodeFieldId(f);
}

jobject JniToReflectedField(JNIEnv* env, jclass, jfieldID fieldID, jboolean) {
  JNIEnvExt* e = Ext(env);
  VField* f = DecodeFieldId(fieldID);
  if (f == nullptr) {
    e->Warn("ToReflectedField: null fieldID");
    return nullptr;
  }
  ClassLinker* linker = e->vmstate->linker;
  EnsureReflectionClasses(linker);
  VClass* k = linker->FindClass("Ljava/lang/reflect/Field;");
  if (k == nullptr) {
    e->vmstate->ThrowNew("Ljava/lang/InternalError;", "reflection mirror missing");
    return nullptr;
  }
  VObject* obj = linker->AllocObject(k);
  if (obj == nullptr) {
    dexvm::runtime::ThrowOutOfMemoryError(*e->vmstate, "ToReflectedField");
    return nullptr;
  }
  StoreSlot(ReflSlot(obj, 0), static_cast<uint64_t>(reinterpret_cast<uintptr_t>(f)));
  if (f->declaring != nullptr) {
    StoreSlot(ReflSlot(obj, 8),
              static_cast<uint64_t>(reinterpret_cast<uintptr_t>(f->declaring)));
  }
  return EncodeJObject(e, obj);
}

}  // namespace dexvm::jni
