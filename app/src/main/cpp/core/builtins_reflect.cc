// java.lang.reflect 三套 + 泛型签名解析器 + Class 反射面 + Proxy 分发接缝。
// 纯移动自 core/builtins.cc（零行为变更；注册/绑定仍在 registrar）。

#include "core/builtins.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <new>
#include <set>
#include <string>
#include <time.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>

#include <dirent.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <arpa/inet.h>

#include "core/exception.h"
#include "core/framework_builtins_internal.h"  // NewLibObject（lib 组合缝）

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include "dex/leb128.h"
#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "session/class_loader.h"
#include "session/session.h"


#include "core/builtins_internal.h"

namespace dexvm::runtime {

// ---- java.lang.Class 元方法（const-class 对象即 VClass）----

Value ClassGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  std::string name = ClassName(reinterpret_cast<VClass*>(self));
  std::u16string out;
  AppendUtf16(&out, name.c_str());
  return MakeStr(state, out, ok);
}

Value ClassToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::string s = k != nullptr && k->IsInterface() ? "interface " : "class ";
  s += ClassName(k);
  std::u16string out;
  AppendUtf16(&out, s.c_str());
  return MakeStr(state, out, ok);
}
// 方法签名文本（反射诊断/IllegalAccessException 消息用）
namespace {
std::string MethodSigStr(const VMethod* m) {
  if (m == nullptr) return "?";
  std::string s;
  if (m->declaring != nullptr) {
    s += m->declaring->descriptor;
    s += ".";
  }
  s += m->name;
  s += "(";
  for (const auto& p : m->param_types) s += p;
  s += ")";
  s += m->return_type;
  return s;
}

// ---- 静态方法查找（FindInterfaceImpl 不搜 static direct）----
}  // namespace
VMethod* FindStaticM(VClass* klass, const char* name, std::vector<std::string> params,
                     const char* ret) {
  if (klass == nullptr) return nullptr;
  for (auto& m : klass->direct_methods) {
    if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) {
      return &m;
    }
  }
  for (auto& m : klass->virtual_methods) {
    if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) {
      return &m;
    }
  }
  return nullptr;
}

// 实例字段查找（沿继承链）
VField* FindInstField(VClass* klass, const char* name) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == name) return &f;
    }
  }
  return nullptr;
}

// ---- 数组元素槽（VArray：元素 8 字节槽）----
ObjHeader* ArrRef(const VArray* a, int32_t i) {
  ObjHeader* o = nullptr;
  std::memcpy(&o, a->data() + static_cast<size_t>(i) * 8, sizeof(o));
  return o;
}
void ArrSetRef(VArray* a, int32_t i, ObjHeader* o) {
  std::memcpy(a->data() + static_cast<size_t>(i) * 8, &o, sizeof(o));
}

// ---- 装箱/拆箱（valueOf/xxxValue 走 guest runtime.dex 实现）----
struct BoxInfo {
  const char* cls;
  const char* getter;
};
namespace {
BoxInfo BoxInfoFor(char type) {
  switch (type) {
    case 'Z': return {"Ljava/lang/Boolean;", "booleanValue"};
    case 'B': return {"Ljava/lang/Byte;", "byteValue"};
    case 'S': return {"Ljava/lang/Short;", "shortValue"};
    case 'C': return {"Ljava/lang/Character;", "charValue"};
    case 'I': return {"Ljava/lang/Integer;", "intValue"};
    case 'J': return {"Ljava/lang/Long;", "longValue"};
    case 'F': return {"Ljava/lang/Float;", "floatValue"};
    case 'D': return {"Ljava/lang/Double;", "doubleValue"};
    default: return {nullptr, nullptr};
  }
}

bool BoxValue(VMState& state, char type, const Value& in, ObjHeader** out) {
  BoxInfo bi = BoxInfoFor(type);
  if (bi.cls == nullptr) {
    *out = in.ref;
    return true;
  }
  VClass* k = state.linker != nullptr ? state.linker->FindClass(bi.cls) : nullptr;
  if (k == nullptr || !CheckClassDefinition(state, k) || !state.linker->InitializeClass(k, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", std::string("box missing ") + bi.cls);
    }
    return false;
  }
  VMethod* m = FindStaticM(k, "valueOf", {std::string(1, type)}, bi.cls);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", std::string("box missing valueOf ") + bi.cls);
    return false;
  }
  Value r{};
  if (!InvokeMethod(state, m, &in, 1, &r)) return false;
  *out = r.ref;
  return true;
}

}  // namespace
bool UnboxValue(VMState& state, ObjHeader* obj, char type, Value* out) {
  BoxInfo bi = BoxInfoFor(type);
  if (bi.cls == nullptr) {
    out->ref = obj;
    return true;
  }
  if (obj == nullptr || obj->klass == nullptr) {
    ThrowNullPointerException(state, "unbox null");
    return false;
  }
  VMethod probe;
  probe.name = bi.getter;
  probe.return_type = std::string(1, type);
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                   std::string("cannot unbox ") + obj->klass->descriptor + " as " + bi.cls);
    return false;
  }
  Value self_v{};
  self_v.ref = obj;
  Value r{};
  if (!InvokeMethod(state, m, &self_v, 1, &r)) return false;
  *out = r;
  return true;
}

// ---- 反射镜像（布局契约 jni/reflection.cc：artX @0、declaringClass @8、accessible @16）----
namespace {
constexpr uint32_t kMirrorArt = 0;
constexpr uint32_t kMirrorDecl = 8;
constexpr uint32_t kMirrorAcc = 16;

uint64_t MirrorGet(VObject* o, uint32_t off) {
  uint64_t v = 0;
  std::memcpy(&v, o->field_data() + off, 8);
  return v;
}
void MirrorSet(VObject* o, uint32_t off, uint64_t v) {
  std::memcpy(o->field_data() + off, &v, 8);
}

VObject* MakeMemberMirror(VMState& state, const char* desc, void* art_ptr, VClass* declaring) {
  VClass* k = state.linker != nullptr ? state.linker->FindClass(desc) : nullptr;
  if (k == nullptr) return nullptr;
  VObject* o = state.linker->AllocObject(k);
  if (o == nullptr) return nullptr;
  MirrorSet(o, kMirrorArt, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(art_ptr)));
  MirrorSet(o, kMirrorDecl, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(declaring)));
  MirrorSet(o, kMirrorAcc, 0);
  return o;
}

VObject* MakeMethodMirror(VMState& state, VMethod* m) {
  const char* desc = (m->name == "<init>") ? "Ljava/lang/reflect/Constructor;"
                                           : "Ljava/lang/reflect/Method;";
  return MakeMemberMirror(state, desc, m, m->declaring);
}
VObject* MakeFieldMirror(VMState& state, VField* f) {
  return MakeMemberMirror(state, "Ljava/lang/reflect/Field;", f, f->declaring);
}

VMethod* MirrorMethod(VObject* o) {
  return reinterpret_cast<VMethod*>(static_cast<uintptr_t>(MirrorGet(o, kMirrorArt)));
}
}  // namespace
VField* MirrorField(VObject* o) {
  return reinterpret_cast<VField*>(static_cast<uintptr_t>(MirrorGet(o, kMirrorArt)));
}
namespace {
bool MirrorAccessible(VObject* o) { return MirrorGet(o, kMirrorAcc) != 0; }

// 成员可见性（Android：setAccessible 后放行；否则 public 成员 + public 声明类）
bool MemberAccessible(VObject* mirror, uint32_t acc, VClass* declaring) {
  if (MirrorAccessible(mirror)) return true;
  if ((acc & 0x1) == 0) return false;
  return declaring != nullptr && (declaring->access_flags & 0x1) != 0;
}

// ---- 泛型签名面（JVMS 4.7.9.1 class signature 子集 → lib Type 镜像）----
// 解析链：ClassLinker::GetClassGenericSignature（dex Ldalvik/annotation/Signature;
// 属性）→ 递归下降解析 class signature → sun.reflect.generics.reflectiveObjects
// lib POJO 镜像（C 档 lib，§31.7：纯 Java 接口/POJO 归 lib;本节只做 VM 接缝的
// 解析与镜像构造,A 档小面）。无签名属性走退化语义（超类/超接口 Class 形态,
// 对齐 OpenJDK Class.getGenericInfo()==null 分支）。
// M 级最小：只做 class 级签名（形式类型参数/超类/超接口）;方法级 signature
// 属性（getGenericReturnType 面）后置。
}  // namespace
namespace {

struct GNode {
  enum class K { kClass, kVar, kWild, kArray, kPrim };
  K k = K::kClass;
  std::string name;           // kClass: 内部名 "g/G$Base"；kVar: 变量名;kPrim: 基元描述符
  std::vector<GNode> args;    // kClass 类型实参；kArray 分量（单元素）
  std::vector<GNode> upper;   // kWild 上界（空→Object）
  std::vector<GNode> lower;   // kWild 下界
  bool has_outer = false;
  std::vector<GNode> outer;   // kClass 嵌套外层（suffix 形态 Louter<TA;>.Inner;）;≤1 元素
};

struct ClassSig {
  std::vector<std::string> formal_names;
  std::vector<std::vector<GNode>> formal_bounds;
  GNode superclass;
  std::vector<GNode> interfaces;
};

// 递归下降游标（深度上限防恶意签名）
struct SigParser {
  std::string_view s;
  size_t i = 0;
  bool ok = true;

