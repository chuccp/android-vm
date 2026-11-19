// ClassLinker：Find/Define/Link/Initialize（语义对照 class_linker.cc，docs 03 章 11.x）
#include "core/class_linker.h"

#include <atomic>
#include <mutex>
#include <set>

namespace {
// FindMiss 降噪：同一描述符只打一次（IpModel 等 5s 轮询曾刷满 logcat），
// 上限 2000 条。
bool FirstFindMiss(const std::string& desc) {
  static std::mutex m;
  static std::set<std::string> seen;
  std::lock_guard<std::mutex> lk(m);
  if (seen.size() > 2000) return false;
  return seen.insert(desc).second;
}
}  // namespace

#include <algorithm>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#include <unistd.h>
#define CLINIT_LOG(...) __android_log_print(ANDROID_LOG_INFO, "DexVmClinitDbg", __VA_ARGS__)
#else
#define CLINIT_LOG(...) \
  do {               \
  } while (0)
#endif

#include <chrono>

#if defined(__ANDROID__)
namespace {
// 启动期类加载耗时采样：只计 FindClass 顶层调用（定义级联/Java 回落递归
// 归并到触发类），超过阈值打一行——定位 onCreate 中“类加载雪崩”的具体类。
thread_local int g_find_class_depth = 0;
struct FindClassTimer {
  const char* desc;
  std::chrono::steady_clock::time_point t0;
  bool top;
  explicit FindClassTimer(const char* d)
      : desc(d), t0(std::chrono::steady_clock::now()), top(g_find_class_depth++ == 0) {}
  ~FindClassTimer() {
    --g_find_class_depth;
    if (!top) return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now() - t0)
                  .count();
    if (ms >= 50) {
      __android_log_print(ANDROID_LOG_INFO, "DexVmClassLoad", "%s %lld ms", desc,
                          static_cast<long long>(ms));
    }
  }
};
}  // namespace
#endif

#include "core/builtins.h"
#include "core/interpreter.h"
#include "dex/leb128.h"

namespace dexvm::runtime {

namespace {

bool SameSignature(const VMethod& a, const VMethod& b) {
  return a.name == b.name && a.return_type == b.return_type && a.param_types == b.param_types;
}

// 接口方法解析闭包（JLS：方法解析含超接口）：invoke-interface 命中
// 超接口声明（如 java.util.Set.iterator() 声明于 Collection——vendored
// overlay 的 Set 只补静态面，抽象面沿 Collection 继承）时，super 链未命中
// 后沿接口向量递归查找（类链各节点的直接接口 + 超接口）。
VMethod* FindInInterfaceClosure(VClass* klass, const VMethod& target) {
  std::vector<VClass*> stack;
  std::vector<VClass*> seen;
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (VClass* iface : k->interfaces) stack.push_back(iface);
  }
  while (!stack.empty()) {
    VClass* cur = stack.back();
    stack.pop_back();
    if (cur == nullptr) continue;
    bool dup = false;
    for (VClass* s : seen)
      if (s == cur) dup = true;
    if (dup) continue;
    seen.push_back(cur);
    for (auto& m : cur->direct_methods) {
      if (!m.IsStatic() && SameSignature(m, target)) return &m;
    }
    for (auto& m : cur->virtual_methods) {
      if (SameSignature(m, target)) return &m;
    }
    for (VClass* iface : cur->interfaces) stack.push_back(iface);
  }
  return nullptr;
}

// 接口字段解析闭包（JLS 5.4.3.2：字段解析含超接口）：sget/iget 命中接口常量
// （如 jackson PrettyPrinter.DEFAULT_SEPARATORS 声明于接口、由实现类
// DefaultPrettyPrinter 继承引用）时，super 链未命中后沿接口向量递归查找
//（类链各节点的直接接口 + 超接口；static/instance 字段均查——接口字段恒
// static，反射面 instance 判定按声明面一并覆盖）。
VField* FindFieldInInterfaceClosure(VClass* klass, const std::string& name,
                                    const std::string& type) {
  std::vector<VClass*> stack;
  std::vector<VClass*> seen;
  for (VClass* k = klass; k != nullptr; k = k->super_class) {
    for (VClass* iface : k->interfaces) stack.push_back(iface);
  }
  while (!stack.empty()) {
    VClass* cur = stack.back();
    stack.pop_back();
    if (cur == nullptr) continue;
    bool dup = false;
    for (VClass* s : seen)
      if (s == cur) dup = true;
    if (dup) continue;
    seen.push_back(cur);
    for (auto& f : cur->static_fields) {
      if (f.name == name && f.type_descriptor == type) return &f;
    }
    for (auto& f : cur->instance_fields) {
      if (f.name == name && f.type_descriptor == type) return &f;
    }
    for (VClass* iface : cur->interfaces) stack.push_back(iface);
  }
  return nullptr;
}

// "Ljava/lang/String;" 形态的参数列表由 proto 展开
void FillProto(dex::DexFile* dex, uint32_t proto_idx, VMethod* m) {
  const dex::ProtoId& proto = dex->GetProto(proto_idx);
  m->return_type = std::string(dex->GetTypeDescriptor(proto.return_type_idx));
  uint32_t n = dex->GetProtoParamCount(proto_idx);
  m->param_types.reserve(n);
  for (uint32_t i = 0; i < n; i++) {
    m->param_types.emplace_back(
        dex->GetTypeDescriptor(dex->GetProtoParamTypeIdx(proto_idx, i)));
  }
  // shorty：返回类型 + 各参数首字符
  std::string shorty;
  shorty.push_back(m->return_type[0] == 'L' || m->return_type[0] == '[' ? 'L'
                                                                       : m->return_type[0]);
  for (auto& p : m->param_types) {
    shorty.push_back(p[0] == 'L' || p[0] == '[' ? 'L' : p[0]);
  }
  m->shorty = shorty;
}

uint32_t FieldSlotSize(const std::string& desc) {
  (void)desc;
  return 8;  // M1：所有字段 8 字节槽
}

}  // namespace

namespace {
std::atomic<uint64_t> g_next_linker_id{1};
}  // namespace

ClassLinker::ClassLinker(mem::Heap* heap, InternTable* intern)
    : heap_(heap),
      intern_(intern),
      instance_id_(g_next_linker_id.fetch_add(1, std::memory_order_relaxed)) {
  // 加固器 DEX 再生成：登记表退役旧 DexFile 时清 DexCache + 退类索引（docs 03 章 10.2）
  loaders_.set_invalidator([this](dex::DexFile* dex) { RetireDexFile(dex); });
}

