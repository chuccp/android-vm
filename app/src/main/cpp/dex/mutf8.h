// MUTF-8（DEX 修饰 UTF-8）解码为 UTF-8 std::string。
// 语义：内嵌 U+0000 以 C0 80 编码，解码后保留为 0x00 字节；
// 补充平面字符以 UTF-16 代理对各 3 字节序列编码，解码时合并为 4 字节 UTF-8。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dexvm::dex {

// 解码 [data, data+max_len) 中的 MUTF-8 串（遇 NUL 终止符或 max_len 结束，
// 终止符不计入结果）。输出为 UTF-8 字节序列（可含内嵌 0x00）。
// 严格模式（lossy=false）：遇到非法/截断序列返回 false（*out 内容未定义）。
// lossy 模式：非法字节以 U+FFFD 替换，恒返回 true。
bool DecodeMutf8(const uint8_t* data, size_t max_len, std::string* out,
                 bool lossy = false);

}  // namespace dexvm::dex
