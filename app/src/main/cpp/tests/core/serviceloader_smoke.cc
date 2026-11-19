// java.util.ServiceLoader 冒烟（M5，docs 14 章 30；P0 双实现消除后走 runtime.dex
// lib 实现）：
//   0. lib 版生效验收（P0 翻转：内建注册删除类全部命中 dex 版；iterator() 走
//      dex 实现；String instanceof CharSequence/Comparable/Serializable；原子类
//      CAS 面；TerminatingThreadLocal extends ThreadLocal）；
//   META-INF/services 配置解析（注释/空行/多实现/顺序/去重）、
//   iterator()/findFirst() 懒加载（按需实例化，缓存复用）、
//   ServiceConfigurationError 三态（类不存在/非子类型/不可实例化）、
//   ClassLoader.getResource(s) 面（lib URL 字段布局 / lib Enumeration）、
//   多资源聚合顺序（双亲链 parent 先、自身多 zip 按序、zip 内 entry 序）。
// 手工构造 stored zip 资源 + RegisterBuiltinClass 测试类（ctor intrinsic
// 计数）+ 直接 InvokeMethod 驱动（同 exam_smoke 探针手法）；boot = runtime.dex。
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/intrinsic.h"
#include "core/interpreter.h"
#include "dex/dex_file.h"
#include "session/class_loader.h"
#include "session/session.h"

