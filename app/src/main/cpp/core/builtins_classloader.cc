// ClassLoader/DexFile 双亲委派面 + ClassLoader 资源面（ServiceLoader/URL）。
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
#include "core/framework_builtins.h"

namespace dexvm::runtime {

// ---- intrinsic 实现（java/lang/ClassLoader 双亲委派模板 + dalvik.system）----

// ClassLoader.loadClass(String) 虚拟调用辅助（Java 回落 / 双亲委派共用）
namespace {
VClass* CallJavaLoadClass(VMState& state, VObject* target, const std::u16string& name) {
  if (target == nullptr || target->klass == nullptr) return nullptr;
  VMethod probe;
  probe.name = "loadClass";
  probe.param_types = {"Ljava/lang/String;"};
  probe.return_type = "Ljava/lang/Class;";
  VMethod* m = target->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) return nullptr;
  VString* s = state.linker->AllocString(name);
  if (s == nullptr) return nullptr;
  Value args[2] = {};
  args[0].ref = target;
  args[1].ref = s;
  Value r{};
  if (!InvokeMethod(state, m, args, 2, &r)) return nullptr;
  return reinterpret_cast<VClass*>(r.ref);
}

// findClass 核心：自身 DexFile 列表顺序查 ClassDef → DefineClass 绑定 loader
VClass* FindClassOwnDex(VMState& state, VObject* self, const std::string& desc) {
  ClassLinker* linker = state.linker;
  session::LoaderState* st = linker->loaders().Lookup(self);
  dex::DexFile* dex = nullptr;
  int idx = linker->loaders().FindClassDef(st, desc, &dex);
  if (idx < 0) return nullptr;
  return linker->DefineClass(dex, static_cast<uint32_t>(idx), self);
}

// java/lang/ClassLoader.<init>(ClassLoader)：建 LoaderState 并绑定 self
}  // namespace
Value CLInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  session::LoaderState* parent = nullptr;
  if (n >= 1 && !IsNullArg(args[0])) {
    parent = state.linker->loaders().LookupOrCreate(static_cast<VObject*>(args[0].ref),
                                                    state.linker->loaders().boot());
  }
  session::LoaderState* st = state.linker->loaders().CreateLoader(parent, "");
  state.linker->loaders().BindObject(self, st);
  return Value{};
}

// java/lang/ClassLoader.findClass(String)：自身 dex 查找，未命中抛 CNFE
Value CLFindClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : "";
  std::string desc = session::DotToDescriptor(name);
  if (VClass* k = FindClassOwnDex(state, self, desc)) {
    Value r{};
    r.ref = k;
    return r;
  }
  state.ThrowNew("Ljava/lang/ClassNotFoundException;", name);
  *ok = false;
  return Value{};
}

// java/lang/ClassLoader.loadClass(String)：双亲委派模板（docs 14 章 28.1）
//   parent.loadClass → 自身 findClass → ClassNotFoundException
Value CLLoadClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : "";
  std::string desc = session::DotToDescriptor(name);
  session::LoaderRegistry& reg = state.linker->loaders();
  session::LoaderState* st = reg.Lookup(self);
  session::LoaderState* parent =
      st != nullptr ? (st->parent != nullptr ? st->parent : reg.boot()) : reg.boot();

  // 1. 双亲委派：parent.loadClass 虚拟调用（ART Java 模板）；boot 无对象走原生链
  if (parent->loader_object != nullptr && parent->loader_object != self) {
    std::u16string udot(name.begin(), name.end());
    VClass* k = CallJavaLoadClass(state, parent->loader_object, udot);
    if (state.HasPendingException()) {
      // 仅吞 ClassNotFoundException（模板语义）；其余异常传递
      ObjHeader* ex = state.pending_exception;
      if (ex != nullptr && ex->klass != nullptr &&
          ex->klass->descriptor == "Ljava/lang/ClassNotFoundException;") {
        state.ClearException();
      } else {
        *ok = false;
        return Value{};
      }
    } else if (k != nullptr) {
      Value r{};
      r.ref = k;
      return r;
    }
  } else if (VClass* k = state.linker->FindClassInLoaderChain(parent, desc)) {
    Value r{};
    r.ref = k;
    return r;
  }

  // 2. 自身 findClass
  if (VClass* k = FindClassOwnDex(state, self, desc)) {
    Value r{};
    r.ref = k;
    return r;
  }
  // 3. broken 模板终点：ClassNotFoundException
  state.ThrowNew("Ljava/lang/ClassNotFoundException;", name);
  *ok = false;
  return Value{};
}

