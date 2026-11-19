// ClassLinker / InternTable（语义对照 third_party/art/runtime/class_linker.cc）
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/vm_types.h"
#include "memory/heap.h"
#include "session/class_loader.h"

namespace dexvm::runtime {

struct VMState;

// 字符串驻留表
class InternTable {
 public:
  VString* Intern(VString* str);  // 已存在返回既有，否则登记返回自身
  VString* Lookup(std::u16string_view chars) const;
  const std::unordered_map<std::u16string, VString*>& entries() const { return table_; }

 private:
  std::unordered_map<std::u16string, VString*> table_;
};

// 每 DexFile 一个的解析缓存（DexCache 等价物；ART 按 ClassLoader 分 DexCache，
// M2 简化为每 DexFile 一个，跨 loader 冲突检测后置）
struct DexCache {
  dex::DexFile* dex = nullptr;
  std::vector<VClass*> types;
  std::vector<VMethod*> methods;
  std::vector<VField*> fields;
  std::vector<VString*> strings;
};

class ClassLinker {
 public:
  ClassLinker(mem::Heap* heap, InternTable* intern);

  // M1 兼容入口：boot（runtime-libs + 内置类）与 app（默认 loader）DexFile
  void AddBootDexFile(dex::DexFile* dex);
  void AddAppDexFile(dex::DexFile* dex);

  // 查找/定义（M2 真实 loader 面，docs 03 章 11.1 / 12 章 22.1）
  VClass* FindClass(std::string_view descriptor);  // = FindClass(nullptr, desc)：boot→app
  VClass* FindClass(VObject* loader, std::string_view descriptor);
  // 带 VMState：Java loadClass 回落的异常可传递到调用方 pending_exception
  VClass* FindClass(VMState* state, VObject* loader, std::string_view descriptor);
  VClass* DefineClass(dex::DexFile* dex, uint32_t class_def_idx);
  VClass* DefineClass(dex::DexFile* dex, uint32_t class_def_idx, VObject* loader);
  VClass* FindArrayClass(VClass* component);
  VClass* PrimitiveClass(char type);  // 'V','Z','B','S','C','I','J','F','D'

  // 原生委派链检索（parent 递归→自身 DexFile 列表顺序；无 Java 回落）
  VClass* FindClassInLoaderChain(session::LoaderState* state, std::string_view descriptor);

  // 解析（带 DexCache；ResolveType 借 dex 归属 loader 做委派查找）
  VClass* ResolveType(dex::DexFile* dex, uint32_t type_idx, VMState* state = nullptr);
  VMethod* ResolveMethod(dex::DexFile* dex, uint32_t method_idx);
  VField* ResolveField(dex::DexFile* dex, uint32_t field_idx);
  VString* ResolveString(dex::DexFile* dex, uint32_t string_idx);

  // 初始化（kInitializing/clinit/重入语义，docs 03 章 11.4）
  bool InitializeClass(VClass* klass, VMState* state = nullptr);

  // 注册内置类（java.lang.Object/String/Class 等由 VM 提供）
  void RegisterBuiltinClass(VClass* klass);

  // 内建类延迟接口解析（P0 双实现消除：CharSequence/Comparable/Serializable 等
  // 接口类改由 runtime.dex 提供，内建注册时接口 VClass 尚不存在）。登记
  // (owner 描述符, 接口描述符)；接口类首次定义时自动补挂到 owner 的 interfaces
  // 向量（IsAssignableFrom/FindInterfaceImpl 按指针判等，需要同一 VClass）。
  void AddPendingBuiltinInterface(std::string owner_desc, std::string iface_desc);

  // 类 Signature 属性（Ldalvik/annotation/Signature; class annotation）解析：
  // 拼接 annotation 的 value 字符串串数组为完整 JVMS 4.7.9.1 签名文本。
  // 成功取到签名返回 true；内建类/无签名属性/解析失败返回 false（调用方按
  // 「无泛型签名」退化语义处理）。只读 walk dex 原始字节，不改 dex/ 模块。
  bool GetClassGenericSignature(VClass* klass, std::string* out);

  // 分配（分配失败路径：GC 后重试，耗尽抛 OOM 由调用方转异常）
  VObject* AllocObject(VClass* klass);
  VArray* AllocArray(VClass* array_class, int32_t length);
  VString* AllocString(std::u16string_view chars);  // 已驻留

  // 加固器 DEX 再生成（docs 03 章 10.2）：旧 DexFile 退役——清 DexCache、
  // 退类索引（class_table_ 条目移除，等待新 DexFile 重新 DefineClass 重建）。
  void RetireDexFile(dex::DexFile* dex);

  mem::Heap* heap() { return heap_; }
  InternTable* intern_table() { return intern_; }
  session::LoaderRegistry& loaders() { return loaders_; }
  const std::vector<VClass*>& all_classes() const { return all_classes_; }

  // 实例身份（进程内单调递增；用于跨会话失效缓存——同进程重开 app 时
  // ClassLinker 对象地址可能复用，指针相等不足以判定同一 VM 实例）。
  uint64_t InstanceId() const { return instance_id_; }

  // 错误信息
  const std::string& last_error() const { return last_error_; }

 private:
  VClass* FindClassImpl(VMState* state, VObject* loader, std::string_view descriptor);
  // docs 12 章 22.1 / class_linker.cc:3392-3399：链上不认识时回落 Java loadClass
  VClass* FindClassViaJavaLoadClass(VMState* state, VObject* loader, const std::string& desc);
  VClass* DefineClassImpl(VMState* state, dex::DexFile* dex, uint32_t class_def_idx,
                          VObject* loader);
  VClass* LoadClass(dex::DexFile* dex, uint32_t class_def_idx);
  bool LinkClass(VClass* klass);
  DexCache* GetOrCreateDexCache(dex::DexFile* dex);
  bool EnsureResolved(VClass* klass);
  // 接口类定义后补挂内建 owner 的 interfaces（AddPendingBuiltinInterface 登记项）
  void AttachPendingBuiltinInterfaces(VClass* iface);
  // <clinit> 收尾：清 owner + 唤醒跨任务等待者（JLS 12.4.2 观测闸）
  void FinishClinit(VClass* klass);

  mem::Heap* heap_;
  InternTable* intern_;
  uint64_t instance_id_ = 0;
  std::string last_error_;
  session::LoaderRegistry loaders_;
  int fallback_depth_ = 0;  // Java loadClass 回落递归保护

  std::unordered_map<dex::DexFile*, std::unique_ptr<DexCache>> dex_caches_;
  std::unordered_map<std::string, VClass*> class_table_;          // descriptor -> class
  std::unordered_map<std::string, VClass*> array_classes_;        // "[X" -> class
  std::unordered_map<char, VClass*> primitive_classes_;
  std::vector<VClass*> all_classes_;  // root 扫描用
  // (owner 描述符, 接口描述符)：内建 String/StringBuilder 等的接口向量延迟补挂
  std::vector<std::pair<std::string, std::string>> pending_builtin_ifaces_;
  // <clinit> 跨任务观测闸（JLS 12.4.2，docs 03 章 12）：owner 任务句柄 +
  // 跨任务等待者表。单 carrier 单 OS 线程（coroutine 14.4 M1）：仅逻辑交错，
  // 无数据竞争。半初始化静态面不可观察（VarHandle$AccessMode 查表失败根因）。
  std::unordered_map<VClass*, void*> clinit_owners_;
  std::unordered_map<VClass*, std::vector<void*>> clinit_waiters_;
};

}  // namespace dexvm::runtime
