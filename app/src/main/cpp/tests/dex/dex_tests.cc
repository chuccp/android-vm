// DEX 解析模块宿主测试（不引 gtest，简单断言宏）。
// 被测对象：dex/*.cc（DexFile::Impl、leb128/mutf8/opcodes/dex_verify）。
// 输入：
//   1) tests/out/fixture.dex（tools/make_fixture.sh 由 javac+d8 生成）；
//   2) 手工合成的最小 DEX（覆盖 d8 不会生成的 VALUE_TYPE 宽度语义与错误路径）。
// 运行：dex_tests <fixture.dex 路径>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "dex/dex_file.h"
#include "dex/leb128.h"
#include "dex/opcodes.h"

namespace {

using dexvm::dex::ClassData;
using dexvm::dex::CodeItem;
using dexvm::dex::DexFile;
using dexvm::dex::EncodedValue;
using dexvm::dex::InsnWidth;

// ---- 简单断言宏 ----
const char* g_test = "";
int g_checks = 0;
int g_failures = 0;

void ExpectTrue(bool cond, const char* expr, int line) {
  ++g_checks;
  if (!cond) {
    ++g_failures;
    std::printf("[FAIL] %s:%d: %s\n", g_test, line, expr);
  }
}

void ExpectEq(uint64_t a, uint64_t b, const char* expr, int line) {
  ++g_checks;
  if (a != b) {
    ++g_failures;
    std::printf("[FAIL] %s:%d: %s (0x%llx != 0x%llx)\n", g_test, line, expr,
                static_cast<unsigned long long>(a),
                static_cast<unsigned long long>(b));
  }
}

void ExpectStr(const std::string& a, const std::string& b, const char* expr,
               int line) {
  ++g_checks;
  if (a != b) {
    ++g_failures;
    std::printf("[FAIL] %s:%d: %s (size %zu != %zu)\n", g_test, line, expr,
                a.size(), b.size());
  }
}

#define EXPECT_TRUE(c) ExpectTrue((c), #c, __LINE__)
#define EXPECT_EQ(a, b) \
  ExpectEq(static_cast<uint64_t>(a), static_cast<uint64_t>(b), #a " == " #b, __LINE__)
#define EXPECT_STR(a, b) ExpectStr((a), (b), #a " == " #b, __LINE__)

// ---- 工具 ----
uint16_t Rd16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

bool LoadFile(const char* path, std::vector<uint8_t>* out) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    return false;
  }
  std::streamsize n = f.tellg();
  if (n < 0) {
    return false;
  }
  out->resize(static_cast<size_t>(n));
  f.seekg(0);
  return n == 0 || static_cast<bool>(f.read(reinterpret_cast<char*>(out->data()), n));
}

// 在 class 的 class_data 中按 (name, 参数个数) 找方法。
bool FindMethod(const DexFile& dex, int class_def_idx, const std::string& name,
                uint32_t param_count, uint32_t* method_idx, uint32_t* code_off,
                std::string* error) {
  ClassData cd;
  if (!dex.GetClassData(static_cast<uint32_t>(class_def_idx), &cd, error)) {
    return false;
  }
  auto check = [&](const dexvm::dex::EncodedMethod& m) -> bool {
    const dexvm::dex::MethodId& id = dex.GetMethodId(m.method_idx);
    if (dex.GetString(id.name_idx) != name) {
      return false;
    }
    return dex.GetProtoParamCount(id.proto_idx) == param_count;
  };
  for (const auto& m : cd.direct_methods) {
    if (check(m)) {
      *method_idx = m.method_idx;
      *code_off = m.code_off;
      return true;
    }
  }
  for (const auto& m : cd.virtual_methods) {
    if (check(m)) {
      *method_idx = m.method_idx;
      *code_off = m.code_off;
      return true;
    }
  }
  return false;
}

// 用 InsnWidth 线性走完 insns，要求恰好覆盖 insns_size（对照 d8 真实字节码）。
bool WalkInsns(const CodeItem* code) {
  uint32_t units = 0;
  while (units < code->insns_size) {
    uint32_t w = InsnWidth(code->insns + units);
    if (w == 0) {
      return false;
    }
    units += w;
    if (units > code->insns_size) {
      return false;
    }
  }
  return units == code->insns_size;
}

// ---- 合成 DEX ----
// 覆盖 d8 不生成的 VALUE_TYPE 与 encoded_value 宽度语义、class_data 截断错误。
// 布局：header | string_ids | type_ids | class_defs | string_data |
//       0x80(截断 class_data) | encoded_array | (pad) | map_list
struct SynthDex {
  std::vector<uint8_t> bytes;
  uint32_t static_values_off = 0;
  uint32_t broken_class_data_off = 0;
  uint32_t class_defs_off = 0;
  uint32_t trunc_values_off = 0;  // 尾部 slack：{size=1, 截断的 DOUBLE 值头}
};

void Put16(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
}
void Put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back(static_cast<uint8_t>(x));
  v.push_back(static_cast<uint8_t>(x >> 8));
  v.push_back(static_cast<uint8_t>(x >> 16));
  v.push_back(static_cast<uint8_t>(x >> 24));
}
void PutUleb(std::vector<uint8_t>& v, uint32_t x) {
  uint8_t buf[5];
  uint8_t* end = dexvm::dex::WriteUleb128(buf, x);
  v.insert(v.end(), buf, end);
}