  bool Eof() const { return i >= s.size(); }
  char Peek() const { return i < s.size() ? s[i] : '\0'; }
  bool Fail() {
    ok = false;
    return false;
  }
  bool Expect(char c) {
    if (Peek() != c) return Fail();
    i++;
    return true;
  }
};

bool ParseTypeArgument(SigParser& p, GNode* out, int depth);
bool ParseReferenceType(SigParser& p, GNode* out, int depth);

// SimpleClassTypeSignature 段名：标识符字符（$ 允许;javac 嵌套类用 $ 直写）
std::string ParseNameSeg(SigParser& p) {
  std::string out;
  while (!p.Eof()) {
    char c = p.Peek();
    if (c == ';' || c == '<' || c == '>' || c == '[' || c == ':' || c == '.' || c == '+' ||
        c == '-') {
      break;
    }
    out += c;
    p.i++;
  }
  return out;
}

bool ParseTypeArgs(SigParser& p, GNode* node, int depth) {
  if (depth > 32) return p.Fail();
  if (!p.Expect('<')) return false;
  while (p.Peek() != '>') {
    if (p.Eof()) return p.Fail();
    GNode a;
    if (!ParseTypeArgument(p, &a, depth + 1)) return false;
    node->args.push_back(std::move(a));
  }
  return p.Expect('>');
}

// ClassTypeSignature: L [pkg/]Name[<args>] (.Name[<args>])* ;
bool ParseClassType(SigParser& p, GNode* out, int depth) {
  if (depth > 32) return p.Fail();
  if (!p.Expect('L')) return false;
  out->k = GNode::K::kClass;
  out->name = ParseNameSeg(p);
  if (out->name.empty()) return p.Fail();
  if (p.Peek() == '<') {
    if (!ParseTypeArgs(p, out, depth)) return false;
  }
  while (p.Peek() == '.') {
    p.i++;  // 嵌套 suffix：外层节点下沉为 outer，raw 名合并为 $ 形态
    GNode inner;
    inner.k = GNode::K::kClass;
    inner.name = ParseNameSeg(p);
    if (inner.name.empty()) return p.Fail();
    if (p.Peek() == '<') {
      if (!ParseTypeArgs(p, &inner, depth)) return false;
    }
    inner.has_outer = true;
    std::string outer_name = out->name;
    inner.outer.push_back(std::move(*out));
    inner.name = outer_name + "$" + inner.name;
    *out = std::move(inner);
  }
  return p.Expect(';');
}

// JavaTypeSignature: ReferenceTypeSignature | BaseType
bool ParseJavaType(SigParser& p, GNode* out, int depth) {
  char c = p.Peek();
  if (c == 'B' || c == 'C' || c == 'D' || c == 'F' || c == 'I' || c == 'J' || c == 'S' ||
      c == 'Z' || c == 'V') {
    out->k = GNode::K::kPrim;
    out->name = std::string(1, c);
    p.i++;
    return true;
  }
  return ParseReferenceType(p, out, depth);
}

bool ParseReferenceType(SigParser& p, GNode* out, int depth) {
  if (depth > 32) return p.Fail();
  char c = p.Peek();
  if (c == 'L') return ParseClassType(p, out, depth);
  if (c == 'T') {
    p.i++;
    out->k = GNode::K::kVar;
    out->name = ParseNameSeg(p);
    if (out->name.empty()) return p.Fail();
    return p.Expect(';');
  }
  if (c == '[') {
    p.i++;
    out->k = GNode::K::kArray;
    GNode comp;
    if (!ParseJavaType(p, &comp, depth + 1)) return false;
    out->args.push_back(std::move(comp));
    return true;
  }
  return p.Fail();
}

// TypeArgument: * | [+-]ReferenceTypeSignature
bool ParseTypeArgument(SigParser& p, GNode* out, int depth) {
  char c = p.Peek();
  if (c == '*') {
    p.i++;
    out->k = GNode::K::kWild;
    return true;
  }
  if (c == '+' || c == '-') {
    p.i++;
    out->k = GNode::K::kWild;
    GNode b;
    if (!ParseReferenceType(p, &b, depth + 1)) return false;
    if (c == '+') {
      out->upper.push_back(std::move(b));
    } else {
      out->lower.push_back(std::move(b));
    }
    return true;
  }
  return ParseReferenceType(p, out, depth);
}

// TypeParameters: '<' TypeParameter+ '>'（ClassSignature 与方法签名共用）
bool ParseFormalTypeParams(SigParser& p, std::vector<std::string>* names,
                           std::vector<std::vector<GNode>>* bounds_out) {
  if (p.Peek() != '<') return true;
  p.i++;
  while (p.Peek() != '>') {
    if (p.Eof()) return false;
    std::string name = ParseNameSeg(p);
    if (name.empty()) return false;
    if (!p.Expect(':')) return false;
    std::vector<GNode> bounds;
    // ClassBound 可为空（T::Lfoo/Bar; 形态）
    if (p.Peek() != ':') {
      GNode b;
      if (!ParseReferenceType(p, &b, 0)) return false;
      bounds.push_back(std::move(b));
    }
    while (p.Peek() == ':') {
      p.i++;
      GNode b;
      if (!ParseReferenceType(p, &b, 0)) return false;
      bounds.push_back(std::move(b));
    }
    if (bounds.empty()) {
      GNode obj;
      obj.k = GNode::K::kClass;
      obj.name = "java/lang/Object";
      bounds.push_back(std::move(obj));
    }
    names->push_back(std::move(name));
    bounds_out->push_back(std::move(bounds));
  }
  return p.Expect('>');
}

// ClassSignature: [<TypeParameters>] SuperclassSignature {SuperinterfaceSignature}
bool ParseClassSignature(std::string_view text, ClassSig* out) {
  SigParser p;
  p.s = text;
  if (!ParseFormalTypeParams(p, &out->formal_names, &out->formal_bounds)) return false;
  if (!ParseClassType(p, &out->superclass, 0)) return false;
  while (!p.Eof()) {
    GNode iface;
    if (!ParseClassType(p, &iface, 0)) return false;
    out->interfaces.push_back(std::move(iface));
  }
  return p.ok;
}

// 方法级泛型签名（JVMS 4.7.9.1 MethodSignature）：
//   [<TypeParameters>] '(' JavaTypeSignature* ')' ReturnType [^ThrowsSignature+]
// Retrofit 的 call adapter / converter 选择直接依赖 getGenericReturnType 与
// getGenericParameterTypes 的 ParameterizedType 形态（Observable<T>、
// Map<String,String> 等）；缺签名解析时回退原始类型会导致所有泛型服务方法
// 创建失败（"Unable to create call adapter" / "Map must include generic types"）。
struct MethodSig {
  std::vector<std::string> formal_names;
  std::vector<std::vector<GNode>> formal_bounds;
  std::vector<GNode> params;
  GNode ret;
};

bool ParseMethodSignature(std::string_view text, MethodSig* out) {
  SigParser p;
  p.s = text;
  if (!ParseFormalTypeParams(p, &out->formal_names, &out->formal_bounds)) return false;
  if (!p.Expect('(')) return false;
  while (p.Peek() != ')') {
    if (p.Eof()) return false;
    GNode t;
    if (!ParseJavaType(p, &t, 0)) return false;
    out->params.push_back(std::move(t));
  }
  if (!p.Expect(')')) return false;
  if (!ParseJavaType(p, &out->ret, 0)) return false;
  // throws 段（^L...;）不影响反射类型面，剩余文本忽略
  return p.ok;
}

// ---- dex annotations_directory 解码（encoded_value 宽度语义：dex 规范
// fixed-width LE，arg = 字节数-1；对照 dex/dex_file.cc DecodeEncodedValue）----
// 用途：① Signature 属性读取（泛型签名面）② 注解实例解码（getDeclaredAnnotations
// 族）。class_linker::GetClassGenericSignature 同名面按 uleb 假设读 VALUE_STRING
// 载荷（与规范/d8 产物不符）；本节按规范解码，为唯一事实源。
struct DexAnnoVal {
  enum class K { kInt, kFloat, kDouble, kString, kType, kField, kMethod, kEnum, kNull, kArray,
                 kAnno };
  K k = K::kNull;
  int64_t i = 0;    // kInt（byte/short/char/int/long/bool 全量）+ kFloat/kDouble 位型
  uint32_t idx = 0;  // kString/kType/kField/kMethod/kEnum
  std::vector<DexAnnoVal> arr;
  std::string anno_type;
  std::vector<std::pair<std::string, DexAnnoVal>> anno_elems;
};

struct DexAnnoItem {
  uint8_t visibility = 0;  // 1 = RUNTIME（getAnnotation/getDeclaredAnnotations 可见面）
  std::string type;
  std::vector<std::pair<std::string, DexAnnoVal>> elems;
};

// fixed-width LE 无符号读（宽度 = arg+1）
bool ReadFixedLE(const uint8_t** pp, const uint8_t* end, int width, uint64_t* out) {
  const uint8_t* p = *pp;
  if (width < 1 || width > 8 || p + width > end) return false;
  uint64_t v = 0;
  for (int i = 0; i < width; i++) v |= static_cast<uint64_t>(p[i]) << (8 * i);
  *pp = p + width;
  *out = v;
  return true;
}

bool DecodeAnnoValue(dex::DexFile* dex, const uint8_t** pp, const uint8_t* end, DexAnnoVal* out,
                     int depth);

bool DecodeAnnoArray(dex::DexFile* dex, const uint8_t** pp, const uint8_t* end,
                     std::vector<DexAnnoVal>* out, int depth) {
  uint32_t count = 0;
  if (!dex::ReadUleb128(pp, end, &count)) return false;
  out->clear();
  out->reserve(count);
  for (uint32_t i = 0; i < count; i++) {
    DexAnnoVal v;
    if (!DecodeAnnoValue(dex, pp, end, &v, depth + 1)) return false;
    out->push_back(std::move(v));
  }
  return true;
}

bool DecodeAnnoElements(dex::DexFile* dex, const uint8_t** pp, const uint8_t* end, uint32_t n,
                        std::vector<std::pair<std::string, DexAnnoVal>>* out, int depth) {
  out->clear();
  out->reserve(n);
  for (uint32_t i = 0; i < n; i++) {
    uint32_t name_idx = 0;
    if (!dex::ReadUleb128(pp, end, &name_idx)) return false;
    DexAnnoVal v;
    if (!DecodeAnnoValue(dex, pp, end, &v, depth + 1)) return false;
    out->emplace_back(dex->GetString(name_idx), std::move(v));
  }
  return true;
}

bool DecodeAnnoValue(dex::DexFile* dex, const uint8_t** pp, const uint8_t* end, DexAnnoVal* out,
                     int depth) {
  if (depth > 32 || *pp >= end) return false;
  uint8_t hb = *(*pp)++;
  uint8_t type = hb & 0x1f;
  uint8_t arg = hb >> 5;
  int width = static_cast<int>(arg) + 1;
  uint64_t raw = 0;
  switch (type) {
    case 0x00:  // VALUE_BYTE
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      out->k = DexAnnoVal::K::kInt;
      out->i = static_cast<int64_t>(static_cast<int8_t>(raw));
      return true;
    case 0x02:  // VALUE_SHORT
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      out->k = DexAnnoVal::K::kInt;
      out->i = static_cast<int64_t>(static_cast<int16_t>(raw));
      return true;
    case 0x03:  // VALUE_CHAR
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      out->k = DexAnnoVal::K::kInt;
      out->i = static_cast<int64_t>(static_cast<uint16_t>(raw));
      return true;
    case 0x04:  // VALUE_INT
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      out->k = DexAnnoVal::K::kInt;
      out->i = static_cast<int64_t>(static_cast<int32_t>(raw));
      return true;
    case 0x06:  // VALUE_LONG
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      out->k = DexAnnoVal::K::kInt;
      out->i = static_cast<int64_t>(raw);
      return true;
    case 0x10:  // VALUE_FLOAT（低 4 字节位型，右零填充）
    case 0x11:  // VALUE_DOUBLE（低 8 字节位型）
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      out->k = type == 0x10 ? DexAnnoVal::K::kFloat : DexAnnoVal::K::kDouble;
      out->i = static_cast<int64_t>(raw);
      return true;
    case 0x15:  // VALUE_METHOD_TYPE（proto idx）
    case 0x16:  // VALUE_METHOD_HANDLE
    case 0x17:  // VALUE_STRING
    case 0x18:  // VALUE_TYPE
    case 0x19:  // VALUE_FIELD
    case 0x1a:  // VALUE_METHOD
    case 0x1b: {  // VALUE_ENUM
      if (!ReadFixedLE(pp, end, width, &raw)) return false;
      switch (type) {
        case 0x17: out->k = DexAnnoVal::K::kString; break;
        case 0x18: out->k = DexAnnoVal::K::kType; break;
        case 0x19: out->k = DexAnnoVal::K::kField; break;
        case 0x1a: out->k = DexAnnoVal::K::kMethod; break;
        case 0x1b: out->k = DexAnnoVal::K::kEnum; break;
        default: out->k = DexAnnoVal::K::kInt; break;  // method_type/handle 当 idx 整型
      }
      out->idx = static_cast<uint32_t>(raw);
      return true;
    }
    case 0x1c:  // VALUE_ARRAY
      out->k = DexAnnoVal::K::kArray;
      return DecodeAnnoArray(dex, pp, end, &out->arr, depth);
    case 0x1d: {  // VALUE_ANNOTATION
      uint32_t type_idx = 0, n = 0;
      if (!dex::ReadUleb128(pp, end, &type_idx)) return false;
      if (!dex::ReadUleb128(pp, end, &n)) return false;
      out->k = DexAnnoVal::K::kAnno;
      out->anno_type = std::string(dex->GetTypeDescriptor(type_idx));
      return DecodeAnnoElements(dex, pp, end, n, &out->anno_elems, depth);
    }
    case 0x1e:  // VALUE_NULL
      out->k = DexAnnoVal::K::kNull;
      return true;
    case 0x1f:  // VALUE_BOOLEAN（值在 arg）
      out->k = DexAnnoVal::K::kInt;
      out->i = arg != 0 ? 1 : 0;
      return true;
    default:
      return false;
  }
}

// annotation_set_item → 注解项解码
bool DecodeAnnoSet(dex::DexFile* dex, uint32_t set_off, std::vector<DexAnnoItem>* out) {
  if (set_off == 0 || set_off >= dex->size()) return true;
  const uint8_t* base = dex->data();
  const uint8_t* end = base + dex->size();
  const uint8_t* p = base + set_off;
  uint32_t count = 0;
  if (p + 4 > end) return false;
  std::memcpy(&count, p, 4);
  p += 4;
  if (p + static_cast<size_t>(count) * 4 > end) return false;
  for (uint32_t i = 0; i < count; i++) {
    uint32_t aoff = 0;
    std::memcpy(&aoff, p + 4 * i, 4);
    if (aoff == 0 || aoff >= dex->size()) continue;
    const uint8_t* ai = base + aoff;
    if (ai + 1 > end) return false;
    DexAnnoItem item;
    item.visibility = *ai++;
    uint32_t type_idx = 0, n = 0;
    if (!dex::ReadUleb128(&ai, end, &type_idx)) return false;
    if (!dex::ReadUleb128(&ai, end, &n)) return false;
    item.type = std::string(dex->GetTypeDescriptor(type_idx));
    if (!DecodeAnnoElements(dex, &ai, end, n, &item.elems, 0)) return false;
    out->push_back(std::move(item));
  }
  return true;
}

// 成员注解装载：kind = 'C'（类级）/'M'（方法）/'F'（字段）；idx 为 method_idx/field_idx。
bool LoadMemberAnnotations(dex::DexFile* dex, uint32_t class_def_idx, char kind, uint32_t idx,
                           std::vector<DexAnnoItem>* out) {
  out->clear();
  if (dex == nullptr || class_def_idx >= dex->NumClassDefs()) return false;
  const dex::ClassDef& cd = dex->GetClassDef(class_def_idx);
  if (cd.annotations_off == 0 || cd.annotations_off >= dex->size()) return true;
  const uint8_t* base = dex->data();
  const uint8_t* end = base + dex->size();
  const uint8_t* p = base + cd.annotations_off;
  uint32_t class_ann_off = 0, fields_size = 0, methods_size = 0, params_size = 0;
  if (p + 16 > end) return false;
  std::memcpy(&class_ann_off, p, 4);
  std::memcpy(&fields_size, p + 4, 4);
  std::memcpy(&methods_size, p + 8, 4);
  std::memcpy(&params_size, p + 12, 4);
  p += 16;
  if (kind == 'C') return DecodeAnnoSet(dex, class_ann_off, out);
  // dex 布局：field_annotation[fields_size] 紧随 16 字节头，method_annotation
  // 在其后（p + fields_size*8）。字段/方法两个列表不可互换——按 kind 选段。
  const uint8_t* list = p;
  uint32_t list_size = fields_size;
  if (kind == 'M') {
    list = p + static_cast<size_t>(fields_size) * 8;
    list_size = methods_size;
  }
  if (list + static_cast<size_t>(list_size) * 8 > end) return false;
  for (uint32_t i = 0; i < list_size; i++) {
    uint32_t member_idx = 0, ann_off = 0;
    std::memcpy(&member_idx, list + 8 * i, 4);
    std::memcpy(&ann_off, list + 8 * i + 4, 4);
    if (member_idx != idx) continue;
    return DecodeAnnoSet(dex, ann_off, out);
  }
  return true;
}

// 参数注解装载（annotations_directory_item.annotated_parameters 段 →
// annotation_set_ref_list → 逐形参 annotation_set_item）。out 按 dex 形参序，
// 每项是该参数的注解组；无 annotations directory / 未登记 = 空（调用方按
// 形参数补空组）。Retrofit ServiceMethod.Builder 依赖本面解析 @Path/@Query 等。
bool LoadParameterAnnotations(dex::DexFile* dex, uint32_t class_def_idx, uint32_t method_idx,
                              std::vector<std::vector<DexAnnoItem>>* out) {
  out->clear();
  if (dex == nullptr || class_def_idx >= dex->NumClassDefs()) return false;
  const dex::ClassDef& cd = dex->GetClassDef(class_def_idx);
  if (cd.annotations_off == 0 || cd.annotations_off >= dex->size()) return true;
  const uint8_t* base = dex->data();
  const uint8_t* end = base + dex->size();
  const uint8_t* p = base + cd.annotations_off;
  uint32_t class_ann_off = 0, fields_size = 0, methods_size = 0, params_size = 0;
  if (p + 16 > end) return false;
  std::memcpy(&class_ann_off, p, 4);
  std::memcpy(&fields_size, p + 4, 4);
  std::memcpy(&methods_size, p + 8, 4);
  std::memcpy(&params_size, p + 12, 4);
  (void)class_ann_off;
  p += 16;
  // 段序：class_ann（头内）→ fields → methods → parameters（dex 规范布局）
  const uint8_t* list =
      p + static_cast<size_t>(fields_size) * 8 + static_cast<size_t>(methods_size) * 8;
  if (list + static_cast<size_t>(params_size) * 8 > end) return false;
  for (uint32_t i = 0; i < params_size; i++) {
    uint32_t member_idx = 0, ref_off = 0;
    std::memcpy(&member_idx, list + 8 * i, 4);
    std::memcpy(&ref_off, list + 8 * i + 4, 4);
    if (member_idx != method_idx) continue;
    if (ref_off == 0 || ref_off >= dex->size()) return true;
    const uint8_t* rl = base + ref_off;
    if (rl + 4 > end) return false;
    uint32_t count = 0;
    std::memcpy(&count, rl, 4);
    rl += 4;
    if (rl + static_cast<size_t>(count) * 4 > end) return false;
    out->resize(count);
    for (uint32_t j = 0; j < count; j++) {
      uint32_t set_off = 0;
      std::memcpy(&set_off, rl + 4 * j, 4);
      if (!DecodeAnnoSet(dex, set_off, &(*out)[j])) return false;
    }
    return true;
  }
  return true;
}

// 类泛型签名文本（Ldalvik/annotation/Signature; value 片段拼接）。
// 规范解码口径（fixed-width LE）；无签名属性返回 false（退化语义走 Class 形态）。
bool ReadClassSignatureText(VClass* k, std::string* out) {
  if (k == nullptr || k->dex_file == nullptr) return false;
  std::vector<DexAnnoItem> annos;
  if (!LoadMemberAnnotations(k->dex_file, k->class_def_idx, 'C', 0, &annos)) return false;
  for (const auto& a : annos) {
    if (a.type != "Ldalvik/annotation/Signature;") continue;
    for (const auto& e : a.elems) {
      if (e.second.k != DexAnnoVal::K::kArray) continue;
      std::string sig;
      for (const auto& part : e.second.arr) {
        if (part.k != DexAnnoVal::K::kString) return false;
        sig += k->dex_file->GetString(part.idx);
      }
      if (!sig.empty()) {
        *out = std::move(sig);
        return true;
      }
    }
  }
  return false;
}

// 方法/构造器泛型签名文本（成员级 Ldalvik/annotation/Signature; 注解）。
bool ReadMethodSignatureText(VMethod* m, std::string* out) {
  if (m == nullptr || m->declaring == nullptr || m->dex_file == nullptr) return false;
  std::vector<DexAnnoItem> annos;
  if (!LoadMemberAnnotations(m->dex_file, m->declaring->class_def_idx, 'M', m->method_idx,
                             &annos)) {
    return false;
  }
  for (const auto& a : annos) {
    if (a.type != "Ldalvik/annotation/Signature;") continue;
    for (const auto& e : a.elems) {
      if (e.second.k != DexAnnoVal::K::kArray) continue;
      std::string sig;
      for (const auto& part : e.second.arr) {
        if (part.k != DexAnnoVal::K::kString) return false;
        sig += m->dex_file->GetString(part.idx);
      }
      if (!sig.empty()) {
        *out = std::move(sig);
        return true;
      }
    }
  }
  return false;
}

// ---- lib Type 镜像构造（sun.reflect.generics.reflectiveObjects POJO）----
void SetMirrorRef(VObject* o, const char* field, ObjHeader* v) {
  VField* f = FindInstField(o->klass, field);
  if (f == nullptr) return;
  ObjHeader** slot = reinterpret_cast<ObjHeader**>(o->field_data() + f->offset);
  *slot = v;
}

VObject* AllocTypeMirror(VMState& state, const char* desc) {
  VClass* k = state.linker != nullptr ? state.linker->FindClass(desc) : nullptr;
  if (k == nullptr) return nullptr;
  VObject* o = state.linker->AllocObject(k);
  return o;
}

// 嵌套类 owner 语义（OpenJDK：签名未显式带外层时按封闭类派生;
// M 级按内部名最后一个 $ 切分近似 getEnclosingClass）
ObjHeader* DeriveOwner(VMState& state, const std::string& binary_name) {
  size_t dollar = binary_name.rfind('$');
  if (dollar == std::string::npos || dollar == 0) return nullptr;
  std::string owner_name = binary_name.substr(0, dollar);
  VClass* oc = state.linker->FindClass("L" + owner_name + ";");
  return reinterpret_cast<ObjHeader*>(oc);
}

struct ReifyCtx {
  VMState* state = nullptr;
  VClass* decl = nullptr;       // 声明类（TypeVariable.genericDeclaration）
  const ClassSig* sig = nullptr;
  const MethodSig* msig = nullptr;  // 方法级类型参数（优先于类级同名参数）
  ObjHeader* decl_obj = nullptr;    // 方法镜像时 TypeVariable.genericDeclaration
  // 递归类型变量 bound 防环（如 `<T:Lnode/e<TT;>;>` 自引用/CRTP）：
  // key = genericDeclaration 身份 + 形参名 → 先建 TypeVariableImpl 骨架并登记，
  // bound 内再次引用同名形参时复用骨架，避免 reify 无限递归（depth 截断 -> 失败）。
  std::map<std::string, ObjHeader*>* var_in_progress = nullptr;
};

// reify 失败必须挂 pending exception（intrinsic 裸 *ok=false 会触发解释器
// 同 pc 无限重试——挂死根因防御，见 interpreter.cc DoCall 失败兜底）。
void FailReify(VMState& state) {
  if (!state.HasPendingException()) {
    state.ThrowNew("Ljava/lang/InternalError;", "generic signature reify failed");
  }
}

bool ReifyType(ReifyCtx& ctx, const GNode& n, ObjHeader** out, int depth);

bool ReifyList(ReifyCtx& ctx, const std::vector<GNode>& nodes, VArray** out, int depth) {
  VMState& state = *ctx.state;
  VClass* type_cls = state.linker->FindClass("Ljava/lang/reflect/Type;");
  if (type_cls == nullptr) return false;
  VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(type_cls),
                                         static_cast<int32_t>(nodes.size()));
  if (arr == nullptr) return false;
  for (size_t i = 0; i < nodes.size(); i++) {
    ObjHeader* t = nullptr;
    if (!ReifyType(ctx, nodes[i], &t, depth + 1)) return false;
    ArrSetRef(arr, static_cast<int32_t>(i), t);
  }
  *out = arr;
  return true;
}

