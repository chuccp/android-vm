// java.lang.String/StringBuilder/StringUTF16 语义面（intrinsic 实现）。
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

#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "session/class_loader.h"
#include "session/session.h"


#include "core/builtins_internal.h"

namespace dexvm::runtime {

// ==== M3：java.lang.String 方法面（intrinsic）====
// 语义对照 java_lang_String.cc（native 方法）+ libcore String.java（其余公开
// 方法面）。本 VM 的 String 为 VString 内建类（jre-excluded 语义：VM 内建优先、
// runtime.dex 不得遮蔽，docs 03 章 8.4），方法面按 Android java.lang.String
// 规格以 intrinsic 落地；regex（matches/replaceAll/split/replaceFirst）、
// format、locale 变体不在此列。
VString* AsString(ObjHeader* o) { return static_cast<VString*>(o); }

// Throwable.detailMessage 字段（沿继承链；与 exception.cc 同语义）
VField* FindDetailMessage(VClass* klass) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields) {
      if (f.name == "detailMessage") return &f;
    }
  }
  return nullptr;
}

std::u16string CopyChars(const VString* s) {
  return std::u16string(s->chars(), s->chars() + s->length);
}

Value MakeStr(VMState& state, const std::u16string& chars, bool* ok) {
  Value r{};
  VString* s = state.linker != nullptr ? state.linker->AllocString(chars) : nullptr;
  if (s == nullptr) {
    ThrowOOM(state, "string allocation failed");
    *ok = false;
    return Value{};
  }
  r.ref = reinterpret_cast<ObjHeader*>(s);
  return r;
}

// char[] 实参 → chars（8 字节槽低 16 位，与 aget-char 一致）
namespace {
bool TakeCharArray(VMState& state, const Value& v, std::u16string* out, bool* ok) {
  if (v.ref == nullptr) {
    ThrowNullPointerException(state, "null char[]");
    *ok = false;
    return false;
  }
  VArray* arr = static_cast<VArray*>(v.ref);
  out->clear();
  out->reserve(static_cast<size_t>(arr->length));
  for (int32_t i = 0; i < arr->length; i++) {
    uint64_t slot;
    std::memcpy(&slot, arr->data() + static_cast<size_t>(i) * 8, sizeof(slot));
    out->push_back(static_cast<char16_t>(slot & 0xffffu));
  }
  return true;
}

// Object.toString() 虚拟调用（valueOf(Object)/StringBuilder.append(Object) 面）
}  // namespace
bool ObjectToString(VMState& state, ObjHeader* obj, std::u16string* out) {
  if (obj == nullptr) {
    *out = u"null";
    return true;
  }
  if (obj->klass == nullptr) return false;
  VMethod probe;
  probe.name = "toString";
  probe.param_types = {};
  probe.return_type = "Ljava/lang/String;";
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return false;
  Value args[1] = {};
  args[0].ref = obj;
  Value r{};
  if (!InvokeMethod(state, m, args, 1, &r)) return false;
  VString* s = r.ref != nullptr ? static_cast<VString*>(r.ref) : nullptr;
  if (s == nullptr) {
    *out = u"null";
    return true;
  }
  out->assign(s->chars(), s->chars() + s->length);
  return true;
}

// Class.getName() 形态（primitive 关键字 / 数组 descriptor 点形态 / 点名）
std::string ClassName(const VClass* k) {
  if (k == nullptr) return "null";
  const std::string& d = k->descriptor;
  if (k->is_primitive) {
    switch (k->primitive_type) {
      case 'V': return "void";
      case 'Z': return "boolean";
      case 'B': return "byte";
      case 'S': return "short";
      case 'C': return "char";
      case 'I': return "int";
      case 'J': return "long";
      case 'F': return "float";
      case 'D': return "double";
      default: break;
    }
  }
  if (!d.empty() && d[0] == '[') {
    std::string out = d;
    std::replace(out.begin(), out.end(), '/', '.');
    return out;
  }
  return session::DescriptorToDot(d);
}

// ---- Java 数字格式化（Integer/Long/Float/Double.toString 语义）----

void AppendUtf16(std::u16string* out, const char* ascii) {
  for (const char* p = ascii; *p != '\0'; ++p) out->push_back(static_cast<char16_t>(static_cast<unsigned char>(*p)));
}

void AppendInt64(std::u16string* out, int64_t v) {
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
  AppendUtf16(out, buf);
}

// Java Float/Double.toString：最短往返有效数字 + 10 进制/科学计数法择形。
// plain 判定（对照 FloatingDecimal）：10^-3 <= |x| < 10^7 → 十进制；否则科学。
void AppendJavaDouble(std::u16string* out, double d, bool is_float) {
  if (std::isnan(d)) {
    AppendUtf16(out, "NaN");
    return;
  }
  if (std::isinf(d)) {
    AppendUtf16(out, d < 0 ? "-Infinity" : "Infinity");
    return;
  }
  if (d == 0.0) {
    AppendUtf16(out, std::signbit(d) ? "-0.0" : "0.0");
    return;
  }
  bool neg = std::signbit(d);
  double a = neg ? -d : d;
  // 最短往返有效数字：p 位正确舍入串中取首个可往返者（对照 FloatingDecimal
  // 的最短区分位数选择；尾零不会出现在最短解中）
  char buf[64];
  std::string digits;
  int exp10 = 0;
  int maxp = is_float ? 9 : 17;
  for (int p = 1; p <= maxp; p++) {
    std::snprintf(buf, sizeof(buf), "%.*e", p - 1, a);
    double back = std::strtod(buf, nullptr);
    bool roundtrip = is_float ? (static_cast<float>(back) == static_cast<float>(a)) : (back == a);
    if (roundtrip || p == maxp) {
      // 解析 d.dddde±xx
      const char* ep = buf;
      while (*ep != 'e' && *ep != 'E' && *ep != '\0') ep++;
      digits.clear();
      for (const char* q = buf; q < ep; q++) {
        if (*q != '.') digits.push_back(*q);
      }
      exp10 = std::atoi(ep + 1);  // 跳过 'e'/'E'（sprintf 形如 d.ddde±xx）
      break;
    }
  }
  while (digits.size() > 1 && digits.back() == '0') digits.pop_back();  // 防御
  std::string body;
  bool plain = exp10 >= -3 && exp10 <= 6;
  if (plain) {
    if (exp10 < 0) {
      body = "0.";
      body.append(static_cast<size_t>(-exp10 - 1), '0');
      body += digits;
    } else if (digits.size() <= static_cast<size_t>(exp10) + 1) {
      body = digits;
      body.append(static_cast<size_t>(exp10 + 1) - digits.size(), '0');
      body += ".0";
    } else {
      body = digits.substr(0, static_cast<size_t>(exp10) + 1);
      body += '.';
      body += digits.substr(static_cast<size_t>(exp10) + 1);
    }
  } else {
    body = digits.substr(0, 1);
    body += '.';
    body += digits.size() > 1 ? digits.substr(1) : std::string("0");
    body += 'E';
    body += std::to_string(exp10);
  }
  if (neg) out->push_back(u'-');
  AppendUtf16(out, body.c_str());
}

// ---- 简单大小写映射（Character.toUpperCase/toLowerCase；full SpecialCasing
// （ß→SS 等多字符展开）不在此列，见遗留项）----
char16_t SimpleUpper(char16_t c) {
  if (c >= u'a' && c <= u'z') return static_cast<char16_t>(c - 32);
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return static_cast<char16_t>(c - 32);  // Latin-1 小写
  return c;
}
char16_t SimpleLower(char16_t c) {
  if (c >= u'A' && c <= u'Z') return static_cast<char16_t>(c + 32);
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return static_cast<char16_t>(c + 32);  // Latin-1 大写
  return c;
}

// ---- java.lang.String intrinsic 方法面 ----

Value StrLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = AsString(self)->length;
  return r;
}

Value StrIsEmpty(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = AsString(self)->length == 0 ? 1 : 0;
  return r;
}

Value StrToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.ref = self;
  return r;
}

Value StrCharAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t idx = n >= 1 ? args[0].i : -1;
  Value r{};
  if (idx < 0 || idx >= s->length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(idx) + ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  r.i = s->chars()[idx];
  return r;
}

Value StrCodePointAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t idx = n >= 1 ? args[0].i : -1;
  Value r{};
  if (idx < 0 || idx >= s->length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(idx) + ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  char16_t c = s->chars()[idx];
  if (c >= 0xD800 && c <= 0xDBFF && idx + 1 < s->length) {
    char16_t c2 = s->chars()[idx + 1];
    if (c2 >= 0xDC00 && c2 <= 0xDFFF) {
      r.i = 0x10000 + ((c - 0xD800) << 10) + (c2 - 0xDC00);
      return r;
    }
  }
  r.i = c;
  return r;
}