void ClassLinker::AddBootDexFile(dex::DexFile* dex) {
  loaders_.AttachDex(loaders_.boot(), dex);
  GetOrCreateDexCache(dex);
}

void ClassLinker::AddAppDexFile(dex::DexFile* dex) {
  loaders_.AttachDex(loaders_.default_app(), dex);
  GetOrCreateDexCache(dex);
}

DexCache* ClassLinker::GetOrCreateDexCache(dex::DexFile* dex) {
  auto it = dex_caches_.find(dex);
  if (it != dex_caches_.end()) return it->second.get();
  auto cache = std::make_unique<DexCache>();
  cache->dex = dex;
  cache->types.resize(dex->NumTypeIds(), nullptr);
  cache->methods.resize(dex->NumMethodIds(), nullptr);
  cache->fields.resize(dex->NumFieldIds(), nullptr);
  cache->strings.resize(dex->NumStringIds(), nullptr);
  DexCache* raw = cache.get();
  dex_caches_[dex] = std::move(cache);
  return raw;
}

void ClassLinker::RegisterBuiltinClass(VClass* klass) {
  klass->status = ClassStatus::kInitialized;
  class_table_[klass->descriptor] = klass;
  all_classes_.push_back(klass);
}

void ClassLinker::AddPendingBuiltinInterface(std::string owner_desc, std::string iface_desc) {
  pending_builtin_ifaces_.emplace_back(std::move(owner_desc), std::move(iface_desc));
}

void ClassLinker::AttachPendingBuiltinInterfaces(VClass* iface) {
  if (iface == nullptr || pending_builtin_ifaces_.empty()) return;
  for (const auto& [owner_desc, iface_desc] : pending_builtin_ifaces_) {
    if (iface_desc != iface->descriptor) continue;
    auto it = class_table_.find(owner_desc);
    if (it == class_table_.end()) continue;
    VClass* owner = it->second;
    if (owner == nullptr) continue;
    if (std::find(owner->interfaces.begin(), owner->interfaces.end(), iface) ==
        owner->interfaces.end()) {
      owner->interfaces.push_back(iface);
    }
  }
}

namespace {

// encoded_value 跳过（dex encoded_value 格式；type=低 5 位、arg=高 3 位）。
// 用于 annotation 元素遍历——除 Signature.value 外的值不解析只跨过。
bool SkipEncodedValue(const uint8_t** pp, const uint8_t* end, uint8_t type, uint8_t arg) {
  const uint8_t* p = *pp;
  switch (type) {
    case 0x00:  // VALUE_BYTE
    case 0x02:  // VALUE_SHORT
    case 0x03:  // VALUE_CHAR
    case 0x04:  // VALUE_INT
    case 0x06:  // VALUE_LONG
    case 0x10:  // VALUE_FLOAT
    case 0x11: {  // VALUE_DOUBLE
      size_t n = static_cast<size_t>(arg) + 1;
      if (p + n > end) return false;
      p += n;
      break;
    }
    case 0x15:  // VALUE_METHOD_TYPE
    case 0x16:  // VALUE_METHOD_HANDLE
    case 0x17:  // VALUE_STRING
    case 0x18:  // VALUE_TYPE
    case 0x19:  // VALUE_FIELD
    case 0x1e:  // VALUE_ENUM?（0x1b=BOOLEAN；0x1e/0x1f 兼容旧位型）
    case 0x1f:
    case 0x20:  // VALUE_FIELD
    case 0x21:  // VALUE_METHOD
    case 0x22: {  // VALUE_ENUM
      uint32_t v = 0;
      if (!dex::ReadUleb128(&p, end, &v)) return false;
      break;
    }
    case 0x1a:  // VALUE_NULL
    case 0x1b:  // VALUE_BOOLEAN（值在 type 高位，无载荷）
      break;
    case 0x1c: {  // VALUE_ARRAY
      uint32_t n = 0;
      if (!dex::ReadUleb128(&p, end, &n)) return false;
      for (uint32_t i = 0; i < n; i++) {
        if (p >= end) return false;
        uint8_t hb = *p++;
        if (!SkipEncodedValue(&p, end, hb & 0x1f, hb >> 5)) return false;
      }
      break;
    }
    case 0x1d: {  // VALUE_ANNOTATION
      uint32_t t = 0, n = 0;
      if (!dex::ReadUleb128(&p, end, &t)) return false;
      if (!dex::ReadUleb128(&p, end, &n)) return false;
      for (uint32_t i = 0; i < n; i++) {
        uint32_t name_idx = 0;
        if (!dex::ReadUleb128(&p, end, &name_idx)) return false;
        if (p >= end) return false;
        uint8_t hb = *p++;
        if (!SkipEncodedValue(&p, end, hb & 0x1f, hb >> 5)) return false;
      }
      break;
    }
    default:
      return false;
  }
  *pp = p;
  return true;
}

}  // namespace

