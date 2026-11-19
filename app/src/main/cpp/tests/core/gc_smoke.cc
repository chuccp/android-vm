// GC 冒烟：根存活、垃圾回收、空闲链复用、帧保守扫描
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "core/builtins.h"
#include "core/class_linker.h"
#include "core/gc.h"
#include "core/interpreter.h"
#include "core/vm_types.h"
#include "memory/heap.h"

namespace {

int g_fail = 0;
#define CHECK(cond, msg)                          \
  do {                                            \
    if (!(cond)) {                                \
      std::printf("FAIL %s\n", msg);              \
      g_fail++;                                   \
    } else {                                      \
      std::printf("PASS %s\n", msg);              \
    }                                             \
  } while (0)

using namespace dexvm;

}  // namespace

int main() {
  auto heap = std::make_unique<mem::Heap>(32 * 1024 * 1024);
  auto intern = std::make_unique<runtime::InternTable>();
  auto linker = std::make_unique<runtime::ClassLinker>(heap.get(), intern.get());
  runtime::RegisterBuiltins(linker.get());
  runtime::VMState state;
  state.heap = heap.get();
  state.intern = intern.get();
  state.linker = linker.get();

  // 活对象：驻留串 + 对象 + 引用数组 + 帧保守根
  runtime::VString* live_str = intern->Intern(linker->AllocString(u"live"));
  runtime::VObject* live_obj = linker->AllocObject(linker->FindClass("Ljava/lang/Object;"));
  runtime::VClass* str_cls = linker->FindClass("Ljava/lang/String;");
  runtime::VClass* arr_cls = linker->FindArrayClass(str_cls);
  runtime::VArray* live_arr = linker->AllocArray(arr_cls, 4);
  reinterpret_cast<runtime::ObjHeader**>(live_arr->data())[0] =
      reinterpret_cast<runtime::ObjHeader*>(live_str);

  // 帧保守根：仅存在于解释器帧 regs 中的对象
  runtime::VObject* frame_only = linker->AllocObject(linker->FindClass("Ljava/lang/Object;"));
  runtime::Frame frame;
  frame.regs.resize(4, 0);
  uintptr_t p = reinterpret_cast<uintptr_t>(frame_only);
  frame.regs[0] = static_cast<uint32_t>(p & 0xFFFFFFFFu);
  frame.regs[1] = static_cast<uint32_t>(p >> 32);
  state.frame_stack.push_back(&frame);

  size_t live_before = heap->live_count();
  gc::CollectGarbage(state);
  CHECK(heap->live_count() <= live_before, "gc runs");
  CHECK(live_str->length == 4 && live_str->chars()[0] == u'l', "interned string survives");
  CHECK(live_obj->klass != nullptr, "rooted object survives");
  CHECK(reinterpret_cast<runtime::ObjHeader**>(live_arr->data())[0] ==
            reinterpret_cast<runtime::ObjHeader*>(live_str),
        "array element edge intact");
  CHECK(frame_only->klass != nullptr, "conservative frame root survives");

  // 垃圾回收：分配大量不可达对象后应被清掉
  for (int round = 0; round < 5; round++) {
    for (int i = 0; i < 2000; i++) {
      linker->AllocString(u"garbage");
      linker->AllocObject(linker->FindClass("Ljava/lang/Object;"));
    }
    gc::CollectGarbage(state);
  }
  size_t after = heap->live_count();
  CHECK(after < 200, "garbage collected across rounds");
  CHECK(live_str->length == 4, "live set intact after churn");

  // 复用：大量分配→回收后 used 不应单调膨胀
  size_t used_peak = 0;
  for (int round = 0; round < 30; round++) {
    for (int i = 0; i < 5000; i++) linker->AllocString(u"x");
    gc::CollectGarbage(state);
    if (heap->bytes_allocated() > used_peak) used_peak = heap->bytes_allocated();
  }
  std::printf("used_after_churn=%zu peak=%zu live=%zu\n", heap->bytes_allocated(), used_peak,
              heap->live_count());
  CHECK(heap->bytes_allocated() < 2 * 1024 * 1024, "free-list reuse keeps usage bounded");

  state.frame_stack.pop_back();
  gc::CollectGarbage(state);
  CHECK(heap->live_count() <= after, "frame-only object collected after root removal");

  if (g_fail == 0) std::printf("ALL PASS\n");
  return g_fail == 0 ? 0 : 1;
}