// BaseDexClassLoader/PathClassLoader/DexClassLoader 构造器 intrinsic 共核：
// dexPath（':' 分隔，.dex/.apk/.jar）+ optDir + libPath + parent → LoaderState
namespace {
Value BdlInitCore(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  ClassLinker* linker = state.linker;
  session::LoaderRegistry& reg = linker->loaders();
  std::string dex_path = n >= 1 && !IsNullArg(args[0]) ? Mutf8Arg(args[0]) : "";
  std::string lib_path = n >= 3 && !IsNullArg(args[2]) ? Mutf8Arg(args[2]) : "";
  VObject* parent_obj = (n >= 4 && !IsNullArg(args[3])) ? static_cast<VObject*>(args[3].ref) : nullptr;

  session::LoaderState* parent =
      parent_obj != nullptr ? reg.LookupOrCreate(parent_obj, reg.boot()) : reg.boot();
  session::LoaderState* st = reg.CreateLoader(parent, lib_path);

  for (const std::string& seg : SplitPathList(dex_path)) {
    std::vector<std::vector<uint8_t>> bytes;
    std::string err;
    if (!ReadDexPathBytes(seg, &bytes, &err)) {
      state.ThrowNew("Ljava/io/IOException;", err.empty() ? seg : err);
      *ok = false;
      return Value{};
    }
    // 资源面（M5 ServiceLoader）：zip 段（.apk/.jar）挂资源查询（META-INF/services 等）
    if (seg.size() > 4 && (seg.compare(seg.size() - 4, 4, ".apk") == 0 ||
                           seg.compare(seg.size() - 4, 4, ".jar") == 0)) {
      st->resource_zips.push_back(seg);
    }
    uint64_t cookie = reg.OpenDexSet(seg, std::move(bytes), &err);
    if (cookie == 0) {
      state.ThrowNew("Ljava/io/IOException;", err.empty() ? seg : err);
      *ok = false;
      return Value{};
    }
    for (dex::DexFile* dex : reg.GetDexFiles(cookie)) reg.AttachDex(st, dex);
  }
  reg.BindObject(self, st);
  return Value{};
}

// optDir 在前（DexClassLoader 4 参）/ libPath 在前（BaseDexClassLoader 4 参）不同序：
// 统一按 BdlInitCore 的 (dexPath, -, libPath, parent) 槽位映射
}  // namespace
Value BDCLInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // (String dexPath, String optDir, String libPath, ClassLoader parent)
  return BdlInitCore(state, self, args, n, ok);
}

Value PCLInit2(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // (String dexPath, String libPath, ClassLoader parent) → 重排到 4 槽
  Value full[4] = {};
  full[0] = n >= 1 ? args[0] : Value{};
  full[2] = n >= 2 ? args[1] : Value{};
  full[3] = n >= 3 ? args[2] : Value{};
  return BdlInitCore(state, self, full, 4, ok);
}

Value PCLInit1(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  // (String dexPath, ClassLoader parent)
  Value full[4] = {};
  full[0] = n >= 1 ? args[0] : Value{};
  full[3] = n >= 2 ? args[1] : Value{};
  return BdlInitCore(state, self, full, 4, ok);
}

// TEMP DIAG: 内存 dex（加固壳解密产物）落盘，供离线反汇编定位 guest 侧
// 加固方法真实字节码。仅诊断期使用，事后移除。
namespace {
void DumpDexBytes(const std::vector<std::vector<uint8_t>>& bytes, const char* tag) {
  static int seq = 0;
  std::string dir = GetContextDataDir();
  if (dir.empty()) return;
  for (size_t i = 0; i < bytes.size(); i++) {
    char name[96];
    std::snprintf(name, sizeof(name), "/%s_%d_%zu.dex", tag, ++seq, i);
    std::ofstream f(dir + name, std::ios::binary | std::ios::trunc);
    if (!f) continue;
    f.write(reinterpret_cast<const char*>(bytes[i].data()),
            static_cast<std::streamsize>(bytes[i].size()));
  }
}
}  // namespace

// InMemoryDexClassLoader 构造（ByteBuffer/byte[]/byte[][] → dex 字节）
Value IMCLInit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  ClassLinker* linker = state.linker;
  session::LoaderRegistry& reg = linker->loaders();
  std::vector<std::vector<uint8_t>> bytes;
  std::string err;
  if (n < 1 || !ExtractDexBytes(args[0], &bytes, &err)) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", err);
    *ok = false;
    return Value{};
  }
  VObject* parent_obj = (n >= 2 && !IsNullArg(args[1])) ? static_cast<VObject*>(args[1].ref) : nullptr;
  session::LoaderState* parent =
      parent_obj != nullptr ? reg.LookupOrCreate(parent_obj, reg.boot()) : reg.boot();
  session::LoaderState* st = reg.CreateLoader(parent, "");
  static uint64_t g_mem_loc = 0;
  std::string loc = "<memory:" + std::to_string(++g_mem_loc) + ">";
  DumpDexBytes(bytes, "imcl");
  uint64_t cookie = reg.OpenDexSet(loc, std::move(bytes), &err);
  if (cookie == 0) {
    state.ThrowNew("Ljava/io/IOException;", err);
    *ok = false;
    return Value{};
  }
  for (dex::DexFile* dex : reg.GetDexFiles(cookie)) reg.AttachDex(st, dex);
  reg.BindObject(self, st);
  return Value{};
}

