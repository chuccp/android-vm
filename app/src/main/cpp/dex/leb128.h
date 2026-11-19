// ULEB128/SLEB128 编解码（语义对照 third_party/art/libartbase/base/leb128.h）。
#pragma once

#include <cstddef>
#include <cstdint>

namespace dexvm::dex {

// 从 [p, end) 读取一个 ULEB128（最多 5 字节编码 32 位值）。
// 成功时 *p 前进越过该值并返回 true；输入截断或 5 字节仍带续位时返回 false。
bool ReadUleb128(const uint8_t** p, const uint8_t* end, uint32_t* out);

// 从 [p, end) 读取一个 SLEB128（32 位有符号，编译语义同 ART DecodeSignedLeb128）。
// 成功时 *p 前进越过该值并返回 true；输入截断或 5 字节仍带续位时返回 false。
bool ReadSleb128(const uint8_t** p, const uint8_t* end, int32_t* out);

// 编码（规范最短形式）。dst 需保证至少 Uleb128Size/Sleb128Size 字节。
// 返回写入结束位置。
uint8_t* WriteUleb128(uint8_t* dst, uint32_t value);
uint8_t* WriteSleb128(uint8_t* dst, int32_t value);

size_t Uleb128Size(uint32_t value);
size_t Sleb128Size(int32_t value);

}  // namespace dexvm::dex