bool ClassLinker::GetClassGenericSignature(VClass* klass, std::string* out) {
  if (klass == nullptr || out == nullptr) return false;
  if (klass->dex_file == nullptr || klass->class_def_idx >= klass->dex_file->NumClassDefs()) {
    return false;
  }
  dex::DexFile* dex = klass->dex_file;
  const dex::ClassDef& cd = dex->GetClassDef(klass->class_def_idx);
  if (cd.annotations_off == 0 || cd.annotations_off >= dex->size()) return false;
  const uint8_t* base = dex->data();
  const uint8_t* end = base + dex->size();

  // annotations_directory_item：4×u32（class_annotations_off/fields_size/
  // annotated_methods_size/annotated_parameters_size）+ 三组 8 字节项。
  const uint8_t* p = base + cd.annotations_off;
  auto rd_u32 = [&](uint32_t* o) {
    if (p + 4 > end) return false;
    std::memcpy(o, p, 4);
    p += 4;
    return true;
  };
  uint32_t class_ann_off = 0, fields_size = 0, methods_size = 0, params_size = 0;
  if (!rd_u32(&class_ann_off) || !rd_u32(&fields_size) || !rd_u32(&methods_size) ||
      !rd_u32(&params_size)) {
    return false;
  }
  p += (static_cast<size_t>(fields_size) + methods_size + params_size) * 8;
  if (p > end || class_ann_off == 0 || class_ann_off >= dex->size()) return false;

  // annotation_set_item：u32 size + u32 offsets[size]
  const uint8_t* as = base + class_ann_off;
  if (as + 4 > end) return false;
  uint32_t asize = 0;
  std::memcpy(&asize, as, 4);
  if (as + 4 + static_cast<size_t>(asize) * 4 > end) return false;

  for (uint32_t i = 0; i < asize; i++) {
    uint32_t aoff = 0;
    std::memcpy(&aoff, as + 4 + 4 * i, 4);
    if (aoff == 0 || aoff >= dex->size()) continue;
    const uint8_t* ai = base + aoff;
    if (ai + 1 > end) continue;
    ai++;  // visibility
    uint32_t type_idx = 0, n = 0;
    if (!dex::ReadUleb128(&ai, end, &type_idx)) continue;
    if (!dex::ReadUleb128(&ai, end, &n)) continue;
    if (dex->GetTypeDescriptor(type_idx) != "Ldalvik/annotation/Signature;") continue;
    for (uint32_t e = 0; e < n; e++) {
      uint32_t name_idx = 0;
      if (!dex::ReadUleb128(&ai, end, &name_idx)) return false;
      if (ai >= end) return false;
      uint8_t hb = *ai++;
      if ((hb & 0x1f) != 0x1c) {  // 非 VALUE_ARRAY 跳过
        if (!SkipEncodedValue(&ai, end, hb & 0x1f, hb >> 5)) return false;
        continue;
      }
      uint32_t parts = 0;
      if (!dex::ReadUleb128(&ai, end, &parts)) return false;
      std::string sig;
      for (uint32_t k = 0; k < parts; k++) {
        if (ai >= end) return false;
        uint8_t st = *ai++;
        if ((st & 0x1f) != 0x17) {  // 非 VALUE_STRING 片段:签名元素退化失败
          return false;
        }
        uint32_t sidx = 0;
        if (!dex::ReadUleb128(&ai, end, &sidx)) return false;
        sig += dex->GetString(sidx);
      }
      if (!sig.empty()) {
        *out = std::move(sig);
        return true;
      }
    }
    return false;
  }
  return false;
}

VClass* ClassLinker::PrimitiveClass(char type) {
  auto it = primitive_classes_.find(type);
  if (it != primitive_classes_.end()) return it->second;
  VClass* k = reinterpret_cast<VClass*>(heap_->Alloc(sizeof(VClass)));
  if (k == nullptr) return nullptr;
  new (k) VClass();
  k->descriptor = std::string(1, type);
  k->is_primitive = true;
  k->primitive_type = type;
  k->object_size = sizeof(ObjHeader);
  k->status = ClassStatus::kInitialized;
  primitive_classes_[type] = k;
  all_classes_.push_back(k);
  return k;
}

VClass* ClassLinker::FindArrayClass(VClass* component) {
  std::string desc = "[" + component->descriptor;
  auto it = array_classes_.find(desc);
  if (it != array_classes_.end()) return it->second;
  VClass* k = reinterpret_cast<VClass*>(heap_->Alloc(sizeof(VClass)));
  if (k == nullptr) return nullptr;
  new (k) VClass();
  k->descriptor = desc;
  k->is_array = true;
  k->component_type = component;
  k->class_loader = component->class_loader;
  k->object_size = sizeof(VArray);
  k->status = ClassStatus::kInitialized;
  // 数组 super = java.lang.Object（若可用）
  auto obj_it = class_table_.find("Ljava/lang/Object;");
  if (obj_it != class_table_.end()) k->super_class = obj_it->second;
  // 类镜像面：klass 槽挂 class_class（Class.getName/toString/instanceof 面
  // ——镜像即对象,checkcast/接口派发按 java.lang.Class 实例走正常路径）
  auto cls_it = class_table_.find("Ljava/lang/Class;");
  if (cls_it != class_table_.end()) k->klass = cls_it->second;
  array_classes_[desc] = k;
  all_classes_.push_back(k);
  return k;
}

VClass* ClassLinker::FindClass(std::string_view descriptor) {
  return FindClassImpl(nullptr, nullptr, descriptor);
}

VClass* ClassLinker::FindClass(VObject* loader, std::string_view descriptor) {
  return FindClassImpl(nullptr, loader, descriptor);
}

VClass* ClassLinker::FindClass(VMState* state, VObject* loader, std::string_view descriptor) {
  return FindClassImpl(state, loader, descriptor);
}

VClass* ClassLinker::FindClassImpl(VMState* state, VObject* loader, std::string_view descriptor) {
  std::string desc(descriptor);
#if defined(__ANDROID__)
  FindClassTimer find_timer(desc.c_str());
#endif
  // 1. ClassTable（含内置类）
  auto it = class_table_.find(desc);
  if (it != class_table_.end()) return it->second;
  // 2. 基本类型/数组
  if (desc.size() == 1) {
    char c = desc[0];
    if (strchr("VZBSCIJFD", c) != nullptr) return PrimitiveClass(c);
  }
  if (!desc.empty() && desc[0] == '[') {
    // 递归找元素类
    VClass* elem = FindClassImpl(state, loader, std::string_view(desc).substr(1));
    return elem == nullptr ? nullptr : FindArrayClass(elem);
  }
  // 3. 双亲委派：boot（bootstrap）优先。任意 loader（含壳 DexClassLoader）
  // 解析 java.* / 基础类时必须先查 boot 链，不能落到 guest loadClass 回落
  // （实测壳 loader 对 boot 类抛空消息 NoClassDefFoundError，导致 HashMap
  // 等 boot 类从 app dex 首次解析失败——OkHttp CipherSuite.<clinit> 实锤）。
  if (loaders_.boot() != nullptr) {
    if (VClass* k = FindClassInLoaderChain(loaders_.boot(), desc)) return k;
  }
  if (loader == nullptr) {
    // M1 兼容：null loader = boot→app 两级（FindClass(desc) 等价 boot loader 查询）
    if (VClass* k = FindClassInLoaderChain(loaders_.default_app(), desc)) return k;
  } else {
    session::LoaderState* st = loaders_.Lookup(loader);
    if (st != nullptr) {
      if (VClass* k = FindClassInLoaderChain(st, desc)) return k;
    }
    // 4. Java loadClass 回落（docs 12 章 22.1）
    if (VClass* k = FindClassViaJavaLoadClass(state, loader, desc)) return k;
    if (state != nullptr && state->HasPendingException()) return nullptr;
  }
  last_error_ = "class not found: " + desc;
#if defined(__ANDROID__)
  // 定位 loader 链缺 dex 的诊断（低量）：descriptor 在 default_app_/boot_ 的
  // 可见性 + 请求 loader 的链状态。
  if (FirstFindMiss(desc)) {
    session::LoaderState* st = loaders_.Lookup(loader);
    dex::DexFile* tmp = nullptr;
    int in_app = loaders_.FindClassDef(loaders_.default_app(), desc, &tmp);
    int in_boot = loaders_.FindClassDef(loaders_.boot(), desc, &tmp);
    __android_log_print(ANDROID_LOG_INFO, "DexVmFindMiss",
                        "%s loader=%s st=%p parent=%p dexes=%zu | default_app=%d boot=%d",
                        desc.c_str(),
                        loader != nullptr && loader->klass != nullptr
                            ? loader->klass->descriptor.c_str()
                            : "-",
                        static_cast<void*>(st), st != nullptr ? static_cast<void*>(st->parent)
                                                              : nullptr,
                        st != nullptr ? st->dex_files.size() : 0, in_app, in_boot);
  }
#endif
  return nullptr;
}

