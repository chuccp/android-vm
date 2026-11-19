// APK Session：zip 解析（stored/deflate via zlib）+ binary xml Manifest 最小解析
#include "session/session.h"

#include <zlib.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>

namespace dexvm::session {

namespace {

// ---- 最小 zip 读取（EOCD -> central directory -> entry data）----

constexpr uint32_t kZipSigLocal = 0x04034b50;
constexpr uint32_t kZipSigCentral = 0x02014b50;
constexpr uint32_t kZipSigEocd = 0x06054b50;
constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflate = 8;

struct ZipEntry {
  std::string name;
  uint16_t method = 0;
  uint32_t compressed_size = 0;
  uint32_t uncompressed_size = 0;
  uint32_t local_header_off = 0;
};

uint16_t Rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t Rd32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

bool Inflate(const uint8_t* src, size_t src_len, size_t dst_len, std::vector<uint8_t>* out) {
  out->resize(dst_len);
  z_stream zs{};
  zs.next_in = const_cast<Bytef*>(src);
  zs.avail_in = static_cast<uInt>(src_len);
  zs.next_out = out->data();
  zs.avail_out = static_cast<uInt>(dst_len);
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;  // raw deflate
  int rc = inflate(&zs, Z_FINISH);
  inflateEnd(&zs);
  return rc == Z_STREAM_END && zs.total_out == dst_len;
}

bool ReadFile(const std::string& path, std::vector<uint8_t>* out, std::string* error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) {
    *error = "cannot open: " + path;
    return false;
  }
  f.seekg(0, std::ios::end);
  out->resize(static_cast<size_t>(f.tellg()));
  f.seekg(0);
  f.read(reinterpret_cast<char*>(out->data()), out->size());
  return true;
}

// DexFile 借用字节的退役保活（对照 class_loader.h LoaderRegistry::retired_bytes_）：
// Session 销毁后 DexBlob 字节不释放，防 DexFile 悬挂（docs 03 章 10.2 语义）。
std::vector<std::vector<uint8_t>>& RetiredBytes() {
  static std::vector<std::vector<uint8_t>> g_retired;
  return g_retired;
}

// ---- binary xml（AndroidManifest.xml）最小解析 ----
// ResChunk 结构：header{type:u16, headerSize:u16, size:u32}；
// string pool (0x0001)、start element (0x0102)。

constexpr uint16_t kResXmlType = 0x0003;
constexpr uint16_t kResStringPoolType = 0x0001;
constexpr uint16_t kResXmlResourceMapType = 0x0180;
constexpr uint16_t kResStartElementType = 0x0102;
constexpr uint16_t kResEndElementType = 0x0103;

// AXML typed value 类型（Res_value dataType；AOSP TypedValue 同值）
constexpr uint8_t kTypeString = 0x03;
constexpr uint8_t kTypeFloat = 0x04;
constexpr uint8_t kTypeIntDec = 0x10;
constexpr uint8_t kTypeIntBoolean = 0x12;

// 元素属性（raw 字符串 + typed value；manifest 值投影原料）
struct XmlAttr {
  std::string key;
  std::string val;  // rawValue 字符串（命中 string pool 时）
  uint8_t vtype = 0;
  uint32_t vdata = 0;
};

// 栈式元素帧：<meta-data> 归父帧、intent-filter 标记父帧（AOSP 同层级语义）。
struct ElemFrame {
  std::string elem;
  std::vector<XmlAttr> attrs;
  std::vector<MetaEntry> metas;  // 子 <meta-data>（application/provider 帧收集）
  bool has_intent_filter = false;
  bool launcher = false;  // intent-filter 内含 LAUNCHER category
};

const XmlAttr* FindAttr(const ElemFrame& f, const char* key) {
  for (const auto& a : f.attrs) {
    if (a.key == key) return &a;
  }
  return nullptr;
}

std::string AttrStr(const ElemFrame& f, const char* key) {
  const XmlAttr* a = FindAttr(f, key);
  return a != nullptr ? a->val : std::string();
}

// 整型属性（TYPE_INT_* 族；AOSP TypedValue.TYPE_FIRST_INT..LAST_INT）
bool AttrInt(const ElemFrame& f, const char* key, int32_t* out) {
  const XmlAttr* a = FindAttr(f, key);
  if (a == nullptr || (a->vtype & 0xF0) != kTypeIntDec) return false;
  *out = static_cast<int32_t>(a->vdata);
  return true;
}

// 引用型属性（TYPE_REFERENCE：@0x7f... 资源 id；theme/icon 等）
bool AttrRef(const ElemFrame& f, const char* key, int32_t* out) {
  const XmlAttr* a = FindAttr(f, key);
  if (a == nullptr || a->vtype != 0x01) return false;
  *out = static_cast<int32_t>(a->vdata);
  return true;
}

// 布尔属性（TYPE_INT_BOOLEAN；缺省回落 def，AOSP getBoolean 同口径）
bool AttrBool(const ElemFrame& f, const char* key, bool def) {
  const XmlAttr* a = FindAttr(f, key);
  if (a == nullptr) return def;
  if (a->vtype == kTypeIntBoolean) return a->vdata != 0;
  return def;
}

// <meta-data> 属性组 -> 投影条目（AOSP PackageParser.parseMetaData 同型：
// android:value 按 typed value 分型；android:resource -> int 资源 id；
// 无 name 或无值/资源 -> 不成条目）。
bool BuildMetaEntry(const std::vector<XmlAttr>& attrs, MetaEntry* out) {
  MetaEntry m;
  const XmlAttr* name = nullptr;
  const XmlAttr* value = nullptr;
  const XmlAttr* resource = nullptr;
  for (const auto& a : attrs) {
    if (a.key == "name") name = &a;
    else if (a.key == "value") value = &a;
    else if (a.key == "resource") resource = &a;
  }
  if (name == nullptr || name->val.empty()) return false;
  m.name = name->val;
  if (value != nullptr) {
    switch (value->vtype) {
      case kTypeString:
        m.type = MetaEntry::Type::kString;
        m.str = value->val;
        break;
      case kTypeIntBoolean:
        m.type = MetaEntry::Type::kBool;
        m.num = value->vdata != 0 ? 1 : 0;
        break;
      case kTypeFloat:
        m.type = MetaEntry::Type::kFloat;
        std::memcpy(&m.f, &value->vdata, sizeof(m.f));
        break;
      default:
        if ((value->vtype & 0xF0) == kTypeIntDec) {
          m.type = MetaEntry::Type::kInt;
          m.num = static_cast<int32_t>(value->vdata);
        } else {
          return false;  // AOSP：其余类型不入 Bundle（warn 后跳过）
        }
        break;
    }
  } else if (resource != nullptr) {
    m.type = MetaEntry::Type::kResource;
    m.num = static_cast<int32_t>(resource->vdata);
  } else {
    return false;
  }
  *out = std::move(m);
  return true;
}

struct StringPool {
  std::vector<std::string> strings;
};

bool ParseStringPool(const uint8_t* p, size_t size, StringPool* pool) {
  if (size < 28) return false;
  uint32_t string_count = Rd32(p + 8);
  uint32_t flags = Rd32(p + 16);
  uint32_t strings_start = Rd32(p + 20);
  bool utf8 = (flags & (1u << 8)) != 0;
  if (string_count > 0x10000) return false;
  pool->strings.reserve(string_count);
  for (uint32_t i = 0; i < string_count; i++) {
    uint32_t off = Rd32(p + 28 + i * 4);
    const uint8_t* s = p + strings_start + off;
    if (s >= p + size) return false;
    if (utf8) {
      // u8 charLen（可 2 字节）+ u8 byteLen（可 2 字节）+ bytes + \0
      auto len8 = [&](const uint8_t** q) -> uint32_t {
        uint32_t v = **q;
        (*q)++;
        if (v & 0x80) {
          v = ((v & 0x7F) << 8) | **q;
          (*q)++;
        }
        return v;
      };
      const uint8_t* q = s;
      len8(&q);  // charLen 丢弃
      uint32_t byte_len = len8(&q);
      if (q + byte_len > p + size) return false;
      pool->strings.emplace_back(reinterpret_cast<const char*>(q), byte_len);
    } else {
      // u16 charLen（可 2 字节）+ utf16 + \0
      uint32_t char_len = Rd16(s);
      const uint8_t* q = s + 2;
      if (char_len & 0x8000) {
        char_len = ((char_len & 0x7FFF) << 16) | Rd16(q);
        q += 2;
      }
      std::string out;
      out.reserve(char_len * 3);
      for (uint32_t j = 0; j < char_len; j++) {
        uint16_t c = Rd16(q + j * 2);
        if (c < 0x80) {
          out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
          out.push_back(static_cast<char>(0xC0 | (c >> 6)));
          out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
          out.push_back(static_cast<char>(0xE0 | (c >> 12)));
          out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
          out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
      }
      pool->strings.push_back(std::move(out));
    }
  }
  return true;
}

}  // namespace

Session::~Session() {
  for (auto& blob : dex_blobs_) RetiredBytes().push_back(std::move(blob.bytes));
}

std::unique_ptr<Session> Session::Open(const std::string& path, std::string* error) {
  std::vector<uint8_t> data;
  if (!ReadFile(path, &data, error)) return nullptr;
  auto s = std::unique_ptr<Session>(new Session());
  s->path_ = path;

  // 单 DEX 文件
  if (data.size() >= 4 && std::memcmp(data.data(), "dex\n", 4) == 0) {
    DexBlob blob;
    blob.name = "classes.dex";
    blob.bytes = std::move(data);
    s->dex_blobs_.push_back(std::move(blob));
    return s;
  }
  // APK（zip）
  if (!s->ParseApk(data, error)) return nullptr;
  if (s->dex_blobs_.empty()) {
    *error = "no classes*.dex in: " + path;
    return nullptr;
  }
  return s;
}

// 按名抽取 zip entry（中央目录扫描 + 局部解压；见 session.h 契约）
bool ReadZipResources(const std::string& zip_path, const std::string& name,
                      std::vector<ResourceHit>* out, std::string* error) {
  if (out == nullptr) return false;
  out->clear();
  std::vector<uint8_t> zip;
  std::string err;
  if (!ReadFile(zip_path, &zip, &err)) {
    if (error != nullptr) *error = err;
    return false;
  }
  if (zip.size() < 22) {
    if (error != nullptr) *error = "zip too small";
    return false;
  }
  // EOCD（注释最长 64K）
  size_t eocd = 0;
  bool found = false;
  size_t scan_start = zip.size() >= 65557 ? zip.size() - 65557 : 0;
  for (size_t i = zip.size() - 22 + 1; i-- > scan_start;) {
    if (Rd32(zip.data() + i) == kZipSigEocd) {
      eocd = i;
      found = true;
      break;
    }
  }
  if (!found) {
    if (error != nullptr) *error = "zip EOCD not found";
    return false;
  }
  uint16_t entry_count = Rd16(zip.data() + eocd + 10);
  uint32_t cd_off = Rd32(zip.data() + eocd + 16);

  size_t p = cd_off;
  for (uint16_t i = 0; i < entry_count; i++) {
    if (p + 46 > zip.size() || Rd32(zip.data() + p) != kZipSigCentral) {
      if (error != nullptr) *error = "bad central directory";
      return false;
    }
    uint16_t method = Rd16(zip.data() + p + 10);
    uint32_t compressed_size = Rd32(zip.data() + p + 20);
    uint32_t uncompressed_size = Rd32(zip.data() + p + 24);
    uint16_t name_len = Rd16(zip.data() + p + 28);
    uint16_t extra_len = Rd16(zip.data() + p + 30);
    uint16_t comment_len = Rd16(zip.data() + p + 32);
    uint32_t local_off = Rd32(zip.data() + p + 42);
    std::string entry_name(reinterpret_cast<const char*>(zip.data() + p + 46), name_len);
    p += 46 + name_len + extra_len + comment_len;

    if (entry_name != name) continue;

    size_t lp = local_off;
    if (lp + 30 > zip.size() || Rd32(zip.data() + lp) != kZipSigLocal) {
      if (error != nullptr) *error = "bad local header: " + entry_name;
      return false;
    }
    uint16_t lname = Rd16(zip.data() + lp + 26);
    uint16_t lextra = Rd16(zip.data() + lp + 28);
    size_t data_off = lp + 30 + lname + lextra;
    if (data_off + compressed_size > zip.size()) {
      if (error != nullptr) *error = "entry data out of range: " + entry_name;
      return false;
    }
    const uint8_t* src = zip.data() + data_off;
    ResourceHit hit;
    hit.url = "jar:file:" + zip_path + "!/" + name;
    if (method == kMethodStored) {
      hit.bytes.assign(src, src + compressed_size);
    } else if (method == kMethodDeflate) {
      if (!Inflate(src, compressed_size, uncompressed_size, &hit.bytes)) {
        if (error != nullptr) *error = "inflate failed: " + entry_name;
        return false;
      }
    } else {
      if (error != nullptr) *error = "unsupported zip method: " + std::to_string(method);
      return false;
    }
    out->push_back(std::move(hit));
  }
  return true;
}

// ---- resources.arsc 最小解析 + AXML 资源事件（docs 20 章；FileProvider 消费面）----
namespace {
// ResTable：ResTable_header(0x0002) + 全局 string pool + ResTable_package(0x0200)*。
// package：type string pool（类型名，type id = 索引+1+typeIdOffset）、key string
// pool（entry 名）、ResTable_type(0x0201)*（config + offsets[] + entry + Res_value）。
// 文件资源（xml/raw 等）= simple entry + TYPE_STRING -> 全局 pool 的 APK 内路径。

constexpr uint16_t kResTableChunkType = 0x0002;
constexpr uint16_t kResTablePackageChunk = 0x0200;
constexpr uint16_t kResTableTypeChunk = 0x0201;
constexpr uint8_t kTypeRefString = 0x03;  // Res_value TYPE_STRING（文件引用）
constexpr uint32_t kNoEntry = 0xFFFFFFFFu;
constexpr uint16_t kEntryComplex = 0x0001;
constexpr uint8_t kTypeFlagSparse = 0x01;
constexpr uint8_t kTypeFlagOffset16 = 0x02;

struct ResSimpleEntry {
  bool defined = false;
  bool is_string = false;
  uint32_t key_idx = 0;
  uint32_t str_idx = 0;  // 全局 string pool 索引（is_string 时）
  // 值面（Resources.getString/getStringArray 语义源）：simple = 单值；
  // complex（bag/map）= (dataType, data) 序（数组元素 = 逐项 TYPE_STRING）。
  uint8_t vtype = 0;
  uint32_t data = 0;
  bool complex = false;
  std::vector<std::pair<uint8_t, uint32_t>> bag;
  // style 面（ResTable_map_entry）：parent 资源 id + bag 项名（attr id）
  uint32_t parent = 0;
  std::vector<std::pair<uint32_t, std::pair<uint8_t, uint32_t>>> style_items;
};
struct ResTypeChunk {
  uint8_t type_id = 0;
  bool default_config = false;
  // 原始 ResTable_config 字节（限定符打分用：density@14、uiMode@29）。
  std::vector<uint8_t> config;
  std::vector<ResSimpleEntry> entries;  // 按 entry index
};
struct ResPkg {
  uint32_t id = 0;
  std::string name;
  std::vector<std::string> type_names;
  std::vector<std::string> key_names;
  std::vector<ResTypeChunk> chunks;
};
struct ResTableData {
  std::vector<std::string> global_strings;
  std::vector<ResPkg> pkgs;
};

// config = 全零（size 字段外）按默认 config 处理（限定符匹配最小面）
bool IsDefaultConfig(const uint8_t* cfg, size_t avail) {
  if (avail < 4) return true;
  uint32_t size = Rd32(cfg);
  if (size < 4 || size > avail) return true;
  for (uint32_t i = 4; i < size; i++) {
    if (cfg[i] != 0) return false;
  }
  return true;
}

// complex entry（ResTable_map_entry + ResTable_map[]）：bag 值序记录
// （数组元素 = 逐项 (TYPE_STRING, 全局 pool 索引)；Resources.getStringArray 源）。
bool ParseComplexEntry(const uint8_t* e, const uint8_t* end, ResSimpleEntry* out) {
  uint16_t entry_size = Rd16(e);
  uint32_t count = Rd32(e + 12);
  if (count > 0x10000) return false;
  out->complex = true;
  out->parent = Rd32(e + 8);  // ResTable_map_entry.parent（style 继承链）
  const uint8_t* m = e + (entry_size >= 16 ? entry_size : 16);
  for (uint32_t i = 0; i < count; i++) {
    if (m + 12 > end) break;
    uint32_t name = Rd32(m + 0);  // ResTable_map.name（style = attr id；+0 起）
    uint8_t dtype = m[7];
    uint32_t d = Rd32(m + 8);
    out->bag.emplace_back(dtype, d);
    out->style_items.emplace_back(name, std::make_pair(dtype, d));
    m += 12;
  }
  return true;
}

bool ParseResTypeChunk(const uint8_t* p, size_t size, ResTypeChunk* out) {
  if (size < 20) return false;
  out->type_id = p[8];
  uint8_t flags = p[9];
  uint32_t entry_count = Rd32(p + 12);
  uint32_t entries_start = Rd32(p + 16);
  uint16_t header_size = Rd16(p + 2);
  if (entry_count > 0x10000) return false;
  out->default_config = IsDefaultConfig(p + 20, size - 20);
  {
    uint32_t cfg_size = Rd32(p + 20);
    if (cfg_size >= 4 && cfg_size <= size - 20) {
      out->config.assign(p + 20, p + 20 + cfg_size);
    }
  }
  const uint8_t* offsets = p + header_size;
  out->entries.resize(entry_count);
  if ((flags & kTypeFlagSparse) != 0) {
    // sparse：(entry index u16, offset/4 u16) 对，按 entry index 排序
    for (uint32_t i = 0; i < entry_count; i++) {
      const uint8_t* pair = offsets + static_cast<size_t>(i) * 4;
      if (pair + 4 > p + size) return false;
      uint16_t idx = Rd16(pair);
      uint32_t off = static_cast<uint32_t>(Rd16(pair + 2)) * 4;
      if (idx >= entry_count) continue;
      const uint8_t* e = p + entries_start + off;
      if (e + 12 > p + size) continue;
      ResSimpleEntry ent;
      ent.defined = true;
      ent.key_idx = Rd32(e + 4);
      if (Rd16(e + 2) & kEntryComplex) {
        if (!ParseComplexEntry(e, p + size, &ent)) continue;
        out->entries[idx] = ent;
        continue;
      }
      ent.vtype = e[11];
      ent.data = Rd32(e + 12);
      ent.is_string = ent.vtype == kTypeRefString;
      ent.str_idx = ent.data;
      out->entries[idx] = ent;
    }
    return true;
  }
  for (uint32_t i = 0; i < entry_count; i++) {
    uint32_t off;
    if ((flags & kTypeFlagOffset16) != 0) {
      const uint8_t* q = offsets + static_cast<size_t>(i) * 2;
      if (q + 2 > p + size) return false;
      uint16_t o16 = Rd16(q);
      if (o16 == 0xFFFF) continue;
      off = static_cast<uint32_t>(o16) * 4;
    } else {
      const uint8_t* q = offsets + static_cast<size_t>(i) * 4;
      if (q + 4 > p + size) return false;
      off = Rd32(q);
      if (off == kNoEntry) continue;
    }
    const uint8_t* e = p + entries_start + off;
    if (e + 12 > p + size) continue;
    ResSimpleEntry ent;
    ent.defined = true;
    ent.key_idx = Rd32(e + 4);
    if (Rd16(e + 2) & kEntryComplex) {
      if (!ParseComplexEntry(e, p + size, &ent)) continue;
      out->entries[i] = ent;
      continue;
    }
    ent.vtype = e[11];
    ent.data = Rd32(e + 12);
    ent.is_string = ent.vtype == kTypeRefString;
    ent.str_idx = ent.data;
    out->entries[i] = ent;
  }
  return true;
}

bool ParseResTable(const std::vector<uint8_t>& data, ResTableData* out, std::string* error) {
  if (data.size() < 12 || Rd16(data.data()) != kResTableChunkType) {
    if (error != nullptr) *error = "not resources.arsc";
    return false;
  }
  size_t p = Rd16(data.data() + 2);
  bool global_seen = false;
  while (p + 8 <= data.size()) {
    uint16_t type = Rd16(data.data() + p);
    uint32_t size = Rd32(data.data() + p + 4);
    uint16_t header_size = Rd16(data.data() + p + 2);
    if (size < 8 || p + size > data.size()) break;
    if (type == kResStringPoolType && !global_seen) {
      StringPool pool;
      if (!ParseStringPool(data.data() + p, size, &pool)) {
        if (error != nullptr) *error = "arsc: bad global string pool";
        return false;
      }
      out->global_strings = std::move(pool.strings);
      global_seen = true;
    } else if (type == kResTablePackageChunk) {
      ResPkg pkg;
      pkg.id = Rd32(data.data() + p + 8);
      // name：u16[128] UTF-16（offset 12，256 字节）
      const uint8_t* np = data.data() + p + 12;
      for (int i = 0; i < 128; i++) {
        uint16_t c = Rd16(np + i * 2);
        if (c == 0) break;
        if (c < 0x80) {
          pkg.name.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
          pkg.name.push_back(static_cast<char>(0xC0 | (c >> 6)));
          pkg.name.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
          pkg.name.push_back(static_cast<char>(0xE0 | (c >> 12)));
          pkg.name.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
          pkg.name.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
      }
      uint32_t type_strings_off = Rd32(data.data() + p + 268);
      uint32_t key_strings_off = Rd32(data.data() + p + 276);
      uint32_t type_id_offset = header_size >= 288 ? Rd32(data.data() + p + 284) : 0;
      if (type_strings_off < size && key_strings_off < size) {
        StringPool tp;
        StringPool kp;
        if (!ParseStringPool(data.data() + p + type_strings_off, size - type_strings_off, &tp)) {
          if (error != nullptr) *error = "arsc: bad type string pool";
          return false;
        }
        if (!ParseStringPool(data.data() + p + key_strings_off, size - key_strings_off, &kp)) {
          if (error != nullptr) *error = "arsc: bad key string pool";
          return false;
        }
        pkg.type_names = std::move(tp.strings);
        pkg.key_names = std::move(kp.strings);
      }
      // package 内 ResTable_type 子块
      size_t q = header_size;
      while (q + 8 <= size) {
        uint16_t ctype = Rd16(data.data() + p + q);
        uint32_t csize = Rd32(data.data() + p + q + 4);
        if (csize < 8 || q + csize > size) break;
        if (ctype == kResTableTypeChunk) {
          ResTypeChunk chunk;
          if (ParseResTypeChunk(data.data() + p + q, csize, &chunk)) {
            pkg.chunks.push_back(std::move(chunk));
          }
        }
        q += csize;
      }
      out->pkgs.push_back(std::move(pkg));
    }
    p += size;
  }
  if (!global_seen) {
    if (error != nullptr) *error = "arsc: no global string pool";
    return false;
  }
  return true;
}

// 表缓存（进程内一次解析多查；nil = 解析失败不缓存，错误随调用返回）
const ResTableData* GetResTable(const std::string& apk_path, std::string* error) {
  static std::map<std::string, std::unique_ptr<ResTableData>> g_tables;
  auto it = g_tables.find(apk_path);
  if (it != g_tables.end()) return it->second.get();
  std::vector<ResourceHit> hits;
  std::string err;
  if (!ReadZipResources(apk_path, "resources.arsc", &hits, &err) || hits.empty()) {
    if (error != nullptr) *error = "resources.arsc not found in " + apk_path + ": " + err;
    return nullptr;
  }
  auto table = std::unique_ptr<ResTableData>(new ResTableData());
  if (!ParseResTable(hits[0].bytes, table.get(), &err)) {
    if (error != nullptr) *error = err;
    return nullptr;
  }
  const ResTableData* raw = table.get();
  g_tables[apk_path] = std::move(table);
  return raw;
}

const ResPkg* FindPkg(const ResTableData& t, uint32_t id, const std::string& name) {
  for (const auto& p : t.pkgs) {
    if (!name.empty()) {
      if (p.name == name) return &p;
    } else if (p.id == id) {
      return &p;
    }
  }
  if (!name.empty()) return nullptr;
  return t.pkgs.empty() ? nullptr : &t.pkgs[0];  // id 未命中回落首包（最小面）
}
}  // namespace

bool Session::ParseApk(const std::vector<uint8_t>& zip, std::string* error) {
  // 从尾部找 EOCD（注释最长 64K）
  if (zip.size() < 22) {
    *error = "zip too small";
    return false;
  }
  size_t eocd = 0;
  bool found = false;
  size_t scan_start = zip.size() >= 65557 ? zip.size() - 65557 : 0;
  for (size_t i = zip.size() - 22 + 1; i-- > scan_start;) {
    if (Rd32(zip.data() + i) == kZipSigEocd) {
      eocd = i;
      found = true;
      break;
    }
  }
  if (!found) {
    *error = "zip EOCD not found";
    return false;
  }
  uint16_t entry_count = Rd16(zip.data() + eocd + 10);
  uint32_t cd_off = Rd32(zip.data() + eocd + 16);

  std::vector<ZipEntry> entries;
  entries.reserve(entry_count);
  size_t p = cd_off;
  for (uint16_t i = 0; i < entry_count; i++) {
    if (p + 46 > zip.size() || Rd32(zip.data() + p) != kZipSigCentral) {
      *error = "bad central directory";
      return false;
    }
    ZipEntry e;
    e.method = Rd16(zip.data() + p + 10);
    e.compressed_size = Rd32(zip.data() + p + 20);
    e.uncompressed_size = Rd32(zip.data() + p + 24);
    uint16_t name_len = Rd16(zip.data() + p + 28);
    uint16_t extra_len = Rd16(zip.data() + p + 30);
    uint16_t comment_len = Rd16(zip.data() + p + 32);
    e.local_header_off = Rd32(zip.data() + p + 42);
    e.name.assign(reinterpret_cast<const char*>(zip.data() + p + 46), name_len);
    entries.push_back(std::move(e));
    p += 46 + name_len + extra_len + comment_len;
  }

  for (const auto& e : entries) {
    bool is_dex = e.name == "classes.dex" ||
                  (e.name.rfind("classes", 0) == 0 && e.name.size() > 11 &&
                   e.name[e.name.size() - 4] == '.' && e.name.substr(e.name.size() - 4) == ".dex");
    bool is_manifest = e.name == "AndroidManifest.xml";
    if (!is_dex && !is_manifest) continue;

    // local header：跳过 name/extra 拿数据
    size_t lp = e.local_header_off;
    if (lp + 30 > zip.size() || Rd32(zip.data() + lp) != kZipSigLocal) {
      *error = "bad local header: " + e.name;
      return false;
    }
    uint16_t lname = Rd16(zip.data() + lp + 26);
    uint16_t lextra = Rd16(zip.data() + lp + 28);
    size_t data_off = lp + 30 + lname + lextra;
    if (data_off + e.compressed_size > zip.size()) {
      *error = "entry data out of range: " + e.name;
      return false;
    }
    const uint8_t* src = zip.data() + data_off;

    std::vector<uint8_t> bytes;
    if (e.method == kMethodStored) {
      bytes.assign(src, src + e.compressed_size);
    } else if (e.method == kMethodDeflate) {
      if (!Inflate(src, e.compressed_size, e.uncompressed_size, &bytes)) {
        *error = "inflate failed: " + e.name;
        return false;
      }
    } else {
      *error = "unsupported zip method: " + std::to_string(e.method);
      return false;
    }

    if (is_dex) {
      DexBlob blob;
      blob.name = e.name;
      blob.bytes = std::move(bytes);
      dex_blobs_.push_back(std::move(blob));
    } else if (is_manifest) {
      if (!ParseManifest(bytes, error)) return false;  // 解析失败按错误处理
      has_manifest_ = true;
    }
  }
  // multidex 顺序：classes.dex、classes2.dex…
  std::sort(dex_blobs_.begin(), dex_blobs_.end(),
            [](const DexBlob& a, const DexBlob& b) { return a.name < b.name; });
  return true;
}

bool Session::ParseManifest(const std::vector<uint8_t>& axml, std::string* error) {
  if (axml.size() < 8 || Rd16(axml.data()) != kResXmlType) {
    *error = "manifest: not binary xml";
    return false;
  }
  StringPool pool;
  bool pool_seen = false;
  std::vector<ElemFrame> stack;  // 元素栈（<meta-data>/intent-filter 归父帧）

  size_t p = Rd16(axml.data() + 2);  // 跳过 XML 头
  while (p + 8 <= axml.size()) {
    uint16_t type = Rd16(axml.data() + p);
    uint32_t size = Rd32(axml.data() + p + 4);
    if (size < 8 || p + size > axml.size()) break;

    if (type == kResStringPoolType && !pool_seen) {
      if (!ParseStringPool(axml.data() + p, size, &pool)) {
        *error = "manifest: bad string pool";
        return false;
      }
      pool_seen = true;
    } else if (type == kResStartElementType && pool_seen) {
      // start element：header(8) + lineNumber/comment(8) + ns/name(8)
      // + attributeStart/Size/count(8) + id/class/style(6)
      const uint8_t* se = axml.data() + p;
      if (size < 36 || stack.size() >= 64) {
        p += size;
        continue;
      }
      uint32_t name_idx = Rd32(se + 20);
      uint16_t attr_start = Rd16(se + 24);
      uint16_t attr_size = Rd16(se + 26);
      uint16_t attr_count = Rd16(se + 28);
      ElemFrame frame;
      frame.elem = name_idx < pool.strings.size() ? pool.strings[name_idx] : "";
      frame.attrs.reserve(attr_count);
      for (uint16_t a = 0; a < attr_count; a++) {
        const uint8_t* ap = se + 16 + attr_start + static_cast<size_t>(a) * attr_size;
        if (ap + 20 > se + size) break;
        uint32_t aname = Rd32(ap + 4);
        uint32_t raw = Rd32(ap + 8);
        uint8_t vtype = ap[15];
        uint32_t vdata = Rd32(ap + 16);
        XmlAttr attr;
        attr.key = aname < pool.strings.size() ? pool.strings[aname] : "";
        if (raw != 0xFFFFFFFFu && raw < pool.strings.size()) attr.val = pool.strings[raw];
        if (attr.val.empty() && vtype == kTypeString && vdata < pool.strings.size()) {
          attr.val = pool.strings[vdata];  // 无 rawValue 时回落 typed string
        }
        attr.vtype = vtype;
        attr.vdata = vdata;
        frame.attrs.push_back(std::move(attr));
      }
      stack.push_back(std::move(frame));
    } else if (type == kResEndElementType && pool_seen) {
      if (stack.empty()) {
        p += size;
        continue;
      }
      ElemFrame f = std::move(stack.back());
      stack.pop_back();
      if (f.elem == "manifest") {
        AttrInt(f, "versionCode", &manifest_.version_code);
        manifest_.version_name = AttrStr(f, "versionName");
        std::string pkg = AttrStr(f, "package");
        if (!pkg.empty()) manifest_.package_name = pkg;
      } else if (f.elem == "uses-sdk") {
        AttrInt(f, "minSdkVersion", &manifest_.min_sdk);
        AttrInt(f, "targetSdkVersion", &manifest_.target_sdk);
      } else if (f.elem == "application") {
        manifest_.application_class = AttrStr(f, "name");
        manifest_.application_meta_data = std::move(f.metas);
        manifest_.debuggable = AttrBool(f, "debuggable", false);
        manifest_.large_heap = AttrBool(f, "largeHeap", false);
        manifest_.has_code = AttrBool(f, "hasCode", true);
        AttrRef(f, "theme", &manifest_.application_theme);
      } else if (f.elem == "provider") {
        ProviderEntry e;
        e.name = AttrStr(f, "name");
        if (!e.name.empty()) {
          e.authorities = AttrStr(f, "authorities");
          AttrInt(f, "initOrder", &e.init_order);
          e.process_name = AttrStr(f, "process");
          e.permission = AttrStr(f, "permission");
          e.read_permission = AttrStr(f, "readPermission");
          e.write_permission = AttrStr(f, "writePermission");
          // AOSP PackageParser：read/writePermission 缺省回落 android:permission
          if (e.read_permission.empty()) e.read_permission = e.permission;
          if (e.write_permission.empty()) e.write_permission = e.permission;
          // exported：显式投影；缺省 = 含 intent-filter（AOSP 组件默认语义）
          e.exported = AttrBool(f, "exported", f.has_intent_filter);
          e.enabled = AttrBool(f, "enabled", true);
          e.multiprocess = AttrBool(f, "multiprocess", false);
          // android:grantUriPermissions（0x0101001b）：FileProvider.attachInfo
          // 消费面（false -> SecurityException）；缺省 false（AOSP 同口径）
          e.grant_uri_permissions = AttrBool(f, "grantUriPermissions", false);
          e.meta_data = std::move(f.metas);
          manifest_.providers.push_back(std::move(e));
        }
      } else if (f.elem == "service") {
        ServiceEntry e;
        e.name = AttrStr(f, "name");
        if (!e.name.empty()) {
          e.process_name = AttrStr(f, "process");
          e.permission = AttrStr(f, "permission");
          e.exported = AttrBool(f, "exported", f.has_intent_filter);
          e.enabled = AttrBool(f, "enabled", true);
          e.meta_data = std::move(f.metas);
          manifest_.services.push_back(std::move(e));
        }
      } else if (f.elem == "activity" || f.elem == "activity-alias") {
        // 注：<activity> 的结束事件只走本分支（此前另有同名 else-if 只记
        // launcher_activity，导致 activities 永远收不到 <activity>、
        // getActivityInfo 注册表为空——androidx NavUtils 查不到自家 Activity，
        // MainActivity.onCreate 抛 NameNotFoundException 包装的 IAE）。
        ActivityEntry e;
        e.name = AttrStr(f, "name");
        if (e.name.empty() && f.elem == "activity-alias") e.name = AttrStr(f, "name");
        if (!e.name.empty()) {
          if (f.elem == "activity" && f.launcher) {
            manifest_.launcher_activity = e.name;
          }
          e.process_name = AttrStr(f, "process");
          e.parent_name = AttrStr(f, "parentActivityName");
          e.target_name = AttrStr(f, "targetActivity");
          e.exported = AttrBool(f, "exported", f.has_intent_filter);
          e.enabled = AttrBool(f, "enabled", true);
          AttrRef(f, "theme", &e.theme);
          AttrInt(f, "launchMode", &e.launch_mode);
          AttrInt(f, "configChanges", &e.config_changes);
          e.meta_data = std::move(f.metas);
          manifest_.activities.push_back(std::move(e));
        }
      } else if (f.elem == "meta-data") {
        // 归父帧（application -> ApplicationInfo.metaData；provider ->
        // ProviderInfo.metaData；service -> ServiceInfo.metaData；
        // activity -> ActivityInfo.metaData）
        if (!stack.empty() && (stack.back().elem == "application" ||
                               stack.back().elem == "provider" ||
                               stack.back().elem == "service" ||
                               stack.back().elem == "activity" ||
                               stack.back().elem == "activity-alias")) {
          MetaEntry m;
          if (BuildMetaEntry(f.attrs, &m)) stack.back().metas.push_back(std::move(m));
        }
      } else if (f.elem == "intent-filter") {
        if (!stack.empty()) stack.back().has_intent_filter = true;
      } else if (f.elem == "category") {
        if (AttrStr(f, "name") == "android.intent.category.LAUNCHER") {
          for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            if (it->elem == "activity") {
              it->launcher = true;
              break;
            }
          }
        }
      }
    }
    p += size;
  }
  return true;
}

// binary xml -> 事件流（XmlResourceParser 最小面；同 manifest AXML 语法）
bool ParseResourceXml(const std::vector<uint8_t>& axml, std::vector<XmlResEvent>* out,
                      std::string* error) {
  if (out == nullptr) return false;
  out->clear();
  if (axml.size() < 8 || Rd16(axml.data()) != kResXmlType) {
    if (error != nullptr) *error = "resource xml: not binary xml";
    return false;
  }
  StringPool pool;
  bool pool_seen = false;
  // AXML resource map（chunk 0x0180）：attribute 名 string pool 索引 -> 资源 id
  // （android:text=0x0101014f 等）。LayoutInflater 面按 id 归名（aapt 编译态）。
  std::vector<uint32_t> res_map;
  size_t p = Rd16(axml.data() + 2);
  while (p + 8 <= axml.size()) {
    uint16_t type = Rd16(axml.data() + p);
    uint32_t size = Rd32(axml.data() + p + 4);
    if (size < 8 || p + size > axml.size()) break;
    if (type == kResStringPoolType && !pool_seen) {
      if (!ParseStringPool(axml.data() + p, size, &pool)) {
        if (error != nullptr) *error = "resource xml: bad string pool";
        return false;
      }
      pool_seen = true;
    } else if (type == kResXmlResourceMapType) {
      uint32_t count = (size - 8) / 4;
      res_map.reserve(count);
      for (uint32_t i = 0; i < count; i++) res_map.push_back(Rd32(axml.data() + p + 8 + i * 4));
    } else if (type == kResStartElementType && pool_seen) {
      const uint8_t* se = axml.data() + p;
      if (size < 36) {
        p += size;
        continue;
      }
      XmlResEvent ev;
      ev.type = XmlResEvent::kStartTag;
      uint32_t name_idx = Rd32(se + 20);
      uint16_t attr_start = Rd16(se + 24);
      uint16_t attr_size = Rd16(se + 26);
      uint16_t attr_count = Rd16(se + 28);
      if (name_idx < pool.strings.size()) ev.name = pool.strings[name_idx];
      ev.attrs.reserve(attr_count);
      for (uint16_t a = 0; a < attr_count; a++) {
        const uint8_t* ap = se + 16 + attr_start + static_cast<size_t>(a) * attr_size;
        if (ap + 20 > se + size) break;
        XmlResAttr attr;
        uint32_t ans = Rd32(ap + 0);
        uint32_t aname = Rd32(ap + 4);
        uint32_t raw = Rd32(ap + 8);
        uint8_t vtype = ap[15];
        uint32_t vdata = Rd32(ap + 16);
        if (ans < pool.strings.size()) attr.ns = pool.strings[ans];
        if (aname < pool.strings.size()) attr.name = pool.strings[aname];
        if (aname < res_map.size()) attr.res_id = res_map[aname];
        if (raw != 0xFFFFFFFFu && raw < pool.strings.size()) attr.value = pool.strings[raw];
        if (attr.value.empty() && vtype == kTypeString && vdata < pool.strings.size()) {
          attr.value = pool.strings[vdata];
        }
        attr.value_type = vtype;
        attr.value_data = vdata;
        ev.attrs.push_back(std::move(attr));
      }
      out->push_back(std::move(ev));
    } else if (type == kResEndElementType && pool_seen) {
      XmlResEvent ev;
      ev.type = XmlResEvent::kEndTag;
      const uint8_t* ee = axml.data() + p;
      if (size >= 24) {
        uint32_t name_idx = Rd32(ee + 20);
        if (name_idx < pool.strings.size()) ev.name = pool.strings[name_idx];
      }
      out->push_back(std::move(ev));
    }
    p += size;
  }
  XmlResEvent end;
  end.type = XmlResEvent::kEndDocument;
  out->push_back(std::move(end));
  return true;
}

namespace {
// 设备 config 打分（Android ResTable::getEntry 的最小实现）：默认 config 最优；
// 非夜间限定符（uiMode 高半字节 != NIGHT_YES）优先；density 越接近设备越优
// （模拟器 ~xxhdpi）。此前"默认优先、余取首个"会命中 night 变体（开屏白字消失）。
int ResChunkScore(const ResTypeChunk& chunk) {
  if (chunk.default_config) return 0;
  int score = 16;
  if (chunk.config.size() > 30) {
    uint8_t ui_mode = chunk.config[29];
    if ((ui_mode & 0x30) == 0x20) score += 40;  // UI_MODE_NIGHT_YES
  }
  if (chunk.config.size() > 16) {
    uint16_t density =
        static_cast<uint16_t>(chunk.config[14] | (chunk.config[15] << 8));
    if (density != 0) {
      constexpr int kTargetDensity = 480;  // xxhdpi
      int diff = density > kTargetDensity ? density - kTargetDensity
                                          : kTargetDensity - density;
      score += diff / 40;
    }
  }
  return score;
}
}  // namespace

bool ReadResourceFile(const std::string& apk_path, uint32_t res_id, std::string* entry_path,
                      std::vector<uint8_t>* bytes, std::string* error) {
  if (bytes == nullptr) return false;
  bytes->clear();
  const ResTableData* table = GetResTable(apk_path, error);
  if (table == nullptr) return false;
  uint32_t pkg_id = res_id >> 24;
  uint32_t type_id = (res_id >> 16) & 0xFF;
  uint32_t entry_idx = res_id & 0xFFFF;
  const ResPkg* pkg = FindPkg(*table, pkg_id, "");
  if (pkg == nullptr) {
    if (error != nullptr) *error = "resId: package not found";
    return false;
  }
  // config 限定符匹配：按设备 config 打分取最优（默认 config 最优；非夜间
  // 优先；density 最近）
  const ResSimpleEntry* hit = nullptr;
  int hit_score = 1 << 30;
  for (const auto& chunk : pkg->chunks) {
    if (chunk.type_id != type_id || entry_idx >= chunk.entries.size()) continue;
    const ResSimpleEntry& e = chunk.entries[entry_idx];
    if (!e.defined) continue;
    int score = ResChunkScore(chunk);
    if (score < hit_score) {
      hit_score = score;
      hit = &e;
      if (score == 0) break;
    }
  }
  if (hit == nullptr || !hit->is_string || hit->str_idx >= table->global_strings.size()) {
    if (error != nullptr) *error = "resId: no file value";
    return false;
  }
  std::string path = table->global_strings[hit->str_idx];
  if (entry_path != nullptr) *entry_path = path;
  std::vector<ResourceHit> hits;
  std::string err;
  if (!ReadZipResources(apk_path, path, &hits, &err) || hits.empty()) {
    if (error != nullptr) *error = "resource file read failed: " + path + ": " + err;
    return false;
  }
  *bytes = std::move(hits[0].bytes);
  return true;
}

// 资产缝：assets/<name> 条目读取（AssetManager.open 语义源；APK zip 内
// stored/deflate 均走 ReadZipResources）。Tabbar_Feed.json 等主 Tab 配置、
// appconfig、证书等资产都经此路径；查无 = false（非致命，调用方转 IOException）。
bool ReadAssetFile(const std::string& apk_path, const std::string& name,
                   std::vector<uint8_t>* bytes, std::string* error) {
  if (bytes == nullptr) return false;
  bytes->clear();
  std::vector<ResourceHit> hits;
  std::string err;
  if (!ReadZipResources(apk_path, "assets/" + name, &hits, &err) || hits.empty()) {
    if (error != nullptr) {
      *error = err.empty() ? ("asset not found: " + name) : err;
    }
    return false;
  }
  *bytes = std::move(hits[0].bytes);
  return true;
}

int32_t LookupResourceId(const std::string& apk_path, const std::string& package_name,
                         const std::string& type, const std::string& entry_name) {
  std::string err;
  const ResTableData* table = GetResTable(apk_path, &err);
  if (table == nullptr) return 0;
  const ResPkg* pkg = FindPkg(*table, 0, package_name);
  if (pkg == nullptr) return 0;
  uint32_t type_id = 0;
  for (size_t i = 0; i < pkg->type_names.size(); i++) {
    if (pkg->type_names[i] == type) {
      type_id = static_cast<uint32_t>(i) + 1;
      break;
    }
  }
  if (type_id == 0) return 0;
  for (const auto& chunk : pkg->chunks) {
    if (chunk.type_id != type_id) continue;
    for (size_t i = 0; i < chunk.entries.size(); i++) {
      const ResSimpleEntry& e = chunk.entries[i];
      if (!e.defined || e.key_idx >= pkg->key_names.size()) continue;
      if (pkg->key_names[e.key_idx] == entry_name) {
        return static_cast<int32_t>((pkg->id << 24) | (type_id << 16) |
                                    static_cast<uint32_t>(i));
      }
    }
  }
  return 0;
}

namespace {
// config 匹配条目查找（ReadResourceFile 同口径：按设备 config 打分取最优）
const ResSimpleEntry* FindResEntryFor(const ResTableData& table, uint32_t pkg_id,
                                      uint32_t type_id, uint32_t entry_idx) {
  const ResPkg* pkg = FindPkg(table, pkg_id, "");
  if (pkg == nullptr) return nullptr;
  const ResSimpleEntry* hit = nullptr;
  int hit_score = 1 << 30;
  for (const auto& chunk : pkg->chunks) {
    if (chunk.type_id != type_id || entry_idx >= chunk.entries.size()) continue;
    const ResSimpleEntry& e = chunk.entries[entry_idx];
    if (!e.defined) continue;
    int score = ResChunkScore(chunk);
    if (score < hit_score) {
      hit_score = score;
      hit = &e;
      if (score == 0) break;
    }
  }
  return hit;
}
}  // namespace

bool LookupResourceString(const std::string& apk_path, uint32_t res_id, std::string* out,
                          std::string* error) {
  if (out == nullptr) return false;
  out->clear();
  const ResTableData* table = GetResTable(apk_path, error);
  if (table == nullptr) return false;
  const ResSimpleEntry* hit =
      FindResEntryFor(*table, res_id >> 24, (res_id >> 16) & 0xFF, res_id & 0xFFFF);
  if (hit == nullptr || !hit->is_string || hit->str_idx >= table->global_strings.size()) {
    if (error != nullptr) *error = "resId: no string value";
    return false;
  }
  *out = table->global_strings[hit->str_idx];
  return true;
}

bool LookupResourceStringArray(const std::string& apk_path, uint32_t res_id,
                               std::vector<std::string>* out, std::string* error) {
  if (out == nullptr) return false;
  out->clear();
  const ResTableData* table = GetResTable(apk_path, error);
  if (table == nullptr) return false;
  const ResSimpleEntry* hit =
      FindResEntryFor(*table, res_id >> 24, (res_id >> 16) & 0xFF, res_id & 0xFFFF);
  if (hit == nullptr || !hit->complex) {
    if (error != nullptr) *error = "resId: no array value";
    return false;
  }
  for (const auto& v : hit->bag) {
    if (v.first == kTypeRefString && v.second < table->global_strings.size()) {
      out->push_back(table->global_strings[v.second]);
    } else {
      out->push_back(std::string());  // 非串元素最小面 = 空串占位
    }
  }
  return true;
}

bool LookupResourceName(const std::string& apk_path, uint32_t res_id, std::string* package,
                        std::string* type, std::string* entry, std::string* error) {
  const ResTableData* table = GetResTable(apk_path, error);
  if (table == nullptr) return false;
  const uint32_t pkg_id = res_id >> 24;
  const uint32_t type_id = (res_id >> 16) & 0xFF;
  const uint32_t entry_idx = res_id & 0xFFFF;
  const ResPkg* pkg = FindPkg(*table, pkg_id, "");
  if (pkg == nullptr) {
    if (error != nullptr) *error = "resId: package not found";
    return false;
  }
  if (package != nullptr) *package = pkg->name;
  if (type != nullptr) {
    *type = type_id >= 1 && type_id <= pkg->type_names.size() ? pkg->type_names[type_id - 1]
                                                              : std::string();
  }
  for (const auto& chunk : pkg->chunks) {
    if (chunk.type_id != type_id || entry_idx >= chunk.entries.size()) continue;
    const ResSimpleEntry& e = chunk.entries[entry_idx];
    if (!e.defined || e.key_idx >= pkg->key_names.size()) continue;
    if (entry != nullptr) *entry = pkg->key_names[e.key_idx];
    return true;
  }
  if (error != nullptr) *error = "resId: entry not found";
  return false;
}

bool LookupResourceStyle(const std::string& apk_path, uint32_t style_res_id, uint32_t* parent,
                         std::vector<StyleItem>* items, std::string* error) {
  if (items == nullptr) return false;
  items->clear();
  if (parent != nullptr) *parent = 0;
  const ResTableData* table = GetResTable(apk_path, error);
  if (table == nullptr) return false;
  const ResSimpleEntry* hit =
      FindResEntryFor(*table, style_res_id >> 24, (style_res_id >> 16) & 0xFF,
                      style_res_id & 0xFFFF);
  if (hit == nullptr || !hit->complex) {
    if (error != nullptr) *error = "style: not a style resource";
    return false;
  }
  uint32_t parent_id = hit->parent;
  for (const auto& item : hit->style_items) {
    StyleItem si;
    si.name = item.first;
    si.type = item.second.first;
    si.data = item.second.second;
    if (si.type == kTypeRefString && si.data < table->global_strings.size()) {
      si.string = table->global_strings[si.data];
    }
    // android:parent 属性项（0x01010000）等价于 map entry 的 parent 字段
    if (si.name == 0x01010000 && parent_id == 0) parent_id = si.data;
    items->push_back(std::move(si));
  }
  if (parent != nullptr) *parent = parent_id;
  return true;
}

bool LookupResourceValue(const std::string& apk_path, uint32_t res_id, uint8_t* out_type,
                         uint32_t* out_data, std::string* out_string, std::string* error) {
  const ResTableData* table = GetResTable(apk_path, error);
  if (table == nullptr) return false;
  uint32_t cur = res_id;
  for (int depth = 0; depth < 16; depth++) {
    const ResSimpleEntry* hit =
        FindResEntryFor(*table, cur >> 24, (cur >> 16) & 0xFF, cur & 0xFFFF);
    if (hit == nullptr) {
      if (error != nullptr) *error = "resId: not found";
      return false;
    }
    if (hit->complex) {
      if (error != nullptr) *error = "resId: bag value unsupported";
      return false;
    }
    uint8_t t = hit->vtype;
    uint32_t d = hit->data;
    // reference / dynamic reference：链式解析到终值（AOSP Res_value 同口径）
    if (t == 0x01 || t == 0x07) {
      cur = d;
      continue;
    }
    if (t == 0x02) {  // theme attribute（?attr/…）：无主题面，不伪造
      if (error != nullptr) *error = "resId: theme attribute unsupported";
      return false;
    }
    if (out_type != nullptr) *out_type = t;
    if (out_data != nullptr) *out_data = d;
    if (out_string != nullptr) {
      out_string->clear();
      if (t == kTypeString && d < table->global_strings.size()) {
        *out_string = table->global_strings[d];
      }
    }
    return true;
  }
  if (error != nullptr) *error = "resId: reference chain too deep";
  return false;
}

}  // namespace dexvm::session
