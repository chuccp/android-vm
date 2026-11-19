// JNI 间接引用表实现（docs 05 章 15.3/15.4/15.15）
#include "jni/handle_table.h"

namespace dexvm::jni {

namespace {
inline uint32_t NextGeneration(uint32_t g) { return (g + 1u) & kGenerationMask; }
}  // namespace

// ---- 句柄编码 ----

Handle MakeHandle(RefKind kind, uint32_t generation, uint32_t index) {
  return (static_cast<uint32_t>(kind) << 30) |
         ((generation & kGenerationMask) << kGenerationShift) | ((index + 1u) & kMaxHandleSlots);
}

RefKind GetHandleKind(Handle h) {
  if (h == kNullHandle) return RefKind::kInvalid;
  switch ((h >> 30) & 0x3u) {
    case 0: return RefKind::kLocal;
    case 1: return RefKind::kGlobal;
    case 2: return RefKind::kWeakGlobal;
    default: return RefKind::kInvalid;
  }
}

bool DecodeHandle(Handle h, uint32_t* index, uint32_t* generation) {
  if (h == kNullHandle) return false;
  uint32_t kind = (h >> 30) & 0x3u;
  if (kind == 3u) return false;
  uint32_t idx1 = h & kMaxHandleSlots;
  if (idx1 == 0) return false;
  if (index != nullptr) *index = idx1 - 1u;
  if (generation != nullptr) *generation = (h >> kGenerationShift) & kGenerationMask;
  return true;
}

// ---- LocalRefTable ----

LocalRefTable::LocalRefTable() { frames_.emplace_back(); }  // base frame

void LocalRefTable::PushFrame() { frames_.emplace_back(); }

bool LocalRefTable::PopFrame() {
  if (frames_.size() <= 1) return false;  // base frame 不可弹出
  for (uint32_t idx : frames_.back().owned) {
    Slot& s = slots_[idx];
    if (s.live) FreeSlot(idx);
  }
  frames_.pop_back();
  return true;
}

Handle LocalRefTable::Add(runtime::ObjHeader* obj) {
  if (obj == nullptr) return kNullHandle;
  if (live_ >= kMaxLocalRefs) return kNullHandle;  // 容量核算（docs 15.12）
  // 新槽单调分配（真 ART IndirectRef 形态）：常态不复用已释放槽——每次
  // 对象返回都是新 slot；表体超高水位后才从 free 池拿旧槽（代位递增，
  // 陈旧解码仍恒失败），只防长跑累计槽量打满表体。
  uint32_t index;
  if (slots_.size() >= kSlotReuseHighWater && !free_.empty()) {
    index = free_.back();
    free_.pop_back();
    Slot& s = slots_[index];
    if (s.live || s.retired) return kNullHandle;  // 防御：池不变量
    s.obj = obj;
    s.live = true;
    s.frame_depth = static_cast<uint32_t>(frames_.size() - 1);
    frames_.back().owned.push_back(index);
    live_++;
    return MakeHandle(RefKind::kLocal, s.generation, index);
  }
  if (slots_.size() >= kMaxHandleSlots) return kNullHandle;  // 表满（M1：当 OOM）
  index = static_cast<uint32_t>(slots_.size());
  slots_.emplace_back();
  Slot& s = slots_[index];
  s.obj = obj;
  s.live = true;
  s.frame_depth = static_cast<uint32_t>(frames_.size() - 1);
  frames_.back().owned.push_back(index);
  live_++;
  return MakeHandle(RefKind::kLocal, s.generation, index);
}

runtime::ObjHeader* LocalRefTable::Get(Handle h) const {
  uint32_t index = 0, generation = 0;
  if (GetHandleKind(h) != RefKind::kLocal) return nullptr;
  if (!DecodeHandle(h, &index, &generation)) return nullptr;
  if (index >= slots_.size()) return nullptr;
  const Slot& s = slots_[index];
  if (!s.live || s.generation != generation) return nullptr;
  return s.obj;
}

bool LocalRefTable::Remove(Handle h) {
  uint32_t index = 0, generation = 0;
  if (GetHandleKind(h) != RefKind::kLocal) return false;
  if (!DecodeHandle(h, &index, &generation)) return false;
  if (index >= slots_.size()) return false;
  Slot& s = slots_[index];
  if (!s.live || s.generation != generation) return false;  // 过期/重复删除：no-op
  // docs 15.15：属于其他 local frame 的 entry no-op（ART WARNING，不抛异常）
  if (s.frame_depth != frames_.size() - 1) return false;
  FreeSlot(index);
  return true;
}

std::string LocalRefTable::DescribeHandle(Handle h) const {
  uint32_t index = 0, generation = 0;
  if (GetHandleKind(h) != RefKind::kLocal) return "kind-not-local";
  if (!DecodeHandle(h, &index, &generation)) return "decode-fail";
  std::string r = "idx=" + std::to_string(index) + " gen=" + std::to_string(generation) +
                  " cur_depth=" + std::to_string(frames_.size());
  if (index >= slots_.size()) {
    r += " slots=" + std::to_string(slots_.size()) + " (out-of-range)";
    return r;
  }
  const Slot& s = slots_[index];
  r += " live=" + std::to_string(s.live ? 1 : 0) +
       " slot_gen=" + std::to_string(s.generation) +
       " created_depth=" + std::to_string(s.frame_depth) +
       " retired=" + std::to_string(s.retired ? 1 : 0);
  return r;
}

bool LocalRefTable::Contains(Handle h) const {
  uint32_t index = 0, generation = 0;
  if (GetHandleKind(h) != RefKind::kLocal) return false;
  if (!DecodeHandle(h, &index, &generation)) return false;
  if (index >= slots_.size()) return false;
  const Slot& s = slots_[index];
  return s.live && s.generation == generation;
}

void LocalRefTable::FreeSlot(uint32_t index) {
  Slot& s = slots_[index];
  if (s.live && live_ > 0) live_--;
  s.obj = nullptr;
  s.live = false;
  // 代位递增（陈旧句柄恒检出：live+generation 双校验，任何过期句柄解码
  // 必然失败——D2 陈旧句柄根因面）。代位回绕即退役（失败语义化）：
  // 0..kGenerationMask 全代值都可能被过期句柄持有，该槽永不再复用。
  uint32_t next = NextGeneration(s.generation);
  if (next == 0) {
    s.retired = true;
    return;
  }
  s.generation = next;
  free_.push_back(index);  // 仅表体超高水位后会被复用（见 Add）
}

bool LocalRefTable::EnsureCapacity(int32_t capacity) const {
  if (capacity <= 0) return true;
  return static_cast<size_t>(capacity) <= remaining();
}

// ---- GlobalRefTable ----

Handle GlobalRefTable::Add(runtime::ObjHeader* obj) {
  if (obj == nullptr) return kNullHandle;
  // 新槽单调分配（同 LocalRefTable：陈旧句柄恒检出）
  if (slots_.size() >= kMaxHandleSlots) return kNullHandle;
  uint32_t index = static_cast<uint32_t>(slots_.size());
  slots_.emplace_back();
  Slot& s = slots_[index];
  s.obj = obj;
  s.live = true;
  s.weak = false;
  return MakeHandle(RefKind::kGlobal, s.generation, index);
}

Handle GlobalRefTable::AddWeak(runtime::ObjHeader* obj) {
  if (obj == nullptr) return kNullHandle;
  Handle h = Add(obj);
  if (h == kNullHandle) return kNullHandle;
  uint32_t index = 0, generation = 0;
  DecodeHandle(h, &index, &generation);
  slots_[index].weak = true;
  return MakeHandle(RefKind::kWeakGlobal, generation, index);
}

runtime::ObjHeader* GlobalRefTable::Get(Handle h) const {
  uint32_t index = 0, generation = 0;
  RefKind kind = GetHandleKind(h);
  if (kind != RefKind::kGlobal && kind != RefKind::kWeakGlobal) return nullptr;
  if (!DecodeHandle(h, &index, &generation)) return nullptr;
  if (index >= slots_.size()) return nullptr;
  const Slot& s = slots_[index];
  if (!s.live || s.generation != generation) return nullptr;
  return s.obj;  // weak 已清除时 obj == nullptr
}

bool GlobalRefTable::Remove(Handle h) {
  uint32_t index = 0, generation = 0;
  RefKind kind = GetHandleKind(h);
  if (kind != RefKind::kGlobal && kind != RefKind::kWeakGlobal) return false;
  if (!DecodeHandle(h, &index, &generation)) return false;
  if (index >= slots_.size()) return false;
  Slot& s = slots_[index];
  if (!s.live || s.generation != generation) return false;
  if (kind == RefKind::kWeakGlobal ? !s.weak : s.weak) return false;  // kind 不匹配
  s.obj = nullptr;
  s.live = false;
  s.weak = false;
  // 释放即退役（同 LocalRefTable）：过期句柄恒检出
  s.generation = NextGeneration(s.generation);
  s.retired = true;
  return true;
}

bool GlobalRefTable::Contains(Handle h) const { return Classify(h) != RefKind::kInvalid; }

RefKind GlobalRefTable::Classify(Handle h) const {
  uint32_t index = 0, generation = 0;
  RefKind kind = GetHandleKind(h);
  if (kind != RefKind::kGlobal && kind != RefKind::kWeakGlobal) return RefKind::kInvalid;
  if (!DecodeHandle(h, &index, &generation)) return RefKind::kInvalid;
  if (index >= slots_.size()) return RefKind::kInvalid;
  const Slot& s = slots_[index];
  if (!s.live || s.generation != generation) return RefKind::kInvalid;
  return s.weak ? RefKind::kWeakGlobal : RefKind::kGlobal;
}

bool GlobalRefTable::ClearWeak(Handle h) {
  uint32_t index = 0, generation = 0;
  if (GetHandleKind(h) != RefKind::kWeakGlobal) return false;
  if (!DecodeHandle(h, &index, &generation)) return false;
  if (index >= slots_.size()) return false;
  Slot& s = slots_[index];
  if (!s.live || s.generation != generation || !s.weak) return false;
  s.obj = nullptr;  // entry 保留（仍可经 GetObjectRefType 识别），对象断链
  return true;
}

}  // namespace dexvm::jni
