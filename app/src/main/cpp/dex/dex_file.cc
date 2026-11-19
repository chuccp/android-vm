// DexFile::Impl 主解析器。
// 二进制布局逐项对照 reference/art/libdexfile/dex/：
//   - dex_file_structs.h        StringId/TypeId/ProtoId/FieldId/MethodId/
//                               ClassDef/TypeList/TryItem 布局
//   - standard_dex_file.h:60-93 CodeItem 布局（4 字节对齐）
//   - class_accessor-inl.h      class_data_item ULEB 流（4 计数 + 四段，
//                               field_idx_diff/method_idx_diff 累积）
//   - dex_file.cc:594-642       ReadSignedInt/ReadUnsignedInt/ReadSignedLong/
//                               ReadUnsignedLong（encoded_value 宽度语义）
//   - dex_file.cc:717-797       EncodedArrayValueIterator（encoded_array 解码）
//   - dex_file-inl.h:270-275    GetTryItems：try_item 数组在 insns 后 4 对齐
#include "dex/dex_file.h"

#include <cstring>
#include <unordered_map>
#include <utility>
#include <vector>

#include "dex/dex_verify.h"
#include "dex/leb128.h"
#include "dex/mutf8.h"

namespace dexvm::dex {
namespace {

// header_item 字段偏移（standard dex，对照 dex_file_structs.h Header）。
constexpr size_t kOffChecksum = 0x08;       // u4
constexpr size_t kOffFileSize = 0x20;       // u4
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

constexpr uint32_t kNoIndex = 0xffffffffu;

// code_item 头（对照 standard_dex_file.h CodeItem）。
constexpr size_t kCodeItemHeaderSize = 16;  // 4*u2 + 2*u4
constexpr size_t kTryItemSize = 8;          // start_addr u4 + insn_count u2 + handler_off u2

// encoded_value 类型（对照 dex_file.h EncodedArrayValueIterator::ValueType）。
constexpr uint8_t kValueByte = 0x00;
constexpr uint8_t kValueShort = 0x02;
constexpr uint8_t kValueChar = 0x03;
constexpr uint8_t kValueInt = 0x04;
constexpr uint8_t kValueLong = 0x06;
constexpr uint8_t kValueFloat = 0x10;
constexpr uint8_t kValueDouble = 0x11;
constexpr uint8_t kValueString = 0x17;
constexpr uint8_t kValueType = 0x18;
constexpr uint8_t kValueNull = 0x1e;
constexpr uint8_t kValueBoolean = 0x1f;
constexpr uint8_t kValueTypeMask = 0x1f;
constexpr uint8_t kValueArgShift = 5;

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

// ---- encoded_value 宽度读取 ----
// 逐条对照 oracle dex_file.cc:594-642：
//   ReadSignedInt/ReadUnsignedInt(zwidth 为零基字节数)：
//   字节流逐字节左移组装，fill_on_right=false 时高位补 0/符号；
//   fill_on_right=true（float/double 位型）时低位补 0。

// 有符号 32 位，字宽 zwidth+1，符号扩展到高位。
int32_t ReadSignedInt(const uint8_t* ptr, int zwidth) {
  int32_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = static_cast<int32_t>((static_cast<uint32_t>(val) >> 8) |
                               (static_cast<uint32_t>(*ptr++) << 24));
  }
  val >>= (3 - zwidth) * 8;
  return val;
}

// 无符号 32 位；fill_on_right=false 高位补 0，true 低位补 0（位型）。
uint32_t ReadUnsignedInt(const uint8_t* ptr, int zwidth, bool fill_on_right) {
  uint32_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = (val >> 8) | (static_cast<uint32_t>(*ptr++) << 24);
  }
  if (!fill_on_right) {
    val >>= (3 - zwidth) * 8;
  }
  return val;
}

// 有符号 64 位，字宽 zwidth+1，符号扩展到高位。
int64_t ReadSignedLong(const uint8_t* ptr, int zwidth) {
  int64_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = static_cast<int64_t>((static_cast<uint64_t>(val) >> 8) |
                               (static_cast<uint64_t>(*ptr++) << 56));
  }
  val >>= (7 - zwidth) * 8;
  return val;
}