SynthDex BuildSynthDex() {
  SynthDex s;
  const std::vector<std::string> kStrings = {
      "Lfixture/Synth;", "syn", "Lfixture/Plain;"};
  const uint32_t kNumTypes = 2;

  // encoded_array：13 个值（含 TYPE/BYTE/SHORT/CHAR/INT/LONG/FLOAT(窄)/DOUBLE/
  // STRING/NULL/BOOLEAN/0 值）。
  std::vector<uint8_t> ea;
  PutUleb(ea, 13);
  ea.push_back(0x18); ea.push_back(0x00);                       // [0] TYPE type_idx=0
  ea.push_back(0x00); ea.push_back(0xFB);                       // [1] BYTE -5
  ea.push_back(0x22); ea.push_back(0xD4); ea.push_back(0xFE);   // [2] SHORT -300
  ea.push_back(0x23); ea.push_back(0x41); ea.push_back(0x00);   // [3] CHAR 'A'
  ea.push_back(0x44);                                           // [4] INT arg=2
  ea.push_back(0x40); ea.push_back(0xE2); ea.push_back(0x01);   //     123456
  ea.push_back(0xE6);                                           // [5] LONG arg=7
  {
    uint64_t l = 0x0123456789ABCDEFull;
    for (int i = 0; i < 8; ++i) ea.push_back(static_cast<uint8_t>(l >> (8 * i)));
  }
  ea.push_back(0x30); ea.push_back(0xC0); ea.push_back(0x3F);   // [6] FLOAT arg=1
                                                                //     bits 0x3FC00000
  ea.push_back(0xF1);                                           // [7] DOUBLE arg=7
  {
    uint64_t d = 0xC002000000000000ull;
    for (int i = 0; i < 8; ++i) ea.push_back(static_cast<uint8_t>(d >> (8 * i)));
  }
  ea.push_back(0x17); ea.push_back(0x01);                       // [8] STRING idx=1
  ea.push_back(0x1E);                                           // [9] NULL
  ea.push_back(0x3F);                                           // [10] BOOLEAN true
  ea.push_back(0x1F);                                           // [11] BOOLEAN false
  ea.push_back(0x04); ea.push_back(0x00);                       // [12] INT 0 -> kZero

  // 各段 blob。
  std::vector<std::vector<uint8_t>> sdata;
  for (const std::string& str : kStrings) {
    std::vector<uint8_t> b;
    PutUleb(b, static_cast<uint32_t>(str.size()));
    b.insert(b.end(), str.begin(), str.end());
    b.push_back(0);
    sdata.push_back(std::move(b));
  }

  const uint32_t kHeaderSize = 0x70;
  const uint32_t kNumStrings = static_cast<uint32_t>(kStrings.size());
  const uint32_t kNumClasses = 2;
  const uint32_t kNumMapItems = 6;

  uint32_t off = kHeaderSize;
  const uint32_t string_ids_off = off;
  off += kNumStrings * 4;
  const uint32_t type_ids_off = off;
  off += kNumTypes * 4;
  const uint32_t class_defs_off = off;
  off += kNumClasses * 32;
  uint32_t sdata_off[8] = {};
  for (uint32_t i = 0; i < kNumStrings; ++i) {
    sdata_off[i] = off;
    off += static_cast<uint32_t>(sdata[i].size());
  }
  const uint32_t broken_off = off;
  off += 1;  // 一个 0x80（ULEB 续位）字节：截断的 class_data_item
  const uint32_t ea_off = off;
  off += static_cast<uint32_t>(ea.size());
  off = (off + 3u) & ~3u;
  const uint32_t map_off = off;
  off += 4 + kNumMapItems * 12;
  off += 2;  // 尾部 slack：{uleb size=1, 截断的 DOUBLE 值头}
  const uint32_t file_size = off;

  std::vector<uint8_t>& v = s.bytes;
  v.reserve(file_size);

  // header_item（0x70）。
  const char magic[8] = {'d', 'e', 'x', '\n', '0', '3', '5', '\0'};
  v.insert(v.end(), magic, magic + 8);
  Put32(v, 0);             // checksum（校验不要求）
  v.insert(v.end(), 20, 0);  // signature
  Put32(v, file_size);
  Put32(v, kHeaderSize);
  Put32(v, 0x12345678);    // endian_tag
  Put32(v, 0);             // link_size
  Put32(v, 0);             // link_off
  Put32(v, map_off);
  Put32(v, kNumStrings);
  Put32(v, string_ids_off);
  Put32(v, kNumTypes);
  Put32(v, type_ids_off);
  Put32(v, 0); Put32(v, 0);  // proto_ids
  Put32(v, 0); Put32(v, 0);  // field_ids
  Put32(v, 0); Put32(v, 0);  // method_ids
  Put32(v, kNumClasses);
  Put32(v, class_defs_off);
  // header 末两字段（对照 dex_file_structs.h Header: data_size_/data_off_）。
  Put32(v, file_size - sdata_off[0]);
  Put32(v, sdata_off[0]);
  EXPECT_EQ(v.size(), kHeaderSize);

  // string_ids / type_ids。
  for (uint32_t i = 0; i < kNumStrings; ++i) Put32(v, sdata_off[i]);
  Put32(v, 0);  // type 0 -> string 0 "Lfixture/Synth;"
  Put32(v, 2);  // type 1 -> string 2 "Lfixture/Plain;"

  // class_defs：
  //   0: static_values -> encoded_array；class_data_off -> 截断字节
  //   1: 无 class_data / 无 static_values
  Put32(v, 0); Put32(v, 0x11); Put32(v, 0xffffffffu); Put32(v, 0);
  Put32(v, 0xffffffffu); Put32(v, 0);
  Put32(v, broken_off);
  Put32(v, ea_off);
  Put32(v, 1); Put32(v, 0x11); Put32(v, 0xffffffffu); Put32(v, 0);
  Put32(v, 0xffffffffu); Put32(v, 0); Put32(v, 0); Put32(v, 0);

  // data。
  for (const auto& b : sdata) v.insert(v.end(), b.begin(), b.end());
  v.push_back(0x80);
  v.insert(v.end(), ea.begin(), ea.end());
  while (v.size() < map_off) v.push_back(0);

  // map_list（header/string_id/type_id/class_def/encoded_array/map_list）。
  Put32(v, kNumMapItems);
  auto map_item = [&](uint16_t type, uint32_t size, uint32_t item_off) {
    Put16(v, type);
    Put16(v, 0);
    Put32(v, size);
    Put32(v, item_off);
  };
  map_item(0x0000, 1, 0);
  map_item(0x0001, kNumStrings, string_ids_off);
  map_item(0x0002, kNumTypes, type_ids_off);
  map_item(0x0006, kNumClasses, class_defs_off);
  map_item(0x2005, 1, ea_off);
  map_item(0x1000, 1, map_off);

  // 尾部 slack：截断 encoded_array 测试用（{uleb size=1, DOUBLE 值头}，无 payload）。
  const uint32_t trunc_off = static_cast<uint32_t>(v.size());
  v.push_back(0x01);   // encoded_array size = 1
  v.push_back(0xF1);   // VALUE_DOUBLE arg=7：需要 8 字节 payload，文件在此结束

  EXPECT_EQ(v.size(), file_size);
  s.static_values_off = ea_off;
  s.broken_class_data_off = broken_off;
  s.class_defs_off = class_defs_off;
  s.trunc_values_off = trunc_off;
  return s;
}

