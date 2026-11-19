// GC 实现：根扫描 + 传递闭包 + 清扫
// 根清单对照 docs 02 章 6.6；解释器帧布局注意：
// Frame::regs 每字节码 vreg = 2 物理 uint32 槽，P(v) = regs[2v] | regs[2v+1]<<32，
// 引用即完整指针 —— 帧扫描按 64 位保守扫描。
//
// 自动 GC（docs 12 章 22.10）下的根面扩展（本轮）：
//   - 多 GuestTask 共享活跃帧集（frame_stack）——挂起 task 的帧随 fiber 冻结
//     在内，收集时全 task 不在运行态（单执行流泵：GC 触发点=分配失败的
//     allocator 入口，即安全点；其余 task 天然挂起）。
//   - 保守 C++ 栈扫描：解释器 pending_result、builtins 中间对象、ThrowNew
//     中转对象等仅存于 C++ 局部的引用（HandleScope 后置）——扫描本线程栈
//     + 挂起 GuestTask fiber 栈/寄存器保存区（经 memory/heap.cc 栈区枚举接缝）。
//   - 保守静态根扫描：dexvm 模块 .data/.bss 全局（framework_builtins 的
//     g_msg_pool 池链头等 C++ 全局对 guest 对象的隐性引用）。
//   - 钉住根（Heap::Pin）：预分配 OutOfMemoryError 单例（ART pre-allocated OOM）。
//   - framework 生命周期单例兜底：Looper 实例全量保活（g_loopers/g_thread_looper
//     为 framework_builtins.cc 匿名命名空间簿记，GC 不可见；Looper->queue->
//     消息链经传递闭包一并保活）。
//   - Thread.allThreads 静态数组经类静态根覆盖（挂起 task 的 Thread peer 保活）。
// SoftReference 清除语义后置（无 ReferenceProcessor，docs 02 章 6.10）。
#include "core/gc.h"

#include <pthread.h>

#include <cstdio>
#include <cstring>
#include <unordered_set>
#include <vector>

#include "core/class_linker.h"
#include "core/interpreter.h"
#include "core/vm_types.h"
#include "memory/heap.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <mach-o/getsect.h>
#else
#include <link.h>
#endif

// ASan（-fsanitize=address）栈/全局红区与保守扫描不兼容（线性读即 abort）：
// ASan 构建只走精确根（类/InternTable/帧/钉住根）——既有测试语义不变；
// 无 ASan 构建（设备 / 常规宿主测试）启用保守扫描（C++ 局部引用防线）。
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define DEXVM_GC_ASAN 1
#endif
#endif
#if defined(__SANITIZE_ADDRESS__)
#define DEXVM_GC_ASAN 1
#endif
#ifndef DEXVM_GC_ASAN
#define DEXVM_GC_ASAN 0
#endif

namespace dexvm::gc {

using runtime::ObjHeader;
using runtime::VArray;
using runtime::VClass;
using runtime::VObject;
using runtime::VString;
using runtime::VMState;

namespace {

struct Marker {
  mem::Heap* heap;
  std::unordered_set<void*> class_set;  // VClass 对象集合（判别类对象）
  std::unordered_set<void*> marked;
  std::vector<ObjHeader*> worklist;

  void Mark(ObjHeader* obj) {
    if (obj == nullptr || marked.count(obj) || !heap->IsHeapStart(obj)) return;
    marked.insert(obj);
    worklist.push_back(obj);
  }

  bool IsClass(const ObjHeader* obj) const { return class_set.count(const_cast<ObjHeader*>(obj)); }

