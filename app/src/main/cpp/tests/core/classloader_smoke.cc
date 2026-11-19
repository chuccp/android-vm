// ClassLoader/DexFile 冒烟测试（M2，docs 14 章 28.1/28.2）：
//   双亲委派顺序 / 自身 dex 查找序 / DexClassLoader 构造 intrinsic /
//   defineClassNative+loadClass / getClassNameList（multidex 去重）/
//   cookie 往返+close / InMemoryDexClassLoader / loadClass Java 回落 /
//   二次 open 新版本类更新（generation，docs 03 章 10.2）。
// 手工构造小 DEX（参考 tests/core/interpreter_smoke.cc 手法）走完整链路：
//   DexFile::Open -> LoaderRegistry -> ClassLinker::FindClass(loader,...) -> InvokeMethod。
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "session/class_loader.h"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                          \
  do {                                                            \
    if (!(cond)) {                                                \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      g_failures++;                                               \
    } else {                                                      \
      std::printf("PASS: %s\n", msg);                             \
    }                                                             \
  } while (0)

// ---- 通用小 DEX 构造（两阶段：先登记 id 表拿索引，再发射代码）----

class DexB {
 public:
  // -- 登记（返回稳定索引，可先于 Build 调用）--
  uint32_t Str(const std::string& s) {
    auto it = string_idx_.find(s);
    if (it != string_idx_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(strings_.size());
    strings_.push_back(s);
    string_idx_[s] = idx;
    return idx;
  }
  uint32_t Type(const std::string& desc) {
    auto it = type_idx_.find(desc);
    if (it != type_idx_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(types_.size());
    types_.push_back(Str(desc));
    type_idx_[desc] = idx;
    return idx;
  }
  uint32_t ProtoSig(const std::string& ret, std::vector<std::string> params) {
    std::string key = ret + "|";
    for (const auto& p : params) key += p + ",";
    auto it = proto_idx_.find(key);
    if (it != proto_idx_.end()) return it->second;
    Proto p;
    p.shorty = Str(Shorty(ret, params));
    p.ret = Type(ret);
    for (const auto& x : params) p.params.push_back(Type(x));
    uint32_t idx = static_cast<uint32_t>(protos_.size());
    protos_.push_back(std::move(p));
    proto_idx_[key] = idx;
    return idx;
  }
  uint32_t MethodId(uint32_t owner_type, uint32_t proto, const std::string& name) {
    std::string key = std::to_string(owner_type) + "|" + std::to_string(proto) + "|" + name;
    auto it = method_idx_.find(key);
    if (it != method_idx_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(methods_.size());
    methods_.push_back({static_cast<uint16_t>(owner_type), static_cast<uint16_t>(proto),
                        Str(name)});
    method_idx_[key] = idx;
    return idx;
  }

  struct BMethod {
    uint32_t method_idx;
    uint32_t access;
    uint16_t regs, ins, outs;
    std::vector<uint16_t> code;
    bool is_virtual = false;
  };
  struct BClass {
    uint32_t type_idx, super_idx, access;
    std::vector<BMethod> methods;
  };
  void AddClass(BClass c) { classes_.push_back(std::move(c)); }

  std::vector<uint8_t> Build();

 private:
  static std::string Shorty(const std::string& ret, const std::vector<std::string>& params) {
    std::string s;
    s.push_back(ret[0] == 'L' || ret[0] == '[' ? 'L' : ret[0]);
    for (const auto& p : params) s.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
    return s;
  }

  std::vector<uint8_t> b;
  void W8(uint8_t v) { b.push_back(v); }
  void W16(uint16_t v) {
    b.push_back(static_cast<uint8_t>(v));
    b.push_back(static_cast<uint8_t>(v >> 8));
  }
  void W32(uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void Uleb(uint32_t v) {
    while (true) {
      uint8_t byte = v & 0x7fu;
      v >>= 7;
      if (v != 0) byte |= 0x80u;
      W8(byte);
      if (v == 0) break;
    }
  }
  void Patch32(size_t pos, uint32_t v) {
    for (int i = 0; i < 4; i++) b[pos + i] = static_cast<uint8_t>(v >> (8 * i));
  }
  void Align4() {
    while (b.size() % 4 != 0) b.push_back(0);
  }
  size_t Pos() const { return b.size(); }

  std::vector<std::string> strings_;
  std::map<std::string, uint32_t> string_idx_;
  std::vector<uint32_t> types_;
  std::map<std::string, uint32_t> type_idx_;
  struct Proto {
    uint32_t shorty, ret;
    std::vector<uint32_t> params;
  };
  std::vector<Proto> protos_;
  std::map<std::string, uint32_t> proto_idx_;
  struct MId {
    uint16_t cls, proto;
    uint32_t name;
  };
  std::vector<MId> methods_;
  std::map<std::string, uint32_t> method_idx_;
  std::vector<BClass> classes_;
};

std::vector<uint8_t> DexB::Build() {
  b.clear();
  b.resize(0x70, 0);

  // id 表
  size_t string_ids_pos = Pos();
  for (size_t i = 0; i < strings_.size(); i++) W32(0);
  size_t type_ids_pos = Pos();
  for (uint32_t s : types_) W32(s);
  size_t proto_ids_pos = Pos();
  std::vector<size_t> proto_params_patch(protos_.size());
  for (size_t i = 0; i < protos_.size(); i++) {
    W32(protos_[i].shorty);
    W32(protos_[i].ret);
    proto_params_patch[i] = Pos();
    W32(0);
  }
  size_t method_ids_pos = Pos();
  for (const auto& m : methods_) {
    W16(m.cls);
    W16(m.proto);
    W32(m.name);
  }

  // class_defs（按 class_idx 升序）
  std::vector<size_t> order(classes_.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::stable_sort(order.begin(), order.end(),
                   [&](size_t x, size_t y) { return classes_[x].type_idx < classes_[y].type_idx; });
  size_t class_defs_pos = Pos();
  std::vector<size_t> class_data_patch(classes_.size());
  for (size_t oi = 0; oi < order.size(); oi++) {
    const BClass& c = classes_[order[oi]];
    W32(c.type_idx);
    W32(c.access);
    W32(c.super_idx);
    W32(0);
    W32(0xffffffff);
    W32(0);
    class_data_patch[order[oi]] = Pos();
    W32(0);
    W32(0);
  }

  // data：TypeList → string_data → code items → class_data → map
  uint32_t data_off = static_cast<uint32_t>(Pos());
  std::vector<size_t> typelist_offs;
  for (size_t i = 0; i < protos_.size(); i++) {
    if (protos_[i].params.empty()) continue;
    Align4();
    typelist_offs.push_back(Pos());
    W32(static_cast<uint32_t>(protos_[i].params.size()));
    for (uint32_t t : protos_[i].params) W16(static_cast<uint16_t>(t));
    Patch32(proto_params_patch[i], static_cast<uint32_t>(typelist_offs.back()));
  }
  for (size_t i = 0; i < strings_.size(); i++) {
    uint32_t off = static_cast<uint32_t>(Pos());
    Patch32(string_ids_pos + i * 4, off);
    Uleb(static_cast<uint32_t>(strings_[i].size()));
    for (char c : strings_[i]) W8(static_cast<uint8_t>(c));
    W8(0);
  }

  // code items（4 对齐）
  std::vector<std::vector<size_t>> code_offs(classes_.size());
  size_t n_code = 0;
  for (size_t ci = 0; ci < classes_.size(); ci++) {
    code_offs[ci].resize(classes_[ci].methods.size(), 0);
    for (size_t mi = 0; mi < classes_[ci].methods.size(); mi++) {
      const BMethod& m = classes_[ci].methods[mi];
      if (m.code.empty()) continue;
      Align4();
      code_offs[ci][mi] = Pos();
      n_code++;
      W16(m.regs);
      W16(m.ins);
      W16(m.outs);
      W16(0);
      W32(0);
      W32(static_cast<uint32_t>(m.code.size()));
      for (uint16_t u : m.code) W16(u);
    }
  }

  // class_data
  for (size_t ci = 0; ci < classes_.size(); ci++) {
    Align4();
    Patch32(class_data_patch[ci], static_cast<uint32_t>(Pos()));
    std::vector<std::pair<uint32_t, size_t>> directs, virtuals;  // method_idx, method 序
    for (size_t mi = 0; mi < classes_[ci].methods.size(); mi++) {
      auto& slot = classes_[ci].methods[mi].is_virtual ? virtuals : directs;
      slot.push_back({classes_[ci].methods[mi].method_idx, mi});
    }
    auto by_idx = [](const auto& x, const auto& y) { return x.first < y.first; };
    std::stable_sort(directs.begin(), directs.end(), by_idx);
    std::stable_sort(virtuals.begin(), virtuals.end(), by_idx);
    Uleb(0);
    Uleb(0);
    Uleb(static_cast<uint32_t>(directs.size()));
    Uleb(static_cast<uint32_t>(virtuals.size()));
    for (const auto& list : {directs, virtuals}) {
      uint32_t prev = 0;
      bool first = true;
      for (const auto& e : list) {
        Uleb(first ? e.first : e.first - prev);
        first = false;
        prev = e.first;
        Uleb(classes_[ci].methods[e.second].access);
        Uleb(static_cast<uint32_t>(code_offs[ci][e.second]));
      }
    }
  }

  // map_list
  Align4();
  uint32_t map_off = static_cast<uint32_t>(Pos());
  std::vector<std::pair<uint16_t, std::pair<uint32_t, uint32_t>>> maps;
  auto MapItem = [&](uint16_t type, uint32_t size, uint32_t off) {
    maps.push_back({type, {size, off}});
  };
  MapItem(0x0000, 1, 0);
  MapItem(0x0001, static_cast<uint32_t>(strings_.size()), string_ids_pos);
  MapItem(0x0002, static_cast<uint32_t>(types_.size()), type_ids_pos);
  MapItem(0x0003, static_cast<uint32_t>(protos_.size()), proto_ids_pos);
  MapItem(0x0005, static_cast<uint32_t>(methods_.size()), method_ids_pos);
  MapItem(0x0006, static_cast<uint32_t>(classes_.size()), class_defs_pos);
  if (!typelist_offs.empty()) {
    MapItem(0x1001, static_cast<uint32_t>(typelist_offs.size()),
            static_cast<uint32_t>(typelist_offs.front()));
  }
  if (n_code > 0) {
    uint32_t first_code = 0;
    for (const auto& v : code_offs) {
      for (size_t off : v) {
        if (off != 0) {
          first_code = static_cast<uint32_t>(off);
          break;
        }
      }
      if (first_code != 0) break;
    }
    MapItem(0x2001, static_cast<uint32_t>(n_code), first_code);
  }
  MapItem(0x1000, 1, map_off);
  W32(static_cast<uint32_t>(maps.size()));
  for (const auto& m : maps) {
    W16(m.first);
    W16(0);
    W32(m.second.first);
    W32(m.second.second);
  }

  // header
  std::memcpy(b.data(), "dex\n035\0", 8);
  uint32_t file_size = static_cast<uint32_t>(b.size());
  Patch32(0x20, file_size);
  Patch32(0x24, 0x70);
  Patch32(0x28, 0x12345678);
  Patch32(0x34, map_off);
  Patch32(0x38, static_cast<uint32_t>(strings_.size()));
  Patch32(0x3c, string_ids_pos);
  Patch32(0x40, static_cast<uint32_t>(types_.size()));
  Patch32(0x44, type_ids_pos);
  Patch32(0x48, static_cast<uint32_t>(protos_.size()));
  Patch32(0x4c, proto_ids_pos);
  Patch32(0x50, 0);
  Patch32(0x54, 0);
  Patch32(0x58, static_cast<uint32_t>(methods_.size()));
  Patch32(0x5c, method_ids_pos);
  Patch32(0x60, static_cast<uint32_t>(classes_.size()));
  Patch32(0x64, class_defs_pos);
  Patch32(0x68, file_size - data_off);
  Patch32(0x6c, data_off);
  return b;
}

// ---- 指令编码 ----

std::vector<uint16_t> Cat(std::initializer_list<std::vector<uint16_t>> parts) {
  std::vector<uint16_t> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

// invoke-static/direct 35c：word0 = count<<12 | G<<8 | op
std::vector<uint16_t> Invoke35c(uint8_t op, uint32_t method_idx,
                                std::initializer_list<uint32_t> regs) {
  std::vector<uint32_t> r(regs);
  std::vector<uint16_t> out = {static_cast<uint16_t>((r.size() << 12) | op),
                               static_cast<uint16_t>(method_idx), 0};
  uint32_t list = 0;
  for (size_t i = 0; i < r.size() && i < 4; i++) list |= (r[i] & 0xfu) << (4 * i);
  out[2] = static_cast<uint16_t>(list);
  if (r.size() == 5) out[0] |= static_cast<uint16_t>((r[4] & 0xfu) << 8);
  return out;
}

// who()I：const/16 v0, value ; return v0
std::vector<uint16_t> CodeConstReturn(int32_t value) {
  return {static_cast<uint16_t>((0 << 8) | 0x13), static_cast<uint16_t>(value),
          static_cast<uint16_t>((0 << 8) | 0x0f)};
}

// ---- 测试装配 ----

using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VMState;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

VMethod* FindMethod(VClass* klass, const char* name, size_t param_count) {
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.param_types.size() == param_count) return &m;
  }
  for (auto& m : klass->virtual_methods) {
    if (m.name == name && m.param_types.size() == param_count) return &m;
  }
  return nullptr;
}

VMethod* FindCtor(VClass* klass, size_t param_count) {
  for (auto& m : klass->direct_methods) {
    if (m.name == "<init>" && m.param_types.size() == param_count) return &m;
  }
  return nullptr;
}

Value StrVal(ClassLinker& linker, const std::string& s) {
  std::u16string u(s.begin(), s.end());
  Value v{};
  v.ref = linker.AllocString(u);
  return v;
}

Value RefVal(void* p) {
  Value v{};
  v.ref = reinterpret_cast<ObjHeader*>(p);
  return v;
}

Value NullVal() {
  Value v{};
  v.ref = nullptr;
  return v;
}

Value LongVal(int64_t x) {
  Value v{};
  v.l = x;
  return v;
}

int32_t InvokeStaticI(VMState& state, VClass* klass, const char* name) {
  VMethod* m = FindMethod(klass, name, 0);
  if (m == nullptr) return -0x7fffffff;
  Value r{};
  if (!InvokeMethod(state, m, nullptr, 0, &r)) return -0x7fffffff;
  return r.i;
}

// new + <init>（intrinsic 构造器或 DEX 构造器通用）
VObject* NewObjectInitKlass(VMState& state, ClassLinker& linker, VClass* k,
                            std::initializer_list<Value> ctor_args) {
  if (k == nullptr) return nullptr;
  VObject* obj = linker.AllocObject(k);
  if (obj == nullptr) return nullptr;
  VMethod* init = FindCtor(k, ctor_args.size());
  if (init == nullptr) return nullptr;
  std::vector<Value> args;
  args.push_back(RefVal(obj));
  for (const Value& v : ctor_args) args.push_back(v);
  Value r{};
  if (!InvokeMethod(state, init, args.data(), args.size(), &r)) return nullptr;
  return obj;
}

VObject* NewObjectInit(VMState& state, ClassLinker& linker, const char* desc,
                       std::initializer_list<Value> ctor_args) {
  return NewObjectInitKlass(state, linker, linker.FindClass(desc), ctor_args);
}

std::string TmpPath(const char* name) {
  const char* dir = std::getenv("TMPDIR");
  std::string p = dir != nullptr ? dir : "/tmp";
  if (!p.empty() && p.back() != '/') p.push_back('/');
  return p + name;
}

bool WriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(bytes.data()),
          static_cast<std::streamsize>(bytes.size()));
  return f.good();
}

VArray* MakeByteArray(ClassLinker& linker, const std::vector<uint8_t>& bytes) {
  VClass* arr_class = linker.FindArrayClass(linker.PrimitiveClass('B'));
  VArray* arr = linker.AllocArray(arr_class, static_cast<int32_t>(bytes.size()));
  for (size_t i = 0; i < bytes.size(); i++) {
    uint64_t slot = bytes[i];
    std::memcpy(arr->data() + i * 8, &slot, sizeof(slot));
  }
  return arr;
}

Value MakeByteArrayVal(ClassLinker& linker, const std::vector<uint8_t>& bytes) {
  return RefVal(MakeByteArray(linker, bytes));
}

Value MakeByteArrayArrayVal(ClassLinker& linker,
                            const std::vector<std::vector<uint8_t>>& parts) {
  VClass* inner = linker.FindArrayClass(linker.PrimitiveClass('B'));
  VClass* outer = linker.FindArrayClass(inner);
  VArray* arr = linker.AllocArray(outer, static_cast<int32_t>(parts.size()));
  for (size_t i = 0; i < parts.size(); i++) {
    uint64_t slot = reinterpret_cast<uintptr_t>(MakeByteArray(linker, parts[i]));
    std::memcpy(arr->data() + i * 8, &slot, sizeof(slot));
  }
  return RefVal(arr);
}

// cookie：DexFile.openDexFileNative([B)J / ([[B)J
int64_t OpenCookieBytes(VMState& state, ClassLinker& linker,
                        const std::vector<std::vector<uint8_t>>& parts) {
  VClass* dex_file = linker.FindClass("Ldalvik/system/DexFile;");
  bool single = parts.size() == 1;
  VMethod* open = nullptr;
  for (auto& m : dex_file->direct_methods) {
    if (m.name == "openDexFileNative" && m.param_types.size() == 1 &&
        m.param_types[0] == (single ? "[B" : "[[B")) {
      open = &m;
      break;
    }
  }
  if (open == nullptr) return 0;
  Value arg = single ? MakeByteArrayVal(linker, parts[0])
                     : MakeByteArrayArrayVal(linker, parts);
  Value r{};
  if (!InvokeMethod(state, open, &arg, 1, &r)) return 0;
  return r.l;
}

Value CallStatic(VMState& state, ClassLinker& linker, const char* klass_desc, const char* name,
                 std::initializer_list<const char*> sig, std::initializer_list<Value> args) {
  VClass* k = linker.FindClass(klass_desc);
  std::vector<std::string> want;
  for (const char* s : sig) want.push_back(s);
  for (auto& m : k->direct_methods) {
    if (m.name == name && m.param_types == want) {
      std::vector<Value> a(args);
      Value r{};
      InvokeMethod(state, &m, a.data(), a.size(), &r);
      return r;
    }
  }
  return Value{};
}

std::vector<std::string> GetClassNameList(VMState& state, ClassLinker& linker, int64_t cookie) {
  Value c{};
  c.l = cookie;
  Value r = CallStatic(state, linker, "Ldalvik/system/DexFile;", "getClassNameList", {"J"}, {c});
  std::vector<std::string> out;
  VArray* arr = reinterpret_cast<VArray*>(r.ref);
  if (arr == nullptr) return out;
  for (int32_t i = 0; i < arr->length; i++) {
    uint64_t slot;
    std::memcpy(&slot, arr->data() + i * 8, sizeof(slot));
    VString* s = reinterpret_cast<VString*>(static_cast<uintptr_t>(slot));
    out.push_back(s != nullptr ? s->ToModifiedUtf8() : "<null>");
  }
  return out;
}

// ---- DEX 规格 ----

// 单类单静态方法 who()/version() 型 DEX
std::vector<uint8_t> BuildWhoDex(const std::string& class_desc, const char* method_name,
                                 int32_t value) {
  DexB d;
  uint32_t t = d.Type(class_desc);
  uint32_t t_obj = d.Type("Ljava/lang/Object;");
  uint32_t p_i = d.ProtoSig("I", {});
  uint32_t m = d.MethodId(t, p_i, method_name);
  DexB::BClass c;
  c.type_idx = t;
  c.super_idx = t_obj;
  c.access = 0x1;
  c.methods.push_back({m, 0x9, 1, 0, 0, CodeConstReturn(value), false});
  d.AddClass(std::move(c));
  return d.Build();
}

// A 用：一个 DEX 里两个类（Dup + Only）
std::vector<uint8_t> BuildTwoWhoDex(const std::string& a_desc, const char* a_method, int32_t a_val,
                                    const std::string& b_desc, const char* b_method,
                                    int32_t b_val) {
  DexB d;
  uint32_t t_obj = d.Type("Ljava/lang/Object;");
  uint32_t p_i = d.ProtoSig("I", {});
  auto make = [&](const std::string& desc, const char* name, int32_t val) {
    uint32_t t = d.Type(desc);
    uint32_t m = d.MethodId(t, p_i, name);
    DexB::BClass c;
    c.type_idx = t;
    c.super_idx = t_obj;
    c.access = 0x1;
    c.methods.push_back({m, 0x9, 1, 0, 0, CodeConstReturn(val), false});
    d.AddClass(std::move(c));
  };
  make(a_desc, a_method, a_val);
  make(b_desc, b_method, b_val);
  return d.Build();
}

// G 用：Found + CustomLoader/NulLoader/MisLoader（Java loadClass 回落）
std::vector<uint8_t> BuildFallbackDex() {
  DexB d;
  uint32_t t_obj = d.Type("Ljava/lang/Object;");
  uint32_t t_cl = d.Type("Ljava/lang/ClassLoader;");
  d.Type("Ljava/lang/Class;");
  uint32_t t_str = d.Type("Ljava/lang/String;");
  uint32_t t_found = d.Type("Ltest/Found;");
  uint32_t t_custom = d.Type("Ltest/CustomLoader;");
  uint32_t t_nul = d.Type("Ltest/NulLoader;");
  uint32_t t_mis = d.Type("Ltest/MisLoader;");
  uint32_t p_void = d.ProtoSig("V", {});
  uint32_t p_i = d.ProtoSig("I", {});
  uint32_t p_cls_str = d.ProtoSig("Ljava/lang/Class;", {"Ljava/lang/String;"});
  uint32_t m_cl_init = d.MethodId(t_cl, p_void, "<init>");

  // Ltest/Found;.tag()I == 7
  {
    DexB::BClass c;
    c.type_idx = t_found;
    c.super_idx = t_obj;
    c.access = 0x1;
    c.methods.push_back({d.MethodId(t_found, p_i, "tag"), 0x9, 1, 0, 0, CodeConstReturn(7), false});
    d.AddClass(std::move(c));
  }
  // 三个 loader：<init> = invoke-direct ClassLoader.<init>()V + return-void；
  // loadClass(String)Class = const-class v0, Found ; return-object v0（或 null）
  for (uint32_t t : {t_custom, t_nul, t_mis}) {
    DexB::BClass c;
    c.type_idx = t;
    c.super_idx = t_cl;
    c.access = 0x1;
    std::vector<uint16_t> init_code = Cat({Invoke35c(0x70, m_cl_init, {0}), {0x000E}});
    c.methods.push_back({d.MethodId(t, p_void, "<init>"), 0x10001, 1, 1, 0, init_code, false});
    std::vector<uint16_t> load_code;
    if (t == t_nul) {
      load_code = {0x0012, 0x0011};  // const/4 v0, 0 ; return-object v0
    } else {
      load_code = {static_cast<uint16_t>((0 << 8) | 0x1c), static_cast<uint16_t>(t_found),
                   0x0011};  // const-class v0, Found ; return-object v0
    }
    c.methods.push_back(
        {d.MethodId(t, p_cls_str, "loadClass"), 0x1, 3, 2, 0, load_code, true});
    (void)t_str;
    d.AddClass(std::move(c));
  }
  return d.Build();
}

}  // namespace

int main() {
  std::printf("== classloader smoke ==\n");

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  // ================= G：loadClass Java 回落（docs 12 章 22.1）=================
  {
    std::vector<uint8_t> fb = BuildFallbackDex();
    VObject* ls = NewObjectInit(state, linker, "Ldalvik/system/InMemoryDexClassLoader;",
                                {MakeByteArrayVal(linker, fb), NullVal()});
    CHECK(ls != nullptr, "InMemoryDexClassLoader ctor (fallback dex)");
    VClass* custom_cls = linker.FindClass(&state, ls, "Ltest/CustomLoader;");
    CHECK(custom_cls != nullptr, "define CustomLoader via loader chain");
    VClass* nul_cls = linker.FindClass(&state, ls, "Ltest/NulLoader;");
    VClass* mis_cls = linker.FindClass(&state, ls, "Ltest/MisLoader;");
    CHECK(nul_cls != nullptr && mis_cls != nullptr, "define Nul/Mis loaders via chain");

    VObject* lc = NewObjectInitKlass(state, linker, custom_cls, {});
    CHECK(lc != nullptr, "CustomLoader instance (ClassLoader.<init> intrinsic)");
    CHECK(linker.loaders().Lookup(lc) != nullptr, "CustomLoader bound to LoaderState");

    // 成功路径：链上找不到 -> 解释器调用 Java loadClass -> const-class Found
    VClass* got = linker.FindClass(&state, lc, "Ltest/Found;");
    CHECK(got != nullptr && got->descriptor == "Ltest/Found;", "Java loadClass fallback hit");
    CHECK(got != nullptr && got->class_loader == ls, "fallback class defined by dex loader");
    CHECK(InvokeStaticI(state, got, "tag") == 7, "Found.tag() == 7");

    // broken loader：loadClass 返回 null -> NPE（Dalvik 兼容）
    VObject* lc2 = NewObjectInitKlass(state, linker, nul_cls, {});
    CHECK(lc2 != nullptr, "NulLoader instance");
    VClass* r = linker.FindClass(&state, lc2, "Lother/Ghost;");
    CHECK(r == nullptr && state.HasPendingException(), "null loadClass -> pending");
    ObjHeader* ex = state.pending_exception;
    CHECK(ex != nullptr && ex->klass != nullptr &&
              ex->klass->descriptor == "Ljava/lang/NullPointerException;",
          "broken loader throws NPE");
    state.ClearException();

    // descriptor 不匹配 -> NoClassDefFoundError
    VObject* lc3 = NewObjectInitKlass(state, linker, mis_cls, {});
    CHECK(lc3 != nullptr, "MisLoader instance");
    r = linker.FindClass(&state, lc3, "Lother/Ghost2;");
    CHECK(r == nullptr && state.HasPendingException(), "mismatch loadClass -> pending");
    ex = state.pending_exception;
    CHECK(ex != nullptr && ex->klass != nullptr &&
              ex->klass->descriptor == "Ljava/lang/NoClassDefFoundError;",
          "mismatch throws NoClassDefFoundError");
    state.ClearException();
  }

  // ================= A：双亲委派顺序 =================
  {
    std::vector<uint8_t> dex_p = BuildWhoDex("Ltest/Dup;", "who", 1);
    std::vector<uint8_t> dex_c =
        BuildTwoWhoDex("Ltest/Dup;", "who", 2, "Ltest/Only;", "who", 20);
    std::string file_p = TmpPath("cl_parent.dex");
    std::string file_c = TmpPath("cl_child.dex");
    CHECK(WriteFile(file_p, dex_p) && WriteFile(file_c, dex_c), "write parent/child dex");
    VObject* p = NewObjectInit(state, linker, "Ldalvik/system/PathClassLoader;",
                               {StrVal(linker, file_p), NullVal()});
    CHECK(p != nullptr, "PathClassLoader ctor (parent)");
    VObject* c = NewObjectInit(state, linker, "Ldalvik/system/PathClassLoader;",
                               {StrVal(linker, file_c), NullVal(), RefVal(p)});
    CHECK(c != nullptr, "PathClassLoader ctor (child)");
    CHECK(linker.loaders().Lookup(c)->parent == linker.loaders().Lookup(p),
          "child LoaderState parent link");

    VClass* dup = linker.FindClass(&state, c, "Ltest/Dup;");
    CHECK(dup != nullptr && InvokeStaticI(state, dup, "who") == 1,
          "parent delegation wins (who==1)");
    CHECK(dup != nullptr && dup->class_loader == p, "Dup defined by parent loader");
    VClass* only = linker.FindClass(&state, c, "Ltest/Only;");
    CHECK(only != nullptr && InvokeStaticI(state, only, "who") == 20,
          "own dex found via chain (who==20)");
    CHECK(only != nullptr && only->class_loader == c, "Only defined by child loader");
    VClass* dup2 = linker.FindClass(&state, p, "Ltest/Dup;");
    CHECK(dup2 == dup, "same class via parent lookup");
  }

  // ================= B：loader 自身 dex 查找顺序 =================
  {
    std::vector<uint8_t> s1 = BuildWhoDex("Ltest/Seq;", "who", 1);
    std::vector<uint8_t> s2 = BuildWhoDex("Ltest/Seq;", "who", 2);
    std::string f1 = TmpPath("cl_seq1.dex");
    std::string f2 = TmpPath("cl_seq2.dex");
    CHECK(WriteFile(f1, s1) && WriteFile(f2, s2), "write seq dexes");
    VObject* s = NewObjectInit(state, linker, "Ldalvik/system/DexClassLoader;",
                               {StrVal(linker, f1 + ":" + f2), NullVal(), NullVal(), NullVal()});
    CHECK(s != nullptr, "DexClassLoader ctor (two dex in dexPath)");
    CHECK(linker.loaders().Lookup(s)->dex_files.size() == 2, "DexPathList has 2 DexFile");
    VClass* seq = linker.FindClass(&state, s, "Ltest/Seq;");
    CHECK(seq != nullptr && InvokeStaticI(state, seq, "who") == 1,
          "first dex in list wins (who==1)");
  }

  // ================= C：DexClassLoader 构造 intrinsic（libPath 元数据）=================
  {
    std::vector<uint8_t> fc = BuildWhoDex("Ltest/FileCls;", "who", 5);
    std::string f = TmpPath("cl_filecls.dex");
    CHECK(WriteFile(f, fc), "write filecls dex");
    VObject* o = NewObjectInit(state, linker, "Ldalvik/system/DexClassLoader;",
                               {StrVal(linker, f), NullVal(), StrVal(linker, "libX"), NullVal()});
    CHECK(o != nullptr, "DexClassLoader ctor intrinsic");
    CHECK(linker.loaders().Lookup(o)->native_library_path == "libX", "nativeLibraryPath set");
    VClass* k = linker.FindClass(&state, o, "Ltest/FileCls;");
    CHECK(k != nullptr && InvokeStaticI(state, k, "who") == 5, "FileCls via dexPath file");
  }

  // ================= D：defineClassNative + loadClass + cookie 往返/close =================
  {
    std::vector<uint8_t> cc = BuildWhoDex("Ltest/CookieCls;", "who", 6);
    VObject* loader = NewObjectInit(state, linker, "Ljava/lang/ClassLoader;", {});
    CHECK(loader != nullptr, "ClassLoader instance");
    int64_t cookie = OpenCookieBytes(state, linker, {cc});
    CHECK(cookie != 0, "openDexFileNative([B) -> cookie");

    std::vector<std::string> names = GetClassNameList(state, linker, cookie);
    CHECK(names.size() == 1 && names[0] == "test.CookieCls", "getClassNameList dot name");

    Value cls = CallStatic(state, linker, "Ldalvik/system/DexFile;", "defineClassNative",
                           {"Ljava/lang/String;", "Ljava/lang/ClassLoader;", "J",
                            "Ldalvik/system/DexFile;"},
                           {StrVal(linker, "test.CookieCls"), RefVal(loader), LongVal(cookie),
                            NullVal()});
    CHECK(cls.ref != nullptr, "defineClassNative defines class");
    Value cls2 = CallStatic(state, linker, "Ldalvik/system/DexFile;", "loadClass",
                            {"Ljava/lang/String;", "Ljava/lang/ClassLoader;"},
                            {StrVal(linker, "test.CookieCls"), RefVal(loader)});
    CHECK(cls2.ref == cls.ref, "loadClass returns same class");

    Value c1{};
    c1.l = cookie;
    Value closed = CallStatic(state, linker, "Ldalvik/system/DexFile;", "closeDexFile", {"J"},
                              {c1});
    CHECK(closed.i == 0, "closeDexFile false while registered");
    for (dexvm::dex::DexFile* dex : linker.loaders().GetDexFiles(cookie)) {
      linker.loaders().DetachDex(dex);
    }
    closed = CallStatic(state, linker, "Ldalvik/system/DexFile;", "closeDexFile", {"J"}, {c1});
    CHECK(closed.i == 1, "closeDexFile true after detach (all deleted)");
    names = GetClassNameList(state, linker, cookie);
    CHECK(names.empty(), "closed cookie has no class names");
  }

  // ================= E：getClassNameList multidex 去重 =================
  {
    std::vector<uint8_t> a =
        BuildTwoWhoDex("Ltest/Dup2;", "who", 3, "Ltest/Extra;", "who", 4);
    std::vector<uint8_t> b = BuildWhoDex("Ltest/Dup2;", "who", 3);
    int64_t cookie = OpenCookieBytes(state, linker, {a, b});
    CHECK(cookie != 0, "openDexFileNative([[B) -> cookie");
    std::vector<std::string> names = GetClassNameList(state, linker, cookie);
    CHECK(names.size() == 2 && names[0] == "test.Dup2" && names[1] == "test.Extra",
          "multidex dedup + dot names");
    Value c{};
    c.l = cookie;
    Value closed = CallStatic(state, linker, "Ldalvik/system/DexFile;", "closeDexFile", {"J"},
                              {c});
    CHECK(closed.i == 1, "unregistered cookie closes clean");
  }

  // ================= F：InMemoryDexClassLoader（byte[] / byte[][]）=================
  {
    std::vector<uint8_t> m1 = BuildWhoDex("Ltest/MemOnly;", "who", 21);
    VObject* im = NewObjectInit(state, linker, "Ldalvik/system/InMemoryDexClassLoader;",
                                {MakeByteArrayVal(linker, m1), NullVal()});
    CHECK(im != nullptr, "InMemoryDexClassLoader(byte[]) ctor");
    VClass* k = linker.FindClass(&state, im, "Ltest/MemOnly;");
    CHECK(k != nullptr && InvokeStaticI(state, k, "who") == 21, "MemOnly via byte[] loader");

    std::vector<uint8_t> m2a = BuildWhoDex("Ltest/Mem2a;", "who", 22);
    std::vector<uint8_t> m2b = BuildWhoDex("Ltest/Mem2b;", "who", 23);
    VObject* im2 = NewObjectInit(state, linker, "Ldalvik/system/InMemoryDexClassLoader;",
                                 {MakeByteArrayArrayVal(linker, {m2a, m2b}), NullVal()});
    CHECK(im2 != nullptr, "InMemoryDexClassLoader(byte[][]) ctor");
    VClass* ka = linker.FindClass(&state, im2, "Ltest/Mem2a;");
    VClass* kb = linker.FindClass(&state, im2, "Ltest/Mem2b;");
    CHECK(ka != nullptr && InvokeStaticI(state, ka, "who") == 22, "Mem2a via byte[][] loader");
    CHECK(kb != nullptr && InvokeStaticI(state, kb, "who") == 23, "Mem2b via byte[][] loader");
  }

  // ================= H：二次 open 新版本类更新（generation，docs 03 章 10.2）=================
  {
    std::string f = TmpPath("cl_gen.dex");
    std::vector<uint8_t> v1 = BuildWhoDex("Ltest/Gen;", "version", 1);
    CHECK(WriteFile(f, v1), "write gen v1");
    VObject* g1 = NewObjectInit(state, linker, "Ldalvik/system/DexClassLoader;",
                                {StrVal(linker, f), NullVal(), NullVal(), NullVal()});
    VClass* gen1 = linker.FindClass(&state, g1, "Ltest/Gen;");
    CHECK(gen1 != nullptr && InvokeStaticI(state, gen1, "version") == 1, "Gen v1 loaded");

    std::vector<uint8_t> v2 = BuildWhoDex("Ltest/Gen;", "version", 2);
    CHECK(WriteFile(f, v2), "overwrite gen v2 (加固器再生成)");
    VObject* g2 = NewObjectInit(state, linker, "Ldalvik/system/DexClassLoader;",
                                {StrVal(linker, f), NullVal(), NullVal(), NullVal()});
    VClass* gen2 = linker.FindClass(&state, g2, "Ltest/Gen;");
    CHECK(gen2 != nullptr && InvokeStaticI(state, gen2, "version") == 2, "Gen v2 redefined");
    VClass* gen1b = linker.FindClass(&state, g1, "Ltest/Gen;");
    CHECK(gen1b == gen2 && InvokeStaticI(state, gen1b, "version") == 2,
          "old loader sees regenerated dex");

    Value cookie_v = CallStatic(state, linker, "Ldalvik/system/DexFile;", "openDexFile",
                                {"Ljava/lang/String;"}, {StrVal(linker, f)});
    CHECK(cookie_v.l != 0, "openDexFile convenience entry");
    const dexvm::session::LoaderRegistry::DexEntry* entry =
        linker.loaders().GetEntry(static_cast<uint64_t>(cookie_v.l));
    CHECK(entry != nullptr && entry->generation == 2, "dex generation == 2 after re-open");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
