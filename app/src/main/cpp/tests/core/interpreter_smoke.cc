// 解释器冒烟测试：add / 字符串(StringFactory) / 调用 / 异常（try-catch + unwind）。
// 手工构造最小 DEX（class Ltest/T;）走完整链路：
//   DexFile::Open -> ClassLinker::DefineClass -> InvokeMethod -> ExecuteFrame。
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                     \
  do {                                                       \
    if (!(cond)) {                                           \
      std::printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
      g_failures++;                                          \
    } else {                                                 \
      std::printf("PASS: %s\n", msg);                        \
    }                                                        \
  } while (0)

// ---- 最小 DEX 构造 ----

class DexBuilder {
 public:
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

  // code_item（4 对齐）；tries 仅支持单 try + 单 handler（caught 用）
  size_t WriteCodeItem(uint16_t regs, uint16_t ins, uint16_t outs,
                       const std::vector<uint16_t>& insns, bool with_try,
                       uint32_t handler_addr, uint32_t handler_type_idx) {
    Align4();
    size_t off = Pos();
    W16(regs);
    W16(ins);
    W16(outs);
    W16(with_try ? 1 : 0);  // tries_size
    W32(0);                 // debug_info_off
    W32(static_cast<uint32_t>(insns.size()));
    for (uint16_t u : insns) W16(u);
    if (with_try) {
      // try_item 数组：insns 末尾 4 对齐（对照 GetTryItems）
      Align4();
      W32(0);                                       // start_addr = 0
      W16(static_cast<uint16_t>(handler_addr));     // insn_count：覆盖 handler 前全部
      W16(1);                                       // handler_off（跳过列表 size 字节）
      // encoded_catch_handler_list：uleb 列表大小 + handler
      Uleb(1);                           // handler 数
      Uleb(1);                           // handler size：1 个 typed 对（正数=无 catch-all）
      Uleb(handler_type_idx);            // type_idx
      Uleb(handler_addr);                // handler code-unit 偏移
    }
    return off;
  }
};

// 字符串表（索引即数组下标）
const char* kStrings[] = {
    "<init>",                          // 0
    "I",                               // 1
    "II",                              // 2
    "III",                             // 3
    "L",                               // 4
    "LL",                              // 5
    "Ljava/lang/ArithmeticException;", // 6
    "Ljava/lang/Object;",              // 7
    "Ljava/lang/String;",              // 8
    "Ltest/T;",                        // 9
    "V",                               // 10
    "VL",                              // 11
    "add",                             // 12
    "boom",                            // 13
    "caught",                          // 14
    "hello",                           // 15
    "mk",                              // 16
    "twice",                           // 17
    "uncaught",                        // 18
};
constexpr size_t kNumStrings = sizeof(kStrings) / sizeof(kStrings[0]);

// type_ids
constexpr uint32_t kTypeArithEx = 0;
constexpr uint32_t kTypeObject = 1;
constexpr uint32_t kTypeString = 2;
constexpr uint32_t kTypeT = 3;
constexpr uint32_t kTypeI = 4;
constexpr uint32_t kTypeV = 5;

// method_ids
constexpr uint32_t kMAdd = 0;
constexpr uint32_t kMBoom = 1;
constexpr uint32_t kMCaught = 2;
constexpr uint32_t kMHello = 3;
constexpr uint32_t kMMk = 4;
constexpr uint32_t kMTwice = 5;
constexpr uint32_t kMUncaught = 6;
constexpr uint32_t kMStringInit = 7;

