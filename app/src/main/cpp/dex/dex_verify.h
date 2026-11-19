// DEX 最小结构校验（内部接口）。
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace dexvm::dex {

// 校验 [data, data+size) 的 DEX 结构：magic/endian/header_size、
// 各表偏移+大小越界、map_list 基本一致性、id/class_def 引用 idx 越界。
// 失败返回 false 并写 error（成功时 *error 不动）。
bool VerifyDexFile(const uint8_t* data, size_t size, std::string* error);

}  // namespace dexvm::dex