// ---- 测试（fixture.dex）----

void TestOpenValid(const std::vector<uint8_t>& fx) {
  std::string error = "sentinel";
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  EXPECT_STR(error, "sentinel");  // 成功不写 error
  if (!dex) return;
  EXPECT_TRUE(dex->data() == fx.data());
  EXPECT_EQ(dex->size(), fx.size());
  EXPECT_EQ(dex->checksum(), Rd32(fx.data() + 0x08));
  EXPECT_TRUE(dex->NumStringIds() > 0);
  EXPECT_TRUE(dex->NumTypeIds() > 0);
  EXPECT_TRUE(dex->NumFieldIds() > 0);
  EXPECT_TRUE(dex->NumMethodIds() > 0);
  EXPECT_TRUE(dex->NumClassDefs() == 3);
  // 三个 fixture 类。
  EXPECT_TRUE(dex->FindClassDefIndexByDescriptor("Lfixture/Calc;") >= 0);
  EXPECT_TRUE(dex->FindClassDefIndexByDescriptor("Lfixture/Operand;") >= 0);
  EXPECT_TRUE(dex->FindClassDefIndexByDescriptor("Lfixture/Shape;") >= 0);
  // 越界访问不 crash。
  EXPECT_STR(dex->GetString(0x7fffffff), "");
  EXPECT_TRUE(dex->GetTypeDescriptor(0x7fffffff).empty());
  EXPECT_EQ(dex->GetProto(0x7fffffff).shorty_idx, 0);
  EXPECT_EQ(dex->GetFieldId(0x7fffffff).name_idx, 0);
  EXPECT_EQ(dex->GetMethodId(0x7fffffff).name_idx, 0);
  EXPECT_EQ(dex->GetClassDef(0x7fffffff).class_idx, 0);
  EXPECT_EQ(dex->GetInterfaceCount(0x7fffffff), 0);
  EXPECT_EQ(dex->GetProtoParamCount(0x7fffffff), 0);
}

void TestOpenRejects(const std::vector<uint8_t>& fx) {
  std::string error;
  // 坏 magic。
  {
    std::vector<uint8_t> bad = fx;
    bad[1] = 'X';
    error.clear();
    EXPECT_TRUE(DexFile::Open(bad.data(), bad.size(), &error) == nullptr);
    EXPECT_TRUE(!error.empty());
  }
  // 截断（buffer < file_size）。
  {
    error.clear();
    EXPECT_TRUE(DexFile::Open(fx.data(), fx.size() - 1, &error) == nullptr);
    EXPECT_TRUE(!error.empty());
    error.clear();
    EXPECT_TRUE(DexFile::Open(fx.data(), 4, &error) == nullptr);
    EXPECT_TRUE(!error.empty());
  }
  // 坏 endian。
  {
    std::vector<uint8_t> bad = fx;
    bad[0x28] = 0;
    error.clear();
    EXPECT_TRUE(DexFile::Open(bad.data(), bad.size(), &error) == nullptr);
    EXPECT_TRUE(!error.empty());
  }
  // 坏 header_size。
  {
    std::vector<uint8_t> bad = fx;
    bad[0x24] = 0;
    error.clear();
    EXPECT_TRUE(DexFile::Open(bad.data(), bad.size(), &error) == nullptr);
    EXPECT_TRUE(!error.empty());
  }
  // 不支持的版本。
  {
    std::vector<uint8_t> bad = fx;
    bad[4] = '9';
    error.clear();
    EXPECT_TRUE(DexFile::Open(bad.data(), bad.size(), &error) == nullptr);
    EXPECT_TRUE(!error.empty());
  }
  // null data。
  {
    error.clear();
    EXPECT_TRUE(DexFile::Open(nullptr, 100, &error) == nullptr);
    EXPECT_TRUE(!error.empty());
  }
}

void TestStrings(const std::vector<uint8_t>& fx) {
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  // 内嵌 \0 的字符串（MUTF-8 C0 80）。
  std::string nul_str("a\0b", 3);
  int nul_hits = 0;
  int str_hits = 0;
  int hello_hits = 0;
  for (uint32_t i = 0; i < dex->NumStringIds(); ++i) {
    const std::string& s = dex->GetString(i);
    if (s == nul_str) ++nul_hits;
    if (s == "hi") ++str_hits;
    if (s == "hello, ") ++hello_hits;
    // 二次读取返回同一缓存。
    EXPECT_TRUE(&dex->GetString(i) == &s);
  }
  EXPECT_EQ(nul_hits, 1);
  EXPECT_EQ(str_hits, 1);
  EXPECT_EQ(hello_hits, 1);
  // 类型描述符。
  int calc_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Calc;");
  EXPECT_TRUE(calc_idx >= 0);
  EXPECT_STR(std::string(dex->GetTypeDescriptor(
                 dex->GetClassDef(static_cast<uint32_t>(calc_idx)).class_idx)),
             "Lfixture/Calc;");
}

