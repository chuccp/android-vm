// JNI 全面（M1 最小面 + M2 补全）：JNIEnv/JavaVM 函数表 + JNIEnvExt/JavaVMExt
// （docs 05 章 15.1-15.15、§28.3；对照 jni_internal.cc/java_vm_ext.cc）
//
// ABI 面与真实 jni.h 完整布局兼容（M2 起）：
//   - JNINativeInterface 采用与 Android/JDK jni.h 完全一致的槽位顺序与签名
//     （全 232 槽：4 reserved + 228 函数；未实现面为告警桩），真实 <jni.h>
//     编译的 .so（C/C++ 两种调用风格）可直接对接；
//   - JNIEnv = _JNIEnv（首成员 functions 指针），调用风格
//     env->functions->FindClass(env, ...) 或 (*env)->FindClass(env, ...) 均可；
//   - JavaVM = _JavaVM（JNIInvokeInterface，顺序与真实 jni.h 一致）。
// 本头与系统 <jni.h> 互斥（同一 TU 不可共含）；tests 侧 ABI 对照见
// tools/jni_full_tests.sh 的槽位偏移校验。
//
// M1/M2 简化：一个 JavaVMExt、每 GuestTask 一个 JNIEnvExt；单线程语义。
#pragma once

#include <cstdarg>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/interpreter.h"
#include "core/vm_types.h"
#include "jni/handle_table.h"
#include "jni/native_manager.h"

#ifndef JNICALL
#define JNICALL
#endif

// ---- JNI 基本类型（与 jni.h 同型）----
using jint = int32_t;
using jlong = int64_t;
using jbyte = int8_t;
using jboolean = uint8_t;
using jchar = uint16_t;
using jshort = int16_t;
using jfloat = float;
using jdouble = double;
using jsize = jint;

struct _jobject {};
struct _jclass : public _jobject {};
struct _jstring : public _jobject {};
struct _jthrowable : public _jobject {};
struct _jarray : public _jobject {};
struct _jobjectArray : public _jarray {};
struct _jbooleanArray : public _jarray {};
struct _jbyteArray : public _jarray {};
struct _jcharArray : public _jarray {};
struct _jshortArray : public _jarray {};
struct _jintArray : public _jarray {};
struct _jlongArray : public _jarray {};
struct _jfloatArray : public _jarray {};
struct _jdoubleArray : public _jarray {};
struct _jmethodID {};
struct _jfieldID {};

using jobject = _jobject*;
using jclass = _jclass*;
using jstring = _jstring*;
using jthrowable = _jthrowable*;
using jweak = jobject;
using jarray = _jarray*;
using jobjectArray = _jobjectArray*;
using jbooleanArray = _jbooleanArray*;
using jbyteArray = _jbyteArray*;
using jcharArray = _jcharArray*;
using jshortArray = _jshortArray*;
using jintArray = _jintArray*;
using jlongArray = _jlongArray*;
using jfloatArray = _jfloatArray*;
using jdoubleArray = _jdoubleArray*;
using jmethodID = _jmethodID*;
using jfieldID = _jfieldID*;

union jvalue {
  jboolean z;
  jbyte b;
  jchar c;
  jshort s;
  jint i;
  jlong j;
  jfloat f;
  jdouble d;
  jobject l;
};

enum jobjectRefType {
  JNIInvalidRefType = 0,
  JNILocalRefType = 1,
  JNIGlobalRefType = 2,
  JNIWeakGlobalRefType = 3,
};

// RegisterNatives 三元组（jni.h JNINativeMethod）
struct JNINativeMethod {
  const char* name;
  const char* signature;
  void* fnPtr;
};