Value StrCodePointBefore(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t idx = n >= 1 ? args[0].i : -1;
  Value r{};
  if (idx <= 0 || idx > s->length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(idx) + ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  char16_t c = s->chars()[idx - 1];
  if (c >= 0xDC00 && c <= 0xDFFF && idx >= 2) {
    char16_t c2 = s->chars()[idx - 2];
    if (c2 >= 0xD800 && c2 <= 0xDBFF) {
      r.i = 0x10000 + ((c2 - 0xD800) << 10) + (c - 0xDC00);
      return r;
    }
  }
  r.i = c;
  return r;
}

Value StrCodePointCount(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t begin = n >= 1 ? args[0].i : 0;
  int32_t end = n >= 2 ? args[1].i : 0;
  Value r{};
  if (begin < 0 || end > s->length || begin > end) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(begin) + ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  int32_t count = 0;
  for (int32_t i = begin; i < end; i++) {
    char16_t c = s->chars()[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < end) {
      char16_t c2 = s->chars()[i + 1];
      if (c2 >= 0xDC00 && c2 <= 0xDFFF) i++;
    }
    count++;
  }
  r.i = count;
  return r;
}

Value StrOffsetByCodePoints(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t index = n >= 1 ? args[0].i : 0;
  int32_t offset = n >= 2 ? args[1].i : 0;
  Value r{};
  if (index < 0 || index > s->length) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  int32_t cp = 0;
  int32_t i = index >= 0 ? 0 : 0;
  int32_t pos = 0;
  int32_t target = 0;
  // 正向：index 之前已有 cp 个 code point；再走 offset
  for (i = 0; i < index; i++) {
    char16_t c = s->chars()[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s->length) {
      char16_t c2 = s->chars()[i + 1];
      if (c2 >= 0xDC00 && c2 <= 0xDFFF) i++;
    }
    cp++;
  }
  (void)cp;
  pos = index;
  if (offset >= 0) {
    target = offset;
    while (target > 0 && pos < s->length) {
      char16_t c = s->chars()[pos];
      if (c >= 0xD800 && c <= 0xDBFF && pos + 1 < s->length) {
        char16_t c2 = s->chars()[pos + 1];
        if (c2 >= 0xDC00 && c2 <= 0xDFFF) pos++;
      }
      pos++;
      target--;
    }
    if (target > 0) {
      state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "offset out of bounds");
      *ok = false;
      return Value{};
    }
  } else {
    target = -offset;
    while (target > 0 && pos > 0) {
      pos--;
      char16_t c = s->chars()[pos];
      if (c >= 0xDC00 && c <= 0xDFFF && pos > 0) {
        char16_t c2 = s->chars()[pos - 1];
        if (c2 >= 0xD800 && c2 <= 0xDBFF) pos--;
      }
      target--;
    }
    if (target > 0) {
      state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;", "offset out of bounds");
      *ok = false;
      return Value{};
    }
  }
  r.i = pos;
  return r;
}

Value StrHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  VString* s = AsString(self);
  int32_t h = 0;
  for (int32_t i = 0; i < s->length; i++) h = 31 * h + s->chars()[i];
  Value r{};
  r.i = h;
  return r;
}

Value StrEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) return r;
  ObjHeader* other = args[0].ref;
  if (other->klass == nullptr || other->klass->descriptor != "Ljava/lang/String;") return r;
  VString* a = AsString(self);
  VString* b = AsString(other);
  if (a->length != b->length) return r;
  if (std::memcmp(a->chars(), b->chars(),
                  static_cast<size_t>(a->length) * sizeof(char16_t)) != 0) return r;
  r.i = 1;
  return r;
}

Value StrEqualsIgnoreCase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) return r;
  ObjHeader* other = args[0].ref;
  if (other->klass == nullptr || other->klass->descriptor != "Ljava/lang/String;") return r;
  VString* a = AsString(self);
  VString* b = AsString(other);
  if (a->length != b->length) return r;
  for (int32_t i = 0; i < a->length; i++) {
    char16_t ca = a->chars()[i], cb = b->chars()[i];
    if (ca != cb && SimpleUpper(ca) != SimpleUpper(cb) && SimpleLower(ca) != SimpleLower(cb)) {
      return r;
    }
  }
  r.i = 1;
  return r;
}

Value StrCompareTo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "compareTo null");
    *ok = false;
    return Value{};
  }
  VString* a = AsString(self);
  VString* b = AsString(args[0].ref);
  int32_t n1 = a->length, n2 = b->length;
  int32_t lim = n1 < n2 ? n1 : n2;
  for (int32_t i = 0; i < lim; i++) {
    int32_t d = a->chars()[i] - b->chars()[i];
    if (d != 0) {
      r.i = d;
      return r;
    }
  }
  r.i = n1 - n2;
  return r;
}

// Comparable<String> 桥（javac 合成 compareTo(Object) 同义；对照 Enum 的
// compareTo(Object) 桥注册）：非 String 收窄抛 CCE，null 抛 NPE，余同语义。
Value StrCompareToObject(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "compareTo null");
    *ok = false;
    return Value{};
  }
  ObjHeader* other = args[0].ref;
  if (other->klass == nullptr || other->klass->descriptor != "Ljava/lang/String;") {
    std::string from =
        other->klass != nullptr ? other->klass->descriptor : "null";
    state.ThrowNew("Ljava/lang/ClassCastException;",
                   from + " cannot be cast to java.lang.String");
    *ok = false;
    return Value{};
  }
  return StrCompareTo(state, self, args, n, ok);
}

// String.format(String, Object...) / format(Locale, String, Object...)：
// builtin String 无 Java 方法体可挂；语义核心在 java.util.Formatter（dex），
// 对照 vendored java.lang.String.format 同式转发（OpenJDK 语义：
// new Formatter().format(fmt, args).toString()）。
Value StrFormat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  bool with_locale =
      n >= 3 && args[0].ref != nullptr && args[0].ref->klass != nullptr &&
      args[0].ref->klass->descriptor == "Ljava/util/Locale;";
  ObjHeader* fmt = with_locale ? args[1].ref : args[0].ref;
  ObjHeader* argv = with_locale ? (n > 2 ? args[2].ref : nullptr) : (n > 1 ? args[1].ref : nullptr);
  if (fmt == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "format");
    *ok = false;
    return Value{};
  }
  VClass* fmt_cls =
      state.linker != nullptr ? state.linker->FindClass("Ljava/util/Formatter;") : nullptr;
  if (fmt_cls == nullptr) {
    state.ClearException();
    state.ThrowNew("Ljava/lang/InternalError;", "java.util.Formatter missing");
    *ok = false;
    return Value{};
  }
  if (!state.linker->InitializeClass(fmt_cls, &state) || state.HasPendingException()) {
    *ok = false;
    return Value{};
  }
  VObject* formatter = state.linker->AllocObject(fmt_cls);
  if (formatter == nullptr) {
    ThrowOOM(state, "String.format");
    *ok = false;
    return Value{};
  }
  VMethod* ctor = LookupDeclaredMethod(fmt_cls, "<init>", with_locale
                                        ? std::vector<std::string>{"Ljava/util/Locale;"}
                                        : std::vector<std::string>{},
                                      true);
  if (ctor == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Formatter <init> missing");
    *ok = false;
    return Value{};
  }
  Value ctor_args[2] = {};
  ctor_args[0].ref = formatter;
  size_t ctor_n = 1;
  if (with_locale) {
    ctor_args[1].ref = args[0].ref;
    ctor_n = 2;
  }
  Value r{};
  if (!InvokeMethod(state, ctor, ctor_args, ctor_n, &r)) {
    *ok = false;
    return Value{};
  }
  VMethod* fmt_m = LookupDeclaredMethod(
      fmt_cls, "format",
      {"Ljava/lang/String;", "[Ljava/lang/Object;"}, true);
  if (fmt_m == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Formatter.format missing");
    *ok = false;
    return Value{};
  }
  Value fm_args[3] = {};
  fm_args[0].ref = formatter;
  fm_args[1].ref = fmt;
  fm_args[2].ref = argv;
  if (!InvokeMethod(state, fmt_m, fm_args, 3, &r)) {
    *ok = false;
    return Value{};
  }
  VMethod* ts = LookupDeclaredMethod(fmt_cls, "toString", {}, true);
  if (ts == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Formatter.toString missing");
    *ok = false;
    return Value{};
  }
  Value ts_args[1] = {};
  ts_args[0].ref = r.ref != nullptr ? r.ref : reinterpret_cast<ObjHeader*>(formatter);
  if (!InvokeMethod(state, ts, ts_args, 1, &r)) {
    *ok = false;
    return Value{};
  }
  return r;
}

Value StrCompareToIgnoreCase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "compareTo null");
    *ok = false;
    return Value{};
  }
  VString* a = AsString(self);
  VString* b = AsString(args[0].ref);
  int32_t n1 = a->length, n2 = b->length;
  int32_t lim = n1 < n2 ? n1 : n2;
  for (int32_t i = 0; i < lim; i++) {
    int32_t d = SimpleLower(a->chars()[i]) - SimpleLower(b->chars()[i]);
    if (d != 0) {
      r.i = d;
      return r;
    }
  }
  r.i = n1 - n2;
  return r;
}

Value StrConcat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "concat null");
    *ok = false;
    return Value{};
  }
  std::u16string chars = CopyChars(AsString(self));
  VString* b = AsString(args[0].ref);
  chars.append(b->chars(), b->chars() + b->length);
  return MakeStr(state, chars, ok);
}

Value StrContains(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "contains null");
    *ok = false;
    return Value{};
  }
  std::u16string needle;
  if (args[0].ref->klass != nullptr &&
      args[0].ref->klass->descriptor == "Ljava/lang/String;") {
    needle = CopyChars(AsString(args[0].ref));
  } else if (!ObjectToString(state, args[0].ref, &needle)) {
    ThrowUnsupportedOperationException(state, "contains: CharSequence toString failed");
    *ok = false;
    return Value{};
  }
  std::u16string hay = CopyChars(AsString(self));
  r.i = hay.find(needle) != std::u16string::npos ? 1 : 0;
  return r;
}