bool ReifyType(ReifyCtx& ctx, const GNode& n, ObjHeader** out, int depth) {
  VMState& state = *ctx.state;
  if (depth > 64) return false;
  switch (n.k) {
    case GNode::K::kPrim: {
      VClass* pc = state.linker->PrimitiveClass(n.name[0]);
      if (pc == nullptr) return false;
      if (pc->klass == nullptr) pc->klass = state.linker->FindClass("Ljava/lang/Class;");
      *out = reinterpret_cast<ObjHeader*>(pc);
      return true;
    }
    case GNode::K::kArray: {
      // 分量为普通类/基元 → 数组 Class;否则 GenericArrayTypeImpl
      ObjHeader* comp = nullptr;
      if (!ReifyType(ctx, n.args[0], &comp, depth + 1)) return false;
      VClass* comp_cls = reinterpret_cast<VClass*>(comp);
      bool plain = comp_cls != nullptr && comp_cls->klass != nullptr &&
                   comp_cls->klass->descriptor == "Ljava/lang/Class;";
      if (plain) {
        VClass* arr_cls = state.linker->FindArrayClass(comp_cls);
        if (arr_cls == nullptr) return false;
        if (arr_cls->klass == nullptr) arr_cls->klass = state.linker->FindClass("Ljava/lang/Class;");
        *out = reinterpret_cast<ObjHeader*>(arr_cls);
        return true;
      }
      VObject* o = AllocTypeMirror(state,
                                   "Lsun/reflect/generics/reflectiveObjects/GenericArrayTypeImpl;");
      if (o == nullptr) return false;
      SetMirrorRef(o, "genericComponentType", comp);
      *out = o;
      return true;
    }
    case GNode::K::kWild: {
      VObject* o = AllocTypeMirror(state,
                                   "Lsun/reflect/generics/reflectiveObjects/WildcardTypeImpl;");
      if (o == nullptr) return false;
      // OpenJDK：上界恒非空（无显式上界时 [Object]）;`? super Y` 上界 [Object]
      std::vector<GNode> real_upper = n.upper;
      if (real_upper.empty()) {
        GNode obj;
        obj.k = GNode::K::kClass;
        obj.name = "java/lang/Object";
        real_upper.push_back(std::move(obj));
      }
      VArray* ua = nullptr;
      VArray* la = nullptr;
      if (!ReifyList(ctx, real_upper, &ua, depth + 1)) return false;
      if (!ReifyList(ctx, n.lower, &la, depth + 1)) return false;
      SetMirrorRef(o, "upperBounds", ua);
      SetMirrorRef(o, "lowerBounds", la);
      *out = o;
      return true;
    }
    case GNode::K::kVar: {
      // 身份键（genericDeclaration, name）：方法级声明优先。
      std::string key;
      {
        uintptr_t decl_id =
            ctx.decl_obj != nullptr ? reinterpret_cast<uintptr_t>(ctx.decl_obj)
                                    : reinterpret_cast<uintptr_t>(ctx.decl);
        key = std::to_string(decl_id) + ":" + n.name;
      }
      if (ctx.var_in_progress != nullptr) {
        auto it = ctx.var_in_progress->find(key);
        if (it != ctx.var_in_progress->end()) {
          *out = it->second;  // 递归 bound 引用：复用已建骨架（环图，语义同 JDK 惰性 bound）
          return true;
        }
      }
      VObject* o =
          AllocTypeMirror(state, "Lsun/reflect/generics/reflectiveObjects/TypeVariableImpl;");
      if (o == nullptr) return false;
      if (ctx.var_in_progress != nullptr) {
        (*ctx.var_in_progress)[key] = reinterpret_cast<ObjHeader*>(o);
      }
      VString* name = state.linker->AllocString(std::u16string(n.name.begin(), n.name.end()));
      // bounds 取形式声明（方法级优先，同名形式参数的 bounds;未声明退化 Object）
      std::vector<GNode> bounds;
      bool found = false;
      if (ctx.msig != nullptr) {
        for (size_t i = 0; i < ctx.msig->formal_names.size(); i++) {
          if (ctx.msig->formal_names[i] == n.name) {
            bounds = ctx.msig->formal_bounds[i];
            found = true;
            break;
          }
        }
      }
      if (!found && ctx.sig != nullptr) {
        for (size_t i = 0; i < ctx.sig->formal_names.size(); i++) {
          if (ctx.sig->formal_names[i] == n.name) {
            bounds = ctx.sig->formal_bounds[i];
            break;
          }
        }
      }
      if (bounds.empty()) {
        GNode obj;
        obj.k = GNode::K::kClass;
        obj.name = "java/lang/Object";
        bounds.push_back(std::move(obj));
      }
      VArray* ba = nullptr;
      if (!ReifyList(ctx, bounds, &ba, depth + 1)) {
        if (ctx.var_in_progress != nullptr) ctx.var_in_progress->erase(key);
        return false;
      }
      SetMirrorRef(o, "name", reinterpret_cast<ObjHeader*>(name));
      SetMirrorRef(o, "genericDeclaration",
                   ctx.decl_obj != nullptr ? ctx.decl_obj
                                           : reinterpret_cast<ObjHeader*>(ctx.decl));
      SetMirrorRef(o, "bounds", ba);
      if (ctx.var_in_progress != nullptr) ctx.var_in_progress->erase(key);
      *out = o;
      return true;
    }
    case GNode::K::kClass: {
      VClass* raw = state.linker->FindClass("L" + n.name + ";");
      if (raw == nullptr) {
        state.ThrowNew("Ljava/lang/TypeNotPresentException;", n.name);
        return false;
      }
      if (n.args.empty() && !n.has_outer) {
        // 无类型实参的顶层类 → Class 形态（OpenJDK reifier 语义）
        *out = reinterpret_cast<ObjHeader*>(raw);
        return true;
      }
      VArray* ta = nullptr;
      if (!ReifyList(ctx, n.args, &ta, depth + 1)) return false;
      ObjHeader* owner = nullptr;
      if (n.has_outer && !n.outer.empty()) {
        if (!ReifyType(ctx, n.outer[0], &owner, depth + 1)) return false;
      } else {
        owner = DeriveOwner(state, n.name);
      }
      VObject* o = AllocTypeMirror(state,
                                   "Lsun/reflect/generics/reflectiveObjects/ParameterizedTypeImpl;");
      if (o == nullptr) return false;
      SetMirrorRef(o, "rawType", reinterpret_cast<ObjHeader*>(raw));
      SetMirrorRef(o, "actualTypeArguments", ta);
      SetMirrorRef(o, "ownerType", owner);
      *out = o;
      return true;
    }
  }
  return false;
}

}  // namespace

// Class.getGenericSuperclass：签名解析 → ParameterizedType/Class;接口恒 null
// （对齐 vendored java.lang.Class.java isInterface 分支）。
Value ClsGetGenericSuperclass(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)args; (void)n;
  Value r{};
  VClass* k = reinterpret_cast<VClass*>(self);
  if (k == nullptr || k->IsInterface()) return r;
  std::string sig;
  if (ReadClassSignatureText(k, &sig)) {
    ClassSig csig;
    if (ParseClassSignature(sig, &csig)) {
      std::map<std::string, ObjHeader*> inprog;
      ReifyCtx ctx{&state, k, &csig};
      ctx.var_in_progress = &inprog;
      ObjHeader* t = nullptr;
      if (!ReifyType(ctx, csig.superclass, &t, 0)) {
        FailReify(state);
        *ok = false;
        return Value{};
      }
      r.ref = t;
      return r;
    }
  }
  // 退化语义：无签名（或签名解析失败）→ superclass.class
  if (!k->is_primitive) r.ref = reinterpret_cast<ObjHeader*>(k->super_class);
  return r;
}

// Class.getGenericInterfaces：签名解析 → Type[]（参数化接口为 ParameterizedType）。
Value ClsGetGenericInterfaces(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  VClass* type_cls = state.linker->FindClass("Ljava/lang/reflect/Type;");
  if (type_cls == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "java.lang.reflect.Type missing");
    *ok = false;
    return Value{};
  }
  std::string sig;
  bool parsed = false;
  ClassSig csig;
  if (ReadClassSignatureText(k, &sig)) parsed = ParseClassSignature(sig, &csig);
  size_t count = parsed ? csig.interfaces.size() : (k != nullptr ? k->interfaces.size() : 0u);
  VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(type_cls),
                                         static_cast<int32_t>(count));
  if (arr == nullptr) {
    ThrowOOM(state, "getGenericInterfaces");
    *ok = false;
    return Value{};
  }
  if (parsed) {
    std::map<std::string, ObjHeader*> inprog;
    ReifyCtx ctx{&state, k, &csig};
    ctx.var_in_progress = &inprog;
    for (size_t i = 0; i < csig.interfaces.size(); i++) {
      ObjHeader* t = nullptr;
      if (!ReifyType(ctx, csig.interfaces[i], &t, 0)) {
        FailReify(state);
        *ok = false;
        return Value{};
      }
      ArrSetRef(arr, static_cast<int32_t>(i), t);
    }
  } else if (k != nullptr) {
    // 退化语义：接口 Class 形态（Class implements Type,补挂接口向量）
    for (size_t i = 0; i < k->interfaces.size(); i++) {
      ArrSetRef(arr, static_cast<int32_t>(i), reinterpret_cast<ObjHeader*>(k->interfaces[i]));
    }
  }
  Value r{};
  r.ref = arr;
  return r;
}

// Class.getTypeParameters：签名 TypeParameters → TypeVariable[]（bound 已解析）。
Value ClsGetTypeParameters(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  VClass* tv_cls = state.linker->FindClass("Ljava/lang/reflect/TypeVariable;");
  if (tv_cls == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "java.lang.reflect.TypeVariable missing");
    *ok = false;
    return Value{};
  }
  ClassSig csig;
  bool have_sig = false;
  std::string sig;
  if (ReadClassSignatureText(k, &sig)) have_sig = ParseClassSignature(sig, &csig);
  size_t count = have_sig ? csig.formal_names.size() : 0u;
  VArray* arr =
      state.linker->AllocArray(state.linker->FindArrayClass(tv_cls), static_cast<int32_t>(count));
  if (arr == nullptr) {
    ThrowOOM(state, "getTypeParameters");
    *ok = false;
    return Value{};
  }
  if (have_sig) {
    std::map<std::string, ObjHeader*> inprog;
    ReifyCtx ctx{&state, k, &csig};
    ctx.var_in_progress = &inprog;
    for (size_t i = 0; i < count; i++) {
      GNode var;
      var.k = GNode::K::kVar;
      var.name = csig.formal_names[i];
      ObjHeader* t = nullptr;
      if (!ReifyType(ctx, var, &t, 0)) {
#if defined(__ANDROID__)
        // TEMP DIAG：泛型形参 reify 失败归因（config 解析链 TypeBindings；事后移除）。
        __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                            "getTypeParameters FAILED cls=%s var=%s sig=%s",
                            k != nullptr ? k->descriptor.c_str() : "?", var.name.c_str(),
                            sig.c_str());
#endif
        FailReify(state);
        *ok = false;
        return Value{};
      }
      ArrSetRef(arr, static_cast<int32_t>(i), t);
    }
  }
  Value r{};
  r.ref = arr;
  return r;
}

// InvocationTargetException 包装（target 字段 @8；ART Method.invoke 语义）
namespace {
void WrapInvocationTarget(VMState& state, ObjHeader* cause) {
  VClass* k =
      state.linker != nullptr ? state.linker->FindClass("Ljava/lang/reflect/InvocationTargetException;")
                              : nullptr;
  ObjHeader* ex = nullptr;
  if (k != nullptr) ex = reinterpret_cast<ObjHeader*>(state.linker->AllocObject(k));
  if (ex == nullptr) {
    if (cause != nullptr) state.Throw(cause);
    return;
  }
  VObject* o = static_cast<VObject*>(ex);
  VField* tf = FindInstField(k, "target");
  if (tf != nullptr) MirrorSet(o, tf->offset, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(cause)));
  state.Throw(ex);
}

// Object[] 参数铺陈 -> Value 序列（逐参拆箱/类型检查；ART reflect 调用约定）
bool MarshalReflectArgs(VMState& state, VMethod* m, VArray* argv, std::vector<Value>* full) {
  size_t argc = argv != nullptr ? static_cast<size_t>(argv->length) : 0u;
  if (argc != m->param_types.size()) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                   "wrong number of arguments: " + std::to_string(argc) + " vs " +
                       std::to_string(m->param_types.size()));
    return false;
  }
  for (size_t i = 0; i < argc; i++) {
    ObjHeader* a = ArrRef(argv, static_cast<int32_t>(i));
    const std::string& pt = m->param_types[i];
    char tc = pt[0];
    Value pv{};
    if (tc == 'L' || tc == '[') {
      if (a != nullptr && a->klass != nullptr && state.linker != nullptr) {
        VClass* pc = state.linker->FindClass(pt);
        if (pc != nullptr && !pc->IsAssignableFrom(a->klass)) {
          state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                         "argument type mismatch: " + a->klass->descriptor + " for " + pt);
          return false;
        }
      }
      pv.ref = a;
    } else {
      if (!UnboxValue(state, a, tc, &pv)) return false;
    }
    full->push_back(pv);
  }
  return true;
}

// ---- java.lang.reflect.AccessibleObject ----
}  // namespace
Value AOASetAccessible(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  MirrorSet(self, kMirrorAcc, (n > 0 && args[0].i != 0) ? 1u : 0u);
  return Value{};
}
Value AOAIsAccessible(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = MirrorAccessible(self) ? 1 : 0;
  return r;
}

// ---- java.lang.reflect.Method / Constructor ----
Value MemberGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VMethod* m = MirrorMethod(self);
  std::string name = m != nullptr ? m->name : "";
  if (m != nullptr && m->name == "<init>" && m->declaring != nullptr) name = ClassName(m->declaring);
  std::u16string out(name.begin(), name.end());
  return MakeStr(state, out, ok);
}
Value MemberGetDeclaringClass(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(MirrorGet(self, kMirrorDecl)));
  return r;
}
Value MemberGetModifiers(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VMethod* m = MirrorMethod(self);
  Value r{};
  r.i = static_cast<int32_t>(m != nullptr ? (m->access_flags & 0xffffu) : 0u);
  return r;
}
Value MethodGetReturnType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  VMethod* m = MirrorMethod(self);
  Value r{};
  if (m != nullptr && state.linker != nullptr) {
    r.ref = reinterpret_cast<ObjHeader*>(state.linker->FindClass(m->return_type));
  }
  return r;
}
namespace {
Value MethodFlagProbe(VMState& state, VObject* self, uint32_t flag, bool* ok) {
  (void)state;
  VMethod* m = MirrorMethod(self);
  Value r{};
  r.i = (m != nullptr && (m->access_flags & flag) != 0) ? 1 : 0;
  *ok = true;
  return r;
}
}  // namespace
Value MethodGetParameterCount(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  VMethod* m = MirrorMethod(self);
  Value r{};
  r.i = m != nullptr ? static_cast<int32_t>(m->param_types.size()) : 0;
  *ok = true;
  return r;
}
Value MethodIsBridge(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  return MethodFlagProbe(state, self, 0x40u, ok);  // ACC_BRIDGE
}
Value MethodIsVarArgs(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  return MethodFlagProbe(state, self, 0x80u, ok);  // ACC_VARARGS
}
Value MethodIsSynthetic(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  return MethodFlagProbe(state, self, 0x1000u, ok);  // ACC_SYNTHETIC
}
Value MethodIsDefault(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  (void)state;
  VMethod* m = MirrorMethod(self);
  bool result = false;
  if (m != nullptr) {
    VClass* decl = reinterpret_cast<VClass*>(
        static_cast<uintptr_t>(MirrorGet(self, kMirrorDecl)));
    result = decl != nullptr && decl->IsInterface() && (m->access_flags & 0x400u) == 0 &&
             (m->access_flags & 0x8u) == 0;
  }
  Value r{};
  r.i = result ? 1 : 0;
  *ok = true;
  return r;
}

Value MethodGetParameterTypes(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)args; (void)n;
  VMethod* m = MirrorMethod(self);
  Value r{};
  if (m == nullptr || state.linker == nullptr) return r;
#if defined(__ANDROID__)
  if (m->declaring != nullptr &&
      m->declaring->descriptor == "Lcom/zhihu/android/api/model/UDIDGuestInfo;") {
    static int anno_logs = 0;  // 降噪：该反射路径高频（防 logcat 拖慢）
    if (anno_logs++ < 50) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "getParameterTypes %s n=%zu",
                          m->name.c_str(), m->param_types.size());
    }
  }