  void MarkClass(VClass* k) {
    Mark(reinterpret_cast<ObjHeader*>(k));
    Mark(reinterpret_cast<ObjHeader*>(k->super_class));
    for (VClass* i : k->interfaces) Mark(reinterpret_cast<ObjHeader*>(i));
    Mark(reinterpret_cast<ObjHeader*>(k->component_type));
    // 类加载器（app loader 等：ClassLinker::Loaders 绑定对象，非类静态）
    Mark(reinterpret_cast<ObjHeader*>(k->class_loader));
    // 静态引用字段（按类型判别）
    for (size_t i = 0; i < k->static_fields.size() && i < k->static_storage.size(); i++) {
      const std::string& t = k->static_fields[i].type_descriptor;
      if (t[0] == 'L' || t[0] == '[') {
        Mark(k->static_storage[i].ref);
      }
    }
  }

  void Process(ObjHeader* obj) {
    if (IsClass(obj)) {
      MarkClass(reinterpret_cast<VClass*>(obj));
      return;
    }
    VClass* k = obj->klass;
    Mark(reinterpret_cast<ObjHeader*>(k));
    if (k == nullptr) return;
    if (k->is_array) {
      if (k->component_type != nullptr &&
          (k->component_type->descriptor[0] == 'L' || k->component_type->descriptor[0] == '[')) {
        VArray* arr = reinterpret_cast<VArray*>(obj);
        auto** refs = reinterpret_cast<ObjHeader**>(arr->data());
        for (int32_t i = 0; i < arr->length; i++) Mark(refs[i]);
      }
      return;
    }
    if (k->is_primitive) return;
    // 普通对象：实例引用字段（8 字节槽，按字段类型）——【含继承链全量】。
    // 根因修复（死对象槽）：klass->instance_fields 仅【自家声明字段】
    // （class_linker LinkClass 口径），超类字段必须沿 super_class 链补扫——
    // 此前只扫精确类自家字段，子类实例的全部继承引用槽（juc ThreadPoolExecutor
    // workQueue / ReentrantLock.sync / Reference.referent 等）漏标，其目标被
    // 清扫回收入空闲链而容器对象存活，后续 iget-object 读到悬挂指针
    // （CheckRef "dirty reference"，位型=reclaimed-block 死对象槽形态）。
    // Referent 语义不变：docs 口径「referent 视为强引用（GC 不清扫）」，
    // Reference.referent 随继承链一并强标（与 RefGet/RefClear intrinsic 一致）。
    VObject* vobj = reinterpret_cast<VObject*>(obj);
    for (VClass* c = k; c != nullptr; c = c->super_class) {
      for (auto& f : c->instance_fields) {
        if (f.is_static) continue;
        const std::string& t = f.type_descriptor;
        if (!t.empty() && (t[0] == 'L' || t[0] == '[')) {
          Mark(*reinterpret_cast<ObjHeader**>(vobj->field_data() + f.offset));
        }
      }
    }
  }
};

// ---- 保守栈扫描（ASan 构建关闭：红区不兼容，见上方宏）----
#if !DEXVM_GC_ASAN

struct StackBounds {
  uintptr_t base = 0;  // 低地址（含）
  uintptr_t top = 0;   // 高地址（不含）
};

// 本线程宿主栈界（macOS pthread_get_stackaddr_np；bionic/Linux pthread_getattr_np）
StackBounds HostStackBounds() {
  StackBounds b;
#if defined(__APPLE__)
  void* top = pthread_get_stackaddr_np(pthread_self());
  size_t sz = pthread_get_stacksize_np(pthread_self());
  if (top == nullptr || sz == 0) return b;
  b.top = reinterpret_cast<uintptr_t>(top);
  b.base = b.top - sz;
#else
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) != 0) return b;
  void* base = nullptr;
  size_t sz = 0;
  if (pthread_attr_getstack(&attr, &base, &sz) == 0 && base != nullptr && sz > 0) {
    b.base = reinterpret_cast<uintptr_t>(base);
    b.top = b.base + sz;
  }
  pthread_attr_destroy(&attr);
#endif
  return b;
}

