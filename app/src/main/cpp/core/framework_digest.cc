// java.security.VmDigest：lib sun.security.provider.{MD5,SHA1,SHA256} 的原生摘要桥。
//
// 启动实测（DexVmProfile）：MainActivity.onCreate 约 49s，其中
// sun.security.provider.MD5.processBlock 占 ~30s——加固壳的 JarVerifier 在
// 启动期对整个 APK 逐 entry 做摘要校验，纯 Java 实现走 switch 解释器逐字节
// 处理（171MB 级输入），成为首帧前最大热点。
//
// AOSP 语义下 MessageDigest 就是 native（Conscrypt/BoringSSL）承接；这里同口径：
// 摘要算法（RFC 1321 / FIPS 180-1 / FIPS 180-4）在 libdexvm 内用 C++ 实现，
// lib Java 侧只保留 SPI 形状，engineUpdate/engineDigest 直调本桥。
#include "core/builtins_internal.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace dexvm::runtime {

namespace {

enum class DigestAlg : uint8_t { kMd5, kSha1, kSha256 };

struct DigestCtx {
  DigestAlg alg = DigestAlg::kMd5;
  uint32_t h[8] = {};
  uint64_t total = 0;  // 已 update 的字节数（padding 不计）
  uint8_t buf[64] = {};
  size_t buf_len = 0;
};

inline uint32_t Rotl32(uint32_t x, uint32_t s) {
  return (x << s) | (x >> (32 - s));
}

inline uint32_t Rotr32(uint32_t x, uint32_t s) {
  return (x >> s) | (x << (32 - s));
}

inline uint32_t LoadLe32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

inline uint32_t LoadBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

inline void StoreLe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

inline void StoreBe32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v >> 24);
  p[1] = static_cast<uint8_t>(v >> 16);
  p[2] = static_cast<uint8_t>(v >> 8);
  p[3] = static_cast<uint8_t>(v);
}

// ---- MD5（RFC 1321）----
const uint32_t kMd5K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613,
    0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193,
    0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d,
    0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122,
    0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
    0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244,
    0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb,
    0xeb86d391};
const uint32_t kMd5S[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9, 14, 20, 5, 9,
    14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

void Md5Block(DigestCtx* c, const uint8_t* p) {
  uint32_t m[16];
  for (int i = 0; i < 16; i++) m[i] = LoadLe32(p + i * 4);
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  for (int i = 0; i < 64; i++) {
    uint32_t f;
    int g;
    if (i < 16) {
      f = (b & cc) | (~b & d);
      g = i;
    } else if (i < 32) {
      f = (d & b) | (~d & cc);
      g = (5 * i + 1) & 15;
    } else if (i < 48) {
      f = b ^ cc ^ d;
      g = (3 * i + 5) & 15;
    } else {
      f = cc ^ (b | ~d);
      g = (7 * i) & 15;
    }
    uint32_t tmp = d;
    d = cc;
    cc = b;
    b = b + Rotl32(a + f + kMd5K[i] + m[g], kMd5S[i]);
    a = tmp;
  }
  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += cc;
  c->h[3] += d;
}

// ---- SHA-1（FIPS 180-1）----
void Sha1Block(DigestCtx* c, const uint8_t* p) {
  uint32_t w[80];
  for (int i = 0; i < 16; i++) w[i] = LoadBe32(p + i * 4);
  for (int i = 16; i < 80; i++) w[i] = Rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3], e = c->h[4];
  for (int i = 0; i < 80; i++) {
    uint32_t f, k;
    if (i < 20) {
      f = (b & cc) | (~b & d);
      k = 0x5a827999;
    } else if (i < 40) {
      f = b ^ cc ^ d;
      k = 0x6ed9eba1;
    } else if (i < 60) {
      f = (b & cc) | (b & d) | (cc & d);
      k = 0x8f1bbcdc;
    } else {
      f = b ^ cc ^ d;
      k = 0xca62c1d6;
    }
    uint32_t t = Rotl32(a, 5) + f + e + k + w[i];
    e = d;
    d = cc;
    cc = Rotl32(b, 30);
    b = a;
    a = t;
  }
  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += cc;
  c->h[3] += d;
  c->h[4] += e;
}

// ---- SHA-256（FIPS 180-4）----
const uint32_t kSha256K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2};

