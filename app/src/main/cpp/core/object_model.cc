// 对象模型辅助（docs 02 章布局语义）
#include "core/vm_types.h"

#include <algorithm>

namespace dexvm::runtime {

std::string VString::ToModifiedUtf8() const {
  std::string out;
  out.reserve(length);
  for (int32_t i = 0; i < length; i++) {
    uint16_t c = chars()[i];
    if (c != 0 && c < 0x80) {
      out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
      out.push_back(static_cast<char>(0xC0 | (c >> 6)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | (c >> 12)));
      out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
  }
  return out;
}

bool VMethod::IsStatic() const { return (access_flags & 0x8) != 0; }

bool VMethod::IsDirect() const {
  // static / private / <init> / <clinit>
  return IsStatic() || (access_flags & 0x2) != 0 || name == "<init>" || name == "<clinit>";
}

VMethod* VClass::GetVTableEntry(uint32_t vtable_idx) const {
  if (vtable_idx >= vtable.size()) return nullptr;
  return vtable[vtable_idx];
}

namespace {
bool SameSignature(const VMethod& a, const VMethod& b) {
  return a.name == b.name && a.return_type == b.return_type && a.param_types == b.param_types;
}

VMethod* FindImplInClass(VClass* klass, const VMethod& target) {
  for (auto& m : klass->virtual_methods)
    if (SameSignature(m, target)) return &m;
  for (auto& m : klass->direct_methods) {
    if (!m.IsStatic() && SameSignature(m, target)) return &m;
  }
  return nullptr;
}
}  // namespace

VMethod* VClass::FindInterfaceImpl(VMethod* interface_method) const {
  for (const VClass* k = this; k != nullptr; k = k->super_class) {
    if (VMethod* impl = FindImplInClass(const_cast<VClass*>(k), *interface_method)) {
      return impl;
    }
    // 接口方法查找沿超接口传递（Collection.stream 默认方法：
    // HashSet -> Set -> Collection）；只查直接接口会漏掉 Java 8+ 默认方法。
    std::vector<VClass*> pending(k->interfaces.begin(), k->interfaces.end());
    for (size_t i = 0; i < pending.size(); ++i) {
      VClass* iface = pending[i];
      if (iface == nullptr) continue;
      if (VMethod* impl = FindImplInClass(iface, *interface_method)) return impl;
      for (VClass* super_iface : iface->interfaces) {
        if (std::find(pending.begin(), pending.end(), super_iface) == pending.end()) {
          pending.push_back(super_iface);
        }
      }
    }
  }
  // Object 方法兜底（invokeinterface 命中 j.l.Object 方法）
  return nullptr;
}

bool VClass::IsAssignableFrom(VClass* other) const {
  if (other == this) return true;
  if (other == nullptr) return false;
  if (IsInterface()) {
    for (VClass* k = other; k != nullptr; k = k->super_class) {
      if (k == this) return true;
      for (VClass* iface : k->interfaces) {
        if (iface == this || IsAssignableFrom(iface)) return true;
      }
    }
    return false;
  }
  if (is_array && other->is_array) {
    // 引用数组协变：[[I <- [[I；[Ljava/lang/Object; <- [Ljava/lang/String;
    if (component_type != nullptr && other->component_type != nullptr) {
      if (component_type->is_primitive || other->component_type->is_primitive) {
        return component_type == other->component_type;
      }
      return component_type->IsAssignableFrom(other->component_type);
    }
  }
  for (VClass* k = other; k != nullptr; k = k->super_class) {
    if (k == this) return true;
  }
  return false;
}

}  // namespace dexvm::runtime
