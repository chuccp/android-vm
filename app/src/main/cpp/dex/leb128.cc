#include "leb128.h"

namespace dexvm::dex {

bool ReadUleb128(const uint8_t** p, const uint8_t* end, uint32_t* out) {
  const uint8_t* ptr = *p;
  uint32_t result = 0;
  for (size_t i = 0; i < 5; ++i) {
    if (ptr >= end) {
      return false;
    }
    uint8_t byte = *ptr++;
    result |= static_cast<uint32_t>(byte & 0x7f) << (i * 7);
    if ((byte & 0x80) == 0) {
      *p = ptr;
      *out = result;
      return true;
    }
  }
  // 5 字节后仍有续位：非法/溢出。
  return false;
}

bool ReadSleb128(const uint8_t** p, const uint8_t* end, int32_t* out) {
  const uint8_t* ptr = *p;
  int32_t result = 0;
  for (size_t i = 0; i < 5; ++i) {
    if (ptr >= end) {
      return false;
    }
    uint8_t byte = *ptr++;
    result |= static_cast<int32_t>(byte & 0x7f) << (i * 7);
    if ((byte & 0x80) == 0) {
      // 同 ART：终止于第 i 字节（i < 4）时按已用位数做符号扩展。
      if (i < 4) {
        int shift = 32 - static_cast<int>(i + 1) * 7;
        result = (result << shift) >> shift;
      }
      *p = ptr;
      *out = result;
      return true;
    }
  }
  // 5 字节后仍有续位：非法/溢出。
  return false;
}

uint8_t* WriteUleb128(uint8_t* dst, uint32_t value) {
  uint8_t out = value & 0x7f;
  value >>= 7;
  while (value != 0) {
    *dst++ = out | 0x80;
    out = value & 0x7f;
    value >>= 7;
  }
  *dst++ = out;
  return dst;
}

uint8_t* WriteSleb128(uint8_t* dst, int32_t value) {
  // 同 ART EncodeSignedLeb128（32 位变体）。
  uint32_t extra_bits = static_cast<uint32_t>(value ^ (value >> 31)) >> 6;
  uint8_t out = value & 0x7f;
  while (extra_bits != 0u) {
    *dst++ = out | 0x80;
    value >>= 7;
    out = value & 0x7f;
    extra_bits >>= 7;
  }
  *dst++ = out;
  return dst;
}

size_t Uleb128Size(uint32_t value) {
  size_t size = 1;
  while (value >>= 7) {
    ++size;
  }
  return size;
}

size_t Sleb128Size(int32_t value) {
  size_t size = 1;
  int32_t remaining = value >> 7;  // 算术移位，保留符号
  bool done = (remaining == 0) && ((value & 0x40) == 0);
  done = done || (remaining == -1 && (value & 0x40) != 0);
  while (!done) {
    ++size;
    value = remaining;
    remaining = value >> 7;
    done = (remaining == 0) && ((value & 0x40) == 0);
    done = done || (remaining == -1 && (value & 0x40) != 0);
  }
  return size;
}

}  // namespace dexvm::dex