void TestProtoParams(const std::vector<uint8_t>& fx) {
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  int calc_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Calc;");
  EXPECT_TRUE(calc_idx >= 0);
  // mix(int, String, double, Calc) -> (I Ljava/lang/String; D Lfixture/Calc;)J
  uint32_t mi = 0, code_off = 0;
  EXPECT_TRUE(FindMethod(*dex, calc_idx, "mix", 4, &mi, &code_off, &error));
  const dexvm::dex::MethodId& id = dex->GetMethodId(mi);
  uint32_t proto = id.proto_idx;
  EXPECT_EQ(dex->GetProtoParamCount(proto), 4);
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(proto, 0))), "I");
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(proto, 1))),
             "Ljava/lang/String;");
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(proto, 2))), "D");
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(proto, 3))),
             "Lfixture/Calc;");
  EXPECT_EQ(dex->GetProtoParamTypeIdx(proto, 4), 0xffffffffu);  // 越界 -> NO_INDEX
  // 返回类型。
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProto(proto).return_type_idx)), "J");
  // 无参 proto：area()I。
  uint32_t ai = 0;
  EXPECT_TRUE(FindMethod(*dex, calc_idx, "area", 0, &ai, &code_off, &error));
  EXPECT_EQ(dex->GetProtoParamCount(dex->GetMethodId(ai).proto_idx), 0);
  // <init>(II)V 两个参数都是 I。
  uint32_t ci = 0;
  EXPECT_TRUE(FindMethod(*dex, calc_idx, "<init>", 2, &ci, &code_off, &error));
  uint32_t cproto = dex->GetMethodId(ci).proto_idx;
  EXPECT_EQ(dex->GetProtoParamCount(cproto), 2);
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(cproto, 0))), "I");
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(cproto, 1))), "I");
}

void TestClassData(const std::vector<uint8_t>& fx) {
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  int calc_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Calc;");
  EXPECT_TRUE(calc_idx >= 0);
  ClassData cd;
  EXPECT_TRUE(dex->GetClassData(static_cast<uint32_t>(calc_idx), &cd, &error));
  // 四段：15 静态常量 / 2 实例字段 / 9 direct（<clinit>+2<init>+6 static）/ 2 virtual。
  EXPECT_EQ(cd.static_fields.size(), 15);
  EXPECT_EQ(cd.instance_fields.size(), 2);
  EXPECT_EQ(cd.direct_methods.size(), 9);
  EXPECT_EQ(cd.virtual_methods.size(), 2);

  // field_idx_diff/method_idx_diff 累积：绝对 idx 严格递增且在表内。
  auto check_fields = [&](const std::vector<dexvm::dex::EncodedField>& v) {
    uint32_t prev = 0;
    bool first = true;
    for (const auto& f : v) {
      EXPECT_TRUE(f.field_idx < dex->NumFieldIds());
      EXPECT_TRUE(first || f.field_idx > prev);
      prev = f.field_idx;
      first = false;
      EXPECT_TRUE(f.access_flags != 0);
    }
  };
  auto check_methods = [&](const std::vector<dexvm::dex::EncodedMethod>& v) {
    uint32_t prev = 0;
    bool first = true;
    for (const auto& m : v) {
      EXPECT_TRUE(m.method_idx < dex->NumMethodIds());
      EXPECT_TRUE(first || m.method_idx > prev);
      prev = m.method_idx;
      first = false;
    }
  };
  check_fields(cd.static_fields);
  check_fields(cd.instance_fields);
  check_methods(cd.direct_methods);
  check_methods(cd.virtual_methods);

  // 静态字段按名字（class_data 顺序 = field_idx 升序 = 名字序）。
  const char* kStaticNames[] = {"AFTER", "B",    "C",      "CLS",   "D",
                                "F",     "I",    "L",      "NUL",   "NULSTR",
                                "S",     "STR",  "ZERO",   "Z_FALSE", "Z_TRUE"};
  for (size_t i = 0; i < 15; ++i) {
    const auto& f = cd.static_fields[i];
    EXPECT_STR(dex->GetString(dex->GetFieldId(f.field_idx).name_idx), kStaticNames[i]);
    EXPECT_TRUE((f.access_flags & 0x8) != 0);  // ACC_STATIC
  }
  // 实例字段：height, width（名字序）。
  EXPECT_STR(dex->GetString(dex->GetFieldId(cd.instance_fields[0].field_idx).name_idx), "height");
  EXPECT_STR(dex->GetString(dex->GetFieldId(cd.instance_fields[1].field_idx).name_idx), "width");

  // direct：<clinit> <init>()V <init>(II)V add greet mix pick safeDiv table（名字序）。
  const char* kDirectNames[] = {"<clinit>", "<init>",  "<init>", "add",    "greet",
                                "mix",      "pick",    "safeDiv", "table"};
  for (size_t i = 0; i < 9; ++i) {
    const auto& m = cd.direct_methods[i];
    EXPECT_STR(dex->GetString(dex->GetMethodId(m.method_idx).name_idx), kDirectNames[i]);
    EXPECT_TRUE((m.access_flags & 0x8) != 0 || i < 3);  // static 或构造器/类初始化
  }
  // virtual：area, mul。
  EXPECT_STR(dex->GetString(dex->GetMethodId(cd.virtual_methods[0].method_idx).name_idx), "area");
  EXPECT_STR(dex->GetString(dex->GetMethodId(cd.virtual_methods[1].method_idx).name_idx), "mul");

  // 越界 class_def idx。
  EXPECT_TRUE(!dex->GetClassData(9999, &cd, &error));
  EXPECT_TRUE(!error.empty());
  error.clear();
  // 接口列表：Calc implements Shape。
  EXPECT_EQ(dex->GetInterfaceCount(static_cast<uint32_t>(calc_idx)), 1);
  EXPECT_STR(std::string(dex->GetTypeDescriptor(
                 dex->GetInterfaceTypeIdx(static_cast<uint32_t>(calc_idx), 0))),
             "Lfixture/Shape;");
  // Operand 无接口。
  int op_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Operand;");
  EXPECT_TRUE(op_idx >= 0);
  EXPECT_EQ(dex->GetInterfaceCount(static_cast<uint32_t>(op_idx)), 0);
  // 继承：Calc 的 superclass 是 Operand。
  EXPECT_STR(std::string(dex->GetTypeDescriptor(
                 dex->GetClassDef(static_cast<uint32_t>(calc_idx)).superclass_idx)),
             "Lfixture/Operand;");
  // Shape 是接口（ACC_ABSTRACT=0x400 | ACC_INTERFACE=0x200）。
  int sh_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Shape;");
  EXPECT_TRUE(sh_idx >= 0);
  EXPECT_EQ(dex->GetClassDef(static_cast<uint32_t>(sh_idx)).access_flags & 0x600, 0x600);
}