#ifndef JNI_FALSE
#define JNI_FALSE 0
#endif
#ifndef JNI_TRUE
#define JNI_TRUE 1
#endif
#ifndef JNI_OK
#define JNI_OK 0
#endif
#ifndef JNI_ERR
#define JNI_ERR (-1)
#endif
#ifndef JNI_EDETACHED
#define JNI_EDETACHED (-2)
#endif
#ifndef JNI_EVERSION
#define JNI_EVERSION (-3)
#endif
#ifndef JNI_COMMIT
#define JNI_COMMIT 1
#endif
#ifndef JNI_ABORT
#define JNI_ABORT 2
#endif
#ifndef JNI_VERSION_1_1
#define JNI_VERSION_1_1 0x00010001
#endif
#ifndef JNI_VERSION_1_2
#define JNI_VERSION_1_2 0x00010002
#endif
#ifndef JNI_VERSION_1_4
#define JNI_VERSION_1_4 0x00010004
#endif
#ifndef JNI_VERSION_1_6
#define JNI_VERSION_1_6 0x00010006
#endif
#ifndef JNI_VERSION_1_8
#define JNI_VERSION_1_8 0x00010008
#endif

struct _JNIEnv;
struct _JavaVM;
using JNIEnv = _JNIEnv;
using JavaVM = _JavaVM;

// ---- JNIEnv 函数表（JNINativeInterface：真实 jni.h 全槽位顺序/签名）----
// 顺序与 Android NDK / JDK jni.h 逐槽一致（工具校验见 tools/jni_full_tests.sh）。
struct JNINativeInterface {
  void* reserved0;
  void* reserved1;
  void* reserved2;
  void* reserved3;

  jint(JNICALL* GetVersion)(JNIEnv* env);
  jclass(JNICALL* DefineClass)(JNIEnv* env, const char* name, jobject loader, const jbyte* buf,
                               jsize len);
  jclass(JNICALL* FindClass)(JNIEnv* env, const char* name);