void Sha256Block(DigestCtx* c, const uint8_t* p) {
  uint32_t w[64];
  for (int i = 0; i < 16; i++) w[i] = LoadBe32(p + i * 4);
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = Rotr32(w[i - 15], 7) ^ Rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = Rotr32(w[i - 2], 17) ^ Rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = c->h[0], b = c->h[1], cc = c->h[2], d = c->h[3];
  uint32_t e = c->h[4], f = c->h[5], g = c->h[6], hh = c->h[7];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = Rotr32(e, 6) ^ Rotr32(e, 11) ^ Rotr32(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = hh + s1 + ch + kSha256K[i] + w[i];
    uint32_t s0 = Rotr32(a, 2) ^ Rotr32(a, 13) ^ Rotr32(a, 22);
    uint32_t maj = (a & b) ^ (a & cc) ^ (b & cc);
    uint32_t t2 = s0 + maj;
    hh = g;
    g = f;
    f = e;
    e = d + t1;
    d = cc;
    cc = b;
    b = a;
    a = t1 + t2;
  }
  c->h[0] += a;
  c->h[1] += b;
  c->h[2] += cc;
  c->h[3] += d;
  c->h[4] += e;
  c->h[5] += f;
  c->h[6] += g;
  c->h[7] += hh;
}

void DigestInit(DigestCtx* c, DigestAlg alg) {
  std::memset(c, 0, sizeof(*c));
  c->alg = alg;
  switch (alg) {
    case DigestAlg::kMd5:
      c->h[0] = 0x67452301;
      c->h[1] = 0xefcdab89;
      c->h[2] = 0x98badcfe;
      c->h[3] = 0x10325476;
      break;
    case DigestAlg::kSha1:
      c->h[0] = 0x67452301;
      c->h[1] = 0xefcdab89;
      c->h[2] = 0x98badcfe;
      c->h[3] = 0x10325476;
      c->h[4] = 0xc3d2e1f0;
      break;
    case DigestAlg::kSha256:
      c->h[0] = 0x6a09e667;
      c->h[1] = 0xbb67ae85;
      c->h[2] = 0x3c6ef372;
      c->h[3] = 0xa54ff53a;
      c->h[4] = 0x510e527f;
      c->h[5] = 0x9b05688c;
      c->h[6] = 0x1f83d9ab;
      c->h[7] = 0x5be0cd19;
      break;
  }
}

void DigestBlock(DigestCtx* c, const uint8_t* p) {
  switch (c->alg) {
    case DigestAlg::kMd5:
      Md5Block(c, p);
      break;
    case DigestAlg::kSha1:
      Sha1Block(c, p);
      break;
    case DigestAlg::kSha256:
      Sha256Block(c, p);
      break;
  }
}

size_t DigestLength(DigestAlg alg) {
  switch (alg) {
    case DigestAlg::kMd5:
      return 16;
    case DigestAlg::kSha1:
      return 20;
    case DigestAlg::kSha256:
      return 32;
  }
  return 0;
}

// byte[] 在 VM 里是 8 字节/元素槽（低字节有效，同 StringFactory/VmAssets 口径）。
inline const uint8_t* ByteArrayData(const VArray* arr) { return arr->data(); }

void ByteArrayToBuffer(const VArray* arr, int32_t offset, int32_t len, uint8_t* dst) {
  const uint8_t* src = ByteArrayData(arr) + static_cast<size_t>(offset) * 8;
  for (int32_t i = 0; i < len; i++) dst[i] = src[static_cast<size_t>(i) * 8];
}

void BufferToByteArray(uint8_t* dst_slots, const uint8_t* src, size_t len) {
  for (size_t i = 0; i < len; i++) dst_slots[i * 8] = src[i];
}

std::mutex g_digest_mu;
std::unordered_map<uint64_t, DigestCtx> g_digest_ctx;
std::atomic<uint64_t> g_digest_next{1};

}  // namespace

// VmDigest.create(String algorithm) -> long handle（不识别返回 0）。
Value VmDigestCreate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1 || args[0].ref == nullptr) return r;
  std::string alg = Mutf8Arg(args[0]);
  DigestAlg a;
  if (alg == "MD5" || alg == "md5") {
    a = DigestAlg::kMd5;
  } else if (alg == "SHA-1" || alg == "SHA1" || alg == "sha-1" || alg == "sha1" ||
             alg == "SHA") {
    a = DigestAlg::kSha1;
  } else if (alg == "SHA-256" || alg == "SHA256" || alg == "sha-256" || alg == "sha256") {
    a = DigestAlg::kSha256;
  } else {
    return r;
  }
  DigestCtx ctx;
  DigestInit(&ctx, a);
  uint64_t handle = g_digest_next.fetch_add(1, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lk(g_digest_mu);
    g_digest_ctx.emplace(handle, ctx);
  }
  r.l = static_cast<int64_t>(handle);
  (void)state;
  return r;
}