Value StrContentEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) return r;  // null -> false
  std::u16string other;
  ObjHeader* o = args[0].ref;
  if (o->klass != nullptr && o->klass->descriptor == "Ljava/lang/String;") {
    other = CopyChars(AsString(o));
  } else if (!ObjectToString(state, o, &other)) {
    ThrowUnsupportedOperationException(state, "contentEquals: CharSequence toString failed");
    *ok = false;
    return Value{};
  }
  std::u16string me = CopyChars(AsString(self));
  r.i = me == other ? 1 : 0;
  return r;
}

Value StrStartsWith(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "startsWith null");
    *ok = false;
    return Value{};
  }
  std::u16string prefix = CopyChars(AsString(args[0].ref));
  int32_t off = n >= 2 ? args[1].i : 0;
  std::u16string me = CopyChars(AsString(self));
  if (off < 0 || off > static_cast<int32_t>(me.size())) return r;
  r.i = me.compare(static_cast<size_t>(off), prefix.size(), prefix) == 0 ? 1 : 0;
  return r;
}

Value StrEndsWith(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "endsWith null");
    *ok = false;
    return Value{};
  }
  std::u16string suffix = CopyChars(AsString(args[0].ref));
  std::u16string me = CopyChars(AsString(self));
  if (suffix.size() > me.size()) return r;
  r.i = me.compare(me.size() - suffix.size(), suffix.size(), suffix) == 0 ? 1 : 0;
  return r;
}

Value StrRegionMatches(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  // (toffset, other, ooffset, len) 或 (ignoreCase, toffset, other, ooffset, len)
  bool ignore = false;
  size_t ai = 0;
  if (n == 5) {
    ignore = args[0].i != 0;
    ai = 1;
  } else if (n != 4) {
    ThrowUnsupportedOperationException(state, "regionMatches: unsupported signature");
    *ok = false;
    return Value{};
  }
  int32_t toffset = args[ai].i;
  if (args[ai + 1].ref == nullptr) {
    ThrowNullPointerException(state, "regionMatches null");
    *ok = false;
    return Value{};
  }
  std::u16string other = CopyChars(AsString(args[ai + 1].ref));
  int32_t ooffset = args[ai + 2].i;
  int32_t len = args[ai + 3].i;
  std::u16string me = CopyChars(AsString(self));
  if (toffset < 0 || ooffset < 0 || len < 0 ||
      toffset > static_cast<int32_t>(me.size()) - len ||
      ooffset > static_cast<int32_t>(other.size()) - len) {
    return r;
  }
  for (int32_t i = 0; i < len; i++) {
    char16_t ca = me[static_cast<size_t>(toffset + i)];
    char16_t cb = other[static_cast<size_t>(ooffset + i)];
    if (ca == cb) continue;
    if (!ignore) return r;
    if (SimpleUpper(ca) != SimpleUpper(cb) && SimpleLower(ca) != SimpleLower(cb)) return r;
  }
  r.i = 1;
  return r;
}

// indexOf / lastIndexOf：按形参形态分派（int ch 与 String 分开注册，
// 避免把 int 位模式误读为引用）
Value StrIndexOfChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  Value r{};
  r.i = -1;
  std::u16string me = CopyChars(AsString(self));
  int32_t ch = args[0].i & 0xFFFF;
  int32_t from = n >= 2 ? args[1].i : 0;
  if (from < 0) from = 0;
  for (int32_t i = from; i < static_cast<int32_t>(me.size()); i++) {
    if (me[static_cast<size_t>(i)] == ch) {
      r.i = i;
      return r;
    }
  }
  return r;
}

Value StrIndexOfStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = -1;
  if (args[0].ref == nullptr) {
    ThrowNullPointerException(state, "indexOf null");
    *ok = false;
    return Value{};
  }
  std::u16string me = CopyChars(AsString(self));
  std::u16string needle = CopyChars(AsString(args[0].ref));
  int32_t from = n >= 2 ? args[1].i : 0;
  if (from < 0) from = 0;
  if (from > static_cast<int32_t>(me.size())) return r;
  size_t pos = me.find(needle, static_cast<size_t>(from));
  r.i = pos == std::u16string::npos ? -1 : static_cast<int32_t>(pos);
  return r;
}

Value StrLastIndexOfChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)ok;
  Value r{};
  r.i = -1;
  std::u16string me = CopyChars(AsString(self));
  int32_t ch = args[0].i & 0xFFFF;
  int32_t from = n >= 2 ? args[1].i : static_cast<int32_t>(me.size()) - 1;
  if (from > static_cast<int32_t>(me.size()) - 1) from = static_cast<int32_t>(me.size()) - 1;
  for (int32_t i = from; i >= 0; i--) {
    if (me[static_cast<size_t>(i)] == ch) {
      r.i = i;
      return r;
    }
  }
  return r;
}

Value StrLastIndexOfStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = -1;
  if (args[0].ref == nullptr) {
    ThrowNullPointerException(state, "lastIndexOf null");
    *ok = false;
    return Value{};
  }
  std::u16string me = CopyChars(AsString(self));
  std::u16string needle = CopyChars(AsString(args[0].ref));
  int32_t from = n >= 2 ? args[1].i : static_cast<int32_t>(me.size());
  if (from > static_cast<int32_t>(me.size())) from = static_cast<int32_t>(me.size());
  for (int32_t i = from; i >= 0; i--) {
    if (static_cast<size_t>(i) + needle.size() <= me.size() &&
        me.compare(static_cast<size_t>(i), needle.size(), needle) == 0) {
      r.i = i;
      return r;
    }
  }
  return r;
}

Value StrReplaceChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  char16_t oldc = static_cast<char16_t>(args[0].i & 0xFFFF);
  char16_t newc = static_cast<char16_t>(args[1].i & 0xFFFF);
  std::u16string me = CopyChars(AsString(self));
  if (oldc != newc) {
    for (auto& c : me) {
      if (c == oldc) c = newc;
    }
  }
  return MakeStr(state, me, ok);
}

Value StrReplaceSeq(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  // (CharSequence,CharSequence)：字面替换（非 regex）
  if (args[0].ref == nullptr || args[1].ref == nullptr) {
    ThrowNullPointerException(state, "replace null");
    *ok = false;
    return Value{};
  }
  std::u16string me = CopyChars(AsString(self));
  std::u16string target = CopyChars(AsString(args[0].ref));
  std::u16string repl = CopyChars(AsString(args[1].ref));
  if (target.empty()) {
    // Java：空目标在每字符间插入替换串
    std::u16string out;
    out.reserve(me.size() * (repl.size() + 1));
    for (size_t i = 0; i < me.size(); i++) {
      out += repl;
      out.push_back(me[i]);
    }
    out += repl;
    return MakeStr(state, out, ok);
  }
  std::u16string out;
  size_t pos = 0;
  while (true) {
    size_t hit = me.find(target, pos);
    if (hit == std::u16string::npos) {
      out.append(me, pos, std::u16string::npos);
      break;
    }
    out.append(me, pos, hit - pos);
    out += repl;
    pos = hit + target.size();
  }
  return MakeStr(state, out, ok);
}

Value StrSubstring1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t begin = n >= 1 ? args[0].i : 0;
  Value r{};
  if (begin < 0 || begin > s->length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "begin=" + std::to_string(begin) + ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  return MakeStr(state, CopyChars(s).substr(static_cast<size_t>(begin)), ok);
}

Value StrSubstring2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t begin = n >= 1 ? args[0].i : 0;
  int32_t end = n >= 2 ? args[1].i : 0;
  if (begin < 0 || end > s->length || begin > end) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "begin=" + std::to_string(begin) + ",end=" + std::to_string(end) +
                       ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  return MakeStr(state, CopyChars(s).substr(static_cast<size_t>(begin),
                                            static_cast<size_t>(end - begin)),
                 ok);
}

Value StrSubSequence(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return StrSubstring2(state, self, args, n, ok);
}

Value StrToLowerCase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  std::u16string me = CopyChars(AsString(self));
  for (auto& c : me) c = SimpleLower(c);
  return MakeStr(state, me, ok);
}

Value StrToUpperCase(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  std::u16string me = CopyChars(AsString(self));
  for (auto& c : me) c = SimpleUpper(c);
  return MakeStr(state, me, ok);
}

Value StrTrim(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  std::u16string me = CopyChars(AsString(self));
  size_t b = 0, e = me.size();
  while (b < e && me[b] <= u' ') b++;
  while (e > b && me[e - 1] <= u' ') e--;
  return MakeStr(state, me.substr(b, e - b), ok);
}

Value StrToCharArray(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  VString* s = AsString(self);
  VClass* arr_class =
      state.linker != nullptr
          ? state.linker->FindArrayClass(state.linker->PrimitiveClass('C'))
          : nullptr;
  VArray* arr = arr_class != nullptr ? state.linker->AllocArray(arr_class, s->length) : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "toCharArray");
    *ok = false;
    return Value{};
  }
  for (int32_t i = 0; i < s->length; i++) {
    uint64_t slot = s->chars()[i];
    std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &slot, sizeof(slot));
  }
  Value r{};
  r.ref = arr;
  return r;
}