// 可读 VMA 表（保守扫描裁剪，Linux/Android）：区间提供方给出的范围可能跨
// 非可读页——bionic pthread_attr_getstack 的 stackaddr 落在前置 guard 页
// （PROT_NONE，实证：SEGV_ACCERR @ guard 页基址，fp 形参槽 base=guard、
// bytes=栈尺寸）；16KB 对齐库的可写 PT_LOAD 也跨 "page size compat" ---p 洞。
// 保守扫描只读可读 VMA（对照 Boehm data-segment clipping / ART stack walk
// 不触 guard 页）——语义修复，非信号兜底。macOS pthread_getstack 口径不含
// guard 页（宿主既有测试行为不变：vma 表为空 = 不裁剪）。
struct ReadableVma {
  uintptr_t lo;
  uintptr_t hi;
};
std::vector<ReadableVma> g_readable_vmas;

void LoadReadableVmas() {
  g_readable_vmas.clear();
#if !defined(__APPLE__)
  std::FILE* f = std::fopen("/proc/self/maps", "r");
  if (f == nullptr) return;
  char line[320];
  while (std::fgets(line, sizeof(line), f) != nullptr) {
    unsigned long long lo = 0, hi = 0;
    char perm[8] = {};
    if (std::sscanf(line, "%llx-%llx %7s", &lo, &hi, perm) == 3 && perm[0] == 'r') {
      g_readable_vmas.push_back({static_cast<uintptr_t>(lo), static_cast<uintptr_t>(hi)});
    }
  }
  std::fclose(f);
#endif
}

void ScanWordsIn(const void* base, const void* end_ptr, Marker& m) {
  uintptr_t p = (reinterpret_cast<uintptr_t>(base) + 7u) & ~static_cast<uintptr_t>(7u);
  uintptr_t end = reinterpret_cast<uintptr_t>(end_ptr);
  for (; p + sizeof(void*) <= end; p += sizeof(void*)) {
    void* w = *reinterpret_cast<void**>(p);
    if (w != nullptr) m.Mark(reinterpret_cast<ObjHeader*>(w));
  }
}

void ScanRangeWords(const void* base, size_t bytes, Marker& m) {
  // 8 字节对齐保守扫描：命中堆起点即标记（假阳性=多保活，无害）。
  // 可读 VMA 裁剪（见 LoadReadableVmas）：非可读页跳过（不读、不崩）。
  uintptr_t lo = reinterpret_cast<uintptr_t>(base);
  uintptr_t hi = lo + bytes;
  if (g_readable_vmas.empty()) {
    ScanWordsIn(base, reinterpret_cast<const void*>(hi), m);
    return;
  }
  for (const ReadableVma& v : g_readable_vmas) {
    if (v.hi <= lo || v.lo >= hi) continue;
    uintptr_t s = v.lo > lo ? v.lo : lo;
    uintptr_t e = v.hi < hi ? v.hi : hi;
    if (s < e) ScanWordsIn(reinterpret_cast<const void*>(s), reinterpret_cast<const void*>(e), m);
  }
}

void AddStackRange(void* base, size_t bytes, void* arg) {
  auto* out = static_cast<std::vector<std::pair<void*, size_t>>*>(arg);
  if (base != nullptr && bytes > 0) out->emplace_back(base, bytes);
}