std::vector<uint8_t> BuildDex() {
  DexBuilder d;
  d.b.resize(0x70, 0);  // header 占位

  // ---- id 表 ----
  size_t string_ids_pos = d.Pos();
  for (size_t i = 0; i < kNumStrings; i++) d.W32(0);  // patch

  size_t type_ids_pos = d.Pos();
  d.W32(6);   // 0 ArithmeticException
  d.W32(7);   // 1 Object
  d.W32(8);   // 2 String
  d.W32(9);   // 3 T
  d.W32(1);   // 4 I
  d.W32(10);  // 5 V

  // proto_ids: {shorty_idx, return_type_idx, parameters_off}
  // 0:(II)I 1:(I)I 2:()String 3:(String)String 4:(String)V 5:()I
  size_t proto_ids_pos = d.Pos();
  d.W32(3); d.W32(kTypeI); size_t p0 = d.Pos(); d.W32(0);
  d.W32(2); d.W32(kTypeI); size_t p1 = d.Pos(); d.W32(0);
  d.W32(4); d.W32(kTypeString); d.W32(0);
  d.W32(5); d.W32(kTypeString); size_t p3 = d.Pos(); d.W32(0);
  d.W32(11); d.W32(kTypeV); size_t p4 = d.Pos(); d.W32(0);
  d.W32(1); d.W32(kTypeI); d.W32(0);

  // method_ids: {ushort class_idx, ushort proto_idx, uint name_idx}
  size_t method_ids_pos = d.Pos();
  auto MethodId = [&d](uint16_t cls, uint16_t proto, uint32_t name) {
    d.W16(cls);
    d.W16(proto);
    d.W32(name);
  };
  MethodId(kTypeT, 0, 12);        // 0 add(II)I
  MethodId(kTypeT, 0, 13);        // 1 boom(II)I
  MethodId(kTypeT, 5, 14);        // 2 caught()I
  MethodId(kTypeT, 2, 15);        // 3 hello()String
  MethodId(kTypeT, 3, 16);        // 4 mk(String)String
  MethodId(kTypeT, 1, 17);        // 5 twice(I)I
  MethodId(kTypeT, 0, 18);        // 6 uncaught(II)I
  MethodId(kTypeString, 4, 0);    // 7 String.<init>(String)

  // class_defs: Ltest/T;
  size_t class_defs_pos = d.Pos();
  d.W32(kTypeT);
  d.W32(0x1);         // public
  d.W32(kTypeObject); // superclass
  d.W32(0);           // interfaces_off
  d.W32(0xffffffff);  // source_file_idx
  d.W32(0);           // annotations_off
  size_t class_data_patch = d.Pos();
  d.W32(0);           // class_data_off (patch)
  d.W32(0);           // static_values_off

  // ---- data 段 ----
  uint32_t data_off = static_cast<uint32_t>(d.Pos());

  // TypeList（4 对齐）
  d.Align4();
  uint32_t tl_ii = static_cast<uint32_t>(d.Pos());
  d.W32(2); d.W16(kTypeI); d.W16(kTypeI);
  d.Align4();
  uint32_t tl_i = static_cast<uint32_t>(d.Pos());
  d.W32(1); d.W16(kTypeI);
  d.Align4();
  uint32_t tl_str = static_cast<uint32_t>(d.Pos());
  d.W32(1); d.W16(kTypeString);
  d.Patch32(p0, tl_ii);
  d.Patch32(p1, tl_i);
  d.Patch32(p3, tl_str);
  d.Patch32(p4, tl_str);

  // string_data
  for (size_t i = 0; i < kNumStrings; i++) {
    uint32_t off = static_cast<uint32_t>(d.Pos());
    d.Patch32(string_ids_pos + i * 4, off);
    const char* s = kStrings[i];
    d.Uleb(static_cast<uint32_t>(std::strlen(s)));  // ASCII：utf16_size = 字节数
    for (const char* c = s; *c != 0; c++) d.W8(static_cast<uint8_t>(*c));
    d.W8(0);
  }

  // code items
  // add: add-int v0,v1,v2 ; return v0
  size_t code_add = d.WriteCodeItem(3, 2, 0, {0x0090, 0x0201, 0x000F}, false, 0, 0);
  // boom: div-int v0,v1,v2 ; return v0
  size_t code_boom = d.WriteCodeItem(3, 2, 0, {0x0093, 0x0201, 0x000F}, false, 0, 0);
  // twice: invoke-static {v2,v2}, add ; move-result v0 ; return v0
  size_t code_twice = d.WriteCodeItem(3, 1, 2,
      {0x2071, kMAdd, 0x0022, 0x000A, 0x000F}, false, 0, 0);
  // hello: const-string v0, "hello" ; return-object v0
  size_t code_hello = d.WriteCodeItem(1, 0, 0, {0x001A, 15, 0x0011}, false, 0, 0);
  // mk: new-instance v0,String ; invoke-direct {v0,v1}, String.<init> ; return-object v0
  size_t code_mk = d.WriteCodeItem(2, 1, 2,
      {0x0022, kTypeString, 0x2070, kMStringInit, 0x0010, 0x0011}, false, 0, 0);
  // caught: try { const/4 v0,1; const/4 v1,0; invoke boom; move-result; return }
  //         catch (ArithmeticException) { move-exception v0; const/16 v0,42; return v0 }
  size_t code_caught = d.WriteCodeItem(3, 0, 2,
      {0x1012, 0x0112, 0x2071, kMBoom, 0x0010, 0x000A, 0x000F,
       0x000D, 0x0013, 0x002A, 0x000F},
      true, /*handler_addr=*/7, /*type_idx=*/kTypeArithEx);
  // uncaught: invoke-static {v1,v2}, boom ; move-result v0 ; return v0
  size_t code_uncaught = d.WriteCodeItem(3, 2, 2,
      {0x2071, kMBoom, 0x0021, 0x000A, 0x000F}, false, 0, 0);

  // class_data（direct_methods 按 method_idx 升序）
  d.Align4();
  uint32_t class_data_off = static_cast<uint32_t>(d.Pos());
  d.Uleb(0);  // static_fields_size
  d.Uleb(0);  // instance_fields_size
  d.Uleb(7);  // direct_methods_size
  d.Uleb(0);  // virtual_methods_size
  struct Emitted {
    uint32_t diff;
    size_t code_off;
  } kEmitted[] = {
      {0, code_add},   {1, code_boom}, {1, code_caught}, {1, code_hello},
      {1, code_mk},    {1, code_twice}, {1, code_uncaught},
  };
  for (const auto& e : kEmitted) {
    d.Uleb(e.diff);
    d.Uleb(0x9);  // public static
    d.Uleb(static_cast<uint32_t>(e.code_off));
  }
  d.Patch32(class_data_patch, class_data_off);

  // map_list（4 对齐）
  d.Align4();
  uint32_t map_off = static_cast<uint32_t>(d.Pos());
  d.W32(10);  // map_item 数
  auto MapItem = [&d](uint16_t type, uint32_t size, uint32_t off) {
    d.W16(type);
    d.W16(0);
    d.W32(size);
    d.W32(off);
  };
  MapItem(0x0000, 1, 0);                             // header_item
  MapItem(0x0001, kNumStrings, string_ids_pos);      // string_id
  MapItem(0x0002, 6, type_ids_pos);                  // type_id
  MapItem(0x0003, 6, proto_ids_pos);                 // proto_id
  MapItem(0x0005, 8, method_ids_pos);                // method_id
  MapItem(0x0006, 1, class_defs_pos);                // class_def
  MapItem(0x1001, 3, tl_ii);                         // type_list（首条）
  MapItem(0x2000, 1, class_data_off);                // class_data_item
  MapItem(0x2001, 7, static_cast<uint32_t>(code_add));  // code_item
  MapItem(0x1000, 1, map_off);                       // map_list

  // ---- header ----
  std::memcpy(d.b.data(), "dex\n035\0", 8);
  uint32_t file_size = static_cast<uint32_t>(d.b.size());
  d.Patch32(0x20, file_size);
  d.Patch32(0x24, 0x70);          // header_size
  d.Patch32(0x28, 0x12345678);    // endian_tag
  d.Patch32(0x34, map_off);
  d.Patch32(0x38, kNumStrings);
  d.Patch32(0x3c, string_ids_pos);
  d.Patch32(0x40, 6);
  d.Patch32(0x44, type_ids_pos);
  d.Patch32(0x48, 6);
  d.Patch32(0x4c, proto_ids_pos);
  d.Patch32(0x50, 0);  // field_ids_size
  d.Patch32(0x54, 0);
  d.Patch32(0x58, 8);
  d.Patch32(0x5c, method_ids_pos);
  d.Patch32(0x60, 1);
  d.Patch32(0x64, class_defs_pos);
  d.Patch32(0x68, file_size - data_off);  // data_size
  d.Patch32(0x6c, data_off);
  return d.b;
}