VClass* ClassLinker::FindClassInLoaderChain(session::LoaderState* state,
                                            std::string_view descriptor) {
  std::string desc(descriptor);
  dex::DexFile* dex = nullptr;
  uint32_t idx = 0;
  session::LoaderState* owner = nullptr;
  if (!loaders_.FindInChain(state, desc, &dex, &idx, &owner)) return nullptr;
  VClass* k = DefineClassImpl(nullptr, dex, idx, owner != nullptr ? owner->loader_object : nullptr);
#if defined(__ANDROID__)
  if (k == nullptr) {
    // 定义失败归因（最后一个抛出点）：链命中 dex/class_def 却定义失败，
    // 后续 Java loadClass 回落抛 CNFE 会覆盖 last_error_——在此固化坐标。
    __android_log_print(ANDROID_LOG_INFO, "DexVmDefineFail", "%s (in %s): %s", desc.c_str(),
                        dex != nullptr && owner != nullptr && owner->loader_object != nullptr
                            ? "loader"
                            : "-",
                        last_error_.c_str());
  }
#endif
  return k;
}

VClass* ClassLinker::FindClassViaJavaLoadClass(VMState* state, VObject* loader,
                                               const std::string& desc) {
  // 对照 class_linker.cc:3392-3399（FindClassViaClassLoader）：
  //   链上不认识 → 经解释器调用 Java 侧 loadClass(String)；
  //   broken loader 返回 null → 抛 NPE（Dalvik 兼容）；
  //   返回类 descriptor 不匹配 → NoClassDefFoundError；
  //   race-safe 插入（M1 单线程：查重复用）。
  if (fallback_depth_ > 0) return nullptr;  // 递归保护（loadClass 内再查找不回落）
  if (loader == nullptr || loader->klass == nullptr) return nullptr;

  VMethod probe;
  probe.name = "loadClass";
  probe.param_types = {"Ljava/lang/String;"};
  probe.return_type = "Ljava/lang/Class;";
  VMethod* m = loader->klass->FindInterfaceImpl(&probe);
  if (m == nullptr) {
    last_error_ = "loadClass: no ClassLoader.loadClass on " + loader->klass->descriptor;
    return nullptr;
  }

  VMState local;
  VMState* st = state;
  if (st == nullptr) {
    // 无 state 降级：临时 VMState（异常降级为 last_error_，不传递）
    local.heap = heap_;
    local.intern = intern_;
    local.linker = this;
    st = &local;
  }

  std::string dot = session::DescriptorToDot(desc);
  std::u16string udot(dot.begin(), dot.end());
  VString* name = AllocString(udot);
  if (name == nullptr) {
    st->ThrowNew("Ljava/lang/OutOfMemoryError;", "loadClass name alloc failed");
    return nullptr;
  }
  Value args[2] = {};
  args[0].ref = loader;
  args[1].ref = name;
  Value result{};
  fallback_depth_++;
  bool ok = InvokeMethod(*st, m, args, 2, &result);
  fallback_depth_--;
  if (!ok) {
    // loadClass 抛异常：传递（state 挂 pending_exception）；降级路径记错误。
    // 归因：把抛出异常的类型+message 并入 last_error（壳 DexClassLoader 内部
    // 失败链只能由此向下追）。
    std::string why;
    if (st->pending_exception != nullptr && st->pending_exception->klass != nullptr) {
      VClass* ek = st->pending_exception->klass;
      why = ek->descriptor;
      for (VClass* c = ek; c != nullptr && why.find(": ") == std::string::npos;
           c = c->super_class) {
        for (auto& f : c->instance_fields) {
          if (f.name != "detailMessage") continue;
          ObjHeader* msg = nullptr;
          std::memcpy(&msg,
                      reinterpret_cast<VObject*>(st->pending_exception)->field_data() +
                          f.offset,
                      sizeof(msg));
          if (msg != nullptr && msg->klass != nullptr) {
            why += ": " + static_cast<VString*>(msg)->ToModifiedUtf8();
          }
          break;
        }
      }
    }
    last_error_ = "loadClass: Java loadClass threw for " + dot +
                  (why.empty() ? "" : ": " + why);
#if defined(__ANDROID__)
    if (FirstFindMiss(desc)) {
      session::LoaderState* lst = loaders_.Lookup(loader);
      dex::DexFile* tmp = nullptr;
      int in_self = loaders_.FindClassDef(lst, desc, &tmp);
      int in_app = loaders_.FindClassDef(loaders_.default_app(), desc, &tmp);
      int in_boot = loaders_.FindClassDef(loaders_.boot(), desc, &tmp);
      session::LoaderState* par = lst != nullptr ? lst->parent : nullptr;
      int in_parent = par != nullptr ? loaders_.FindClassDef(par, desc, &tmp) : -2;
      __android_log_print(
          ANDROID_LOG_INFO, "DexVmFindMiss",
          "%s loader=%s st=%p self=%d dexes=%zu parent=%p parent_self=%d | app=%d boot=%d",
          desc.c_str(),
          loader != nullptr && loader->klass != nullptr ? loader->klass->descriptor.c_str()
                                                        : "-",
          static_cast<void*>(lst), in_self, lst != nullptr ? lst->dex_files.size() : 0,
          static_cast<void*>(par), in_parent, in_app, in_boot);
    }
#endif
    return nullptr;
  }
  VClass* got = reinterpret_cast<VClass*>(result.ref);
  if (got == nullptr) {
    st->ThrowNew("Ljava/lang/NullPointerException;",
                 "ClassLoader.loadClass returned null for " + dot);
    last_error_ = "loadClass: broken loader returned null for " + dot;
    return nullptr;
  }
  if (got->descriptor != desc) {
    st->ThrowNew("Ljava/lang/NoClassDefFoundError;",
                 "loadClass returned " + got->descriptor + " instead of " + desc);
    last_error_ = "loadClass: descriptor mismatch for " + dot;
    return nullptr;
  }
  // race-safe 插入：输掉竞争复用胜者（M1 单线程：查重）
  auto it = class_table_.find(desc);
  if (it != class_table_.end()) return it->second;
  class_table_[desc] = got;
  AttachPendingBuiltinInterfaces(got);
  return got;
}