// 无符号 64 位；fill_on_right=false 高位补 0，true 低位补 0（位型）。
uint64_t ReadUnsignedLong(const uint8_t* ptr, int zwidth, bool fill_on_right) {
  uint64_t val = 0;
  for (int i = zwidth; i >= 0; --i) {
    val = (val >> 8) | (static_cast<uint64_t>(*ptr++) << 56);
  }
  if (!fill_on_right) {
    val >>= (7 - zwidth) * 8;
  }
  return val;
}

const ProtoId kEmptyProto = {};
const FieldId kEmptyField = {};
const MethodId kEmptyMethod = {};
const ClassDef kEmptyClassDef = {};

}  // namespace

struct DexFile::Impl {
  const uint8_t* data = nullptr;
  uint32_t file_size = 0;  // header.file_size（≤ size）
  uint32_t checksum = 0;

  // id 表（size + offset，指向原始字节）。
  uint32_t string_ids_size = 0;
  uint32_t string_ids_off = 0;
  uint32_t type_ids_size = 0;
  uint32_t type_ids_off = 0;
  uint32_t proto_ids_size = 0;
  uint32_t proto_ids_off = 0;
  uint32_t field_ids_size = 0;
  uint32_t field_ids_off = 0;
  uint32_t method_ids_size = 0;
  uint32_t method_ids_off = 0;
  uint32_t class_defs_size = 0;
  uint32_t class_defs_off = 0;

  // Open 时解析出的 id/class_def 表。
  std::vector<ProtoId> proto_ids;
  std::vector<FieldId> field_ids;
  std::vector<MethodId> method_ids;
  std::vector<ClassDef> class_defs;

  // GetString 的 MUTF-8 解码缓存（懒填充；预分配保证引用稳定）。
  mutable std::vector<std::string> strings;
  mutable std::vector<uint8_t> string_state;  // 0=未解码 1=已解码 2=解码失败

  // descriptor -> class_def 下标（懒建）。
  mutable bool descriptor_index_ready = false;
  mutable std::unordered_map<std::string_view, uint32_t> descriptor_index;

  // code_off -> CodeItem（懒解析；node-based 容器保证指针稳定）。
  mutable std::unordered_map<uint32_t, CodeItem> code_items;

  // 解析 string/type/proto/field/method/class_def 表（VerifyDexFile 已校验越界）。
  bool ParseTables(std::string* error);

  // GetString 的缓存实现。
  const std::string& GetStringCached(uint32_t idx) const;

  // TypeList（proto 参数 / 接口列表）。
  uint32_t TypeListCount(uint32_t off) const;
  uint32_t TypeListTypeIdx(uint32_t off, uint32_t i) const;

  // class_data_item 解码（对照 class_accessor-inl.h）。
  bool DecodeClassData(uint32_t off, ClassData* out, std::string* error) const;

  // EncodedArray（static_values_off）解码（对照 EncodedArrayValueIterator）。
  bool DecodeEncodedArray(uint32_t off, std::vector<EncodedValue>* out,
                          std::string* error) const;
  bool DecodeEncodedValue(const uint8_t** pp, const uint8_t* end,
                          EncodedValue* out, std::string* error) const;

  // code_item 解码（对照 standard_dex_file.h CodeItem + dex_file-inl.h GetTryItems）。
  const CodeItem* GetCodeItemCached(uint32_t code_off, std::string* error) const;
  bool DecodeCodeItem(uint32_t code_off, CodeItem* out, std::string* error) const;
};

