#include "dex_verify.h"

#include <cstring>

#include "leb128.h"

namespace dexvm::dex {
namespace {

// header_item 字段偏移（standard dex，对照 dex_file_structs.h / DEX 规范）。
constexpr size_t kOffMagic = 0x00;          // u1[8] "dex\n035\0"
constexpr size_t kOffChecksum = 0x08;       // u4
constexpr size_t kOffFileSize = 0x20;       // u4
constexpr size_t kOffHeaderSize = 0x24;     // u4
constexpr size_t kOffEndianTag = 0x28;      // u4
constexpr size_t kOffMapOff = 0x34;         // u4
constexpr size_t kOffStringIdsSize = 0x38;  // u4
constexpr size_t kOffStringIdsOff = 0x3c;   // u4
constexpr size_t kOffTypeIdsSize = 0x40;    // u4
constexpr size_t kOffTypeIdsOff = 0x44;     // u4
constexpr size_t kOffProtoIdsSize = 0x48;   // u4
constexpr size_t kOffProtoIdsOff = 0x4c;    // u4
constexpr size_t kOffFieldIdsSize = 0x50;   // u4
constexpr size_t kOffFieldIdsOff = 0x54;    // u4
constexpr size_t kOffMethodIdsSize = 0x58;  // u4
constexpr size_t kOffMethodIdsOff = 0x5c;   // u4
constexpr size_t kOffClassDefsSize = 0x60;  // u4
constexpr size_t kOffClassDefsOff = 0x64;   // u4

constexpr uint32_t kEndianConstant = 0x12345678;
constexpr uint32_t kNoIndex = 0xffffffffu;
constexpr size_t kHeaderSize35 = 0x70;
constexpr size_t kHeaderSize41 = 0x78;
constexpr uint32_t kMinVersion = 35;
constexpr uint32_t kMaxVersion = 41;

// map_item type（对照 dex_file.h MapItemType）。
constexpr uint16_t kMapHeaderItem = 0x0000;
constexpr uint16_t kMapStringIdItem = 0x0001;
constexpr uint16_t kMapTypeIdItem = 0x0002;
constexpr uint16_t kMapProtoIdItem = 0x0003;
constexpr uint16_t kMapFieldIdItem = 0x0004;
constexpr uint16_t kMapMethodIdItem = 0x0005;
constexpr uint16_t kMapClassDefItem = 0x0006;
constexpr uint16_t kMapCallSiteIdItem = 0x0007;
constexpr uint16_t kMapMethodHandleItem = 0x0008;
constexpr uint16_t kMapMapList = 0x1000;
constexpr uint16_t kMapTypeList = 0x1001;
constexpr uint16_t kMapAnnotationSetRefList = 0x1002;
constexpr uint16_t kMapAnnotationSetItem = 0x1003;
constexpr uint16_t kMapClassDataItem = 0x2000;
constexpr uint16_t kMapCodeItem = 0x2001;
constexpr uint16_t kMapStringDataItem = 0x2002;
constexpr uint16_t kMapDebugInfoItem = 0x2003;
constexpr uint16_t kMapAnnotationItem = 0x2004;
constexpr uint16_t kMapEncodedArrayItem = 0x2005;
constexpr uint16_t kMapAnnotationsDirectoryItem = 0x2006;
constexpr uint16_t kMapHiddenapiClassData = 0xF000;

uint16_t Rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool Fail(std::string* error, const char* msg) {
  if (error != nullptr) {
    *error = msg;
  }
  return false;
}

// offset + length 是否落在 [0, limit) 内（防溢出）。
bool InBounds(uint32_t off, uint64_t length, uint64_t limit) {
  return off <= limit && length <= limit - off;
}

// 固定大小 map_item 的单条目字节数；变长返回 0。
uint32_t MapItemFixedSize(uint16_t type) {
  switch (type) {
    case kMapStringIdItem:
    case kMapTypeIdItem:
    case kMapCallSiteIdItem:
      return 4;
    case kMapFieldIdItem:
    case kMapMethodIdItem:
    case kMapMethodHandleItem:
      return 8;
    case kMapProtoIdItem:
      return 12;
    case kMapClassDefItem:
      return 32;
    default:
      return 0;
  }
}

// map_item type -> 去重桶下标（0..20）；未知类型返回 -1。
int MapTypeIndex(uint16_t type) {
  if (type <= kMapMethodHandleItem) {
    return type;  // 0..8
  }
  if (type >= kMapMapList && type <= kMapAnnotationSetItem) {
    return 9 + (type - kMapMapList);  // 9..12
  }
  if (type >= kMapClassDataItem && type <= kMapAnnotationsDirectoryItem) {
    return 13 + (type - kMapClassDataItem);  // 13..19
  }
  if (type == kMapHiddenapiClassData) {
    return 20;
  }
  return -1;
}

// TypeList 校验：u4 size + u2 type_idx[size]，所有 type_idx < type_ids_size。
bool VerifyTypeList(const uint8_t* data, uint32_t file_size, uint32_t off,
                    uint32_t type_ids_size, std::string* error) {
  if (!InBounds(off, 4, file_size)) {
    return Fail(error, "type_list out of bounds");
  }
  uint32_t count = Rd32(data + off);
  if (!InBounds(off, 4u + static_cast<uint64_t>(count) * 2u, file_size)) {
    return Fail(error, "type_list entries out of bounds");
  }
  for (uint32_t i = 0; i < count; ++i) {
    uint16_t idx = Rd16(data + off + 4 + i * 2);
    if (idx >= type_ids_size) {
      return Fail(error, "type_list type_idx out of range");
    }
  }
  return true;
}

bool VerifyStringData(const uint8_t* data, uint32_t file_size, uint32_t off,
                      std::string* error) {
  if (!InBounds(off, 1, file_size)) {
    return Fail(error, "string_data_off out of bounds");
  }
  const uint8_t* p = data + off;
  const uint8_t* end = data + file_size;
  uint32_t utf16_size;
  if (!ReadUleb128(&p, end, &utf16_size)) {
    return Fail(error, "bad string_data utf16_size");
  }
  // MUTF-8 数据以 NUL 结尾。
  while (p < end && *p != 0) {
    ++p;
  }
  if (p == end) {
    return Fail(error, "unterminated string_data");
  }
  return true;
}

bool VerifyMap(const uint8_t* data, uint32_t file_size, uint32_t map_off,
               uint32_t string_ids_size, uint32_t string_ids_off,
               uint32_t type_ids_size, uint32_t type_ids_off,
               uint32_t proto_ids_size, uint32_t proto_ids_off,
               uint32_t field_ids_size, uint32_t field_ids_off,
               uint32_t method_ids_size, uint32_t method_ids_off,
               uint32_t class_defs_size, uint32_t class_defs_off,
               std::string* error) {
  if (!InBounds(map_off, 4, file_size)) {
    return Fail(error, "map_off out of bounds");
  }
  uint32_t count = Rd32(data + map_off);
  if (!InBounds(map_off, 4u + static_cast<uint64_t>(count) * 12u,
                file_size)) {
    return Fail(error, "map_list out of bounds");
  }
  bool seen[21] = {};  // MapTypeIndex 桶 0..20 查重
  bool have_header = false;
  bool have_map_list = false;
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* item = data + map_off + 4 + i * 12;
    uint16_t type = Rd16(item);
    uint16_t unused = Rd16(item + 2);
    uint32_t size = Rd32(item + 4);
    uint32_t off = Rd32(item + 8);
    if (unused != 0) {
      return Fail(error, "map_item unused != 0");
    }
    int bucket = MapTypeIndex(type);
    if (bucket < 0) {
      return Fail(error, "map_item unknown type");
    }
    if (seen[bucket]) {
      return Fail(error, "duplicate map_item type");
    }
    seen[bucket] = true;
    switch (type) {
      case kMapHeaderItem:
        if (size != 1 || off != 0) {
          return Fail(error, "bad header map_item");
        }
        have_header = true;
        break;
      case kMapMapList:
        if (size != 1 || off != map_off) {
          return Fail(error, "bad map_list map_item");
        }
        have_map_list = true;
        break;
      case kMapStringIdItem:
        if (size != string_ids_size || off != string_ids_off) {
          return Fail(error, "map_item/string_ids mismatch");
        }
        break;
      case kMapTypeIdItem:
        if (size != type_ids_size || off != type_ids_off) {
          return Fail(error, "map_item/type_ids mismatch");
        }
        break;
      case kMapProtoIdItem:
        if (size != proto_ids_size || off != proto_ids_off) {
          return Fail(error, "map_item/proto_ids mismatch");
        }
        break;
      case kMapFieldIdItem:
        if (size != field_ids_size || off != field_ids_off) {
          return Fail(error, "map_item/field_ids mismatch");
        }
        break;
      case kMapMethodIdItem:
        if (size != method_ids_size || off != method_ids_off) {
          return Fail(error, "map_item/method_ids mismatch");
        }
        break;
      case kMapClassDefItem:
        if (size != class_defs_size || off != class_defs_off) {
          return Fail(error, "map_item/class_defs mismatch");
        }
        break;
      default:
        break;
    }
    uint32_t item_size = MapItemFixedSize(type);
    if (item_size != 0) {
      if (!InBounds(off, static_cast<uint64_t>(item_size) * size,
                    file_size)) {
        return Fail(error, "map_item entries out of bounds");
      }
    } else if (size != 0 && !InBounds(off, 1, file_size)) {
      return Fail(error, "map_item offset out of bounds");
    }
  }
  if (!have_header || !have_map_list) {
    return Fail(error, "map_list missing header/map_list entry");
  }
  return true;
}

}  // namespace