VClass* ClassLinker::DefineClass(dex::DexFile* dex, uint32_t class_def_idx) {
  session::LoaderState* st = loaders_.GetDexLoader(dex);
  return DefineClassImpl(nullptr, dex, class_def_idx,
                         st != nullptr ? st->loader_object : nullptr);
}

VClass* ClassLinker::DefineClass(dex::DexFile* dex, uint32_t class_def_idx, VObject* loader) {
  return DefineClassImpl(nullptr, dex, class_def_idx, loader);
}

VClass* ClassLinker::DefineClassImpl(VMState* state, dex::DexFile* dex, uint32_t class_def_idx,
                                     VObject* loader) {
  (void)state;
  std::string error;
  const dex::ClassDef& def = dex->GetClassDef(class_def_idx);
  std::string descriptor(dex->GetTypeDescriptor(def.class_idx));
  // 竞争语义 M1：单线程，直接查重（race-safe insert 简化保留：输掉竞争复用胜者）
  auto it = class_table_.find(descriptor);
  if (it != class_table_.end()) return it->second;

  VClass* klass = reinterpret_cast<VClass*>(heap_->Alloc(sizeof(VClass)));
  if (klass == nullptr) {
    last_error_ = "oom: class " + descriptor;
    return nullptr;
  }
  new (klass) VClass();
  klass->descriptor = descriptor;
  klass->access_flags = def.access_flags;
  klass->dex_file = dex;
  klass->class_def_idx = class_def_idx;
  klass->class_loader = loader;  // M2：登记 class_loader 元数据
  klass->status = ClassStatus::kIdx;
  // 类镜像面（存量缺口根因修复）：VClass 自身即对象——klass 槽挂 class_class，
  // 镜像可走正常 checkcast/instanceof/toString/接口派发（Class.getName 等）。
  // 此前仅 builtin 类镜像挂载，dex 定义类镜像靠解释器 M4.3 回落兜底
  //（ObjectToString/值语义面仍失败——generics 详情拼接 append(mirror) 挂死面）。
  {
    auto cls_it = class_table_.find("Ljava/lang/Class;");
    if (cls_it != class_table_.end()) klass->klass = cls_it->second;
  }

  dex::ClassData cdata;
  if (def.class_data_off != 0) {
    if (!dex->GetClassData(class_def_idx, &cdata, &error)) {
      last_error_ = error;
      return nullptr;
    }
  }

  // LoadClass：字段/方法元数据
  for (const auto& f : cdata.static_fields) {
    const dex::FieldId& fid = dex->GetFieldId(f.field_idx);
    VField vf;
    vf.declaring = klass;
    vf.name = dex->GetString(fid.name_idx);
    vf.type_descriptor = dex->GetTypeDescriptor(fid.type_idx);
    vf.access_flags = f.access_flags;
    vf.dex_file = dex;
    vf.field_idx = f.field_idx;
    vf.is_static = true;
    klass->static_fields.push_back(std::move(vf));
  }
  for (const auto& f : cdata.instance_fields) {
    const dex::FieldId& fid = dex->GetFieldId(f.field_idx);
    VField vf;
    vf.declaring = klass;
    vf.name = dex->GetString(fid.name_idx);
    vf.type_descriptor = dex->GetTypeDescriptor(fid.type_idx);
    vf.access_flags = f.access_flags;
    vf.dex_file = dex;
    vf.field_idx = f.field_idx;
    vf.is_static = false;
    klass->instance_fields.push_back(std::move(vf));
  }
  auto load_method = [&](const dex::EncodedMethod& em, std::vector<VMethod>* out) {
    const dex::MethodId& mid = dex->GetMethodId(em.method_idx);
    VMethod vm;
    vm.declaring = klass;
    vm.name = dex->GetString(mid.name_idx);
    FillProto(dex, mid.proto_idx, &vm);
    vm.access_flags = em.access_flags;
    vm.dex_file = dex;
    vm.method_idx = em.method_idx;
    if (em.code_off != 0) {
      vm.code = dex->GetCodeItem(em.code_off, &error);
      if (vm.code == nullptr) {
        last_error_ = error;
        return false;
      }
    }
    out->push_back(std::move(vm));
    return true;
  };
  for (const auto& em : cdata.direct_methods)
    if (!load_method(em, &klass->direct_methods)) return nullptr;
  for (const auto& em : cdata.virtual_methods)
    if (!load_method(em, &klass->virtual_methods)) return nullptr;

  // java.lang.* 标准 native（Float/Double 位转换等）绑 intrinsic：vendored lib 类的
  // native 方法无 JNI 注册，这里按登记表挂 VM 实现（否则 UnsatisfiedLinkError）。
  BindWellKnownNatives(klass);

  class_table_[descriptor] = klass;
  all_classes_.push_back(klass);
  klass->status = ClassStatus::kLoaded;
  // P0 翻转：接口类由 runtime.dex 提供——定义即补挂内建 owner 的接口向量
  AttachPendingBuiltinInterfaces(klass);

  // LoadSuperAndInterfaces
  // 定义失败即摘除类表条目（ART ERRONEOUS 语义的落地面：残骸类不得被后续
  // FindClass 命中）。此前残骸留在 class_table_（status=kLoaded、object_size=0），
  // FindClass 步骤 1 直返该残骸 → AllocObject(0) → Heap::Alloc(0) bump 到
  // region 末端 → new (mem) VObject() 写 [base+size] 越界 → SIGSEGV
  // （BottomNav/PAGView inflate 链实证）。摘除后同类下次解析可重试（壳可能随后
  // 加载到含缺失超类的 dex）。
  auto unlink_failed_definition = [&]() { class_table_.erase(descriptor); };
  if (def.superclass_idx != 0xffffffffu) {
    klass->super_class = ResolveType(dex, def.superclass_idx);
    if (klass->super_class == nullptr) {
      last_error_ = "super not found for " + descriptor;
      unlink_failed_definition();
      return nullptr;
    }
  }
  uint32_t niface = dex->GetInterfaceCount(class_def_idx);
  for (uint32_t i = 0; i < niface; i++) {
    uint32_t iface_idx = dex->GetInterfaceTypeIdx(class_def_idx, i);
    VClass* iface = ResolveType(dex, iface_idx);
    if (iface == nullptr) {
      // 归因：接口解析失败此前静默返回、复用上一次残留 last_error（实证：
      // androidx.fragment.app.Fragment 报成无关的 VectorDrawableCompat）。
      last_error_ = "interface not found: " +
                    std::string(dex->GetTypeDescriptor(iface_idx)) + " for " + descriptor;
      unlink_failed_definition();
      return nullptr;
    }
    klass->interfaces.push_back(iface);
  }

  if (!LinkClass(klass)) {
    unlink_failed_definition();
    return nullptr;
  }
  return klass;
}

