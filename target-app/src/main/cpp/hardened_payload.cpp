#include <cstdint>

namespace {

constexpr uint32_t kPayloadCookieSeed = 0x517a91c3U;
volatile uint32_t g_payload_cookie = 0;

extern "C" void PayloadInitialize() __attribute__((constructor));
extern "C" void PayloadInitialize() {
  g_payload_cookie = kPayloadCookieSeed ^ 0xa17fU;
}

}  // namespace

extern "C" __attribute__((visibility("default"))) int hp_mix_7f(
    int value) {
  const uint32_t mixed =
      static_cast<uint32_t>(value) * 17U;
  return static_cast<int>((mixed ^ 0x33U) + 0x44U);
}

extern "C" __attribute__((visibility("default"))) int hp_alive_7f() {
  return g_payload_cookie == (kPayloadCookieSeed ^ 0xa17fU) ? 1 : 0;
}