// dalvik/system/DexFile.openDexFileNative：文件/字节 → cookie（不透明 ID）
namespace {
Value OpenDexCommon(VMState& state, std::vector<std::vector<uint8_t>> bytes,
                    const std::string& location, bool* ok) {
  std::string err;
  DumpDexBytes(bytes, "dexbytes");
  uint64_t cookie = state.linker->loaders().OpenDexSet(location, std::move(bytes), &err);
  if (cookie == 0) {
    state.ThrowNew("Ljava/io/IOException;", err.empty() ? location : err);
    *ok = false;
    return Value{};
  }
  Value r{};
  r.l = static_cast<int64_t>(cookie);
  return r;
}

}  // namespace
Value DexOpenNative(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string path = n >= 1 ? Mutf8Arg(args[0]) : "";
  std::vector<std::vector<uint8_t>> bytes;
  std::string err;
  if (!ReadDexPathBytes(path, &bytes, &err)) {
    state.ThrowNew("Ljava/io/IOException;", err.empty() ? path : err);
    *ok = false;
    return Value{};
  }
  return OpenDexCommon(state, std::move(bytes), path, ok);
}

Value DexOpenNativeBytes(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::vector<std::vector<uint8_t>> bytes;
  std::string err;
  if (n < 1 || !ExtractDexBytes(args[0], &bytes, &err)) {
    state.ThrowNew("Ljava/lang/IllegalArgumentException;", err);
    *ok = false;
    return Value{};
  }
  static uint64_t g_mem_loc = 0;
  return OpenDexCommon(state, std::move(bytes), "<dexfile:" + std::to_string(++g_mem_loc) + ">", ok);
}

// dalvik/system/DexFile.defineClassNative(name, loader, cookie, dexFile)（28.2）
Value DexDefineClassNative(VMState& state, VObject* self, const Value* args, size_t n,
                           bool* ok) {
  (void)self;
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : "";
  VObject* loader = (n >= 2 && !IsNullArg(args[1])) ? static_cast<VObject*>(args[1].ref) : nullptr;
  int64_t cookie = n >= 3 ? args[2].l : 0;
  std::string desc = session::DotToDescriptor(name);
  session::LoaderRegistry& reg = state.linker->loaders();
  std::vector<dex::DexFile*> dex_files = reg.GetDexFiles(static_cast<uint64_t>(cookie));
  if (dex_files.empty()) {
    state.ThrowNew("Ljava/io/IOException;", "invalid dex cookie");
    *ok = false;
    return Value{};
  }
  for (dex::DexFile* dex : dex_files) {
    int idx = dex->FindClassDefIndexByDescriptor(desc);
    if (idx < 0) continue;
    session::LoaderState* st =
        loader != nullptr ? reg.LookupOrCreate(loader, reg.boot()) : reg.boot();
    // RegisterDexFile + InsertDexFileInToClassLoader（dalvik_system_DexFile.cc:501-511）
    reg.AttachDex(st, dex);
    VClass* k = state.linker->DefineClass(dex, static_cast<uint32_t>(idx), loader);
    Value r{};
    r.ref = k;
    return r;
  }
  // ART：找不到 ClassDef 记 log 返回 nullptr（无异常）
  return Value{};
}

// dalvik/system/DexFile.loadClass(name, loader)：loader 自身 dex → defineClass
Value DexLoadClass(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  std::string name = n >= 1 ? Mutf8Arg(args[0]) : "";
  VObject* loader = (n >= 2 && !IsNullArg(args[1])) ? static_cast<VObject*>(args[1].ref) : nullptr;
  std::string desc = session::DotToDescriptor(name);
  session::LoaderRegistry& reg = state.linker->loaders();
  session::LoaderState* st =
      loader != nullptr ? reg.LookupOrCreate(loader, reg.boot()) : reg.boot();
  dex::DexFile* dex = nullptr;
  int idx = reg.FindClassDef(st, desc, &dex);
  if (idx < 0) return Value{};  // ART 语义：未命中返回 null
  VClass* k = state.linker->DefineClass(dex, static_cast<uint32_t>(idx), loader);
  Value r{};
  r.ref = k;
  return r;
}