bool ClassLinker::LinkClass(VClass* klass) {
  // 超类必须已完成布局（status >= kResolved 且 object_size 已赋值）：残骸超类
  // 会让本节从 off=0 续排，子类字段与超类字段重叠（堆混型），且子类
  // object_size 建立在残缺布局上。
  if (klass->super_class != nullptr &&
      (klass->super_class->status < ClassStatus::kResolved ||
       klass->super_class->object_size < sizeof(ObjHeader))) {
    last_error_ = "super not linked: " + klass->super_class->descriptor + " for " +
                  klass->descriptor;
    return false;
  }
  // 字段布局：从父类对象尾部续排（docs 03 章 11.3）；reference 优先分组留待后置
  uint32_t off = 0;
  if (klass->super_class != nullptr) {
    uint32_t super_size = klass->super_class->object_size;
    if (super_size >= sizeof(ObjHeader)) off = super_size - static_cast<uint32_t>(sizeof(ObjHeader));
  }
  for (auto& f : klass->instance_fields) {
    f.offset = off;
    off += FieldSlotSize(f.type_descriptor);
  }
  klass->object_size = sizeof(ObjHeader) + off;

  uint32_t slot = 0;
  for (auto& f : klass->static_fields) f.offset = slot++;
  // JLS 2.12 默认值语义：static 字段零初始化。注意 `Value{}` 花括号聚合初始化
  // 只清首成员（4 字节），高 4 字节不确定——必须显式全宽清零。
  Value zero;
  std::memset(&zero, 0, sizeof(zero));
  klass->static_storage.assign(klass->static_fields.size(), zero);

  // vtable：复制父表，同签名覆盖，新方法追加（docs 03 章 11.3）
  klass->vtable.clear();
  if (klass->super_class != nullptr) klass->vtable = klass->super_class->vtable;
  for (auto& m : klass->virtual_methods) {
    bool replaced = false;
    for (VMethod*& slot_m : klass->vtable) {
      if (SameSignature(*slot_m, m)) {
        slot_m = &m;
        replaced = true;
        break;
      }
    }
    if (!replaced) klass->vtable.push_back(&m);
  }

  klass->status = ClassStatus::kResolved;
  return true;
}

VClass* ClassLinker::ResolveType(dex::DexFile* dex, uint32_t type_idx, VMState* state) {
  DexCache* cache = GetOrCreateDexCache(dex);
  if (cache->types[type_idx] != nullptr) return cache->types[type_idx];
  // 借 dex 归属 loader 做委派查找（ART：DexCache 按 ClassLoader 注册）
  session::LoaderState* st = loaders_.GetDexLoader(dex);
  VObject* loader = st != nullptr ? st->loader_object : nullptr;
  VClass* k = FindClassImpl(state, loader, dex->GetTypeDescriptor(type_idx));
  if (k != nullptr) cache->types[type_idx] = k;
  return k;
}

VString* ClassLinker::ResolveString(dex::DexFile* dex, uint32_t string_idx) {
  DexCache* cache = GetOrCreateDexCache(dex);
  if (cache->strings[string_idx] != nullptr) return cache->strings[string_idx];
  const std::string& mutf8 = dex->GetString(string_idx);
  // MUTF-8 -> UTF-16（内嵌 \0 需按字节展开；这里按 DEX 字符串语义）
  std::u16string chars;
  chars.reserve(mutf8.size());
  for (size_t i = 0; i < mutf8.size();) {
    unsigned char c = mutf8[i];
    unsigned char c1 = i + 1 < mutf8.size() ? static_cast<unsigned char>(mutf8[i + 1]) : 0;
    if (c == 0xC0 && c1 == 0x80) {
      chars.push_back(0);  // MUTF-8 内嵌 \0
      i += 2;
    } else if (c < 0x80) {
      chars.push_back(c);
      i += 1;
    } else if ((c & 0xE0) == 0xC0) {
      chars.push_back(static_cast<char16_t>(((c & 0x1F) << 6) | (c1 & 0x3F)));
      i += 2;
    } else {
      chars.push_back(static_cast<char16_t>(((c & 0x0F) << 12) | ((c1 & 0x3F) << 6) |
                                            (static_cast<unsigned char>(mutf8[i + 2]) & 0x3F)));
      i += 3;
    }
  }
  VString* interned = intern_->Lookup(chars);
  if (interned == nullptr) {
    interned = AllocString(chars);
    if (interned == nullptr) return nullptr;
    intern_->Intern(interned);
  }
  cache->strings[string_idx] = interned;
  return interned;
}

