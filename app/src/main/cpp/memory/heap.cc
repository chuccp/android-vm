// Guest Heap：非移动 arena + 空闲链复用 + mark-sweep（docs 02 章）
// + 分配失败 GC 重试链（docs 12 章 22.10 AllocateInternalWithGc）
#include "memory/heap.h"

#include <sys/mman.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace dexvm::mem {

namespace {
constexpr size_t kAlign = 16;
size_t AlignUp(size_t n) { return (n + kAlign - 1) & ~(kAlign - 1); }

// 堆不变量诊断（低量：仅新建 region / 坏块时打点）。设备 logcat 标签 DexVmHeap；
// 宿主测试无 android/log.h → printf。
void HeapLog(const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
#if defined(__ANDROID__)
  __android_log_vprint(ANDROID_LOG_INFO, "DexVmHeap", fmt, ap);
#else
  std::vfprintf(stderr, fmt, ap);
  std::fputc('\n', stderr);
#endif
  va_end(ap);
}

// 挂起 GuestTask 栈区枚举接缝（gc.cc 消费；core/builtins.cc 装接协程侧）
StackRangeVisitorFn g_stack_visitor = nullptr;
void* g_stack_visitor_ctx = nullptr;
}  // namespace

void SetGlobalStackProvider(StackRangeVisitorFn fn, void* ctx) {
  g_stack_visitor = fn;
  g_stack_visitor_ctx = ctx;
}

void VisitGlobalStackRanges(StackRangeAddFn add, void* arg) {
  if (g_stack_visitor != nullptr) g_stack_visitor(g_stack_visitor_ctx, add, arg);
}