// ---- getBytes 面（AOSP String.getBytes：默认 UTF-8）----
// UTF-8/US-ASCII/ISO-8859-1 热面本地收口（§31.7 A 档 String 简单热路径；
// 不可映射字符 '?'——AOSP CodingErrorAction.REPLACE 语义）；其余字符集抛
// UnsupportedEncodingException（复杂 charset 编解码归 lib 缝，按需后置）。
// 字符集名按 CharsetProvider 别名集归一（大小写 + '-''_''.'' ' 折叠）。
namespace {
std::u16string NormalizeCharsetName(const uint16_t* chars, int32_t len) {
  std::u16string s;
  s.reserve(static_cast<size_t>(len));
  for (int32_t i = 0; i < len; i++) {
    char16_t c = chars[i];
    if (c == u'-' || c == u'_' || c == u'.' || c == u' ') continue;
    if (c >= u'a' && c <= u'z') c = static_cast<char16_t>(c - u'a' + u'A');
    s.push_back(c);
  }
  return s;
}

bool CharsetIs(const std::u16string& name, std::initializer_list<const char16_t*> aliases) {
  for (const char16_t* a : aliases) {
    if (name == a) return true;
  }
  return false;
}

Value MakeByteArray(VMState& state, const std::string& bytes, bool* ok) {
  VClass* arr_class = state.linker != nullptr
                          ? state.linker->FindArrayClass(state.linker->PrimitiveClass('B'))
                          : nullptr;
  VArray* arr =
      arr_class != nullptr ? state.linker->AllocArray(arr_class, static_cast<int32_t>(bytes.size()))
                           : nullptr;
  if (arr == nullptr) {
    ThrowOOM(state, "getBytes");
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < bytes.size(); i++) {
    uint64_t slot = static_cast<uint8_t>(bytes[i]);
    std::memcpy(arr->data() + i * 8, &slot, sizeof(slot));
  }
  Value r{};
  r.ref = arr;
  return r;
}

void EncodeUtf8(const VString* s, std::string& out) {
  const uint16_t* c = s->chars();
  int32_t len = s->length;
  for (int32_t i = 0; i < len; i++) {
    uint32_t cp = c[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len && c[i + 1] >= 0xDC00 && c[i + 1] <= 0xDFFF) {
      cp = 0x10000u + ((cp - 0xD800u) << 10) + (c[i + 1] - 0xDC00u);
      i++;
    } else if (cp >= 0xD800 && cp <= 0xDFFF) {
      out.push_back('?');  // 孤立代理：AOSP REPLACE 语义
      continue;
    }
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
}

void Encode8Bit(const VString* s, std::string& out, uint32_t max_cp) {
  const uint16_t* c = s->chars();
  for (int32_t i = 0; i < s->length; i++) {
    uint32_t cp = c[i];
    out.push_back(cp <= max_cp ? static_cast<char>(cp) : '?');
  }
}

}  // namespace
Value StrGetBytes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  std::u16string name = u"UTF8";  // Android 默认字符集 UTF-8
  if (n >= 1) {
    if (args[0].ref == nullptr) {
      state.ThrowNew("Ljava/lang/NullPointerException;", "charsetName == null");
      *ok = false;
      return Value{};
    }
    VString* cs = AsString(reinterpret_cast<ObjHeader*>(args[0].ref));
    name = NormalizeCharsetName(cs->chars(), cs->length);
  }
  VString* me = AsString(self);
  std::string out;
  if (CharsetIs(name, {u"UTF8", u"UNICODE11UTF8"})) {
    EncodeUtf8(me, out);
  } else if (CharsetIs(name, {u"USASCII", u"ASCII", u"ISO646US", u"ANSIX341968", u"ANSIX341986",
                              u"IBM367", u"CP367", u"CSASCII", u"ISOIR6", u"ISO646BASIC1983",
                              u"ISO646IRV1983", u"ISO646IRV1991", u"646"})) {
    Encode8Bit(me, out, 0x7F);
  } else if (CharsetIs(name, {u"ISO88591", u"LATIN1", u"L1", u"ISOIR100", u"CSISOLATIN1",
                              u"IBM819", u"CP819", u"88591"})) {
    Encode8Bit(me, out, 0xFF);
  } else {
    std::string cname;
    for (char16_t ch : name) cname.push_back(ch <= 0x7F ? static_cast<char>(ch) : '?');
    state.ThrowNew("Ljava/io/UnsupportedEncodingException;", "charset " + cname);
    *ok = false;
    return Value{};
  }
  return MakeByteArray(state, out, ok);
}

// String.getBytes(Charset)：热面沿用本地编码器；长尾字符集委托 lib
// Charset.encode(String) → ByteBuffer（受检异常面不外泄）。
Value StrGetBytesCharset(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  *ok = true;
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "charset == null");
    *ok = false;
    return Value{};
  }
  VObject* charset = reinterpret_cast<VObject*>(args[0].ref);
  VString* me = AsString(self);
  Value name_v{};
  if (!CallVirtual(state, charset, "name", {}, "Ljava/lang/String;", nullptr, 0, &name_v)) {
    *ok = false;
    return Value{};
  }
  if (name_v.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "charset name == null");
    *ok = false;
    return Value{};
  }
  VString* cs = AsString(reinterpret_cast<ObjHeader*>(name_v.ref));
  std::u16string name = NormalizeCharsetName(cs->chars(), cs->length);
  std::string out;
  if (CharsetIs(name, {u"UTF8", u"UNICODE11UTF8"})) {
    EncodeUtf8(me, out);
  } else if (CharsetIs(name, {u"USASCII", u"ASCII", u"ISO646US", u"ANSIX341968", u"ANSIX341986",
                              u"IBM367", u"CP367", u"CSASCII", u"ISOIR6", u"ISO646BASIC1983",
                              u"ISO646IRV1983", u"ISO646IRV1991", u"646"})) {
    Encode8Bit(me, out, 0x7F);
  } else if (CharsetIs(name, {u"ISO88591", u"LATIN1", u"L1", u"ISOIR100", u"CSISOLATIN1",
                              u"IBM819", u"CP819", u"88591"})) {
    Encode8Bit(me, out, 0xFF);
  } else {
    // 通用字符集：lib Charset.encode(String) → ByteBuffer.get(byte[])
    VClass* byte_cls = state.linker != nullptr
                           ? state.linker->FindArrayClass(state.linker->PrimitiveClass('B'))
                           : nullptr;
    if (byte_cls == nullptr) {
      state.ThrowNew("Ljava/lang/InternalError;", "byte[] missing");
      *ok = false;
      return Value{};
    }
    Value encode_arg[1] = {};
    encode_arg[0].ref = reinterpret_cast<ObjHeader*>(me);
    Value bb{};
    if (!CallVirtual(state, charset, "encode", {"Ljava/lang/String;"}, "Ljava/nio/ByteBuffer;",
                     encode_arg, 1, &bb)) {
      *ok = false;
      return Value{};
    }
    if (bb.ref == nullptr) return Value{};
    Value rem{};
    if (!CallVirtual(state, reinterpret_cast<VObject*>(bb.ref), "remaining", {}, "I", nullptr, 0,
                     &rem)) {
      *ok = false;
      return Value{};
    }
    VArray* arr = state.linker->AllocArray(byte_cls, rem.i);
    if (arr == nullptr) {
      ThrowOOM(state, "getBytes");
      *ok = false;
      return Value{};
    }
    Value get_arg[1] = {};
    get_arg[0].ref = reinterpret_cast<ObjHeader*>(arr);
    Value ignored{};
    if (!CallVirtual(state, reinterpret_cast<VObject*>(bb.ref), "get", {"[B"},
                     "Ljava/nio/ByteBuffer;", get_arg, 1, &ignored)) {
      *ok = false;
      return Value{};
    }
    Value r{};
    r.ref = arr;
    return r;
  }
  return MakeByteArray(state, out, ok);
}

Value StrGetChars(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  VString* s = AsString(self);
  int32_t src_begin = n >= 1 ? args[0].i : 0;
  int32_t src_end = n >= 2 ? args[1].i : 0;
  Value r{};
  if (src_begin < 0 || src_begin > src_end || src_end > s->length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "srcBegin=" + std::to_string(src_begin) + ",srcEnd=" + std::to_string(src_end) +
                       ",length=" + std::to_string(s->length));
    *ok = false;
    return Value{};
  }
  if (n < 4 || args[2].ref == nullptr) {
    ThrowNullPointerException(state, "getChars null dst");
    *ok = false;
    return Value{};
  }
  VArray* dst = static_cast<VArray*>(args[2].ref);
  int32_t dst_begin = n >= 4 ? args[3].i : 0;
  if (dst_begin < 0 || dst_begin + (src_end - src_begin) > dst->length) {
    state.ThrowNew("Ljava/lang/ArrayIndexOutOfBoundsException;",
                   std::to_string(dst_begin + (src_end - src_begin) - dst->length));
    *ok = false;
    return Value{};
  }
  for (int32_t i = 0; i < src_end - src_begin; i++) {
    uint64_t slot = s->chars()[src_begin + i];
    std::memcpy(dst->data() + static_cast<size_t>(dst_begin + i) * 8, &slot, sizeof(slot));
  }
  return r;
}

// valueOf 系列
Value StrValueOfBool(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  return MakeStr(state, (n >= 1 && args[0].i != 0) ? std::u16string(u"true") : std::u16string(u"false"), ok);
}