#endif
  VClass* cls_class = state.linker->FindClass("Ljava/lang/Class;");
  VClass* arr_class = state.linker->FindArrayClass(cls_class);
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(m->param_types.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getParameterTypes");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < m->param_types.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i),
              reinterpret_cast<ObjHeader*>(state.linker->FindClass(m->param_types[i])));
  }
  r.ref = arr;
  return r;
}
namespace {
// Type[] 兜底面（方法级泛型签名解析后置：无签名时 JDK 返回原始 Class 形态）
Value BuildTypeArray(VMState& state, const std::vector<std::string>& descs, bool* ok) {
  VClass* type_class = state.linker->FindClass("Ljava/lang/reflect/Type;");
  VClass* arr_class = state.linker->FindArrayClass(type_class);
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(descs.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "generic type array");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < descs.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i),
              reinterpret_cast<ObjHeader*>(state.linker->FindClass(descs[i])));
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}
}  // namespace
Value MethodGetGenericParameterTypes(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok) {
  (void)args;
  (void)n;
  VMethod* m = MirrorMethod(self);
  if (m == nullptr) return Value{};
  // dex 方法 Signature 解析 → 逐形参 Type（ParameterizedType/TypeVariable/
  // WildcardType/GenericArrayType 镜像）；无签名或形参数不符 → 原始类型回退。
  std::string sig;
  MethodSig msig;
  if (m->declaring != nullptr && ReadMethodSignatureText(m, &sig) &&
      ParseMethodSignature(sig, &msig) && msig.params.size() == m->param_types.size()) {
    VClass* type_cls = state.linker->FindClass("Ljava/lang/reflect/Type;");
    VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(type_cls),
                                           static_cast<int32_t>(msig.params.size()));
    if (arr == nullptr) {
      ThrowOOM(state, "getGenericParameterTypes");
      *ok = false;
      return Value{};
    }
    std::string class_sig;
    ClassSig csig;
    bool have_csig = ReadClassSignatureText(m->declaring, &class_sig) &&
                     ParseClassSignature(class_sig, &csig);
    std::map<std::string, ObjHeader*> inprog;
    ReifyCtx ctx{&state, m->declaring, have_csig ? &csig : nullptr, &msig, self};
    ctx.var_in_progress = &inprog;
    for (size_t i = 0; i < msig.params.size(); i++) {
      ObjHeader* t = nullptr;
      if (!ReifyType(ctx, msig.params[i], &t, 0)) {
        FailReify(state);
        *ok = false;
        return Value{};
      }
      ArrSetRef(arr, static_cast<int32_t>(i), t);
    }
    Value r{};
    r.ref = arr;
    *ok = true;
    return r;
  }
  return BuildTypeArray(state, m->param_types, ok);
}
Value MethodGetGenericExceptionTypes(VMState& state, VObject* self, const Value* args, size_t n,
                                     bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  return BuildTypeArray(state, {}, ok);
}
Value MethodGetGenericReturnType(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  (void)args;
  (void)n;
  VMethod* m = MirrorMethod(self);
  Value r{};
  *ok = true;
  if (m == nullptr) return r;
  std::string sig;
  MethodSig msig;
  if (m->declaring != nullptr && ReadMethodSignatureText(m, &sig) &&
      ParseMethodSignature(sig, &msig)) {
    std::string class_sig;
    ClassSig csig;
    bool have_csig = ReadClassSignatureText(m->declaring, &class_sig) &&
                     ParseClassSignature(class_sig, &csig);
    std::map<std::string, ObjHeader*> inprog;
    ReifyCtx ctx{&state, m->declaring, have_csig ? &csig : nullptr, &msig, self};
    ctx.var_in_progress = &inprog;
    ObjHeader* t = nullptr;
    if (ReifyType(ctx, msig.ret, &t, 0)) {
      r.ref = t;
      return r;
    }
    FailReify(state);
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->FindClass(m->return_type));
  return r;
}
Value MethodGetExceptionTypes(VMState& state, VObject* self, const Value* args, size_t n,
                              bool* ok) {
  (void)self; (void)args; (void)n;
  // VM 内建不登记 throws 元数据：空数组（Android 语义：未声明受检异常）
  Value r{};
  VClass* cls_class = state.linker->FindClass("Ljava/lang/Class;");
  VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(cls_class), 0);
  if (arr == nullptr) {
    ThrowOOM(state, "getExceptionTypes");
    *ok = false;
    return r;
  }
  r.ref = arr;
  return r;
}
Value MethodInvoke(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VMethod* m = MirrorMethod(self);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/reflect/InvocationTargetException;", "null method mirror");
    *ok = false;
    return Value{};
  }
  ObjHeader* recv = (n > 0) ? args[0].ref : nullptr;
  VArray* argv =
      (n > 1 && args[1].ref != nullptr) ? reinterpret_cast<VArray*>(args[1].ref) : nullptr;
  if (!MemberAccessible(self, m->access_flags, m->declaring)) {
    state.ThrowNew("Ljava/lang/IllegalAccessException;", MethodSigStr(m));
    *ok = false;
    return Value{};
  }
  if (!m->IsStatic() && recv == nullptr) {
    ThrowNullPointerException(state, "Method.invoke on null receiver");
    *ok = false;
    return Value{};
  }
  std::vector<Value> full;
  if (!m->IsStatic()) {
    Value s{};
    s.ref = recv;
    full.push_back(s);
  }
  if (!MarshalReflectArgs(state, m, argv, &full)) {
    *ok = false;
    return Value{};
  }
  // 虚派发（java.lang.reflect.Method.invoke 语义，JLS 15.12.4.4）：非 static
  // 且非私有实例方法按接收者运行时类动态查找（接口方法镜像经此落到实现——
  // Proxy handler 链 method.invoke(target) 必经）；未命中回落镜像声明直调。
  VMethod* resolved = m;
  if (!m->IsStatic() && m->name != "<init>" && (m->access_flags & 0x2) == 0 && recv != nullptr &&
      recv->klass != nullptr) {
    if (VMethod* impl = recv->klass->FindInterfaceImpl(m)) resolved = impl;
  }
  Value r{};
  if (!InvokeMethod(state, resolved, full.data(), full.size(), &r)) {
    ObjHeader* cause = state.pending_exception;
    state.ClearException();
    WrapInvocationTarget(state, cause);
    *ok = false;
    return Value{};
  }
  if (m->return_type == "V") return Value{};
  ObjHeader* boxed = nullptr;
  if (!BoxValue(state, m->return_type[0], r, &boxed)) {
    *ok = false;
    return Value{};
  }
  Value out{};
  out.ref = boxed;
  return out;
}
Value CtorNewInstance(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VMethod* m = MirrorMethod(self);
  if (m == nullptr || m->declaring == nullptr) {
    state.ThrowNew("Ljava/lang/reflect/InvocationTargetException;", "null constructor mirror");
    *ok = false;
    return Value{};
  }
  if (!MemberAccessible(self, m->access_flags, m->declaring)) {
    state.ThrowNew("Ljava/lang/IllegalAccessException;", MethodSigStr(m));
    *ok = false;
    return Value{};
  }
  if (!CheckClassDefinition(state, m->declaring) ||
      !state.linker->InitializeClass(m->declaring, &state)) {
    *ok = false;
    return Value{};
  }
  VObject* obj = state.linker->AllocObject(m->declaring);
  if (obj == nullptr) {
    ThrowOOM(state, "Constructor.newInstance");
    *ok = false;
    return Value{};
  }
  VArray* argv =
      (n > 0 && args[0].ref != nullptr) ? reinterpret_cast<VArray*>(args[0].ref) : nullptr;
  std::vector<Value> full;
  Value s{};
  s.ref = obj;
  full.push_back(s);
  if (!MarshalReflectArgs(state, m, argv, &full)) {
    *ok = false;
    return Value{};
  }
  Value r{};
  if (!InvokeMethod(state, m, full.data(), full.size(), &r)) {
    ObjHeader* cause = state.pending_exception;
    state.ClearException();
    WrapInvocationTarget(state, cause);
    *ok = false;
    return Value{};
  }
  Value out{};
  out.ref = obj;
  return out;
}

// ---- java.lang.reflect.Field ----
Value FieldGetName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VField* f = MirrorField(self);
  std::string name = f != nullptr ? f->name : "";
  std::u16string out(name.begin(), name.end());
  return MakeStr(state, out, ok);
}
Value FieldGetDeclaringClass(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(MirrorGet(self, kMirrorDecl)));
  return r;
}
Value FieldGetType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  VField* f = MirrorField(self);
  Value r{};
  if (f != nullptr && state.linker != nullptr) {
    r.ref = reinterpret_cast<ObjHeader*>(state.linker->FindClass(f->type_descriptor));
  }
  return r;
}
// 字段泛型签名文本（字段级 Ldalvik/annotation/Signature; 注解）。
bool ReadFieldSignatureText(VField* f, std::string* out) {
  if (f == nullptr || f->declaring == nullptr || f->dex_file == nullptr) return false;
  std::vector<DexAnnoItem> annos;
  if (!LoadMemberAnnotations(f->dex_file, f->declaring->class_def_idx, 'F', f->field_idx,
                             &annos)) {
    return false;
  }
  for (const auto& a : annos) {
    if (a.type != "Ldalvik/annotation/Signature;") continue;
    for (const auto& e : a.elems) {
      if (e.second.k != DexAnnoVal::K::kArray) continue;
      std::string sig;
      for (const auto& part : e.second.arr) {
        if (part.k != DexAnnoVal::K::kString) return false;
        sig += f->dex_file->GetString(part.idx);
      }
      if (!sig.empty()) {
        *out = std::move(sig);
        return true;
      }
    }
  }
  return false;
}

Value FieldGetGenericType(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)args;
  (void)n;
  (void)ok;
  VField* f = MirrorField(self);
  Value r{};
  if (f == nullptr || state.linker == nullptr) return r;
  // 字段级泛型签名 → ParameterizedType/TypeVariable/GenericArrayType 镜像。
  // 缺此面时 List<SearchTab> 等字段经 Jackson 解析退化为原始 List（元素
  // LinkedHashMap），下游强转 SearchTab 抛 ClassCastException；feed/tabs
  // 模型普遍依赖字段泛型。
  std::string sig;
  if (ReadFieldSignatureText(f, &sig)) {
    GNode node;
    SigParser parser;
    parser.s = sig;
    if (ParseJavaType(parser, &node, 0) && parser.ok) {
      std::string class_sig;
      ClassSig csig;
      bool have_csig = ReadClassSignatureText(f->declaring, &class_sig) &&
                       ParseClassSignature(class_sig, &csig);
      std::map<std::string, ObjHeader*> inprog;
      ReifyCtx ctx{&state, f->declaring, have_csig ? &csig : nullptr, nullptr, nullptr};
      ctx.var_in_progress = &inprog;
      ObjHeader* t = nullptr;
      if (ReifyType(ctx, node, &t, 0)) {
        r.ref = t;
        return r;
      }
      FailReify(state);
      *ok = false;
      return r;
    }
  }
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->FindClass(f->type_descriptor));
  return r;
}
Value FieldGetModifiers(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VField* f = MirrorField(self);
  Value r{};
  r.i = static_cast<int32_t>(f != nullptr ? (f->access_flags & 0xffffu) : 0u);
  return r;
}
// Field.isSynthetic / isEnumConstant（反射面长尾：dex 访问标志投影；
// ACC_SYNTHETIC=0x1000 / ACC_ENUM=0x4000）
Value FieldIsSynthetic(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VField* f = MirrorField(self);
  Value r{};
  r.i = (f != nullptr && (f->access_flags & 0x1000u) != 0) ? 1 : 0;
  return r;
}
Value FieldIsEnumConstant(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VField* f = MirrorField(self);
  Value r{};
  r.i = (f != nullptr && (f->access_flags & 0x4000u) != 0) ? 1 : 0;
  return r;
}

// 字段槽窄读/窄写（8 字节槽 + 窄型收位，对照解释器 NarrowRead/NarrowWrite）
namespace {
Value FieldReadSlot(const uint8_t* p, char type) {
  uint64_t slot = 0;
  std::memcpy(&slot, p, 8);
  Value v{};
  switch (type) {
    case 'Z': v.i = static_cast<int32_t>(slot & 0xffu); break;
    case 'B': v.i = static_cast<int32_t>(static_cast<int8_t>(slot & 0xffu)); break;
    case 'S': v.i = static_cast<int32_t>(static_cast<int16_t>(slot & 0xffffu)); break;
    case 'C': v.i = static_cast<int32_t>(slot & 0xffffu); break;
    case 'I': v.i = static_cast<int32_t>(slot); break;
    case 'J': v.l = static_cast<int64_t>(slot); break;
    case 'F': {
      uint32_t bits = static_cast<uint32_t>(slot);
      std::memcpy(&v.i, &bits, 4);
      break;
    }
    case 'D': v.l = static_cast<int64_t>(slot); break;
    default: v.ref = reinterpret_cast<ObjHeader*>(static_cast<uintptr_t>(slot)); break;
  }
  return v;
}
void FieldWriteSlot(uint8_t* p, char type, const Value& v) {
  uint64_t slot = 0;
  switch (type) {
    case 'Z': slot = static_cast<uint64_t>(v.i) & 0xffu; break;
    case 'B': slot = static_cast<uint64_t>(static_cast<int64_t>(v.i)) & 0xffull; break;
    case 'S': slot = static_cast<uint64_t>(static_cast<int64_t>(v.i)) & 0xffffull; break;
    case 'C': slot = static_cast<uint64_t>(v.i) & 0xffffu; break;
    case 'I': slot = static_cast<uint64_t>(static_cast<int64_t>(v.i)); break;
    case 'J': slot = static_cast<uint64_t>(v.l); break;
    case 'F': {
      uint32_t bits = 0;
      std::memcpy(&bits, &v.i, 4);
      slot = bits;
      break;
    }
    case 'D': slot = static_cast<uint64_t>(v.l); break;
    default: slot = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(v.ref)); break;
  }
  std::memcpy(p, &slot, 8);
}

bool FieldSlotPtr(VMState& state, VObject* self, VObject* recv, uint8_t** out, char* type,
                  bool* ok) {
  VField* f = MirrorField(self);
  if (f == nullptr || f->declaring == nullptr) {
    state.ThrowNew("Ljava/lang/reflect/InvocationTargetException;", "null field mirror");
    *ok = false;
    return false;
  }
  if (!MemberAccessible(self, f->access_flags, f->declaring)) {
    state.ThrowNew("Ljava/lang/IllegalAccessException;", f->declaring->descriptor + "." + f->name);
    *ok = false;
    return false;
  }
  *type = f->type_descriptor.empty() ? 'L' : f->type_descriptor[0];
  if (f->is_static) {
    // 静态字段反射读写先确保声明类已初始化（OpenJDK Field.get/set 语义：
    // "the class that declared the field is initialized if it has not already
    // been initialized"；theUnsafe 反射获取链依赖此面）。
    if (!state.linker->InitializeClass(f->declaring, &state)) {
      *ok = false;
      return false;
    }
    if (f->offset >= f->declaring->static_storage.size()) {
      state.ThrowNew("Ljava/lang/NoSuchFieldError;", f->name + " storage missing");
      *ok = false;
      return false;
    }
    *out = reinterpret_cast<uint8_t*>(&f->declaring->static_storage[f->offset]);
    return true;
  }
  if (recv == nullptr) {
    ThrowNullPointerException(state, "Field.get/set on null receiver");
    *ok = false;
    return false;
  }
  *out = recv->field_data() + f->offset;
  return true;
}

}  // namespace
Value FieldGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  ObjHeader* recv = (n > 0) ? args[0].ref : nullptr;
  uint8_t* p = nullptr;
  char type = 'L';
  if (!FieldSlotPtr(state, self, static_cast<VObject*>(recv), &p, &type, ok)) return Value{};
  Value raw = FieldReadSlot(p, type);
  ObjHeader* boxed = nullptr;
  if (!BoxValue(state, type, raw, &boxed)) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = boxed;
  return r;
}
Value FieldSet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  ObjHeader* recv = (n > 0) ? args[0].ref : nullptr;
  ObjHeader* val = (n > 1) ? args[1].ref : nullptr;
  uint8_t* p = nullptr;
  char type = 'L';
  if (!FieldSlotPtr(state, self, static_cast<VObject*>(recv), &p, &type, ok)) return Value{};
  Value raw{};
  if (!UnboxValue(state, val, type, &raw)) {
    *ok = false;
    return Value{};
  }
  FieldWriteSlot(p, type, raw);
  return Value{};
}

// Field typed 访问面（AOSP get/setBoolean..get/setDouble）：严格类型校验 +
// 原值槽读写（libdexjni/ZHObjectRegistryCenter 反射注册链消费）。
Value FieldGetTypedImpl(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                        char want) {
  ObjHeader* recv = (n > 0) ? args[0].ref : nullptr;
  uint8_t* p = nullptr;
  char type = 'L';
  if (!FieldSlotPtr(state, self, static_cast<VObject*>(recv), &p, &type, ok)) return Value{};
  if (type != want) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                   std::string("field type mismatch, want ") + want);
    *ok = false;
    return Value{};
  }
  return FieldReadSlot(p, type);
}

Value FieldSetTypedImpl(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                        char want) {
  ObjHeader* recv = (n > 0) ? args[0].ref : nullptr;
  uint8_t* p = nullptr;
  char type = 'L';
  if (!FieldSlotPtr(state, self, static_cast<VObject*>(recv), &p, &type, ok)) return Value{};
  if (type != want) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                   std::string("field type mismatch, want ") + want);
    *ok = false;
    return Value{};
  }
  Value raw{};
  if (want == 'J' || want == 'D') {
    raw.l = (n > 1) ? args[1].l : 0;
  } else {
    raw.i = (n > 1) ? args[1].i : 0;
  }
  FieldWriteSlot(p, type, raw);
  return Value{};
}


// ---- java.lang.reflect.Array（lib native 面；BindWellKnownNatives 绑定）----
// 数组创建/元素访问是 VM 原语（无字节码可从 Class 值建数组），非 JLI 逻辑；
// 元素 = 8 字节槽（对照解释器 aget/aput），typed 变体共享 raw 读写。
// java.lang.invoke 裁剪家族（MethodType/MethodHandleDispatch 参数解析）与
// juc（PriorityBlockingQueue/TimSort 等）经 ArrayList.toArray(Array.newInstance)
// 消费本面。

namespace {
bool ArraySlotPtr(VMState& state, ObjHeader* arr_obj, int32_t idx, uint8_t** out, char* type,
                  bool* ok) {
  if (arr_obj == nullptr) {
    ThrowNullPointerException(state, "Array op on null");
    *ok = false;
    return false;
  }
  if (arr_obj->klass == nullptr || !arr_obj->klass->is_array) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "not an array");
    *ok = false;
    return false;
  }
  VArray* arr = static_cast<VArray*>(arr_obj);
  if (idx < 0 || idx >= arr->length) {
    ThrowArrayIndexOutOfBoundsException(state, idx, arr->length);
    *ok = false;
    return false;
  }
  VClass* comp = arr->klass->component_type;
  *type = (comp != nullptr && comp->is_primitive) ? comp->primitive_type : 'L';
  *out = arr->data() + static_cast<size_t>(idx) * 8;
  return true;
}

Value ArrayNewArrayDims(VMState& state, VClass* comp, const std::vector<int32_t>& dims,
                        bool* ok) {
  std::string desc(dims.size(), '[');
  desc += comp->descriptor;
  for (int32_t d : dims) {
    if (d < 0) {
      state.ThrowNew("Ljava/lang/NegativeArraySizeException;", std::to_string(d));
      *ok = false;
      return Value{};
    }
  }
  VClass* arr_cls = state.linker->FindClass(desc);
  if (arr_cls == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "array class missing: " + desc);
    *ok = false;
    return Value{};
  }
  VArray* arr = state.linker->AllocArray(arr_cls, dims[0]);
  if (arr == nullptr) {
    ThrowOOM(state, "Array.newArray");
    *ok = false;
    return Value{};
  }
  // 多维：递归建子数组（Java 语义：全展开）
  if (dims.size() > 1) {
    std::vector<int32_t> sub(dims.begin() + 1, dims.end());
    for (int32_t i = 0; i < dims[0]; i++) {
      Value sub_r = ArrayNewArrayDims(state, comp, sub, ok);
      if (!*ok) return Value{};
      uint64_t slot = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(sub_r.ref));
      std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &slot, sizeof(slot));
    }
  }
  Value r{};
  r.ref = arr;
  return r;
}

Value ArrayNewArrayCommon(VMState& state, const Value* args, size_t n, bool* ok,
                          const std::vector<int32_t>* dims_in) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Array componentType");
    *ok = false;
    return Value{};
  }
  VClass* comp = reinterpret_cast<VClass*>(args[0].ref);
  std::vector<int32_t> dims;
  if (dims_in != nullptr) {
    dims = *dims_in;
  } else {
    if (n < 2) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "Array.newArray dims");
      *ok = false;
      return Value{};
    }
    dims.push_back(args[1].i);
  }
  return ArrayNewArrayDims(state, comp, dims, ok);
}

}  // namespace
Value ArrayNewArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  return ArrayNewArrayCommon(state, args, n, ok, nullptr);
}

Value ArrayMultiNewArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 2 || args[1].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Array dims");
    *ok = false;
    return Value{};
  }
  VArray* dims_arr = static_cast<VArray*>(args[1].ref);
  std::vector<int32_t> dims;
  for (int32_t i = 0; i < dims_arr->length; i++) {
    uint64_t slot = 0;
    std::memcpy(&slot, dims_arr->data() + static_cast<size_t>(i) * 8, sizeof(slot));
    dims.push_back(static_cast<int32_t>(slot));
  }
  if (dims.empty()) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "empty dims");
    *ok = false;
    return Value{};
  }
  return ArrayNewArrayCommon(state, args, n, ok, &dims);
}