bool VerifyDexFile(const uint8_t* data, size_t size, std::string* error) {
  if (data == nullptr) {
    return Fail(error, "null data");
  }
  if (size < kHeaderSize35) {
    return Fail(error, "file too small for dex header");
  }

  // magic: "dex\n" + 3 位版本 + '\0'，接受 035..041。
  if (std::memcmp(data + kOffMagic, "dex\n", 4) != 0) {
    return Fail(error, "bad dex magic");
  }
  if (data[7] != 0 || data[4] < '0' || data[4] > '9' || data[5] < '0' ||
      data[5] > '9' || data[6] < '0' || data[6] > '9') {
    return Fail(error, "bad dex version");
  }
  uint32_t version = static_cast<uint32_t>(data[4] - '0') * 100 +
                     static_cast<uint32_t>(data[5] - '0') * 10 +
                     static_cast<uint32_t>(data[6] - '0');
  if (version < kMinVersion || version > kMaxVersion) {
    return Fail(error, "unsupported dex version");
  }

  uint32_t file_size = Rd32(data + kOffFileSize);
  uint32_t header_size = Rd32(data + kOffHeaderSize);
  uint32_t endian_tag = Rd32(data + kOffEndianTag);

  if (endian_tag != kEndianConstant) {
    return Fail(error, "bad endian_tag");
  }
  size_t expected_header_size = (version >= 41) ? kHeaderSize41 : kHeaderSize35;
  if (header_size != expected_header_size) {
    return Fail(error, "bad header_size");
  }
  if (file_size > size) {
    return Fail(error, "truncated dex (file_size > buffer)");
  }
  if (file_size < header_size) {
    return Fail(error, "bad file_size");
  }
  const uint32_t fs = file_size;  // 后续越界检查以 file_size 为界

  uint32_t map_off = Rd32(data + kOffMapOff);
  uint32_t string_ids_size = Rd32(data + kOffStringIdsSize);
  uint32_t string_ids_off = Rd32(data + kOffStringIdsOff);
  uint32_t type_ids_size = Rd32(data + kOffTypeIdsSize);
  uint32_t type_ids_off = Rd32(data + kOffTypeIdsOff);
  uint32_t proto_ids_size = Rd32(data + kOffProtoIdsSize);
  uint32_t proto_ids_off = Rd32(data + kOffProtoIdsOff);
  uint32_t field_ids_size = Rd32(data + kOffFieldIdsSize);
  uint32_t field_ids_off = Rd32(data + kOffFieldIdsOff);
  uint32_t method_ids_size = Rd32(data + kOffMethodIdsSize);
  uint32_t method_ids_off = Rd32(data + kOffMethodIdsOff);
  uint32_t class_defs_size = Rd32(data + kOffClassDefsSize);
  uint32_t class_defs_off = Rd32(data + kOffClassDefsOff);

  // 各表偏移 + 大小越界（对齐按 DEX 规范 4 字节）。
  struct Table {
    const char* name;
    uint32_t size;
    uint32_t off;
    uint32_t entry_size;
  };
  const Table tables[] = {
      {"string_ids", string_ids_size, string_ids_off, 4},
      {"type_ids", type_ids_size, type_ids_off, 4},
      {"proto_ids", proto_ids_size, proto_ids_off, 12},
      {"field_ids", field_ids_size, field_ids_off, 8},
      {"method_ids", method_ids_size, method_ids_off, 8},
      {"class_defs", class_defs_size, class_defs_off, 32},
  };
  for (const Table& t : tables) {
    if (t.size == 0) {
      continue;
    }
    if ((t.off & 3) != 0) {
      return Fail(error, "unaligned id table offset");
    }
    if (!InBounds(t.off, static_cast<uint64_t>(t.entry_size) * t.size, fs)) {
      return Fail(error, "id table out of bounds");
    }
  }

  // map_list 基本一致性（map_off == 0 表示无 map，跳过）。
  if (map_off != 0) {
    if ((map_off & 3) != 0) {
      return Fail(error, "unaligned map_off");
    }
    if (!VerifyMap(data, fs, map_off, string_ids_size, string_ids_off,
                   type_ids_size, type_ids_off, proto_ids_size, proto_ids_off,
                   field_ids_size, field_ids_off, method_ids_size,
                   method_ids_off, class_defs_size, class_defs_off, error)) {
      return false;
    }
  }

  // string_id_item：string_data_off 越界与 string_data 可读性。
  for (uint32_t i = 0; i < string_ids_size; ++i) {
    uint32_t off = Rd32(data + string_ids_off + i * 4);
    if (!VerifyStringData(data, fs, off, error)) {
      return false;
    }
  }

  // type_id_item：descriptor_idx < string_ids_size。
  for (uint32_t i = 0; i < type_ids_size; ++i) {
    uint32_t desc_idx = Rd32(data + type_ids_off + i * 4);
    if (desc_idx >= string_ids_size) {
      return Fail(error, "type_id descriptor_idx out of range");
    }
  }

  // proto_id_item：引用 + parameters_off TypeList。
  for (uint32_t i = 0; i < proto_ids_size; ++i) {
    const uint8_t* p = data + proto_ids_off + i * 12;
    uint32_t shorty_idx = Rd32(p);
    uint32_t return_type_idx = Rd32(p + 4);
    uint32_t parameters_off = Rd32(p + 8);
    if (shorty_idx >= string_ids_size) {
      return Fail(error, "proto_id shorty_idx out of range");
    }
    if (return_type_idx >= type_ids_size) {
      return Fail(error, "proto_id return_type_idx out of range");
    }
    if (parameters_off != 0 &&
        !VerifyTypeList(data, fs, parameters_off, type_ids_size, error)) {
      return false;
    }
  }

  // field_id_item：class_idx/type_idx/name_idx。
  for (uint32_t i = 0; i < field_ids_size; ++i) {
    const uint8_t* p = data + field_ids_off + i * 8;
    uint16_t class_idx = Rd16(p);
    uint16_t type_idx = Rd16(p + 2);
    uint32_t name_idx = Rd32(p + 4);
    if (class_idx >= type_ids_size || type_idx >= type_ids_size) {
      return Fail(error, "field_id class/type idx out of range");
    }
    if (name_idx >= string_ids_size) {
      return Fail(error, "field_id name_idx out of range");
    }
  }

  // method_id_item：class_idx/proto_idx/name_idx。
  for (uint32_t i = 0; i < method_ids_size; ++i) {
    const uint8_t* p = data + method_ids_off + i * 8;
    uint16_t class_idx = Rd16(p);
    uint16_t proto_idx = Rd16(p + 2);
    uint32_t name_idx = Rd32(p + 4);
    if (class_idx >= type_ids_size) {
      return Fail(error, "method_id class_idx out of range");
    }
    if (proto_idx >= proto_ids_size) {
      return Fail(error, "method_id proto_idx out of range");
    }
    if (name_idx >= string_ids_size) {
      return Fail(error, "method_id name_idx out of range");
    }
  }

  // class_def_item：引用 idx 与 off 越界检查。
  for (uint32_t i = 0; i < class_defs_size; ++i) {
    const uint8_t* p = data + class_defs_off + i * 32;
    uint32_t class_idx = Rd32(p);
    uint32_t superclass_idx = Rd32(p + 8);
    uint32_t interfaces_off = Rd32(p + 12);
    uint32_t source_file_idx = Rd32(p + 16);
    uint32_t annotations_off = Rd32(p + 20);
    uint32_t class_data_off = Rd32(p + 24);
    uint32_t static_values_off = Rd32(p + 28);
    if (class_idx >= type_ids_size) {
      return Fail(error, "class_def class_idx out of range");
    }
    if (superclass_idx != kNoIndex && superclass_idx >= type_ids_size) {
      return Fail(error, "class_def superclass_idx out of range");
    }
    if (source_file_idx != kNoIndex && source_file_idx >= string_ids_size) {
      return Fail(error, "class_def source_file_idx out of range");
    }
    if (interfaces_off != 0 &&
        !VerifyTypeList(data, fs, interfaces_off, type_ids_size, error)) {
      return false;
    }
    if (annotations_off != 0 && !InBounds(annotations_off, 1, fs)) {
      return Fail(error, "class_def annotations_off out of bounds");
    }
    if (class_data_off != 0 && !InBounds(class_data_off, 1, fs)) {
      return Fail(error, "class_def class_data_off out of bounds");
    }
    if (static_values_off != 0 && !InBounds(static_values_off, 1, fs)) {
      return Fail(error, "class_def static_values_off out of bounds");
    }
  }

  return true;
}

}  // namespace dexvm::dex