// ---- 静态根扫描（dexvm 模块 .data/.bss 全局；Boehm 静态根同款）----
#if defined(__APPLE__)
void ScanImageStaticRoots(void (*visit)(void*, size_t, void*), void* arg) {
  // 宿主测试：全 TU 链进主可执行；设备（bionic）走 dl_iterate 路径。
  // 64 位进程：按 mach_header_64 取段（getsectiondata 仅 64 位签名可用）。
  const mach_header* hdr = _dyld_get_image_header(0);
  if (hdr == nullptr || hdr->magic != MH_MAGIC_64) return;
  const mach_header_64* hdr64 = reinterpret_cast<const mach_header_64*>(hdr);
  unsigned long sz = 0;
  const char* sects[][2] = {{"__DATA", "__data"},
                            {"__DATA", "__bss"},
                            {"__DATA", "__common"},
                            {"__DATA_CONST", "__data"}};
  for (auto& s : sects) {
    uint8_t* p = getsectiondata(hdr64, s[0], s[1], &sz);
    if (p != nullptr && sz > 0) visit(p, sz, arg);
  }
}
#else
int PhdrStaticRoots(dl_phdr_info* info, size_t, void* arg) {
  // 仅 dexvm 模块（设备全局在 libdexvm.so；避免扫 ART/libc 数据面噪声）
  if (info->dlpi_name == nullptr || std::strstr(info->dlpi_name, "dexvm") == nullptr) return 0;
  auto* v = static_cast<std::pair<void (*)(void*, size_t, void*), void*>*>(arg);
  for (int i = 0; i < info->dlpi_phnum; i++) {
    const ElfW(Phdr)& ph = info->dlpi_phdr[i];
    if (ph.p_type == PT_LOAD && (ph.p_flags & PF_W) != 0 && ph.p_memsz > 0) {
      v->first(reinterpret_cast<void*>(info->dlpi_addr + ph.p_vaddr), ph.p_memsz, v->second);
    }
  }
  return 0;
}
void ScanImageStaticRoots(void (*visit)(void*, size_t, void*), void* arg) {
  std::pair<void (*)(void*, size_t, void*), void*> p{visit, arg};
  dl_iterate_phdr(PhdrStaticRoots, &p);
}
#endif

void VisitAsRange(void* base, size_t bytes, void* arg) {
  auto* out = static_cast<std::vector<std::pair<void*, size_t>>*>(arg);
  if (base != nullptr && bytes > 0) out->emplace_back(base, bytes);
}

#endif  // !DEXVM_GC_ASAN

// 重入护栏：GC 过程中的分配失败不再触发新一轮（22.10「等待在跑 GC」的
// 单流同步落法——无并发收集者，防递归）。
bool g_in_gc = false;

}  // namespace

void GcAttach(VMState& state) {
  if (state.heap == nullptr) return;
  state.heap->SetGcTrigger(
      [](void* ctx) {
        CollectGarbage(*static_cast<VMState*>(ctx));
      },
      &state);
}

