// Uri/UriMatcher/ContentResolver 家族 smoke（tools/uri_resolver_tests.sh）。
// 引导链：RegisterBuiltins + RegisterFrameworkBuiltins + runtime.dex boot +
// uri_resolver.apk app loader（tests/uri-src fixture，aapt2 打包真 binary
// manifest）。覆盖：
//   1) Uri 查询面（RFC 2396/AOSP StringUri 口径：分量拆解/解码面/查询参数/
//      opaque/relative/host-port-userInfo/encode-decode/normalizeScheme/
//      equals-hashCode/fromParts/withAppendedPath/EMPTY）
//   2) UriMatcher lib 面（runtime.dex 消费链：'*'/'#' 通配、特异度序
//      字面>#>*、DFS 回溯、长路径段匹配、尾 '/' 容错、负 code IAE）
//   3) ContentResolver 路由（attachInfo 实例注册表分发：CRUD 全形到
//      fixture provider、authority 分号多值逐键、ContentValues lib 读回、
//      bulkInsert/call 委派；未注册 authority 的 query = 安静 null 游标
//      （厂商 referrer 等探测链兼容口径，见 CRQuery 注释））
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/framework_builtins.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "framework/lifecycle_driver.h"
#include "session/session.h"

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

using dexvm::framework::ConfigFromManifest;
using dexvm::framework::LifecycleConfig;
using dexvm::framework::LifecycleDriver;
using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::Value;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VMState;
using dexvm::runtime::VObject;
using dexvm::runtime::VString;

std::u16string ToU16(const std::string& s) { return std::u16string(s.begin(), s.end()); }

std::string ResultUtf8(const Value& r) {
  VString* s = reinterpret_cast<VString*>(r.ref);
  return s != nullptr ? s->ToModifiedUtf8() : "<null>";
}

VField* FindField(VClass* klass, const char* name) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& f : k->instance_fields)
      if (f.name == name) return &f;
    for (auto& f : k->static_fields)
      if (f.name == name) return &f;
  }
  return nullptr;
}

std::string ReadStaticString(VClass* klass, const char* name) {
  VField* f = klass != nullptr ? FindField(klass, name) : nullptr;
  if (f == nullptr || !f->is_static || f->offset >= klass->static_storage.size()) return "<null>";
  const Value& slot = klass->static_storage[f->offset];
  return slot.ref != nullptr ? static_cast<VString*>(slot.ref)->ToModifiedUtf8() : "<null>";
}