Value StrValueOfChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string s(1, static_cast<char16_t>(n >= 1 ? args[0].i & 0xFFFF : 0));
  return MakeStr(state, s, ok);
}

Value StrValueOfInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string s;
  AppendInt64(&s, n >= 1 ? args[0].i : 0);
  return MakeStr(state, s, ok);
}

Value StrValueOfLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string s;
  AppendInt64(&s, n >= 1 ? args[0].l : 0);
  return MakeStr(state, s, ok);
}

Value StrValueOfFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string s;
  AppendJavaDouble(&s, n >= 1 ? args[0].f : 0.0f, true);
  return MakeStr(state, s, ok);
}

Value StrValueOfDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string s;
  AppendJavaDouble(&s, n >= 1 ? args[0].d : 0.0, false);
  return MakeStr(state, s, ok);
}

Value StrValueOfChars(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string chars;
  if (!TakeCharArray(state, n >= 1 ? args[0] : Value{}, &chars, ok)) return Value{};
  return MakeStr(state, chars, ok);
}

Value StrValueOfCharsRange(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string chars;
  if (!TakeCharArray(state, n >= 1 ? args[0] : Value{}, &chars, ok)) return Value{};
  int32_t off = n >= 2 ? args[1].i : 0;
  int32_t cnt = n >= 3 ? args[2].i : 0;
  if (off < 0 || cnt < 0 || off > static_cast<int32_t>(chars.size()) ||
      cnt > static_cast<int32_t>(chars.size()) - off) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "offset=" + std::to_string(off) + ",count=" + std::to_string(cnt) +
                       ",length=" + std::to_string(chars.size()));
    *ok = false;
    return Value{};
  }
  return MakeStr(state, chars.substr(static_cast<size_t>(off), static_cast<size_t>(cnt)), ok);
}

Value StrValueOfObject(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::u16string s;
  if (!ObjectToString(state, n >= 1 ? args[0].ref : nullptr, &s)) {
    ThrowUnsupportedOperationException(state, "valueOf(Object): toString failed");
    *ok = false;
    return Value{};
  }
  return MakeStr(state, s, ok);
}

Value StrIntern(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  Value r{};
  VString* s = AsString(self);
  if (state.linker != nullptr) {
    VString* interned = state.linker->intern_table()->Intern(s);
    r.ref = interned;
    return r;
  }
  r.ref = self;
  return r;
}

// ---- java.lang.StringBuilder（AbstractStringBuilder 语义：char[] value + int count）----
// 内建类：runtime.dex 未打包 java.lang（子集）实现面，且 OpenJDK StringBuilder
// 依赖 String 内部布局（value/coder），与本 VM 的 VString 内建类不兼容；
// 按 Android AbstractStringBuilder 规格以内建 intrinsic 落地（docs 03 章 8.4）。

struct SBState {
  VArray* value = nullptr;
  int32_t count = 0;
};

namespace {
SBState GetSB(const VObject* self) {
  SBState st;
  const uint8_t* base = reinterpret_cast<const VObject*>(self)->field_data();
  uint64_t slot;
  std::memcpy(&slot, base + 0, sizeof(slot));
  st.value = reinterpret_cast<VArray*>(static_cast<uintptr_t>(slot));
  std::memcpy(&slot, base + 8, sizeof(slot));
  st.count = static_cast<int32_t>(slot);
  return st;
}

void SetSB(VObject* self, const SBState& st) {
  uint8_t* base = self->field_data();
  uint64_t slot = reinterpret_cast<uintptr_t>(st.value);
  std::memcpy(base + 0, &slot, sizeof(slot));
  slot = static_cast<uint64_t>(static_cast<int64_t>(st.count));
  std::memcpy(base + 8, &slot, sizeof(slot));
}

// 确保 value 缓冲 >= need；空 value 等价 capacity 16（Java super(16) 语义）。
// 增长规则（AbstractStringBuilder.newCapacity）：newcap = max(need, old*2+2)。
VArray* SBEnsure(VMState& state, VObject* self, int32_t need, bool* ok) {
  SBState st = GetSB(self);
  int32_t cap = st.value != nullptr ? st.value->length : 16;
  if (st.value != nullptr && cap >= need) return st.value;
  if (st.value == nullptr && need <= 16) {
    VClass* arr_class16 = state.linker->FindArrayClass(state.linker->PrimitiveClass('C'));
    VArray* a16 = state.linker->AllocArray(arr_class16, 16);
    if (a16 == nullptr) {
      ThrowOOM(state, "StringBuilder init");
      *ok = false;
      return nullptr;
    }
    st.value = a16;
    SetSB(self, st);
    return a16;
  }
  int32_t newcap = cap * 2 + 2;
  if (newcap < need) newcap = need;
  VClass* arr_class = state.linker->FindArrayClass(state.linker->PrimitiveClass('C'));
  VArray* arr = state.linker->AllocArray(arr_class, newcap);
  if (arr == nullptr) {
    ThrowOOM(state, "StringBuilder growth");
    *ok = false;
    return nullptr;
  }
  for (int32_t i = 0; i < st.count; i++) {
    uint64_t slot;
    std::memcpy(&slot, st.value->data() + static_cast<size_t>(i) * 8, sizeof(slot));
    std::memcpy(arr->data() + static_cast<size_t>(i) * 8, &slot, sizeof(slot));
  }
  st.value = arr;
  SetSB(self, st);
  return arr;
}

bool SBAppendChars(VMState& state, VObject* self, const std::u16string& chars, bool* ok) {
  SBState st = GetSB(self);
  int32_t need = st.count + static_cast<int32_t>(chars.size());
  VArray* buf = SBEnsure(state, self, need, ok);
  if (buf == nullptr) return false;
  st = GetSB(self);
  for (size_t i = 0; i < chars.size(); i++) {
    uint64_t slot = chars[i];
    std::memcpy(buf->data() + static_cast<size_t>(st.count + i) * 8, &slot, sizeof(slot));
  }
  st.count = need;
  SetSB(self, st);
  return true;
}

std::u16string SBChars(const VObject* self) {
  SBState st = GetSB(self);
  std::u16string out;
  out.reserve(static_cast<size_t>(st.count));
  for (int32_t i = 0; i < st.count; i++) {
    uint64_t slot;
    std::memcpy(&slot, st.value->data() + static_cast<size_t>(i) * 8, sizeof(slot));
    out.push_back(static_cast<char16_t>(slot & 0xffffu));
  }
  return out;
}

void SBSetChars(VObject* self, const std::u16string& chars, bool* ok, VMState& state) {
  SBState st = GetSB(self);
  st.count = 0;
  SetSB(self, st);
  SBAppendChars(state, self, chars, ok);
}

}  // namespace
Value SBInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n; (void)ok;
  SBState st;
  SetSB(self, st);
  return Value{};
}

Value SBInitCap(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t cap = n >= 1 ? args[0].i : 16;
  if (cap < 0) {
    state.ThrowNew("Ljava/lang/NegativeArraySizeException;", std::to_string(cap));
    *ok = false;
    return Value{};
  }
  SBState st;
  SetSB(self, st);
  // AbstractStringBuilder(int)：value = new char[capacity]（精确容量，可为 0）
  VClass* arr_class = state.linker->FindArrayClass(state.linker->PrimitiveClass('C'));
  VArray* arr = state.linker->AllocArray(arr_class, cap);
  if (arr == nullptr) {
    ThrowOOM(state, "StringBuilder capacity");
    *ok = false;
    return Value{};
  }
  st.value = arr;
  st.count = 0;
  SetSB(self, st);
  return Value{};
}

Value SBInitStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  SBState st;
  SetSB(self, st);
  if (n >= 1 && args[0].ref != nullptr) {
    std::u16string chars = CopyChars(AsString(args[0].ref));
    if (!SBAppendChars(state, self, chars, ok)) return Value{};
  }
  return Value{};
}

Value SBInitCS(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  SBState st;
  SetSB(self, st);
  if (n >= 1 && args[0].ref != nullptr) {
    std::u16string chars;
    if (!ObjectToString(state, args[0].ref, &chars)) {
      ThrowUnsupportedOperationException(state, "StringBuilder(CharSequence) toString failed");
      *ok = false;
      return Value{};
    }
    if (!SBAppendChars(state, self, chars, ok)) return Value{};
  }
  return Value{};
}