// dalvik/system/DexFile.getClassNameList(cookie)：multidex 去重 dot 名（28.2）
Value DexGetClassNameList(VMState& state, VObject* self, const Value* args, size_t n,
                          bool* ok) {
  (void)self;
  int64_t cookie = n >= 1 ? args[0].l : 0;
  std::set<std::string> dots;  // set = multidex 去重 + 字典序（对照 ART 实现）
  for (dex::DexFile* dex : state.linker->loaders().GetDexFiles(static_cast<uint64_t>(cookie))) {
    for (uint32_t i = 0; i < dex->NumClassDefs(); i++) {
      const dex::ClassDef& def = dex->GetClassDef(i);
      dots.insert(session::DescriptorToDot(dex->GetTypeDescriptor(def.class_idx)));
    }
  }
  ClassLinker* linker = state.linker;
  VClass* string = linker->FindClass("Ljava/lang/String;");
  VClass* arr_class = string != nullptr ? linker->FindArrayClass(string) : nullptr;
  if (arr_class == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "no String[] class");
    *ok = false;
    return Value{};
  }
  VArray* arr = linker->AllocArray(arr_class, static_cast<int32_t>(dots.size()));
  if (arr == nullptr) {
    ThrowOOM(state, "getClassNameList");
    *ok = false;
    return Value{};
  }
  int32_t i = 0;
  for (const std::string& dot : dots) {
    std::u16string u(dot.begin(), dot.end());
    VString* s = linker->AllocString(u);
    uint64_t slot = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(s));
    std::memcpy(arr->data() + i * 8, &slot, sizeof(slot));
    i++;
  }
  Value r{};
  r.ref = arr;
  return r;
}

// dalvik/system/DexFile.closeDexFile(cookie)：清缓存；未注册才移除；全删净 true
Value DexCloseDexFile(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  int64_t cookie = n >= 1 ? args[0].l : 0;
  bool all = state.linker->loaders().CloseCookie(static_cast<uint64_t>(cookie));
  Value r{};
  r.i = all ? 1 : 0;
  return r;
}

// 静态便捷入口：openDexFile(String) / loadDex(String,String,int)（28.2）
Value DexOpenDexFile(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return DexOpenNative(state, self, args, n, ok);
}

Value DexLoadDex(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  return DexOpenNative(state, self, args, n, ok);
}
// ==== M5：ClassLoader 资源面（META-INF/services 资源；ServiceLoader 本体由
// runtime.dex 提供——P0 翻转后 lib ServiceLoader 的 LazyClassPathLookupIterator
// 资源读取面经 BindWellKnownNatives 绑 native（parse）走本节抽取语义）====

