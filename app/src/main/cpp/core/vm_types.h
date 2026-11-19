// 对象模型（语义对照 third_party/art/runtime/mirror/）
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "dex/dex_file.h"

namespace dexvm::runtime {

class VClass;
struct VMethod;
struct VField;

// 对象头（docs 02 章 6.1：klass + monitor(LockWord)）
struct ObjHeader {
  VClass* klass = nullptr;
  uint32_t monitor = 0;  // M1：简单锁字；monitor 语义后置
};

// 普通对象：实例字段区紧随对象头之后（byte 偏移 = sizeof(ObjHeader)+field.offset）
struct VObject : ObjHeader {
  uint8_t* field_data() { return reinterpret_cast<uint8_t*>(this) + sizeof(ObjHeader); }
  const uint8_t* field_data() const {
    return reinterpret_cast<const uint8_t*>(this) + sizeof(ObjHeader);
  }
};

// 数组：元素区紧随 length 之后
struct VArray : ObjHeader {
  int32_t length = 0;
  uint8_t* data() { return reinterpret_cast<uint8_t*>(this) + sizeof(VArray); }
  const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(this) + sizeof(VArray); }
};

// String：UTF-16 chars 紧随 length 之后
struct VString : ObjHeader {
  int32_t length = 0;
  uint16_t* chars() { return reinterpret_cast<uint16_t*>(this) + (sizeof(VString) / 2); }
  const uint16_t* chars() const {
    return reinterpret_cast<const uint16_t*>(this) + (sizeof(VString) / 2);
  }
  std::string ToModifiedUtf8() const;
};

// 字段值槽（static storage / instance field 统一 8 字节槽）
//
// NSDMI（int64_t l = 0）为【根因修复】：聚合初始化 `Value{}` / `Value r{};`
// 只对首成员做值初始化，联合体尾 4 字节按标准不保证清零——arm64 -O0（debug）
// 代码生成为 `str wzr`（4 字节）+ `ldr x0`（8 字节返回），返回寄存器高 32 位
// 带栈垃圾；深递归下该垃圾稳定为堆指针高半（0xb400007a00000000 形态），
// 经 intrinsic 返回值 -> move-result-object -> check-cast 解引用 SIGSEGV。
// 给宽成员挂 NSDMI 后 `Value{}`/`Value v;` 全 8 字节清零（已对照 NDK clang
// 28 aarch64 -O0/-O2 代码生成验证：str xzr 全宽）。
union Value {
  int64_t l = 0;
  int32_t i;
  float f;
  double d;
  ObjHeader* ref;
};

enum class ClassStatus : uint8_t {
  kNotReady,
  kIdx,
  kLoaded,
  kResolved,
  kVerified,
  kInitializing,
  kInitialized,
};

struct VField {
  VClass* declaring = nullptr;
  std::string name;
  std::string type_descriptor;  // 如 "I"、"Ljava/lang/String;"
  uint32_t access_flags = 0;
  uint32_t offset = 0;  // instance：对象内字节偏移；static：static_storage 槽号
  dex::DexFile* dex_file = nullptr;
  uint32_t field_idx = 0;
  bool is_static = false;
};

struct VMethod {
  VClass* declaring = nullptr;
  std::string name;
  std::string shorty;                 // 如 "VII"（首字符为返回类型）
  std::vector<std::string> param_types;
  std::string return_type;            // descriptor
  uint32_t access_flags = 0;
  const dex::CodeItem* code = nullptr;  // nullptr = abstract/native
  dex::DexFile* dex_file = nullptr;
  uint32_t method_idx = 0;
  void* native_fn = nullptr;  // Intrinsic（core/intrinsic.h）或未派发前为空
  void* jni_native_fn = nullptr;  // RegisterNatives 原始 JNI 函数（经 trampoline 派发）

  bool HasCode() const { return code != nullptr; }
  bool IsStatic() const;
  bool IsDirect() const;  // static/private/constructor
};

struct VClass : ObjHeader {
  std::string descriptor;  // "Lfoo/Bar;"、"[I"、"I"
  uint32_t access_flags = 0;
  VClass* super_class = nullptr;
  std::vector<VClass*> interfaces;

  dex::DexFile* dex_file = nullptr;  // 定义所在（内置类为 nullptr）
  uint32_t class_def_idx = 0;
  VObject* class_loader = nullptr;  // M1：nullptr=boot，非空=app（M2 换真实 ClassLoader）

  std::vector<VField> static_fields;
  std::vector<VField> instance_fields;
  std::vector<VMethod> direct_methods;
  std::vector<VMethod> virtual_methods;
  std::vector<VMethod*> vtable;

  std::vector<Value> static_storage;

  uint32_t object_size = 0;  // 实例字节数（含对象头）
  ClassStatus status = ClassStatus::kNotReady;

  // 数组/基本类型
  bool is_array = false;
  bool is_primitive = false;
  char primitive_type = 0;  // 'V','Z','B','S','C','I','J','F','D'
  VClass* component_type = nullptr;

  bool IsInterface() const { return (access_flags & 0x200) != 0; }
  bool IsAbstract() const { return (access_flags & 0x400) != 0; }

  // vtable 派发：virtual method index -> 实现
  VMethod* GetVTableEntry(uint32_t vtable_idx) const;
  // interface 派发：沿继承链线性查找实现（IMT 后置）
  VMethod* FindInterfaceImpl(VMethod* interface_method) const;
  // 虚调用派发缓存（ART vtable/IMT 等价最小面）：resolved method -> 实现。
  // 首次调用走 FindInterfaceImpl 线性签名查找，之后 O(1)/小表线性命中；
  // 单载体协作调度下无锁（与 DexCache 同口径）。上限 64 项，满则清空重建。
  std::vector<std::pair<const VMethod*, VMethod*>> dispatch_cache;
  bool IsAssignableFrom(VClass* other) const;  // this <- other 可赋值
};

}  // namespace dexvm::runtime
