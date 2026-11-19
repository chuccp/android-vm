// JNI 最小面（M1）冒烟测试（docs 05 章 15.1-15.15）：
//   引用生命周期（local/global/weak + frame pop 释放 + weak 清除提升）
//   GetMethodID + CallStaticIntMethod 调 fixture.Calc.add
//   NewStringUTF/GetStringUTFChars 往返（含 MUTF-8 内嵌 NUL）
//   异常 ThrowNew/ExceptionCheck/Clear
//   字段读写（8 字节槽 + static final EnsureModifiable）
//   数组区域/元素（8 字节槽窄化）
//   IsInstanceOf/IsSameObject、monitor 重入、GetVersion/GetJavaVM
// 复用 tests/fixture-src 产物 fixture.dex（tools/make_fixture.sh）。
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "jni/jni_env.h"
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

std::vector<uint8_t> ReadFile(const char* path) {
  std::vector<uint8_t> out;
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return out;
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  out.resize(static_cast<size_t>(n));
  if (n > 0 && std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
  std::fclose(f);
  return out;
}

// CallStaticIntMethodV 变参桥（测 V 形态）
jint CallAddV(JNIEnv* e, jclass cls, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  jint r = e->functions->CallStaticIntMethodV(e, cls, mid, ap);
  va_end(ap);
  return r;
}

}  // namespace

