// JNI 全面（M2）冒烟测试（docs §28.3、docs 05/06/07 章）：
//   RegisterNatives/UnregisterNatives（'!' FastNative、继承链查找、非 native 拒绝、
//     三元组校验、@CriticalNative 延迟设置）+ native trampoline 调用（解释器 hook 入口）
//   System.loadLibrary 链路（dlopen 真实 .so -> JNI_OnLoad -> SetClassLoaderOverride
//     包裹 FindClass -> RegisterNatives -> 版本校验 -> 幂等/冲突 -> OnUnload/dlclose）
//   反射 ID 往返（Method/Constructor/Field 镜像骨架）
//   Call 族全 9 返回类型 + CallNonvirtual 族（虚/非虚派发差异）
//   GetStringRegion/GetStringUTFRegion、Get/SetPrimitiveArrayCritical/GetStringCritical 恒 pin
//   IsAssignableFrom（ART 方向）、EnsureLocalCapacity 真实容量核算
// 用法: jni_full_smoke <fixture.dex> <libfixture_native.so>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "jni/jni_env.h"
#include "jni/native_manager.h"
#include "jni/reflection.h"
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

// ===================== 测试 native 函数（JNI C ABI）=====================

int g_void_side_effect = 0;

jint JNICALL n_add(JNIEnv*, jclass, jint a, jint b) { return a + b; }
jobject JNICALL n_retObject(JNIEnv* env, jclass, jobject v) { (void) env; return v; }
jboolean JNICALL n_retBoolean(JNIEnv*, jclass, jboolean z) { return (jboolean) (z ? 0 : 1); }
jbyte JNICALL n_retByte(JNIEnv*, jclass, jbyte b) { return (jbyte) (b + 1); }
jchar JNICALL n_retChar(JNIEnv*, jclass, jchar c) { return (jchar) (c + 1); }
jshort JNICALL n_retShort(JNIEnv*, jclass, jshort s) { return (jshort) (s + 1); }
jint JNICALL n_retInt(JNIEnv*, jclass, jint i) { return i + 1; }
jlong JNICALL n_retLong(JNIEnv*, jclass, jlong j) { return j + 1; }
jfloat JNICALL n_retFloat(JNIEnv*, jclass, jfloat f) { return f * 2.0f; }
jdouble JNICALL n_retDouble(JNIEnv*, jclass, jdouble d) { return d * 2.0; }
void JNICALL n_retVoid(JNIEnv*, jclass, jint i) { g_void_side_effect = i; }

// 混合 GPR/FPR 交错参数（形状重排 ABI 验证）：(IDFJLjava/lang/String;)J
//（本 TU 用 jni_env.h：C 风格为 env->functions->X(env, ...)）
jlong JNICALL n_mix(JNIEnv* env, jclass, jint i, jdouble d, jfloat f, jlong j, jstring s) {
  jsize len = 0;
  if (s != nullptr) len = env->functions->GetStringLength(env, s);
  return i + (jlong) d + (jlong) f + j + len;
}

jstring JNICALL n_greet(JNIEnv* env, jclass, jstring s) {
  const char* utf = env->functions->GetStringUTFChars(env, s, nullptr);
  std::string out = std::string("n:") + (utf != nullptr ? utf : "");
  if (utf != nullptr) env->functions->ReleaseStringUTFChars(env, s, utf);
  return env->functions->NewStringUTF(env, out.c_str());
}

// 实例 native（self 忽略或参与）
jint JNICALL n_incr(JNIEnv*, jobject, jint v) { return v + 1; }
jlong JNICALL n_tag41(JNIEnv*, jobject) { return 41; }  // Pa.tag
jlong JNICALL n_tag42(JNIEnv*, jobject) { return 42; }  // Ch.tag（覆盖）

// V 形态变参桥
jlong CallStaticLongV(JNIEnv* e, jclass cls, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  jlong r = e->functions->CallStaticLongMethodV(e, cls, mid, ap);
  va_end(ap);
  return r;
}
jint CallNonvirtualIntV(JNIEnv* e, jobject obj, jclass cls, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  jint r = e->functions->CallNonvirtualIntMethodV(e, obj, cls, mid, ap);
  va_end(ap);
  return r;
}
jint CallIntV(JNIEnv* e, jobject obj, jmethodID mid, ...) {
  va_list ap;
  va_start(ap, mid);
  jint r = e->functions->CallIntMethodV(e, obj, mid, ap);
  va_end(ap);
  return r;
}

}  // namespace

// .so 的 JNI_OnLoad 经此探针观察 ClassLoader override（主程序 -export_dynamic 导出）
extern "C" void* dexvm_test_loader_probe(void* env) {
  return static_cast<dexvm::jni::JNIEnvExt*>(env)->class_loader;
}