  jmethodID(JNICALL* FromReflectedMethod)(JNIEnv* env, jobject jlr_method);
  jfieldID(JNICALL* FromReflectedField)(JNIEnv* env, jobject jlr_field);
  jobject(JNICALL* ToReflectedMethod)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                      jboolean isStatic);

  jclass(JNICALL* GetSuperclass)(JNIEnv* env, jclass clazz);
  jboolean(JNICALL* IsAssignableFrom)(JNIEnv* env, jclass clazz1, jclass clazz2);

  jobject(JNICALL* ToReflectedField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jboolean isStatic);

  jint(JNICALL* Throw)(JNIEnv* env, jthrowable obj);
  jint(JNICALL* ThrowNew)(JNIEnv* env, jclass clazz, const char* msg);
  jthrowable(JNICALL* ExceptionOccurred)(JNIEnv* env);
  void(JNICALL* ExceptionDescribe)(JNIEnv* env);
  void(JNICALL* ExceptionClear)(JNIEnv* env);
  void(JNICALL* FatalError)(JNIEnv* env, const char* msg);

  jint(JNICALL* PushLocalFrame)(JNIEnv* env, jint capacity);
  jobject(JNICALL* PopLocalFrame)(JNIEnv* env, jobject result);

  jobject(JNICALL* NewGlobalRef)(JNIEnv* env, jobject obj);
  void(JNICALL* DeleteGlobalRef)(JNIEnv* env, jobject obj);
  void(JNICALL* DeleteLocalRef)(JNIEnv* env, jobject obj);
  jboolean(JNICALL* IsSameObject)(JNIEnv* env, jobject a, jobject b);

  jobject(JNICALL* NewLocalRef)(JNIEnv* env, jobject ref);
  jint(JNICALL* EnsureLocalCapacity)(JNIEnv* env, jint capacity);

  jobject(JNICALL* AllocObject)(JNIEnv* env, jclass clazz);
  jobject(JNICALL* NewObject)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jobject(JNICALL* NewObjectV)(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args);
  jobject(JNICALL* NewObjectA)(JNIEnv* env, jclass clazz, jmethodID methodID, const jvalue* args);

  jclass(JNICALL* GetObjectClass)(JNIEnv* env, jobject obj);
  jboolean(JNICALL* IsInstanceOf)(JNIEnv* env, jobject obj, jclass clazz);
  jmethodID(JNICALL* GetMethodID)(JNIEnv* env, jclass clazz, const char* name, const char* sig);

  jobject(JNICALL* CallObjectMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jobject(JNICALL* CallObjectMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jobject(JNICALL* CallObjectMethodA)(JNIEnv* env, jobject obj, jmethodID methodID,
                                      const jvalue* args);
  jboolean(JNICALL* CallBooleanMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jboolean(JNICALL* CallBooleanMethodV)(JNIEnv* env, jobject obj, jmethodID methodID,
                                        va_list args);
  jboolean(JNICALL* CallBooleanMethodA)(JNIEnv* env, jobject obj, jmethodID methodID,
                                        const jvalue* args);
  jbyte(JNICALL* CallByteMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jbyte(JNICALL* CallByteMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jbyte(JNICALL* CallByteMethodA)(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args);
  jchar(JNICALL* CallCharMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jchar(JNICALL* CallCharMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jchar(JNICALL* CallCharMethodA)(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args);
  jshort(JNICALL* CallShortMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jshort(JNICALL* CallShortMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jshort(JNICALL* CallShortMethodA)(JNIEnv* env, jobject obj, jmethodID methodID,
                                    const jvalue* args);
  jint(JNICALL* CallIntMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jint(JNICALL* CallIntMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jint(JNICALL* CallIntMethodA)(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args);
  jlong(JNICALL* CallLongMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jlong(JNICALL* CallLongMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jlong(JNICALL* CallLongMethodA)(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args);
  jfloat(JNICALL* CallFloatMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jfloat(JNICALL* CallFloatMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jfloat(JNICALL* CallFloatMethodA)(JNIEnv* env, jobject obj, jmethodID methodID,
                                    const jvalue* args);
  jdouble(JNICALL* CallDoubleMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  jdouble(JNICALL* CallDoubleMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  jdouble(JNICALL* CallDoubleMethodA)(JNIEnv* env, jobject obj, jmethodID methodID,
                                      const jvalue* args);
  void(JNICALL* CallVoidMethod)(JNIEnv* env, jobject obj, jmethodID methodID, ...);
  void(JNICALL* CallVoidMethodV)(JNIEnv* env, jobject obj, jmethodID methodID, va_list args);
  void(JNICALL* CallVoidMethodA)(JNIEnv* env, jobject obj, jmethodID methodID, const jvalue* args);

  jobject(JNICALL* CallNonvirtualObjectMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                               jmethodID methodID, ...);
  jobject(JNICALL* CallNonvirtualObjectMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                                jmethodID methodID, va_list args);
  jobject(JNICALL* CallNonvirtualObjectMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                                jmethodID methodID, const jvalue* args);
  jboolean(JNICALL* CallNonvirtualBooleanMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                                 jmethodID methodID, ...);
  jboolean(JNICALL* CallNonvirtualBooleanMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                                  jmethodID methodID, va_list args);
  jboolean(JNICALL* CallNonvirtualBooleanMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                                  jmethodID methodID, const jvalue* args);
  jbyte(JNICALL* CallNonvirtualByteMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                           jmethodID methodID, ...);
  jbyte(JNICALL* CallNonvirtualByteMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                            jmethodID methodID, va_list args);
  jbyte(JNICALL* CallNonvirtualByteMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                            jmethodID methodID, const jvalue* args);
  jchar(JNICALL* CallNonvirtualCharMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                           jmethodID methodID, ...);
  jchar(JNICALL* CallNonvirtualCharMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                            jmethodID methodID, va_list args);
  jchar(JNICALL* CallNonvirtualCharMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                            jmethodID methodID, const jvalue* args);
  jshort(JNICALL* CallNonvirtualShortMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                             jmethodID methodID, ...);
  jshort(JNICALL* CallNonvirtualShortMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                              jmethodID methodID, va_list args);
  jshort(JNICALL* CallNonvirtualShortMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                              jmethodID methodID, const jvalue* args);
  jint(JNICALL* CallNonvirtualIntMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                         jmethodID methodID, ...);
  jint(JNICALL* CallNonvirtualIntMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                          jmethodID methodID, va_list args);
  jint(JNICALL* CallNonvirtualIntMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                          jmethodID methodID, const jvalue* args);
  jlong(JNICALL* CallNonvirtualLongMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                           jmethodID methodID, ...);
  jlong(JNICALL* CallNonvirtualLongMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                            jmethodID methodID, va_list args);
  jlong(JNICALL* CallNonvirtualLongMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                            jmethodID methodID, const jvalue* args);
  jfloat(JNICALL* CallNonvirtualFloatMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                             jmethodID methodID, ...);
  jfloat(JNICALL* CallNonvirtualFloatMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                              jmethodID methodID, va_list args);
  jfloat(JNICALL* CallNonvirtualFloatMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                              jmethodID methodID, const jvalue* args);
  jdouble(JNICALL* CallNonvirtualDoubleMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                               jmethodID methodID, ...);
  jdouble(JNICALL* CallNonvirtualDoubleMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                                jmethodID methodID, va_list args);
  jdouble(JNICALL* CallNonvirtualDoubleMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                                jmethodID methodID, const jvalue* args);
  void(JNICALL* CallNonvirtualVoidMethod)(JNIEnv* env, jobject obj, jclass clazz,
                                          jmethodID methodID, ...);
  void(JNICALL* CallNonvirtualVoidMethodV)(JNIEnv* env, jobject obj, jclass clazz,
                                           jmethodID methodID, va_list args);
  void(JNICALL* CallNonvirtualVoidMethodA)(JNIEnv* env, jobject obj, jclass clazz,
                                           jmethodID methodID, const jvalue* args);

  jfieldID(JNICALL* GetFieldID)(JNIEnv* env, jclass clazz, const char* name, const char* sig);

  jobject(JNICALL* GetObjectField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jboolean(JNICALL* GetBooleanField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jbyte(JNICALL* GetByteField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jchar(JNICALL* GetCharField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jshort(JNICALL* GetShortField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jint(JNICALL* GetIntField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jlong(JNICALL* GetLongField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jfloat(JNICALL* GetFloatField)(JNIEnv* env, jobject obj, jfieldID fieldID);
  jdouble(JNICALL* GetDoubleField)(JNIEnv* env, jobject obj, jfieldID fieldID);

  void(JNICALL* SetObjectField)(JNIEnv* env, jobject obj, jfieldID fieldID, jobject value);
  void(JNICALL* SetBooleanField)(JNIEnv* env, jobject obj, jfieldID fieldID, jboolean value);
  void(JNICALL* SetByteField)(JNIEnv* env, jobject obj, jfieldID fieldID, jbyte value);
  void(JNICALL* SetCharField)(JNIEnv* env, jobject obj, jfieldID fieldID, jchar value);
  void(JNICALL* SetShortField)(JNIEnv* env, jobject obj, jfieldID fieldID, jshort value);
  void(JNICALL* SetIntField)(JNIEnv* env, jobject obj, jfieldID fieldID, jint value);
  void(JNICALL* SetLongField)(JNIEnv* env, jobject obj, jfieldID fieldID, jlong value);
  void(JNICALL* SetFloatField)(JNIEnv* env, jobject obj, jfieldID fieldID, jfloat value);
  void(JNICALL* SetDoubleField)(JNIEnv* env, jobject obj, jfieldID fieldID, jdouble value);

  jmethodID(JNICALL* GetStaticMethodID)(JNIEnv* env, jclass clazz, const char* name,
                                        const char* sig);

  jobject(JNICALL* CallStaticObjectMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jobject(JNICALL* CallStaticObjectMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                            va_list args);
  jobject(JNICALL* CallStaticObjectMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                            const jvalue* args);
  jboolean(JNICALL* CallStaticBooleanMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jboolean(JNICALL* CallStaticBooleanMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                              va_list args);
  jboolean(JNICALL* CallStaticBooleanMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                              const jvalue* args);
  jbyte(JNICALL* CallStaticByteMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jbyte(JNICALL* CallStaticByteMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args);
  jbyte(JNICALL* CallStaticByteMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                        const jvalue* args);
  jchar(JNICALL* CallStaticCharMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jchar(JNICALL* CallStaticCharMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args);
  jchar(JNICALL* CallStaticCharMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                        const jvalue* args);
  jshort(JNICALL* CallStaticShortMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jshort(JNICALL* CallStaticShortMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                          va_list args);
  jshort(JNICALL* CallStaticShortMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                          const jvalue* args);
  jint(JNICALL* CallStaticIntMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jint(JNICALL* CallStaticIntMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args);
  jint(JNICALL* CallStaticIntMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                      const jvalue* args);
  jlong(JNICALL* CallStaticLongMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jlong(JNICALL* CallStaticLongMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                        va_list args);
  jlong(JNICALL* CallStaticLongMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                        const jvalue* args);
  jfloat(JNICALL* CallStaticFloatMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jfloat(JNICALL* CallStaticFloatMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                          va_list args);
  jfloat(JNICALL* CallStaticFloatMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                          const jvalue* args);
  jdouble(JNICALL* CallStaticDoubleMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  jdouble(JNICALL* CallStaticDoubleMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                            va_list args);
  jdouble(JNICALL* CallStaticDoubleMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                            const jvalue* args);
  void(JNICALL* CallStaticVoidMethod)(JNIEnv* env, jclass clazz, jmethodID methodID, ...);
  void(JNICALL* CallStaticVoidMethodV)(JNIEnv* env, jclass clazz, jmethodID methodID, va_list args);
  void(JNICALL* CallStaticVoidMethodA)(JNIEnv* env, jclass clazz, jmethodID methodID,
                                       const jvalue* args);

  jfieldID(JNICALL* GetStaticFieldID)(JNIEnv* env, jclass clazz, const char* name, const char* sig);

  jobject(JNICALL* GetStaticObjectField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jboolean(JNICALL* GetStaticBooleanField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jbyte(JNICALL* GetStaticByteField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jchar(JNICALL* GetStaticCharField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jshort(JNICALL* GetStaticShortField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jint(JNICALL* GetStaticIntField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jlong(JNICALL* GetStaticLongField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jfloat(JNICALL* GetStaticFloatField)(JNIEnv* env, jclass clazz, jfieldID fieldID);
  jdouble(JNICALL* GetStaticDoubleField)(JNIEnv* env, jclass clazz, jfieldID fieldID);

  void(JNICALL* SetStaticObjectField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jobject value);
  void(JNICALL* SetStaticBooleanField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jboolean value);
  void(JNICALL* SetStaticByteField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jbyte value);
  void(JNICALL* SetStaticCharField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jchar value);
  void(JNICALL* SetStaticShortField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jshort value);
  void(JNICALL* SetStaticIntField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jint value);
  void(JNICALL* SetStaticLongField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jlong value);
  void(JNICALL* SetStaticFloatField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jfloat value);
  void(JNICALL* SetStaticDoubleField)(JNIEnv* env, jclass clazz, jfieldID fieldID, jdouble value);

  jstring(JNICALL* NewString)(JNIEnv* env, const jchar* unicode, jsize len);
  jsize(JNICALL* GetStringLength)(JNIEnv* env, jstring str);
  const jchar*(JNICALL* GetStringChars)(JNIEnv* env, jstring str, jboolean* isCopy);
  void(JNICALL* ReleaseStringChars)(JNIEnv* env, jstring str, const jchar* chars);
  jstring(JNICALL* NewStringUTF)(JNIEnv* env, const char* bytes);
  jsize(JNICALL* GetStringUTFLength)(JNIEnv* env, jstring str);
  const char*(JNICALL* GetStringUTFChars)(JNIEnv* env, jstring str, jboolean* isCopy);
  void(JNICALL* ReleaseStringUTFChars)(JNIEnv* env, jstring str, const char* chars);

  jsize(JNICALL* GetArrayLength)(JNIEnv* env, jarray array);
  jobjectArray(JNICALL* NewObjectArray)(JNIEnv* env, jsize length, jclass elementClass,
                                        jobject initialElement);
  jobject(JNICALL* GetObjectArrayElement)(JNIEnv* env, jobjectArray array, jsize index);
  void(JNICALL* SetObjectArrayElement)(JNIEnv* env, jobjectArray array, jsize index, jobject value);

  jbooleanArray(JNICALL* NewBooleanArray)(JNIEnv* env, jsize length);
  jbyteArray(JNICALL* NewByteArray)(JNIEnv* env, jsize length);
  jcharArray(JNICALL* NewCharArray)(JNIEnv* env, jsize length);
  jshortArray(JNICALL* NewShortArray)(JNIEnv* env, jsize length);
  jintArray(JNICALL* NewIntArray)(JNIEnv* env, jsize length);
  jlongArray(JNICALL* NewLongArray)(JNIEnv* env, jsize length);
  jfloatArray(JNICALL* NewFloatArray)(JNIEnv* env, jsize length);
  jdoubleArray(JNICALL* NewDoubleArray)(JNIEnv* env, jsize length);

  jboolean*(JNICALL* GetBooleanArrayElements)(JNIEnv* env, jbooleanArray array, jboolean* isCopy);
  jbyte*(JNICALL* GetByteArrayElements)(JNIEnv* env, jbyteArray array, jboolean* isCopy);
  jchar*(JNICALL* GetCharArrayElements)(JNIEnv* env, jcharArray array, jboolean* isCopy);
  jshort*(JNICALL* GetShortArrayElements)(JNIEnv* env, jshortArray array, jboolean* isCopy);
  jint*(JNICALL* GetIntArrayElements)(JNIEnv* env, jintArray array, jboolean* isCopy);
  jlong*(JNICALL* GetLongArrayElements)(JNIEnv* env, jlongArray array, jboolean* isCopy);
  jfloat*(JNICALL* GetFloatArrayElements)(JNIEnv* env, jfloatArray array, jboolean* isCopy);
  jdouble*(JNICALL* GetDoubleArrayElements)(JNIEnv* env, jdoubleArray array, jboolean* isCopy);

  void(JNICALL* ReleaseBooleanArrayElements)(JNIEnv* env, jbooleanArray array, jboolean* elems,
                                             jint mode);
  void(JNICALL* ReleaseByteArrayElements)(JNIEnv* env, jbyteArray array, jbyte* elems, jint mode);
  void(JNICALL* ReleaseCharArrayElements)(JNIEnv* env, jcharArray array, jchar* elems, jint mode);
  void(JNICALL* ReleaseShortArrayElements)(JNIEnv* env, jshortArray array, jshort* elems, jint mode);
  void(JNICALL* ReleaseIntArrayElements)(JNIEnv* env, jintArray array, jint* elems, jint mode);
  void(JNICALL* ReleaseLongArrayElements)(JNIEnv* env, jlongArray array, jlong* elems, jint mode);
  void(JNICALL* ReleaseFloatArrayElements)(JNIEnv* env, jfloatArray array, jfloat* elems, jint mode);
  void(JNICALL* ReleaseDoubleArrayElements)(JNIEnv* env, jdoubleArray array, jdouble* elems,
                                            jint mode);

  void(JNICALL* GetBooleanArrayRegion)(JNIEnv* env, jbooleanArray array, jsize start, jsize len,
                                       jboolean* buf);
  void(JNICALL* GetByteArrayRegion)(JNIEnv* env, jbyteArray array, jsize start, jsize len,
                                    jbyte* buf);
  void(JNICALL* GetCharArrayRegion)(JNIEnv* env, jcharArray array, jsize start, jsize len,
                                    jchar* buf);
  void(JNICALL* GetShortArrayRegion)(JNIEnv* env, jshortArray array, jsize start, jsize len,
                                     jshort* buf);
  void(JNICALL* GetIntArrayRegion)(JNIEnv* env, jintArray array, jsize start, jsize len, jint* buf);
  void(JNICALL* GetLongArrayRegion)(JNIEnv* env, jlongArray array, jsize start, jsize len,
                                    jlong* buf);
  void(JNICALL* GetFloatArrayRegion)(JNIEnv* env, jfloatArray array, jsize start, jsize len,
                                     jfloat* buf);
  void(JNICALL* GetDoubleArrayRegion)(JNIEnv* env, jdoubleArray array, jsize start, jsize len,
                                      jdouble* buf);

  void(JNICALL* SetBooleanArrayRegion)(JNIEnv* env, jbooleanArray array, jsize start, jsize len,
                                       const jboolean* buf);
  void(JNICALL* SetByteArrayRegion)(JNIEnv* env, jbyteArray array, jsize start, jsize len,
                                    const jbyte* buf);
  void(JNICALL* SetCharArrayRegion)(JNIEnv* env, jcharArray array, jsize start, jsize len,
                                    const jchar* buf);
  void(JNICALL* SetShortArrayRegion)(JNIEnv* env, jshortArray array, jsize start, jsize len,
                                     const jshort* buf);
  void(JNICALL* SetIntArrayRegion)(JNIEnv* env, jintArray array, jsize start, jsize len,
                                   const jint* buf);
  void(JNICALL* SetLongArrayRegion)(JNIEnv* env, jlongArray array, jsize start, jsize len,
                                    const jlong* buf);
  void(JNICALL* SetFloatArrayRegion)(JNIEnv* env, jfloatArray array, jsize start, jsize len,
                                     const jfloat* buf);
  void(JNICALL* SetDoubleArrayRegion)(JNIEnv* env, jdoubleArray array, jsize start, jsize len,
                                      const jdouble* buf);

  jint(JNICALL* RegisterNatives)(JNIEnv* env, jclass clazz, const JNINativeMethod* methods,
                                 jint nMethods);
  jint(JNICALL* UnregisterNatives)(JNIEnv* env, jclass clazz);
  jint(JNICALL* MonitorEnter)(JNIEnv* env, jobject obj);
  jint(JNICALL* MonitorExit)(JNIEnv* env, jobject obj);
  jint(JNICALL* GetJavaVM)(JNIEnv* env, JavaVM** vm);

  void(JNICALL* GetStringRegion)(JNIEnv* env, jstring str, jsize start, jsize len, jchar* buf);
  void(JNICALL* GetStringUTFRegion)(JNIEnv* env, jstring str, jsize start, jsize len, char* buf);

  void*(JNICALL* GetPrimitiveArrayCritical)(JNIEnv* env, jarray array, jboolean* isCopy);
  void(JNICALL* ReleasePrimitiveArrayCritical)(JNIEnv* env, jarray array, void* carray, jint mode);

  const jchar*(JNICALL* GetStringCritical)(JNIEnv* env, jstring str, jboolean* isCopy);
  void(JNICALL* ReleaseStringCritical)(JNIEnv* env, jstring str, const jchar* carray);

  jweak(JNICALL* NewWeakGlobalRef)(JNIEnv* env, jobject obj);
  void(JNICALL* DeleteWeakGlobalRef)(JNIEnv* env, jweak ref);

  jboolean(JNICALL* ExceptionCheck)(JNIEnv* env);

  jobject(JNICALL* NewDirectByteBuffer)(JNIEnv* env, void* address, jlong capacity);
  void*(JNICALL* GetDirectBufferAddress)(JNIEnv* env, jobject buf);
  jlong(JNICALL* GetDirectBufferCapacity)(JNIEnv* env, jobject buf);

  jobjectRefType(JNICALL* GetObjectRefType)(JNIEnv* env, jobject obj);
};

struct _JNIEnv {
  const JNINativeInterface* functions;
};

// ---- JavaVM 函数表（JNIInvokeInterface：真实 jni.h 顺序）----
struct JNIInvokeInterface {
  void* reserved0;
  void* reserved1;
  void* reserved2;
  jint(JNICALL* DestroyJavaVM)(JavaVM* vm);
  jint(JNICALL* AttachCurrentThread)(JavaVM* vm, JNIEnv** p_env, void* thr_args);
  jint(JNICALL* DetachCurrentThread)(JavaVM* vm);
  jint(JNICALL* GetEnv)(JavaVM* vm, void** p_env, jint version);
  jint(JNICALL* AttachCurrentThreadAsDaemon)(JavaVM* vm, JNIEnv** p_env, void* thr_args);
};

struct _JavaVM {
  const JNIInvokeInterface* functions;
};

// ---- VM 内部结构（M1 简化：一个 VM、每 GuestTask 一个 Env）----
namespace dexvm::jni {

struct JavaVMExt;

// GetPrimitiveArrayCritical pin（docs 15.9 恒 pin）：VM 数组为 8 字节元素槽，
// 恒返回按元素宽度打包的 pin 视图（is_copy=JNI_FALSE），Release 时同步修改
//（mode 语义同 Release<Type>ArrayElements）。
struct CriticalPin {
  runtime::VArray* array = nullptr;
  void* packed = nullptr;
  size_t length = 0;
  char primitive = 0;  // 'Z','B','C','S','I','J','F','D'
  uint32_t refs = 0;
};

struct JNIEnvExt : _JNIEnv {
  LocalRefTable locals;                      // 非 guest-task 上下文（宿主/VM 启动期）
  // guest task（fiber）各自的 local 引用表：多 task 在同一 OS 线程协作切换，
  // 共用一张 frame 栈时跨 task 的 Push/Pop 会交错（A 的 native 返回时弹掉 B
  // 的帧 → B 已建句柄成陈旧引用；加固 native 跨 JNI 回调缓存 jobject 的面会
  // 命中）。ART 中 local ref 本就是线程私有，这里按 task 拆分。
  std::map<void*, std::unique_ptr<LocalRefTable>> task_locals;
  // 当前上下文应使用的 local 表（guest task 上下文取其表，否则 base 表）。
  LocalRefTable& Locals();
  dexvm::runtime::VMState* vmstate = nullptr;  // GuestTask 的 VM 状态（异常挂点）
  JavaVMExt* vm = nullptr;
  dexvm::runtime::VObject* class_loader = nullptr;  // FindClass loader override（nativeLoad/OnLoad）
  std::vector<CriticalPin> critical_pins;   // 恒 pin 语义的 pin 记录
  std::string warning_sink;                  // JNI WARNING 记录（ART 打日志+dump 栈）

  // JNI WARNING（docs 15.15：DeleteLocalRef 跨 frame no-op 等；不抛异常）
  void Warn(const std::string& msg);
};

struct JavaVMExt : _JavaVM {
  GlobalRefTable globals;         // global/weak-global 表（docs 15.15）
  std::vector<JNIEnvExt*> attached;  // 已挂接 Env（M2：GuestTask/ThreadList）
  NativeManager natives;          // SharedLibrary + RegisterNatives（docs 07 章）

  void Attach(JNIEnvExt* env);
  void Detach(JNIEnvExt* env);
};

// 初始化（填充函数表指针）
void InitJavaVMExt(JavaVMExt* vm);
void InitJNIEnvExt(JNIEnvExt* env, JavaVMExt* vm, dexvm::runtime::VMState* state);
// guest task 结束回收其 local 引用表（core 任务结束钩子接缝）。
void ReleaseTaskLocalRefs(void* task);

// ---- 编解码（docs 05 章 15.3/15.4）----
jobject EncodeJObject(JNIEnvExt* env, dexvm::runtime::ObjHeader* obj);  // -> 当前 local frame
dexvm::runtime::ObjHeader* DecodeJObject(JNIEnvExt* env, jobject ref);  // generation 校验

// method/field ID 不透明编码（jni::EncodeArtMethod/EncodeArtField 等价物）：
// jmethodID = VMethod*、jfieldID = VField* 的指针位型包装。
jmethodID EncodeMethodId(dexvm::runtime::VMethod* m);
dexvm::runtime::VMethod* DecodeMethodId(jmethodID id);
jfieldID EncodeFieldId(dexvm::runtime::VField* f);
dexvm::runtime::VField* DecodeFieldId(jfieldID id);

// FindClass loader 优先级（jni_internal.cc:393-420 GetClassLoader）：
//   nativeLoad/JNI_OnLoad override -> 当前帧声明类 loader -> system（M2 无）-> boot
dexvm::runtime::VObject* GetEffectiveClassLoader(JNIEnvExt* env);

// NormalizeJniClassDescriptor（docs 15.6 / §28.3）：dot -> slash、数组/基本类型直通
std::string NormalizeJniClassDescriptor(const char* name);

// native 调用局部帧（generic JNI stub：进入 PushLocalFrame，
// native 返回析构 PopLocalFrame 自动释放，docs 15.12/15.15）
class NativeCallScope {
 public:
  NativeCallScope(JNIEnvExt* env, jint capacity = 0);
  ~NativeCallScope();
  NativeCallScope(const NativeCallScope&) = delete;
  NativeCallScope& operator=(const NativeCallScope&) = delete;

 private:
  LocalRefTable* table_;  // 进入时确定（按 task 分表）；析构弹同一张表
};

}  // namespace dexvm::jni
