#include "core/class_linker.h"

namespace dexvm::runtime {

VString* InternTable::Intern(VString* str) {
  if (str == nullptr) return nullptr;
  std::u16string key(str->chars(), str->chars() + str->length);
  auto it = table_.find(key);
  if (it != table_.end()) return it->second;
  table_.emplace(std::move(key), str);
  return str;
}

VString* InternTable::Lookup(std::u16string_view chars) const {
  auto it = table_.find(std::u16string(chars));
  return it == table_.end() ? nullptr : it->second;
}

}  // namespace dexvm::runtime
