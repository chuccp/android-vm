// dexvm_dex 公共接口：DEX 文件解析（语义对照 third_party/art/libdexfile）
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace dexvm::dex {

struct FieldId {
  uint32_t class_idx;
  uint32_t type_idx;
  uint32_t name_idx;
};

struct MethodId {
  uint32_t class_idx;
  uint32_t proto_idx;
  uint32_t name_idx;
};

struct ProtoId {
  uint32_t shorty_idx;
  uint32_t return_type_idx;
  uint32_t parameters_off;  // TypeList；0 表示无参数
};

struct ClassDef {
  uint32_t class_idx;
  uint32_t access_flags;
  uint32_t superclass_idx;  // 0xffffffff 无父类
  uint32_t interfaces_off;  // TypeList；0 表示无
  uint32_t source_file_idx;
  uint32_t annotations_off;
  uint32_t class_data_off;   // 0 表示无 class_data
  uint32_t static_values_off;  // EncodedArray；0 表示无
};

struct EncodedField {
  uint32_t field_idx;
  uint32_t access_flags;
};

struct EncodedMethod {
  uint32_t method_idx;
  uint32_t access_flags;
  uint32_t code_off;  // 0 = abstract/native
};

struct ClassData {
  std::vector<EncodedField> static_fields;
  std::vector<EncodedField> instance_fields;
  std::vector<EncodedMethod> direct_methods;
  std::vector<EncodedMethod> virtual_methods;
};

struct CodeItem {
  uint32_t registers_size;
  uint32_t ins_size;
  uint32_t outs_size;
  uint32_t tries_size;
  uint32_t debug_info_off;
  uint32_t insns_size;    // code units
  const uint16_t* insns;  // 指向 DexFile 内部字节
  uint32_t tries_off;     // try_item 数组偏移；0 = 无
};

// EncodedArray 解码后的静态值（static_values_off）
struct EncodedValue {
  enum class Kind : uint8_t {
    kZero,      // VALUE_BYTE/SHORT/CHAR/INT/FLOAT/LONG/DOUBLE 的 0 值
    kInt,       // 存 int64（含 byte/short/char/int）
    kLong,
    kFloat,
    kDouble,
    kString,    // 存 string_idx
    kType,      // 存 type_idx
    kNull,
    kBoolean,   // 存 int64 0/1
  };
  Kind kind = Kind::kZero;
  int64_t int_value = 0;
  uint64_t bits = 0;  // float/double 位型
  uint32_t idx = 0;   // string_idx / type_idx
};

class DexFile {
 public:
  // data 在 DexFile 生命周期内必须保持有效（不拷贝、不接管）。
  // 校验失败返回 nullptr 并写 error。
  static std::unique_ptr<DexFile> Open(const uint8_t* data, size_t size,
                                      std::string* error);
  ~DexFile();

  DexFile(const DexFile&) = delete;
  DexFile& operator=(const DexFile&) = delete;

  const uint8_t* data() const { return data_; }
  size_t size() const { return size_; }
  uint32_t checksum() const;

  // ---- 字符串/类型 ----
  // MUTF-8 解码为 std::string（内部缓存）。
  const std::string& GetString(uint32_t idx) const;
  std::string_view GetTypeDescriptor(uint32_t idx) const;

  uint32_t NumStringIds() const;
  uint32_t NumTypeIds() const;
  uint32_t NumFieldIds() const;
  uint32_t NumMethodIds() const;

  // ---- id 表 ----
  const ProtoId& GetProto(uint32_t idx) const;
  const FieldId& GetFieldId(uint32_t idx) const;
  const MethodId& GetMethodId(uint32_t idx) const;

  // proto 参数（parameters_off 指向的 TypeList）
  uint32_t GetProtoParamCount(uint32_t proto_idx) const;
  uint32_t GetProtoParamTypeIdx(uint32_t proto_idx, uint32_t i) const;

  // ---- 类 ----
  uint32_t NumClassDefs() const;
  const ClassDef& GetClassDef(uint32_t i) const;
  // 按 descriptor（如 "Lfoo/Bar;"）查找；找不到返回 -1
  int FindClassDefIndexByDescriptor(std::string_view descriptor) const;

  uint32_t GetInterfaceCount(uint32_t class_def_idx) const;
  uint32_t GetInterfaceTypeIdx(uint32_t class_def_idx, uint32_t i) const;

  // class_data_item 解码（失败返回 false 并写 error）
  bool GetClassData(uint32_t class_def_idx, ClassData* out, std::string* error) const;

  // static_values_off 的 EncodedArray 解码（无静态值时 out 为空、返回 true）
  bool GetStaticValues(uint32_t class_def_idx, std::vector<EncodedValue>* out,
                       std::string* error) const;

  // ---- 代码 ----
  // code_off == 0 返回 nullptr；非法偏移返回 nullptr 并写 error
  const CodeItem* GetCodeItem(uint32_t code_off, std::string* error) const;

 private:
  DexFile() = default;

  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// 指令宽度（code units）。insns 指向一条指令首字。
// switch/fill-array-data 的 payload 指令宽度按 payload 头计算。
// opcode 表语义对照 third_party/art/libdexfile（instruction-list）。
uint32_t InsnWidth(const uint16_t* insns);

}  // namespace dexvm::dex
