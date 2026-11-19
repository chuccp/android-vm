// Guest Heap（docs 02 章）：非移动 arena + 空闲链复用 + mark-sweep
// + 分配失败 GC 重试链（docs 12 章 22.10 AllocateInternalWithGc）
#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace dexvm::runtime {
struct ObjHeader;
class VClass;
}  // namespace dexvm::runtime

namespace dexvm::mem {

// 挂起 GuestTask 的 fiber 栈/寄存器保存区枚举（gc.cc 保守栈扫描消费）：
// add(base, bytes, arg) 登记一段需保守扫描的内存区。由 core/builtins.cc 在
// GuestSchedOps 装接时注册（经 ops->visit_stacks 调入 coroutine/）；不链
// coroutine/ 的二进制不注册 = 仅扫本线程 C++ 栈。
using StackRangeAddFn = void (*)(void* base, size_t bytes, void* arg);
using StackRangeVisitorFn = void (*)(void* ctx, StackRangeAddFn add, void* arg);
void SetGlobalStackProvider(StackRangeVisitorFn fn, void* ctx);
void VisitGlobalStackRanges(StackRangeAddFn add, void* arg);

class Heap {
 public:
  // ART kMaxGcAttempts（docs 12 章 22.10）：分配失败重试链的全堆 GC 轮数上限。
  static constexpr int kMaxGcAttempts = 5;

  // 默认容量 256MB（业务负载 64MB 偏小——messagePump 3.5 分钟即耗尽；设备 RAM 允许）。
  explicit Heap(size_t capacity = 256 * 1024 * 1024);
  ~Heap();

  // 分配 size 字节并清零；先查空闲链；失败进入 22.10 AllocateInternalWithGc
  // 重试链（gc_trigger_ 回收后重试，至 kMaxGcAttempts）；仍失败返回 nullptr
  //（调用方抛 OutOfMemoryError）。未装 GC 触发器时行为同旧语义（直接 nullptr）。
  void* Alloc(size_t size);

  // 分配失败 GC 接线：trigger(ctx) = 一轮 mark-sweep（core/gc.cc CollectGarbage）。
  // 单流同步模型下无「在跑 GC」可等待——重试链即刻触发即刻完成；
  // 重入（GC 过程中的分配失败）直接返回 nullptr 防递归。
  using GcTriggerFn = void (*)(void* ctx);
  void SetGcTrigger(GcTriggerFn fn, void* ctx);

  // GC 钉住根（ART pre-allocated OOM 等）：Sweep 恒保留；gc.cc 标记传递闭包入口。
  void Pin(void* obj);
  const std::vector<void*>& pinned() const { return pinned_; }

  // GC 观测（runtime_contract 堆水位行扩展）：轮数/累计回收字节/
  // 重试链救回（分配失败→GC→重试成功）次数。
  void NoteGcRun(size_t freed_bytes);
  size_t gc_runs() const { return gc_runs_; }
  size_t gc_freed_bytes() const { return gc_freed_bytes_; }
  size_t alloc_recovered() const { return alloc_recovered_; }

  // 是否为在堆对象起始地址（存活对象）
  bool IsHeapStart(const void* p) const;

  // 是否为堆分配区地址（快速范围判别：O(#regions)，无哈希查找）。
  // 解释器引用卫生墙的常驻判真路径——ART debug IsValidObject 同型空间范围
  // 判别；完全在堆外的野指针仍会落到诊断路径。
  bool IsHeapPointer(const void* p) const;

  // 脏指针形态诊断（CheckRef 卫生墙注记；仅失败路径调用，O(live)）：
  //   kNull        空
  //   kHeapStart   存活对象起点（调用方已先行判真）
  //   kHeapInterior 落在某存活对象【内部】（owner/owner_off 给出包含对象与
  //                内偏移——半写/槽错位的直接证据）
  //   kReclaimed   落在堆分配区（[base,used)）但不落在任何存活对象——
  //                清扫残骸（死对象槽：槽存旧指针、对象已被 mark-sweep 回收）
  //   kOutside     完全在堆分配区外（栈/映射垃圾位型）
  enum class PtrShape : uint8_t { kNull, kHeapStart, kHeapInterior, kReclaimed, kOutside };
  PtrShape ClassifyPtr(const void* p, const void** owner, size_t* owner_off) const;

  // 遍历全部在堆对象起点（gc.cc framework 单例兜底扫描用）
  void ForEachAllocated(void (*fn)(void* obj, void* arg), void* arg) const;

  // 清扫：保留 live 中的块（+ pinned 钉住根），其余按块回收进空闲链复用
  void Sweep(const std::vector<void*>& live);

  size_t bytes_allocated() const { return used_; }
  size_t capacity() const { return capacity_; }
  size_t live_count() const { return sizes_.size(); }

 private:
  struct Region {
    uint8_t* base;
    size_t size;
    size_t used;
  };
  // 纯分配（空闲链复用 + arena bump + 扩区）；失败返回 nullptr，无 GC。
  void* AllocRaw(size_t size);

  // p..p+size 是否完全落在某个 region 内（空闲链坏块诊断用）。
  bool InAnyRegion(const void* p, size_t size) const;

  std::vector<Region> regions_;
  std::unordered_map<void*, size_t> sizes_;            // 存活对象 -> 块大小
  std::unordered_map<size_t, std::vector<void*>> free_lists_;
  size_t capacity_;
  size_t used_ = 0;

  GcTriggerFn gc_trigger_ = nullptr;
  void* gc_ctx_ = nullptr;
  bool in_alloc_retry_ = false;  // 重试链重入护栏
  size_t bad_free_blocks_ = 0;   // 空闲链坏块丢弃计数（堆元数据被踩诊断）
  std::vector<void*> pinned_;    // 钉住根（Sweep 恒保留）
  size_t gc_runs_ = 0;
  size_t gc_freed_bytes_ = 0;
  size_t alloc_recovered_ = 0;
};

}  // namespace dexvm::mem