Value ArrayGetLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "Array.getLength");
    *ok = false;
    return Value{};
  }
  ObjHeader* o = args[0].ref;
  if (o->klass == nullptr || !o->klass->is_array) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "not an array");
    *ok = false;
    return Value{};
  }
  Value r{};
  r.i = static_cast<VArray*>(o)->length;
  return r;
}

Value ArrayGetBoxed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint8_t* p = nullptr;
  char type = 'L';
  int32_t idx = (n > 1) ? args[1].i : 0;
  if (!ArraySlotPtr(state, n > 0 ? args[0].ref : nullptr, idx, &p, &type, ok)) return Value{};
  Value raw = FieldReadSlot(p, type);
  ObjHeader* boxed = nullptr;
  if (!BoxValue(state, type, raw, &boxed)) {
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = boxed;
  return r;
}

// typed getter（getBoolean..getDouble）：按数组元素型 raw 返回
Value ArrayGetRaw(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint8_t* p = nullptr;
  char type = 'L';
  int32_t idx = (n > 1) ? args[1].i : 0;
  if (!ArraySlotPtr(state, n > 0 ? args[0].ref : nullptr, idx, &p, &type, ok)) return Value{};
  return FieldReadSlot(p, type);
}

Value ArraySetBoxed(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint8_t* p = nullptr;
  char type = 'L';
  int32_t idx = (n > 1) ? args[1].i : 0;
  if (!ArraySlotPtr(state, n > 0 ? args[0].ref : nullptr, idx, &p, &type, ok)) return Value{};
  ObjHeader* val = (n > 2) ? args[2].ref : nullptr;
  Value raw{};
  if (!UnboxValue(state, val, type, &raw)) {
    *ok = false;
    return Value{};
  }
  FieldWriteSlot(p, type, raw);
  return Value{};
}

// typed setter（setBoolean..setDouble）：按数组元素型 raw 写
Value ArraySetRaw(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  uint8_t* p = nullptr;
  char type = 'L';
  int32_t idx = (n > 1) ? args[1].i : 0;
  if (!ArraySlotPtr(state, n > 0 ? args[0].ref : nullptr, idx, &p, &type, ok)) return Value{};
  Value raw{};
  if (n > 2) {
    if (type == 'J' || type == 'D') {
      raw.l = args[2].l;
    } else {
      raw.i = args[2].i;
    }
  }
  FieldWriteSlot(p, type, raw);
  return Value{};
}

// ---- java.lang.Class 反射面（docs 14 章 29：壳 getDeclaredMethod/setAccessible 必用）----

// 参数 Class[] -> descriptor 列表
namespace {
bool ParamDescriptors(VMState& state, VArray* params, std::vector<std::string>* out) {
  if (params == nullptr) return true;
  for (int32_t i = 0; i < params->length; i++) {
    VClass* c = reinterpret_cast<VClass*>(ArrRef(params, i));
    if (c == nullptr) {
      state.ThrowNew("Ljava/lang/NullPointerException;", "parameter type is null");
      return false;
    }
    out->push_back(c->descriptor);
  }
  return true;
}

bool SameParams(const VMethod& m, const std::vector<std::string>& params) {
  return m.param_types == params;
}

// getDeclaredMethod 语义：本类 direct+virtual（含 private/static）按名+签名匹配
}  // namespace
VMethod* LookupDeclaredMethod(VClass* klass, const std::string& name,
                              const std::vector<std::string>& params, bool public_only) {
  for (VClass* k = klass; k != nullptr; k = (public_only ? k->super_class : nullptr)) {
    for (auto& m : k->direct_methods) {
      if (m.name == name && SameParams(m, params) && (!public_only || (m.access_flags & 0x1))) {
        return &m;
      }
    }
    for (auto& m : k->virtual_methods) {
      if (m.name == name && SameParams(m, params) && (!public_only || (m.access_flags & 0x1))) {
        return &m;
      }
    }
  }
  return nullptr;
}

VField* LookupField(VClass* klass, const std::string& name, bool public_only) {
  for (VClass* k = klass; k != nullptr; k = (public_only ? k->super_class : nullptr)) {
    for (auto& f : k->instance_fields) {
      if (f.name == name && (!public_only || (f.access_flags & 0x1))) return &f;
    }
    for (auto& f : k->static_fields) {
      if (f.name == name && (!public_only || (f.access_flags & 0x1))) return &f;
    }
  }
  return nullptr;
}

namespace {
Value ClsGetDeclaredMethodCommon(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok, bool public_only) {
  VClass* k = reinterpret_cast<VClass*>(self);
  std::string name = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  VArray* params = (n > 1 && args[1].ref != nullptr) ? reinterpret_cast<VArray*>(args[1].ref) : nullptr;
  std::vector<std::string> ptypes;
  if (!ParamDescriptors(state, params, &ptypes)) {
    *ok = false;
    return Value{};
  }
  VMethod* m = LookupDeclaredMethod(k, name, ptypes, public_only);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodException;", ClassName(k) + "." + name);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = MakeMethodMirror(state, m);
  if (r.ref == nullptr) {
    ThrowOOM(state, "getDeclaredMethod");
    *ok = false;
  }
  return r;
}
}  // namespace
Value ClsGetDeclaredMethod(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ClsGetDeclaredMethodCommon(state, self, args, n, ok, false);
}
Value ClsGetMethod(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ClsGetDeclaredMethodCommon(state, self, args, n, ok, true);
}

namespace {
Value ClsGetDeclaredFieldCommon(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok, bool public_only) {
  VClass* k = reinterpret_cast<VClass*>(self);
  std::string name = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  VField* f = LookupField(k, name, public_only);
  if (f == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchFieldException;", ClassName(k) + "." + name);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = MakeFieldMirror(state, f);
  if (r.ref == nullptr) {
    ThrowOOM(state, "getDeclaredField");
    *ok = false;
  }
  return r;
}
}  // namespace
Value ClsGetDeclaredField(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ClsGetDeclaredFieldCommon(state, self, args, n, ok, false);
}
Value ClsGetField(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ClsGetDeclaredFieldCommon(state, self, args, n, ok, true);
}

Value ClsGetDeclaredMethods(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<VMethod*> ms;
  // 构造器/类初始化器不是方法（MakeMethodMirror 对 <init> 会产出 Constructor
  // 镜像，混入 Method[] 后调用方 isBridge 等即 NoSuchMethodError——Jackson 实锤）。
  for (auto& m : k->direct_methods) {
    if (m.name != "<init>" && m.name != "<clinit>") ms.push_back(&m);
  }
  for (auto& m : k->virtual_methods) {
    if (m.name != "<init>" && m.name != "<clinit>") ms.push_back(&m);
  }
  VClass* cls_class = state.linker->FindClass("Ljava/lang/Class;");
  VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(cls_class),
                                         static_cast<int32_t>(ms.size()));
  // 元素类型应为 Method[]：按 mirror 类建数组
  if (arr == nullptr) {
    ThrowOOM(state, "getDeclaredMethods");
    *ok = false;
    return Value{};
  }
  arr->klass = state.linker->FindArrayClass(
      state.linker->FindClass("Ljava/lang/reflect/Method;"));
  for (size_t i = 0; i < ms.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), MakeMethodMirror(state, ms[i]));
  }
  Value r{};
  r.ref = arr;
  return r;
}
namespace {
// java.lang.Class.getMethods()：public 方法含继承面（类链 + 接口递归；
// 构造器排除；按 名字+参数+返回 去重——重写方法只保留最派生声明）。
void CollectPublicMethods(VClass* k, std::vector<VMethod*>* out,
                          std::set<std::string>* seen) {
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& m : c->virtual_methods) {
      if ((m.access_flags & kAccPublic) == 0 || m.name == "<init>") continue;
      std::string key = m.name + "(";
      for (const auto& p : m.param_types) key += p + ";";
      key += ")" + m.return_type;
      if (seen->insert(key).second) out->push_back(&m);
    }
    for (auto& m : c->direct_methods) {
      if ((m.access_flags & kAccPublic) == 0 || m.name == "<init>") continue;
      std::string key = m.name + "(";
      for (const auto& p : m.param_types) key += p + ";";
      key += ")" + m.return_type;
      if (seen->insert(key).second) out->push_back(&m);
    }
    for (VClass* iface : c->interfaces) CollectPublicMethods(iface, out, seen);
  }
}
}  // namespace
Value ClsGetDeclaringClass(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)args;
  (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  // 嵌套类 owner 近似（内部名末个 $ 切分，同 getEnclosingClass 口径）；
  // 顶层类/基本类型/数组 → null（JDK 同口径）
  if (k != nullptr && !k->is_array && !k->is_primitive && k->descriptor.size() > 2 &&
      k->descriptor[0] == 'L') {
    std::string name = k->descriptor.substr(1, k->descriptor.size() - 2);
    r.ref = DeriveOwner(state, name);
  }
  *ok = true;
  return r;
}

// Class.getEnclosingClass：成员/局部/匿名类均返回最外层类（内部名末个 $
// 切分）；顶层类/数组/基本类型 → null。Jackson POJO 内省（k0/h.B）必需面。
Value ClsGetEnclosingClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  if (k != nullptr && !k->is_array && !k->is_primitive && k->descriptor.size() > 2 &&
      k->descriptor[0] == 'L') {
    std::string name = k->descriptor.substr(1, k->descriptor.size() - 2);
    r.ref = DeriveOwner(state, name);
  }
  *ok = true;
  return r;
}

// Class.getEnclosingMethod()：VM 无源级方法归属元数据（局部/匿名类），
// 恒 null（AOSP 顶层/成员类本就返回 null；packer 反射探测按 null 降级）。
Value ClsGetEnclosingMethod(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}
Value ClsGetMethods(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<VMethod*> ms;
  std::set<std::string> seen;
  CollectPublicMethods(k, &ms, &seen);
  VClass* arr_class =
      state.linker->FindArrayClass(state.linker->FindClass("Ljava/lang/reflect/Method;"));
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(ms.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getMethods");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < ms.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), MakeMethodMirror(state, ms[i]));
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

Value ClsGetFields(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<VField*> fs;
  std::set<std::string> seen;
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto* list : {&c->instance_fields, &c->static_fields}) {
      for (auto& f : *list) {
        if ((f.access_flags & kAccPublic) == 0) continue;
        if (!seen.insert(f.name).second) continue;
        fs.push_back(&f);
      }
    }
  }
  VClass* arr_class =
      state.linker->FindArrayClass(state.linker->FindClass("Ljava/lang/reflect/Field;"));
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(fs.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getFields");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < fs.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), MakeFieldMirror(state, fs[i]));
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

Value ClsGetConstructors(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<VMethod*> cs;
  for (auto& m : k->direct_methods) {
    if (m.name == "<init>" && (m.access_flags & kAccPublic) != 0) cs.push_back(&m);
  }
  VClass* arr_class =
      state.linker->FindArrayClass(state.linker->FindClass("Ljava/lang/reflect/Constructor;"));
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(cs.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getConstructors");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < cs.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), MakeMethodMirror(state, cs[i]));
  }
  Value r{};
  r.ref = arr;
  *ok = true;
  return r;
}

Value ClsGetDeclaredFields(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<VField*> fs;
  for (auto& f : k->instance_fields) fs.push_back(&f);
  for (auto& f : k->static_fields) fs.push_back(&f);
  VClass* arr_class =
      state.linker->FindArrayClass(state.linker->FindClass("Ljava/lang/reflect/Field;"));
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(fs.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getDeclaredFields");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < fs.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), MakeFieldMirror(state, fs[i]));
  }
  Value r{};
  r.ref = arr;
  return r;
}
Value ClsGetDeclaredConstructors(VMState& state, VObject* self, const Value* args, size_t n,
                                 bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<VMethod*> cs;
  for (auto& m : k->direct_methods) {
    if (m.name == "<init>") cs.push_back(&m);
  }
#if defined(__ANDROID__)
  if (k != nullptr && k->descriptor == "Lcom/zhihu/android/api/model/UDIDGuestInfo;") {
    // TEMP DIAG: Jackson 反射枚举构造器面（no-Creators 归因；事后移除）。
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "getDeclaredConstructors ctors=%zu direct=%zu", cs.size(),
                        k->direct_methods.size());
    for (auto& m : k->direct_methods) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "  direct method %s idx=%u",
                          m.name.c_str(), m.method_idx);
    }
    int shown = 0;
    for (auto it = state.frame_stack.rbegin(); it != state.frame_stack.rend() && shown < 8;
         ++it, ++shown) {
      const Frame* f = *it;
      if (f == nullptr || f->method == nullptr) continue;
      const VMethod* fm = f->method;
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "  caller %s.%s",
                          fm->declaring != nullptr ? fm->declaring->descriptor.c_str() : "?",
                          fm->name.c_str());
    }
  }
#endif
  VClass* arr_class =
      state.linker->FindArrayClass(state.linker->FindClass("Ljava/lang/reflect/Constructor;"));
  VArray* arr = state.linker->AllocArray(arr_class, static_cast<int32_t>(cs.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getDeclaredConstructors");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < cs.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), MakeMethodMirror(state, cs[i]));
  }
  Value r{};
  r.ref = arr;
  return r;
}

Value ClsNewInstance(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  if (k->IsAbstract() || k->IsInterface() || k->is_array || k->is_primitive) {
    state.ThrowNew("Ljava/lang/InstantiationException;", k->descriptor);
    *ok = false;
    return Value{};
  }
  if (!CheckClassDefinition(state, k) || !state.linker->InitializeClass(k, &state)) {
    *ok = false;
    return Value{};
  }
  VObject* obj = state.linker->AllocObject(k);
  if (obj == nullptr) {
    ThrowOOM(state, "Class.newInstance");
    *ok = false;
    return Value{};
  }
  VMethod probe;
  probe.name = "<init>";
  probe.return_type = "V";
  VMethod* ctor = k->FindInterfaceImpl(&probe);
  if (ctor == nullptr) {
    state.ThrowNew("Ljava/lang/InstantiationException;",
                   ClassName(k) + " no no-arg constructor");
    *ok = false;
    return Value{};
  }
  Value self_v{};
  self_v.ref = obj;
  Value r{};
  if (!InvokeMethod(state, ctor, &self_v, 1, &r)) {
    // ART：构造异常原样上抛（Class.newInstance 语义）
    *ok = false;
    return Value{};
  }
  Value out{};
  out.ref = obj;
  return out;
}

Value ClsGetSuperclass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  if (k != nullptr && !k->IsInterface() && !k->is_primitive) {
    r.ref = reinterpret_cast<ObjHeader*>(k->super_class);
  }
  return r;
}
Value ClsGetInterfaces(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  VClass* cls_class = state.linker->FindClass("Ljava/lang/Class;");
  VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(cls_class),
                                         k != nullptr ? static_cast<int32_t>(k->interfaces.size()) : 0);
  if (arr == nullptr) {
    ThrowOOM(state, "getInterfaces");
    *ok = false;
    return Value{};
  }
  if (k != nullptr) {
    for (size_t i = 0; i < k->interfaces.size(); i++) {
      ArrSetRef(arr, static_cast<int32_t>(i), reinterpret_cast<ObjHeader*>(k->interfaces[i]));
    }
  }
  Value r{};
  r.ref = arr;
  return r;
}
Value ClsIsAssignableFrom(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  VClass* other = (n > 0) ? reinterpret_cast<VClass*>(args[0].ref) : nullptr;
  Value r{};
  r.i = (k != nullptr && other != nullptr && k->IsAssignableFrom(other)) ? 1 : 0;
  return r;
}
Value ClsIsInstance(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  ObjHeader* o = (n > 0) ? args[0].ref : nullptr;
  Value r{};
  r.i = (k != nullptr && o != nullptr && o->klass != nullptr && k->IsAssignableFrom(o->klass))
            ? 1
            : 0;
  return r;
}
Value ClsCast(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VClass* k = reinterpret_cast<VClass*>(self);
  ObjHeader* o = (n > 0) ? args[0].ref : nullptr;
  if (o != nullptr && k != nullptr && o->klass != nullptr && !k->IsAssignableFrom(o->klass)) {
    ThrowClassCastException(state, o->klass->descriptor, k->descriptor);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = o;
  return r;
}
Value ClsIsInterface(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  r.i = (k != nullptr && k->IsInterface()) ? 1 : 0;
  return r;
}
Value ClsIsEnum(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  bool is_enum = false;
  if (k != nullptr && k->super_class != nullptr &&
      k->super_class->descriptor == "Ljava/lang/Enum;") {
    is_enum = true;
  }
  r.i = is_enum ? 1 : 0;
  return r;
}
// Class.isAnnotation（ACC_ANNOTATION 0x2000）：Jackson 的注解内省面必需，
// 缺失会让 ObjectMapper 反序列化在注解判定处抛 NoSuchMethodError。
Value ClsIsAnnotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  r.i = (k != nullptr && (k->access_flags & 0x2000u) != 0) ? 1 : 0;
  return r;
}
Value ClsIsArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  r.i = (k != nullptr && k->is_array) ? 1 : 0;
  return r;
}
Value ClsIsPrimitive(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  r.i = (k != nullptr && k->is_primitive) ? 1 : 0;
  return r;
}
Value ClsGetComponentType(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  if (k != nullptr && k->is_array) {
    r.ref = reinterpret_cast<ObjHeader*>(k->component_type);
  }
  return r;
}
// Class.getPackage()/getPackageName()：包名 = 二进制名去掉末段（内部类 $ 保留
// 在外层类上）；getPackage 返回 lib java.lang.Package（函数面）。
// Class.getCanonicalName()：AOSP 口径 = 二进制名（'/'→'.'，内部类 $ 保留；
// 数组为 "[...;" 的 canonical 形；原始类型即名本身）。此前误接 getSimpleName
// 会让 Lifecycle 等按 lastIndexOf('.') 截取时越界（begin>length 事故根因）。
Value ClsGetCanonicalName(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)args;
  (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::string name;
  if (k != nullptr) {
    const std::string& d = k->descriptor;
    if (d.size() >= 2 && d.front() == 'L' && d.back() == ';') {
      name = d.substr(1, d.size() - 2);
    } else if (!d.empty() && d.front() == '[') {
      // 数组：canonical 形 = 元素 canonical + "[]"（基本类型用关键字名）
      int dims = 0;
      while (dims < static_cast<int>(d.size()) && d[dims] == '[') dims++;
      std::string elem = d.substr(dims);
      std::string base;
      if (elem.size() >= 2 && elem.front() == 'L' && elem.back() == ';') {
        base = elem.substr(1, elem.size() - 2);
        for (char& c : base) {
          if (c == '/') c = '.';
        }
      } else if (!elem.empty()) {
        switch (elem[0]) {
          case 'I': base = "int"; break;
          case 'J': base = "long"; break;
          case 'Z': base = "boolean"; break;
          case 'B': base = "byte"; break;
          case 'S': base = "short"; break;
          case 'C': base = "char"; break;
          case 'F': base = "float"; break;
          case 'D': base = "double"; break;
          case 'V': base = "void"; break;
          default: base = elem; break;
        }
      }
      name = base;
      for (int i = 0; i < dims; i++) name += "[]";
    } else {
      name = d;
    }
    for (char& c : name) {
      if (c == '/') c = '.';
    }
  }
  return MakeStr(state, std::u16string(name.begin(), name.end()), ok);
}

