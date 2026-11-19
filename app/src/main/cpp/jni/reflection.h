// 反射 ID 面（docs 05 章 15.8；jni_internal.cc:FromReflectedMethod/ToReflectedMethod 等）
// java/lang/reflect/{Method,Constructor,Field} 骨架镜像类经
// ClassLinker::RegisterBuiltinClass 登记（不改 core/builtins.*）。
// 反射对象 <-> VMethod*/VField* 往返必须可通；Class.getDeclaredMethod 等价入口后置。
#pragma once

#include <string>

#include "core/vm_types.h"
#include "jni/jni_env.h"

namespace dexvm::runtime {
class ClassLinker;
}

namespace dexvm::jni {

// 幂等登记反射镜像骨架（Method/Constructor/Field：artMethod/artField 8 字节槽）
void EnsureReflectionClasses(dexvm::runtime::ClassLinker* linker);

// ---- JNIEnv 表槽位实现（真实签名）----
jmethodID JniFromReflectedMethod(JNIEnv* env, jobject jlr_method);
jfieldID JniFromReflectedField(JNIEnv* env, jobject jlr_field);
jobject JniToReflectedMethod(JNIEnv* env, jclass clazz, jmethodID methodID, jboolean isStatic);
jobject JniToReflectedField(JNIEnv* env, jclass clazz, jfieldID fieldID, jboolean isStatic);

}  // namespace dexvm::jni