VMethod* ClassLinker::ResolveMethod(dex::DexFile* dex, uint32_t method_idx) {
  DexCache* cache = GetOrCreateDexCache(dex);
  if (cache->methods[method_idx] != nullptr) return cache->methods[method_idx];
  const dex::MethodId& mid = dex->GetMethodId(method_idx);
  VClass* klass = ResolveType(dex, mid.class_idx);
  if (klass == nullptr) return nullptr;

  VMethod target;
  target.name = dex->GetString(mid.name_idx);
  FillProto(dex, mid.proto_idx, &target);

  VMethod* found = nullptr;
  for (VClass* k = klass; k != nullptr && found == nullptr; k = k->super_class) {
    for (auto& m : k->direct_methods)
      if (SameSignature(m, target)) {
        found = &m;
        break;
      }
    if (found == nullptr) {
      for (auto& m : k->virtual_methods)
        if (SameSignature(m, target)) {
          found = &m;
          break;
        }
    }
    if (found == nullptr && k->IsInterface()) {
      for (auto& m : k->virtual_methods)
        if (SameSignature(m, target)) {
          found = &m;
          break;
        }
    }
  }
  // super 链未命中：接口闭包回落（JLS 方法解析含超接口，见
  // FindInInterfaceClosure 注释）
  if (found == nullptr) found = FindInInterfaceClosure(klass, target);
  if (found != nullptr) cache->methods[method_idx] = found;
  else last_error_ = "method not found: " + target.name;
  return found;
}

VField* ClassLinker::ResolveField(dex::DexFile* dex, uint32_t field_idx) {
  DexCache* cache = GetOrCreateDexCache(dex);
  if (cache->fields[field_idx] != nullptr) return cache->fields[field_idx];
  const dex::FieldId& fid = dex->GetFieldId(field_idx);
  VClass* klass = ResolveType(dex, fid.class_idx);
  if (klass == nullptr) return nullptr;
  std::string name = dex->GetString(fid.name_idx);
  std::string type(dex->GetTypeDescriptor(fid.type_idx));
  VField* found = nullptr;
  for (VClass* k = klass; k != nullptr && found == nullptr; k = k->super_class) {
    for (auto& f : k->static_fields)
      if (f.name == name && f.type_descriptor == type) {
        found = &f;
        break;
      }
    if (found == nullptr) {
      for (auto& f : k->instance_fields)
        if (f.name == name && f.type_descriptor == type) {
          found = &f;
          break;
        }
    }
  }
  // super 链未命中：接口闭包回落（JLS 5.4.3.2 字段解析含超接口，见
  // FindFieldInInterfaceClosure 注释——PrettyPrinter.DEFAULT_SEPARATORS 形）
  if (found == nullptr) found = FindFieldInInterfaceClosure(klass, name, type);
  if (found != nullptr) cache->fields[field_idx] = found;
  else last_error_ = "field not found: " + name;
  return found;
}

namespace {

// <clinit> 完成谓词（跨任务等待；pred 在调度器锁内调用，不得回调调度器接口）
bool ClinitSettled(void* arg) {
  return static_cast<const VClass*>(arg)->status != ClassStatus::kInitializing;
}

}  // namespace

void ClassLinker::FinishClinit(VClass* klass) {
  clinit_owners_.erase(klass);
  auto it = clinit_waiters_.find(klass);
  if (it == clinit_waiters_.end()) return;
  std::vector<void*> waiters;
  waiters.swap(it->second);
  clinit_waiters_.erase(it);
  const GuestSchedOps* sched = GuestSched();
  if (sched == nullptr) return;
  for (void* w : waiters) sched->wake(w, kGuestWaitJoin);
}

