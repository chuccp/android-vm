#include "mutf8.h"

namespace dexvm::dex {
namespace {

// 追加一个码点的 UTF-8 编码。
void AppendUtf8(std::string* out, uint32_t cp) {
  if (cp < 0x80) {
    out->push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out->push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out->push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out->push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out->push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool Fail(std::string* out, size_t* i, bool lossy) {
  if (!lossy) {
    return false;
  }
  AppendUtf8(out, 0xFFFD);  // U+FFFD 替换字符
  ++*i;
  return true;
}

constexpr uint32_t kReplacementChar = 0xFFFD;

}  // namespace

bool DecodeMutf8(const uint8_t* data, size_t max_len, std::string* out,
                 bool lossy) {
  out->clear();
  size_t i = 0;
  while (i < max_len) {
    uint8_t b0 = data[i];
    if (b0 == 0) {
      break;  // NUL 终止符（内嵌 NUL 走 C0 80）
    }
    if (b0 < 0x80) {
      out->push_back(static_cast<char>(b0));
      ++i;
      continue;
    }
    if (b0 == 0xC0) {
      // C0 80 = U+0000；其余 C0 序列为非法（过度编码）。
      if (i + 1 < max_len && data[i + 1] == 0x80) {
        out->push_back('\0');
        i += 2;
        continue;
      }
      if (!Fail(out, &i, lossy)) return false;
      continue;
    }
    if (b0 < 0xC2) {
      // 0x80..0xBF（孤立续位）与 0xC1（过度编码）均非法。
      if (!Fail(out, &i, lossy)) return false;
      continue;
    }
    if (b0 < 0xE0) {
      // 2 字节序列：U+0080..U+07FF。
      if (i + 1 >= max_len) {
        if (!lossy) return false;
        AppendUtf8(out, kReplacementChar);
        i = max_len;  // 截断：吞掉剩余
        continue;
      }
      uint8_t b1 = data[i + 1];
      if ((b1 & 0xC0) != 0x80) {
        if (!Fail(out, &i, lossy)) return false;
        continue;
      }
      AppendUtf8(out, (static_cast<uint32_t>(b0 & 0x1F) << 6) |
                          (b1 & 0x3F));
      i += 2;
      continue;
    }
    if (b0 < 0xF0) {
      // 3 字节序列：U+0800..U+FFFF，含代理对成员。
      if (i + 2 >= max_len) {
        if (!lossy) return false;
        AppendUtf8(out, kReplacementChar);
        i = max_len;
        continue;
      }
      uint8_t b1 = data[i + 1];
      uint8_t b2 = data[i + 2];
      if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
        if (!Fail(out, &i, lossy)) return false;
        continue;
      }
      uint32_t cp = (static_cast<uint32_t>(b0 & 0x0F) << 12) |
                    (static_cast<uint32_t>(b1 & 0x3F) << 6) |
                    (b2 & 0x3F);
      if (cp >= 0xD800 && cp <= 0xDBFF) {
        // 高位代理：必须紧跟低位代理的 3 字节序列。
        if (i + 5 >= max_len) {
          if (!lossy) return false;
          AppendUtf8(out, kReplacementChar);
          i = max_len;
          continue;
        }
        uint8_t c0 = data[i + 3];
        uint8_t c1 = data[i + 4];
        uint8_t c2 = data[i + 5];
        if (c0 < 0xE0 || c0 >= 0xF0 || (c1 & 0xC0) != 0x80 ||
            (c2 & 0xC0) != 0x80) {
          if (!Fail(out, &i, lossy)) return false;
          continue;
        }
        uint32_t lo = (static_cast<uint32_t>(c0 & 0x0F) << 12) |
                      (static_cast<uint32_t>(c1 & 0x3F) << 6) |
                      (c2 & 0x3F);
        if (lo < 0xDC00 || lo > 0xDFFF) {
          if (!Fail(out, &i, lossy)) return false;
          continue;
        }
        uint32_t supp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        AppendUtf8(out, supp);
        i += 6;
        continue;
      }
      if (cp >= 0xDC00 && cp <= 0xDFFF) {
        // 孤立低位代理。
        if (!Fail(out, &i, lossy)) return false;
        continue;
      }
      AppendUtf8(out, cp);
      i += 3;
      continue;
    }
    // MUTF-8 无 4 字节序列（补充平面走代理对）。
    if (!Fail(out, &i, lossy)) return false;
  }
  return true;
}

}  // namespace dexvm::dex