bool DexFile::Impl::ParseTables(std::string* error) {
  const uint8_t* d = data;

  proto_ids.resize(proto_ids_size);
  for (uint32_t i = 0; i < proto_ids_size; ++i) {
    const uint8_t* p = d + proto_ids_off + i * 12;
    ProtoId& e = proto_ids[i];
    e.shorty_idx = Rd32(p);
    e.return_type_idx = Rd32(p + 4);
    e.parameters_off = Rd32(p + 8);
    if (e.shorty_idx >= string_ids_size || e.return_type_idx >= type_ids_size ||
        (e.parameters_off != 0 && !InBounds(e.parameters_off, 4, file_size))) {
      return Fail(error, "bad proto_id_item");
    }
  }

  field_ids.resize(field_ids_size);
  for (uint32_t i = 0; i < field_ids_size; ++i) {
    const uint8_t* p = d + field_ids_off + i * 8;
    FieldId& e = field_ids[i];
    e.class_idx = Rd16(p);
    e.type_idx = Rd16(p + 2);
    e.name_idx = Rd32(p + 4);
    if (e.class_idx >= type_ids_size || e.type_idx >= type_ids_size ||
        e.name_idx >= string_ids_size) {
      return Fail(error, "bad field_id_item");
    }
  }

  method_ids.resize(method_ids_size);
  for (uint32_t i = 0; i < method_ids_size; ++i) {
    const uint8_t* p = d + method_ids_off + i * 8;
    MethodId& e = method_ids[i];
    e.class_idx = Rd16(p);
    e.proto_idx = Rd16(p + 2);
    e.name_idx = Rd32(p + 4);
    if (e.class_idx >= type_ids_size || e.proto_idx >= proto_ids_size ||
        e.name_idx >= string_ids_size) {
      return Fail(error, "bad method_id_item");
    }
  }

  class_defs.resize(class_defs_size);
  for (uint32_t i = 0; i < class_defs_size; ++i) {
    const uint8_t* p = d + class_defs_off + i * 32;
    ClassDef& e = class_defs[i];
    e.class_idx = Rd32(p);
    e.access_flags = Rd32(p + 4);
    e.superclass_idx = Rd32(p + 8);
    e.interfaces_off = Rd32(p + 12);
    e.source_file_idx = Rd32(p + 16);
    e.annotations_off = Rd32(p + 20);
    e.class_data_off = Rd32(p + 24);
    e.static_values_off = Rd32(p + 28);
    if (e.class_idx >= type_ids_size ||
        (e.superclass_idx != kNoIndex && e.superclass_idx >= type_ids_size) ||
        (e.source_file_idx != kNoIndex && e.source_file_idx >= string_ids_size)) {
      return Fail(error, "bad class_def_item");
    }
    if (e.interfaces_off != 0 && !InBounds(e.interfaces_off, 4, file_size)) {
      return Fail(error, "bad class_def interfaces_off");
    }
    if (e.class_data_off != 0 && !InBounds(e.class_data_off, 1, file_size)) {
      return Fail(error, "bad class_def class_data_off");
    }
    if (e.static_values_off != 0 && !InBounds(e.static_values_off, 1, file_size)) {
      return Fail(error, "bad class_def static_values_off");
    }
  }

  // 缓存预分配（引用稳定）。
  strings.resize(string_ids_size);
  string_state.assign(string_ids_size, 0);
  return true;
}

const std::string& DexFile::Impl::GetStringCached(uint32_t idx) const {
  static const std::string kEmpty;
  if (idx >= string_ids_size) {
    return kEmpty;
  }
  if (string_state[idx] == 0) {
    std::string out;
    bool ok = false;
    uint32_t off = Rd32(data + string_ids_off + idx * 4);
    if (off < file_size) {
      const uint8_t* p = data + off;
      const uint8_t* end = data + file_size;
      uint32_t utf16_size = 0;
      // string_data_item: uleb utf16_size + MUTF-8 数据 + NUL。
      if (ReadUleb128(&p, end, &utf16_size)) {
        ok = DecodeMutf8(p, static_cast<size_t>(end - p), &out,
                         /*lossy=*/false);
      }
    }
    strings[idx] = std::move(out);
    string_state[idx] = ok ? 1 : 2;
  }
  return strings[idx];
}

uint32_t DexFile::Impl::TypeListCount(uint32_t off) const {
  if (off == 0 || !InBounds(off, 4, file_size)) {
    return 0;
  }
  return Rd32(data + off);
}

uint32_t DexFile::Impl::TypeListTypeIdx(uint32_t off, uint32_t i) const {
  if (off == 0 || !InBounds(off, 4, file_size)) {
    return kNoIndex;
  }
  uint32_t count = Rd32(data + off);
  if (i >= count) {
    return kNoIndex;
  }
  // 64 位算 off + 4 + i*2 + 2，防 u32 回绕。
  uint64_t entry = static_cast<uint64_t>(off) + 4u + static_cast<uint64_t>(i) * 2u;
  if (entry + 2u > file_size) {
    return kNoIndex;
  }
  return Rd16(data + entry);
}