Value ClsGetPackageName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::string pkg;
  if (k != nullptr) {
    std::string d = k->descriptor;
    if (!d.empty() && d[0] == 'L' && d.back() == ';') d = d.substr(1, d.size() - 2);
    size_t slash = d.rfind('/');
    if (slash != std::string::npos) {
      pkg = d.substr(0, slash);
      for (char& ch : pkg) {
        if (ch == '/') ch = '.';
      }
    }
  }
  return MakeStr(state, std::u16string(pkg.begin(), pkg.end()), ok);
}

Value ClsGetPackage(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  bool name_ok = true;
  Value name = ClsGetPackageName(state, self, args, n, &name_ok);
  if (!name_ok || name.ref == nullptr) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "Class.getPackage: package name unavailable");
    }
    *ok = false;
    return r;
  }
  *ok = true;
  if (static_cast<VString*>(name.ref)->length == 0) return r;  // default package = null
  bool lib_ok = true;
  // lib Package 构造面：(String, Module)（JDK11 形态；module = unnamed module）
  Value cargs[2] = {};
  cargs[0] = name;
  VObject* pkg = NewLibObject(state, "Ljava/lang/Package;",
                              {"Ljava/lang/String;", "Ljava/lang/Module;"}, cargs, 2, &lib_ok);
  if (!lib_ok) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/NoClassDefFoundError;",
                     "Class.getPackage: java.lang.Package unavailable");
    }
    *ok = false;
    return r;
  }
  r.ref = reinterpret_cast<ObjHeader*>(pkg);
  return r;
}

Value ClsGetSimpleName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  std::string name;
  if (k != nullptr) {
    std::string d = k->descriptor;
    if (!d.empty() && d[0] == 'L' && d.back() == ';') d = d.substr(1, d.size() - 2);
    size_t slash = d.rfind('/');
    if (slash != std::string::npos) d = d.substr(slash + 1);
    size_t dollar = d.rfind('$');
    if (dollar != std::string::npos) d = d.substr(dollar + 1);
    name = d;
  }
  std::u16string out(name.begin(), name.end());
  return MakeStr(state, out, ok);
}
// Class.isAnonymousClass/isLocalClass/isMemberClass/isSynthetic（缺口 6：Gson
// 反射面）。前三者按 dex 二进制名约定（与 ClsGetSimpleName 同口径，OpenJDK
// 面依赖 InnerClasses 属性、本 VM Class 名面沿二进制名约定）：
//   尾段 $<纯数字>        -> 匿名类（Outer$1）
//   尾段 $<数字开头非纯数> -> 局部类（Outer$1Inner）
//   尾段 $<非数字开头>    -> 成员类（Outer$Inner）
//   无 $                  -> 三者皆否。
namespace {
enum class InnerNameKind { kTopLevel, kMember, kLocal, kAnonymous };
InnerNameKind ClassInnerNameKind(const VClass* k) {
  if (k == nullptr) return InnerNameKind::kTopLevel;
  std::string d = k->descriptor;
  if (d.size() < 3 || d[0] != 'L' || d.back() != ';') return InnerNameKind::kTopLevel;
  d = d.substr(1, d.size() - 2);
  size_t slash = d.rfind('/');
  if (slash != std::string::npos) d = d.substr(slash + 1);
  size_t dollar = d.rfind('$');
  if (dollar == std::string::npos || dollar + 1 >= d.size()) {
    return InnerNameKind::kTopLevel;
  }
  std::string tail = d.substr(dollar + 1);
  bool first_digit = tail[0] >= '0' && tail[0] <= '9';
  bool all_digits = first_digit;
  for (size_t i = 1; i < tail.size(); i++) {
    if (tail[i] < '0' || tail[i] > '9') all_digits = false;
  }
  if (all_digits) return InnerNameKind::kAnonymous;
  return first_digit ? InnerNameKind::kLocal : InnerNameKind::kMember;
}
}  // namespace
Value ClsIsAnonymousClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = (ClassInnerNameKind(reinterpret_cast<VClass*>(self)) == InnerNameKind::kAnonymous) ? 1 : 0;
  return r;
}
Value ClsIsLocalClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = (ClassInnerNameKind(reinterpret_cast<VClass*>(self)) == InnerNameKind::kLocal) ? 1 : 0;
  return r;
}
Value ClsIsMemberClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = (ClassInnerNameKind(reinterpret_cast<VClass*>(self)) == InnerNameKind::kMember) ? 1 : 0;
  return r;
}
Value ClsIsSynthetic(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  // ACC_SYNTHETIC（dex class_def access_flags 位；与 Class.isSynthetic Java 面
  // getModifiers()&0x1000 同口径）
  r.i = (k != nullptr && (k->access_flags & 0x1000u) != 0) ? 1 : 0;
  return r;
}
Value ClsGetModifiers(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  uint32_t flags = k != nullptr ? (k->access_flags & 0xffffu) : 0u;
  // ART 语义（jvmti ClassGetModifiers 同口径）：内嵌类的 Java 语言修饰符
  // （STATIC/PRIVATE/PROTECTED…）只存在于 class 级系统注解
  // Ldalvik/annotation/InnerClass; 的 accessFlags——dex class_def 里既没有
  // STATIC，也把 PRIVATE 内嵌类记成 PUBLIC。InnerClass 注解存在时整组替换，
  // 不能按位叠加（叠加会出现 PUBLIC|PRIVATE 非法组合）。缺失该注解时回落
  // class_def 低 16 位。Jackson ClassUtil.isNonStaticInnerClass 依赖 STATIC
  // 位判定（否则静态嵌套类被当作非静态成员类拒绝实例化）。
  if (k != nullptr && k->dex_file != nullptr) {
    std::vector<DexAnnoItem> annos;
    if (LoadMemberAnnotations(k->dex_file, k->class_def_idx, 'C', 0, &annos)) {
      for (const auto& a : annos) {
        if (a.type != "Ldalvik/annotation/InnerClass;") continue;
        for (const auto& e : a.elems) {
          if (e.first == "accessFlags" && e.second.k == DexAnnoVal::K::kInt) {
            flags = static_cast<uint32_t>(e.second.i) & 0xffffu;
          }
        }
        break;
      }
    }
  }
  Value r{};
  r.i = static_cast<int32_t>(flags);
  return r;
}
Value ClsGetEnumConstants(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VClass* k = reinterpret_cast<VClass*>(self);
  // AOSP getEnumConstants：非枚举返回 null
  if (k == nullptr || k->super_class == nullptr ||
      k->super_class->descriptor != "Ljava/lang/Enum;") {
    return Value{};
  }
  // ART 语义：getEnumConstantsShared 经 values() 触发枚举类初始化。未初始化
  // 直接读 static_storage 会得到空表（实测 Jackson MapperFeature
  // collectFeatureDefaults 全零 → USE_ANNOTATIONS 等默认全关 → 注解反序列化
  // Creator 消失，UDIDGuestInfo no-Creators）。
  {
    Value init_args[1] = {};
    init_args[0].ref = reinterpret_cast<ObjHeader*>(k);
    bool init_ok = true;
    UnsafeEnsureClassInitialized(state, nullptr, init_args, 1, &init_ok);
    if (!init_ok || state.HasPendingException()) {
      *ok = false;
      return Value{};
    }
  }
  std::vector<ObjHeader*> consts;
  for (auto& f : k->static_fields) {
    if (f.type_descriptor == k->descriptor && f.offset < k->static_storage.size()) {
      ObjHeader* o = k->static_storage[f.offset].ref;
      if (o != nullptr) consts.push_back(o);
    }
  }
  // AOSP：返回组件为枚举类本身的数组（[L<Enum>;）——运行时数组类决定下游
  // (Enum[])/具体枚举[] checkcast 的数组协变成败，不得退化为 [Ljava/lang/Object;。
  VArray* arr = state.linker->AllocArray(state.linker->FindArrayClass(k),
                                         static_cast<int32_t>(consts.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getEnumConstants");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < consts.size(); i++) ArrSetRef(arr, static_cast<int32_t>(i), consts[i]);
  Value r{};
  r.ref = arr;
  return r;
}
Value ClsDesiredAssertionStatus(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = 0;  // Android 默认断言关闭
  return r;
}
Value ClsForName(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string dot = (n > 0 && args[0].ref != nullptr) ? Mutf8Arg(args[0]) : "";
  std::string desc = session::DotToDescriptor(dot);
  VObject* loader = (n > 2) ? static_cast<VObject*>(args[2].ref) : nullptr;
  VClass* k = state.linker->FindClass(&state, loader, desc);
  if (k == nullptr) {
    if (!state.HasPendingException()) {
      // 诊断面：查无类名/链上错误随消息带出（被包装异常时可见根因）
      std::string msg = dot;
      if (!state.linker->last_error().empty()) {
        msg += " (linker: " + state.linker->last_error() + ")";
      }
      std::fprintf(stderr, "dexvm: Class.forName not found: %s\n", msg.c_str());
      state.ThrowNew("Ljava/lang/ClassNotFoundException;", msg);
    }
    *ok = false;
    return Value{};
  }
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(k);
  return r;
}
Value ClsGetClassLoader(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VClass* k = reinterpret_cast<VClass*>(self);
  Value r{};
  if (k != nullptr) r.ref = k->class_loader;
  return r;
}
// ---- Class.getPrimitiveClass（Boolean.TYPE 等 <clinit> 语义面）----
Value ClassGetPrimitiveClass(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getPrimitiveClass");
    *ok = false;
    return Value{};
  }
  std::string name = Mutf8Arg(args[0]);
  char type = 0;
  if (name == "void") type = 'V';
  else if (name == "boolean") type = 'Z';
  else if (name == "byte") type = 'B';
  else if (name == "char") type = 'C';
  else if (name == "short") type = 'S';
  else if (name == "int") type = 'I';
  else if (name == "long") type = 'J';
  else if (name == "float") type = 'F';
  else if (name == "double") type = 'D';
  if (type == 0) {
    state.ThrowNew("Ljava/lang/InternalError;", "not a primitive: " + name);
    *ok = false;
    return Value{};
  }
  VClass* k = state.linker->PrimitiveClass(type);
  if (k == nullptr) {
    ThrowOOM(state, "getPrimitiveClass");
    *ok = false;
    return Value{};
  }
  // 镜像面（semface 同约定）：primitive 镜像挂 class_class，invoke-virtual on
  // mirror 可派发（Class.* intrinsic self=镜像）。
  if (k->klass == nullptr) {
    VClass* class_class = state.linker->FindClass("Ljava/lang/Class;");
    if (class_class != nullptr) k->klass = class_class;
  }
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(k);  // 类镜像 = VClass 自身（ClassGetName 同约定）
  return r;
}

// ---- java.lang.annotation 反射面（成员面：dex annotations_directory 解码）----
// Method/Constructor/Field 的 getAnnotation/getDeclaredAnnotations 族：dex 注解
// 项（RUNTIME 可见面）→ 元素解码（本 TU 宽度语义解析器）→ 注解实例
// （sun.reflect.annotation.AnnotationInvocationHandler + java.lang.reflect.Proxy
// 分发接缝；§31.7 C 档 lib 本体 + VM 薄缝）。
// Class 级面维持"无注解"投影（调用方按无注解回落默认实现——历史契约不变）。
namespace {

ObjHeader* MakeAnnoProxyFromElems(VMState& state, VClass* anno_cls,
                                  const std::vector<std::pair<std::string, DexAnnoVal>>& elems,
                                  dex::DexFile* dex, bool* ok);

// 元素值 → guest 对象（member_desc = 注解方法返回类型；数组按其分量类型重建）
bool ReifyAnnoValue(VMState& state, dex::DexFile* dex, const DexAnnoVal& v,
                    const std::string& member_desc, ObjHeader** out, int depth) {
  if (depth > 32) return false;
  switch (v.k) {
    case DexAnnoVal::K::kInt: {
      char tc = !member_desc.empty() && std::string("ZBCSIJ").find(member_desc[0]) !=
                                            std::string::npos
                    ? member_desc[0]
                    : 'I';
      Value in{};
      in.l = v.i;
      return BoxValue(state, tc, in, out);
    }
    case DexAnnoVal::K::kFloat: {
      Value in{};
      uint32_t bits = static_cast<uint32_t>(static_cast<uint64_t>(v.i));
      std::memcpy(&in.f, &bits, sizeof(bits));
      return BoxValue(state, 'F', in, out);
    }
    case DexAnnoVal::K::kDouble: {
      Value in{};
      uint64_t bits = static_cast<uint64_t>(v.i);
      std::memcpy(&in.d, &bits, sizeof(bits));
      return BoxValue(state, 'D', in, out);
    }
    case DexAnnoVal::K::kString: {
      VString* s = state.linker->ResolveString(dex, v.idx);
      if (s == nullptr) return false;
      *out = reinterpret_cast<ObjHeader*>(s);
      return true;
    }
    case DexAnnoVal::K::kType: {
      VClass* k = state.linker->ResolveType(dex, v.idx, &state);
      if (k == nullptr) return false;
      *out = reinterpret_cast<ObjHeader*>(k);
      return true;
    }
    case DexAnnoVal::K::kField: {
      VField* f = state.linker->ResolveField(dex, v.idx);
      if (f == nullptr) return false;
      *out = MakeFieldMirror(state, f);
      return *out != nullptr;
    }
    case DexAnnoVal::K::kMethod: {
      VMethod* m = state.linker->ResolveMethod(dex, v.idx);
      if (m == nullptr) return false;
      *out = MakeMethodMirror(state, m);
      return *out != nullptr;
    }
    case DexAnnoVal::K::kEnum: {
      VField* f = state.linker->ResolveField(dex, v.idx);
      if (f == nullptr || !f->is_static || f->declaring == nullptr) return false;
      if (!state.linker->InitializeClass(f->declaring, &state)) return false;
      if (f->offset >= f->declaring->static_storage.size()) return false;
      *out = f->declaring->static_storage[f->offset].ref;
      return true;
    }
    case DexAnnoVal::K::kNull:
      *out = nullptr;
      return true;
    case DexAnnoVal::K::kArray: {
      std::string elem_desc = "Ljava/lang/Object;";
      if (member_desc.size() > 1 && member_desc[0] == '[') elem_desc = member_desc.substr(1);
      VClass* elem_cls = state.linker->FindClass(elem_desc);
      VClass* arr_cls = elem_cls != nullptr ? state.linker->FindArrayClass(elem_cls) : nullptr;
      if (arr_cls == nullptr) return false;
      VArray* arr = state.linker->AllocArray(arr_cls, static_cast<int32_t>(v.arr.size()));
      if (arr == nullptr) return false;
      for (size_t i = 0; i < v.arr.size(); i++) {
        ObjHeader* e = nullptr;
        if (!ReifyAnnoValue(state, dex, v.arr[i], elem_desc, &e, depth + 1)) return false;
        ArrSetRef(arr, static_cast<int32_t>(i), e);
      }
      *out = reinterpret_cast<ObjHeader*>(arr);
      return true;
    }
    case DexAnnoVal::K::kAnno: {
      VClass* nested = state.linker->FindClass(v.anno_type);
      if (nested == nullptr) return false;
      bool ok2 = true;
      ObjHeader* proxy = MakeAnnoProxyFromElems(state, nested, v.anno_elems, dex, &ok2);
      if (!ok2 || proxy == nullptr) return false;
      *out = proxy;
      return true;
    }
  }
  return false;
}

// 注解实例（OpenJDK AnnotationParser 形态）：memberValues（LinkedHashMap）→
// sun.reflect.annotation.AnnotationInvocationHandler → Proxy 分发接缝。
ObjHeader* MakeAnnoProxyFromElems(VMState& state, VClass* anno_cls,
                                  const std::vector<std::pair<std::string, DexAnnoVal>>& elems,
                                  dex::DexFile* dex, bool* ok) {
  *ok = false;
  if (anno_cls == nullptr || !state.linker->InitializeClass(anno_cls, &state)) return nullptr;
  VClass* map_cls = state.linker->FindClass("Ljava/util/LinkedHashMap;");
  if (map_cls == nullptr || !state.linker->InitializeClass(map_cls, &state)) return nullptr;
  VObject* map = state.linker->AllocObject(map_cls);
  if (map == nullptr) return nullptr;
  Value ctor_r{};
  if (!CallVirtual(state, map, "<init>", {}, "V", nullptr, 0, &ctor_r)) return nullptr;

  auto member_desc_of = [&](const std::string& name) -> std::string {
    for (auto& am : anno_cls->virtual_methods) {
      if (am.name == name && am.param_types.empty()) return am.return_type;
    }
    return std::string();
  };
  auto put_member = [&](const std::string& name, ObjHeader* val) -> bool {
    Value put_args[2] = {};
    VString* key = state.linker->AllocString(std::u16string(name.begin(), name.end()));
    if (key == nullptr) return false;
    put_args[0].ref = reinterpret_cast<ObjHeader*>(key);
    put_args[1].ref = val;
    Value ignored{};
    return CallVirtual(state, map, "put", {"Ljava/lang/Object;", "Ljava/lang/Object;"},
                       "Ljava/lang/Object;", put_args, 2, &ignored);
  };

  // 注解元素默认值：DEX 以 system annotation Ldalvik/annotation/AnnotationDefault;
  // 挂在注解类上（内嵌 annotated_annotation = 各元素默认值）。缺失显式值的元素
  // 必须按默认补齐——Jackson 读 @JsonProperty.access()/required() 等默认元素，
  // 旧代理直接 IncompleteAnnotationException（g0.f.a.a.u missing element access）
  // → UDIDGuestInfo 反序列化中断、guest 账户创建失败。
  std::set<std::string> present;
  for (const auto& e : elems) present.insert(e.first);
  if (anno_cls->dex_file != nullptr) {
    std::vector<DexAnnoItem> cls_annos;
    if (LoadMemberAnnotations(anno_cls->dex_file, anno_cls->class_def_idx, 'C', 0, &cls_annos)) {
      for (const auto& a : cls_annos) {
        if (a.type != "Ldalvik/annotation/AnnotationDefault;") continue;
        for (const auto& e : a.elems) {
          if (e.second.k != DexAnnoVal::K::kAnno) continue;
          for (const auto& de : e.second.anno_elems) {
            if (present.count(de.first) != 0) continue;
            std::string md = member_desc_of(de.first);
            ObjHeader* val = nullptr;
            if (!ReifyAnnoValue(state, anno_cls->dex_file, de.second, md, &val, 0)) {
              if (state.HasPendingException()) return nullptr;
              continue;
            }
            if (!put_member(de.first, val)) return nullptr;
          }
        }
        break;
      }
    }
  }

  for (const auto& e : elems) {
    // 元素类型 = 注解方法（无参同名）返回类型
    std::string member_desc = member_desc_of(e.first);
    ObjHeader* val = nullptr;
    if (!ReifyAnnoValue(state, dex, e.second, member_desc, &val, 0)) return nullptr;
    if (!put_member(e.first, val)) return nullptr;
  }
  VClass* handler_cls =
      state.linker->FindClass("Lsun/reflect/annotation/AnnotationInvocationHandler;");
  if (handler_cls == nullptr || !state.linker->InitializeClass(handler_cls, &state)) return nullptr;
  VObject* handler = state.linker->AllocObject(handler_cls);
  if (handler == nullptr) return nullptr;
  Value hargs[2] = {};
  hargs[0].ref = reinterpret_cast<ObjHeader*>(anno_cls);
  hargs[1].ref = reinterpret_cast<ObjHeader*>(map);
  Value ignored{};
  if (!CallVirtual(state, handler, "<init>",
                   {"Ljava/lang/Class;", "Ljava/util/Map;"}, "V", hargs, 2, &ignored)) {
    return nullptr;
  }
  // Proxy.newProxyInstance(loader, [anno], handler)
  VClass* proxy_cls = state.linker->FindClass("Ljava/lang/reflect/Proxy;");
  if (proxy_cls == nullptr || !state.linker->InitializeClass(proxy_cls, &state)) return nullptr;
  VClass* cls_cls = state.linker->FindClass("Ljava/lang/Class;");
  VClass* cls_arr = cls_cls != nullptr ? state.linker->FindArrayClass(cls_cls) : nullptr;
  if (cls_arr == nullptr) return nullptr;
  VArray* ifaces = state.linker->AllocArray(cls_arr, 1);
  if (ifaces == nullptr) return nullptr;
  ArrSetRef(ifaces, 0, reinterpret_cast<ObjHeader*>(anno_cls));
  VMethod* new_proxy = FindStaticM(
      proxy_cls, "newProxyInstance",
      {"Ljava/lang/ClassLoader;", "[Ljava/lang/Class;", "Ljava/lang/reflect/InvocationHandler;"},
      "Ljava/lang/Object;");
  if (new_proxy == nullptr) return nullptr;
  Value pargs[3] = {};
  pargs[0].ref = anno_cls->class_loader;
  pargs[1].ref = reinterpret_cast<ObjHeader*>(ifaces);
  pargs[2].ref = reinterpret_cast<ObjHeader*>(handler);
  Value proxy{};
  if (!InvokeMethod(state, new_proxy, pargs, 3, &proxy)) return nullptr;
  *ok = proxy.ref != nullptr;
  return proxy.ref;
}

// 成员镜像定位（Method/Constructor = artMethod 镜像；Field = artField 镜像；
// 按镜像类描述符分流——两类镜像槽位布局同构，不可互读）
bool MemberMirrorLocate(VObject* mirror, char* kind, uint32_t* idx, dex::DexFile** dex,
                        uint32_t* class_def_idx) {
  if (mirror == nullptr || mirror->klass == nullptr) return false;
  const std::string& d = mirror->klass->descriptor;
  if (d == "Ljava/lang/reflect/Method;" || d == "Ljava/lang/reflect/Constructor;") {
    VMethod* m = MirrorMethod(mirror);
    if (m == nullptr || m->declaring == nullptr || m->dex_file == nullptr) return false;
    *kind = 'M';
    *idx = m->method_idx;
    *dex = m->dex_file;
    *class_def_idx = m->declaring->class_def_idx;
    return true;
  }
  if (d == "Ljava/lang/reflect/Field;") {
    VField* f = MirrorField(mirror);
    if (f == nullptr || f->declaring == nullptr || f->dex_file == nullptr) return false;
    *kind = 'F';
    *idx = f->field_idx;
    *dex = f->dex_file;
    *class_def_idx = f->declaring->class_def_idx;
    return true;
  }
  return false;
}

// 成员注解装载（RUNTIME 注解项全量；builtin 成员无 dex 归属 = 空）
bool MemberAnnoLoad(VObject* mirror, std::vector<DexAnnoItem>* out) {
  out->clear();
  char kind = 0;
  uint32_t idx = 0;
  dex::DexFile* dex = nullptr;
  uint32_t class_def_idx = 0;
  if (!MemberMirrorLocate(mirror, &kind, &idx, &dex, &class_def_idx)) return true;
  return LoadMemberAnnotations(dex, class_def_idx, kind, idx, out);
}

}  // namespace

// Method/Constructor/Field.getAnnotation(Class) / getDeclaredAnnotation(Class)
// （成员面：RUNTIME 可见直接注解；缺省 null）
Value MemberGetAnnotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "annotationClass == null");
    *ok = false;
    return r;
  }
  VClass* wanted = reinterpret_cast<VClass*>(args[0].ref);
  std::vector<DexAnnoItem> annos;
  if (!MemberAnnoLoad(self, &annos)) {
    state.ThrowNew("Ljava/lang/InternalError;", "annotation decode failed");
    *ok = false;
    return r;
  }
  char kind = 0;
  uint32_t idx = 0;
  dex::DexFile* dex = nullptr;
  uint32_t class_def_idx = 0;
  (void)MemberMirrorLocate(self, &kind, &idx, &dex, &class_def_idx);