void TestCodeItems(const std::vector<uint8_t>& fx) {
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  int calc_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Calc;");
  EXPECT_TRUE(calc_idx >= 0);

  // safeDiv：try/catch -> tries_size == 1，try_item 紧随 insns 后 4 对齐。
  uint32_t mi = 0, code_off = 0;
  EXPECT_TRUE(FindMethod(*dex, calc_idx, "safeDiv", 2, &mi, &code_off, &error));
  const CodeItem* code = dex->GetCodeItem(code_off, &error);
  EXPECT_TRUE(code != nullptr);
  if (code) {
    EXPECT_EQ(code->registers_size, 2);
    EXPECT_EQ(code->ins_size, 2);
    EXPECT_EQ(code->outs_size, 0);
    EXPECT_EQ(code->tries_size, 1);
    EXPECT_EQ(code->insns_size, 4);
    EXPECT_TRUE(code->insns != nullptr);
    EXPECT_TRUE(code->tries_off != 0);
    EXPECT_EQ(code->tries_off % 4, 0);
    // try_item[0]: start_addr=0, insn_count=1（区间 [0,1)，仅保护 div 一条，
    // 对照 oracle FindTryItem: end = start + insn_count 半开区间）。
    const uint8_t* try0 = dex->data() + code->tries_off;
    EXPECT_EQ(Rd32(try0), 0);
    EXPECT_EQ(Rd16(try0 + 4), 1);
    EXPECT_TRUE(WalkInsns(code));
    // 缓存：同一 code_off 返回同一对象。
    EXPECT_TRUE(dex->GetCodeItem(code_off, &error) == code);
  }

  // mix：宽参数 ins_size = 1+1+2+1 = 5。
  EXPECT_TRUE(FindMethod(*dex, calc_idx, "mix", 4, &mi, &code_off, &error));
  code = dex->GetCodeItem(code_off, &error);
  EXPECT_TRUE(code != nullptr);
  if (code) {
    EXPECT_EQ(code->ins_size, 5);
    EXPECT_EQ(code->registers_size, 5);
    EXPECT_EQ(code->tries_size, 0);
    EXPECT_EQ(code->tries_off, 0);
    EXPECT_TRUE(WalkInsns(code));
  }

  // table：insns 含 fill-array-data payload，走宽度表恰好覆盖。
  EXPECT_TRUE(FindMethod(*dex, calc_idx, "table", 0, &mi, &code_off, &error));
  code = dex->GetCodeItem(code_off, &error);
  EXPECT_TRUE(code != nullptr);
  if (code) {
    EXPECT_EQ(code->insns_size, 36);
    EXPECT_TRUE(WalkInsns(code));
  }

  // 所有带 code 的方法：InsnWidth 线性覆盖 insns_size（对照 d8 真实字节码）。
  ClassData cd;
  EXPECT_TRUE(dex->GetClassData(static_cast<uint32_t>(calc_idx), &cd, &error));
  for (const auto& m : cd.direct_methods) {
    if (m.code_off == 0) continue;
    const CodeItem* c = dex->GetCodeItem(m.code_off, &error);
    EXPECT_TRUE(c != nullptr);
    if (c) EXPECT_TRUE(WalkInsns(c));
  }
  for (const auto& m : cd.virtual_methods) {
    if (m.code_off == 0) continue;
    const CodeItem* c = dex->GetCodeItem(m.code_off, &error);
    EXPECT_TRUE(c != nullptr);
    if (c) EXPECT_TRUE(WalkInsns(c));
  }

  // 抽象方法：code_off == 0 -> nullptr 且不算错误。
  int sh_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Shape;");
  EXPECT_TRUE(sh_idx >= 0);
  uint32_t ai = 0;
  EXPECT_TRUE(FindMethod(*dex, sh_idx, "area", 0, &ai, &code_off, &error));
  EXPECT_EQ(code_off, 0);
  error = "sentinel";
  EXPECT_TRUE(dex->GetCodeItem(0, &error) == nullptr);
  EXPECT_STR(error, "sentinel");

  // 非法 code_off：出界 / 未对齐 -> nullptr + error。
  error.clear();
  EXPECT_TRUE(dex->GetCodeItem(static_cast<uint32_t>(fx.size() - 4), &error) == nullptr);
  EXPECT_TRUE(!error.empty());
  error.clear();
  EXPECT_TRUE(dex->GetCodeItem(0x25, &error) == nullptr);
  EXPECT_TRUE(!error.empty());
}

