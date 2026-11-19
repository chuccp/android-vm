// java.util.VmLinkedList：lib LinkedList 的节点遍历原生桥。
//
// 启动实测（DexVmProfile）：MainActivity.onCreate 中 LinkedList.node 占 ~16s
// （占比第一）。caller 链定位为 app 模块/进程追踪初始化：
//   LinkedList.node <- LinkedList.get / ListItr.<init> <- ... <- b1.h
// 即 app 侧按 index 反复取节点（O(n²) 遍历）——AOSP 上由 ART JIT 承接，
// switch 解释器下每个 node 步骤都是一次 DoCall + 帧分配，被放大成秒级。
//
// 这里按 AOSP “集合核心由运行时高效实现” 的口径，把 node/get 的遍历下沉到
// C++：语义（双向就近遍历、越界异常）与 lib 源码一致，lib 侧仅保留签名。
#include "core/builtins_internal.h"
#include "core/framework_builtins.h"
#include "core/framework_builtins_internal.h"
#include "core/intrinsic.h"
#include "core/vm_types.h"

#include <cstring>
#include <mutex>

namespace dexvm::runtime {

namespace {

// 布局缓存按 ClassLinker 实例身份归属：常驻会话可在同进程多次打开（每次
// 新 Heap/ClassLinker），且栈地址可能复用——指针相等不足以判定同一实例，
// 复用旧 VClass* 会悬挂。
struct LinkedListLayout {
  uint64_t linker_id = 0;
  VClass* node_cls = nullptr;
  uint32_t size_off = 0;
  uint32_t first_off = 0;
  uint32_t last_off = 0;
  uint32_t item_off = 0;
  uint32_t next_off = 0;
  uint32_t prev_off = 0;
};

LinkedListLayout g_ll;
std::mutex g_ll_mu;

bool EnsureLinkedListLayout(VMState& state) {
  const uint64_t linker_id = state.linker->InstanceId();
  if (g_ll.linker_id == linker_id) return g_ll.node_cls != nullptr;
  std::lock_guard<std::mutex> lk(g_ll_mu);
  if (g_ll.linker_id == linker_id) return g_ll.node_cls != nullptr;
  LinkedListLayout layout;
  layout.linker_id = linker_id;
  VClass* list_cls = state.linker->FindClass("Ljava/util/LinkedList;");
  layout.node_cls = state.linker->FindClass("Ljava/util/LinkedList$Node;");
  if (list_cls == nullptr || layout.node_cls == nullptr) return false;
  VField* size_f = FindInstanceField(list_cls, "size");
  VField* first_f = FindInstanceField(list_cls, "first");
  VField* last_f = FindInstanceField(list_cls, "last");
  VField* item_f = FindInstanceField(layout.node_cls, "item");
  VField* next_f = FindInstanceField(layout.node_cls, "next");
  VField* prev_f = FindInstanceField(layout.node_cls, "prev");
  if (size_f == nullptr || first_f == nullptr || last_f == nullptr || item_f == nullptr ||
      next_f == nullptr || prev_f == nullptr) {
    return false;
  }
  layout.size_off = size_f->offset;
  layout.first_off = first_f->offset;
  layout.last_off = last_f->offset;
  layout.item_off = item_f->offset;
  layout.next_off = next_f->offset;
  layout.prev_off = prev_f->offset;
  g_ll = layout;
  return true;
}

inline int32_t ReadIntField(VObject* o, uint32_t off) {
  int32_t v = 0;
  std::memcpy(&v, o->field_data() + off, sizeof(v));
  return v;
}

inline ObjHeader* ReadRefField(VObject* o, uint32_t off) {
  ObjHeader* r = nullptr;
  std::memcpy(&r, o->field_data() + off, sizeof(r));
  return r;
}

// AOSP lib LinkedList.node：index < size/2 从头走，否则从尾走。
ObjHeader* NodeAt(VMState& state, VObject* list, int32_t index, bool* ok) {
  int32_t size = ReadIntField(list, g_ll.size_off);
  if (index < 0 || index >= size) {
    state.ThrowNew("Ljava/lang/IndexOutOfBoundsException;",
                   "Index: " + std::to_string(index) + ", Size: " + std::to_string(size));
    *ok = false;
    return nullptr;
  }
  if (index < (size >> 1)) {
    ObjHeader* n = ReadRefField(list, g_ll.first_off);
    for (int32_t i = 0; i < index; i++) {
      if (n == nullptr) break;
      n = ReadRefField(reinterpret_cast<VObject*>(n), g_ll.next_off);
    }
    return n;
  }
  ObjHeader* n = ReadRefField(list, g_ll.last_off);
  for (int32_t i = size - 1; i > index; i--) {
    if (n == nullptr) break;
    n = ReadRefField(reinterpret_cast<VObject*>(n), g_ll.prev_off);
  }
  return n;
}

}  // namespace

// VmLinkedList.get(LinkedList list, int index) -> Object item
Value VmLinkedListGet(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "list == null");
    *ok = false;
    return r;
  }
  if (!EnsureLinkedListLayout(state)) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "VmLinkedList layout unavailable");
    *ok = false;
    return r;
  }
  VObject* list = reinterpret_cast<VObject*>(args[0].ref);
  ObjHeader* node = NodeAt(state, list, args[1].i, ok);
  if (!*ok) return r;
  if (node != nullptr) r.ref = ReadRefField(reinterpret_cast<VObject*>(node), g_ll.item_off);
  return r;
}

// VmLinkedList.node(LinkedList list, int index) -> Node
Value VmLinkedListNode(VMState& state, VObject* self, const Value* args, size_t n, bool* ok) {
  (void)self;
  *ok = true;
  Value r{};
  if (n < 2 || args[0].ref == nullptr) {
    state.ThrowNew("Ljava/lang/NullPointerException;", "list == null");
    *ok = false;
    return r;
  }
  if (!EnsureLinkedListLayout(state)) {
    state.ThrowNew("Ljava/lang/IllegalStateException;", "VmLinkedList layout unavailable");
    *ok = false;
    return r;
  }
  VObject* list = reinterpret_cast<VObject*>(args[0].ref);
  r.ref = NodeAt(state, list, args[1].i, ok);
  return r;
}

}  // namespace dexvm::runtime