// VmDigest.update(long handle, byte[] input, int offset, int len)
Value VmDigestUpdate(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 4) return r;
  uint64_t handle = static_cast<uint64_t>(args[0].l);
  VArray* arr = reinterpret_cast<VArray*>(args[1].ref);
  int32_t offset = args[2].i;
  int32_t len = args[3].i;
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "input == null");
    *ok = false;
    return r;
  }
  if (offset < 0 || len < 0 || offset > arr->length || len > arr->length - offset) {
    state.ThrowNew("Ljava/lang/ArrayIndexOutOfBoundsException;", "digest update range");
    *ok = false;
    return r;
  }
  std::lock_guard<std::mutex> lk(g_digest_mu);
  auto it = g_digest_ctx.find(handle);
  if (it == g_digest_ctx.end()) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "digest handle closed");
    *ok = false;
    return r;
  }
  DigestCtx* c = &it->second;
  c->total += static_cast<uint64_t>(len);
  const int32_t end = offset + len;
  int32_t pos = offset;
  if (c->buf_len != 0) {
    size_t take = 64 - c->buf_len;
    if (take > static_cast<size_t>(len)) take = static_cast<size_t>(len);
    ByteArrayToBuffer(arr, pos, static_cast<int32_t>(take), c->buf + c->buf_len);
    c->buf_len += take;
    pos += static_cast<int32_t>(take);
    if (c->buf_len == 64) {
      DigestBlock(c, c->buf);
      c->buf_len = 0;
    }
  }
  while (end - pos >= 64) {
    ByteArrayToBuffer(arr, pos, 64, c->buf);
    DigestBlock(c, c->buf);
    pos += 64;
  }
  if (pos < end) {
    int32_t rem = end - pos;
    ByteArrayToBuffer(arr, pos, rem, c->buf);
    c->buf_len = static_cast<size_t>(rem);
  }
  return r;
}

// VmDigest.digest(long handle) -> byte[]（final + reset，JDK engineDigest 语义）
Value VmDigestDigest(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  uint64_t handle = static_cast<uint64_t>(args[0].l);
  DigestCtx ctx;
  {
    std::lock_guard<std::mutex> lk(g_digest_mu);
    auto it = g_digest_ctx.find(handle);
    if (it == g_digest_ctx.end()) return r;  // 已 close：空返回（不应发生）
    ctx = it->second;
    // JDK engineDigest 语义：完成即复位，实例可继续 update/digest。
    DigestInit(&it->second, it->second.alg);
  }
  uint64_t bit_len = ctx.total * 8;
  ctx.buf[ctx.buf_len++] = 0x80;
  if (ctx.buf_len > 56) {
    while (ctx.buf_len < 64) ctx.buf[ctx.buf_len++] = 0;
    DigestBlock(&ctx, ctx.buf);
    ctx.buf_len = 0;
  }
  while (ctx.buf_len < 56) ctx.buf[ctx.buf_len++] = 0;
  if (ctx.alg == DigestAlg::kMd5) {
    for (int i = 0; i < 8; i++) ctx.buf[56 + i] = static_cast<uint8_t>(bit_len >> (8 * i));
  } else {
    for (int i = 0; i < 8; i++) ctx.buf[56 + i] = static_cast<uint8_t>(bit_len >> (8 * (7 - i)));
  }
  DigestBlock(&ctx, ctx.buf);

  size_t out_len = DigestLength(ctx.alg);
  uint8_t out[32];
  if (ctx.alg == DigestAlg::kMd5) {
    for (int i = 0; i < 4; i++) StoreLe32(out + i * 4, ctx.h[i]);
  } else if (ctx.alg == DigestAlg::kSha1) {
    for (int i = 0; i < 5; i++) StoreBe32(out + i * 4, ctx.h[i]);
  } else {
    for (int i = 0; i < 8; i++) StoreBe32(out + i * 4, ctx.h[i]);
  }

  VClass* byte_arr = state.linker->FindClass("[B");
  if (byte_arr == nullptr) return r;
  VArray* arr = state.linker->AllocArray(byte_arr, static_cast<int32_t>(out_len));
  if (arr == nullptr) {
    state.ThrowNew("Ljava/lang/OutOfMemoryError;", "VmDigest.digest");
    *ok = false;
    return r;
  }
  BufferToByteArray(arr->data(), out, out_len);
  r.ref = arr;
  return r;
}

// VmDigest.reset(long handle)：复位并保留 handle（engineReset 语义）
Value VmDigestReset(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  uint64_t handle = static_cast<uint64_t>(args[0].l);
  std::lock_guard<std::mutex> lk(g_digest_mu);
  auto it = g_digest_ctx.find(handle);
  if (it != g_digest_ctx.end()) DigestInit(&it->second, it->second.alg);
  (void)state;
  return r;
}

// VmDigest.close(long handle)：释放上下文（lib SPI 无 finalizer，显式关闭可选）。
Value VmDigestClose(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)state;
  *ok = true;
  Value r{};
  if (n < 1) return r;
  uint64_t handle = static_cast<uint64_t>(args[0].l);
  std::lock_guard<std::mutex> lk(g_digest_mu);
  g_digest_ctx.erase(handle);
  return r;
}

}  // namespace dexvm::runtime
