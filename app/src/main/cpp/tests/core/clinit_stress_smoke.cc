// clinit 深递归链压力测试（M4.3 SIGSEGV 复现面）：
//   30-50 层类初始化互相引用（C0.<clinit>→C1.touch()→C1.<clinit>→…）+
//   每层 check-cast/instance-of/宽值字段（long/double 静态 + 编码静态值）/
//   静态引用 + move-result/move-result-wide/move-result-object 混合 +
//   StringFactory（new String(char[])）+ move-exception + 线程字段面。
//
// 用法：clinit_stress_smoke <clinit_stress.dex>
//   dex 由 tools/clinit_stress_tests.sh 以 javac+d8 生成（真实 d8 字节码形态）。
// 阶段：
//   1. run()        —— 深递归 clinit 链（40 层）
//   2. sum()        —— 每层 check() 结果核对（确定性期望值）
//   3. verify()     —— 初始化后静态引用/宽值不变式
//   4. GC 后再 verify（保守扫描帧 + 静态引用存活）
//   5. wideMix()×2  —— 宽值 move-result 链确定性
//   6. scan(pool)   —— aget-object/check-cast/instance-of/d() 崩溃点形态
//   7. alias()      —— StringFactory 别名回写
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/gc.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"

namespace {

using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

int g_failures = 0;

#define CHECK(cond, msg)                                              \
  do {                                                                \
    if (!(cond)) {                                                    \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__);     \
      g_failures++;                                                   \
    } else {                                                          \
      std::printf("PASS: %s\n", msg);                                 \
    }                                                                 \
  } while (0)

std::vector<uint8_t> ReadFile(const char* path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),
                              std::istreambuf_iterator<char>());
}

void DumpDiag(VMState& state) {
  if (!state.HasPendingException()) return;
  ObjHeader* ex = state.pending_exception;
  std::printf("  pending: %s\n",
              (ex != nullptr && ex->klass != nullptr) ? ex->klass->descriptor.c_str()
                                                      : "(null klass)");
  if (ex != nullptr && ex->klass != nullptr) {
    VObject* o = reinterpret_cast<VObject*>(ex);
    for (VClass* k = o->klass; k != nullptr; k = k->super_class) {
      bool done = false;
      for (const auto& f : k->instance_fields) {
        if (f.name == "detailMessage") {
          ObjHeader* s = nullptr;
          std::memcpy(&s, o->field_data() + f.offset, sizeof(s));
          if (s != nullptr) {
            std::printf("  message: %s\n", static_cast<VString*>(s)->ToModifiedUtf8().c_str());
          }
          done = true;
          break;
        }
      }
      if (done) break;
    }
  }
  for (const std::string& s : state.last_throw_backtrace) std::printf("    at %s\n", s.c_str());
}

VMethod* FindStaticM(VClass* k, const char* name) {
  if (k == nullptr) return nullptr;
  for (auto& m : k->direct_methods) {
    if (m.name == name && m.IsStatic()) return &m;
  }
  return nullptr;
}

// 无参静态调用；成功返回 true 并出参 result
bool CallStatic(VMState& state, VClass* k, const char* name, Value* out) {
  VMethod* m = FindStaticM(k, name);
  if (m == nullptr) {
    std::printf("FAIL: method %s not found\n", name);
    g_failures++;
    return false;
  }
  Value r{};
  if (!dexvm::runtime::InvokeMethod(state, m, nullptr, 0, &r)) {
    std::printf("FAIL: %s() threw\n", name);
    DumpDiag(state);
    state.ClearException();
    g_failures++;
    return false;
  }
  *out = r;
  return true;
}

bool CallStaticI(VMState& state, VClass* k, const char* name, int32_t* out) {
  Value r{};
  if (!CallStatic(state, k, name, &r)) return false;
  *out = r.i;
  return true;
}

// scan(Object[])：单参静态调用
bool CallStaticRef1(VMState& state, VClass* k, const char* name, ObjHeader* arg, Value* out) {
  VMethod* m = FindStaticM(k, name);
  if (m == nullptr) {
    std::printf("FAIL: method %s not found\n", name);
    g_failures++;
    return false;
  }
  Value a{};
  a.ref = arg;
  Value r{};
  if (!dexvm::runtime::InvokeMethod(state, m, &a, 1, &r)) {
    std::printf("FAIL: %s(ref) threw\n", name);
    DumpDiag(state);
    state.ClearException();
    g_failures++;
    return false;
  }
  *out = r;
  return true;
}