bool DexFile::Impl::DecodeClassData(uint32_t off, ClassData* out,
                                    std::string* error) const {
  const uint8_t* p = data + off;
  const uint8_t* end = data + file_size;
  uint32_t num_static = 0;
  uint32_t num_instance = 0;
  uint32_t num_direct = 0;
  uint32_t num_virtual = 0;
  if (!ReadUleb128(&p, end, &num_static) ||
      !ReadUleb128(&p, end, &num_instance) ||
      !ReadUleb128(&p, end, &num_direct) ||
      !ReadUleb128(&p, end, &num_virtual)) {
    return Fail(error, "truncated class_data_item counts");
  }

  // class_data_item：ULEB 流四段（对照 class_accessor-inl.h VisitFieldsAndMethods）。
  // field_idx_diff/method_idx_diff 为增量，累积为绝对 idx。
  auto read_fields = [&](uint32_t count, std::vector<EncodedField>* dst,
                         const char* what) -> bool {
    uint64_t idx = 0;
    dst->reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t diff = 0;
      uint32_t access_flags = 0;
      if (!ReadUleb128(&p, end, &diff) ||
          !ReadUleb128(&p, end, &access_flags)) {
        if (error != nullptr) {
          *error = std::string("truncated class_data ") + what;
        }
        return false;
      }
      idx += diff;
      if (idx >= field_ids_size) {
        return Fail(error, "class_data field_idx out of range");
      }
      dst->push_back({static_cast<uint32_t>(idx), access_flags});
    }
    return true;
  };
  auto read_methods = [&](uint32_t count, std::vector<EncodedMethod>* dst,
                          const char* what) -> bool {
    uint64_t idx = 0;
    dst->reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t diff = 0;
      uint32_t access_flags = 0;
      uint32_t code_off = 0;
      if (!ReadUleb128(&p, end, &diff) ||
          !ReadUleb128(&p, end, &access_flags) ||
          !ReadUleb128(&p, end, &code_off)) {
        if (error != nullptr) {
          *error = std::string("truncated class_data ") + what;
        }
        return false;
      }
      idx += diff;
      if (idx >= method_ids_size) {
        return Fail(error, "class_data method_idx out of range");
      }
      if (code_off != 0 && !InBounds(code_off, kCodeItemHeaderSize, file_size)) {
        return Fail(error, "class_data code_off out of bounds");
      }
      dst->push_back({static_cast<uint32_t>(idx), access_flags, code_off});
    }
    return true;
  };

  if (!read_fields(num_static, &out->static_fields, "static fields") ||
      !read_fields(num_instance, &out->instance_fields, "instance fields") ||
      !read_methods(num_direct, &out->direct_methods, "direct methods") ||
      !read_methods(num_virtual, &out->virtual_methods, "virtual methods")) {
    return false;
  }
  return true;
}