#if defined(__ANDROID__)
  if (wanted->descriptor == "Lkotlin/Metadata;") {
    // TEMP DIAG: Kotlin 元数据注解可见性（Jackson Kotlin 模块构造器合成依赖；
    // UDIDGuestInfo no-Creators 根因定位；事后移除）。
    int meta_count = 0;
    int elems = -1;
    std::string owner = "?";
    if (dex != nullptr && class_def_idx < dex->NumClassDefs()) {
      owner = std::string(dex->GetTypeDescriptor(dex->GetClassDef(class_def_idx).class_idx));
    }
    for (const auto& a : annos) {
      if (a.type == "Lkotlin/Metadata;") {
        meta_count++;
        elems = static_cast<int>(a.elems.size());
      }
    }
    __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                        "Metadata owner=%s kind=%c idx=%u annos=%zu meta=%d elems=%d", owner.c_str(),
                        kind, idx, annos.size(), meta_count, elems);
  }
#endif
#if defined(__ANDROID__)
  if (VMethod* mm = MirrorMethod(self)) {
    if (mm->declaring != nullptr &&
        mm->declaring->descriptor == "Lcom/zhihu/android/api/model/UDIDGuestInfo;") {
      int vis = 0;
      int wanted_hits = 0;
      for (const auto& a : annos) {
        if (a.visibility == 1) vis++;
        if (a.visibility == 1 && a.type == wanted->descriptor) wanted_hits++;
      }
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                          "memberGetAnnotation %s.%s wanted=%s annos=%zu vis=%d hits=%d",
                          mm->declaring->descriptor.c_str(), mm->name.c_str(),
                          wanted->descriptor.c_str(), annos.size(), vis, wanted_hits);
    }
  }
#endif
  for (const auto& a : annos) {
    if (a.visibility != 1 || a.type != wanted->descriptor) continue;
    bool ok2 = true;
    ObjHeader* proxy = MakeAnnoProxyFromElems(state, wanted, a.elems, dex, &ok2);
    if (!ok2) {
      if (!state.HasPendingException()) {
        state.ThrowNew("Ljava/lang/InternalError;", "annotation reify failed");
      }
      *ok = false;
      return r;
    }
    r.ref = proxy;
    *ok = true;
    return r;
  }
  *ok = true;
  return r;
}

// Method/Constructor/Field.getDeclaredAnnotations()/getAnnotations()（成员面：
// RUNTIME 可见注解数组）
Value MemberGetDeclaredAnnotations(VMState& state, VObject* self, const Value* args, size_t n,
                                   bool* ok) {
  (void)args;
  (void)n;
  Value r{};
#if defined(__ANDROID__)
  {
    static int g_decl_calls = 0;
    VMethod* mm = MirrorMethod(self);
    if (g_decl_calls < 400) {
      g_decl_calls++;
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                          "declAnnosCall #%d member=%s.%s", g_decl_calls,
                          mm != nullptr && mm->declaring != nullptr
                              ? mm->declaring->descriptor.c_str()
                              : "?",
                          mm != nullptr ? mm->name.c_str() : "?");
    }
  }
#endif
  VClass* anno = state.linker->FindClass("Ljava/lang/annotation/Annotation;");
  VClass* arr_cls = anno != nullptr ? state.linker->FindArrayClass(anno) : nullptr;
  if (arr_cls == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "java.lang.annotation.Annotation missing");
    *ok = false;
    return r;
  }
  char kind = 0;
  uint32_t idx = 0;
  dex::DexFile* dex = nullptr;
  uint32_t class_def_idx = 0;
  (void)MemberMirrorLocate(self, &kind, &idx, &dex, &class_def_idx);
  std::vector<DexAnnoItem> annos;
  if (!MemberAnnoLoad(self, &annos)) {
    state.ThrowNew("Ljava/lang/InternalError;", "annotation decode failed");
    *ok = false;
    return r;
  }
  std::vector<ObjHeader*> proxies;
  for (const auto& a : annos) {
    if (a.visibility != 1) continue;
    VClass* anno_cls = state.linker->FindClass(a.type);
    if (anno_cls == nullptr) {
      state.ClearException();
      continue;
    }
    bool ok2 = true;
    ObjHeader* proxy = MakeAnnoProxyFromElems(state, anno_cls, a.elems, dex, &ok2);
    if (!ok2) {
      if (!state.HasPendingException()) {
        state.ThrowNew("Ljava/lang/InternalError;", "annotation reify failed");
      }
      *ok = false;
      return r;
    }
    if (proxy != nullptr) proxies.push_back(proxy);
  }
#if defined(__ANDROID__)
  if (VMethod* mm = MirrorMethod(self)) {
    if (mm->declaring != nullptr &&
        (mm->declaring->descriptor == "Lcom/zhihu/android/api/model/UDIDGuestInfo;" ||
         mm->declaring->descriptor == "Lcom/zhihu/android/api/model/GuestResponse;")) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                          "getDeclaredAnnotations %s annos=%zu proxies=%zu", mm->name.c_str(),
                          annos.size(), proxies.size());
    }
  }
#endif
  VArray* arr = state.linker->AllocArray(arr_cls, static_cast<int32_t>(proxies.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getDeclaredAnnotations");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < proxies.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), proxies[i]);
  }
  r.ref = arr;
  *ok = true;
  return r;
}

// Method/Constructor.getParameterAnnotations()：[[Annotation;（逐形参 RUNTIME
// 注解组；无注解参数 = 空数组）。Retrofit ServiceMethod.Builder 的必需面。
Value MethodGetParameterAnnotations(VMState& state, VObject* self, const Value* args, size_t n,
                                    bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  VClass* anno = state.linker->FindClass("Ljava/lang/annotation/Annotation;");
  VClass* anno_arr = anno != nullptr ? state.linker->FindArrayClass(anno) : nullptr;
  VClass* arr2 = anno_arr != nullptr ? state.linker->FindArrayClass(anno_arr) : nullptr;
  if (arr2 == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "java.lang.annotation.Annotation missing");
    *ok = false;
    return r;
  }
  VMethod* m = MirrorMethod(self);
  int32_t count = m != nullptr ? static_cast<int32_t>(m->param_types.size()) : 0;
#if defined(__ANDROID__)
  {
    static int g_param_calls = 0;
    if (g_param_calls < 200) {
      g_param_calls++;
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "paramAnnosCall #%d member=%s.%s",
                          g_param_calls,
                          m != nullptr && m->declaring != nullptr
                              ? m->declaring->descriptor.c_str()
                              : "?",
                          m != nullptr ? m->name.c_str() : "?");
    }
  }
#endif
  std::vector<std::vector<DexAnnoItem>> params;
  char kind = 0;
  uint32_t idx = 0;
  dex::DexFile* dex = nullptr;
  uint32_t class_def_idx = 0;
  if (MemberMirrorLocate(self, &kind, &idx, &dex, &class_def_idx)) {
    if (!LoadParameterAnnotations(dex, class_def_idx, idx, &params)) {
      state.ThrowNew("Ljava/lang/InternalError;", "parameter annotation decode failed");
      *ok = false;
      return r;
    }
#if defined(__ANDROID__)
    // TEMP DIAG: UDIDGuestInfo 构造器参数注解可见性（Jackson creator 检测；
    // 事后移除）。
    if (m != nullptr && m->declaring != nullptr &&
        m->declaring->descriptor == "Lcom/zhihu/android/api/model/UDIDGuestInfo;") {
      __android_log_print(ANDROID_LOG_INFO, "DexVmAnno",
                          "getParameterAnnotations %s idx=%u kind=%c params=%zu", m->name.c_str(),
                          idx, kind, params.size());
      for (size_t i = 0; i < params.size(); i++) {
        int vis = 0;
        for (const auto& a : params[i]) {
          if (a.visibility == 1) vis++;
        }
        __android_log_print(ANDROID_LOG_INFO, "DexVmAnno", "  param[%zu] annos=%zu vis=%d", i,
                            params[i].size(), vis);
      }
    }
#endif
    // annotation_set_ref_list 长度 = dex proto 形参数；与镜像参数数不一致时
    // 取大值（镜像缺省不影响外层数组形状）。
    if (static_cast<int32_t>(params.size()) > count) {
      count = static_cast<int32_t>(params.size());
    }
  }
  VArray* outer = state.linker->AllocArray(arr2, count);
  if (outer == nullptr) {
    ThrowOOM(state, "getParameterAnnotations");
    *ok = false;
    return r;
  }
  for (int32_t i = 0; i < count; i++) {
    std::vector<ObjHeader*> proxies;
    if (static_cast<size_t>(i) < params.size()) {
      for (const auto& a : params[i]) {
        if (a.visibility != 1) continue;
        VClass* anno_cls = state.linker->FindClass(a.type);
        if (anno_cls == nullptr) {
          state.ClearException();
          continue;
        }
        bool ok2 = true;
        ObjHeader* proxy = MakeAnnoProxyFromElems(state, anno_cls, a.elems, dex, &ok2);
        if (!ok2) {
          if (!state.HasPendingException()) {
            state.ThrowNew("Ljava/lang/InternalError;", "parameter annotation reify failed");
          }
          *ok = false;
          return r;
        }
        if (proxy != nullptr) proxies.push_back(proxy);
      }
    }
    VArray* inner = state.linker->AllocArray(anno_arr, static_cast<int32_t>(proxies.size()));
    if (inner == nullptr) {
      ThrowOOM(state, "getParameterAnnotations");
      *ok = false;
      return r;
    }
    for (size_t j = 0; j < proxies.size(); j++) {
      ArrSetRef(inner, static_cast<int32_t>(j), proxies[j]);
    }
    ArrSetRef(outer, i, reinterpret_cast<ObjHeader*>(inner));
  }
  r.ref = outer;
  return r;
}

// Method/Constructor/Field.isAnnotationPresent(Class)
Value MemberIsAnnotationPresent(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  Value got = MemberGetAnnotation(state, self, args, n, ok);
  Value r{};
  if (!*ok) return r;
  r.i = got.ref != nullptr ? 1 : 0;
  return r;
}

// ---- java.lang.annotation 反射面（Class 级：dex 类注解解码）----
// 历史契约曾把 Class 注解面钉成"无注解投影"，但实测知乎 Jackson 反序列化
// Kotlin data class（UDIDGuestInfo 等）依赖类注解（@kotlin.Metadata/Jackson
// 注解）找 Creator；成员面（Method/Constructor/Field）已按 dex 解码承接，
// Class 面同口径补齐（RUNTIME 可见直接注解）。
namespace {
bool LoadClassAnnotations(VObject* self, std::vector<DexAnnoItem>* annos,
                          dex::DexFile** dex_out) {
  annos->clear();
  *dex_out = nullptr;
  VClass* k = reinterpret_cast<VClass*>(self);
  if (k == nullptr || k->dex_file == nullptr) return true;
  *dex_out = k->dex_file;
  return LoadMemberAnnotations(k->dex_file, k->class_def_idx, 'C', 0, annos);
}
}  // namespace