bool ClassLinker::InitializeClass(VClass* klass, VMState* state) {
  if (klass == nullptr) return false;
  if (klass->status == ClassStatus::kInitialized) return true;

  const GuestSchedOps* sched = GuestSched();
  void* me = sched != nullptr ? sched->current_task() : nullptr;
  const bool dbg_clinit = klass->descriptor.find("US_ASCII") != std::string::npos;

  if (klass->status == ClassStatus::kInitializing) {
    void* owner = nullptr;
    auto own = clinit_owners_.find(klass);
    if (own != clinit_owners_.end()) owner = own->second;
    // 同任务递归进入（clinit 内引用本类）直通（docs 03 章 12 重入语义）；
    // 无任务上下文（旧单流宿主）同口径。
    if (dbg_clinit && (klass->descriptor.find("US_ASCII") != std::string::npos)) {
      CLINIT_LOG("REENTER %s me=%p owner=%p same=%d tid=%ld", klass->descriptor.c_str(), me,
                 owner, me == owner ? 1 : 0, static_cast<long>(gettid()));
    }
    if (me == nullptr || owner == nullptr || me == owner) return true;
    // 跨任务进入（JLS 12.4.2）：阻塞至初始任务完成——半初始化静态面不可
    // 观测（抢占交错下半建 methodNameToAccessMode 查表失败即此根因）。
    clinit_waiters_[klass].push_back(me);
    while (klass->status == ClassStatus::kInitializing) {
      sched->wait(kGuestWaitJoin, -1, ClinitSettled, klass);
    }
    auto wit = clinit_waiters_.find(klass);
    if (wit != clinit_waiters_.end()) {
      std::vector<void*>& waiters = wit->second;
      for (size_t i = 0; i < waiters.size(); i++) {
        if (waiters[i] == me) {
          waiters.erase(waiters.begin() + static_cast<long>(i));
          break;
        }
      }
      if (waiters.empty()) clinit_waiters_.erase(wit);
    }
    return klass->status == ClassStatus::kInitialized;
  }

  // 认领初始化（状态 + owner 同刻登记；跨任务首触竞态窗口在收口内）
  klass->status = ClassStatus::kInitializing;
  if (me != nullptr) clinit_owners_[klass] = me;

  // 先初始化父类（JLS 12.4.1；失败回退 kResolved 保持可重试语义）
  if (klass->super_class != nullptr && !InitializeClass(klass->super_class, state)) {
    klass->status = ClassStatus::kResolved;
    FinishClinit(klass);
    return false;
  }

  // static encoded values（docs 03 章 11.4）
  if (klass->dex_file != nullptr && klass->class_def_idx < klass->dex_file->NumClassDefs()) {
    const dex::ClassDef& def = klass->dex_file->GetClassDef(klass->class_def_idx);
    if (def.static_values_off != 0) {
      std::vector<dex::EncodedValue> values;
      std::string error;
      if (!klass->dex_file->GetStaticValues(klass->class_def_idx, &values, &error)) {
        last_error_ = error;
        klass->status = ClassStatus::kResolved;
        FinishClinit(klass);
        return false;
      }
      for (size_t i = 0; i < values.size() && i < klass->static_fields.size(); i++) {
        // 防御：static_storage 与 static_fields 失配（内建注册后追加字段/链接
        // 半途失败路径）时按零值扩容，绝不越界写（SEGV@0 事故根因）。
        if (i >= klass->static_storage.size()) {
          Value zero;
          std::memset(&zero, 0, sizeof(zero));
          klass->static_storage.resize(klass->static_fields.size(), zero);
        }
        Value& slot = klass->static_storage[i];
        const dex::EncodedValue& ev = values[i];
        switch (ev.kind) {
          case dex::EncodedValue::Kind::kInt:
          case dex::EncodedValue::Kind::kBoolean:
            slot.l = ev.int_value;
            break;
          case dex::EncodedValue::Kind::kLong:
            slot.l = ev.int_value;
            break;
          case dex::EncodedValue::Kind::kFloat:
            slot.l = static_cast<int64_t>(ev.bits);
            break;
          case dex::EncodedValue::Kind::kDouble:
            slot.l = static_cast<int64_t>(ev.bits);
            break;
          case dex::EncodedValue::Kind::kString:
            slot.ref = reinterpret_cast<ObjHeader*>(ResolveString(klass->dex_file, ev.idx));
            break;
          case dex::EncodedValue::Kind::kType:
            slot.ref = reinterpret_cast<ObjHeader*>(ResolveType(klass->dex_file, ev.idx));
            break;
          case dex::EncodedValue::Kind::kNull:
          case dex::EncodedValue::Kind::kZero:
            slot.l = 0;
            break;
        }
      }
    }
  }

  // <clinit>
  VMethod* clinit = nullptr;
  for (auto& m : klass->direct_methods) {
    if (m.name == "<clinit>") {
      clinit = &m;
      break;
    }
  }
  const bool dbg_trace = klass->descriptor.find("US_ASCII") != std::string::npos ||
                         klass->descriptor.find("ISO_8859_1") != std::string::npos;
  if (dbg_trace) {
    CLINIT_LOG("INIT %s tid=%ld clinit=%d hasCode=%d state=%d slots=%zu fields=%zu",
               klass->descriptor.c_str(), static_cast<long>(gettid()),
               clinit != nullptr ? 1 : 0, clinit != nullptr ? (clinit->HasCode() ? 1 : 0) : 0,
               state != nullptr ? 1 : 0, klass->static_storage.size(),
               klass->static_fields.size());
  }
  if (clinit != nullptr && clinit->HasCode() && state != nullptr) {
    Value result;
    if (!InvokeMethod(*state, clinit, nullptr, 0, &result)) {
      // <clinit> 异常：包装留给调用方（docs 03 章 12），这里记录 erroneous 语义
      klass->status = ClassStatus::kResolved;
      FinishClinit(klass);
      return false;
    }
  }

  if (dbg_trace) {
    uint64_t s0 = klass->static_storage.empty() ? 0 : static_cast<uint64_t>(klass->static_storage[0].l);
    CLINIT_LOG("CLINIT-DONE %s tid=%ld slots=%zu slot0=%llx", klass->descriptor.c_str(),
               static_cast<long>(gettid()),
               klass->static_storage.size(), static_cast<unsigned long long>(s0));
  }
  klass->status = ClassStatus::kInitialized;
  FinishClinit(klass);
  return true;
}

VObject* ClassLinker::AllocObject(VClass* klass) {
  // 未完成链接（status=kLoaded / 布局缺失 object_size）的类不得实例化：
  // ART 对 ERRONEOUS 类抛 NoClassDefFoundError；此处无 VMState，返回 nullptr
  // 由调用方按分配失败处理（NewView 等已判空）。残骸类曾以 object_size=0
  // 进入 Heap::Alloc → bump 到 region 末端写越界（SIGSEGV 实证）。
  if (klass == nullptr) return nullptr;
  if (klass->object_size < sizeof(ObjHeader) || klass->status == ClassStatus::kLoaded) {
    last_error_ = "alloc on unlinked class: " + klass->descriptor;
    return nullptr;
  }
  void* mem = heap_->Alloc(klass->object_size);
  if (mem == nullptr) return nullptr;
  VObject* obj = new (mem) VObject();
  // Java 语义：字段零初始化由 Heap::AllocRaw 在分配点完成（空闲链复用/arena
  // bump/新 region 三条路径都 memset 整块）。各 <init> intrinsic 不得再清
  // 整实例区——dex 允许在 invoke-direct super 之前写字段（合成 this$0），
  // 超级构造器整块清零会冲掉它（TabLayout$SlidingTabIndicator 实锤）。
  obj->klass = klass;
  return obj;
}

VArray* ClassLinker::AllocArray(VClass* array_class, int32_t length) {
  if (length < 0) return nullptr;
  size_t elem = 8;  // M1 统一 8 字节/元素（引用与宽值）
  size_t size = sizeof(VArray) + static_cast<size_t>(length) * elem;
  void* mem = heap_->Alloc(size);
  if (mem == nullptr) return nullptr;
  VArray* arr = new (mem) VArray();
  arr->klass = array_class;
  arr->length = length;
  return arr;
}

VString* ClassLinker::AllocString(std::u16string_view chars) {
  size_t size = sizeof(VString) + chars.size() * sizeof(uint16_t);
  void* mem = heap_->Alloc(size);
  if (mem == nullptr) return nullptr;
  VString* str = new (mem) VString();
  str->klass = nullptr;  // 由 VM 内置 String 类填充（见 builtins）
  auto it = class_table_.find("Ljava/lang/String;");
  if (it != class_table_.end()) str->klass = it->second;
  str->length = static_cast<int32_t>(chars.size());
  std::memcpy(str->chars(), chars.data(), chars.size() * sizeof(uint16_t));
  return str;
}

void ClassLinker::RetireDexFile(dex::DexFile* dex) {
  // docs 03 章 10.2：加固器再生成/close 路径退役旧 DexFile——
  // 清 DexCache、退类索引（class_table_ 条目移除，等新 DexFile 重建 DefineClass）。
  if (dex == nullptr) return;
  dex_caches_.erase(dex);
  for (VClass* k : all_classes_) {
    if (k->dex_file != dex) continue;
    auto it = class_table_.find(k->descriptor);
    if (it != class_table_.end() && it->second == k) class_table_.erase(it);
    k->status = ClassStatus::kNotReady;
  }
}

}  // namespace dexvm::runtime