int main(int argc, char** argv) {
  const char* dex_path = argc > 1 ? argv[1] : "app/src/main/cpp/tests/out/fixture.dex";
  const char* so_path = argc > 2 ? argv[2] : "";

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

  dexvm::jni::JavaVMExt vm;
  dexvm::jni::InitJavaVMExt(&vm);
  dexvm::jni::JNIEnvExt env;
  dexvm::jni::InitJNIEnvExt(&env, &vm, &state);
  JNIEnv* e = &env;

  using dexvm::runtime::ClassStatus;
  using dexvm::runtime::ObjHeader;
  using dexvm::runtime::Value;
  using dexvm::runtime::VClass;
  using dexvm::runtime::VMethod;
  using dexvm::runtime::VObject;

  // 骨架类工具（测试内构造 native 方法宿主类）
  auto MakeSkel = [&](const char* desc, VClass* super) -> VClass* {
    void* mem = heap.Alloc(sizeof(VClass));
    VClass* k = new (mem) VClass();
    k->descriptor = desc;
    k->super_class = super;
    k->status = ClassStatus::kInitialized;
    k->object_size = sizeof(ObjHeader);
    k->klass = linker.FindClass("Ljava/lang/Class;");
    linker.RegisterBuiltinClass(k);
    return k;
  };
  auto AddNative = [&](VClass* k, const char* name, std::vector<std::string> params,
                       const char* ret, bool is_static, bool is_virtual) -> VMethod* {
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
    m.access_flags = 0x100u /*native*/ | (is_static ? 0x8u : 0u) | (is_virtual ? 0x1u : 0x2u);
    m.code = nullptr;
    if (is_virtual) {
      k->virtual_methods.push_back(std::move(m));
      return &k->virtual_methods.back();
    }
    k->direct_methods.push_back(std::move(m));
    return &k->direct_methods.back();
  };

  jclass objectCls = e->functions->FindClass(e, "java.lang.Object");
  jclass stringCls = e->functions->FindClass(e, "java.lang.String");

  // =================================================================
  // 1. 反射 ID 往返（java/lang/reflect 镜像骨架）
  // =================================================================
  std::printf("---- 反射 ID ----\n");
  dexvm::jni::EnsureReflectionClasses(&linker);
  jclass methodCls = e->functions->FindClass(e, "java.lang.reflect.Method");
  jclass ctorCls = e->functions->FindClass(e, "java.lang.reflect.Constructor");
  jclass fieldCls = e->functions->FindClass(e, "java.lang.reflect.Field");
  CHECK(methodCls != nullptr && ctorCls != nullptr && fieldCls != nullptr,
        "reflect mirror classes registered");

  jclass calcCls = e->functions->FindClass(e, "fixture.Calc");
  jclass operandCls = e->functions->FindClass(e, "fixture.Operand");
  jclass shapeCls = e->functions->FindClass(e, "fixture.Shape");
  CHECK(calcCls != nullptr && operandCls != nullptr, "FindClass fixture classes");

  jmethodID ctorII = e->functions->GetMethodID(e, calcCls, "<init>", "(II)V");
  jmethodID addMid = e->functions->GetStaticMethodID(e, calcCls, "add", "(II)I");
  jfieldID widthF = e->functions->GetFieldID(e, calcCls, "width", "I");
  CHECK(ctorII != nullptr && addMid != nullptr && widthF != nullptr, "fixture IDs");

  jobject ctorObj = e->functions->ToReflectedMethod(e, calcCls, ctorII, JNI_FALSE);
  CHECK(ctorObj != nullptr &&
            e->functions->IsSameObject(e, e->functions->GetObjectClass(e, ctorObj), ctorCls),
        "ToReflectedMethod <init> -> Constructor mirror");
  CHECK(e->functions->FromReflectedMethod(e, ctorObj) == ctorII,
        "FromReflectedMethod(ctor) roundtrip");

  jobject mObj = e->functions->ToReflectedMethod(e, calcCls, addMid, JNI_TRUE);
  CHECK(mObj != nullptr &&
            e->functions->IsSameObject(e, e->functions->GetObjectClass(e, mObj), methodCls),
        "ToReflectedMethod add -> Method mirror");
  CHECK(e->functions->FromReflectedMethod(e, mObj) == addMid, "FromReflectedMethod(add) roundtrip");

  jobject fObj = e->functions->ToReflectedField(e, calcCls, widthF, JNI_FALSE);
  CHECK(fObj != nullptr &&
            e->functions->IsSameObject(e, e->functions->GetObjectClass(e, fObj), fieldCls),
        "ToReflectedField width -> Field mirror");
  CHECK(e->functions->FromReflectedField(e, fObj) == widthF, "FromReflectedField roundtrip");
  CHECK(e->functions->FromReflectedMethod(e, fObj) == nullptr, "FromReflectedMethod on Field -> null");
  CHECK(e->functions->FromReflectedField(e, mObj) == nullptr, "FromReflectedField on Method -> null");

  // =================================================================
  // 2. RegisterNatives：注册 C 函数 -> native trampoline 调用
  // =================================================================
  std::printf("---- RegisterNatives / trampoline ----\n");
  VClass* host = MakeSkel("Lfixture/NativeHost;", linker.FindClass("Ljava/lang/Object;"));
  VClass* inHost = MakeSkel("Lfixture/InstanceHost;", linker.FindClass("Ljava/lang/Object;"));
  // 注意：VMethod 存于 vector，push_back 后旧指针失效——先全部声明，再取指针
  AddNative(host, "snAdd", {"I", "I"}, "I", true, false);
  AddNative(host, "snMix", {"I", "D", "F", "J", "Ljava/lang/String;"}, "J", true, false);
  AddNative(host, "snGreet", {"Ljava/lang/String;"}, "Ljava/lang/String;", true, false);
  AddNative(host, "soAdd", {"I", "I"}, "I", true, false);  // .so JNI_OnLoad 注册目标
  AddNative(inHost, "incr", {"I"}, "I", false, true);
  AddNative(inHost, "setV", {"I"}, "V", false, true);

  jclass hostCls = e->functions->FindClass(e, "fixture.NativeHost");
  jclass inHostCls = e->functions->FindClass(e, "fixture.InstanceHost");
  CHECK(hostCls != nullptr && inHostCls != nullptr, "skeleton host classes");

  {
    JNINativeMethod methods[3];
    methods[0] = {"snAdd", "!(II)I", (void*) n_add};
    methods[1] = {"snMix", "(IDFJLjava/lang/String;)J", (void*) n_mix};
    methods[2] = {"snGreet", "(Ljava/lang/String;)Ljava/lang/String;", (void*) n_greet};
    CHECK(e->functions->RegisterNatives(e, hostCls, methods, 3) == JNI_OK,
          "RegisterNatives static trio");
    JNINativeMethod m1[2];
    m1[0] = {"incr", "(I)I", (void*) n_incr};
    m1[1] = {"setV", "!(I)V", (void*) n_retVoid};
    CHECK(e->functions->RegisterNatives(e, inHostCls, m1, 2) == JNI_OK, "RegisterNatives instance");
  }
  CHECK(dexvm::jni::DecodeMethodId(e->functions->GetStaticMethodID(e, hostCls, "snAdd", "(II)I"))
                ->jni_native_fn == (void*) n_add,
        "native_fn 登记（SetEntryPointFromJni 等价）");
  CHECK((dexvm::jni::DecodeMethodId(e->functions->GetStaticMethodID(e, hostCls, "snAdd", "(II)I"))
             ->access_flags & dexvm::jni::kAccFastNative) != 0,
        "'!' 前缀 -> FastNative 标记");
  CHECK(dexvm::jni::DecodeMethodId(e->functions->GetMethodID(e, inHostCls, "setV", "(I)V"))
                ->jni_native_fn == (void*) n_retVoid,
        "'!' 前缀 (I)V -> FastNative");

  // 调用：CallStaticIntMethod -> InvokeJni -> native trampoline
  jint sum = e->functions->CallStaticIntMethod(e, hostCls, e->functions->GetStaticMethodID(
      e, hostCls, "snAdd", "(II)I"), 2, 3);
  CHECK(!e->functions->ExceptionCheck(e) && sum == 5, "CallStaticIntMethod snAdd(2,3)==5 (native)");
  {
    jvalue args[2];
    args[0].i = 40;
    args[1].i = 2;
    sum = e->functions->CallStaticIntMethodA(e, hostCls,
                                            e->functions->GetStaticMethodID(e, hostCls, "snAdd",
                                                                            "(II)I"), args);
    CHECK(sum == 42, "CallStaticIntMethodA snAdd(40,2)==42 (native)");
  }
  // 混合参数形状（GPR/FPR 交错）
  {
    jstring s = e->functions->NewStringUTF(e, "abcd");  // len 4
    jmethodID mixMid = e->functions->GetStaticMethodID(e, hostCls, "snMix",
                                                       "(IDFJLjava/lang/String;)J");
    jlong r = e->functions->CallStaticLongMethod(e, hostCls, mixMid, 1, 2.5, 1.5f, (jlong) 10, s);
    CHECK(!e->functions->ExceptionCheck(e) && r == 1 + 2 + 1 + 10 + 4,
          "snMix mixed GPR/FPR marshalling");
    CHECK(CallStaticLongV(e, hostCls, mixMid, 1, 2.5, 1.5f, (jlong) 10, s) == 18, "snMix V form");
    e->functions->DeleteLocalRef(e, s);
  }
  // object 参数/返回（local ref 往返）
  {
    jstring s = e->functions->NewStringUTF(e, "hi");
    jmethodID greetMid = e->functions->GetStaticMethodID(e, hostCls, "snGreet",
                                                        "(Ljava/lang/String;)Ljava/lang/String;");
    jstring out = (jstring) e->functions->CallStaticObjectMethod(e, hostCls, greetMid, s);
    const char* utf = e->functions->GetStringUTFChars(e, out, nullptr);
    CHECK(utf != nullptr && std::strcmp(utf, "n:hi") == 0, "snGreet object arg/ret");
    e->functions->ReleaseStringUTFChars(e, out, utf);
    e->functions->DeleteLocalRef(e, s);
    e->functions->DeleteLocalRef(e, out);
  }
  // 实例 native
  jobject recv = e->functions->AllocObject(e, inHostCls);
  jmethodID incrMid = e->functions->GetMethodID(e, inHostCls, "incr", "(I)I");
  CHECK(e->functions->CallIntMethod(e, recv, incrMid, 41) == 42, "CallIntMethod incr (instance native)");
  CHECK(CallIntV(e, recv, incrMid, 9) == 10, "CallIntMethodV incr (instance native)");
  jmethodID setVMid = e->functions->GetMethodID(e, inHostCls, "setV", "(I)V");
  e->functions->CallVoidMethod(e, recv, setVMid, 77);
  CHECK(g_void_side_effect == 77, "CallVoidMethod setV side effect");

  // 解释器 hook 入口直调（core/interpreter.cc InvokeMethod 的后置 hook 点）
  {
    Value args[2];
    args[0].i = 100;
    args[1].i = 1;
    Value r{};
    bool ok = dexvm::jni::InvokeNativeTrampoline(&state,
                 dexvm::jni::DecodeMethodId(
                     e->functions->GetStaticMethodID(e, hostCls, "snAdd", "(II)I")),
                 args, 2, &r);
    CHECK(ok && r.l == 101, "InvokeNativeTrampoline direct (interpreter hook entry)");
  }

  // =================================================================
  // 3. RegisterNatives 错误面
  // =================================================================
  std::printf("---- RegisterNatives 错误面 ----\n");
  {
    JNINativeMethod bad;
    bad.name = nullptr;
    bad.signature = "(II)I";
    bad.fnPtr = (void*) n_add;
    CHECK(e->functions->RegisterNatives(e, hostCls, &bad, 1) == JNI_ERR, "null name -> JNI_ERR");
    CHECK(e->functions->RegisterNatives(e, hostCls, &bad, -1) == JNI_ERR,
          "negative count -> JNI_ERR");
    CHECK(e->functions->RegisterNatives(e, hostCls, nullptr, 0) == JNI_OK,
          "count 0 -> JNI_OK (warning)");
    e->functions->ExceptionClear(e);

    JNINativeMethod missing = {"nope", "()V", (void*) n_retVoid};
    CHECK(e->functions->RegisterNatives(e, hostCls, &missing, 1) == JNI_ERR,
          "missing method -> JNI_ERR");
    CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "missing method pending NoSuchMethodError");
    e->functions->ExceptionClear(e);

    // 非 native 目标（fixture.Calc.add 有码）-> NoSuchMethodError
    JNINativeMethod nonNative = {"add", "(II)I", (void*) n_add};
    CHECK(e->functions->RegisterNatives(e, calcCls, &nonNative, 1) == JNI_ERR,
          "non-native target -> JNI_ERR");
    CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "non-native target pending NoSuchMethodError");
    e->functions->ExceptionClear(e);
  }

  // 继承链：子类 RegisterNatives 命中父类声明
  {
    VClass* parent = MakeSkel("Lfixture/RegParent;", linker.FindClass("Ljava/lang/Object;"));
    VClass* child = MakeSkel("Lfixture/RegChild;", parent);
    AddNative(parent, "pnAdd", {"I", "I"}, "I", true, false);
    jclass childCls = e->functions->FindClass(e, "fixture.RegChild");
    jclass pCls = e->functions->FindClass(e, "fixture.RegParent");
    JNINativeMethod m = {"pnAdd", "(II)I", (void*) n_add};
    CHECK(e->functions->RegisterNatives(e, childCls, &m, 1) == JNI_OK,
          "RegisterNatives via subclass hits parent declaration");
    CHECK(dexvm::jni::DecodeMethodId(e->functions->GetStaticMethodID(e, pCls, "pnAdd", "(II)I"))
                  ->jni_native_fn == (void*) n_add,
          "parent method entry set");
    jmethodID pm = e->functions->GetStaticMethodID(e, pCls, "pnAdd", "(II)I");
    CHECK(e->functions->CallStaticIntMethod(e, pCls, pm, 1, 2) == 3, "parent-declared native call");
  }

  // =================================================================
  // 4. @CriticalNative 延迟设置
  // =================================================================
  std::printf("---- @CriticalNative 延迟设置 ----\n");
  {
    VClass* k = MakeSkel("Lfixture/CritHost;", linker.FindClass("Ljava/lang/Object;"));
    AddNative(k, "crit", {}, "J", true, false);
    AddNative(k, "norm", {}, "J", true, false);
    jclass kCls = e->functions->FindClass(e, "fixture.CritHost");
    VMethod* crit = dexvm::jni::DecodeMethodId(e->functions->GetStaticMethodID(e, kCls, "crit", "()J"));
    VMethod* norm = dexvm::jni::DecodeMethodId(e->functions->GetStaticMethodID(e, kCls, "norm", "()J"));
    crit->access_flags |= dexvm::jni::kAccCriticalNative;
    k->status = ClassStatus::kResolved;  // 未 visibly initialized
    JNINativeMethod m[2] = {{"crit", "()J", (void*) n_tag41}, {"norm", "()J", (void*) n_tag42}};
    CHECK(e->functions->RegisterNatives(e, kCls, m, 2) == JNI_OK, "RegisterNatives critical pair");
    CHECK(crit->jni_native_fn == nullptr, "@CriticalNative 延迟设置（类未初始化）");
    CHECK(norm->jni_native_fn == (void*) n_tag42, "普通 native 立即设置");
    k->status = ClassStatus::kInitialized;  // 模拟初始化完成
    dexvm::jni::FlushDeferredNativeRegistrations(&vm.natives);
    CHECK(crit->jni_native_fn == (void*) n_tag41, "FlushDeferredNativeRegistrations 应用延迟项");
  }

  // =================================================================
  // 5. CallNonvirtual 族（虚/非虚派发差异）
  // =================================================================
  std::printf("---- CallNonvirtual ----\n");
  {
    VClass* pa = MakeSkel("Lfixture/TagParent;", linker.FindClass("Ljava/lang/Object;"));
    VClass* ch = MakeSkel("Lfixture/TagChild;", pa);
    VMethod* paTag = AddNative(pa, "tag", {}, "J", false, true);
    VMethod* chTag = AddNative(ch, "tag", {}, "J", false, true);
    jclass paCls = e->functions->FindClass(e, "fixture.TagParent");
    jclass chCls = e->functions->FindClass(e, "fixture.TagChild");
    JNINativeMethod mp = {"tag", "()J", (void*) n_tag41};
    JNINativeMethod mc = {"tag", "()J", (void*) n_tag42};
    CHECK(e->functions->RegisterNatives(e, paCls, &mp, 1) == JNI_OK &&
              e->functions->RegisterNatives(e, chCls, &mc, 1) == JNI_OK,
          "RegisterNatives override pair");
    jobject chObj = e->functions->AllocObject(e, chCls);
    jmethodID paMid = e->functions->GetMethodID(e, paCls, "tag", "()J");
    jmethodID chMid = e->functions->GetMethodID(e, chCls, "tag", "()J");
    CHECK(e->functions->CallLongMethod(e, chObj, paMid) == 42, "virtual dispatch -> child impl");
    CHECK(e->functions->CallNonvirtualLongMethod(e, chObj, paCls, paMid) == 41,
          "CallNonvirtualLongMethod -> parent impl");
    CHECK(e->functions->CallNonvirtualLongMethodA(e, chObj, paCls, paMid, nullptr) == 41,
          "CallNonvirtualLongMethodA");
    CHECK(e->functions->CallNonvirtualLongMethodV(e, chObj, paCls, paMid, nullptr) == 41,
          "CallNonvirtualLongMethodV");
    CHECK(e->functions->CallLongMethod(e, chObj, chMid) == 42, "child mid direct virtual");
    // interpreted 方法非虚（fixture.Operand.value，无覆盖 -> 与虚派发同值）
    jvalue cargs[2];
    cargs[0].i = 3;
    cargs[1].i = 4;
    jobject calc = e->functions->NewObjectA(e, calcCls, ctorII, cargs);  // base=7
    jmethodID valueMid = e->functions->GetMethodID(e, operandCls, "value", "()I");
    CHECK(e->functions->CallNonvirtualIntMethod(e, calc, operandCls, valueMid) == 7,
          "CallNonvirtualIntMethod interpreted");
    CHECK(CallNonvirtualIntV(e, calc, operandCls, valueMid) == 7, "CallNonvirtualIntMethodV");
    {
      jvalue noargs[1];
      CHECK(e->functions->CallNonvirtualIntMethodA(e, calc, operandCls, valueMid, noargs) == 7,
            "CallNonvirtualIntMethodA");
    }
    // CallNonvirtual 全类型族（同一批 idX 实例 native）
    (void) paTag;
    (void) chTag;
  }

  // =================================================================
  // 6. Call 族全 9 返回类型（instance 变参 / static A / nonvirtual A）
  // =================================================================
  std::printf("---- Call 全类型族 ----\n");
  {
    VClass* fam = MakeSkel("Lfixture/Family;", linker.FindClass("Ljava/lang/Object;"));
    // 静态族（idX 双份：static 与 instance 同 C 函数）
    struct FamDef {
      const char* name;
      const char* sig;
      void* fn;
    } defs[] = {
        {"sObject", "(Ljava/lang/Object;)Ljava/lang/Object;", (void*) n_retObject},
        {"sBoolean", "(Z)Z", (void*) n_retBoolean},
        {"sByte", "(B)B", (void*) n_retByte},
        {"sChar", "(C)C", (void*) n_retChar},
        {"sShort", "(S)S", (void*) n_retShort},
        {"sInt", "(I)I", (void*) n_retInt},
        {"sLong", "(J)J", (void*) n_retLong},
        {"sFloat", "(F)F", (void*) n_retFloat},
        {"sDouble", "(D)D", (void*) n_retDouble},
        {"sVoid", "(I)V", (void*) n_retVoid},
    };
    for (const auto& d : defs) {
      std::string sig(d.sig);
      AddNative(fam, d.name, {sig.substr(1, sig.find(')') - 1)}, sig.substr(sig.find(')') + 1).c_str(),
                true, false);
    }
    // instance 族
    struct FamDef idefs[] = {
        {"iObject", "(Ljava/lang/Object;)Ljava/lang/Object;", (void*) n_retObject},
        {"iBoolean", "(Z)Z", (void*) n_retBoolean},
        {"iByte", "(B)B", (void*) n_retByte},
        {"iChar", "(C)C", (void*) n_retChar},
        {"iShort", "(S)S", (void*) n_retShort},
        {"iInt", "(I)I", (void*) n_retInt},
        {"iLong", "(J)J", (void*) n_retLong},
        {"iFloat", "(F)F", (void*) n_retFloat},
        {"iDouble", "(D)D", (void*) n_retDouble},
        {"iVoid", "(I)V", (void*) n_retVoid},
    };
    for (const auto& d : idefs) {
      std::string sig(d.sig);
      AddNative(fam, d.name, {sig.substr(1, sig.find(')') - 1)}, sig.substr(sig.find(')') + 1).c_str(),
                false, true);
    }
    jclass famCls = e->functions->FindClass(e, "fixture.Family");
    JNINativeMethod regs[20];
    int ri = 0;
    for (const auto& d : defs) regs[ri++] = {d.name, d.sig, d.fn};
    for (const auto& d : idefs) regs[ri++] = {d.name, d.sig, d.fn};
    CHECK(e->functions->RegisterNatives(e, famCls, regs, ri) == JNI_OK, "RegisterNatives family");

    auto SMid = [&](const char* name, const char* sig) {
      return e->functions->GetStaticMethodID(e, famCls, name, sig);
    };
    auto IMid = [&](const char* name, const char* sig) {
      return e->functions->GetMethodID(e, famCls, name, sig);
    };
    jobject rObj = e->functions->AllocObject(e, objectCls);

    // static：A 形态全类型
    {
      jvalue a;
      a.l = rObj;
      CHECK(e->functions->CallStaticObjectMethodA(e, famCls, SMid("sObject",
          "(Ljava/lang/Object;)Ljava/lang/Object;"), &a) != nullptr, "static Object A");
      a.z = 1;
      CHECK(e->functions->CallStaticBooleanMethodA(e, famCls, SMid("sBoolean", "(Z)Z"), &a) ==
                JNI_FALSE, "static Boolean A");
      a.b = 5;
      CHECK(e->functions->CallStaticByteMethodA(e, famCls, SMid("sByte", "(B)B"), &a) == 6,
            "static Byte A");
      a.c = 0x41;
      CHECK(e->functions->CallStaticCharMethodA(e, famCls, SMid("sChar", "(C)C"), &a) == 0x42,
            "static Char A");
      a.s = -3;
      CHECK(e->functions->CallStaticShortMethodA(e, famCls, SMid("sShort", "(S)S"), &a) == -2,
            "static Short A");
      a.i = 10;
      CHECK(e->functions->CallStaticIntMethodA(e, famCls, SMid("sInt", "(I)I"), &a) == 11,
            "static Int A");
      a.j = (jlong) 1 << 40;
      CHECK(e->functions->CallStaticLongMethodA(e, famCls, SMid("sLong", "(J)J"), &a) ==
                ((jlong) 1 << 40) + 1, "static Long A");
      a.f = 1.25f;
      CHECK(e->functions->CallStaticFloatMethodA(e, famCls, SMid("sFloat", "(F)F"), &a) == 2.5f,
            "static Float A");
      a.d = 1.5;
      CHECK(e->functions->CallStaticDoubleMethodA(e, famCls, SMid("sDouble", "(D)D"), &a) == 3.0,
            "static Double A");
      a.i = 5;
      e->functions->CallStaticVoidMethodA(e, famCls, SMid("sVoid", "(I)V"), &a);
      CHECK(g_void_side_effect == 5, "static Void A");
    }
    // instance：变参形态全类型
    jobject fObj2 = e->functions->AllocObject(e, famCls);
    CHECK(e->functions->CallObjectMethod(e, fObj2, IMid("iObject",
              "(Ljava/lang/Object;)Ljava/lang/Object;"), rObj) != nullptr, "instance Object varargs");
    CHECK(e->functions->CallBooleanMethod(e, fObj2, IMid("iBoolean", "(Z)Z"), 1) == JNI_FALSE,
          "instance Boolean varargs");
    CHECK(e->functions->CallByteMethod(e, fObj2, IMid("iByte", "(B)B"), 5) == 6,
          "instance Byte varargs");
    CHECK(e->functions->CallCharMethod(e, fObj2, IMid("iChar", "(C)C"), 0x41) == 0x42,
          "instance Char varargs");
    CHECK(e->functions->CallShortMethod(e, fObj2, IMid("iShort", "(S)S"), -3) == -2,
          "instance Short varargs");
    CHECK(e->functions->CallIntMethod(e, fObj2, IMid("iInt", "(I)I"), 10) == 11,
          "instance Int varargs");
    CHECK(e->functions->CallLongMethod(e, fObj2, IMid("iLong", "(J)J"), (jlong) 100) == 101,
          "instance Long varargs");
    CHECK(e->functions->CallFloatMethod(e, fObj2, IMid("iFloat", "(F)F"), 1.25f) == 2.5f,
          "instance Float varargs");
    CHECK(e->functions->CallDoubleMethod(e, fObj2, IMid("iDouble", "(D)D"), 1.5) == 3.0,
          "instance Double varargs");
    e->functions->CallVoidMethod(e, fObj2, IMid("iVoid", "(I)V"), 9);
    CHECK(g_void_side_effect == 9, "instance Void varargs");

    // nonvirtual：A 形态全类型（同 mid 直调）
    {
      jvalue a;
      a.l = rObj;
      CHECK(e->functions->CallNonvirtualObjectMethodA(e, fObj2, famCls, IMid("iObject",
          "(Ljava/lang/Object;)Ljava/lang/Object;"), &a) != nullptr, "nonvirtual Object A");
      a.i = 10;
      CHECK(e->functions->CallNonvirtualIntMethodA(e, fObj2, famCls, IMid("iInt", "(I)I"), &a) == 11,
            "nonvirtual Int A");
      a.j = 100;
      CHECK(e->functions->CallNonvirtualLongMethodA(e, fObj2, famCls, IMid("iLong", "(J)J"), &a) ==
                101, "nonvirtual Long A");
      a.d = 1.5;
      CHECK(e->functions->CallNonvirtualDoubleMethodA(e, fObj2, famCls, IMid("iDouble", "(D)D"),
                                                      &a) == 3.0, "nonvirtual Double A");
      a.f = 1.25f;
      CHECK(e->functions->CallNonvirtualFloatMethodA(e, fObj2, famCls, IMid("iFloat", "(F)F"), &a) ==
                2.5f, "nonvirtual Float A");
      a.b = 5;
      CHECK(e->functions->CallNonvirtualByteMethodA(e, fObj2, famCls, IMid("iByte", "(B)B"), &a) == 6,
            "nonvirtual Byte A");
      a.c = 0x41;
      CHECK(e->functions->CallNonvirtualCharMethodA(e, fObj2, famCls, IMid("iChar", "(C)C"), &a) ==
                0x42, "nonvirtual Char A");
      a.s = -3;
      CHECK(e->functions->CallNonvirtualShortMethodA(e, fObj2, famCls, IMid("iShort", "(S)S"), &a) ==
                -2, "nonvirtual Short A");
      a.z = 1;
      CHECK(e->functions->CallNonvirtualBooleanMethodA(e, fObj2, famCls, IMid("iBoolean", "(Z)Z"),
                                                       &a) == JNI_FALSE, "nonvirtual Boolean A");
      a.i = 3;
      e->functions->CallNonvirtualVoidMethodA(e, fObj2, famCls, IMid("iVoid", "(I)V"), &a);
      CHECK(g_void_side_effect == 3, "nonvirtual Void A");
    }
    // static V 形态 spot check
    CHECK(CallStaticLongV(e, famCls, SMid("sLong", "(J)J"), (jlong) 7) == 8, "static Long V form");

    // UnregisterNatives：清登记 -> UnsatisfiedLinkError
    CHECK(e->functions->UnregisterNatives(e, famCls) == JNI_OK, "UnregisterNatives");
    e->functions->CallIntMethod(e, fObj2, IMid("iInt", "(I)I"), 1);
    CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "unregistered native -> UnsatisfiedLinkError");
    e->functions->ExceptionClear(e);
  }

  // =================================================================
  // 7. 字符串区域 + Critical 恒 pin
  // =================================================================
  std::printf("---- String region / Critical ----\n");
  {
    jstring s = e->functions->NewStringUTF(e, "hello JNI");
    jchar buf[16] = {};
    e->functions->GetStringRegion(e, s, 6, 3, buf);
    CHECK(buf[0] == 'J' && buf[1] == 'N' && buf[2] == 'I', "GetStringRegion content");
    e->functions->GetStringRegion(e, s, 0, 5, buf);
    CHECK(buf[0] == 'h' && buf[4] == 'o', "GetStringRegion prefix");
    e->functions->GetStringRegion(e, s, 8, 5, buf);
    CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "GetStringRegion OOB -> SIOOBE");
    e->functions->ExceptionClear(e);
    char ubuf[32] = {};
    e->functions->GetStringUTFRegion(e, s, 6, 3, ubuf);
    CHECK(std::strcmp(ubuf, "JNI") == 0, "GetStringUTFRegion content + NUL");
    jstring sn = e->functions->NewStringUTF(e, "a\xC0\x80" "b");
    char nbuf[16] = {};
    e->functions->GetStringUTFRegion(e, sn, 0, 3, nbuf);
    CHECK(std::strcmp(nbuf, "a\xC0\x80" "b") == 0, "GetStringUTFRegion embedded NUL (MUTF-8)");
    e->functions->GetStringUTFRegion(e, sn, 1, 5, nbuf);
    CHECK(e->functions->ExceptionCheck(e) == JNI_TRUE, "GetStringUTFRegion OOB -> SIOOBE");
    e->functions->ExceptionClear(e);

    // GetStringCritical：恒 pin（is_copy=FALSE）
    jboolean sc = JNI_TRUE;
    const jchar* cp = e->functions->GetStringCritical(e, s, &sc);
    CHECK(cp != nullptr && sc == JNI_FALSE && cp[0] == 'h' && cp[8] == 'I',
          "GetStringCritical pin read");
    e->functions->ReleaseStringCritical(e, s, cp);

    // GetPrimitiveArrayCritical：恒 pin + Release 同步修改
    jintArray ia = e->functions->NewIntArray(e, 4);
    jint init[4] = {1, 2, 3, 4};
    e->functions->SetIntArrayRegion(e, ia, 0, 4, init);
    jboolean ac = JNI_TRUE;
    void* p = e->functions->GetPrimitiveArrayCritical(e, (jarray) ia, &ac);
    CHECK(p != nullptr && ac == JNI_FALSE, "GetPrimitiveArrayCritical pin (is_copy=FALSE)");
    jint* ip = static_cast<jint*>(p);
    ip[1] = 99;
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ia, p, 0);
    jint out[4] = {};
    e->functions->GetIntArrayRegion(e, ia, 0, 4, out);
    CHECK(out[0] == 1 && out[1] == 99 && out[2] == 3 && out[3] == 4,
          "Release mode 0 同步修改写回");
    // JNI_ABORT 丢弃
    p = e->functions->GetPrimitiveArrayCritical(e, (jarray) ia, &ac);
    static_cast<jint*>(p)[2] = -1;
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ia, p, JNI_ABORT);
    e->functions->GetIntArrayRegion(e, ia, 0, 4, out);
    CHECK(out[2] == 3, "Release JNI_ABORT discards");
    // JNI_COMMIT 写回不解除 pin
    p = e->functions->GetPrimitiveArrayCritical(e, (jarray) ia, &ac);
    static_cast<jint*>(p)[3] = 44;
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ia, p, JNI_COMMIT);
    void* p2 = e->functions->GetPrimitiveArrayCritical(e, (jarray) ia, &ac);
    CHECK(p2 == p, "JNI_COMMIT keeps pin (same pointer)");
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ia, p2, 0);
    e->functions->GetIntArrayRegion(e, ia, 3, 1, out);
    CHECK(out[0] == 44, "JNI_COMMIT wrote back");
    // 嵌套 pin：同指针
    p = e->functions->GetPrimitiveArrayCritical(e, (jarray) ia, &ac);
    p2 = e->functions->GetPrimitiveArrayCritical(e, (jarray) ia, &ac);
    CHECK(p == p2, "nested critical -> same pin");
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ia, p, 0);
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ia, p2, 0);
    // byte 数组（1 字节元素宽）
    jbyteArray ba = e->functions->NewByteArray(e, 2);
    jbyte bin[2] = {-5, 100};
    e->functions->SetByteArrayRegion(e, ba, 0, 2, bin);
    void* bp = e->functions->GetPrimitiveArrayCritical(e, (jarray) ba, &ac);
    static_cast<jbyte*>(bp)[0] = 7;
    e->functions->ReleasePrimitiveArrayCritical(e, (jarray) ba, bp, 0);
    jbyte bout[2] = {};
    e->functions->GetByteArrayRegion(e, ba, 0, 2, bout);
    CHECK(bout[0] == 7 && bout[1] == 100, "byte critical pin write-back");
  }

  // =================================================================
  // 8. IsAssignableFrom（ART 方向）+ EnsureLocalCapacity 核算
  // =================================================================
  std::printf("---- IsAssignableFrom / EnsureLocalCapacity ----\n");
  CHECK(e->functions->IsAssignableFrom(e, calcCls, operandCls) == JNI_TRUE,
        "IsAssignableFrom: Calc -> Operand");
  CHECK(e->functions->IsAssignableFrom(e, operandCls, calcCls) == JNI_FALSE,
        "IsAssignableFrom: Operand -/-> Calc");
  CHECK(e->functions->IsAssignableFrom(e, calcCls, shapeCls) == JNI_TRUE,
        "IsAssignableFrom: Calc -> Shape(interface)");
  CHECK(e->functions->IsAssignableFrom(e, calcCls, objectCls) == JNI_TRUE,
        "IsAssignableFrom: Calc -> Object");
  CHECK(e->functions->IsAssignableFrom(e, calcCls, stringCls) == JNI_FALSE,
        "IsAssignableFrom: Calc -/-> String");
  {
    jintArray ia = e->functions->NewIntArray(e, 1);
    jclass intArrCls = e->functions->GetObjectClass(e, (jarray) ia);
    jclass objArrCls = (jclass) e->functions->FindClass(e, "[Ljava/lang/Object;");
    CHECK(e->functions->IsAssignableFrom(e, intArrCls, intArrCls) == JNI_TRUE,
          "IsAssignableFrom: [I -> [I");
    CHECK(e->functions->IsAssignableFrom(e, objArrCls, intArrCls) == JNI_FALSE,
          "IsAssignableFrom: [I -/-> [Ljava/lang/Object;（基本类型分量）");
  }
  {
    // 真实容量核算：填充至 kMaxLocalRefs 余量 10，Ensure(50) 失败 Ensure(5) 成功
    jobject keep = e->functions->AllocObject(e, objectCls);
    size_t before = env.locals.remaining();
    CHECK(e->functions->PushLocalFrame(e, 8) == 0, "PushLocalFrame");
    size_t room = env.locals.remaining();
    CHECK(room > 60, "capacity room available");
    for (size_t i = 0; i < room - 10; i++) {
      if (e->functions->NewLocalRef(e, keep) == nullptr) break;
    }
    CHECK(e->functions->EnsureLocalCapacity(e, 5) == 0, "EnsureLocalCapacity(5) ok");
    CHECK(e->functions->EnsureLocalCapacity(e, 50) != 0, "EnsureLocalCapacity(50) -> JNI_ERR");
    CHECK(e->functions->PushLocalFrame(e, 4096) != 0, "PushLocalFrame over capacity -> JNI_ERR");
    e->functions->PopLocalFrame(e, nullptr);
    CHECK(env.locals.remaining() == before, "capacity restored after PopLocalFrame");
  }

  // =================================================================
  // 9. System.loadLibrary 链路（dlopen 真实 .so）
  // =================================================================
  std::printf("---- loadLibrary / JNI_OnLoad ----\n");
  if (so_path[0] == 0) {
    std::printf("FAIL: no .so path argument\n");
    g_failures++;
  } else {
    // 测试侧持有 dlopen 引用：VM 的 dlclose 不会 unmap，全局读安全
    void* th = dlopen(so_path, RTLD_NOW);
    CHECK(th != nullptr, "test-side dlopen holds mapping");
    VObject* loader1 = static_cast<VObject*>(
        dexvm::jni::DecodeJObject(&env, e->functions->AllocObject(e, objectCls)));
    VObject* loader2 = static_cast<VObject*>(
        dexvm::jni::DecodeJObject(&env, e->functions->AllocObject(e, objectCls)));
    std::string err;
    bool ok = vm.natives.LoadNativeLibrary(&env, so_path, loader1, nullptr, &err);
    CHECK(ok, "LoadNativeLibrary fixture .so");
    if (!ok) std::printf("  error: %s\n", err.c_str());
    CHECK(env.class_loader == nullptr, "class loader override restored after load");

    void* handle = vm.natives.Find(so_path) != nullptr ? vm.natives.Find(so_path)->handle : nullptr;
    CHECK(handle != nullptr, "SharedLibrary handle registered");
    if (handle != nullptr) {
      int* calls = static_cast<int*>(dlsym(th, "g_onload_calls"));
      void** observed = static_cast<void**>(dlsym(th, "g_onload_loader"));
      int* fc = static_cast<int*>(dlsym(th, "g_findclass_ok"));
      int* reg = static_cast<int*>(dlsym(th, "g_register_ok"));
      CHECK(calls != nullptr && *calls == 1, "JNI_OnLoad ran once");
      CHECK(observed != nullptr && *observed == loader1,
            "OnLoad 内 ClassLoader override == nativeLoad loader");
      CHECK(fc != nullptr && *fc == 1, "OnLoad 内 FindClass 走 override loader 成功");
      CHECK(reg != nullptr && *reg == 1, "OnLoad 内 RegisterNatives 成功");
    }
    // .so 注册的 native 经解释/JNI 调用（loadLibrary -> OnLoad -> RegisterNatives -> call）
    jclass hostCls2 = e->functions->FindClass(e, "fixture.NativeHost");
    jmethodID soAdd = e->functions->GetStaticMethodID(e, hostCls2, "soAdd", "(II)I");
    CHECK(soAdd != nullptr && e->functions->CallStaticIntMethod(e, hostCls2, soAdd, 20, 22) == 42,
          "dlopen .so registered soAdd(20,22)==42");

    // 幂等：同 path/allocator 二次加载
    err.clear();
    CHECK(vm.natives.LoadNativeLibrary(&env, so_path, loader1, nullptr, &err) == true,
          "second load same loader idempotent");
    {
      int* calls = static_cast<int*>(dlsym(th, "g_onload_calls"));
      CHECK(calls != nullptr && *calls == 1, "idempotent load does not re-run JNI_OnLoad");
    }
    // 冲突：不同 loader 拒绝（java_vm_ext.cc:999-1048）
    err.clear();
    CHECK(vm.natives.LoadNativeLibrary(&env, so_path, loader2, nullptr, &err) == false,
          "conflicting loader rejected");
    CHECK(err.find("already opened") != std::string::npos, "conflict error message");

    // 无 JNI_OnLoad 的系统库：直接成功
    err.clear();
    CHECK(vm.natives.LoadNativeLibrary(&env, "/usr/lib/libSystem.B.dylib", loader1, nullptr, &err) ==
              true,
          "system library without JNI_OnLoad loads");

    // 卸载：JNI_OnUnload + dlclose
    err.clear();
    int* unloads = static_cast<int*>(dlsym(th, "g_onunload_calls"));
    CHECK(vm.natives.UnloadNativeLibrary(&env, so_path, &err) == true, "UnloadNativeLibrary");
    CHECK(unloads != nullptr && *unloads == 1, "JNI_OnUnload ran");
    CHECK(vm.natives.Find(so_path) == nullptr, "SharedLibrary entry removed");

    // 版本校验：Bad JNI version
    int* ver = static_cast<int*>(dlsym(th, "g_onload_version"));
    if (ver != nullptr) *ver = 0x00000123;
    err.clear();
    CHECK(vm.natives.LoadNativeLibrary(&env, so_path, loader1, nullptr, &err) == false,
          "bad JNI_OnLoad version rejected");
    CHECK(err.find("Bad JNI version") != std::string::npos, "bad version error message");
    // 失败缓存：再次加载报 previous attempt（java_vm_ext.cc CheckOnLoadResult）
    err.clear();
    CHECK(vm.natives.LoadNativeLibrary(&env, so_path, loader1, nullptr, &err) == false,
          "failed OnLoad cached");
    CHECK(err.find("previous attempt") != std::string::npos, "failed OnLoad error message");
    // 清理 + 恢复版本 -> 可重新加载
    vm.natives.UnloadNativeLibrary(&env, so_path, &err);
    if (ver != nullptr) *ver = 0x00010006;
    err.clear();
    CHECK(vm.natives.LoadNativeLibrary(&env, so_path, loader1, nullptr, &err) == true,
          "reload after cleanup");
    {
      int* calls2 = static_cast<int*>(dlsym(th, "g_onload_calls"));
      CHECK(calls2 != nullptr && *calls2 == 3, "reloaded OnLoad ran again");
    }
    vm.natives.UnloadNativeLibrary(&env, so_path, &err);
    // 未知路径卸载
    CHECK(vm.natives.UnloadNativeLibrary(&env, "/no/such/lib.so", &err) == false,
          "unload unknown path fails");
    if (th != nullptr) dlclose(th);
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