bool CallVirtual(VMState& state, VObject* obj, const char* name, std::vector<std::string> params,
                 const char* ret, const Value* args, size_t n, Value* out) {
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return false;
  std::vector<Value> full;
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!InvokeMethod(state, m, full.data(), full.size(), &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

// 静态方法查找（FindInterfaceImpl 不搜 static direct；同 builtins 口径）。
VMethod* FindStaticM(VClass* klass, const char* name, std::vector<std::string> params,
                     const char* ret) {
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (auto& m : k->direct_methods) {
      if (m.IsStatic() && m.name == name && m.return_type == ret && m.param_types == params) {
        return &m;
      }
    }
  }
  return nullptr;
}

bool CallStatic(VMState& state, VClass* klass, const char* name, std::vector<std::string> params,
                const char* ret, const Value* args, size_t n, Value* out) {
  VMethod* m = FindStaticM(klass, name, std::move(params), ret);
  if (m == nullptr) return false;
  Value r{};
  if (!InvokeMethod(state, m, args, n, &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

std::string PendingDetail(VMState& state) {
  ObjHeader* ex = state.pending_exception;
  if (ex == nullptr || ex->klass == nullptr) return "<none>";
  std::string desc = ex->klass->descriptor;
  std::string msg;
  VObject* obj = reinterpret_cast<VObject*>(ex);
  for (VClass* k = obj->klass; k != nullptr && msg.empty(); k = k->super_class) {
    for (const auto& f : k->instance_fields) {
      if (f.name == "detailMessage") {
        ObjHeader* s = nullptr;
        std::memcpy(&s, obj->field_data() + f.offset, sizeof(s));
        if (s != nullptr) msg = static_cast<VString*>(s)->ToModifiedUtf8();
        break;
      }
    }
  }
  return desc + ": " + msg;
}

// ---- 便捷：Uri.parse / getter ----

VObject* ParseUri(VMState& state, ClassLinker& linker, const char* s) {
  VClass* uri_cls = linker.FindClass("Landroid/net/Uri;");
  Value arg{};
  arg.ref = linker.AllocString(ToU16(s));
  Value r{};
  if (uri_cls == nullptr) return nullptr;
  if (!CallStatic(state, uri_cls, "parse", {"Ljava/lang/String;"}, "Landroid/net/Uri;", &arg, 1,
                  &r)) {
    return nullptr;
  }
  return reinterpret_cast<VObject*>(r.ref);
}

// toString() 取用（Uri 身份/拼串断言）
std::string ObjToString(VMState& state, VObject* obj) {
  Value t{};
  if (!CallVirtual(state, obj, "toString", {}, "Ljava/lang/String;", nullptr, 0, &t)) return "<err>";
  return ResultUtf8(t);
}

// 数组元素写（8 字节槽；同 framework_builtins 口径）
void ArrSetRef(dexvm::runtime::VArray* a, int32_t i, ObjHeader* v) {
  std::memcpy(a->data() + static_cast<size_t>(i) * 8, &v, sizeof(v));
}

// expect "<null>" 表示期望 Java null。
bool UriStrIs(VMState& state, VObject* u, const char* getter, const char* expect) {
  Value r{};
  if (!CallVirtual(state, u, getter, {}, "Ljava/lang/String;", nullptr, 0, &r)) return false;
  if (r.ref == nullptr) return std::string(expect) == "<null>";
  return ResultUtf8(r) == expect;
}

int32_t UriInt(VMState& state, VObject* u, const char* getter) {
  Value r{};
  if (!CallVirtual(state, u, getter, {}, "I", nullptr, 0, &r)) return -999;
  return static_cast<int32_t>(r.l);
}

bool UriBool(VMState& state, VObject* u, const char* getter) {
  Value r{};
  if (!CallVirtual(state, u, getter, {}, "Z", nullptr, 0, &r)) return false;
  return r.l != 0;
}

// UriMatcher（lib，runtime.dex boot）：new UriMatcher(code) -> 实例。
VObject* NewMatcher(VMState& state, ClassLinker& linker, int32_t code) {
  VClass* k = linker.FindClass("Landroid/content/UriMatcher;");
  if (k == nullptr) return nullptr;
  VObject* o = linker.AllocObject(k);
  if (o == nullptr) return nullptr;
  Value arg{};
  arg.i = code;
  if (!CallVirtual(state, o, "<init>", {"I"}, "V", &arg, 1, nullptr)) return nullptr;
  return o;
}

void MatcherAdd(VMState& state, VObject* m, const char* auth, const char* path, int32_t code) {
  Value args[3] = {};
  args[0].ref = state.linker->AllocString(ToU16(auth));
  args[1].ref = path != nullptr ? state.linker->AllocString(ToU16(path)) : nullptr;
  args[2].i = code;
  CallVirtual(state, m, "addURI",
              {"Ljava/lang/String;", "Ljava/lang/String;", "I"}, "V", args, 3, nullptr);
}

int32_t MatcherMatch(VMState& state, VObject* m, VObject* uri) {
  Value arg{};
  arg.ref = uri;
  Value r{};
  if (!CallVirtual(state, m, "match", {"Landroid/net/Uri;"}, "I", &arg, 1, &r)) return -999;
  return static_cast<int32_t>(r.l);
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== uri_resolver smoke ==\n");

  const std::string runtime_dex =
      argc > 1 ? argv[1] : "runtime-libs/build/runtime-dex/runtime.dex";
  const std::string fixture_apk =
      argc > 2 ? argv[2] : "app/src/main/cpp/tests/out/uri_resolver.apk";

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  dexvm::runtime::RegisterFrameworkBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  std::string err;
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;

  // ---- boot + fixture 装载 ----
  {
    auto boot = dexvm::session::Session::Open(runtime_dex, &err);
    CHECK(boot != nullptr && !boot->dex_blobs().empty(), "open runtime.dex");
    if (boot == nullptr) return 1;
    for (const auto& blob : boot->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse boot dex: " + blob.name).c_str());
      if (dex_file == nullptr) return 1;
      linker.AddBootDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
  }
  auto app_session = dexvm::session::Session::Open(fixture_apk, &err);
  CHECK(app_session != nullptr && !app_session->dex_blobs().empty(), "open fixture APK");
  if (app_session == nullptr) {
    std::printf("  error: %s\n", err.c_str());
    return 1;
  }
  VObject* app_loader = nullptr;
  {
    for (const auto& blob : app_session->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse app dex: " + blob.name).c_str());
      if (dex_file == nullptr) return 1;
      linker.AddAppDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    VClass* cl = linker.FindClass("Ljava/lang/ClassLoader;");
    app_loader = linker.AllocObject(cl);
    linker.loaders().BindObject(app_loader, linker.loaders().default_app());
    CHECK(app_loader != nullptr, "app loader bound");
  }

  // ---- 1) Uri 查询面（分量拆解/解码）----
  {
    VObject* u = ParseUri(state, linker,
                          "content://com.example.auth/a/b/c?x=1&y=hello%20world#frag");
    CHECK(u != nullptr, "Uri.parse(content sample)");
    if (u != nullptr) {
      CHECK(UriStrIs(state, u, "getScheme", "content"), "getScheme == content");
      CHECK(UriStrIs(state, u, "getAuthority", "com.example.auth"), "getAuthority");
      CHECK(UriStrIs(state, u, "getEncodedAuthority", "com.example.auth"), "getEncodedAuthority");
      CHECK(UriStrIs(state, u, "getPath", "/a/b/c"), "getPath");
      CHECK(UriStrIs(state, u, "getEncodedPath", "/a/b/c"), "getEncodedPath");
      CHECK(UriStrIs(state, u, "getQuery", "x=1&y=hello world"), "getQuery 解码");
      CHECK(UriStrIs(state, u, "getEncodedQuery", "x=1&y=hello%20world"), "getEncodedQuery");
      CHECK(UriStrIs(state, u, "getFragment", "frag"), "getFragment");
      CHECK(UriStrIs(state, u, "getHost", "com.example.auth"), "getHost");
      CHECK(UriInt(state, u, "getPort") == -1, "getPort == -1");
      CHECK(UriStrIs(state, u, "getUserInfo", "<null>"), "getUserInfo == null");
      CHECK(UriBool(state, u, "isHierarchical"), "isHierarchical");
      CHECK(!UriBool(state, u, "isOpaque"), "!isOpaque");
      CHECK(UriBool(state, u, "isAbsolute"), "isAbsolute");
      CHECK(!UriBool(state, u, "isRelative"), "!isRelative");
      CHECK(UriStrIs(state, u, "getLastPathSegment", "c"), "getLastPathSegment");

      Value segs{};
      CHECK(CallVirtual(state, u, "getPathSegments", {}, "Ljava/util/List;", nullptr, 0, &segs),
            "getPathSegments call");
      VObject* list = reinterpret_cast<VObject*>(segs.ref);
      if (list != nullptr) {
        Value sz{};
        CallVirtual(state, list, "size", {}, "I", nullptr, 0, &sz);
        CHECK(sz.l == 3, "segments.size() == 3");
        Value idx{};
        idx.i = 1;
        Value one{};
        CallVirtual(state, list, "get", {"I"}, "Ljava/lang/Object;", &idx, 1, &one);
        CHECK(ResultUtf8(one) == "b", "segments.get(1) == b");
      }

      Value k{};
      k.ref = linker.AllocString(u"x");
      Value v{};
      CHECK(CallVirtual(state, u, "getQueryParameter", {"Ljava/lang/String;"},
                        "Ljava/lang/String;", &k, 1, &v) &&
                ResultUtf8(v) == "1",
            "getQueryParameter(x) == 1");
      k.ref = linker.AllocString(u"y");
      CHECK(CallVirtual(state, u, "getQueryParameter", {"Ljava/lang/String;"},
                        "Ljava/lang/String;", &k, 1, &v) &&
                ResultUtf8(v) == "hello world",
            "getQueryParameter(y) == hello world");
      k.ref = linker.AllocString(u"z");
      CHECK(CallVirtual(state, u, "getQueryParameter", {"Ljava/lang/String;"},
                        "Ljava/lang/String;", &k, 1, &v) &&
                v.ref == nullptr,
            "getQueryParameter(missing) == null");

      // getQueryParameterNames / getQueryParameters
      Value names{};
      CHECK(CallVirtual(state, u, "getQueryParameterNames", {}, "Ljava/util/Set;", nullptr, 0,
                        &names) &&
                names.ref != nullptr,
            "getQueryParameterNames call");
      VObject* nset = reinterpret_cast<VObject*>(names.ref);
      if (nset != nullptr) {
        Value sz{};
        CallVirtual(state, nset, "size", {}, "I", nullptr, 0, &sz);
        CHECK(sz.l == 2, "parameterNames.size() == 2");
      }
    }

    // 多值参数 + 缺 '=' 值
    {
      VObject* u = ParseUri(state, linker, "content://a/p?x=1&y&x=3");
      Value k{};
      k.ref = linker.AllocString(u"x");
      Value list_v{};
      CHECK(CallVirtual(state, u, "getQueryParameters", {"Ljava/lang/String;"},
                        "Ljava/util/List;", &k, 1, &list_v) &&
                list_v.ref != nullptr,
            "getQueryParameters(x) call");
      VObject* list = reinterpret_cast<VObject*>(list_v.ref);
      if (list != nullptr) {
        Value sz{};
        CallVirtual(state, list, "size", {}, "I", nullptr, 0, &sz);
        CHECK(sz.l == 2, "getQueryParameters(x).size() == 2");
        Value idx{};
        idx.i = 1;
        Value one{};
        CallVirtual(state, list, "get", {"I"}, "Ljava/lang/Object;", &idx, 1, &one);
        CHECK(ResultUtf8(one) == "3", "getQueryParameters(x).get(1) == 3");
      }
      k.ref = linker.AllocString(u"y");
      Value yv{};
      CHECK(CallVirtual(state, u, "getQueryParameter", {"Ljava/lang/String;"},
                        "Ljava/lang/String;", &k, 1, &yv) &&
                yv.ref != nullptr && ResultUtf8(yv) == "",
            "getQueryParameter(y 无 '=') == 空串");
    }

    // userinfo/port/host 拆解
    {
      VObject* u = ParseUri(state, linker, "http://user:pw@host.example:8080/p");
      CHECK(u != nullptr, "Uri.parse(userinfo sample)");
      if (u != nullptr) {
        CHECK(UriStrIs(state, u, "getUserInfo", "user:pw"), "getUserInfo == user:pw");
        CHECK(UriStrIs(state, u, "getHost", "host.example"), "getHost == host.example");
        CHECK(UriInt(state, u, "getPort") == 8080, "getPort == 8080");
        CHECK(UriStrIs(state, u, "getAuthority", "user:pw@host.example:8080"), "getAuthority");
        CHECK(UriStrIs(state, u, "getPath", "/p"), "getPath == /p");
      }
    }

    // opaque（mailto）：分量 null 面 + 查询参数抛 UnsupportedOperationException
    {
      VObject* u = ParseUri(state, linker, "mailto:foo@bar.com");
      CHECK(u != nullptr, "Uri.parse(mailto sample)");
      if (u != nullptr) {
        CHECK(UriBool(state, u, "isOpaque"), "mailto isOpaque");
        CHECK(!UriBool(state, u, "isHierarchical"), "mailto !isHierarchical");
        CHECK(UriStrIs(state, u, "getScheme", "mailto"), "mailto getScheme");
        CHECK(UriStrIs(state, u, "getSchemeSpecificPart", "foo@bar.com"), "opaque ssp");
        CHECK(UriStrIs(state, u, "getPath", "<null>"), "opaque getPath == null");
        CHECK(UriStrIs(state, u, "getAuthority", "<null>"), "opaque getAuthority == null");
        CHECK(UriStrIs(state, u, "getQuery", "<null>"), "opaque getQuery == null");
        Value k{};
        k.ref = linker.AllocString(u"x");
        Value v{};
        bool ok = CallVirtual(state, u, "getQueryParameter", {"Ljava/lang/String;"},
                              "Ljava/lang/String;", &k, 1, &v);
        CHECK(!ok && state.HasPendingException() &&
                  PendingDetail(state).find("Ljava/lang/UnsupportedOperationException;") == 0,
              "opaque getQueryParameter -> UnsupportedOperationException");
        state.ClearException();
        Value segs{};
        CallVirtual(state, u, "getPathSegments", {}, "Ljava/util/List;", nullptr, 0, &segs);
        VObject* list = reinterpret_cast<VObject*>(segs.ref);
        if (list != nullptr) {
          Value sz{};
          CallVirtual(state, list, "size", {}, "I", nullptr, 0, &sz);
          CHECK(sz.l == 0, "opaque getPathSegments == []");
        }
      }
    }

    // relative（无 scheme）+ authority 缺席 + 空 path/query 面
    {
      VObject* u = ParseUri(state, linker, "/plain/path");
      CHECK(u != nullptr && UriBool(state, u, "isRelative"), "relative sample");
      if (u != nullptr) {
        CHECK(UriStrIs(state, u, "getScheme", "<null>"), "relative getScheme == null");
        CHECK(UriStrIs(state, u, "getPath", "/plain/path"), "relative getPath");
        CHECK(UriStrIs(state, u, "getAuthority", "<null>"), "relative getAuthority == null");
        CHECK(UriBool(state, u, "isHierarchical"), "relative isHierarchical");
      }
      VObject* a = ParseUri(state, linker, "content://auth");
      CHECK(a != nullptr && UriStrIs(state, a, "getPath", ""), "content://auth getPath == ''");
      if (a != nullptr) {
        CHECK(UriStrIs(state, a, "getQuery", "<null>"), "无 '?' getQuery == null");
        CHECK(UriStrIs(state, a, "getAuthority", "auth"), "空 path authority 面");
      }
      VObject* b = ParseUri(state, linker, "content://auth?");
      CHECK(b != nullptr && UriStrIs(state, b, "getQuery", ""), "'?' 空 query == ''");
    }
  }

  // ---- 2) 编解码/构造/身份面 ----
  {
    VClass* uri_cls = linker.FindClass("Landroid/net/Uri;");
    Value s{};
    s.ref = linker.AllocString(u"a%20b%2Fc");
    Value r{};
    CHECK(CallStatic(state, uri_cls, "decode", {"Ljava/lang/String;"}, "Ljava/lang/String;", &s, 1,
                     &r) &&
              ResultUtf8(r) == "a b/c",
          "Uri.decode('a%20b%2Fc') == 'a b/c'");
    s.ref = linker.AllocString(u"a b/c");
    CHECK(CallStatic(state, uri_cls, "encode", {"Ljava/lang/String;"}, "Ljava/lang/String;", &s, 1,
                     &r) &&
              ResultUtf8(r) == "a%20b%2Fc",
          "Uri.encode('a b/c') == 'a%20b%2Fc'");
    s.ref = linker.AllocString(u"a_b-c!~'()*");
    CHECK(CallStatic(state, uri_cls, "encode", {"Ljava/lang/String;"}, "Ljava/lang/String;", &s, 1,
                     &r) &&
              ResultUtf8(r) == "a_b-c!~'()*",
          "Uri.encode 白名单原样");
    s.ref = linker.AllocString(u"ü");
    CHECK(CallStatic(state, uri_cls, "encode", {"Ljava/lang/String;"}, "Ljava/lang/String;", &s, 1,
                     &r) &&
              ResultUtf8(r) == "%C3%BC",
          "Uri.encode 非 ASCII UTF-8 转义");
    s.ref = nullptr;
    CHECK(CallStatic(state, uri_cls, "decode", {"Ljava/lang/String;"}, "Ljava/lang/String;", &s, 1,
                     &r) &&
              r.ref == nullptr,
          "Uri.decode(null) == null");

    // fromParts / withAppendedPath / normalizeScheme
    Value args[3] = {};
    args[0].ref = linker.AllocString(u"content");
    args[1].ref = linker.AllocString(u"//auth/p");
    args[2].ref = linker.AllocString(u"f");
    Value r2{};
    CHECK(CallStatic(state, uri_cls, "fromParts",
                     {"Ljava/lang/String;", "Ljava/lang/String;", "Ljava/lang/String;"},
                     "Landroid/net/Uri;", args, 3, &r2) &&
              r2.ref != nullptr &&
              ObjToString(state, reinterpret_cast<VObject*>(r2.ref)) == "content://auth/p#f",
          "Uri.fromParts 拼串");
    VObject* base = ParseUri(state, linker, "content://auth/a");
    Value wargs[2] = {};
    wargs[0].ref = base;
    wargs[1].ref = linker.AllocString(u"b c");
    Value wr{};
    CHECK(CallStatic(state, uri_cls, "withAppendedPath",
                     {"Landroid/net/Uri;", "Ljava/lang/String;"}, "Landroid/net/Uri;", wargs, 2,
                     &wr) &&
              wr.ref != nullptr &&
              ObjToString(state, reinterpret_cast<VObject*>(wr.ref)) == "content://auth/a/b%20c",
          "Uri.withAppendedPath 编码拼接");
    VObject* up = ParseUri(state, linker, "HTTP://X/y");
    Value nr{};
    CHECK(CallVirtual(state, up, "normalizeScheme", {}, "Landroid/net/Uri;", nullptr, 0, &nr) &&
              nr.ref != nullptr,
          "normalizeScheme call");
    if (nr.ref != nullptr) {
      CHECK(UriStrIs(state, reinterpret_cast<VObject*>(nr.ref), "getScheme", "http"),
            "normalizeScheme 小写 scheme");
    }

    // equals/hashCode（toString 口径）
    VObject* e1 = ParseUri(state, linker, "content://a/b");
    VObject* e2 = ParseUri(state, linker, "content://a/b");
    VObject* e3 = ParseUri(state, linker, "content://a/c");
    Value ev{};
    ev.ref = e2;
    Value bv{};
    CHECK(CallVirtual(state, e1, "equals", {"Ljava/lang/Object;"}, "Z", &ev, 1, &bv) && bv.l != 0,
          "Uri.equals(同串) == true");
    ev.ref = e3;
    CHECK(CallVirtual(state, e1, "equals", {"Ljava/lang/Object;"}, "Z", &ev, 1, &bv) && bv.l == 0,
          "Uri.equals(异串) == false");
    Value h1{};
    Value h2{};
    CallVirtual(state, e1, "hashCode", {}, "I", nullptr, 0, &h1);
    CallVirtual(state, e2, "hashCode", {}, "I", nullptr, 0, &h2);
    CHECK(h1.l == h2.l, "Uri.hashCode 同串同值");

    // Uri.EMPTY 静态面
    VField* empty_f = FindField(uri_cls, "EMPTY");
    CHECK(empty_f != nullptr && empty_f->is_static, "Uri.EMPTY 静态字段");
    if (empty_f != nullptr && empty_f->offset < uri_cls->static_storage.size()) {
      ObjHeader* eo = uri_cls->static_storage[empty_f->offset].ref;
      CHECK(eo != nullptr, "Uri.EMPTY 非空");
      if (eo != nullptr) {
        Value t{};
        CallVirtual(state, reinterpret_cast<VObject*>(eo), "toString", {}, "Ljava/lang/String;",
                    nullptr, 0, &t);
        CHECK(ResultUtf8(t) == "", "Uri.EMPTY.toString() == ''");
      }
    }
  }

  // ---- 3) UriMatcher lib 面（runtime.dex boot 消费链）----
  {
    VObject* m = NewMatcher(state, linker, -1);
    CHECK(m != nullptr, "new UriMatcher(NO_MATCH)");
    if (m != nullptr) {
      MatcherAdd(state, m, "auth", "a/b", 1);
      MatcherAdd(state, m, "auth", "a/*", 2);
      MatcherAdd(state, m, "auth", "a/#", 3);
      MatcherAdd(state, m, "auth", "special", 4);
      MatcherAdd(state, m, "auth", "*", 5);
      MatcherAdd(state, m, "*", "z", 6);
      MatcherAdd(state, m, "auth2", "*/x", 7);
      MatcherAdd(state, m, "auth2", "a/y", 8);
      MatcherAdd(state, m, "auth3", "t/", 9);

      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/a/b")) == 1,
            "match 字面优先（a/b -> 1）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/a/9")) == 3,
            "match '#' 优于 '*'（a/9 -> 3）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/a/x")) == 2,
            "match '*' 兜底（a/x -> 2）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/special")) == 4,
            "match 字面优于通配（special -> 4 非 5）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/other")) == 5,
            "match '*' 单段（other -> 5）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://other/z")) == 6,
            "match '*' authority（other/z -> 6）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth2/a/x")) == 7,
            "match 回溯（exact 子树不中回落 '*' -> 7）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth2/a/y")) == 8,
            "match 回溯特异（a/y -> 8）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth3/t")) == 9,
            "match 尾 '/' 容错（t/ -> 9）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/a")) == 5,
            "match 'auth/*' 覆盖单段 a -> 5");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth2/a")) == -1,
            "match 长规则不压短 URI（auth2/a -> NO_MATCH）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth/a/b/c")) == -1,
            "match 全段耗尽（a/b/c -> NO_MATCH）");
      CHECK(MatcherMatch(state, m, ParseUri(state, linker, "content://auth")) == -1,
            "match 无规则节点 -> NO_MATCH");

      // 负 code IAE（AOSP 文案）
      Value args[3] = {};
      args[0].ref = state.linker->AllocString(u"auth");
      args[1].ref = state.linker->AllocString(u"neg");
      args[2].i = -2;
      bool ok = CallVirtual(state, m, "addURI",
                            {"Ljava/lang/String;", "Ljava/lang/String;", "I"}, "V", args, 3,
                            nullptr);
      CHECK(!ok && state.HasPendingException() &&
                PendingDetail(state) == "Ljava/lang/IllegalArgumentException;: code -2 is negative: -2",
            "addURI 负 code -> IAE AOSP 文案");
      state.ClearException();
    }
  }

  // ---- 4) ContentResolver 路由（fixture 生命周期 + 注册表分发）----
  {
    LifecycleConfig cfg = ConfigFromManifest(app_session->manifest());
    CHECK(cfg.providers.size() == 2, "fixture provider count == 2");
    LifecycleDriver driver(state, app_loader);
    bool ran = driver.Run(cfg);
    CHECK(ran, "lifecycle run completes");
    if (!ran) std::printf("  error: %s\n", driver.error().c_str());
    CHECK(ReadStaticString(linker.FindClass("Luritest/UriProbeProvider;"), "log") ==
              "alpha.onCreate\n",
          "alpha.onCreate 跑（lib UriMatcher 登记链）");
    CHECK(ReadStaticString(linker.FindClass("Luritest/BetaProvider;"), "log") == "beta.onCreate\n",
          "beta.onCreate 跑");

    // getContentResolver 单例（Context 钩子）
    VObject* ctxw = linker.AllocObject(linker.FindClass("Landroid/content/ContextWrapper;"));
    Value cr_v{};
    CHECK(CallVirtual(state, ctxw, "getContentResolver", {}, "Landroid/content/ContentResolver;",
                      nullptr, 0, &cr_v) &&
              cr_v.ref != nullptr,
          "Context.getContentResolver 非空");
    VObject* resolver = reinterpret_cast<VObject*>(cr_v.ref);
    if (resolver == nullptr) {
      return 1;
    }

    // query 路由（alpha；UriMatcher.match 面经 fixture 记录）
    {
      Value args[5] = {};
      args[0].ref = ParseUri(state, linker, "content://com.dexvm.uri.alpha/items/7");
      Value r{};
      CHECK(CallVirtual(state, resolver, "query",
                        {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                         "[Ljava/lang/String;", "Ljava/lang/String;"},
                        "Landroid/database/Cursor;", args, 5, &r) &&
                r.ref != nullptr,
            "resolver.query -> provider.query");
      std::string log = ReadStaticString(linker.FindClass("Luritest/UriProbeProvider;"), "log");
      CHECK(log.find("alpha.query:content://com.dexvm.uri.alpha/items/7:m=2") != std::string::npos,
            "query 路由到 alpha + UriMatcher match==2");
      VObject* cursor = reinterpret_cast<VObject*>(r.ref);
      if (cursor != nullptr) {
        Value bv{};
        CHECK(CallVirtual(state, cursor, "moveToFirst", {}, "Z", nullptr, 0, &bv) && bv.l != 0,
              "cursor.moveToFirst");
        Value zero{};
        zero.i = 0;
        Value lv{};
        CHECK(CallVirtual(state, cursor, "getLong", {"I"}, "J", &zero, 1, &lv) && lv.l == 2,
              "cursor.getLong(0) == match 码");
        Value idx{};
        idx.i = 1;
        Value sv{};
        CHECK(CallVirtual(state, cursor, "getString", {"I"}, "Ljava/lang/String;", &idx, 1, &sv) &&
                  ResultUtf8(sv) == "row-2",
              "cursor.getString(1) == row-2");
      }
    }

    // insert/update/delete/getType 路由 + ContentValues lib 读回
    {
      VObject* values = linker.AllocObject(linker.FindClass("Landroid/content/ContentValues;"));
      CHECK(values != nullptr && CallVirtual(state, values, "<init>", {}, "V", nullptr, 0, nullptr),
            "new ContentValues（lib POJO）");
      Value kv[2] = {};
      kv[0].ref = linker.AllocString(u"name");
      kv[1].ref = linker.AllocString(u"v1");
      CHECK(CallVirtual(state, values, "put", {"Ljava/lang/String;", "Ljava/lang/String;"}, "V", kv,
                        2, nullptr),
            "ContentValues.put");

      Value args[2] = {};
      args[0].ref = ParseUri(state, linker, "content://com.dexvm.uri.alpha/items");
      args[1].ref = values;
      Value r{};
      CHECK(CallVirtual(state, resolver, "insert",
                        {"Landroid/net/Uri;", "Landroid/content/ContentValues;"},
                        "Landroid/net/Uri;", args, 2, &r) &&
                r.ref != nullptr,
            "resolver.insert -> provider.insert");
      CHECK(ObjToString(state, reinterpret_cast<VObject*>(r.ref)) ==
                "content://com.dexvm.uri.alpha/items/42",
            "insert 返回 withAppendedPath 面");
      std::string log = ReadStaticString(linker.FindClass("Luritest/UriProbeProvider;"), "log");
      CHECK(log.find("alpha.insert:content://com.dexvm.uri.alpha/items:name=v1") !=
                std::string::npos,
            "insert 路由 + ContentValues.getAsString 读回");

      Value uargs[4] = {};
      uargs[0].ref = args[0].ref;
      uargs[1].ref = values;
      Value ir{};
      CHECK(CallVirtual(state, resolver, "update",
                        {"Landroid/net/Uri;", "Landroid/content/ContentValues;",
                         "Ljava/lang/String;", "[Ljava/lang/String;"},
                        "I", uargs, 4, &ir) &&
                ir.l == 1,
            "resolver.update -> provider.update(1)");

      Value dargs[3] = {};
      dargs[0].ref = args[0].ref;
      Value dr{};
      CHECK(CallVirtual(state, resolver, "delete",
                        {"Landroid/net/Uri;", "Ljava/lang/String;", "[Ljava/lang/String;"},
                        "I", dargs, 3, &dr) &&
                dr.l == 2,
            "resolver.delete -> provider.delete(2)");

      Value gargs[1] = {};
      gargs[0].ref = args[0].ref;
      Value gr{};
      CHECK(CallVirtual(state, resolver, "getType", {"Landroid/net/Uri;"}, "Ljava/lang/String;",
                        gargs, 1, &gr) &&
                ResultUtf8(gr) == "vnd.dexvm/alpha",
            "resolver.getType -> provider.getType");

      // beta/gamma 分号多值 authority 逐键分发
      Value bargs[5] = {};
      bargs[0].ref = ParseUri(state, linker, "content://com.dexvm.uri.gamma/x");
      Value br{};
      CHECK(CallVirtual(state, resolver, "query",
                        {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                         "[Ljava/lang/String;", "Ljava/lang/String;"},
                        "Landroid/database/Cursor;", bargs, 5, &br) &&
                br.ref != nullptr,
            "gamma 键分发到 BetaProvider");
      std::string blog = ReadStaticString(linker.FindClass("Luritest/BetaProvider;"), "log");
      CHECK(blog.find("beta.query:content://com.dexvm.uri.gamma/x") != std::string::npos,
            "分号多值 authority 逐键注册（gamma -> beta）");
      CHECK(blog.find("alpha") == std::string::npos, "beta 日志无 alpha 串（分发不串台）");
    }

    // bulkInsert（逐条 insert 计数）
    {
      VClass* cv_cls = linker.FindClass("Landroid/content/ContentValues;");
      VClass* cv_arr = linker.FindArrayClass(cv_cls);
      dexvm::runtime::VArray* arr = linker.AllocArray(cv_arr, 2);
      for (int i = 0; i < 2; i++) {
        VObject* v = linker.AllocObject(cv_cls);
        CallVirtual(state, v, "<init>", {}, "V", nullptr, 0, nullptr);
        Value kv[2] = {};
        kv[0].ref = linker.AllocString(u"name");
        kv[1].ref = linker.AllocString(u"bulk");
        CallVirtual(state, v, "put", {"Ljava/lang/String;", "Ljava/lang/String;"}, "V", kv, 2,
                    nullptr);
        ArrSetRef(arr, i, reinterpret_cast<ObjHeader*>(v));
      }
      Value args[2] = {};
      args[0].ref = ParseUri(state, linker, "content://com.dexvm.uri.alpha/items");
      args[1].ref = arr;
      Value r{};
      CHECK(CallVirtual(state, resolver, "bulkInsert",
                        {"Landroid/net/Uri;", "[Landroid/content/ContentValues;"}, "I", args, 2,
                        &r) &&
                r.l == 2,
            "resolver.bulkInsert 计数 == 2");
    }

    // call 路由（provider.call 具体默认 null，AOSP 同值）
    {
      Value args[4] = {};
      args[0].ref = ParseUri(state, linker, "content://com.dexvm.uri.alpha/items");
      args[1].ref = linker.AllocString(u"method");
      args[2].ref = linker.AllocString(u"arg");
      Value r{};
      CHECK(CallVirtual(state, resolver, "call",
                        {"Landroid/net/Uri;", "Ljava/lang/String;", "Ljava/lang/String;",
                         "Landroid/os/Bundle;"},
                        "Landroid/os/Bundle;", args, 4, &r) &&
                r.ref == nullptr,
            "resolver.call -> provider.call 默认 null");
    }

    // 未注册 authority：query 安静返回 null 游标、不挂异常（厂商 referrer
    // 探测链在非华为设备上的兼容口径；AOSP 抛面会打断账户/设备信息链）。
    {
      Value args[5] = {};
      args[0].ref = ParseUri(state, linker, "content://com.dexvm.uri.missing/x");
      Value r{};
      bool ok = CallVirtual(state, resolver, "query",
                            {"Landroid/net/Uri;", "[Ljava/lang/String;", "Ljava/lang/String;",
                             "[Ljava/lang/String;", "Ljava/lang/String;"},
                            "Landroid/database/Cursor;", args, 5, &r);
      CHECK(ok && !state.HasPendingException() && r.ref == nullptr,
            "未注册 authority query -> null 游标且无异常");
    }
  }

  if (g_failures == 0) {
    std::printf("ALL PASS\n");
    return 0;
  }
  std::printf("%d FAILURES\n", g_failures);
  return 1;
}
