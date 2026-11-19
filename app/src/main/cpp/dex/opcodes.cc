// opcode 宽度表 + InsnWidth。
// 表逐条复制自 third_party/art/libdexfile/dex/dex_instruction_list.h 的
// (opcode, format)，宽度按 dex_instruction-inl.h 的 SizeInCodeUnits(Format)：
//   10x/12x/11n/11x/10t -> 1；20t..22c -> 2；32x..3rc -> 3；45cc/4rcc -> 4；51l -> 5。
// payload 变宽（NOP 0x00 编码）同 dex_instruction.cc SizeInCodeUnitsComplexOpcode：
//   0x0100 packed-switch: 4 + size*2；0x0200 sparse-switch: 2 + size*4；
//   0x0300 fill-array-data: 4 + (elem_width*size + 1)/2；其余 0x00 开头按 nop=1。
#include "opcodes.h"

#include "dex/dex_file.h"

namespace dexvm::dex {
namespace {

enum class Fmt : uint8_t {
  k10x, k12x, k11n, k11x, k10t,
  k20t, k22x, k21t, k21s, k21h, k21c, k23x, k22b, k22t, k22s, k22c,
  k32x, k30t, k31t, k31i, k31c, k35c, k3rc,
  k45cc, k4rcc,
  k51l,
};

constexpr uint32_t FormatWidth(Fmt f) {
  switch (f) {
    case Fmt::k10x:
    case Fmt::k12x:
    case Fmt::k11n:
    case Fmt::k11x:
    case Fmt::k10t:
      return 1;
    case Fmt::k20t:
    case Fmt::k22x:
    case Fmt::k21t:
    case Fmt::k21s:
    case Fmt::k21h:
    case Fmt::k21c:
    case Fmt::k23x:
    case Fmt::k22b:
    case Fmt::k22t:
    case Fmt::k22s:
    case Fmt::k22c:
      return 2;
    case Fmt::k32x:
    case Fmt::k30t:
    case Fmt::k31t:
    case Fmt::k31i:
    case Fmt::k31c:
    case Fmt::k35c:
    case Fmt::k3rc:
      return 3;
    case Fmt::k45cc:
    case Fmt::k4rcc:
      return 4;
    case Fmt::k51l:
      return 5;
  }
  return 1;
}

// (opcode, format)：对照 DEX_INSTRUCTION_LIST，含 unused 槽位（k10x）。
#define DEX_OPCODE_FORMATS(V) \
  V(0x00, k10x) V(0x01, k12x) V(0x02, k22x) V(0x03, k32x) \
  V(0x04, k12x) V(0x05, k22x) V(0x06, k32x) V(0x07, k12x) \
  V(0x08, k22x) V(0x09, k32x) V(0x0A, k11x) V(0x0B, k11x) \
  V(0x0C, k11x) V(0x0D, k11x) V(0x0E, k10x) V(0x0F, k11x) \
  V(0x10, k11x) V(0x11, k11x) V(0x12, k11n) V(0x13, k21s) \
  V(0x14, k31i) V(0x15, k21h) V(0x16, k21s) V(0x17, k31i) \
  V(0x18, k51l) V(0x19, k21h) V(0x1A, k21c) V(0x1B, k31c) \
  V(0x1C, k21c) V(0x1D, k11x) V(0x1E, k11x) V(0x1F, k21c) \
  V(0x20, k22c) V(0x21, k12x) V(0x22, k21c) V(0x23, k22c) \
  V(0x24, k35c) V(0x25, k3rc) V(0x26, k31t) V(0x27, k11x) \
  V(0x28, k10t) V(0x29, k20t) V(0x2A, k30t) V(0x2B, k31t) \
  V(0x2C, k31t) V(0x2D, k23x) V(0x2E, k23x) V(0x2F, k23x) \
  V(0x30, k23x) V(0x31, k23x) V(0x32, k22t) V(0x33, k22t) \
  V(0x34, k22t) V(0x35, k22t) V(0x36, k22t) V(0x37, k22t) \
  V(0x38, k21t) V(0x39, k21t) V(0x3A, k21t) V(0x3B, k21t) \
  V(0x3C, k21t) V(0x3D, k21t) V(0x3E, k10x) V(0x3F, k10x) \
  V(0x40, k10x) V(0x41, k10x) V(0x42, k10x) V(0x43, k10x) \
  V(0x44, k23x) V(0x45, k23x) V(0x46, k23x) V(0x47, k23x) \
  V(0x48, k23x) V(0x49, k23x) V(0x4A, k23x) V(0x4B, k23x) \
  V(0x4C, k23x) V(0x4D, k23x) V(0x4E, k23x) V(0x4F, k23x) \
  V(0x50, k23x) V(0x51, k23x) V(0x52, k22c) V(0x53, k22c) \
  V(0x54, k22c) V(0x55, k22c) V(0x56, k22c) V(0x57, k22c) \
  V(0x58, k22c) V(0x59, k22c) V(0x5A, k22c) V(0x5B, k22c) \
  V(0x5C, k22c) V(0x5D, k22c) V(0x5E, k22c) V(0x5F, k22c) \
  V(0x60, k21c) V(0x61, k21c) V(0x62, k21c) V(0x63, k21c) \
  V(0x64, k21c) V(0x65, k21c) V(0x66, k21c) V(0x67, k21c) \
  V(0x68, k21c) V(0x69, k21c) V(0x6A, k21c) V(0x6B, k21c) \
  V(0x6C, k21c) V(0x6D, k21c) V(0x6E, k35c) V(0x6F, k35c) \
  V(0x70, k35c) V(0x71, k35c) V(0x72, k35c) V(0x73, k10x) \
  V(0x74, k3rc) V(0x75, k3rc) V(0x76, k3rc) V(0x77, k3rc) \
  V(0x78, k3rc) V(0x79, k10x) V(0x7A, k10x) V(0x7B, k12x) \
  V(0x7C, k12x) V(0x7D, k12x) V(0x7E, k12x) V(0x7F, k12x) \
  V(0x80, k12x) V(0x81, k12x) V(0x82, k12x) V(0x83, k12x) \
  V(0x84, k12x) V(0x85, k12x) V(0x86, k12x) V(0x87, k12x) \
  V(0x88, k12x) V(0x89, k12x) V(0x8A, k12x) V(0x8B, k12x) \
  V(0x8C, k12x) V(0x8D, k12x) V(0x8E, k12x) V(0x8F, k12x) \
  V(0x90, k23x) V(0x91, k23x) V(0x92, k23x) V(0x93, k23x) \
  V(0x94, k23x) V(0x95, k23x) V(0x96, k23x) V(0x97, k23x) \
  V(0x98, k23x) V(0x99, k23x) V(0x9A, k23x) V(0x9B, k23x) \
  V(0x9C, k23x) V(0x9D, k23x) V(0x9E, k23x) V(0x9F, k23x) \
  V(0xA0, k23x) V(0xA1, k23x) V(0xA2, k23x) V(0xA3, k23x) \
  V(0xA4, k23x) V(0xA5, k23x) V(0xA6, k23x) V(0xA7, k23x) \
  V(0xA8, k23x) V(0xA9, k23x) V(0xAA, k23x) V(0xAB, k23x) \
  V(0xAC, k23x) V(0xAD, k23x) V(0xAE, k23x) V(0xAF, k23x) \
  V(0xB0, k12x) V(0xB1, k12x) V(0xB2, k12x) V(0xB3, k12x) \
  V(0xB4, k12x) V(0xB5, k12x) V(0xB6, k12x) V(0xB7, k12x) \
  V(0xB8, k12x) V(0xB9, k12x) V(0xBA, k12x) V(0xBB, k12x) \
  V(0xBC, k12x) V(0xBD, k12x) V(0xBE, k12x) V(0xBF, k12x) \
  V(0xC0, k12x) V(0xC1, k12x) V(0xC2, k12x) V(0xC3, k12x) \
  V(0xC4, k12x) V(0xC5, k12x) V(0xC6, k12x) V(0xC7, k12x) \
  V(0xC8, k12x) V(0xC9, k12x) V(0xCA, k12x) V(0xCB, k12x) \
  V(0xCC, k12x) V(0xCD, k12x) V(0xCE, k12x) V(0xCF, k12x) \
  V(0xD0, k22s) V(0xD1, k22s) V(0xD2, k22s) V(0xD3, k22s) \
  V(0xD4, k22s) V(0xD5, k22s) V(0xD6, k22s) V(0xD7, k22s) \
  V(0xD8, k22b) V(0xD9, k22b) V(0xDA, k22b) V(0xDB, k22b) \
  V(0xDC, k22b) V(0xDD, k22b) V(0xDE, k22b) V(0xDF, k22b) \
  V(0xE0, k22b) V(0xE1, k22b) V(0xE2, k22b) V(0xE3, k10x) \
  V(0xE4, k10x) V(0xE5, k10x) V(0xE6, k10x) V(0xE7, k10x) \
  V(0xE8, k10x) V(0xE9, k10x) V(0xEA, k10x) V(0xEB, k10x) \
  V(0xEC, k10x) V(0xED, k10x) V(0xEE, k10x) V(0xEF, k10x) \
  V(0xF0, k10x) V(0xF1, k10x) V(0xF2, k10x) V(0xF3, k10x) \
  V(0xF4, k10x) V(0xF5, k10x) V(0xF6, k10x) V(0xF7, k10x) \
  V(0xF8, k10x) V(0xF9, k10x) V(0xFA, k45cc) V(0xFB, k4rcc) \
  V(0xFC, k35c) V(0xFD, k3rc) V(0xFE, k21c) V(0xFF, k21c)

constexpr uint8_t kWidthTable[256] = {
#define WIDTH_ENTRY(op, fmt) [op] = static_cast<uint8_t>(FormatWidth(Fmt::fmt)),
    DEX_OPCODE_FORMATS(WIDTH_ENTRY)
#undef WIDTH_ENTRY
};

#undef DEX_OPCODE_FORMATS

}  // namespace

uint32_t InsnWidth(const uint16_t* insns) {
  uint16_t first = insns[0];
  if ((first & 0xff) == kNop) {
    // NOP 编码的变长 payload 或普通 nop（对照 SizeInCodeUnitsComplexOpcode）。
    switch (first) {
      case kPackedSwitchPayload:  // ident + size + first_key + targets[size]
        return 4 + 2u * insns[1];
      case kSparseSwitchPayload:  // ident + size + (key,target)[size]
        return 2 + 4u * insns[1];
      case kArrayDataPayload: {   // ident + elem_width + size(u32) + data
        uint32_t elem_width = insns[1];
        uint32_t length =
            static_cast<uint32_t>(insns[2]) |
            (static_cast<uint32_t>(insns[3]) << 16);
        // 同 ART：4 + ceil(elem_width * length / 2)，64 位计算防溢出。
        uint64_t data_units =
            (static_cast<uint64_t>(elem_width) * length + 1) / 2;
        return static_cast<uint32_t>(4 + data_units);
      }
      default:
        return 1;  // 普通 nop。
    }
  }
  return kWidthTable[first & 0xff];
}

}  // namespace dexvm::dex