bool DexFile::Impl::DecodeEncodedValue(const uint8_t** pp, const uint8_t* end,
                                       EncodedValue* out,
                                       std::string* error) const {
  const uint8_t* p = *pp;
  if (p >= end) {
    return Fail(error, "truncated encoded_value");
  }
  uint8_t header = *p++;
  uint8_t type = header & kValueTypeMask;
  int arg = header >> kValueArgShift;  // 零基字节数
  size_t width = static_cast<size_t>(arg) + 1;

  auto need = [&](size_t n) -> bool {
    if (static_cast<size_t>(end - p) < n) {
      return Fail(error, "truncated encoded_value payload");
    }
    return true;
  };

  out->kind = EncodedValue::Kind::kZero;
  out->int_value = 0;
  out->bits = 0;
  out->idx = 0;

  switch (type) {
    case kValueByte: {
      if (width > 1 || !need(width)) return Fail(error, "bad VALUE_BYTE");
      int64_t v = ReadSignedInt(p, arg);
      p += width;
      out->kind = (v == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kInt;
      out->int_value = v;
      break;
    }
    case kValueShort: {
      if (width > 2 || !need(width)) return Fail(error, "bad VALUE_SHORT");
      int64_t v = ReadSignedInt(p, arg);
      p += width;
      out->kind = (v == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kInt;
      out->int_value = v;
      break;
    }
    case kValueChar: {
      if (width > 2 || !need(width)) return Fail(error, "bad VALUE_CHAR");
      int64_t v = ReadUnsignedInt(p, arg, /*fill_on_right=*/false);
      p += width;
      out->kind = (v == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kInt;
      out->int_value = v;
      break;
    }
    case kValueInt: {
      if (width > 4 || !need(width)) return Fail(error, "bad VALUE_INT");
      int64_t v = ReadSignedInt(p, arg);
      p += width;
      out->kind = (v == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kInt;
      out->int_value = v;
      break;
    }
    case kValueLong: {
      if (width > 8 || !need(width)) return Fail(error, "bad VALUE_LONG");
      int64_t v = ReadSignedLong(p, arg);
      p += width;
      out->kind = (v == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kLong;
      out->int_value = v;
      break;
    }
    case kValueFloat: {
      if (width > 4 || !need(width)) return Fail(error, "bad VALUE_FLOAT");
      uint64_t bits = ReadUnsignedInt(p, arg, /*fill_on_right=*/true);
      p += width;
      out->kind = (bits == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kFloat;
      out->bits = bits;
      break;
    }
    case kValueDouble: {
      if (width > 8 || !need(width)) return Fail(error, "bad VALUE_DOUBLE");
      uint64_t bits = ReadUnsignedLong(p, arg, /*fill_on_right=*/true);
      p += width;
      out->kind = (bits == 0) ? EncodedValue::Kind::kZero : EncodedValue::Kind::kDouble;
      out->bits = bits;
      break;
    }
    case kValueString: {
      if (width > 4 || !need(width)) return Fail(error, "bad VALUE_STRING");
      uint32_t idx = ReadUnsignedInt(p, arg, /*fill_on_right=*/false);
      p += width;
      if (idx >= string_ids_size) {
        return Fail(error, "encoded_value string_idx out of range");
      }
      out->kind = EncodedValue::Kind::kString;
      out->idx = idx;
      break;
    }
    case kValueType: {
      if (width > 4 || !need(width)) return Fail(error, "bad VALUE_TYPE");
      uint32_t idx = ReadUnsignedInt(p, arg, /*fill_on_right=*/false);
      p += width;
      if (idx >= type_ids_size) {
        return Fail(error, "encoded_value type_idx out of range");
      }
      out->kind = EncodedValue::Kind::kType;
      out->idx = idx;
      break;
    }
    case kValueNull: {
      // 对照 EncodedArrayValueIterator：kNull/kBoolean 无 payload（width=0）。
      out->kind = EncodedValue::Kind::kNull;
      break;
    }
    case kValueBoolean: {
      out->kind = EncodedValue::Kind::kBoolean;
      out->int_value = (arg != 0) ? 1 : 0;
      break;
    }
    default:
      return Fail(error, "unsupported encoded_value type");
  }
  *pp = p;
  return true;
}

bool DexFile::Impl::DecodeEncodedArray(uint32_t off,
                                       std::vector<EncodedValue>* out,
                                       std::string* error) const {
  const uint8_t* p = data + off;
  const uint8_t* end = data + file_size;
  uint32_t size = 0;
  if (!ReadUleb128(&p, end, &size)) {
    return Fail(error, "truncated encoded_array size");
  }
  out->reserve(size);
  for (uint32_t i = 0; i < size; ++i) {
    EncodedValue v;
    if (!DecodeEncodedValue(&p, end, &v, error)) {
      return false;
    }
    out->push_back(v);
  }
  return true;
}

bool DexFile::Impl::DecodeCodeItem(uint32_t code_off, CodeItem* out,
                                   std::string* error) const {
  // CodeItem 4 字节对齐（standard_dex_file.h: kAlignment = 4）。
  if ((code_off & 3) != 0) {
    return Fail(error, "unaligned code_off");
  }
  if (!InBounds(code_off, kCodeItemHeaderSize, file_size)) {
    return Fail(error, "code_item out of bounds");
  }
  const uint8_t* p = data + code_off;
  out->registers_size = Rd16(p);
  out->ins_size = Rd16(p + 2);
  out->outs_size = Rd16(p + 4);
  out->tries_size = Rd16(p + 6);
  out->debug_info_off = Rd32(p + 8);
  out->insns_size = Rd32(p + 12);
  if (out->debug_info_off != 0 && out->debug_info_off >= file_size) {
    return Fail(error, "debug_info_off out of bounds");
  }
  uint64_t insns_bytes = static_cast<uint64_t>(out->insns_size) * 2u;
  if (!InBounds(code_off + kCodeItemHeaderSize, insns_bytes, file_size)) {
    return Fail(error, "code_item insns out of bounds");
  }
  out->insns = reinterpret_cast<const uint16_t*>(data + code_off + kCodeItemHeaderSize);

  if (out->tries_size == 0) {
    out->tries_off = 0;
    return true;
  }
  // tries_size > 0：try_item 数组紧随 insns 之后按 4 字节对齐
  // （对照 dex_file-inl.h GetTryItems = RoundUp(insns_end, 4)）。
  uint64_t insns_end = static_cast<uint64_t>(code_off) + kCodeItemHeaderSize + insns_bytes;
  uint64_t tries_start = (insns_end + 3u) & ~static_cast<uint64_t>(3u);
  uint64_t tries_bytes = static_cast<uint64_t>(out->tries_size) * kTryItemSize;
  if (!InBounds(static_cast<uint32_t>(tries_start), tries_bytes, file_size)) {
    return Fail(error, "code_item try_items out of bounds");
  }
  out->tries_off = static_cast<uint32_t>(tries_start);
  return true;
}

const CodeItem* DexFile::Impl::GetCodeItemCached(uint32_t code_off,
                                                std::string* error) const {
  auto it = code_items.find(code_off);
  if (it != code_items.end()) {
    return &it->second;
  }
  CodeItem item;
  if (!DecodeCodeItem(code_off, &item, error)) {
    return nullptr;
  }
  return &code_items.emplace(code_off, item).first->second;
}

// ---- DexFile 公共接口 ----

// static
std::unique_ptr<DexFile> DexFile::Open(const uint8_t* data, size_t size,
                                       std::string* error) {
  if (!VerifyDexFile(data, size, error)) {
    return nullptr;
  }
  auto dex = std::unique_ptr<DexFile>(new DexFile());
  dex->data_ = data;
  dex->size_ = size;
  dex->impl_ = std::make_unique<Impl>();
  Impl* impl = dex->impl_.get();
  impl->data = data;
  impl->file_size = Rd32(data + kOffFileSize);
  impl->checksum = Rd32(data + kOffChecksum);
  impl->string_ids_size = Rd32(data + kOffStringIdsSize);
  impl->string_ids_off = Rd32(data + kOffStringIdsOff);
  impl->type_ids_size = Rd32(data + kOffTypeIdsSize);
  impl->type_ids_off = Rd32(data + kOffTypeIdsOff);
  impl->proto_ids_size = Rd32(data + kOffProtoIdsSize);
  impl->proto_ids_off = Rd32(data + kOffProtoIdsOff);
  impl->field_ids_size = Rd32(data + kOffFieldIdsSize);
  impl->field_ids_off = Rd32(data + kOffFieldIdsOff);
  impl->method_ids_size = Rd32(data + kOffMethodIdsSize);
  impl->method_ids_off = Rd32(data + kOffMethodIdsOff);
  impl->class_defs_size = Rd32(data + kOffClassDefsSize);
  impl->class_defs_off = Rd32(data + kOffClassDefsOff);
  if (!impl->ParseTables(error)) {
    return nullptr;
  }
  return dex;
}

DexFile::~DexFile() = default;

uint32_t DexFile::checksum() const {
  return impl_->checksum;
}

const std::string& DexFile::GetString(uint32_t idx) const {
  return impl_->GetStringCached(idx);
}

std::string_view DexFile::GetTypeDescriptor(uint32_t idx) const {
  if (idx >= impl_->type_ids_size) {
    return {};
  }
  uint32_t string_idx = Rd32(impl_->data + impl_->type_ids_off + idx * 4);
  return impl_->GetStringCached(string_idx);
}

uint32_t DexFile::NumStringIds() const {
  return impl_->string_ids_size;
}

uint32_t DexFile::NumTypeIds() const {
  return impl_->type_ids_size;
}

uint32_t DexFile::NumFieldIds() const {
  return impl_->field_ids_size;
}

uint32_t DexFile::NumMethodIds() const {
  return impl_->method_ids_size;
}

const ProtoId& DexFile::GetProto(uint32_t idx) const {
  if (idx >= impl_->proto_ids.size()) {
    return kEmptyProto;
  }
  return impl_->proto_ids[idx];
}

const FieldId& DexFile::GetFieldId(uint32_t idx) const {
  if (idx >= impl_->field_ids.size()) {
    return kEmptyField;
  }
  return impl_->field_ids[idx];
}

const MethodId& DexFile::GetMethodId(uint32_t idx) const {
  if (idx >= impl_->method_ids.size()) {
    return kEmptyMethod;
  }
  return impl_->method_ids[idx];
}

uint32_t DexFile::GetProtoParamCount(uint32_t proto_idx) const {
  if (proto_idx >= impl_->proto_ids.size()) {
    return 0;
  }
  return impl_->TypeListCount(impl_->proto_ids[proto_idx].parameters_off);
}

uint32_t DexFile::GetProtoParamTypeIdx(uint32_t proto_idx, uint32_t i) const {
  if (proto_idx >= impl_->proto_ids.size()) {
    return kNoIndex;
  }
  return impl_->TypeListTypeIdx(impl_->proto_ids[proto_idx].parameters_off, i);
}

uint32_t DexFile::NumClassDefs() const {
  return impl_->class_defs_size;
}

const ClassDef& DexFile::GetClassDef(uint32_t i) const {
  if (i >= impl_->class_defs.size()) {
    return kEmptyClassDef;
  }
  return impl_->class_defs[i];
}

int DexFile::FindClassDefIndexByDescriptor(std::string_view descriptor) const {
  Impl* impl = impl_.get();
  if (!impl->descriptor_index_ready) {
    for (uint32_t i = 0; i < impl->class_defs.size(); ++i) {
      std::string_view desc = GetTypeDescriptor(impl->class_defs[i].class_idx);
      // 首个定义优先（与 ART FindClassDef 的二分查找一致：class_def 按
      // class_idx 升序，重复项取第一个）。
      impl->descriptor_index.emplace(desc, i);
    }
    impl->descriptor_index_ready = true;
  }
  auto it = impl->descriptor_index.find(descriptor);
  if (it == impl->descriptor_index.end()) {
    return -1;
  }
  return static_cast<int>(it->second);
}

uint32_t DexFile::GetInterfaceCount(uint32_t class_def_idx) const {
  if (class_def_idx >= impl_->class_defs.size()) {
    return 0;
  }
  return impl_->TypeListCount(impl_->class_defs[class_def_idx].interfaces_off);
}

uint32_t DexFile::GetInterfaceTypeIdx(uint32_t class_def_idx, uint32_t i) const {
  if (class_def_idx >= impl_->class_defs.size()) {
    return kNoIndex;
  }
  return impl_->TypeListTypeIdx(impl_->class_defs[class_def_idx].interfaces_off, i);
}

bool DexFile::GetClassData(uint32_t class_def_idx, ClassData* out,
                           std::string* error) const {
  if (out == nullptr) {
    return Fail(error, "null ClassData out");
  }
  *out = ClassData();
  if (class_def_idx >= impl_->class_defs.size()) {
    return Fail(error, "class_def idx out of range");
  }
  uint32_t off = impl_->class_defs[class_def_idx].class_data_off;
  if (off == 0) {
    return true;  // 无 class_data
  }
  return impl_->DecodeClassData(off, out, error);
}

bool DexFile::GetStaticValues(uint32_t class_def_idx,
                              std::vector<EncodedValue>* out,
                              std::string* error) const {
  if (out == nullptr) {
    return Fail(error, "null EncodedValue out");
  }
  out->clear();
  if (class_def_idx >= impl_->class_defs.size()) {
    return Fail(error, "class_def idx out of range");
  }
  uint32_t off = impl_->class_defs[class_def_idx].static_values_off;
  if (off == 0) {
    return true;  // 无静态值
  }
  return impl_->DecodeEncodedArray(off, out, error);
}

const CodeItem* DexFile::GetCodeItem(uint32_t code_off,
                                     std::string* error) const {
  if (code_off == 0) {
    return nullptr;  // abstract/native：无 code_item，不算错误
  }
  return impl_->GetCodeItemCached(code_off, error);
}

}  // namespace dexvm::dex