namespace {

using dexvm::runtime::ClassLinker;
using dexvm::runtime::InternTable;
using dexvm::runtime::InvokeMethod;
using dexvm::runtime::ObjHeader;
using dexvm::runtime::ToIntrinsicSlot;
using dexvm::runtime::Value;
using dexvm::runtime::VArray;
using dexvm::runtime::VClass;
using dexvm::runtime::VField;
using dexvm::runtime::VMethod;
using dexvm::runtime::VObject;
using dexvm::runtime::VMState;
using dexvm::runtime::VString;

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

// ---- 实例化计数（懒加载观测）----
int g_impl_a = 0;
int g_impl_b = 0;

Value CtorCounterA(VMState&, VObject*, const Value*, size_t, bool* ok) {
  g_impl_a++;
  *ok = true;
  return Value{};
}
Value CtorCounterB(VMState&, VObject*, const Value*, size_t, bool* ok) {
  g_impl_b++;
  *ok = true;
  return Value{};
}

// ---- stored zip 写入（本地头 + 中央目录 + EOCD；CRC 置 0——读取端不校验）----
void W16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>(v));
  b.push_back(static_cast<uint8_t>(v >> 8));
}
void W32(std::vector<uint8_t>& b, uint32_t v) {
  for (int i = 0; i < 4; i++) b.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

bool WriteZip(const std::string& path,
              const std::vector<std::pair<std::string, std::string>>& entries) {
  std::vector<uint8_t> b;
  std::vector<uint32_t> local_offs;
  std::vector<uint32_t> cd_entry_offs;
  for (const auto& e : entries) {
    local_offs.push_back(static_cast<uint32_t>(b.size()));
    W32(b, 0x04034b50);
    W16(b, 20);
    W16(b, 0);
    W16(b, 0);  // stored
    W16(b, 0);
    W16(b, 0);
    W32(b, 0);  // crc
    W32(b, static_cast<uint32_t>(e.second.size()));
    W32(b, static_cast<uint32_t>(e.second.size()));
    W16(b, static_cast<uint16_t>(e.first.size()));
    W16(b, 0);
    for (char c : e.first) b.push_back(static_cast<uint8_t>(c));
    for (char c : e.second) b.push_back(static_cast<uint8_t>(c));
  }
  uint32_t cd_off = static_cast<uint32_t>(b.size());
  for (size_t i = 0; i < entries.size(); i++) {
    cd_entry_offs.push_back(static_cast<uint32_t>(b.size()));
    W32(b, 0x02014b50);
    W16(b, 20);
    W16(b, 20);
    W16(b, 0);
    W16(b, 0);
    W16(b, 0);
    W16(b, 0);
    W32(b, 0);
    W32(b, static_cast<uint32_t>(entries[i].second.size()));
    W32(b, static_cast<uint32_t>(entries[i].second.size()));
    W16(b, static_cast<uint16_t>(entries[i].first.size()));
    W16(b, 0);
    W16(b, 0);
    W16(b, 0);
    W16(b, 0);
    W32(b, 0);
    W32(b, local_offs[i]);
    for (char c : entries[i].first) b.push_back(static_cast<uint8_t>(c));
  }
  uint32_t cd_size = static_cast<uint32_t>(b.size()) - cd_off;
  W32(b, 0x06054b50);
  W16(b, 0);
  W16(b, 0);
  W16(b, static_cast<uint16_t>(entries.size()));
  W16(b, static_cast<uint16_t>(entries.size()));
  W32(b, cd_size);
  W32(b, cd_off);
  W16(b, 0);

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
  return f.good();
}

// ---- 测试类工厂（RegisterBuiltinClass；ctor intrinsic 计数）----

constexpr uint32_t kPub = 0x1;
constexpr uint32_t kStatic = 0x8;
constexpr uint32_t kIface = 0x200;
constexpr uint32_t kAbstract = 0x400;
constexpr uint32_t kCtor = 0x10000;

VClass* MakeTestClass(ClassLinker& linker, VClass* class_class, const char* desc, VClass* super,
                      uint32_t access, uint32_t inst_bytes) {
  void* mem = linker.heap()->Alloc(sizeof(VClass));
  if (mem == nullptr) return nullptr;
  VClass* k = new (mem) VClass();
  k->descriptor = desc;
  k->super_class = super;
  k->access_flags = access;
  k->object_size = static_cast<uint32_t>(sizeof(ObjHeader)) + inst_bytes;
  k->klass = class_class;
  k->status = dexvm::runtime::ClassStatus::kInitialized;
  linker.RegisterBuiltinClass(k);
  return k;
}

VMethod* AddCtorIntrinsic(VClass* k, dexvm::runtime::IntrinsicFn fn) {
  VMethod m;
  m.declaring = k;
  m.name = "<init>";
  m.return_type = "V";
  m.shorty = "V";
  m.access_flags = kPub | kCtor;
  m.native_fn = ToIntrinsicSlot(fn);
  k->direct_methods.push_back(m);
  return &k->direct_methods.back();
}

// 只声明带参构造（无 public 无参构造 → 不可实例化态）
void AddArgCtorOnly(VClass* k) {
  VMethod m;
  m.declaring = k;
  m.name = "<init>";
  m.param_types = {"I"};
  m.return_type = "V";
  m.shorty = "VI";
  m.access_flags = kPub | kCtor;
  k->direct_methods.push_back(m);
}

// ---- 查找/调用辅助 ----

VMethod* FindMethod(VClass* klass, const char* name, const std::vector<std::string>& params,
                    const char* ret) {
  if (klass == nullptr) return nullptr;
  for (auto& m : klass->direct_methods) {
    if (m.name == name && m.param_types == params && m.return_type == ret) return &m;
  }
  for (auto& m : klass->virtual_methods) {
    if (m.name == name && m.param_types == params && m.return_type == ret) return &m;
  }
  return nullptr;
}

Value Call(VMState& state, VClass* klass, const char* name, const std::vector<std::string>& params,
           const char* ret, std::initializer_list<Value> args, bool* ok) {
  VMethod* m = FindMethod(klass, name, params, ret);
  if (m == nullptr) {
    *ok = false;
    return Value{};
  }
  std::vector<Value> a(args);
  Value r{};
  *ok = InvokeMethod(state, m, a.empty() ? nullptr : a.data(), a.size(), &r);
  return r;
}

Value RefVal(void* p) {
  Value v{};
  v.ref = reinterpret_cast<ObjHeader*>(p);
  return v;
}

std::string StrOf(ObjHeader* o) {
  return o != nullptr ? static_cast<VString*>(o)->ToModifiedUtf8() : std::string("<null>");
}

// 实例的类 descriptor（Ltest/ImplA; 等）
std::string DescOf(ObjHeader* o) {
  return (o != nullptr && o->klass != nullptr) ? o->klass->descriptor : std::string("<null>");
}

// 失败诊断：pending exception 的类 + detailMessage（load/next 链路排障）
void DumpPending(VMState& state) {
  ObjHeader* ex = state.pending_exception;
  if (ex == nullptr) return;
  VClass* k = reinterpret_cast<VObject*>(ex)->klass;
  std::string msg;
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& f : c->instance_fields) {
      if (f.name == "detailMessage") {
        ObjHeader* v = nullptr;
        std::memcpy(&v, reinterpret_cast<VObject*>(ex)->field_data() + f.offset, sizeof(v));
        msg = StrOf(v);
        break;
      }
    }
    if (!msg.empty()) break;
  }
  std::printf("  [diag] pending: %s: %s\n", DescOf(ex).c_str(), msg.c_str());
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("== serviceloader smoke (M5, P0 翻转后走 runtime.dex lib 实现) ==\n");
  const std::string tmp = "/tmp/dexvm_sl_smoke";
  const std::string runtime_dex =
      argc > 1 ? argv[1] : std::string("runtime-libs/build/runtime-dex/runtime.dex");

  dexvm::mem::Heap heap;
  InternTable intern;
  ClassLinker linker(&heap, &intern);
  dexvm::runtime::RegisterBuiltins(&linker);
  VMState state;
  state.heap = &heap;
  state.intern = &intern;
  state.linker = &linker;

  // ---- boot classpath：runtime.dex（lib ServiceLoader/URL/Optional/Enumeration 等）----
  std::vector<std::unique_ptr<dexvm::dex::DexFile>> owned_dex;
  {
    std::string err;
    auto boot = dexvm::session::Session::Open(runtime_dex, &err);
    CHECK(boot != nullptr && !boot->dex_blobs().empty(), "open runtime.dex");
    if (boot == nullptr) {
      std::printf("  error: %s\n", err.c_str());
      return 1;
    }
    for (const auto& blob : boot->dex_blobs()) {
      std::string derr;
      auto dex_file = dexvm::dex::DexFile::Open(blob.bytes.data(), blob.bytes.size(), &derr);
      CHECK(dex_file != nullptr, ("parse boot dex: " + blob.name).c_str());
      linker.AddBootDexFile(dex_file.get());
      owned_dex.push_back(std::move(dex_file));
    }
    std::printf("boot dex files: %zu (runtime.dex)\n", boot->dex_blobs().size());
  }

  // ================= 0. lib 版生效（P0 翻转验收：内建注册删除后命中 dex 版）=================
  {
    struct {
      const char* desc;
    } kFlipped[] = {
        {"Ljava/util/ServiceLoader;"},
        {"Ljava/util/ServiceConfigurationError;"},
        {"Ljava/util/NoSuchElementException;"},
        {"Ljava/net/URL;"},
        {"Ljava/lang/CompoundEnumeration;"},
        {"Ljava/lang/CharSequence;"},
        {"Ljava/lang/Comparable;"},
        {"Ljava/lang/Iterable;"},
        {"Ljava/lang/AutoCloseable;"},
        {"Ljava/lang/Cloneable;"},
        {"Ljava/io/Serializable;"},
        {"Ljava/lang/annotation/Annotation;"},
        {"Ljava/util/function/IntConsumer;"},
        {"Ljava/util/function/ToIntFunction;"},
        {"Ljava/util/concurrent/atomic/AtomicBoolean;"},
        {"Ljava/util/concurrent/atomic/AtomicLong;"},
        {"Ljava/util/concurrent/atomic/AtomicReference;"},
        {"Ljava/util/concurrent/atomic/AtomicReferenceArray;"},
        {"Ljava/nio/ByteBuffer;"},
        {"Ljdk/internal/misc/TerminatingThreadLocal;"},
    };
    for (const auto& f : kFlipped) {
      VClass* k = linker.FindClass(f.desc);
      CHECK(k != nullptr && k->dex_file != nullptr,
            (std::string("lib 版生效: ") + f.desc + " 由 runtime.dex 提供").c_str());
    }
    // 内建 ServiceLoader$LazyIterator 不复存在（lib 为 JDK9 形态内部类）
    VClass* li = linker.FindClass("Ljava/util/ServiceLoader$LazyIterator;");
    CHECK(li == nullptr || li->dex_file != nullptr,
          "LazyIterator 内建已删（lib 形态无此类/或 dex 版）");
    // ServiceLoader.iterator() 走 dex 实现（非 intrinsic）
    {
      VClass* sl = linker.FindClass("Ljava/util/ServiceLoader;");
      VMethod probe;
      probe.name = "iterator";
      probe.return_type = "Ljava/util/Iterator;";
      VMethod* it_m = sl != nullptr ? sl->FindInterfaceImpl(&probe) : nullptr;
      CHECK(it_m != nullptr && it_m->code != nullptr && it_m->dex_file != nullptr,
            "ServiceLoader.iterator() 走 dex 实现（code != null、非 intrinsic）");
    }
    // String instanceof CharSequence/Comparable/Serializable（接口向量延迟补挂）
    {
      VClass* str = linker.FindClass("Ljava/lang/String;");
      VClass* cs = linker.FindClass("Ljava/lang/CharSequence;");
      VClass* cmp = linker.FindClass("Ljava/lang/Comparable;");
      VClass* ser = linker.FindClass("Ljava/io/Serializable;");
      CHECK(cs != nullptr && cs->IsAssignableFrom(str), "String instanceof CharSequence");
      CHECK(cmp != nullptr && cmp->IsAssignableFrom(str), "String instanceof Comparable");
      CHECK(ser != nullptr && ser->IsAssignableFrom(str), "String instanceof Serializable");
    }
    // Serializable cast 成功（check-cast 语义面：IsAssignableFrom 链）
    {
      VClass* ser = linker.FindClass("Ljava/io/Serializable;");
      VString* s = linker.AllocString(u"x");
      CHECK(ser != nullptr && s != nullptr && ser->IsAssignableFrom(s->klass),
            "Serializable cast String 成功");
    }
    // TerminatingThreadLocal extends ThreadLocal（lib 链对齐 OpenJDK）
    {
      VClass* ttl = linker.FindClass("Ljdk/internal/misc/TerminatingThreadLocal;");
      VClass* tl = linker.FindClass("Ljava/lang/ThreadLocal;");
      CHECK(ttl != nullptr && tl != nullptr && ttl->super_class == tl,
            "TerminatingThreadLocal extends ThreadLocal");
    }
    // 原子类 lib 版可用（AtomicReference CAS 面）
    {
      bool ok = false;
      VClass* ar = linker.FindClass("Ljava/util/concurrent/atomic/AtomicReference;");
      VObject* o = linker.AllocObject(ar);
      Value init_arg{};
      init_arg.ref = reinterpret_cast<ObjHeader*>(linker.AllocString(u"v0"));
      Value r = Call(state, ar, "<init>", {"Ljava/lang/Object;"}, "V", {RefVal(o), init_arg}, &ok);
      CHECK(ok && o != nullptr, "lib AtomicReference.<init>(Object)");
      Value cas_r =
          Call(state, ar, "compareAndSet", {"Ljava/lang/Object;", "Ljava/lang/Object;"}, "Z",
               {RefVal(o), init_arg, RefVal(linker.AllocString(u"v1"))}, &ok);
      CHECK(ok && cas_r.i == 1, "lib AtomicReference.compareAndSet 翻转成功");
      Value g = Call(state, ar, "get", {}, "Ljava/lang/Object;", {RefVal(o)}, &ok);
      CHECK(ok && g.ref != nullptr && StrOf(g.ref) == "v1", "lib AtomicReference.get 读新值");
    }
  }

  // ---- 资源 zip（stored）----
  // zipA：注释/空行/多实现/顺序/去重；zipB：聚合序第二源；zipC：跨 loader 聚合
  CHECK(WriteZip(tmp + "_a.zip",
                 {{"META-INF/services/test.Svc",
                   "# 首行注释\n"
                   "\n"
                   "  test.ImplB   \n"
                   "test.ImplA # 行尾注释\n"
                   "\ttest.ImplB\n"
                   "# 尾注释\n"}}),
        "write zipA (parse fixture)");
  CHECK(WriteZip(tmp + "_b.zip",
                 {{"META-INF/services/test.Svc", "test.ImplA\n"},
                  {"META-INF/services/other.Name", "test.ImplB\n"}}),
        "write zipB (aggregate fixture)");
  CHECK(WriteZip(tmp + "_c.zip", {{"META-INF/services/test.Svc", "test.ImplB\n"}}),
        "write zipC (chain fixture)");
  {
    std::vector<dexvm::session::ResourceHit> hits;
    std::string err;
    CHECK(dexvm::session::ReadZipResources(tmp + "_a.zip", "META-INF/services/test.Svc", &hits,
                                           &err) &&
              hits.size() == 1,
          "ReadZipResources zipA: one hit");
    CHECK(hits.size() == 1 &&
              std::string(reinterpret_cast<const char*>(hits[0].bytes.data()),
                          hits[0].bytes.size()).find("test.ImplB") != std::string::npos,
          "ReadZipResources zipA: content intact");
    CHECK(hits.size() == 1 && hits[0].url.find("!/META-INF/services/test.Svc") != std::string::npos,
          "ReadZipResources zipA: url form jar:file:...!/entry");
  }

  // ---- 测试类（builtin 登记；ctor intrinsic 计数）----
  VClass* class_class = linker.FindClass("Ljava/lang/Class;");
  VClass* object = linker.FindClass("Ljava/lang/Object;");
  VClass* svc = MakeTestClass(linker, class_class, "Ltest/Svc;", object,
                              kPub | kIface | kAbstract, 0);
  VClass* impl_a = MakeTestClass(linker, class_class, "Ltest/ImplA;", object, kPub, 0);
  VClass* impl_b = MakeTestClass(linker, class_class, "Ltest/ImplB;", object, kPub, 0);
  VClass* not_sub = MakeTestClass(linker, class_class, "Ltest/NotSubtype;", object, kPub, 0);
  VClass* no_ctor = MakeTestClass(linker, class_class, "Ltest/NoCtor;", object, kPub, 0);
  VClass* abstr = MakeTestClass(linker, class_class, "Ltest/AbstractImpl;", object,
                                kPub | kAbstract, 0);
  impl_a->interfaces.push_back(svc);
  impl_b->interfaces.push_back(svc);
  no_ctor->interfaces.push_back(svc);
  abstr->interfaces.push_back(svc);
  AddCtorIntrinsic(impl_a, CtorCounterA);
  AddCtorIntrinsic(impl_b, CtorCounterB);
  AddCtorIntrinsic(not_sub, CtorCounterA);
  AddArgCtorOnly(no_ctor);
  CHECK(svc != nullptr && impl_a != nullptr && impl_b != nullptr, "test classes registered");

  // java.util.Iterator / Enumeration / Optional 由 runtime.dex 提供（P0 翻转：
  // 不再建桩——曾建桩即双实现遮蔽；类定义/接口向量全部命中 lib 版）。

  // ---- Loader 装配：parent(zipA) → child(zipB)；独立 loader 用 zipC ----
  auto& reg = linker.loaders();
  dexvm::session::LoaderState* parent_st = reg.CreateLoader(reg.boot(), "");
  parent_st->resource_zips.push_back(tmp + "_a.zip");
  dexvm::session::LoaderState* child_st = reg.CreateLoader(parent_st, "");
  child_st->resource_zips.push_back(tmp + "_b.zip");
  dexvm::session::LoaderState* solo_st = reg.CreateLoader(reg.boot(), "");
  solo_st->resource_zips.push_back(tmp + "_c.zip");

  VClass* cl_cls = linker.FindClass("Ljava/lang/ClassLoader;");
  VObject* parent_cl = linker.AllocObject(cl_cls);
  VObject* child_cl = linker.AllocObject(cl_cls);
  VObject* solo_cl = linker.AllocObject(cl_cls);
  reg.BindObject(parent_cl, parent_st);
  reg.BindObject(child_cl, child_st);
  reg.BindObject(solo_cl, solo_st);
  CHECK(parent_cl != nullptr && child_cl != nullptr && solo_cl != nullptr, "loader objects bound");

  // ================= 1. services 解析（注释/空行/多实现/顺序/去重）=================
  {
    bool ok = false;
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(parent_cl)}, &ok);
    if (!ok) DumpPending(state);
    CHECK(ok && sl.ref != nullptr, "load(Svc, parent) -> ServiceLoader");
    Value it = Call(state, sl.ref != nullptr ? sl.ref->klass : nullptr, "iterator", {},
                    "Ljava/util/Iterator;", {sl}, &ok);
    if (!ok) DumpPending(state);
    CHECK(ok && it.ref != nullptr, "iterator()");
    // zipA 内容 = [ImplB, ImplA]（去重保首现、注释/空行裁剪、顺序保留）
    Value n1 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    CHECK(ok && n1.ref != nullptr && DescOf(n1.ref) == "Ltest/ImplB;", "parse order: ImplB first");
    Value n2 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    CHECK(ok && n2.ref != nullptr && DescOf(n2.ref) == "Ltest/ImplA;", "parse order: ImplA second");
    Value more = Call(state, it.ref->klass, "hasNext", {}, "Z", {it}, &ok);
    CHECK(ok && more.i == 0, "no third provider (dedup ImplB)");
    CHECK(DescOf(n1.ref) == "Ltest/ImplB;", "instance class = ImplB");
  }

  // ================= 2. 懒加载（iterator/next 按需实例化）=================
  {
    g_impl_a = g_impl_b = 0;
    bool ok = false;
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(parent_cl)}, &ok);
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    CHECK(ok, "lazy: load+iterator ok");
    CHECK(g_impl_a == 0 && g_impl_b == 0, "lazy: nothing instantiated at iterator()");
    Value h = Call(state, it.ref->klass, "hasNext", {}, "Z", {it}, &ok);
    CHECK(ok && h.i == 1 && g_impl_a == 0 && g_impl_b == 0, "lazy: hasNext does not instantiate");
    Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    CHECK(ok && g_impl_b == 1 && g_impl_a == 0, "lazy: next() instantiates only ImplB");
    Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    CHECK(ok && g_impl_b == 1 && g_impl_a == 1, "lazy: second next() instantiates ImplA");
    // 缓存复用：新迭代器不重复实例化
    Value it2 = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Call(state, it2.ref->klass, "next", {}, "Ljava/lang/Object;", {it2}, &ok);
    CHECK(ok && g_impl_b == 1 && g_impl_a == 1, "lazy: cache reused across iterators");
    // next() 耗尽 → NoSuchElementException
    Call(state, it2.ref->klass, "next", {}, "Ljava/lang/Object;", {it2}, &ok);
    Call(state, it2.ref->klass, "next", {}, "Ljava/lang/Object;", {it2}, &ok);  // 耗尽
    CHECK(!ok && state.HasPendingException() &&
              reinterpret_cast<VObject*>(state.pending_exception)->klass->descriptor ==
                  "Ljava/util/NoSuchElementException;",
          "exhausted next() -> NoSuchElementException");
    state.ClearException();
  }

  // ================= 2b. findFirst 懒加载（只实例化首个）=================
  {
    g_impl_a = g_impl_b = 0;
    bool ok = false;
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(parent_cl)}, &ok);
    Value f = Call(state, sl.ref->klass, "findFirst", {}, "Ljava/util/Optional;", {sl}, &ok);
    CHECK(ok && f.ref != nullptr, "findFirst returns Optional");
    // zipA 首个 = ImplB；只实例化它
    CHECK(g_impl_b == 1 && g_impl_a == 0, "findFirst lazy: only first provider instantiated");
    ObjHeader* v = nullptr;
    std::memcpy(&v, reinterpret_cast<VObject*>(f.ref)->field_data(), sizeof(v));
    CHECK(v != nullptr && DescOf(v) == "Ltest/ImplB;", "findFirst value = ImplB");
    // 空配置 → Optional.empty()
    Value sl2 = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                     {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                     "Ljava/util/ServiceLoader;", {RefVal(not_sub), RefVal(parent_cl)}, &ok);
    Value f2 = Call(state, sl2.ref->klass, "findFirst", {}, "Ljava/util/Optional;", {sl2}, &ok);
    CHECK(ok && f2.ref != nullptr, "findFirst on empty config -> Optional.empty");
    ObjHeader* v2 = reinterpret_cast<ObjHeader*>(1);
    std::memcpy(&v2, reinterpret_cast<VObject*>(f2.ref)->field_data(), sizeof(v2));
    CHECK(v2 == nullptr, "empty Optional value == null");
  }

  // ================= 3. ServiceConfigurationError 三态 =================
  // 3a. 类不存在
  {
    bool ok = false;
    CHECK(WriteZip(tmp + "_e1.zip",
                   {{"META-INF/services/test.Svc", "test.Missing\n"}}),
          "write zipE1 (missing class)");
    dexvm::session::LoaderState* st = reg.CreateLoader(reg.boot(), "");
    st->resource_zips.push_back(tmp + "_e1.zip");
    VObject* cl = linker.AllocObject(cl_cls);
    reg.BindObject(cl, st);
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(cl)}, &ok);
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    CHECK(!ok && state.HasPendingException(), "missing class -> pending SCE");
    VObject* ex = reinterpret_cast<VObject*>(state.pending_exception);
    CHECK(ex != nullptr && ex->klass != nullptr &&
              ex->klass->descriptor == "Ljava/util/ServiceConfigurationError;",
          "missing class -> ServiceConfigurationError");
    state.ClearException();
  }
  // 3b. 非子类型
  {
    bool ok = false;
    CHECK(WriteZip(tmp + "_e2.zip",
                   {{"META-INF/services/test.Svc", "test.NotSubtype\n"}}),
          "write zipE2 (not a subtype)");
    dexvm::session::LoaderState* st = reg.CreateLoader(reg.boot(), "");
    st->resource_zips.push_back(tmp + "_e2.zip");
    VObject* cl = linker.AllocObject(cl_cls);
    reg.BindObject(cl, st);
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(cl)}, &ok);
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    VObject* ex = reinterpret_cast<VObject*>(state.pending_exception);
    CHECK(!ok && ex != nullptr && ex->klass != nullptr &&
              ex->klass->descriptor == "Ljava/util/ServiceConfigurationError;",
          "not a subtype -> ServiceConfigurationError");
    state.ClearException();
  }
  // 3c. 不可实例化（无 public 无参构造）
  {
    bool ok = false;
    CHECK(WriteZip(tmp + "_e3.zip",
                   {{"META-INF/services/test.Svc", "test.NoCtor\ntest.AbstractImpl\n"}}),
          "write zipE3 (not instantiable)");
    dexvm::session::LoaderState* st = reg.CreateLoader(reg.boot(), "");
    st->resource_zips.push_back(tmp + "_e3.zip");
    VObject* cl = linker.AllocObject(cl_cls);
    reg.BindObject(cl, st);
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(cl)}, &ok);
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    VObject* ex = reinterpret_cast<VObject*>(state.pending_exception);
    CHECK(!ok && ex != nullptr && ex->klass != nullptr &&
              ex->klass->descriptor == "Ljava/util/ServiceConfigurationError;",
          "no public no-arg ctor -> ServiceConfigurationError");
    state.ClearException();
    // 懒迭代：错误只在到达该条目时抛（首个条目 NoCtor 即抛；AbstractImpl 未触达）
  }

  // ================= 4. ClassLoader 资源查找（getResource(s)）=================
  {
    bool ok = false;
    Value u = Call(state, cl_cls, "getResource", {"Ljava/lang/String;"}, "Ljava/net/URL;",
                   {RefVal(parent_cl), RefVal(linker.AllocString(u"META-INF/services/test.Svc"))},
                   &ok);
    CHECK(ok && u.ref != nullptr, "getResource hit");
    Value s = Call(state, u.ref->klass, "toString", {}, "Ljava/lang/String;", {u}, &ok);
    CHECK(ok && StrOf(s.ref).find("META-INF/services/test.Svc") != std::string::npos,
          "URL.toString parseable (contains entry name)");
    Value miss = Call(state, cl_cls, "getResource", {"Ljava/lang/String;"}, "Ljava/net/URL;",
                      {RefVal(parent_cl), RefVal(linker.AllocString(u"no/such/entry"))}, &ok);
    CHECK(ok && miss.ref == nullptr, "getResource miss -> null");
    // getResources：parent(zipA 1 条) + child(zipB 1 条) → 2 条，parent 先
    Value e = Call(state, cl_cls, "getResources", {"Ljava/lang/String;"},
                   "Ljava/util/Enumeration;",
                   {RefVal(child_cl), RefVal(linker.AllocString(u"META-INF/services/test.Svc"))},
                   &ok);
    CHECK(ok && e.ref != nullptr, "getResources returns Enumeration");
    Value m1 = Call(state, e.ref->klass, "hasMoreElements", {}, "Z", {e}, &ok);
    CHECK(ok && m1.i == 1, "enumeration has element 1");
    Value u1 = Call(state, e.ref->klass, "nextElement", {}, "Ljava/lang/Object;", {e}, &ok);
    CHECK(ok && u1.ref != nullptr &&
              DescOf(u1.ref) == "Ljava/net/URL;",
          "enumeration element is URL");
    Value m2 = Call(state, e.ref->klass, "hasMoreElements", {}, "Z", {e}, &ok);
    Value u2 = Call(state, e.ref->klass, "nextElement", {}, "Ljava/lang/Object;", {e}, &ok);
    Value m3 = Call(state, e.ref->klass, "hasMoreElements", {}, "Z", {e}, &ok);
    CHECK(ok && m2.i == 1 && u2.ref != nullptr && m3.i == 0,
          "enumeration has exactly 2 elements");
  }

  // ================= 5. 多资源聚合顺序 =================
  // 5a. 双亲链：parent(zipA) 先、child(zipB) 后 → 名序 [ImplB, ImplA, ...]
  {
    bool ok = false;
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(child_cl)}, &ok);
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Value n1 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    Value n2 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    if (!ok) DumpPending(state);
    CHECK(ok && DescOf(n1.ref) == "Ltest/ImplB;" && DescOf(n2.ref) == "Ltest/ImplA;",
          "chain order: parent zipA names first");
    Value more = Call(state, it.ref->klass, "hasNext", {}, "Z", {it}, &ok);
    if (!ok) DumpPending(state);
    // zipB 的 test.Svc 内容是 test.ImplA（与 zipA 重名去重）→ 无第三条
    CHECK(ok && more.i == 0, "chain order: dedup across zips (ImplA kept first)");
  }
  // 5b. 同 loader 多 zip：zipB 在前/zipC 在后（注入序）→ 名序 [ImplA, ImplB]
  {
    bool ok = false;
    dexvm::session::LoaderState* st = reg.CreateLoader(reg.boot(), "");
    st->resource_zips.push_back(tmp + "_b.zip");
    st->resource_zips.push_back(tmp + "_c.zip");
    VObject* cl = linker.AllocObject(cl_cls);
    reg.BindObject(cl, st);
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;", "Ljava/lang/ClassLoader;"},
                    "Ljava/util/ServiceLoader;", {RefVal(svc), RefVal(cl)}, &ok);
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Value n1 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    Value n2 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    CHECK(ok && DescOf(n1.ref) == "Ltest/ImplA;" && DescOf(n2.ref) == "Ltest/ImplB;",
          "multi-zip order: zipB names before zipC");
  }

  // ================= 附加：load(Class) 走线程上下文 ClassLoader（lib 语义）=================
  {
    bool ok = false;
    // 上下文 loader = solo_cl（zipC 资源可见）——lib load(Class) 取
    // Thread.currentThread().getContextClassLoader()（与 Android/JDK 语义一致）
    VClass* thr_cls = linker.FindClass("Ljava/lang/Thread;");
    Value cur = Call(state, thr_cls, "currentThread", {}, "Ljava/lang/Thread;", {}, &ok);
    CHECK(ok && cur.ref != nullptr, "Thread.currentThread()");
    Call(state, thr_cls, "setContextClassLoader", {"Ljava/lang/ClassLoader;"}, "V",
         {cur, RefVal(solo_cl)}, &ok);
    CHECK(ok, "setContextClassLoader(solo)");
    Value sl = Call(state, linker.FindClass("Ljava/util/ServiceLoader;"), "load",
                    {"Ljava/lang/Class;"}, "Ljava/util/ServiceLoader;", {RefVal(svc)}, &ok);
    CHECK(ok && sl.ref != nullptr, "load(Class) ok");
    Value it = Call(state, sl.ref->klass, "iterator", {}, "Ljava/util/Iterator;", {sl}, &ok);
    Value n1 = Call(state, it.ref->klass, "next", {}, "Ljava/lang/Object;", {it}, &ok);
    if (!ok) DumpPending(state);
    CHECK(ok && n1.ref != nullptr && DescOf(n1.ref) == "Ltest/ImplB;",
          "load(Class) resolves via thread context class loader");
  }

  std::printf("\n== %s (%d failures) ==\n", g_failures == 0 ? "PASS" : "FAIL", g_failures);
  return g_failures == 0 ? 0 : 1;
}