size_t CollectGarbage(VMState& state) {
  mem::Heap* heap = state.heap;
  if (heap == nullptr || state.linker == nullptr) return 0;
  if (g_in_gc) return 0;
  g_in_gc = true;
  size_t used_before = heap->bytes_allocated();

  Marker m{heap, {}, {}, {}};

  // 类对象集合（用于判别 VClass）
  for (VClass* k : state.linker->all_classes()) m.class_set.insert(k);

  // 根：全部类（含其静态引用 + class_loader）
  for (VClass* k : state.linker->all_classes()) m.MarkClass(k);

  // 根：驻留字符串
  for (auto& [key, str] : state.intern->entries()) {
    m.Mark(reinterpret_cast<ObjHeader*>(str));
  }

  // 根：pending exception
  m.Mark(state.pending_exception);

  // 根：钉住根（预分配 OOM 单例等；传递闭包含其 klass/detailMessage）
  for (void* p : heap->pinned()) m.Mark(reinterpret_cast<ObjHeader*>(p));

  // 根：解释器帧（保守扫描 64 位槽对；frame_stack = 多 GuestTask 共享活跃帧集，
  // fiber 换出的挂起 task 帧同样在内）
  for (runtime::Frame* f : state.frame_stack) {
    for (size_t i = 0; i + 1 < f->regs.size(); i += 2) {
      uintptr_t v = static_cast<uintptr_t>(f->regs[i]) |
                    (static_cast<uintptr_t>(f->regs[i + 1]) << 32);
      if (heap->IsHeapStart(reinterpret_cast<void*>(v))) {
        m.Mark(reinterpret_cast<ObjHeader*>(v));
      }
    }
  }

#if !DEXVM_GC_ASAN
  // 可读 VMA 表重建（本轮保守扫描裁剪；见 LoadReadableVmas 注释）
  LoadReadableVmas();
  // 根：保守 C++ 栈扫描（builtins/解释器/异常中转的局部引用防线）。
  // 本上下文：&marker 向上至栈顶（栈向下增长，调用方帧全在其上）；
  // 在 fiber 上时宿主栈整体补扫（调度器/RunGuestMain 帧冻结在宿主栈）；
  // 挂起 GuestTask 的 fiber 栈 + ucontext 保存区经栈区枚举接缝全量扫描。
  {
    char marker = 0;  // 取址=当前 SP 近似（本帧在栈上恒有存储）
    const void* sp = &marker;
    StackBounds host = HostStackBounds();
    uintptr_t sp_addr = reinterpret_cast<uintptr_t>(sp);
    std::vector<std::pair<void*, size_t>> ranges;
    mem::VisitGlobalStackRanges(&AddStackRange, &ranges);
    bool on_fiber = false;
    for (auto& [b, n] : ranges) {
      uintptr_t lo = reinterpret_cast<uintptr_t>(b);
      if (sp_addr >= lo && sp_addr < lo + n) on_fiber = true;
    }
    if (host.top > host.base) {
      if (on_fiber) {
        // 宿主栈全量（调度面冻结帧；低于当前 SP 的残区仅过度保活，无害）
        ScanRangeWords(reinterpret_cast<const void*>(host.base), host.top - host.base, m);
      } else if (sp_addr >= host.base && sp_addr < host.top) {
        ScanRangeWords(sp, host.top - sp_addr, m);
      }
    }
    for (auto& [b, n] : ranges) ScanRangeWords(b, n, m);
  }

  // 根：保守静态根扫描（dexvm 模块全局——framework_builtins 的 g_msg_pool
  // 池链头等；池链沿 Message.next 实例字段传递闭包）
  {
    std::vector<std::pair<void*, size_t>> statics;
    ScanImageStaticRoots(&VisitAsRange, &statics);
    for (auto& [b, n] : statics) ScanRangeWords(b, n, m);
  }
#endif  // !DEXVM_GC_ASAN

  // 根：framework 生命周期单例兜底——Looper 实例全量保活。
  // （g_loopers/g_thread_looper 在 framework_builtins.cc 匿名命名空间，簿记
  // 不可见；Looper->queue->在队消息链经传递闭包一并保活。消息池不在此列：
  // 池链头经静态根扫描覆盖，池内消息沿 next 链传递闭包。）
  {
    VClass* looper_cls = nullptr;
    for (VClass* k : state.linker->all_classes()) {
      if (k->descriptor == "Landroid/os/Looper;") {
        looper_cls = k;
        break;
      }
    }
    if (looper_cls != nullptr) {
      struct LooperScan {
        Marker* m;
        VClass* klass;
      } ls{&m, looper_cls};
      heap->ForEachAllocated(
          [](void* obj, void* arg) {
            auto* s = static_cast<LooperScan*>(arg);
            ObjHeader* o = reinterpret_cast<ObjHeader*>(obj);
            if (o->klass == s->klass) s->m->Mark(o);  // 仅比对 klass 指针，不解引用深字段
          },
          &ls);
    }
  }

  // 传递闭包
  while (!m.worklist.empty()) {
    ObjHeader* obj = m.worklist.back();
    m.worklist.pop_back();
    m.Process(obj);
  }

  // 清扫
  std::vector<void*> live;
  live.reserve(m.marked.size());
  for (void* p : m.marked) live.push_back(p);
  heap->Sweep(live);

  size_t freed = used_before - heap->bytes_allocated();
  heap->NoteGcRun(freed);
  g_in_gc = false;
  return freed;
}

}  // namespace dexvm::gc
