#include <jni.h>

#include <android/log.h>
#include <dlfcn.h>
#include <unistd.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

extern "C" int hp_mix_7f(int value);
extern "C" int hp_alive_7f();

namespace {

constexpr uint32_t kCookieSeed = 0x6d763263U;
constexpr uint32_t kCookieMask = 0xa5c33c5aU;
constexpr uint32_t kResultBias = 4919U;
constexpr uint8_t kEncryptedText[] = {
    0x2b, 0x79, 0x81, 0x2d, 0x9e, 0x88, 0x32,
    0x56, 0xdf, 0x95, 0x74, 0x1c, 0x00, 0x99,
};

volatile uint32_t g_fixture_cookie = 0;
volatile uint32_t g_fixture_calls = 0;

extern "C" void FixtureInitialize() __attribute__((constructor));
extern "C" void FixtureInitialize() {
  g_fixture_cookie = kCookieSeed ^ kCookieMask;
}

bool CookieValid() {
  return g_fixture_cookie == (kCookieSeed ^ kCookieMask);
}

bool TraceDetected() {
  FILE* status = std::fopen("/proc/self/status", "r");
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

uint32_t RuntimeKey() {
  return static_cast<uint32_t>(getpid()) ^
      (TraceDetected() ? 1U : 0U);
}

std::array<char, sizeof(kEncryptedText) + 1> DecryptFixtureText() {
  std::array<char, sizeof(kEncryptedText) + 1> output{};
  uint8_t rolling = 0x5d;
  for (size_t index = 0; index < sizeof(kEncryptedText); ++index) {
    output[index] = static_cast<char>(
        kEncryptedText[index] ^ rolling);
    rolling = static_cast<uint8_t>(
        rolling * 33U + 0x17U + index);
  }
  return output;
}

}  // namespace

static jbyteArray JNICALL FixtureArray(
    JNIEnv* env, jclass, jbyteArray input) {
  if (input == nullptr) {
    env->ThrowNew(
        env->FindClass("java/lang/NullPointerException"),
        "hardened byte array is null");
    return 0;
  }
  const jsize length = env->GetArrayLength(input);
  std::vector<jbyte> bytes(static_cast<size_t>(length));
  env->GetByteArrayRegion(input, 0, length, bytes.data());
  uint8_t rolling = 0x5d;
  for (jsize index = 0; index < length; ++index) {
    bytes[static_cast<size_t>(index)] ^= static_cast<jbyte>(rolling);
    rolling = static_cast<uint8_t>(
        rolling * 33U + 0x17U + index);
  }
  jbyteArray output = env->NewByteArray(length);
  if (output != nullptr && length > 0) {
    env->SetByteArrayRegion(
        output, 0, length, bytes.data());
  }
  return output;
}

static jint JNICALL FixtureCallback(JNIEnv* env, jclass fixture) {
  const jmethodID callback = env->GetStaticMethodID(
      fixture, "onNativeCallback", "(I)V");
  if (callback == nullptr) return -1;
  env->CallStaticVoidMethod(fixture, callback, 0x4f21);
  return env->ExceptionCheck() ? -2 : 0x4f21;
}

static void JNICALL FixtureThrow(JNIEnv* env, jclass) {
  env->ThrowNew(
      env->FindClass("java/lang/IllegalArgumentException"),
      "hardened-native-failure");
}

static jint JNICALL FixtureIntegrity(JNIEnv*, jclass) {
  if (!CookieValid() || hp_alive_7f() != 1) return -1;
  Dl_info info{};
  if (dladdr(
          reinterpret_cast<void*>(&hp_mix_7f),
          &info) == 0 ||
      info.dli_fname == nullptr) {
    return -2;
  }
  return hp_mix_7f(11);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(
    JavaVM* vm, void* reserved) {
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
  if (TraceDetected()) {
    __android_log_print(
        ANDROID_LOG_WARN, "HardenedFixture",
        "debugger-like tracer detected; compatibility path enabled");
  }
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_coke_vm_HardenedNativeFixture_a(
    JNIEnv*, jclass, jint value) {
  if (!CookieValid()) return -1;
  g_fixture_calls = g_fixture_calls + 1U;
  const uint32_t mixed =
      static_cast<uint32_t>(value) ^ 0x5a5aU;
  return static_cast<jint>(
      mixed * 31U + kResultBias +
      static_cast<uint32_t>(hp_mix_7f(value)));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_coke_vm_HardenedNativeFixture_b(
    JNIEnv* env, jclass) {
  if (!CookieValid()) return nullptr;
  g_fixture_calls = g_fixture_calls + 1U;
  const auto decrypted = DecryptFixtureText();
  return env->NewStringUTF(decrypted.data());
}

extern "C" JNIEXPORT jint JNICALL
Java_com_coke_vm_HardenedNativeFixture_c(
    JNIEnv*, jclass) {
  if (!CookieValid()) return -1;
  Dl_info info{};
  const bool resolved =
      dladdr(
          reinterpret_cast<void*>(
              &Java_com_coke_vm_HardenedNativeFixture_a),
          &info) != 0 &&
      info.dli_fname != nullptr;
  return resolved
      ? static_cast<jint>(g_fixture_calls ^ RuntimeKey())
      : -2;
}

const JNINativeMethod kFixtureMethods[] = {
    {const_cast<char*>("a"), const_cast<char*>("(I)I"),
     reinterpret_cast<void*>(&Java_com_coke_vm_HardenedNativeFixture_a)},
    {const_cast<char*>("b"), const_cast<char*>("()Ljava/lang/String;"),
     reinterpret_cast<void*>(&Java_com_coke_vm_HardenedNativeFixture_b)},
    {const_cast<char*>("c"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&Java_com_coke_vm_HardenedNativeFixture_c)},
    {const_cast<char*>("d"), const_cast<char*>("([B)[B"),
     reinterpret_cast<void*>(&FixtureArray)},
    {const_cast<char*>("e"), const_cast<char*>("()V"),
     reinterpret_cast<void*>(&FixtureThrow)},
    {const_cast<char*>("f"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&FixtureIntegrity)},
    {const_cast<char*>("g"), const_cast<char*>("()I"),
     reinterpret_cast<void*>(&FixtureCallback)},
};

extern "C" JNIEXPORT void JNICALL
Java_com_coke_vm_HardenedNativeFixture_register(
    JNIEnv* env, jclass) {
  jclass fixture = env->FindClass(
      "com/coke/vm/HardenedNativeFixture");
  if (fixture == nullptr) return;
  env->RegisterNatives(
      fixture, kFixtureMethods,
      static_cast<jint>(
          sizeof(kFixtureMethods) / sizeof(kFixtureMethods[0])));
  env->DeleteLocalRef(fixture);
}