namespace {
uint64_t SLGetU64(const VObject* o, uint32_t off) {
  uint64_t v = 0;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}
void SLSetU64(VObject* o, uint32_t off, uint64_t v) {
  std::memcpy(o->field_data() + off, &v, sizeof(v));
}

// 配置文件行解析（ServiceLoader.parse 语义）：'#' 截断注释、跳过空行、
// 首个空白前的 token 为类名、同名去重保首现、文件间顺序保留。
void ParseServiceConfig(const std::vector<uint8_t>& bytes, std::vector<std::string>* names) {
  size_t i = 0;
  while (i < bytes.size()) {
    size_t eol = i;
    while (eol < bytes.size() && bytes[eol] != '\n') eol++;
    std::string line(reinterpret_cast<const char*>(bytes.data() + i), eol - i);
    i = eol + 1;
    size_t hash = line.find('#');
    if (hash != std::string::npos) line.resize(hash);
    size_t b = line.find_first_not_of(" \t\r");
    if (b == std::string::npos) continue;
    size_t e = line.find_first_of(" \t\r", b);
    std::string name = line.substr(b, e == std::string::npos ? std::string::npos : e - b);
    if (name.empty()) continue;
    if (std::find(names->begin(), names->end(), name) == names->end()) {
      names->push_back(std::move(name));
    }
  }
}

// 资源聚合（ClassLoader.getResources 语义；session IO 构建下走 zip entry 抽取，
// 否则恒空——boot runtime.dex 无资源语义一致）
void SLCollectResources(const std::string& name, session::LoaderState* st,
                        std::vector<session::ResourceHit>* out) {
#ifdef DEXVM_SESSION_IO
  session::GetLoaderResources(st, name, out);
#else
  (void)name;
  (void)st;
  (void)out;
#endif
}

// ServiceConfigurationError 抛出（JDK fail()："<service>: <msg>"）
void SLFail(VMState& state, VClass* service, const std::string& msg, bool* ok) {
  std::string full = (service != nullptr ? ClassName(service) : std::string("?")) + ": " + msg;
  state.ThrowNew("Ljava/util/ServiceConfigurationError;", full);
  *ok = false;
}

// public 无参构造（构造器不继承：Java getConstructor() 只看声明类）
VMethod* FindPublicNoArgCtor(VClass* k) {
  if (k == nullptr) return nullptr;
  for (auto& m : k->direct_methods) {
    if (m.name == "<init>" && m.param_types.empty() && (m.access_flags & kAccPublic) != 0) {
      return &m;
    }
  }
  return nullptr;
}

// P0 翻转：ServiceLoader/$LazyIterator 内建 intrinsic 实现删除（load/iterator/
// findFirst/stream/LazyIterator 面）——由 runtime.dex lib ServiceLoader（JDK9
// 形态）承接；URL 骨架/CompoundEnumeration intrinsic 同删（类定义在 lib）。
// ---- ClassLoader.getResource(s) 面（URL 骨架 / Enumeration）----

// 资源链查询（loader 对象 → LoaderState 链；parent 先）
void CLCollectResources(VMState& state, VObject* self, const std::string& name,
                        std::vector<session::ResourceHit>* out) {
  session::LoaderState* st = self != nullptr
                                 ? state.linker->loaders().Lookup(self)
                                 : state.linker->loaders().boot();
  if (st == nullptr) st = state.linker->loaders().boot();
  SLCollectResources(name, st, out);
}

// ---- lib URL 字段面（P0 翻转：java.net.URL 由 runtime.dex 提供）----
// 布局契约（OpenJDK java.net.URL）：protocol/host/authority/file/ref 为 String、
// port/hashCode 为 int（8 字节槽低 4 字节）；按名沿继承链写入（不走 <init>——
// handler 查找面后置）。spec 约定："proto:rest" → protocol="proto"、file="rest"
// （toExternalForm native 面按 protocol+":"+file[+#ref] 还原，round-trip 精确）。

VField* FindFieldInChain(VClass* k, const char* name, const char* type) {
  for (VClass* c = k; c != nullptr; c = c->super_class) {
    for (auto& f : c->instance_fields) {
      if (f.name == name && f.type_descriptor == type) return &f;
    }
  }
  return nullptr;
}

void SetURLRefField(VObject* u, const char* name, ObjHeader* v) {
  if (u == nullptr || u->klass == nullptr) return;
  if (VField* f = FindFieldInChain(u->klass, name, "Ljava/lang/String;")) {
    ObjHeader** slot = reinterpret_cast<ObjHeader**>(u->field_data() + f->offset);
    *slot = v;
  }
}

void SetURLIntField(VObject* u, const char* name, int32_t v) {
  if (u == nullptr || u->klass == nullptr) return;
  if (VField* f = FindFieldInChain(u->klass, name, "I")) {
    uint64_t slot = static_cast<uint64_t>(static_cast<uint32_t>(v));
    std::memcpy(u->field_data() + f->offset, &slot, sizeof(slot));
  }
}

// URL 实例 → spec 还原（protocol+":"+file[+#ref]；与 URLToExternalForm 一致）
std::string URLSpecFromFields(VObject* u) {
  auto get_str = [&](const char* name) -> std::string {
    if (u == nullptr || u->klass == nullptr) return "";
    if (VField* f = FindFieldInChain(u->klass, name, "Ljava/lang/String;")) {
      ObjHeader* v = nullptr;
      std::memcpy(&v, u->field_data() + f->offset, sizeof(v));
      if (v == nullptr) return "";
      return static_cast<VString*>(v)->ToModifiedUtf8();
    }
    return "";
  };
  std::string spec = get_str("protocol");
  if (!spec.empty() || get_str("file").empty()) spec += ":";
  spec += get_str("file");
  std::string ref = get_str("ref");
  if (!ref.empty()) {
    spec += "#";
    spec += ref;
  }
  return spec;
}

// hit → java.net.URL 实例（lib 字段布局；spec = hit.url）
}  // namespace
Value MakeURL(VMState& state, const session::ResourceHit& hit, bool* ok) {
  VClass* url_cls = state.linker->FindClass("Ljava/net/URL;");
  VObject* u = url_cls != nullptr ? state.linker->AllocObject(url_cls) : nullptr;
  if (u == nullptr) {
    state.ThrowNew("Ljava/lang/InternalError;", "URL alloc failed");
    *ok = false;
    return Value{};
  }
  // "proto:rest" 拆分（round-trip 由 URLToExternalForm native 面保证）
  std::string proto;
  std::string rest;
  size_t colon = hit.url.find(':');
  if (colon == std::string::npos) {
    rest = hit.url;
  } else {
    proto = hit.url.substr(0, colon);
    rest = hit.url.substr(colon + 1);
  }
  auto make_str = [&](const std::string& s) -> ObjHeader* {
    std::u16string u16;
    AppendUtf16(&u16, s.c_str());
    Value sv = MakeStr(state, u16, ok);
    return *ok ? sv.ref : nullptr;
  };
  ObjHeader* proto_s = make_str(proto);
  if (!*ok) return Value{};
  ObjHeader* rest_s = make_str(rest);
  if (!*ok) return Value{};
  ObjHeader* host_s = make_str("");
  if (!*ok) return Value{};
  SetURLRefField(u, "protocol", proto_s);
  SetURLRefField(u, "file", rest_s);
  SetURLRefField(u, "host", host_s);
  SetURLIntField(u, "port", -1);
  Value r{};
  r.ref = u;
  return r;
}