void TestStaticValuesFixture(const std::vector<uint8_t>& fx) {
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  int calc_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Calc;");
  EXPECT_TRUE(calc_idx >= 0);
  ClassData cd;
  EXPECT_TRUE(dex->GetClassData(static_cast<uint32_t>(calc_idx), &cd, &error));
  std::vector<EncodedValue> vals;
  EXPECT_TRUE(dex->GetStaticValues(static_cast<uint32_t>(calc_idx), &vals, &error));
  // 末位字段 AFTER=7 非默认，无尾部省略：15 个值对应 15 个静态字段。
  EXPECT_EQ(vals.size(), cd.static_fields.size());
  EXPECT_EQ(vals.size(), 15);

  std::string nul_str("a\0b", 3);
  for (size_t i = 0; i < vals.size() && i < cd.static_fields.size(); ++i) {
    const std::string name =
        dex->GetString(dex->GetFieldId(cd.static_fields[i].field_idx).name_idx);
    const EncodedValue& v = vals[i];
    if (name == "AFTER") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kInt);
      EXPECT_EQ(v.int_value, 7);
    } else if (name == "B") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kInt);
      EXPECT_EQ(v.int_value, static_cast<uint64_t>(-5));
    } else if (name == "C") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kInt);
      EXPECT_EQ(v.int_value, 65);
    } else if (name == "CLS") {
      // d8 不把 class 字面量提升进 static_values：数组里是 <clinit> 前的
      // 默认值 null（<clinit> 里 const-class + sput）。
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kNull);
    } else if (name == "D") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kDouble);
      EXPECT_EQ(v.bits, 0xC002000000000000ull);  // -2.25 位型
    } else if (name == "F") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kFloat);
      EXPECT_EQ(v.bits, 0x3FC00000u);  // 1.5f 位型
    } else if (name == "I") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kInt);
      EXPECT_EQ(v.int_value, 123456);
    } else if (name == "L") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kLong);
      EXPECT_EQ(v.int_value, 0x0123456789ABCDEFull);
    } else if (name == "NUL") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kNull);
    } else if (name == "NULSTR") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kString);
      EXPECT_STR(dex->GetString(v.idx), nul_str);
    } else if (name == "S") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kInt);
      EXPECT_EQ(v.int_value, static_cast<uint64_t>(-300));
    } else if (name == "STR") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kString);
      EXPECT_STR(dex->GetString(v.idx), "hi");
    } else if (name == "ZERO") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kZero);  // 数值 0 -> kZero
    } else if (name == "Z_FALSE") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kBoolean);
      EXPECT_EQ(v.int_value, 0);
    } else if (name == "Z_TRUE") {
      EXPECT_TRUE(v.kind == EncodedValue::Kind::kBoolean);
      EXPECT_EQ(v.int_value, 1);
    } else {
      EXPECT_TRUE(false);  // 未知字段名
    }
  }

  // Operand 无 static_values：空 + true。
  int op_idx = dex->FindClassDefIndexByDescriptor("Lfixture/Operand;");
  EXPECT_TRUE(op_idx >= 0);
  EXPECT_TRUE(dex->GetStaticValues(static_cast<uint32_t>(op_idx), &vals, &error));
  EXPECT_EQ(vals.size(), 0);
  // 越界。
  EXPECT_TRUE(!dex->GetStaticValues(9999, &vals, &error));
  EXPECT_TRUE(!error.empty());
}

void TestFindClassDef(const std::vector<uint8_t>& fx) {
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(fx.data(), fx.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  int a = dex->FindClassDefIndexByDescriptor("Lfixture/Calc;");
  int b = dex->FindClassDefIndexByDescriptor("Lfixture/Operand;");
  int c = dex->FindClassDefIndexByDescriptor("Lfixture/Shape;");
  EXPECT_TRUE(a >= 0 && b >= 0 && c >= 0);
  EXPECT_TRUE(a != b && b != c);
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetClassDef(a).class_idx)),
             "Lfixture/Calc;");
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetClassDef(b).class_idx)),
             "Lfixture/Operand;");
  EXPECT_STR(std::string(dex->GetTypeDescriptor(dex->GetClassDef(c).class_idx)),
             "Lfixture/Shape;");
  EXPECT_EQ(dex->FindClassDefIndexByDescriptor("Lfixture/Missing;"), static_cast<uint64_t>(-1));
  EXPECT_EQ(dex->FindClassDefIndexByDescriptor(""), static_cast<uint64_t>(-1));
  // 重复查询命中缓存路径。
  EXPECT_EQ(dex->FindClassDefIndexByDescriptor("Lfixture/Calc;"), a);
}

// ---- 测试（合成 DEX）----