Value ClassGetAnnotation(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "annotationClass == null");
    *ok = false;
    return r;
  }
  VClass* wanted = reinterpret_cast<VClass*>(args[0].ref);
  std::vector<DexAnnoItem> annos;
  dex::DexFile* dex = nullptr;
  if (!LoadClassAnnotations(self, &annos, &dex)) {
    state.ThrowNew("Ljava/lang/InternalError;", "class annotation decode failed");
    *ok = false;
    return r;
  }
  for (const auto& a : annos) {
    if (a.visibility != 1 || a.type != wanted->descriptor) continue;
    bool ok2 = true;
    ObjHeader* proxy = MakeAnnoProxyFromElems(state, wanted, a.elems, dex, &ok2);
    if (!ok2) {
      if (!state.HasPendingException()) {
        state.ThrowNew("Ljava/lang/InternalError;", "class annotation reify failed");
      }
      *ok = false;
      return r;
    }
    r.ref = proxy;
    *ok = true;
    return r;
  }
  *ok = true;
  return r;
}

Value ClassGetAnnotations(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  Value r{};
  std::vector<DexAnnoItem> annos;
  dex::DexFile* dex = nullptr;
  if (!LoadClassAnnotations(self, &annos, &dex)) {
    state.ThrowNew("Ljava/lang/InternalError;", "class annotation decode failed");
    *ok = false;
    return r;
  }
  std::vector<ObjHeader*> proxies;
  for (const auto& a : annos) {
    if (a.visibility != 1) continue;
    VClass* anno_cls = state.linker->FindClass(a.type);
    if (anno_cls == nullptr) {
      state.ClearException();
      continue;
    }
    bool ok2 = true;
    ObjHeader* proxy = MakeAnnoProxyFromElems(state, anno_cls, a.elems, dex, &ok2);
    if (!ok2) {
      if (!state.HasPendingException()) {
        state.ThrowNew("Ljava/lang/InternalError;", "class annotation reify failed");
      }
      *ok = false;
      return r;
    }
    if (proxy != nullptr) proxies.push_back(proxy);
  }
  VClass* anno = state.linker->FindClass("Ljava/lang/annotation/Annotation;");
  VClass* arr_cls = anno != nullptr ? state.linker->FindArrayClass(anno) : nullptr;
  if (arr_cls == nullptr) {
    ThrowOOM(state, "getAnnotations");
    *ok = false;
    return r;
  }
  VArray* arr = state.linker->AllocArray(arr_cls, static_cast<int32_t>(proxies.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getAnnotations");
    *ok = false;
    return r;
  }
  for (size_t i = 0; i < proxies.size(); i++) {
    ArrSetRef(arr, static_cast<int32_t>(i), proxies[i]);
  }
  r.ref = arr;
  *ok = true;
  return r;
}

Value ClassIsAnnotationPresent(VMState& state, VObject* self, const Value* args, size_t n,
                               bool* ok) {
  Value got = ClassGetAnnotation(state, self, args, n, ok);
  Value r{};
  if (!*ok) return r;
  r.i = got.ref != nullptr ? 1 : 0;
  return r;
}
// lib java.lang.Class.getModule()：本 VM 无 JPMS 命名模块——返回 unnamed module
// （java.lang.Module 的 ALL_UNNAMED_MODULE 单例；isNamed()==false 语义）。
Value ClsGetModule(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  VClass* mod = state.linker->FindClass("Ljava/lang/Module;");
  if (mod == nullptr || !state.linker->InitializeClass(mod, &state)) {
    state.ThrowNew("Ljava/lang/InternalError;", "java.lang.Module unavailable");
    *ok = false;
    return Value{};
  }
  for (auto& f : mod->static_fields) {
    if (f.name == "ALL_UNNAMED_MODULE") {
      Value r{};
      r.ref = reinterpret_cast<ObjHeader*>(mod->static_storage[f.offset].ref);
      if (r.ref != nullptr) {
        *ok = true;
        return r;
      }
    }
  }
  // 兜底：裸 Module（零字段；isNamed()=name!=null → false）
  Value r{};
  r.ref = reinterpret_cast<ObjHeader*>(state.linker->AllocObject(mod));
  if (r.ref == nullptr) {
    ThrowOOM(state, "Module alloc failed");
    *ok = false;
  }
  return r;
}
// java.lang.Class.getConstructor/getDeclaredConstructor（反射面补齐：
// Constructor 镜像由 GetDeclaredConstructors 同源产出；public 过滤按 Java 语义）
namespace {
Value ClsGetCtorCore(VMState& state, VObject* self, const Value* args, size_t n, bool* ok,
                     bool public_only) {
  VClass* k = reinterpret_cast<VClass*>(self);
  std::vector<std::string> want;
  if (n >= 1 && !IsNullArg(args[0])) {
    VArray* ps = reinterpret_cast<VArray*>(args[0].ref);
    for (int32_t i = 0; i < ps->length; i++) {
      ObjHeader* c = nullptr;
      std::memcpy(&c, ps->data() + i * 8, sizeof(c));
      want.push_back(c != nullptr ? reinterpret_cast<VClass*>(c)->descriptor : "Ljava/lang/Object;");
    }
  }
  for (auto& m : k->direct_methods) {
    if (m.name != "<init>" || m.param_types != want) continue;
    if (public_only && (m.access_flags & 0x1) == 0) continue;
    Value r{};
    r.ref = MakeMethodMirror(state, &m);
    if (r.ref == nullptr) {
      ThrowOOM(state, "getConstructor mirror");
      *ok = false;
      return Value{};
    }
    *ok = true;
    return r;
  }
  state.ThrowNew("Ljava/lang/NoSuchMethodException;", ClassName(k) + ".<init>");
  *ok = false;
  return Value{};
}
}  // namespace
Value ClsGetConstructor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ClsGetCtorCore(state, self, args, n, ok, true);
}
Value ClsGetDeclaredConstructor(VMState& state, VObject* self, const Value* args, size_t n,
                                bool* ok) {
  return ClsGetCtorCore(state, self, args, n, ok, false);
}

// <clinit> 覆写空面（VarHandle 形态原子类：<clinit> findVarHandle 不可达，
// ---- java.lang.reflect.Proxy 分发接缝（§31.7：C 档 lib 本体 + VM 薄缝）----
// Proxy Java 本体在 lib（runtime-libs java/lang/reflect/Proxy.java 补源：API
// 形状/参数校验/实例化全 Java）；VM 缝只做两件事（BindWellKnownNatives 绑到
// lib native 槽）：
//   defineProxyClass0 —— 合成代理类（super=lib Proxy、interfaces=接口族、每
//     方法一条分发 intrinsic）——OpenJDK 生成字节码在 dex VM 的等价物；
//   isProxyClass0 —— 合成登记判定。
// 方法身份 = 分发槽 trampoline（编译期模板函数表；IntrinsicFn 槽位不携带
// VMethod*，身份经【槽号 + 接收者类】在分发表定位）。容量 = 每代理类每方法
// 一槽（kProxyDispatchSlots；超限 UnsupportedOperationException 如实报）。
// 分发语义（OpenJDK 生成体的等价物）：接口闭包全部实例方法（含 default/超
// 接口）+ Object 公共面（equals/hashCode/toString）路由
// InvocationHandler.invoke(this, Method, Object[]）；参数装箱、返回值按声明
// 返回型拆箱（handler 返回 null 拆箱 = NPE）；handler 抛出按原异常穿出
// （UndeclaredThrowableException 包装后置）。非移动 GC 下裸指针安全。
namespace {
constexpr size_t kProxyDispatchSlots = 96;

}  // namespace
struct ProxyDispatchTarget {
  VClass* proxy_klass = nullptr;
  VMethod* iface_method = nullptr;
};
namespace {
std::vector<ProxyDispatchTarget> g_proxy_dispatch[kProxyDispatchSlots];
std::vector<std::pair<std::string, VClass*>> g_proxy_defined;  // key -> 合成类（幂等缓存）

VMethod* ProxyTargetFor(VClass* klass, size_t slot) {
  if (klass == nullptr || slot >= kProxyDispatchSlots) return nullptr;
  for (const auto& t : g_proxy_dispatch[slot]) {
    if (t.proxy_klass == klass) return t.iface_method;
  }
  return nullptr;
}

// lib Proxy 布局契约：protected InvocationHandler h（沿继承链找）
VField* ProxyHandlerField(VClass* klass) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == "h") return &f;
    }
  }
  return nullptr;
}

Value ProxyDispatchImpl(size_t slot, VMState& state, VObject* self, const Value* args, size_t n,
                        bool* ok) {
  VMethod* target =
      self != nullptr && self->klass != nullptr ? ProxyTargetFor(self->klass, slot) : nullptr;
  if (target == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "proxy dispatch target lost");
    *ok = false;
    return Value{};
  }
  VField* h_f = ProxyHandlerField(self->klass);
  ObjHeader* handler = nullptr;
  if (h_f != nullptr) {
    std::memcpy(&handler, self->field_data() + h_f->offset, sizeof(handler));
  }
  if (handler == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "proxy handler == null");
    *ok = false;
    return Value{};
  }
  // Method 镜像（接口方法声明；Object 方法同口径）
  VObject* mirror = MakeMethodMirror(state, target);
  // 参数装箱 -> Object[]
  VClass* obj_cls = state.linker->FindClass("Ljava/lang/Object;");
  VClass* oarr_cls = obj_cls != nullptr ? state.linker->FindArrayClass(obj_cls) : nullptr;
  VArray* argv =
      oarr_cls != nullptr ? state.linker->AllocArray(oarr_cls, static_cast<int32_t>(n)) : nullptr;
  if (mirror == nullptr || argv == nullptr) {
    ThrowOOM(state, "proxy dispatch");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < n; i++) {
    char tc = i < target->param_types.size() && !target->param_types[i].empty()
                  ? target->param_types[i][0]
                  : 'L';
    if (tc == 'L' || tc == '[') {
      ArrSetRef(argv, static_cast<int32_t>(i), args[i].ref);
    } else {
      ObjHeader* boxed = nullptr;
      if (!BoxValue(state, tc, args[i], &boxed)) {
        *ok = false;
        return Value{};
      }
      ArrSetRef(argv, static_cast<int32_t>(i), boxed);
    }
  }
  // h.invoke(this, method, args)
  VMethod probe;
  probe.name = "invoke";
  probe.param_types = {"Ljava/lang/Object;", "Ljava/lang/reflect/Method;", "[Ljava/lang/Object;"};
  probe.return_type = "Ljava/lang/Object;";
  VMethod* invoke_m =
      handler->klass != nullptr ? handler->klass->FindInterfaceImpl(&probe) : nullptr;
  if (invoke_m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;",
                   "InvocationHandler.invoke(Object,Method,Object[])");
    *ok = false;
    return Value{};
  }
  Value call[4] = {};
  call[0].ref = handler;
  call[1].ref = self;
  call[2].ref = mirror;
  call[3].ref = argv;
  Value r{};
  if (!InvokeMethod(state, invoke_m, call, 4, &r)) {
    *ok = false;
    return Value{};
  }
  const std::string& rt = target->return_type;
  if (rt == "V") return Value{};
  if (!rt.empty() && (rt[0] == 'L' || rt[0] == '[')) return r;
  if (r.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;",
                   "proxy handler returned null for " + MethodSigStr(target));
    *ok = false;
    return Value{};
  }
  Value out{};
  if (!UnboxValue(state, r.ref, rt[0], &out)) {
    *ok = false;
    return Value{};
  }
  return out;
}

// 分发槽 trampoline（编译期展开；槽号经模板参数固化）
template <size_t N>
Value ProxyDispatchSlot(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return ProxyDispatchImpl(N, state, self, args, n, ok);
}

template <size_t... I>
constexpr std::array<IntrinsicFn, sizeof...(I)> ProxyTrampolineTable(std::index_sequence<I...>) {
  return {&ProxyDispatchSlot<I>...};
}
constexpr auto kProxyTrampolines =
    ProxyTrampolineTable(std::make_index_sequence<kProxyDispatchSlots>{});

// 合成类 <init>(InvocationHandler)：h 落字段（super ctor 语义的字段面）
Value ProxyInitHandler(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (self == nullptr || n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "proxy handler == null");
    *ok = false;
    return Value{};
  }
  VField* h_f = ProxyHandlerField(self->klass);
  if (h_f == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "proxy h field missing");
    *ok = false;
    return Value{};
  }
  std::memcpy(self->field_data() + h_f->offset, &args[0].ref, sizeof(args[0].ref));
  *ok = true;
  return Value{};
}

}  // namespace
Value ProxyDefine0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  Value r{};
  VObject* loader =
      (n > 0 && args[0].ref != nullptr) ? reinterpret_cast<VObject*>(args[0].ref) : nullptr;
  VArray* ifaces =
      (n > 1 && args[1].ref != nullptr) ? reinterpret_cast<VArray*>(args[1].ref) : nullptr;
  if (ifaces == nullptr || ifaces->length == 0) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "no interfaces");
    *ok = false;
    return r;
  }
  std::vector<VClass*> direct;
  std::string key = std::to_string(reinterpret_cast<uintptr_t>(loader)) + "|";
  for (int32_t i = 0; i < ifaces->length; i++) {
    VClass* ic = reinterpret_cast<VClass*>(ArrRef(ifaces, i));
    if (ic == nullptr) {
      state.ThrowNew("Ljava/lang/NullPointerException;", "interface == null");
      *ok = false;
      return r;
    }
    if (!ic->IsInterface()) {
      state.ThrowNew("Ljava/lang/IllegalArgumentException;", "not an interface: " + ic->descriptor);
      *ok = false;
      return r;
    }
    for (VClass* seen : direct) {
      if (seen == ic) {
        state.ThrowNew("Ljava/lang/IllegalArgumentException;",
                       "repeated interface: " + ic->descriptor);
        *ok = false;
        return r;
      }
    }
    direct.push_back(ic);
    key += ic->descriptor + ";";
  }
  for (const auto& e : g_proxy_defined) {
    if (e.first == key) {
      r.ref = e.second;
      *ok = true;
      return r;
    }
  }

  // 方法集：接口闭包全部实例方法（含 default/超接口）按签名去重；
  // Object 公共面（equals/hashCode/toString）并入（OpenJDK 路由 handler 口径）。
  std::vector<VMethod*> methods;
  auto add_method = [&methods](VMethod* m) {
    for (VMethod* seen : methods) {
      if (seen->name == m->name && seen->return_type == m->return_type &&
          seen->param_types == m->param_types) {
        return;
      }
    }
    methods.push_back(m);
  };
  std::vector<VClass*> stack = direct;
  std::vector<VClass*> seen_ifaces;
  while (!stack.empty()) {
    VClass* cur = stack.back();
    stack.pop_back();
    if (cur == nullptr) continue;
    bool dup = false;
    for (VClass* s : seen_ifaces) dup = dup || s == cur;
    if (dup) continue;
    seen_ifaces.push_back(cur);
    for (VClass* pi : cur->interfaces) stack.push_back(pi);
    for (auto& m : cur->virtual_methods) {
      if (!m.IsStatic() && m.name != "<init>" && m.name != "<clinit>") add_method(&m);
    }
    for (auto& m : cur->direct_methods) {
      if (!m.IsStatic() && m.name != "<init>" && m.name != "<clinit>") add_method(&m);
    }
  }
  {
    VClass* obj = state.linker->FindClass("Ljava/lang/Object;");
    const char* kObjectTrio[] = {"equals", "hashCode", "toString"};
    for (const char* nm : kObjectTrio) {
      if (obj == nullptr) break;
      VMethod* found = nullptr;
      for (auto& m : obj->virtual_methods) {
        if (m.name == nm) {
          found = &m;
          break;
        }
      }
      if (found == nullptr) {
        for (auto& m : obj->direct_methods) {
          if (m.name == nm) {
            found = &m;
            break;
          }
        }
      }
      if (found != nullptr) add_method(found);
    }
  }
  if (methods.size() > kProxyDispatchSlots) {
    state.ThrowNew("Ljava/lang/UnsupportedOperationException;",
                   "proxy method count " + std::to_string(methods.size()) + " exceeds " +
                       std::to_string(kProxyDispatchSlots));
    *ok = false;
    return r;
  }

  VClass* proxy_super = state.linker->FindClass("Ljava/lang/reflect/Proxy;");
  if (proxy_super == nullptr) {
    state.ThrowNew("Ljava/lang/NoClassDefFoundError;", "Ljava/lang/reflect/Proxy;");
    *ok = false;
    return r;
  }
  void* mem = state.linker->heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) {
    ThrowOOM(state, "proxy class");
    *ok = false;
    return r;
  }
  VClass* k = new (mem) VClass();
  static int32_t s_proxy_seq = 0;
  k->descriptor = "Ljava/lang/reflect/Proxy$Proxy" + std::to_string(s_proxy_seq++) + ";";
  k->super_class = proxy_super;
  k->interfaces = direct;
  k->access_flags = 0x1 | 0x10;  // public final
  k->object_size = proxy_super->object_size;
  k->status = ClassStatus::kInitialized;
  k->class_loader = loader;
  k->klass = state.linker->FindClass("Ljava/lang/Class;");
  for (size_t i = 0; i < methods.size(); i++) {
    VMethod m = *methods[i];
    m.declaring = k;
    m.access_flags = (m.access_flags & ~(0x400u | 0x100u)) | 0x1u;  // 去 abstract/native，public
    m.code = nullptr;
    m.dex_file = nullptr;
    m.method_idx = 0;
    m.jni_native_fn = nullptr;
    m.native_fn = ToIntrinsicSlot(kProxyTrampolines[i]);
    k->virtual_methods.push_back(m);
    g_proxy_dispatch[i].push_back({k, methods[i]});
  }
  {
    VMethod init;
    init.declaring = k;
    init.name = "<init>";
    init.shorty = "VL";
    init.param_types = {"Ljava/lang/reflect/InvocationHandler;"};
    init.return_type = "V";
    init.access_flags = 0x1u | 0x10000u;  // public <init>
    init.native_fn = ToIntrinsicSlot(ProxyInitHandler);
    k->direct_methods.push_back(init);
  }
  state.linker->RegisterBuiltinClass(k);
  g_proxy_defined.push_back({key, k});
  r.ref = k;
  *ok = true;
  return r;
}

Value ProxyIsProxy0(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)n;
  VClass* k = (n > 0 && args[0].ref != nullptr) ? reinterpret_cast<VClass*>(args[0].ref) : nullptr;
  Value r{};
  for (const auto& e : g_proxy_defined) {
    if (e.second == k) {
      r.i = 1;
      *ok = true;
      return r;
    }
  }
  r.i = 0;
  *ok = true;
  return r;
}

}  // namespace dexvm::runtime