Value SBAppendStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    if (!SBAppendChars(state, self, u"null", ok)) return Value{};
  } else {
    std::u16string chars = CopyChars(AsString(args[0].ref));
    if (!SBAppendChars(state, self, chars, ok)) return Value{};
  }
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendCS(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars;
  if (!ObjectToString(state, n >= 1 ? args[0].ref : nullptr, &chars)) {
    ThrowUnsupportedOperationException(state, "append(CharSequence) toString failed");
    *ok = false;
    return Value{};
  }
  if (!SBAppendChars(state, self, chars, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendObj(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return SBAppendCS(state, self, args, n, ok);
}

Value SBAppendCharsArg(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars;
  if (!TakeCharArray(state, n >= 1 ? args[0] : Value{}, &chars, ok)) return Value{};
  if (!SBAppendChars(state, self, chars, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendCharsRange(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars;
  if (!TakeCharArray(state, n >= 1 ? args[0] : Value{}, &chars, ok)) return Value{};
  int32_t off = n >= 2 ? args[1].i : 0;
  int32_t len = n >= 3 ? args[2].i : 0;
  if (off < 0 || len < 0 || off > static_cast<int32_t>(chars.size()) ||
      len > static_cast<int32_t>(chars.size()) - off) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "offset=" + std::to_string(off) + ",len=" + std::to_string(len) +
                       ",length=" + std::to_string(chars.size()));
    *ok = false;
    return Value{};
  }
  if (!SBAppendChars(state, self, chars.substr(static_cast<size_t>(off), static_cast<size_t>(len)),
                     ok)) {
    return Value{};
  }
  Value r{};
  r.ref = self;
  return r;
}

// append(CharSequence,int,int)/insert(int,CharSequence,int,int)（CS 子序列面）。
// OpenJDK 语义：csq == null → "null" 串；越界 StringIndexOutOfBoundsException。
namespace {
bool CSRangeChars(VMState& state, const Value* args, size_t n, size_t base, std::u16string* out,
                  bool* ok) {
  std::u16string chars;
  if (n <= base + 2 || args[base].ref == nullptr) {
    chars = u"null";
  } else if (!ObjectToString(state, args[base].ref, &chars)) {
    ThrowUnsupportedOperationException(state, "CharSequence toString failed");
    *ok = false;
    return false;
  }
  int32_t start = n > base + 1 ? args[base + 1].i : 0;
  int32_t end = n > base + 2 ? args[base + 2].i : static_cast<int32_t>(chars.size());
  if (start < 0 || end > static_cast<int32_t>(chars.size()) || start > end) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "start " + std::to_string(start) + ", end " + std::to_string(end) +
                       ", length " + std::to_string(chars.size()));
    *ok = false;
    return false;
  }
  *out = chars.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
  return true;
}

}  // namespace
Value SBAppendCSRange(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string sub;
  if (!CSRangeChars(state, args, n, 0, &sub, ok)) return Value{};
  if (!SBAppendChars(state, self, sub, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertCSRange(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string sub;
  if (!CSRangeChars(state, args, n, 1, &sub, ok)) return Value{};
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), sub);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendBool(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (!SBAppendChars(state, self, (n >= 1 && args[0].i != 0) ? u"true" : u"false", ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s(1, static_cast<char16_t>(n >= 1 ? args[0].i & 0xFFFF : 0));
  if (!SBAppendChars(state, self, s, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendInt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s;
  AppendInt64(&s, n >= 1 ? args[0].i : 0);
  if (!SBAppendChars(state, self, s, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s;
  AppendInt64(&s, n >= 1 ? args[0].l : 0);
  if (!SBAppendChars(state, self, s, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s;
  AppendJavaDouble(&s, n >= 1 ? args[0].f : 0.0f, true);
  if (!SBAppendChars(state, self, s, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s;
  AppendJavaDouble(&s, n >= 1 ? args[0].d : 0.0, false);
  if (!SBAppendChars(state, self, s, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBAppendCodePoint(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t cp = n >= 1 ? args[0].i : 0;
  if (cp < 0 || cp > 0x10FFFF) {
    ThrowIllegalArgumentException(state, "appendCodePoint " + std::to_string(cp));
    *ok = false;
    return Value{};
  }
  std::u16string s;
  if (cp < 0x10000) {
    s.push_back(static_cast<char16_t>(cp));
  } else {
    int32_t v = cp - 0x10000;
    s.push_back(static_cast<char16_t>(0xD800 + (v >> 10)));
    s.push_back(static_cast<char16_t>(0xDC00 + (v & 0x3FF)));
  }
  if (!SBAppendChars(state, self, s, ok)) return Value{};
  Value r{};
  r.ref = self;
  return r;
}

Value SBToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  return MakeStr(state, SBChars(self), ok);
}

Value SBLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = GetSB(self).count;
  return r;
}

Value SBCapacity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  SBState st = GetSB(self);
  Value r{};
  r.i = st.value != nullptr ? st.value->length : 16;
  return r;
}

Value SBEnsureCapacity(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t min = n >= 1 ? args[0].i : 0;
  if (min > 0) SBEnsure(state, self, min, ok);
  return Value{};
}

Value SBTrimToSize(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  SBState st = GetSB(self);
  if (st.value != nullptr && st.value->length > st.count) {
    if (st.count == 0) {
      st.value = nullptr;
      SetSB(self, st);
    } else {
      std::u16string chars = SBChars(self);
      st.count = 0;
      st.value = nullptr;
      SetSB(self, st);
      SBAppendChars(state, self, chars, ok);
    }
  }
  return Value{};
}

Value SBCharAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  int32_t idx = n >= 1 ? args[0].i : -1;
  Value r{};
  if (idx < 0 || idx >= GetSB(self).count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(idx) + ",length=" + std::to_string(GetSB(self).count));
    *ok = false;
    return Value{};
  }
  r.i = chars[static_cast<size_t>(idx)];
  return r;
}

Value SBCodePointAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // 复用 String 语义
  VString* tmp = nullptr;
  std::u16string chars = SBChars(self);
  (void)tmp;
  if (state.linker == nullptr) {
    *ok = false;
    return Value{};
  }
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrCodePointAt(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBCodePointBefore(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  if (state.linker == nullptr) {
    *ok = false;
    return Value{};
  }
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrCodePointBefore(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBCodePointCount(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrCodePointCount(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBOffsetByCodePoints(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrOffsetByCodePoints(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBSetCharAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  SBState st = GetSB(self);
  int32_t idx = n >= 1 ? args[0].i : -1;
  if (idx < 0 || idx >= st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(idx) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  VArray* buf = SBEnsure(state, self, st.count, ok);
  if (buf == nullptr) return Value{};
  uint64_t slot = static_cast<uint64_t>(n >= 2 ? args[1].i & 0xFFFF : 0);
  std::memcpy(buf->data() + static_cast<size_t>(idx) * 8, &slot, sizeof(slot));
  return Value{};
}

Value SBSetLength(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t nl = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (nl < 0) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "newLength=" + std::to_string(nl));
    *ok = false;
    return Value{};
  }
  if (nl <= st.count) {
    st.count = nl;
    SetSB(self, st);
    return Value{};
  }
  VArray* buf = SBEnsure(state, self, nl, ok);
  if (buf == nullptr) return Value{};
  st = GetSB(self);
  for (int32_t i = st.count; i < nl; i++) {
    uint64_t slot = 0;
    std::memcpy(buf->data() + static_cast<size_t>(i) * 8, &slot, sizeof(slot));
  }
  st.count = nl;
  SetSB(self, st);
  return Value{};
}

Value SBDelete(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t start = n >= 1 ? args[0].i : 0;
  int32_t end = n >= 2 ? args[1].i : 0;
  SBState st = GetSB(self);
  if (start < 0 || start > end || end > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "start=" + std::to_string(start) + ",end=" + std::to_string(end) +
                       ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string chars = SBChars(self);
  chars.erase(static_cast<size_t>(start), static_cast<size_t>(end - start));
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBDeleteCharAt(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t idx = n >= 1 ? args[0].i : -1;
  SBState st = GetSB(self);
  if (idx < 0 || idx >= st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(idx) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string chars = SBChars(self);
  chars.erase(static_cast<size_t>(idx), 1);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBReplace(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t start = n >= 1 ? args[0].i : 0;
  int32_t end = n >= 2 ? args[1].i : 0;
  SBState st = GetSB(self);
  if (start < 0 || start > end || end > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "start=" + std::to_string(start) + ",end=" + std::to_string(end) +
                       ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string repl;
  if (n >= 3 && args[2].ref != nullptr) repl = CopyChars(AsString(args[2].ref));
  std::u16string chars = SBChars(self);
  chars.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), repl);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

// insert 系列：插入前缀 [0, index) 之后
Value SBInsertStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string ins;
  if (n >= 2 && args[1].ref != nullptr) {
    if (args[1].ref->klass != nullptr &&
        args[1].ref->klass->descriptor == "Ljava/lang/String;") {
      ins = CopyChars(AsString(args[1].ref));
    } else if (!ObjectToString(state, args[1].ref, &ins)) {
      ThrowUnsupportedOperationException(state, "insert toString failed");
      *ok = false;
      return Value{};
    }
  } else {
    ins = u"null";
  }
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), ins);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s(1, static_cast<char16_t>(n >= 2 ? args[1].i & 0xFFFF : 0));
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), s);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertCharsArg(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string ins;
  if (!TakeCharArray(state, n >= 2 ? args[1] : Value{}, &ins, ok)) return Value{};
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), ins);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertCharsRange(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string src;
  if (!TakeCharArray(state, n >= 2 ? args[1] : Value{}, &src, ok)) return Value{};
  int32_t off = n >= 3 ? args[2].i : 0;
  int32_t len = n >= 4 ? args[3].i : 0;
  if (off < 0 || len < 0 || off > static_cast<int32_t>(src.size()) ||
      len > static_cast<int32_t>(src.size()) - off) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "offset=" + std::to_string(off) + ",len=" + std::to_string(len));
    *ok = false;
    return Value{};
  }
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), src, static_cast<size_t>(off), static_cast<size_t>(len));
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertBool(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string s = (n >= 2 && args[1].i != 0) ? u"true" : u"false";
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), s);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertNum(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // (int index, int/long/float/double) 由调用方注册的具体变体拼数
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string s;
  AppendInt64(&s, n >= 2 ? args[1].i : 0);
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), s);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertLong(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string s;
  AppendInt64(&s, n >= 2 ? args[1].l : 0);
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), s);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertFloat(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string s;
  AppendJavaDouble(&s, n >= 2 ? args[1].f : 0.0f, true);
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), s);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBInsertDouble(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  int32_t index = n >= 1 ? args[0].i : 0;
  SBState st = GetSB(self);
  if (index < 0 || index > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index=" + std::to_string(index) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  std::u16string s;
  AppendJavaDouble(&s, n >= 2 ? args[1].d : 0.0, false);
  std::u16string chars = SBChars(self);
  chars.insert(static_cast<size_t>(index), s);
  SBSetChars(self, chars, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBReverse(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  // Java AbstractStringBuilder.reverse：按 code point 逆序，代理对保持前后序
  std::u16string chars = SBChars(self);
  std::vector<std::u16string> units;  // 每个元素为 1 个 code unit 或 1 个代理对
  for (size_t i = 0; i < chars.size();) {
    char16_t c = chars[i];
    if (c >= 0xD800 && c <= 0xDBFF && i + 1 < chars.size() && chars[i + 1] >= 0xDC00 &&
        chars[i + 1] <= 0xDFFF) {
      units.push_back(chars.substr(i, 2));
      i += 2;
    } else {
      units.push_back(chars.substr(i, 1));
      i += 1;
    }
  }
  std::u16string out;
  out.reserve(chars.size());
  for (auto it = units.rbegin(); it != units.rend(); ++it) out += *it;
  SBSetChars(self, out, ok, state);
  Value r{};
  r.ref = self;
  return r;
}

Value SBSubstring1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  SBState st = GetSB(self);
  int32_t start = n >= 1 ? args[0].i : 0;
  if (start < 0 || start > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "start=" + std::to_string(start) + ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  return MakeStr(state, SBChars(self).substr(static_cast<size_t>(start)), ok);
}

Value SBSubstring2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  SBState st = GetSB(self);
  int32_t start = n >= 1 ? args[0].i : 0;
  int32_t end = n >= 2 ? args[1].i : 0;
  if (start < 0 || start > end || end > st.count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "start=" + std::to_string(start) + ",end=" + std::to_string(end) +
                       ",length=" + std::to_string(st.count));
    *ok = false;
    return Value{};
  }
  return MakeStr(state, SBChars(self).substr(static_cast<size_t>(start),
                                             static_cast<size_t>(end - start)),
                 ok);
}

Value SBSubSequence(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return SBSubstring2(state, self, args, n, ok);
}

Value SBIndexOfChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrIndexOfChar(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBIndexOfStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrIndexOfStr(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBLastIndexOfChar(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrLastIndexOfChar(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBLastIndexOfStr(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrLastIndexOfStr(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

Value SBGetChars(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::u16string chars = SBChars(self);
  Value sref = MakeStr(state, chars, ok);
  if (!*ok) return Value{};
  return StrGetChars(state, static_cast<VObject*>(sref.ref), args, n, ok);
}

// StringBuilder.compareTo(StringBuilder)：逐字符比较（AbstractStringBuilder 语义）
Value SBCompareTo(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  r.i = 0;
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "compareTo null");
    *ok = false;
    return Value{};
  }
  std::u16string a = SBChars(self);
  std::u16string b = SBChars(static_cast<VObject*>(args[0].ref));
  size_t lim = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < lim; i++) {
    int32_t d = a[i] - b[i];
    if (d != 0) {
      r.i = d;
      return r;
    }
  }
  r.i = static_cast<int32_t>(a.size() - b.size());
  return r;
}

// Comparable<StringBuilder> 桥（javac 合成 compareTo(Object) 同义；对照
// String/Enum 的 compareTo(Object) 桥注册）：非 StringBuilder 收窄抛 CCE。
Value SBCompareToObject(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  if (n < 1 || args[0].ref == nullptr) {
    ThrowNullPointerException(state, "compareTo null");
    *ok = false;
    return Value{};
  }
  ObjHeader* other = args[0].ref;
  if (other->klass == nullptr || other->klass->descriptor != "Ljava/lang/StringBuilder;") {
    std::string from = other->klass != nullptr ? other->klass->descriptor : "null";
    state.ThrowNew("Ljava/lang/ClassCastException;",
                   from + " cannot be cast to java.lang.StringBuilder");
    *ok = false;
    return Value{};
  }
  return SBCompareTo(state, self, args, n, ok);
}
// ---- java.lang.Object 元方法（valueOf(Object)/toString 链路）----

Value ObjHashCode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.i = static_cast<int32_t>(reinterpret_cast<uintptr_t>(self) >> 3);
  return r;
}

Value ObjEquals(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)n; (void)ok;
  Value r{};
  r.i = (n >= 1 && args[0].ref == self) ? 1 : 0;
  return r;
}

Value ObjToString(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args; (void)n;
  std::string name = ClassName(self->klass);
  char buf[32];
  std::snprintf(buf, sizeof(buf), "@%08x",
                static_cast<unsigned>(reinterpret_cast<uintptr_t>(self) >> 3));
  std::u16string out;
  AppendUtf16(&out, (name + buf).c_str());
  return MakeStr(state, out, ok);
}

Value ObjGetClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state; (void)args; (void)n; (void)ok;
  Value r{};
  r.ref = self->klass;
  return r;
}

// java.lang.Object.clone（Java 语义）：数组 = 公共浅拷贝（同型同长，元素槽直拷，
// Enum.values() 等编译产物经此取副本）；Cloneable 对象 = 字段区浅拷贝；
// 其余抛 CloneNotSupportedException。
Value ObjClone(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  *ok = true;
  Value r{};
  if (self == nullptr || self->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "clone on null");
    *ok = false;
    return r;
  }
  VClass* k = self->klass;
  if (k->is_array) {
    VArray* a = reinterpret_cast<VArray*>(self);
    VArray* copy = state.linker->AllocArray(k, a->length);
    if (copy == nullptr) {
      ThrowOOM(state, "Object.clone(array)");
      *ok = false;
      return r;
    }
    std::memcpy(copy->data(), a->data(), static_cast<size_t>(a->length) * 8);
    r.ref = copy;
    return r;
  }
  VClass* cloneable = state.linker->FindClass("Ljava/lang/Cloneable;");
  if (cloneable == nullptr || !cloneable->IsAssignableFrom(k)) {
    state.ThrowNew("Ljava/lang/CloneNotSupportedException;", k->descriptor);
    *ok = false;
    return r;
  }
  VObject* copy = state.linker->AllocObject(k);
  if (copy == nullptr) {
    ThrowOOM(state, "Object.clone");
    *ok = false;
    return r;
  }
  if (k->object_size > sizeof(ObjHeader)) {
    std::memcpy(copy->field_data(), self->field_data(), k->object_size - sizeof(ObjHeader));
  }
  r.ref = copy;
  return r;
}
// ---- java.lang.String.split（regex 面薄委托：lib java.util.regex.Pattern）----
// §31.7：String 是 A 档 VM 接缝类（builtin），split 为小面委托——regex 引擎语义
// 全在 lib（runtime.dex java.util.regex.Pattern 纯 Java）；本节只做派发转发。
namespace {
bool StrSplitImpl(VMState& state, VObject* self, const Value* args, size_t n, bool has_limit,
                  Value* result) {
  if (n < 1 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "split: regex == null");
    return false;
  }
  VClass* pat_cls = state.linker->FindClass("Ljava/util/regex/Pattern;");
  if (pat_cls == nullptr || !CheckClassDefinition(state, pat_cls) ||
      !state.linker->InitializeClass(pat_cls, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "java.util.regex.Pattern missing");
    }
    return false;
  }
  VMethod* compile =
      FindStaticM(pat_cls, "compile", {"Ljava/lang/String;"}, "Ljava/util/regex/Pattern;");
  if (compile == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Pattern.compile missing");
    return false;
  }
  Value cv{};
  cv.ref = args[0].ref;
  Value pat{};
  if (!InvokeMethod(state, compile, &cv, 1, &pat)) return false;
  if (pat.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Pattern.compile returned null");
    return false;
  }
  VMethod probe;
  probe.name = "split";
  probe.return_type = "[Ljava/lang/String;";
  if (has_limit) {
    probe.param_types = {"Ljava/lang/CharSequence;", "I"};
  } else {
    probe.param_types = {"Ljava/lang/CharSequence;"};
  }
  VMethod* split = pat.ref->klass->FindInterfaceImpl(&probe);
  if (split == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Pattern.split missing");
    return false;
  }
  Value sv[3] = {};
  sv[0].ref = pat.ref;
  sv[1].ref = self;
  if (has_limit) sv[2].i = n >= 2 ? args[1].i : 0;
  return InvokeMethod(state, split, sv, has_limit ? 3u : 2u, result);
}

}  // namespace
Value StrSplit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  if (!StrSplitImpl(state, self, args, n, false, &r)) {
    *ok = false;
    return Value{};
  }
  return r;
}
Value StrSplitLimit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  if (!StrSplitImpl(state, self, args, n, true, &r)) {
    *ok = false;
    return Value{};
  }
  return r;
}
// ---- java.lang.String.matches/replaceAll（regex 面薄委托：lib java.util.regex）----
// §31.7：String 是 A 档 VM 接缝类（builtin），regex 面同 split 先例小面委托——
// 匹配/替换语义全在 lib（Pattern/Matcher 纯 Java）；本节只做派发转发。
namespace {
bool StrCompilePattern(VMState& state, const Value& regex_arg, Value* pat_out) {
  if (regex_arg.ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "regex == null");
    return false;
  }
  VClass* pat_cls = state.linker->FindClass("Ljava/util/regex/Pattern;");
  if (pat_cls == nullptr || !CheckClassDefinition(state, pat_cls) ||
      !state.linker->InitializeClass(pat_cls, &state)) {
    if (!state.HasPendingException()) {
      state.ThrowNew("Ljava/lang/InternalError;", "java.util.regex.Pattern missing");
    }
    return false;
  }
  VMethod* compile =
      FindStaticM(pat_cls, "compile", {"Ljava/lang/String;"}, "Ljava/util/regex/Pattern;");
  if (compile == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Pattern.compile missing");
    return false;
  }
  if (!InvokeMethod(state, compile, &regex_arg, 1, pat_out)) return false;
  if (pat_out->ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "Pattern.compile returned null");
    return false;
  }
  return true;
}

bool StrMatcherOf(VMState& state, VObject* pat, VObject* self, Value* matcher_out) {
  VMethod probe;
  probe.name = "matcher";
  probe.param_types = {"Ljava/lang/CharSequence;"};
  probe.return_type = "Ljava/util/regex/Matcher;";
  VMethod* matcher = pat->klass != nullptr ? pat->klass->FindInterfaceImpl(&probe) : nullptr;
  if (matcher == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Pattern.matcher missing");
    return false;
  }
  Value mv[2] = {};
  mv[0].ref = pat;
  mv[1].ref = self;
  return InvokeMethod(state, matcher, mv, 2, matcher_out);
}

}  // namespace
Value StrMatches(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  if (n < 1) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", "matches");
    *ok = false;
    return Value{};
  }
  Value pat{};
  if (!StrCompilePattern(state, args[0], &pat)) {
    *ok = false;
    return Value{};
  }
  Value matcher{};
  if (!StrMatcherOf(state, static_cast<VObject*>(pat.ref), self, &matcher)) {
    *ok = false;
    return Value{};
  }
  VMethod probe;
  probe.name = "matches";
  probe.return_type = "Z";
  VMethod* matches = matcher.ref != nullptr && matcher.ref->klass != nullptr
                        ? matcher.ref->klass->FindInterfaceImpl(&probe)
                        : nullptr;
  if (matches == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Matcher.matches missing");
    *ok = false;
    return Value{};
  }
  Value mv{};
  mv.ref = matcher.ref;
  if (!InvokeMethod(state, matches, &mv, 1, &r)) {
    *ok = false;
    return Value{};
  }
  return r;
}
Value StrReplaceAll(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  Value r{};
  if (n < 2 || args[1].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "replaceAll: replacement == null");
    *ok = false;
    return Value{};
  }
  Value pat{};
  if (!StrCompilePattern(state, args[0], &pat)) {
    *ok = false;
    return Value{};
  }
  Value matcher{};
  if (!StrMatcherOf(state, static_cast<VObject*>(pat.ref), self, &matcher)) {
    *ok = false;
    return Value{};
  }
  VMethod probe;
  probe.name = "replaceAll";
  probe.param_types = {"Ljava/lang/String;"};
  probe.return_type = "Ljava/lang/String;";
  VMethod* replace_all = matcher.ref != nullptr && matcher.ref->klass != nullptr
                             ? matcher.ref->klass->FindInterfaceImpl(&probe)
                             : nullptr;
  if (replace_all == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "Matcher.replaceAll missing");
    *ok = false;
    return Value{};
  }
  Value mv[2] = {};
  mv[0].ref = matcher.ref;
  mv[1].ref = args[1].ref;
  if (!InvokeMethod(state, replace_all, mv, 2, &r)) {
    *ok = false;
    return Value{};
  }
  return r;
}
// <clinit> HI/LO_BYTE_SHIFT 消费——内建 String 面助手与 lib 字节缓冲通道衔接）
Value StringUTF16IsBigEndian(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)state; (void)self; (void)args; (void)n; (void)ok;
  Value r{};
  const uint16_t probe = 1;
  r.i = *reinterpret_cast<const uint8_t*>(&probe) == 0 ? 1 : 0;
  return r;
}
// ---- java.lang.String java.base 内部界限面（lib StringUTF16/StringLatin1 消费）----
// String = A 档 mirror 布局类（builtin），lib 助手编译期引用其包内 static 界限
// 检查（checkOffset/checkIndex/checkBoundsBeginEnd/checkBoundsOffCount）；
// 语义照抄 vendored java/lang/String.java（StringIndexOutOfBoundsException 口径）。