using dexvm::runtime::InvokeMethod;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VMethod;
using dexvm::runtime::VString;
using dexvm::runtime::VMState;

VMethod* FindMethod(VClass* klass, const char* name, size_t param_count) {
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.param_types.size() == param_count) return &m;
  }
  return nullptr;
}

}  // namespace

int main() {
  static std::vector<uint8_t> dex_bytes = BuildDex();
  std::string error;
  auto dex = dexvm::dex::DexFile::Open(dex_bytes.data(), dex_bytes.size(), &error);
  if (dex == nullptr) {
    std::printf("FAIL: DexFile::Open: %s\n", error.c_str());
    return 1;
  }

  dexvm::mem::Heap heap;
  dexvm::runtime::InternTable intern;
  dexvm::runtime::ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  linker.AddAppDexFile(dex.get());

  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  VClass* t = linker.FindClass("Ltest/T;");
  CHECK(t != nullptr, "FindClass Ltest/T;");
  if (t == nullptr) return 1;

  // ---- add 冒烟 ----
  {
    VMethod* add = FindMethod(t, "add", 2);
    CHECK(add != nullptr, "resolve add(II)I");
    Value args[2] = {};
    args[0].i = 3;
    args[1].i = 4;
    Value result{};
    bool ok = add != nullptr && InvokeMethod(state, add, args, 2, &result);
    CHECK(ok && !state.HasPendingException(), "add invoke ok");
    CHECK(result.i == 7, "add(3,4) == 7");
  }

  // ---- 字符串冒烟（const-string + StringFactory 重写）----
  {
    VMethod* hello = FindMethod(t, "hello", 0);
    Value result{};
    bool ok = hello != nullptr && InvokeMethod(state, hello, nullptr, 0, &result);
    CHECK(ok && !state.HasPendingException(), "hello invoke ok");
    VString* s = reinterpret_cast<VString*>(result.ref);
    CHECK(s != nullptr && s->ToModifiedUtf8() == "hello", "hello() == \"hello\"");
  }
  {
    VMethod* mk = FindMethod(t, "mk", 1);
    std::u16string world = u"world";
    VString* src = linker.AllocString(world);
    Value args[1] = {};
    args[0].ref = reinterpret_cast<dexvm::runtime::ObjHeader*>(src);
    Value result{};
    bool ok = mk != nullptr && InvokeMethod(state, mk, args, 1, &result);
    CHECK(ok && !state.HasPendingException(), "mk invoke ok");
    VString* out = reinterpret_cast<VString*>(result.ref);
    CHECK(out != nullptr && out->ToModifiedUtf8() == "world", "mk(\"world\") content");
    CHECK(out != nullptr && src != nullptr && out != src, "StringFactory new identity");
  }

  // ---- 调用冒烟（invoke-static 跨方法）----
  {
    VMethod* twice = FindMethod(t, "twice", 1);
    Value args[1] = {};
    args[0].i = 5;
    Value result{};
    bool ok = twice != nullptr && InvokeMethod(state, twice, args, 1, &result);
    CHECK(ok && !state.HasPendingException(), "twice invoke ok");
    CHECK(result.i == 10, "twice(5) == 10");
  }

  // ---- 异常冒烟（try/catch 跨帧捕获 + 未捕获 unwind）----
  {
    VMethod* caught = FindMethod(t, "caught", 0);
    Value result{};
    bool ok = caught != nullptr && InvokeMethod(state, caught, nullptr, 0, &result);
    CHECK(ok && !state.HasPendingException(), "caught invoke ok");
    CHECK(result.i == 42, "caught() == 42 (div-by-zero caught)");
  }
  {
    VMethod* uncaught = FindMethod(t, "uncaught", 2);
    Value args[2] = {};
    args[0].i = 1;
    args[1].i = 0;
    Value result{};
    bool ok = uncaught != nullptr && InvokeMethod(state, uncaught, args, 2, &result);
    CHECK(!ok, "uncaught invoke returns false");
    CHECK(state.HasPendingException(), "uncaught pending exception");
    dexvm::runtime::ObjHeader* ex = state.pending_exception;
    CHECK(ex != nullptr && ex->klass != nullptr &&
              ex->klass->descriptor == "Ljava/lang/ArithmeticException;",
          "pending is ArithmeticException");
    state.ClearException();
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