// ClassLoader.getParent()（lib ServiceLoader ModuleServicesLookupIterator 的
// 双亲走链面）：LoaderState.parent → loader 对象（boot 之上 null）
Value CLGetParent(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)args;
  (void)n;
  session::LoaderState* st =
      self != nullptr ? state.linker->loaders().Lookup(self) : nullptr;
  session::LoaderState* p = st != nullptr ? st->parent : nullptr;
  Value r{};
  r.ref = p != nullptr ? p->loader_object : nullptr;
  *ok = true;
  return r;
}

// ClassLoader.getSystemClassLoader()（lib ServiceLoader.load 的 cl==null 回落）：
// M1 模型的 default app loader 对象
Value CLGetSystemClassLoader(VMState& state, VObject* self, const Value* args, size_t n,
                             bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  session::LoaderState* st = state.linker->loaders().default_app();
  Value r{};
  r.ref = st != nullptr ? st->loader_object : nullptr;
  *ok = true;
  return r;
}

Value CLGetResource(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  std::string name = Mutf8Arg(args[0]);
  std::vector<session::ResourceHit> hits;
  CLCollectResources(state, self, name, &hits);
  if (hits.empty()) return Value{};  // null
  return MakeURL(state, hits[0], ok);
}

// 实例虚调用辅助（本节早于 CallVirtual 定义处，独立小面）
bool InvokeVirtualNamed(VMState& state, VObject* obj, const char* name,
                        std::vector<std::string> params, const char* ret, const Value* args,
                        size_t n, Value* out) {
  if (obj == nullptr || obj->klass == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "invoke on null");
    return false;
  }
  VMethod probe;
  probe.name = name;
  probe.param_types = std::move(params);
  probe.return_type = ret;
  VMethod* m = obj->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", name);
    return false;
  }
  std::vector<Value> full;
  full.reserve(n + 1);
  Value self_v{};
  self_v.ref = obj;
  full.push_back(self_v);
  for (size_t i = 0; i < n; i++) full.push_back(args[i]);
  Value r{};
  if (!InvokeMethod(state, m, full.data(), full.size(), &r)) return false;
  if (out != nullptr) *out = r;
  return true;
}

Value CLGetResources(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)n;
  std::string name = Mutf8Arg(args[0]);
  std::vector<session::ResourceHit> hits;
  CLCollectResources(state, self, name, &hits);
  // 返回 lib Enumeration<URL>：java.util.Vector 聚合 + elements()（java.util 语义；
  // 原 sun.misc.CompoundEnumeration 内建已删，聚合面由 lib Collection 承接）
  VClass* vec_cls = state.linker->FindClass("Ljava/util/Vector;");
  if (vec_cls == nullptr || !state.linker->InitializeClass(vec_cls, &state)) {
    state.ThrowNew("Ljava/lang/InternalError;", "Vector unavailable for getResources");
    *ok = false;
    return Value{};
  }
  VObject* vec = state.linker->AllocObject(vec_cls);
  if (vec == nullptr) {
    ThrowOOM(state, "getResources");
    *ok = false;
    return Value{};
  }
  Value ignored{};
  if (!InvokeVirtualNamed(state, vec, "<init>", {}, "V", nullptr, 0, &ignored)) {
    *ok = false;
    return Value{};
  }
  for (size_t i = 0; i < hits.size(); i++) {
    Value u = MakeURL(state, hits[i], ok);
    if (!*ok) return Value{};
    Value arg{};
    arg.ref = u.ref;
    if (!InvokeVirtualNamed(state, vec, "addElement", {"Ljava/lang/Object;"}, "V", &arg, 1,
                            &ignored)) {
      *ok = false;
      return Value{};
    }
  }
  Value en{};
  if (!InvokeVirtualNamed(state, vec, "elements", {}, "Ljava/util/Enumeration;", nullptr, 0, &en)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return en;
}