Value StrCheckOffset(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int32_t offset = n > 0 ? args[0].i : 0;
  int32_t length = n > 1 ? args[1].i : 0;
  if (offset < 0 || offset > length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "offset " + std::to_string(offset) + ",length " + std::to_string(length));
    *ok = false;
    return Value{};
  }
  *ok = true;
  return Value{};
}

Value StrCheckIndex(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int32_t index = n > 0 ? args[0].i : 0;
  int32_t length = n > 1 ? args[1].i : 0;
  if (index < 0 || index >= length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "index " + std::to_string(index) + ",length " + std::to_string(length));
    *ok = false;
    return Value{};
  }
  *ok = true;
  return Value{};
}

Value StrCheckBoundsBeginEnd(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  int32_t begin = n > 0 ? args[0].i : 0;
  int32_t end = n > 1 ? args[1].i : 0;
  int32_t length = n > 2 ? args[2].i : 0;
  if (begin < 0 || begin > end || end > length) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;", "begin " + std::to_string(begin) +
                                                                      ", end " +
                                                                      std::to_string(end) +
                                                                      ", length " +
                                                                      std::to_string(length));
    *ok = false;
    return Value{};
  }
  *ok = true;
  return Value{};
}

Value StrCheckBoundsOffCount(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  int32_t offset = n > 0 ? args[0].i : 0;
  int32_t count = n > 1 ? args[1].i : 0;
  int32_t length = n > 2 ? args[2].i : 0;
  if (offset < 0 || count < 0 || offset > length - count) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "offset " + std::to_string(offset) + ", count " + std::to_string(count) +
                       ", length " + std::to_string(length));
    *ok = false;
    return Value{};
  }
  *ok = true;
  return Value{};
}

