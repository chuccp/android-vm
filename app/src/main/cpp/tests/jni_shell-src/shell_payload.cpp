// JNI 壳语义测试 .so 的跨库依赖面（dladdr 完整性观测）。
// 技法参考启发自 v1 target-apk/src/main/cpp/hardened_payload.cpp（构造 cookie +
// 确定性混合）；命名/结构按 v2 tests 口径重写。
// 编译：tools/jni_shell_tests.sh（-shared -fPIC；libshell_fixture.so 链接本库）。
#include <cstdint>

namespace {

constexpr uint32_t kPayloadSeed = 0x517a91c3U;
volatile uint32_t g_payload_cookie = 0;
int g_payload_init_calls = 0;

extern "C" void ShellPayloadCtor() __attribute__((constructor));
extern "C" void ShellPayloadCtor() {
  g_payload_cookie = kPayloadSeed ^ 0xa17fU;
  g_payload_init_calls += 1;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int shell_mix_7f(int value) {
  const uint32_t mixed = static_cast<uint32_t>(value) * 17U;
  return static_cast<int>((mixed ^ 0x33U) + 0x44U);
}

extern "C" __attribute__((visibility("default"))) int shell_alive_7f() {
  return g_payload_cookie == (kPayloadSeed ^ 0xa17fU) ? 1 : 0;
}

extern "C" __attribute__((visibility("default"))) int shell_payload_init_calls() {
  return g_payload_init_calls;
}