// getResourceAsStream / getSystemResource(s) 系（Android ClassLoader 语义：
// getResource 取 URL 后 openStream；System 形经 getSystemClassLoader 委派）。
Value CLGetResourceAsStream(VMState& state, VObject* self, const Value* args, size_t n,
                            bool* ok) {
  (void)n;
  Value url{};
  if (!InvokeVirtualNamed(state, self, "getResource", {"Ljava/lang/String;"},
                          "Ljava/net/URL;", args, 1, &url)) {
    *ok = false;
    return Value{};
  }
  if (url.ref == nullptr) {
    *ok = true;
    return Value{};  // null
  }
  Value stream{};
  if (!InvokeVirtualNamed(state, static_cast<VObject*>(url.ref), "openStream", {},
                          "Ljava/io/InputStream;", nullptr, 0, &stream)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return stream;
}

Value CLGetSystemResource(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  Value loader = CLGetSystemClassLoader(state, nullptr, nullptr, 0, ok);
  if (!*ok || loader.ref == nullptr) {
    *ok = true;
    return Value{};  // 无系统 loader：null
  }
  Value url{};
  if (!InvokeVirtualNamed(state, static_cast<VObject*>(loader.ref), "getResource",
                          {"Ljava/lang/String;"}, "Ljava/net/URL;", args, 1, &url)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return url;
}

Value CLGetSystemResources(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)n;
  VClass* cl = state.linker->FindClass("Ljava/lang/ClassLoader;");
  VMethod* m =
      cl != nullptr ? FindStaticM(cl, "getSystemClassLoader", {}, "Ljava/lang/ClassLoader;") : nullptr;
  Value loader{};
  if (m == nullptr || !InvokeMethod(state, m, nullptr, 0, &loader) || loader.ref == nullptr) {
    *ok = false;
    return Value{};
  }
  Value res{};
  if (!InvokeVirtualNamed(state, static_cast<VObject*>(loader.ref), "getResources",
                          {"Ljava/lang/String;"}, "Ljava/util/Enumeration;", args, 1, &res)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return res;
}

Value CLGetSystemResourceAsStream(VMState& state, VObject* self, const Value* args, size_t n,
                                  bool* ok) {
  (void)self;
  (void)n;
  Value url = CLGetSystemResource(state, nullptr, args, n, ok);
  if (!*ok) return Value{};
  if (url.ref == nullptr) {
    *ok = true;
    return Value{};
  }
  Value stream{};
  if (!InvokeVirtualNamed(state, static_cast<VObject*>(url.ref), "openStream", {},
                          "Ljava/io/InputStream;", nullptr, 0, &stream)) {
    *ok = false;
    return Value{};
  }
  *ok = true;
  return stream;
}

// ---- P0 翻转接缝：lib 类最小 native 面（BindWellKnownNatives 覆写表落点）----

// lib java.net.URL.toExternalForm()（handler 查找面后置；MakeURL 字段约定的逆）：
// protocol+":"+file[+#ref]。String concat（fail(URL) 消息）经 toString→本面。
Value URLToExternalForm(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)args;
  (void)n;
  std::string spec = URLSpecFromFields(self);
  std::u16string u;
  AppendUtf16(&u, spec.c_str());
  return MakeStr(state, u, ok);
}
// java.lang.invoke 后置；VALUE 静态槽保持 null，CAS 方法整体覆写）
Value NoopClinit(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)state;
  (void)self;
  (void)args;
  (void)n;
  *ok = true;
  return Value{};
}

// lib ServiceLoader$ModuleServicesLookupIterator.iteratorFor(ClassLoader)：
// 无 JPMS 服务目录（jdk.internal.module.ServicesCatalog 未入 VM）——classpath
// 语义恒空（模块注册面不参与，provider 全部走 LazyClassPathLookupIterator）。
Value SLModuleIteratorFor(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  (void)args;
  (void)n;
  VClass* cs = state.linker->FindClass("Ljava/util/Collections;");
  if (cs == nullptr || !state.linker->InitializeClass(cs, &state)) {
    state.ThrowNew("Ljava/lang/InternalError;", "Collections unavailable");
    *ok = false;
    return Value{};
  }
  VMethod* empty = FindStaticM(cs, "emptyList", {}, "Ljava/util/List;");
  if (empty == nullptr) {
    state.ThrowNew("Ljava/lang/NoSuchMethodError;", "Collections.emptyList");
    *ok = false;
    return Value{};
  }
  Value lst{};
  if (!InvokeMethod(state, empty, nullptr, 0, &lst)) return Value{};
  Value r{};
  if (!InvokeVirtualNamed(state, static_cast<VObject*>(lst.ref), "iterator", {},
                          "Ljava/util/Iterator;", nullptr, 0, &r)) {
    return Value{};
  }
  *ok = true;
  return r;
}