// coder()：内建 VString 恒 UTF16 存储（COMPACT_STRINGS=false 契约）→ 恒 1。
Value StrCoder(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  Value r{};
  r.i = 1;
  *ok = true;
  return r;
}

// 内部面 getBytes(byte[] dst, int dstBegin, byte coder)：本串按目标 coder 写入
// dst（lib AbstractStringBuilder.putStringAt 消费；字节序 = StringUTF16.
// putChar/getChar 同契约——HI/LO 按宿主端序）。内建串恒 UTF16 源：
//   coder=UTF16(1)：每 char 两字节写 dst[(dstBegin+i)<<1 ...]；
//   coder=LATIN1(0)：每 char 窄化单字节（低 8 位）写 dst[dstBegin+i]。
Value StrGetBytesInternal(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  if (self == nullptr || n < 3 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "getBytes dst");
    *ok = false;
    return Value{};
  }
  VArray* dst = reinterpret_cast<VArray*>(args[0].ref);
  int32_t dst_begin = args[1].i;
  int32_t coder = args[2].i;
  const VString* s = reinterpret_cast<const VString*>(static_cast<const ObjHeader*>(self));
  const int32_t count = s->length;
  if (dst_begin < 0 || (coder != 0 && coder != 1)) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "dstBegin " + std::to_string(dst_begin) + ", coder " + std::to_string(coder));
    *ok = false;
    return Value{};
  }
  const int32_t need = coder == 1 ? ((dst_begin + count) << 1) : (dst_begin + count);
  if (need > dst->length || dst_begin + count < dst_begin) {
    state.ThrowNew("Ljava/lang/StringIndexOutOfBoundsException;",
                   "dstBegin " + std::to_string(dst_begin) + ", count " + std::to_string(count) +
                       ", length " + std::to_string(dst->length));
    *ok = false;
    return Value{};
  }
  const uint16_t probe = 1;
  const bool big = *reinterpret_cast<const uint8_t*>(&probe) == 0;
  const int hi_shift = big ? 8 : 0;
  const int lo_shift = big ? 0 : 8;
  for (int32_t i = 0; i < count; i++) {
    uint16_t c = s->chars()[i];
    if (coder == 1) {
      int32_t idx = (dst_begin + i) << 1;
      uint64_t b0 = static_cast<uint64_t>((c >> hi_shift) & 0xffu);
      uint64_t b1 = static_cast<uint64_t>((c >> lo_shift) & 0xffu);
      std::memcpy(dst->data() + static_cast<size_t>(idx) * 8, &b0, 8);
      std::memcpy(dst->data() + static_cast<size_t>(idx + 1) * 8, &b1, 8);
    } else {
      uint64_t b0 = static_cast<uint64_t>(c & 0xffu);
      std::memcpy(dst->data() + static_cast<size_t>(dst_begin + i) * 8, &b0, 8);
    }
  }
  *ok = true;
  return Value{};
}

}  // namespace dexvm::runtime