int main(int argc, char** argv) {
  const char* dex_path = argc > 1 ? argv[1] : "app/src/main/cpp/tests/out/fixture.dex";
  static std::vector<uint8_t> dex_bytes = ReadFile(dex_path);
  if (dex_bytes.empty()) {
    std::printf("FAIL: cannot read fixture dex: %s\n", dex_path);
    return 1;
  }
  std::string error;
  auto dex = dexvm::dex::DexFile::Open(dex_bytes.data(), dex_bytes.size(), &error);
  if (dex == nullptr) {
    std::printf("FAIL: DexFile::Open: %s\n", error.c_str());
    return 1;
  }

  dexvm::mem::Heap heap;
  dexvm::runtime::InternTable intern;
  dexvm::runtime::ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  linker.AddAppDexFile(dex.get());

  dexvm::runtime::VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  // 一个 JavaVMExt + 一个 JNIEnvExt（M1：每 GuestTask 一个 Env）
  dexvm::jni::JavaVMExt vm;
  dexvm::jni::InitJavaVMExt(&vm);
  dexvm::jni::JNIEnvExt env;
  dexvm::jni::InitJNIEnvExt(&env, &vm, &state);
  JNIEnv* e = &env;

  // ================= 类与对象 =================
  jclass calcCls = e->functions->FindClass(e, "fixture.Calc");
  CHECK(calcCls != nullptr, "FindClass fixture.Calc");
  jclass calcCls2 = e->functions->FindClass(e, "fixture/Calc");
  CHECK(calcCls2 != nullptr && e->functions->IsSameObject(e, calcCls, calcCls2),
        "FindClass slash form same class");
  jclass operandCls = e->functions->FindClass(e, "fixture.Operand");
  jclass shapeCls = e->functions->FindClass(e, "fixture.Shape");
  jclass stringCls = e->functions->FindClass(e, "java.lang.String");
  jclass objectCls = e->functions->FindClass(e, "java.lang.Object");
  CHECK(operandCls != nullptr && shapeCls != nullptr && stringCls != nullptr, "FindClass others");

  CHECK(e->functions->FindClass(e, "no.such.Cls") == nullptr, "FindClass missing -> null");
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "FindClass missing pending NoClassDefFoundError");
  e->functions->ExceptionClear(e);

  CHECK(e->functions->DefineClass(e, "Foo", nullptr, nullptr, 0) == nullptr,
        "DefineClass unsupported -> nullptr");

  // ================= 构造对象 + 引用生命周期 =================
  jmethodID ctorII = e->functions->GetMethodID(e, calcCls, "<init>", "(II)V");
  CHECK(ctorII != nullptr, "GetMethodID <init>(II)V");
  jobject calc = nullptr;
  {
    jvalue args[2];
    args[0].i = 3;
    args[1].i = 4;
    calc = e->functions->NewObjectA(e, calcCls, ctorII, args);
  }
  CHECK(calc != nullptr, "NewObjectA Calc(3,4)");
  jobject calcVarargs = e->functions->NewObject(e, calcCls, ctorII, 9, 9);
  CHECK(calcVarargs != nullptr && !e->functions->ExceptionCheck(e), "NewObject varargs Calc(9,9)");

  jclass calcKlass = e->functions->GetObjectClass(e, calc);
  CHECK(calcKlass != nullptr && e->functions->IsSameObject(e, calcKlass, calcCls),
        "GetObjectClass == Calc class");

  jobject r2 = e->functions->NewLocalRef(e, calc);
  CHECK(r2 != nullptr && e->functions->IsSameObject(e, r2, calc), "NewLocalRef same object");
  CHECK(e->functions->GetObjectRefType(e, r2) == JNILocalRefType, "local ref type");
  e->functions->DeleteLocalRef(e, r2);
  CHECK(e->functions->GetObjectRefType(e, r2) == JNIInvalidRefType, "DeleteLocalRef invalidates");

  // Push/PopLocalFrame：pop 释放当前 frame 全部句柄
  e->functions->PushLocalFrame(e, 16);
  jobject r3 = e->functions->NewLocalRef(e, calc);
  e->functions->PopLocalFrame(e, nullptr);
  CHECK(e->functions->GetObjectRefType(e, r3) == JNIInvalidRefType, "PopLocalFrame releases frame");

  // PopLocalFrame(survivor)：survivor 转外层 frame
  e->functions->PushLocalFrame(e, 16);
  jobject r4 = e->functions->NewLocalRef(e, calc);
  jobject survivor = e->functions->PopLocalFrame(e, r4);
  CHECK(survivor != nullptr && e->functions->IsSameObject(e, survivor, calc),
        "PopLocalFrame(survivor) converts to outer frame");
  CHECK(e->functions->GetObjectRefType(e, survivor) == JNILocalRefType, "survivor is local ref");

  // DeleteLocalRef 跨 frame no-op（docs 15.15）
  e->functions->PushLocalFrame(e, 16);
  e->functions->DeleteLocalRef(e, survivor);  // 属外层 frame：no-op
  e->functions->PopLocalFrame(e, nullptr);
  CHECK(e->functions->GetObjectRefType(e, survivor) == JNILocalRefType,
        "cross-frame DeleteLocalRef is no-op");

  // native 调用帧自动释放（generic stub 语义）
  {
    dexvm::jni::NativeCallScope scope(&env, 0);
    jobject r5 = e->functions->NewLocalRef(e, calc);
    CHECK(e->functions->GetObjectRefType(e, r5) == JNILocalRefType, "native frame ref live");
  }
  {
    jobject r5 = nullptr;
    {
      dexvm::jni::NativeCallScope scope(&env, 0);
      r5 = e->functions->NewLocalRef(e, calc);
    }
    CHECK(e->functions->GetObjectRefType(e, r5) == JNIInvalidRefType,
          "native return auto-releases locals");
  }

  CHECK(e->functions->EnsureLocalCapacity(e, 32) == 0, "EnsureLocalCapacity");

  // Global / WeakGlobal（挂 JavaVM）
  jobject g = e->functions->NewGlobalRef(e, calc);
  CHECK(g != nullptr && e->functions->GetObjectRefType(e, g) == JNIGlobalRefType, "NewGlobalRef");
  e->functions->DeleteLocalRef(e, calc);
  CHECK(e->functions->GetObjectRefType(e, g) == JNIGlobalRefType, "global survives local delete");
  jobject promoted = e->functions->NewLocalRef(e, g);
  CHECK(promoted != nullptr && e->functions->GetObjectRefType(e, promoted) == JNILocalRefType,
        "global -> NewLocalRef");
  jclass promotedCls = e->functions->GetObjectClass(e, promoted);
  CHECK(promotedCls != nullptr && e->functions->IsSameObject(e, promotedCls, calcCls),
        "global target class intact");

  jweak w = (jweak) e->functions->NewWeakGlobalRef(e, g);
  CHECK(w != nullptr && e->functions->GetObjectRefType(e, w) == JNIWeakGlobalRefType,
        "NewWeakGlobalRef");
  jobject wlift = e->functions->NewLocalRef(e, w);
  CHECK(wlift != nullptr && e->functions->IsSameObject(e, wlift, g), "weak promote (live)");
  // 模拟 GC weak clear（docs 15.15 "GC may clear entry"）
  vm.globals.ClearWeak((dexvm::jni::Handle)(uintptr_t) w);
  CHECK(e->functions->NewLocalRef(e, w) == nullptr, "cleared weak -> NewLocalRef null");
  CHECK(e->functions->IsSameObject(e, w, nullptr) == JNI_TRUE, "IsSameObject(cleared weak, null)");
  CHECK(e->functions->GetObjectRefType(e, w) == JNIWeakGlobalRefType, "cleared weak still weak entry");
  e->functions->DeleteWeakGlobalRef(e, w);
  CHECK(e->functions->GetObjectRefType(e, w) == JNIInvalidRefType, "DeleteWeakGlobalRef");
  e->functions->DeleteGlobalRef(e, g);
  CHECK(e->functions->GetObjectRefType(e, g) == JNIInvalidRefType, "DeleteGlobalRef");

  // Encode/Decode 直接往返（docs 15.3/15.4）
  {
    dexvm::runtime::ObjHeader* raw = dexvm::jni::DecodeJObject(&env, promoted);
    CHECK(raw != nullptr && raw->klass != nullptr, "DecodeJObject");
    jobject enc = dexvm::jni::EncodeJObject(&env, raw);
    CHECK(enc != nullptr && e->functions->IsSameObject(e, enc, promoted), "EncodeJObject roundtrip");
  }

  // ================= 方法调用 =================
  // static int add(int,int)（fixture.Calc.add）
  jmethodID addMid = e->functions->GetStaticMethodID(e, calcCls, "add", "(II)I");
  CHECK(addMid != nullptr, "GetStaticMethodID add(II)I");
  jint sum = e->functions->CallStaticIntMethod(e, calcCls, addMid, 2, 3);
  CHECK(!e->functions->ExceptionCheck(e) && sum == 5, "CallStaticIntMethod add(2,3)==5");
  {
    jvalue args[2];
    args[0].i = 10;
    args[1].i = 20;
    sum = e->functions->CallStaticIntMethodA(e, calcCls, addMid, args);
    CHECK(sum == 30, "CallStaticIntMethodA add(10,20)==30");
  }
  CHECK(CallAddV(e, calcCls, addMid, 4, 5) == 9, "CallStaticIntMethodV add(4,5)==9");

  // static int[] table()
  jmethodID tableMid = e->functions->GetStaticMethodID(e, calcCls, "table", "()[I");
  CHECK(tableMid != nullptr, "GetStaticMethodID table()[I");
  jobject arr = e->functions->CallStaticObjectMethod(e, calcCls, tableMid);
  CHECK(arr != nullptr && e->functions->GetArrayLength(e, (jarray) arr) == 12,
        "CallStaticObjectMethod table() length 12");

  // Boolean 族（M1 无 CheckJNI 签名校验：按返回槽窄化）
  CHECK(e->functions->CallStaticBooleanMethod(e, calcCls, addMid, 1, 0) == JNI_TRUE,
        "CallStaticBooleanMethod narrowing true");
  CHECK(e->functions->CallStaticBooleanMethod(e, calcCls, addMid, 0, 0) == JNI_FALSE,
        "CallStaticBooleanMethod narrowing false");

  // static void：<clinit> 重跑
  jmethodID clinitMid = e->functions->GetStaticMethodID(e, calcCls, "<clinit>", "()V");
  CHECK(clinitMid != nullptr, "GetStaticMethodID <clinit>()V");
  e->functions->CallStaticVoidMethod(e, calcCls, clinitMid);
  CHECK(!e->functions->ExceptionCheck(e), "CallStaticVoidMethod <clinit> ok");

  // 实例调用：mul(k)=area()*k（不依赖跨类字段槽布局）
  jmethodID mulMid = e->functions->GetMethodID(e, calcCls, "mul", "(I)I");
  CHECK(mulMid != nullptr, "GetMethodID mul(I)I");
  CHECK(e->functions->CallIntMethod(e, promoted, mulMid, 5) == 60,
        "CallIntMethod mul(5)==60 (area=3*4)");
  CHECK(e->functions->CallBooleanMethod(e, promoted, mulMid, 0) == JNI_FALSE,
        "CallBooleanMethod narrowing false (mul(0)==0)");
  CHECK(e->functions->CallBooleanMethod(e, promoted, mulMid, 1) == JNI_TRUE,
        "CallBooleanMethod narrowing true (mul(1)!=0)");

  // 实例 void：<init> 重跑（字段被重置可观察）
  jvalue cargs[2];
  cargs[0].i = 5;
  cargs[1].i = 6;
  e->functions->CallVoidMethodA(e, promoted, ctorII, cargs);
  jfieldID widthF = e->functions->GetFieldID(e, calcCls, "width", "I");
  CHECK(!e->functions->ExceptionCheck(e) && widthF != nullptr &&
            e->functions->GetIntField(e, promoted, widthF) == 5,
        "CallVoidMethodA <init>(5,6) resets width==5");

  // Object 族实例形态（void 返回按 object 收 = null）
  jobject voidAsObj = e->functions->CallObjectMethodA(e, promoted, ctorII, cargs);
  CHECK(voidAsObj == nullptr, "CallObjectMethodA void result -> null");

  // 方法解析失败 -> NoSuchMethodError
  CHECK(e->functions->GetStaticMethodID(e, calcCls, "nope", "()V") == nullptr,
        "GetStaticMethodID missing -> null");
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "missing method pending NoSuchMethodError");
  e->functions->ExceptionClear(e);
  CHECK(e->functions->GetStaticMethodID(e, calcCls, "value", "()I") == nullptr,
        "GetStaticMethodID rejects instance method");
  e->functions->ExceptionClear(e);

  // ================= 字符串往返 =================
  jstring s1 = e->functions->NewStringUTF(e, "hello JNI");
  CHECK(s1 != nullptr && e->functions->GetStringLength(e, s1) == 9, "NewStringUTF + GetStringLength");
  {
    jboolean isCopy = JNI_FALSE;
    const char* utf = e->functions->GetStringUTFChars(e, s1, &isCopy);
    CHECK(utf != nullptr && isCopy == JNI_TRUE && std::strcmp(utf, "hello JNI") == 0,
          "GetStringUTFChars roundtrip");
    CHECK(e->functions->GetStringUTFLength(e, s1) == 9, "GetStringUTFLength");
    e->functions->ReleaseStringUTFChars(e, s1, utf);
  }
  // MUTF-8 内嵌 NUL（C0 80）：3 字符
  jstring s2 = e->functions->NewStringUTF(e, "a\xC0\x80" "b");
  CHECK(s2 != nullptr && e->functions->GetStringLength(e, s2) == 3,
        "NewStringUTF embedded NUL length 3");
  {
    const char* utf = e->functions->GetStringUTFChars(e, s2, nullptr);
    CHECK(utf != nullptr && std::strcmp(utf, "a\xC0\x80" "b") == 0,
          "GetStringUTFChars MUTF-8 embedded NUL roundtrip");
    e->functions->ReleaseStringUTFChars(e, s2, utf);
  }
  jchar wide[4] = {'w', 'i', 'd', 'e'};
  jstring s3 = e->functions->NewString(e, wide, 4);
  CHECK(s3 != nullptr && e->functions->GetStringLength(e, s3) == 4, "NewString");
  {
    jboolean isCopy = JNI_FALSE;
    const jchar* chars = e->functions->GetStringChars(e, s3, &isCopy);
    CHECK(chars != nullptr && isCopy == JNI_TRUE && std::memcmp(chars, wide, sizeof(wide)) == 0,
          "GetStringChars roundtrip");
    e->functions->ReleaseStringChars(e, s3, chars);
  }

  // ================= 异常 =================
  jclass npeCls = e->functions->FindClass(e, "java.lang.NullPointerException");
  e->functions->ThrowNew(e, npeCls, "boom");
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "ThrowNew pending");
  jthrowable occ = e->functions->ExceptionOccurred(e);
  CHECK(occ != nullptr && e->functions->GetObjectRefType(e, occ) == JNILocalRefType,
        "ExceptionOccurred local ref");
  jclass occCls = e->functions->GetObjectClass(e, occ);
  CHECK(occCls != nullptr && e->functions->IsSameObject(e, occCls, npeCls),
        "pending exception class");
  e->functions->ExceptionDescribe(e);  // 打印不抛
  e->functions->ExceptionClear(e);
  CHECK(e->functions->ExceptionCheck(e) == JNI_FALSE && e->functions->ExceptionOccurred(e) == nullptr,
        "ExceptionClear");

  jclass iseCls = e->functions->FindClass(e, "java.lang.IllegalStateException");
  jobject thrown = e->functions->AllocObject(e, iseCls);
  CHECK(e->functions->Throw(e, (jthrowable) thrown) == 0, "Throw");
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "Throw pending");
  CHECK(e->functions->IsSameObject(e, e->functions->ExceptionOccurred(e), thrown),
        "ExceptionOccurred identity");
  e->functions->ExceptionClear(e);

  // ================= 字段读写 =================
  jfieldID fI = e->functions->GetStaticFieldID(e, calcCls, "I", "I");
  jfieldID fL = e->functions->GetStaticFieldID(e, calcCls, "L", "J");
  jfieldID fD = e->functions->GetStaticFieldID(e, calcCls, "D", "D");
  jfieldID fF = e->functions->GetStaticFieldID(e, calcCls, "F", "F");
  jfieldID fSTR = e->functions->GetStaticFieldID(e, calcCls, "STR", "Ljava/lang/String;");
  CHECK(fI && fL && fD && fF && fSTR, "GetStaticFieldID set");
  CHECK(e->functions->GetStaticIntField(e, calcCls, fI) == 123456, "GetStaticIntField I==123456");
  CHECK(e->functions->GetStaticLongField(e, calcCls, fL) == 0x123456789ABCDEFL,
        "GetStaticLongField L");
  CHECK(e->functions->GetStaticDoubleField(e, calcCls, fD) == -2.25, "GetStaticDoubleField D");
  CHECK(e->functions->GetStaticFloatField(e, calcCls, fF) == 1.5f, "GetStaticFloatField F");
  {
    jstring sv = (jstring) e->functions->GetStaticObjectField(e, calcCls, fSTR);
    const char* utf = e->functions->GetStringUTFChars(e, sv, nullptr);
    CHECK(sv != nullptr && utf != nullptr && std::strcmp(utf, "hi") == 0, "GetStaticObjectField STR");
    e->functions->ReleaseStringUTFChars(e, sv, utf);
  }

  jfieldID widthF2 = e->functions->GetFieldID(e, calcCls, "width", "I");
  jfieldID heightF = e->functions->GetFieldID(e, calcCls, "height", "I");
  jfieldID baseF = e->functions->GetFieldID(e, calcCls, "base", "I");  // 继承字段（沿超类查找）
  CHECK(widthF2 && heightF && baseF, "GetFieldID width/height + inherited base");
  CHECK(e->functions->GetIntField(e, promoted, widthF2) == 5, "GetIntField width==5");
  CHECK(e->functions->GetIntField(e, promoted, heightF) == 6, "GetIntField height==6");
  e->functions->SetIntField(e, promoted, widthF2, 7);
  CHECK(e->functions->GetIntField(e, promoted, widthF2) == 7, "SetIntField width==7");
  // 继承字段读写（JNI 沿超类解析；注：core LinkClass M1 未串联父类字段偏移，
  // base 与 width 同槽，这里只验证 JNI 读写路径本身）
  e->functions->SetIntField(e, promoted, baseF, 77);
  CHECK(e->functions->GetIntField(e, promoted, baseF) == 77, "Set/GetIntField inherited base");

  // static final 写：EnsureModifiable 拒绝（docs 15.7）
  e->functions->SetStaticIntField(e, calcCls, fI, 1);
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "SetStaticIntField final -> IllegalAccessError");
  e->functions->ExceptionClear(e);

  // ================= 数组 =================
  jintArray ia = e->functions->NewIntArray(e, 8);
  CHECK(ia != nullptr && e->functions->GetArrayLength(e, (jarray) ia) == 8, "NewIntArray");
  jint in3[3] = {7, 8, 9};
  e->functions->SetIntArrayRegion(e, ia, 2, 3, in3);
  jint out3[3] = {};
  e->functions->GetIntArrayRegion(e, ia, 2, 3, out3);
  CHECK(std::memcmp(in3, out3, sizeof(in3)) == 0, "Set/GetIntArrayRegion roundtrip");
  {
    jboolean isCopy = JNI_FALSE;
    jint* elems = e->functions->GetIntArrayElements(e, ia, &isCopy);
    CHECK(elems != nullptr && isCopy == JNI_TRUE && elems[2] == 7, "GetIntArrayElements");
    elems[0] = 42;
    e->functions->ReleaseIntArrayElements(e, ia, elems, 0);  // 写回
    jint v = 0;
    e->functions->GetIntArrayRegion(e, ia, 0, 1, &v);
    CHECK(v == 42, "Release mode 0 writes back");
    jint* elems2 = e->functions->GetIntArrayElements(e, ia, nullptr);
    elems2[1] = 99;
    e->functions->ReleaseIntArrayElements(e, ia, elems2, JNI_ABORT);  // 丢弃
    e->functions->GetIntArrayRegion(e, ia, 1, 1, &v);
    CHECK(v == 0, "Release JNI_ABORT discards");
    jint* elems3 = e->functions->GetIntArrayElements(e, ia, nullptr);
    elems3[3] = -1;
    e->functions->ReleaseIntArrayElements(e, ia, elems3, JNI_COMMIT);  // 写回不释放
    std::free(elems3);
    e->functions->GetIntArrayRegion(e, ia, 3, 1, &v);
    CHECK(v == -1, "Release JNI_COMMIT writes back");
  }

  // 窄化槽（8 字节元素槽）
  jbyteArray ba = e->functions->NewByteArray(e, 2);
  jbyte bin[2] = {-5, 100};
  e->functions->SetByteArrayRegion(e, ba, 0, 2, bin);
  jbyte bout[2] = {};
  e->functions->GetByteArrayRegion(e, ba, 0, 2, bout);
  CHECK(bout[0] == -5 && bout[1] == 100, "ByteArray region narrowing");
  jbooleanArray za = e->functions->NewBooleanArray(e, 3);
  jboolean zin[3] = {1, 0, 1};
  e->functions->SetBooleanArrayRegion(e, za, 0, 3, zin);
  jboolean zout[3] = {};
  e->functions->GetBooleanArrayRegion(e, za, 0, 3, zout);
  CHECK(zout[0] == 1 && zout[1] == 0 && zout[2] == 1, "BooleanArray region");
  jcharArray ca = e->functions->NewCharArray(e, 2);
  jchar cin[2] = {0x41, 0xFFFF};
  e->functions->SetCharArrayRegion(e, ca, 0, 2, cin);
  jchar cout[2] = {};
  e->functions->GetCharArrayRegion(e, ca, 0, 2, cout);
  CHECK(cout[0] == 0x41 && cout[1] == 0xFFFF, "CharArray region");
  jlongArray ja = e->functions->NewLongArray(e, 2);
  jlong jin[2] = {0x123456789ABCLL, -2};
  e->functions->SetLongArrayRegion(e, ja, 0, 2, jin);
  jlong jout[2] = {};
  e->functions->GetLongArrayRegion(e, ja, 0, 2, jout);
  CHECK(jout[0] == 0x123456789ABCLL && jout[1] == -2, "LongArray region (wide slot)");

  // 对象数组
  jobjectArray oa = e->functions->NewObjectArray(e, 3, calcCls, promoted);
  CHECK(oa != nullptr && e->functions->GetArrayLength(e, (jarray) oa) == 3, "NewObjectArray");
  jobject e0 = e->functions->GetObjectArrayElement(e, oa, 0);
  CHECK(e0 != nullptr && e->functions->IsSameObject(e, e0, promoted), "GetObjectArrayElement init");
  jobject other = e->functions->NewObjectA(e, calcCls, ctorII, cargs);
  e->functions->SetObjectArrayElement(e, oa, 1, other);
  CHECK(e->functions->IsSameObject(e, e->functions->GetObjectArrayElement(e, oa, 1), other),
        "SetObjectArrayElement");
  e->functions->SetObjectArrayElement(e, oa, 0, s1);  // String 进 Calc[]：ArrayStoreException
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "ArrayStoreException on wrong component");
  e->functions->ExceptionClear(e);
  e->functions->GetObjectArrayElement(e, oa, 5);
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "ArrayIndexOutOfBounds");
  e->functions->ExceptionClear(e);

  // ================= IsInstanceOf / IsSameObject =================
  CHECK(e->functions->IsInstanceOf(e, promoted, calcCls) == JNI_TRUE, "IsInstanceOf Calc");
  CHECK(e->functions->IsInstanceOf(e, promoted, operandCls) == JNI_TRUE, "IsInstanceOf super");
  CHECK(e->functions->IsInstanceOf(e, promoted, shapeCls) == JNI_TRUE, "IsInstanceOf interface");
  CHECK(e->functions->IsInstanceOf(e, promoted, stringCls) == JNI_FALSE, "IsInstanceOf negative");
  CHECK(e->functions->IsInstanceOf(e, nullptr, calcCls) == JNI_TRUE, "IsInstanceOf(null) true");
  CHECK(e->functions->IsSameObject(e, promoted, promoted) == JNI_TRUE, "IsSameObject reflexive");
  CHECK(e->functions->IsSameObject(e, promoted, other) == JNI_FALSE, "IsSameObject different");
  jclass superOfCalc = e->functions->GetSuperclass(e, calcCls);
  CHECK(superOfCalc != nullptr && e->functions->IsSameObject(e, superOfCalc, operandCls),
        "GetSuperclass Calc -> Operand");
  CHECK(e->functions->GetSuperclass(e, shapeCls) == nullptr, "GetSuperclass(interface) null");

  // ================= Monitor 重入 =================
  jobject mon = e->functions->AllocObject(e, objectCls);
  CHECK(mon != nullptr, "AllocObject java.lang.Object");
  CHECK(e->functions->MonitorEnter(e, mon) == 0 && e->functions->MonitorEnter(e, mon) == 0,
        "MonitorEnter reentrant");
  CHECK(dexvm::jni::DecodeJObject(&env, mon)->monitor == 2, "monitor count == 2");
  CHECK(e->functions->MonitorExit(e, mon) == 0 && e->functions->MonitorExit(e, mon) == 0,
        "MonitorExit unwinds");
  CHECK(dexvm::jni::DecodeJObject(&env, mon)->monitor == 0, "monitor count == 0");
  e->functions->MonitorExit(e, mon);
  CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "MonitorExit underflow -> IllegalMonitorState");
  e->functions->ExceptionClear(e);

  // String 特判：AllocObject(String) -> 空 VString（AllocEmptyString 语义）
  jobject emptyStr = e->functions->AllocObject(e, stringCls);
  CHECK(emptyStr != nullptr && e->functions->GetStringLength(e, (jstring) emptyStr) == 0,
        "AllocObject(String) -> empty string");

  // ================= VM =================
  CHECK(e->functions->GetVersion(e) == JNI_VERSION_1_6, "GetVersion == JNI_VERSION_1_6");
  JavaVM* vmOut = nullptr;
  CHECK(e->functions->GetJavaVM(e, &vmOut) == JNI_OK && vmOut == (JavaVM*) &vm, "GetJavaVM");
  void* penv = nullptr;
  CHECK(vmOut->functions->GetEnv(vmOut, &penv, JNI_VERSION_1_6) == JNI_OK && penv == e,
        "JavaVM.GetEnv -> attached env");

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