constexpr int kLevels = 40;

// =====================================================================
// 手工 DEX 堆叠（fixture.dex 模式 fallback）：d8 不会自然产出的对抗形态
//   - StringFactory 别名回写：this 散布 3 个 vreg
//   - move-wide 重叠对（dest 对与 src 对共享 vreg）
//   - wide 对槽死亡后双 vreg 复用为对象 + check-cast
//   - 5 词 invoke 且 wide 占 word0/word2（35c 字映射）
//   - 10 层 <clinit> 手工链（宽值/静态引用/move-result 混合）
// =====================================================================

class DexB {
 public:
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
    std::string shorty;
    shorty.push_back(ret[0] == 'L' || ret[0] == '[' ? 'L' : ret[0]);
    for (const auto& p : params) shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
    uint32_t idx = static_cast<uint32_t>(protos_.size());
    protos_.push_back({Str(shorty), Type(ret), [&](const std::vector<std::string>& ps) {
                         std::vector<uint32_t> t;
                         for (const auto& p : ps) t.push_back(Type(p));
                         return t;
                       }(params)});
    proto_idx_[key] = idx;
    return idx;
  }
  uint32_t Method(uint32_t owner, uint32_t proto, const std::string& name) {
    std::string key = std::to_string(owner) + "|" + std::to_string(proto) + "|" + name;
    auto it = method_idx_.find(key);
    if (it != method_idx_.end()) return it->second;
    uint32_t idx = static_cast<uint32_t>(methods_.size());
    methods_.push_back({static_cast<uint16_t>(owner), static_cast<uint16_t>(proto), Str(name)});
    method_idx_[key] = idx;
    return idx;
  }

  struct BMethod {
    uint32_t method_idx;
    uint32_t access;
    uint16_t regs, ins;
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
      W16(2);  // outs
      W16(0);
      W32(0);
      W32(static_cast<uint32_t>(m.code.size()));
      for (uint16_t u : m.code) W16(u);
    }
  }
  for (size_t ci = 0; ci < classes_.size(); ci++) {
    Align4();
    Patch32(class_data_patch[ci], static_cast<uint32_t>(Pos()));
    std::vector<std::pair<uint32_t, size_t>> directs, virtuals;
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
namespace ins {

std::vector<uint16_t> I11x(uint8_t op, uint8_t aa) { return {static_cast<uint16_t>((aa << 8) | op)}; }
std::vector<uint16_t> I12x(uint8_t op, uint8_t a, uint8_t bb) {
  return {static_cast<uint16_t>((bb << 12) | (a << 8) | op)};
}
std::vector<uint16_t> I21c(uint8_t op, uint8_t aa, uint16_t idx) {
  return {static_cast<uint16_t>((aa << 8) | op), idx};
}
std::vector<uint16_t> I22c(uint8_t op, uint8_t a, uint8_t bb, uint16_t idx) {
  return {static_cast<uint16_t>((bb << 12) | (a << 8) | op), idx};
}
std::vector<uint16_t> I31i(uint8_t op, uint8_t aa, uint32_t val) {
  return {static_cast<uint16_t>((aa << 8) | op), static_cast<uint16_t>(val),
          static_cast<uint16_t>(val >> 16)};
}
// 51l：const-wide（64 位立即数，5 code units）
std::vector<uint16_t> I51l(uint8_t op, uint8_t aa, uint64_t val) {
  return {static_cast<uint16_t>((aa << 8) | op), static_cast<uint16_t>(val),
          static_cast<uint16_t>(val >> 16), static_cast<uint16_t>(val >> 32),
          static_cast<uint16_t>(val >> 48)};
}
std::vector<uint16_t> I23x(uint8_t op, uint8_t aa, uint8_t bb, uint8_t cc) {
  return {static_cast<uint16_t>((aa << 8) | op), static_cast<uint16_t>((cc << 8) | bb)};
}
std::vector<uint16_t> Invoke35c(uint8_t op, uint32_t method_idx, std::vector<uint32_t> regs) {
  std::vector<uint16_t> out = {static_cast<uint16_t>((regs.size() << 12) | op),
                               static_cast<uint16_t>(method_idx), 0};
  uint32_t list = 0;
  for (size_t i = 0; i < regs.size() && i < 4; i++) list |= (regs[i] & 0xfu) << (4 * i);
  out[2] = static_cast<uint16_t>(list);
  if (regs.size() == 5) out[0] |= static_cast<uint16_t>((regs[4] & 0xfu) << 8);
  return out;
}
std::vector<uint16_t> Cat(std::initializer_list<std::vector<uint16_t>> parts) {
  std::vector<uint16_t> out;
  for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
  return out;
}

}  // namespace ins

// 对抗 DEX：Ltest/U; 工具 + Ltest/C0..C9 clinit 链 + Ltest/Drv;.boot()I
std::vector<uint8_t> BuildAdversarialDex() {
  using namespace ins;
  DexB d;
  const uint32_t tObj = d.Type("Ljava/lang/Object;");
  const uint32_t tStr = d.Type("Ljava/lang/String;");
  const uint32_t tCharArr = d.Type("[C");
  const uint32_t tJ = d.Type("J");
  const uint32_t tI = d.Type("I");
  const uint32_t tV = d.Type("V");
  const uint32_t tO = d.Type("Ljava/lang/Object;");
  (void)tO;
  const uint32_t tU = d.Type("Ltest/U;");
  const uint32_t tDrv = d.Type("Ltest/Drv;");
  constexpr int kChain = 10;
  uint32_t tC[kChain];
  for (int i = 0; i < kChain; i++) tC[i] = d.Type("Ltest/C" + std::to_string(i) + ";");

  const uint32_t pVV = d.ProtoSig("V", {});
  const uint32_t pVI = d.ProtoSig("V", {"I"});
  (void)pVI;
  const uint32_t pII = d.ProtoSig("I", {"I"});
  (void)pII;
  const uint32_t pJJ = d.ProtoSig("J", {"J"});
  const uint32_t pJJI = d.ProtoSig("J", {"J", "J", "I"});
  const uint32_t pIO = d.ProtoSig("Ljava/lang/Object;", {"I"});
  const uint32_t pOO = d.ProtoSig("Ljava/lang/Object;", {"Ljava/lang/Object;"});
  const uint32_t pVCharArr = d.ProtoSig("V", {"[C"});
  const uint32_t pJ_IJ = d.ProtoSig("J", {"J", "I"});
  const uint32_t pS = d.ProtoSig("Ljava/lang/String;", {});

  const uint32_t mStrInit = d.Method(tStr, pVCharArr, "<init>");
  const uint32_t mMix = d.Method(tU, pJ_IJ, "mix");
  const uint32_t mBox = d.Method(tU, pIO, "box");
  const uint32_t mD = d.Method(tU, pOO, "d");
  const uint32_t mAlias = d.Method(tU, pS, "alias");
  const uint32_t mOverlap = d.Method(tU, pJJ, "overlap");
  const uint32_t mRefOver = d.Method(tU, pS, "refOverWide");
  const uint32_t mFive = d.Method(tU, pJJI, "fiveWord");
  const uint32_t mBoot = d.Method(tDrv, d.ProtoSig("I", {}), "boot");
  uint32_t mTouch[kChain], mClinit[kChain];
  for (int i = 0; i < kChain; i++) {
    mTouch[i] = d.Method(tC[i], pVV, "touch");
    mClinit[i] = d.Method(tC[i], pVV, "<clinit>");
  }

  auto M = [](uint32_t idx, uint32_t access, uint16_t regs, uint16_t ins,
              std::vector<uint16_t> code, bool virt = false) {
    DexB::BMethod m;
    m.method_idx = idx;
    m.access = access;
    m.regs = regs;
    m.ins = ins;
    m.code = std::move(code);
    m.is_virtual = virt;
    return m;
  };
  constexpr uint32_t kStat = 0x9;      // public static
  constexpr uint32_t kPubStat = 0x9;
  constexpr uint32_t kDir = 0x10001;   // public constructor
  (void)kStat;

  // U.mix(J, I)J：mul-long 31 + return（regs=5/ins=3：参数 J@v2、I@v4）
  std::vector<uint16_t> code_mix = Cat({
      I51l(0x18, 0, 31),             // const-wide v0, 31（局部对 (0,1)）
      I23x(0x9D, 2, 2, 0),           // mul-long v2, v2, v0
      I11x(0x10, 2),                 // return-wide v2
  });
  // U.box(I)Ljava/lang/Object;：new String(char[1]{'a'})
  std::vector<uint16_t> code_box = Cat({
      I22c(0x22, 0, 0, (uint16_t)tStr),  // new-instance v0, String
      I12x(0x12, 2, 1),                  // const/4 v2, 1
      I22c(0x23, 2, 2, (uint16_t)tCharArr),  // new-array v2, v2, [C
      I21c(0x13, 3, 0x61),               // const/16 v3, 'a'
      I12x(0x12, 4, 0),                  // const/4 v4, 0
      I23x(0x50, 3, 2, 4),               // aput-char v3, v2, v4
      Invoke35c(0x70, mStrInit, {0, 2}),  // invoke-direct {v0, v2}, String.<init>([C)V
      I11x(0x11, 0),                      // return-object v0
  });
  // U.d(Ljava/lang/Object;)Ljava/lang/Object;：instance-of + check-cast（regs=2/ins=1：参数@v1）
  std::vector<uint16_t> code_d = Cat({
      I22c(0x20, 0, 1, (uint16_t)tStr),  // instance-of v0, v1, String
      {static_cast<uint16_t>((0 << 8) | 0x38), 4},  // if-eqz v0, +4（跳过 check-cast）
      I21c(0x1F, 1, (uint16_t)tStr),     // check-cast v1, String
      I11x(0x11, 1),                     // return-object v1
  });
  // U.alias()Ljava/lang/String;：this 别名 3 vreg + StringFactory
  std::vector<uint16_t> code_alias = Cat({
      I22c(0x22, 0, 0, (uint16_t)tStr),  // new-instance v0, String
      I12x(0x07, 1, 0),                  // move-object v1, v0
      I12x(0x07, 2, 0),                  // move-object v2, v0
      I12x(0x12, 3, 2),                  // const/4 v3, 2
      I22c(0x23, 3, 3, (uint16_t)tCharArr),  // new-array v3, v3, [C
      I21c(0x13, 4, 0x61),               // const/16 v4, 'a'
      I12x(0x12, 5, 0),                  // const/4 v5, 0
      I23x(0x50, 4, 3, 5),               // aput-char v4, v3, v5
      I21c(0x13, 4, 0x62),               // const/16 v4, 'b'
      I12x(0x12, 5, 1),                  // const/4 v5, 1
      I23x(0x50, 4, 3, 5),               // aput-char v4, v3, v5
      Invoke35c(0x70, mStrInit, {0, 3}),  // invoke-direct {v0, v3}, String.<init>([C)V
      I11x(0x11, 2),                     // return-object v2（别名槽）
  });
  // U.overlap()J：move-wide 重叠对
  std::vector<uint16_t> code_overlap = Cat({
      I51l(0x18, 0, 0x11223344u),  // const-wide v0, L（低 32 即值域）
      I12x(0x04, 1, 0),            // move-wide v1, v0（dest 对 (1,2) 与 src 对 (0,1) 重叠）
      I12x(0x04, 3, 1),            // move-wide v3, v1
      I11x(0x10, 3),               // return-wide v3
  });
  // U.refOverWide()Ljava/lang/String;：wide 对死亡后两 vreg 复用为对象 + check-cast
  std::vector<uint16_t> code_row = Cat({
      I51l(0x18, 0, 0x55667788u),           // const-wide v0
      I12x(0x04, 2, 0),                     // move-wide v2, v0（镜像陈旧形态源）
      I22c(0x22, 2, 0, (uint16_t)tStr),     // new-instance v2, String（复用 wide 起点）
      I22c(0x22, 3, 0, (uint16_t)tStr),     // new-instance v3, String（复用镜像槽）
      I12x(0x12, 4, 1),                     // const/4 v4, 1
      I22c(0x23, 4, 4, (uint16_t)tCharArr),  // new-array v4, v4, [C
      I21c(0x13, 5, 0x7a),                  // const/16 v5, 'z'
      I12x(0x12, 6, 0),                     // const/4 v6, 0
      I23x(0x50, 5, 4, 6),                  // aput-char v5, v4, v6
      Invoke35c(0x70, mStrInit, {2, 4}),    // invoke-direct {v2, v4}, String.<init>([C)V
      I21c(0x1F, 2, (uint16_t)tStr),        // check-cast v2, String
      I21c(0x1F, 3, (uint16_t)tStr),        // check-cast v3, String（镜像槽现为对象）
      Invoke35c(0x70, mStrInit, {3, 4}),    // invoke-direct {v3, v4}, String.<init>([C)V
      I11x(0x11, 2),                        // return-object v2
  });
  // U.fiveWord(J, J, I)J：add-long + int-to-long（regs=6/ins=5：参数 J@v1、J@v3、I@v5）
  std::vector<uint16_t> code_five = Cat({
      I23x(0x9B, 1, 1, 3),  // add-long v1, v1, v3
      I12x(0x81, 5, 5),     // int-to-long v5, v5
      I23x(0x9B, 1, 1, 5),  // add-long v1, v1, v5
      I11x(0x10, 1),        // return-wide v1
  });

  DexB::BClass u;
  u.type_idx = tU;
  u.super_idx = tObj;
  u.access = 1;
  u.methods.push_back(M(mMix, kPubStat, 5, 3, code_mix));
  u.methods.push_back(M(mBox, kPubStat, 5, 1, code_box));
  u.methods.push_back(M(mD, kPubStat, 2, 1, code_d));
  u.methods.push_back(M(mAlias, kPubStat, 6, 0, code_alias));
  u.methods.push_back(M(mOverlap, kPubStat, 5, 0, code_overlap));
  u.methods.push_back(M(mRefOver, kPubStat, 7, 0, code_row));
  u.methods.push_back(M(mFive, kPubStat, 6, 5, code_five));
  d.AddClass(std::move(u));

  for (int i = 0; i < kChain; i++) {
    DexB::BClass c;
    c.type_idx = tC[i];
    c.super_idx = tObj;
    c.access = 1;
    c.methods.push_back(M(mTouch[i], kPubStat, 1, 0, {0x000e}));  // return-void
    // <clinit>：next.touch() + wide/move-result + box + instance-of + d()
    std::vector<uint16_t> cl = Cat({
        Invoke35c(0x71, mTouch[(i + 1) % kChain], {}),  // invoke-static next.touch()V
        I51l(0x18, 0, 0x01234567u + i),                 // const-wide v0
        I12x(0x12, 2, i & 0x7),                         // const/4 v2, i
        Invoke35c(0x71, mMix, {0, 1, 2}),               // invoke-static {v0,v1,v2}, mix(J,I)J
        I11x(0x0B, 4),                                  // move-result-wide v4
        Invoke35c(0x71, mBox, {2}),                     // invoke-static {v2}, box(I)Object
        I11x(0x0C, 6),                                  // move-result-object v6
        I22c(0x20, 7, 6, (uint16_t)tStr),               // instance-of v7, v6, String
        Invoke35c(0x71, mD, {6}),                       // invoke-static {v6}, d(Object)Object
        I11x(0x0C, 6),                                  // move-result-object v6
        {0x000e},                                       // return-void
    });
    c.methods.push_back(M(mClinit[i], kPubStat, 8, 0, cl));
    d.AddClass(std::move(c));
  }

  // Drv.boot()I：驱动对抗面 + 5 词 invoke；期望 100+200+7 = 307
  std::vector<uint16_t> code_boot = Cat({
      Invoke35c(0x71, mTouch[0], {}),      // invoke-static C0.touch()V（触发 clinit 链）
      Invoke35c(0x71, mAlias, {}),         // invoke-static alias()
      I11x(0x0C, 0),                       // move-result-object v0
      I21c(0x1F, 0, (uint16_t)tStr),       // check-cast v0, String
      Invoke35c(0x71, mOverlap, {}),       // invoke-static overlap()J
      I11x(0x0B, 2),                       // move-result-wide v2
      Invoke35c(0x71, mRefOver, {}),       // invoke-static refOverWide()
      I11x(0x0C, 4),                       // move-result-object v4
      I21c(0x1F, 4, (uint16_t)tStr),       // check-cast v4, String
      I51l(0x18, 8, 100),                  // const-wide v8, 100
      I51l(0x18, 10, 200),                 // const-wide v10, 200
      I12x(0x12, 12, 7),                   // const/4 v12, 7
      Invoke35c(0x71, mFive, {8, 9, 10, 11, 12}),  // fiveWord(J,J,I)J
      I11x(0x0B, 0),                       // move-result-wide v0
      I12x(0x84, 0, 0),                    // long-to-int v0, v0
      I11x(0x0F, 0),                       // return v0
  });
  DexB::BClass drv;
  drv.type_idx = tDrv;
  drv.super_idx = tObj;
  drv.access = 1;
  drv.methods.push_back(M(mBoot, kPubStat, 13, 0, code_boot));
  d.AddClass(std::move(drv));

  return d.Build();
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== clinit stress smoke ==\n");
  if (argc < 2) {
    std::printf("usage: clinit_stress_smoke <clinit_stress.dex>\n");
    return 2;
  }

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  std::vector<uint8_t> bytes = ReadFile(argv[1]);
  CHECK(!bytes.empty(), "read clinit_stress.dex");
  std::string derr;
  auto dex = dexvm::dex::DexFile::Open(bytes.data(), bytes.size(), &derr);
  CHECK(dex != nullptr, "DexFile::Open clinit_stress.dex");
  if (dex == nullptr) {
    std::printf("  dex error: %s\n", derr.c_str());
    return 1;
  }
  linker.AddAppDexFile(dex.get());

  VClass* entry = linker.FindClass("Lstress/Entry;");
  CHECK(entry != nullptr, "FindClass Lstress/Entry;");
  if (entry == nullptr) return 1;

  // ---- 阶段 1：深递归 clinit 链 ----
  {
    int32_t v = 0;
    CHECK(CallStaticI(state, entry, "run", &v), "run() deep chain (40 levels)");
    CHECK(v == 1, "run() == 1");
  }

  // ---- 阶段 2：每层 check() 确定性求和 ----
  {
    int32_t v = 0;
    int32_t expected = 0;
    for (int i = 0; i < kLevels; i++) expected += i * 3 + 8;
    bool ok = CallStaticI(state, entry, "sum", &v);
    CHECK(ok, "sum() invoke");
    if (ok) {
      std::printf("  sum = %d (expected %d)\n", v, expected);
      CHECK(v == expected, "sum() deterministic value");
    }
  }

  // ---- 阶段 3：初始化不变式 ----
  {
    Value r{};
    bool ok = CallStatic(state, entry, "verify", &r);
    CHECK(ok && r.i == 1, "verify() == true after init");
  }

  // ---- 阶段 4：GC 后不变式（帧保守扫描 + 静态引用存活）----
  {
    dexvm::gc::CollectGarbage(state);
    Value r{};
    bool ok = CallStatic(state, entry, "verify", &r);
    CHECK(ok && r.i == 1, "verify() == true after GC");
  }

  // ---- 阶段 5：宽值 move-result 链确定性 ----
  {
    Value r1{};
    Value r2{};
    bool ok1 = CallStatic(state, entry, "wideMix", &r1);
    bool ok2 = CallStatic(state, entry, "wideMix", &r2);
    CHECK(ok1 && ok2, "wideMix() invoke x2");
    if (ok1 && ok2) CHECK(r1.l == r2.l, "wideMix() deterministic");
  }

  // ---- 阶段 6：scan(pool) —— check-cast/instance-of/d() 崩溃点形态 ----
  {
    VClass* obj_cls = linker.FindClass("Ljava/lang/Object;");
    VClass* obj_arr = linker.FindArrayClass(obj_cls);
    CHECK(obj_arr != nullptr, "array class [Ljava/lang/Object;");
    const int kPool = 64;
    VArray* pool = linker.AllocArray(obj_arr, kPool);
    CHECK(pool != nullptr, "alloc pool array");
    if (pool != nullptr) {
      for (int i = 0; i < kPool; i++) {
        ObjHeader* el = nullptr;
        if (i % 3 == 0) {
          el = reinterpret_cast<ObjHeader*>(linker.AllocString(u"stress-pool-element"));
        } else if (i % 3 == 1) {
          VClass* long_arr = linker.FindArrayClass(linker.FindClass("J"));
          VArray* la = linker.AllocArray(long_arr, 3);
          if (la != nullptr) {
            for (int j = 0; j < 3; j++) {
              uint64_t lv = static_cast<uint64_t>(i) * 100 + static_cast<uint64_t>(j);
              std::memcpy(la->data() + static_cast<size_t>(j) * 8, &lv, 8);
            }
          }
          el = reinterpret_cast<ObjHeader*>(la);
        } else {
          el = reinterpret_cast<ObjHeader*>(linker.AllocString(u"x"));
        }
        std::memcpy(pool->data() + static_cast<size_t>(i) * 8, &el, 8);
      }
      Value r1{};
      Value r2{};
      bool ok1 = CallStaticRef1(state, entry, "scan",
                                reinterpret_cast<ObjHeader*>(pool), &r1);
      bool ok2 = CallStaticRef1(state, entry, "scan",
                                reinterpret_cast<ObjHeader*>(pool), &r2);
      CHECK(ok1 && ok2, "scan(pool) invoke x2");
      if (ok1 && ok2) {
        std::printf("  scan = %d\n", r1.i);
        CHECK(r1.i == r2.i, "scan() deterministic");
        CHECK(r1.i >= kPool, "scan() >= pool size (every element visited)");
      }
    }
  }

  // ---- 阶段 7：StringFactory 别名回写 ----
  {
    int32_t v = 0;
    CHECK(CallStaticI(state, entry, "alias", &v), "alias() invoke");
    CHECK(v == 6, "alias() == 6 (two 3-char strings)");
  }

  // ---- 阶段 8：手工 DEX 堆叠（fixture.dex 模式 fallback；对抗形态）----
  std::unique_ptr<dexvm::dex::DexFile> adv_dex;
  {
    std::vector<uint8_t> adv = BuildAdversarialDex();
    CHECK(!adv.empty(), "build adversarial dex");
    std::string derr2;
    adv_dex = dexvm::dex::DexFile::Open(adv.data(), adv.size(), &derr2);
    CHECK(adv_dex != nullptr, "open adversarial dex");
    if (adv_dex != nullptr) {
      linker.AddAppDexFile(adv_dex.get());
      VClass* drv = linker.FindClass("Ltest/Drv;");
      CHECK(drv != nullptr, "FindClass Ltest/Drv;");
      if (drv != nullptr) {
        int32_t v = 0;
        CHECK(CallStaticI(state, drv, "boot", &v), "hand-rolled boot() (10-level clinit chain)");
        CHECK(v == 307, "boot() == 307 (fiveWord 100+200+7)");
      }
      VClass* u = linker.FindClass("Ltest/U;");
      CHECK(u != nullptr, "FindClass Ltest/U;");
      if (u != nullptr) {
        Value r{};
        bool ok = CallStatic(state, u, "alias", &r);
        CHECK(ok && r.ref != nullptr, "hand-rolled alias()");
        if (ok && r.ref != nullptr) {
          CHECK(static_cast<VString*>(r.ref)->ToModifiedUtf8() == "ab",
                "alias() == \"ab\" (StringFactory 别名回写)");
        }
        Value r2{};
        ok = CallStatic(state, u, "overlap", &r2);
        CHECK(ok && r2.l == 0x11223344, "overlap() == 0x11223344 (move-wide 重叠对)");
        Value r3{};
        ok = CallStatic(state, u, "refOverWide", &r3);
        CHECK(ok && r3.ref != nullptr, "refOverWide() (wide 槽复用 + check-cast)");
      }
    }
  }

  // ---- 终态：GC + verify ----
  {
    dexvm::gc::CollectGarbage(state);
    Value r{};
    bool ok = CallStatic(state, entry, "verify", &r);
    CHECK(ok && r.i == 1, "verify() == true final");
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