void TestSynthStaticValues(const std::vector<uint8_t>& fx) {
  (void)fx;
  SynthDex s = BuildSynthDex();
  std::string error;
  std::unique_ptr<DexFile> dex = DexFile::Open(s.bytes.data(), s.bytes.size(), &error);
  EXPECT_TRUE(dex != nullptr);
  if (!dex) return;
  EXPECT_EQ(dex->NumStringIds(), 3);
  EXPECT_EQ(dex->NumTypeIds(), 2);
  EXPECT_EQ(dex->NumClassDefs(), 2);
  EXPECT_EQ(dex->FindClassDefIndexByDescriptor("Lfixture/Synth;"), 0);
  EXPECT_EQ(dex->FindClassDefIndexByDescriptor("Lfixture/Plain;"), 1);
  EXPECT_EQ(dex->FindClassDefIndexByDescriptor("Lfixture/Nope;"), static_cast<uint64_t>(-1));

  std::vector<EncodedValue> vals;
  EXPECT_TRUE(dex->GetStaticValues(0, &vals, &error));
  EXPECT_EQ(vals.size(), 13);
  if (vals.size() != 13) return;

  // [0] VALUE_TYPE -> kType（d8 不会从 Java 源生成，合成覆盖）。
  EXPECT_TRUE(vals[0].kind == EncodedValue::Kind::kType);
  EXPECT_EQ(vals[0].idx, 0);
  EXPECT_STR(std::string(dex->GetTypeDescriptor(vals[0].idx)), "Lfixture/Synth;");
  // [1..4] BYTE/SHORT/CHAR/INT -> kInt（含宽度收窄与符号/零扩展）。
  EXPECT_TRUE(vals[1].kind == EncodedValue::Kind::kInt);
  EXPECT_EQ(vals[1].int_value, static_cast<uint64_t>(-5));
  EXPECT_TRUE(vals[2].kind == EncodedValue::Kind::kInt);
  EXPECT_EQ(vals[2].int_value, static_cast<uint64_t>(-300));
  EXPECT_TRUE(vals[3].kind == EncodedValue::Kind::kInt);
  EXPECT_EQ(vals[3].int_value, 65);
  EXPECT_TRUE(vals[4].kind == EncodedValue::Kind::kInt);
  EXPECT_EQ(vals[4].int_value, 123456);
  // [5] LONG 宽值。
  EXPECT_TRUE(vals[5].kind == EncodedValue::Kind::kLong);
  EXPECT_EQ(vals[5].int_value, 0x0123456789ABCDEFull);
  // [6] FLOAT 窄宽（低位补 0）。
  EXPECT_TRUE(vals[6].kind == EncodedValue::Kind::kFloat);
  EXPECT_EQ(vals[6].bits, 0x3FC00000u);
  // [7] DOUBLE 宽值。
  EXPECT_TRUE(vals[7].kind == EncodedValue::Kind::kDouble);
  EXPECT_EQ(vals[7].bits, 0xC002000000000000ull);
  // [8] STRING。
  EXPECT_TRUE(vals[8].kind == EncodedValue::Kind::kString);
  EXPECT_EQ(vals[8].idx, 1);
  EXPECT_STR(dex->GetString(vals[8].idx), "syn");
  // [9] NULL / [10..11] BOOLEAN。
  EXPECT_TRUE(vals[9].kind == EncodedValue::Kind::kNull);
  EXPECT_TRUE(vals[10].kind == EncodedValue::Kind::kBoolean);
  EXPECT_EQ(vals[10].int_value, 1);
  EXPECT_TRUE(vals[11].kind == EncodedValue::Kind::kBoolean);
  EXPECT_EQ(vals[11].int_value, 0);
  // [12] 0 值 -> kZero。
  EXPECT_TRUE(vals[12].kind == EncodedValue::Kind::kZero);

  // Plain：class_data_off/static_values_off 均为 0 -> 空 + true。
  ClassData cd;
  EXPECT_TRUE(dex->GetClassData(1, &cd, &error));
  EXPECT_EQ(cd.static_fields.size(), 0);
  EXPECT_EQ(cd.direct_methods.size(), 0);
  EXPECT_TRUE(dex->GetStaticValues(1, &vals, &error));
  EXPECT_EQ(vals.size(), 0);
}

void TestSynthErrors(const std::vector<uint8_t>& fx) {
  (void)fx;
  // class_data_off 指向截断 ULEB -> false + error。
  {
    SynthDex s = BuildSynthDex();
    std::string error;
    std::unique_ptr<DexFile> dex = DexFile::Open(s.bytes.data(), s.bytes.size(), &error);
    EXPECT_TRUE(dex != nullptr);
    if (!dex) return;
    ClassData cd;
    error.clear();
    EXPECT_TRUE(!dex->GetClassData(0, &cd, &error));
    EXPECT_TRUE(!error.empty());
    // broken byte 确实是 class_data_off。
    EXPECT_EQ(dex->GetClassDef(0).class_data_off, s.broken_class_data_off);
    EXPECT_EQ(s.bytes[s.broken_class_data_off], 0x80);
  }
  // 不支持的 encoded_value 类型 -> false + error。
  {
    SynthDex s = BuildSynthDex();
    s.bytes[s.static_values_off + 1] = 0x19;  // VALUE_FIELD（不支持）
    std::string error;
    std::unique_ptr<DexFile> dex = DexFile::Open(s.bytes.data(), s.bytes.size(), &error);
    EXPECT_TRUE(dex != nullptr);
    if (!dex) return;
    std::vector<EncodedValue> vals;
    error.clear();
    EXPECT_TRUE(!dex->GetStaticValues(0, &vals, &error));
    EXPECT_TRUE(!error.empty());
  }
  // VALUE_TYPE 的 type_idx 越界 -> false + error。
  {
    SynthDex s = BuildSynthDex();
    s.bytes[s.static_values_off + 2] = 0x05;  // type_idx=5 >= 2
    std::string error;
    std::unique_ptr<DexFile> dex = DexFile::Open(s.bytes.data(), s.bytes.size(), &error);
    EXPECT_TRUE(dex != nullptr);
    if (!dex) return;
    std::vector<EncodedValue> vals;
    error.clear();
    EXPECT_TRUE(!dex->GetStaticValues(0, &vals, &error));
    EXPECT_TRUE(!error.empty());
  }
  // VALUE_BYTE 宽度违规（arg=1 -> width 2 > 1）-> false + error。
  {
    SynthDex s = BuildSynthDex();
    // [1] BYTE 值头位于 size uleb(1) + [0](2) 之后 = +3。
    s.bytes[s.static_values_off + 3] = 0x20;
    std::string error;
    std::unique_ptr<DexFile> dex = DexFile::Open(s.bytes.data(), s.bytes.size(), &error);
    EXPECT_TRUE(dex != nullptr);
    if (!dex) return;
    std::vector<EncodedValue> vals;
    error.clear();
    EXPECT_TRUE(!dex->GetStaticValues(0, &vals, &error));
    EXPECT_TRUE(!error.empty());
  }
  // encoded_array 截断（static_values_off 指向尾部 slack：
  // {size=1, VALUE_DOUBLE arg=7} 但 payload 不存在）-> false + error。
  {
    SynthDex s = BuildSynthDex();
    // class_def[0].static_values_off（class_defs_off + 28）改指截断数组。
    uint8_t* p = s.bytes.data() + s.class_defs_off + 28;
    p[0] = static_cast<uint8_t>(s.trunc_values_off);
    p[1] = static_cast<uint8_t>(s.trunc_values_off >> 8);
    p[2] = static_cast<uint8_t>(s.trunc_values_off >> 16);
    p[3] = static_cast<uint8_t>(s.trunc_values_off >> 24);
    std::string error;
    std::unique_ptr<DexFile> dex = DexFile::Open(s.bytes.data(), s.bytes.size(), &error);
    EXPECT_TRUE(dex != nullptr);
    if (!dex) return;
    std::vector<EncodedValue> vals;
    error.clear();
    EXPECT_TRUE(!dex->GetStaticValues(0, &vals, &error));
    EXPECT_TRUE(!error.empty());
  }
}

