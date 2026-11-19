/* M2 loadLibrary/JNI_OnLoad 测试 .so（真实 <jni.h> C ABI）。
 * 编译：tools/jni_full_tests.sh（-shared -fPIC；未定义符号由主程序 -export_dynamic 提供）。
 * 验证面：
 *   - JNI_OnLoad：GetEnv（JavaVM 表 ABI）、FindClass（ClassLoader override 内查找）、
 *     RegisterNatives（JNINativeMethod 表 ABI）-> 全链 loadLibrary -> OnLoad -> 注册 -> 调用
 *   - JNI_OnUnload 卸载路径
 *   - 版本校验（g_onload_version 可由测试改写触发 Bad JNI version）
 */
#include <jni.h>

/* 主测试程序导出的探针（读 env 的 ClassLoader override，验证 OnLoad 期间 override 生效） */
extern void* dexvm_test_loader_probe(void* env);

void* g_onload_loader = 0;      /* OnLoad 期间观察到的 class loader override */
int g_onload_calls = 0;
int g_onload_version = 0x00010006;  /* JNI_VERSION_1_6；测试可改写 */
int g_findclass_ok = 0;
int g_register_ok = 0;
int g_onunload_calls = 0;

static jint JNICALL so_add(JNIEnv* env, jclass cls, jint a, jint b) {
  (void) env;
  (void) cls;
  return a + b;
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env = 0;
  (void) reserved;
  if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }
  g_onload_calls++;
  g_onload_loader = dexvm_test_loader_probe(env);

  jclass cls = (*env)->FindClass(env, "fixture.NativeHost");
  if (cls != 0) {
    g_findclass_ok = 1;
    {
      static JNINativeMethod methods[1];
      methods[0].name = "soAdd";
      methods[0].signature = "(II)I";
      methods[0].fnPtr = (void*) so_add;
      if ((*env)->RegisterNatives(env, cls, methods, 1) == 0) {
        g_register_ok = 1;
      }
    }
    (*env)->DeleteLocalRef(env, cls);
  }
  return g_onload_version;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
  (void) vm;
  (void) reserved;
  g_onunload_calls++;
}