Heap::Heap(size_t capacity) : capacity_(capacity) {
  Region r;
  r.size = 8 * 1024 * 1024;
  r.base = static_cast<uint8_t*>(
      ::mmap(nullptr, r.size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  r.used = 0;
  if (r.base != MAP_FAILED) {
    regions_.push_back(r);
    HeapLog("new region %p..%p (initial)", r.base, r.base + r.size);
  }
}

Heap::~Heap() {
  for (auto& r : regions_) ::munmap(r.base, r.size);
}

bool Heap::InAnyRegion(const void* p, size_t size) const {
  const uintptr_t a = reinterpret_cast<uintptr_t>(p);
  for (const Region& r : regions_) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(r.base);
    if (a >= base && a + size <= base + r.size) return true;
  }
  return false;
}

void* Heap::AllocRaw(size_t size) {
  // 1. 空闲链复用（精确尺寸键）
  auto fit = free_lists_.find(size);
  while (fit != free_lists_.end() && !fit->second.empty()) {
    void* p = fit->second.back();
    fit->second.pop_back();
    // 不变量：空闲链块必须落在某个 region 内（坏块=堆元数据被踩，
    // 直接复用会把「野指针」交给 new (mem) VObject() → SEGV_ACCERR 写保护页）。
    // 发现坏块：丢弃并记账诊断，继续尝试下一条/走 bump。
    if (!InAnyRegion(p, size)) {
      bad_free_blocks_++;
      HeapLog("BAD-FREE-BLOCK free_lists_[%zu] p=%p regions=%zu (dropped)",
              size, p, regions_.size());
      continue;
    }
    sizes_[p] = size;
    used_ += size;
    std::memset(p, 0, size);
    return p;
  }
  // 2. arena bump
  if (used_ + size > capacity_) return nullptr;
  for (auto& r : regions_) {
    if (r.used + size <= r.size) {
      void* p = r.base + r.used;
      r.used += size;
      sizes_[p] = size;
      used_ += size;
      std::memset(p, 0, size);
      return p;
    }
  }
  size_t rs = size < 8 * 1024 * 1024 ? 8 * 1024 * 1024 : size;
  Region r;
  r.base = static_cast<uint8_t*>(
      ::mmap(nullptr, rs, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
  if (r.base == MAP_FAILED) return nullptr;
  r.size = rs;
  r.used = size;
  used_ += size;
  void* p = r.base;
  std::memset(p, 0, size);
  regions_.push_back(r);
  sizes_[p] = size;
  HeapLog("new region %p..%p used=%zu total=%zu", r.base, r.base + rs, size, used_);
  return p;
}

void Heap::SetGcTrigger(GcTriggerFn fn, void* ctx) {
  gc_trigger_ = fn;
  gc_ctx_ = ctx;
}

void Heap::Pin(void* obj) {
  if (obj == nullptr) return;
  for (void* p : pinned_) {
    if (p == obj) return;
  }
  pinned_.push_back(obj);
}

void Heap::NoteGcRun(size_t freed_bytes) {
  gc_runs_++;
  gc_freed_bytes_ += freed_bytes;
}

void* Heap::Alloc(size_t size) {
  // 0 字节块给最小块：否则 bump 在 r.used==r.size 时满足
  // `r.used + 0 <= r.size`，返回 region 末地址（一过性越界指针），写对象头即
  // SEGV_ACCERR（AllocObject(object_size=0) 曾实证踩中）。
  size = AlignUp(size == 0 ? 1 : size);
  void* p = AllocRaw(size);
  // 分配点不变量：返回块必须落在 region 内（否则调用方 new (mem) 会写坏页
  // → SEGV_ACCERR）。坏块按分配失败处理（抛 OOM 可 catch），不让线程死。
  if (p != nullptr && !InAnyRegion(p, size)) {
    bad_free_blocks_++;
    HeapLog("BAD-ALLOC p=%p size=%zu regions=%zu (treated as OOM)", p, size,
            regions_.size());
    p = nullptr;
  }
  if (p != nullptr || gc_trigger_ == nullptr || in_alloc_retry_) return p;
  // 即使空堆也装不下：不必触发 GC
  if (size > capacity_) return nullptr;

  // docs 12 章 22.10 AllocateInternalWithGc 多阶段策略（对照 reference/art
  // gc/heap.cc AllocateInternalWithGc）：
  //   等待在跑 GC → 一轮阻塞 GC → 重试；仍失败循环全堆 GC（清 SoftReference
  //   语义后置——本轮 GC 无 ReferenceProcessor，注记于 gc.cc），要求每轮有
  //   回收增益（生产率条件），至 kMaxGcAttempts（ART=5）耗尽返回 nullptr。
  // 单流同步模型：GC 为 stop-the-world 同步调用，「等待在跑 GC」即重入护栏
  //（GC 过程中的分配失败不再递归触发，直接失败）。
  in_alloc_retry_ = true;
  for (int attempt = 0; attempt < kMaxGcAttempts; attempt++) {
    size_t before = used_;
    gc_trigger_(gc_ctx_);
    p = AllocRaw(size);
    if (p != nullptr) {
      alloc_recovered_++;
      break;
    }
    if (used_ >= before) break;  // 无回收增益（生产率条件）：再试无意义
  }
  in_alloc_retry_ = false;
  return p;
}

bool Heap::IsHeapStart(const void* p) const { return sizes_.find(const_cast<void*>(p)) != sizes_.end(); }

// 快速路径：区域范围判别（解释器引用卫生墙常驻调用；O(#regions) 无哈希）。
bool Heap::IsHeapPointer(const void* p) const {
  if (p == nullptr) return false;
  const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
  for (const Region& r : regions_) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(r.base);
    if (addr >= base && addr < base + r.used) return true;
  }
  return false;
}

Heap::PtrShape Heap::ClassifyPtr(const void* p, const void** owner, size_t* owner_off) const {
  if (owner != nullptr) *owner = nullptr;
  if (owner_off != nullptr) *owner_off = 0;
  if (p == nullptr) return PtrShape::kNull;
  void* key = const_cast<void*>(p);
  if (sizes_.find(key) != sizes_.end()) return PtrShape::kHeapStart;
  const uintptr_t addr = reinterpret_cast<uintptr_t>(p);
  bool in_alloc_zone = false;
  for (const Region& r : regions_) {
    uintptr_t base = reinterpret_cast<uintptr_t>(r.base);
    if (addr >= base && addr < base + r.used) {
      in_alloc_zone = true;
      break;
    }
  }
  if (!in_alloc_zone) return PtrShape::kOutside;
  // 存活对象内部？（含已回收块：不命中即残骸）
  for (const auto& [op, sz] : sizes_) {
    uintptr_t ob = reinterpret_cast<uintptr_t>(op);
    if (addr > ob && addr < ob + sz) {
      if (owner != nullptr) *owner = op;
      if (owner_off != nullptr) *owner_off = static_cast<size_t>(addr - ob);
      return PtrShape::kHeapInterior;
    }
  }
  return PtrShape::kReclaimed;
}

void Heap::ForEachAllocated(void (*fn)(void* obj, void* arg), void* arg) const {
  for (const auto& [p, sz] : sizes_) {
    (void)sz;
    fn(p, arg);
  }
}

void Heap::Sweep(const std::vector<void*>& live) {
  std::unordered_map<void*, size_t> next;
  next.reserve(live.size() + pinned_.size());
  size_t used = 0;
  for (void* p : live) {
    auto it = sizes_.find(p);
    if (it == sizes_.end()) continue;
    next[p] = it->second;
    used += it->second;
  }
  // 钉住根恒保留（传递闭包由 gc.cc 标记；这里兜底防测试驱动 Sweep 漏标）
  for (void* p : pinned_) {
    if (next.find(p) != next.end()) continue;
    auto it = sizes_.find(p);
    if (it == sizes_.end()) continue;
    next[p] = it->second;
    used += it->second;
  }
  for (auto& [p, sz] : sizes_) {
    if (next.find(p) == next.end()) free_lists_[sz].push_back(p);
  }
  sizes_.swap(next);
  used_ = used;
}

}  // namespace dexvm::mem