void TestInsnWidth() {
  // ≥15 个代表 opcode + 3 种 payload（对照 dex_instruction_list.h 宽度）。
  {
    uint16_t b[4] = {0x0000, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 1);  // nop
  }
  {
    uint16_t b[4] = {0x0110, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 1);  // move v0,v1 (12x)
  }
  {
    uint16_t b[4] = {0x000E, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 1);  // return-void (10x)
  }
  {
    uint16_t b[4] = {0x0012, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 1);  // const/4 (11n)
  }
  {
    uint16_t b[4] = {0x0028, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 1);  // goto (10t)
  }
  {
    uint16_t b[4] = {0x0021, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 1);  // array-length (12x)
  }
  {
    uint16_t b[4] = {0x0029, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // goto/16 (20t)
  }
  {
    uint16_t b[4] = {0x001A, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // const-string (21c)
  }
  {
    uint16_t b[4] = {0x0002, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // move/from16 (22x)
  }
  {
    uint16_t b[4] = {0x00D8, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // add-int/lit8 (22b)
  }
  {
    uint16_t b[4] = {0x0032, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // if-eq (22t)
  }
  {
    uint16_t b[4] = {0x0038, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // if-eqz (21t)
  }
  {
    uint16_t b[4] = {0x0090, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // add-int (23x)
  }
  {
    uint16_t b[4] = {0x0031, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 2);  // cmp-long (23x)
  }
  {
    uint16_t b[4] = {0x0014, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 3);  // const (31i)
  }
  {
    uint16_t b[4] = {0x0026, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 3);  // fill-array-data (31t)
  }
  {
    uint16_t b[4] = {0x0003, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 3);  // move/16 (32x)
  }
  {
    uint16_t b[4] = {0x002A, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 3);  // goto/32 (30t)
  }
  {
    uint16_t b[4] = {0x006E, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 3);  // invoke-virtual (35c)
  }
  {
    uint16_t b[4] = {0x0074, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 3);  // invoke-virtual/range (3rc)
  }
  {
    uint16_t b[4] = {0x00FA, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 4);  // invoke-polymorphic (45cc)
  }
  {
    uint16_t b[4] = {0x00FB, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 4);  // invoke-polymorphic/range (4rcc)
  }
  {
    uint16_t b[4] = {0x0018, 0, 0, 0};
    EXPECT_EQ(InsnWidth(b), 5);  // const-wide (51l)
  }
  // payloads。
  {
    uint16_t b[16] = {0x0100, 3, 0, 0};  // packed-switch, size=3
    EXPECT_EQ(InsnWidth(b), 4 + 2 * 3);
  }
  {
    uint16_t b[16] = {0x0200, 2, 0, 0};  // sparse-switch, size=2
    EXPECT_EQ(InsnWidth(b), 2 + 4 * 2);
  }
  {
    uint16_t b[16] = {0x0300, 2, 3, 0};  // fill-array-data, width=2 size=3
    EXPECT_EQ(InsnWidth(b), 4 + (2 * 3 + 1) / 2);
  }
  {
    uint16_t b[16] = {0x0300, 1, 4, 0};  // fill-array-data, width=1 size=4
    EXPECT_EQ(InsnWidth(b), 4 + (1 * 4 + 1) / 2);
  }
  {
    uint16_t b[16] = {0x0300, 8, 1, 0};  // fill-array-data, width=8 size=1
    EXPECT_EQ(InsnWidth(b), 4 + (8 * 1 + 1) / 2);
  }
  {
    uint16_t b[4] = {0x0400, 0, 0, 0};  // 0x04?? 非 payload 签名 -> 按 nop
    EXPECT_EQ(InsnWidth(b), 1);
  }
}

struct TestEntry {
  const char* name;
  void (*fn)(const std::vector<uint8_t>&);
};

const TestEntry kFixtureTests[] = {
    {"OpenValid", TestOpenValid},
    {"OpenRejects", TestOpenRejects},
    {"Strings", TestStrings},
    {"ProtoParams", TestProtoParams},
    {"ClassData", TestClassData},
    {"CodeItems", TestCodeItems},
    {"StaticValuesFixture", TestStaticValuesFixture},
    {"FindClassDef", TestFindClassDef},
    {"SynthStaticValues", TestSynthStaticValues},
    {"SynthErrors", TestSynthErrors},
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: %s <fixture.dex>\n", argv[0]);
    return 2;
  }
  std::vector<uint8_t> fx;
  if (!LoadFile(argv[1], &fx)) {
    std::printf("cannot read fixture: %s\n", argv[1]);
    return 2;
  }
  std::printf("fixture: %s (%zu bytes)\n", argv[1], fx.size());

  for (const TestEntry& t : kFixtureTests) {
    g_test = t.name;
    int before = g_failures;
    t.fn(fx);
    std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
  }
  g_test = "InsnWidth";
  {
    int before = g_failures;
    TestInsnWidth();
    std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", g_test);
  }

  std::printf("----\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