// lib ServiceLoader$LazyClassPathLookupIterator.parse(URL)：资源读取面
// （Q2 IO 接缝）——URL spec（jar:file:<zip>!/<entry>）→ session zip 抽取 →
// ServiceLoader.parse 行语义（ParseServiceConfig）→ ArrayList<String>.iterator()。
// stream 解码链（sun.nio.cs.UTF_8/InputStreamReader）不入 VM，读面在此收口。
Value SLParseConfig(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  if (n < 1 || IsNullArg(args[0])) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "parse url == null");
    *ok = false;
    return Value{};
  }
  std::string spec = URLSpecFromFields(static_cast<VObject*>(args[0].ref));
  std::vector<uint8_t> bytes;
  bool have = false;
#ifdef DEXVM_SESSION_IO
  // "jar:file:<zip>!/<entry>" → zip + entry（ResourceHit.url 生成约定的逆）
  const std::string kPrefix = "jar:file:";
  if (spec.rfind(kPrefix, 0) == 0) {
    std::string rest = spec.substr(kPrefix.size());
    size_t bang = rest.rfind("!/");
    if (bang != std::string::npos) {
      std::string zip = rest.substr(0, bang);
      std::string entry = rest.substr(bang + 2);
      std::vector<session::ResourceHit> hits;
      std::string err;
      if (session::ReadZipResources(zip, entry, &hits, &err)) {
        for (const auto& h : hits) {
          if (h.url == spec) {
            bytes = h.bytes;
            have = true;
            break;
          }
        }
        if (!have && !hits.empty()) {
          bytes = hits.front().bytes;
          have = true;
        }
      }
    }
  }
#else
  (void)spec;
#endif
  if (!have) {
    // JDK fail() 形态：Error accessing configuration file
    state.ThrowNew("Ljava/util/ServiceConfigurationError;",
                   "Error accessing configuration file: " + spec);
    *ok = false;
    return Value{};
  }
  std::vector<std::string> names;
  ParseServiceConfig(bytes, &names);
  // parseLine 语义：跨配置文件去重门（LazyClassPathLookupIterator.providerNames
  // 字段 Set）——首现保留（lib parse 行为；文件内去重 ParseServiceConfig 已做）
  VField* pn_f = self != nullptr ? FindInstField(self->klass, "providerNames") : nullptr;
  VObject* seen =
      pn_f != nullptr ? reinterpret_cast<VObject*>(
                            *reinterpret_cast<ObjHeader**>(self->field_data() + pn_f->offset))
                      : nullptr;
  if (seen != nullptr) {
    std::vector<std::string> fresh;
    for (const std::string& name : names) {
      std::u16string u;
      AppendUtf16(&u, name.c_str());
      Value sv = MakeStr(state, u, ok);
      if (!*ok) return Value{};
      Value arg{};
      arg.ref = sv.ref;
      Value added{};
      if (!InvokeVirtualNamed(state, seen, "add", {"Ljava/lang/Object;"}, "Z", &arg, 1, &added)) {
        return Value{};
      }
      if (added.i != 0) fresh.push_back(name);
    }
    names = std::move(fresh);
  }
  VClass* al = state.linker->FindClass("Ljava/util/ArrayList;");
  if (al == nullptr || !state.linker->InitializeClass(al, &state)) {
    state.ThrowNew("Ljava/lang/InternalError;", "ArrayList unavailable");
    *ok = false;
    return Value{};
  }
  VObject* list = state.linker->AllocObject(al);
  if (list == nullptr) {
    ThrowOOM(state, "parse names");
    *ok = false;
    return Value{};
  }
  Value ignored{};
  if (!InvokeVirtualNamed(state, list, "<init>", {}, "V", nullptr, 0, &ignored)) {
    *ok = false;
    return Value{};
  }
  for (const std::string& name : names) {
    std::u16string u;
    AppendUtf16(&u, name.c_str());
    Value sv = MakeStr(state, u, ok);
    if (!*ok) return Value{};
    Value arg{};
    arg.ref = sv.ref;
    if (!InvokeVirtualNamed(state, list, "add", {"Ljava/lang/Object;"}, "Z", &arg, 1, &ignored)) {
      *ok = false;
      return Value{};
    }
  }
  Value r{};
  if (!InvokeVirtualNamed(state, list, "iterator", {}, "Ljava/util/Iterator;", nullptr, 0, &r)) {
    return Value{};
  }
  *ok = true;
  return r;
}

}  // namespace dexvm::runtime
