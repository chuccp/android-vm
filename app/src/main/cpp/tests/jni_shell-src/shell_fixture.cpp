// "壳形态" JNI 语义测试 .so（真实 <jni.h> C ABI/表）。
// 对抗手法清单作语义素材（参考启发自 v1 target-apk/src/main/cpp/hardened_fixture.cpp
// 与 GuestNativeLibraryTest.kt 断言面）：构造 cookie、TracerPid 反调试探测、
// Rolling-XOR 变换、ThrowNew、RegisterNatives 绑定、CallStaticVoidMethod 回调、
// dladdr 跨库完整性。命名/结构按 v2 tests 口径重写；测试断言按 Android/JNI 语义。
// 编译：tools/jni_shell_tests.sh（-shared -fPIC，链接 libshell_payload.so）。
#include <jni.h>

#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" int shell_mix_7f(int value);
extern "C" int shell_alive_7f();

namespace {

constexpr uint32_t kCookieSeed = 0x6d763263U;
constexpr uint32_t kCookieMask = 0xa5c33c5aU;
constexpr int kResultBias = 4919;
constexpr uint8_t kEncryptedText[] = {
    0x2b, 0x79, 0x81, 0x36, 0x97, 0x9f, 0x3a, 0x5f, 0x9c, 0x9f, 0x7b,
};  // rolling-XOR("vm-shell-ok")

volatile uint32_t g_shell_cookie = 0;

extern "C" void ShellFixtureCtor() __attribute__((constructor));
extern "C" void ShellFixtureCtor() {
  g_shell_cookie = kCookieSeed ^ kCookieMask;
}

bool CookieValid() {
  return g_shell_cookie == (kCookieSeed ^ kCookieMask);
}

// TracerPid 反调试探测素材（/proc/self/status；非 Linux 平台缺文件 = 无跟踪器）
bool TracerDetected() {
  std::FILE* status = std::fopen("/proc/self/status", "r");
  if (status == nullptr) return false;
  std::array<char, 256> line{};
  bool traced = false;
  while (std::fgets(line.data(), static_cast<int>(line.size()), status)) {
    if (std::strncmp(line.data(), "TracerPid:", 10) == 0) {
      traced = std::strtol(line.data() + 10, nullptr, 10) != 0;
      break;
    }
  }
  std::fclose(status);
  return traced;
}

std::array<char, sizeof(kEncryptedText) + 1> DecryptText() {
  std::array<char, sizeof(kEncryptedText) + 1> output{};
  uint8_t rolling = 0x5d;
  for (size_t index = 0; index < sizeof(kEncryptedText); ++index) {
    output[index] = static_cast<char>(kEncryptedText[index] ^ rolling);
    rolling = static_cast<uint8_t>(rolling * 33U + 0x17U + index);
  }
  return output;
}

}  // namespace

static jint JNICALL ShellMixed(JNIEnv*, jclass, jint value) {
  if (!CookieValid()) return -1;
  const uint32_t mixed = static_cast<uint32_t>(value) ^ 0x5a5aU;
  return static_cast<jint>(mixed * 31U + kResultBias +
                           static_cast<uint32_t>(shell_mix_7f(value)));
}

static jstring JNICALL ShellDecrypted(JNIEnv* env, jclass) {
  if (!CookieValid()) return nullptr;
  const auto text = DecryptText();
  return env->NewStringUTF(text.data());
}

static jint JNICALL ShellSelfCheck(JNIEnv*, jclass) {
  if (!CookieValid() || shell_alive_7f() != 1) return 0;
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&shell_mix_7f), &info) == 0 ||
      info.dli_fname == nullptr) {
    return 0;
  }
  return 1;
}

static jbyteArray JNICALL ShellTransform(JNIEnv* env, jclass, jbyteArray input) {
  if (input == nullptr) {
    env->ThrowNew(env->FindClass("java/lang/NullPointerException"),
                  "shell-byte-array-null");
    return nullptr;
  }
  const jsize length = env->GetArrayLength(input);
  std::vector<jbyte> bytes(static_cast<size_t>(length));
  env->GetByteArrayRegion(input, 0, length, bytes.data());
  uint8_t rolling = 0x5d;
  for (jsize index = 0; index < length; ++index) {
    bytes[static_cast<size_t>(index)] ^= static_cast<jbyte>(rolling);
    rolling = static_cast<uint8_t>(rolling * 33U + 0x17U + index);
  }
  jbyteArray output = env->NewByteArray(length);
  if (output != nullptr && length > 0) {
    env->SetByteArrayRegion(output, 0, length, bytes.data());
  }
  return output;
}

static void JNICALL ShellFail(JNIEnv* env, jclass) {
  env->ThrowNew(env->FindClass("java/lang/IllegalArgumentException"),
                "shell-native-failure");
}

static jint JNICALL ShellIntegrity(JNIEnv*, jclass) {
  if (!CookieValid() || shell_alive_7f() != 1) return -1;
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&shell_mix_7f), &info) == 0 ||
      info.dli_fname == nullptr) {
    return -2;
  }
  return shell_mix_7f(11);
}

static jint JNICALL ShellCallback(JNIEnv* env, jclass fixture) {
  const jmethodID callback =
      env->GetStaticMethodID(fixture, "onNativeCallback", "(I)V");
  if (callback == nullptr) return -1;
  env->CallStaticVoidMethod(fixture, callback, 0x4f21);
  return env->ExceptionCheck() ? -2 : 0x4f21;
}

static jint JNICALL ShellTracerProbe(JNIEnv*, jclass) {
  return TracerDetected() ? 1 : 0;
}

const JNINativeMethod kShellMethods[] = {
    {const_cast<char*>("mixed"), const_cast<char*>("(I)I"),
     reinterpret_cast<void*>(&ShellMixed)},
    {const_cast<char*>("decrypted"), const_cast<char*>("()Ljava/lang/String;"),
     reinterpret_cast<void*>(&ShellDecrypted)},
    {const_cast<char*>("selfCheck"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&ShellSelfCheck)},
    {const_cast<char*>("transform"), const_cast<char*>("([B)[B"),
     reinterpret_cast<void*>(&ShellTransform)},
    {const_cast<char*>("fail"), const_cast<char*>("()V"),
     reinterpret_cast<void*>(&ShellFail)},
    {const_cast<char*>("integrity"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&ShellIntegrity)},
    {const_cast<char*>("callbackRoundtrip"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&ShellCallback)},
    {const_cast<char*>("tracerProbe"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&ShellTracerProbe)},
};

extern "C" JNIEXPORT void JNICALL Java_shelltest_ShellNative_register(
    JNIEnv* env, jclass) {
  jclass fixture = env->FindClass("shelltest/ShellNative");
  if (fixture == nullptr) return;
  env->RegisterNatives(fixture, kShellMethods,
                       static_cast<jint>(sizeof(kShellMethods) /
                                         sizeof(kShellMethods[0])));
  env->DeleteLocalRef(fixture);
}

// 宿主 dlsym 观测点（JNI_OnLoad 执行计数）
extern "C" __attribute__((visibility("default"))) int g_onload_calls = 0;

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
  if (!CookieValid() || reserved != nullptr || vm == nullptr) {
    return JNI_ERR;
  }
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK ||
      env == nullptr) {
    return JNI_ERR;
  }
  Dl_info info{};
  if (dladdr(reinterpret_cast<void*>(&JNI_OnLoad), &info) == 0 ||
      info.dli_fname == nullptr) {
    return JNI_ERR;
  }
  g_onload_calls += 1;
  return JNI_VERSION_1_6;
}
