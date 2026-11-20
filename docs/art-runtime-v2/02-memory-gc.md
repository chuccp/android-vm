# Guest 内存与 GC

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](01-foundation.md) · [下一部分](03-bootstrap-and-class-loading.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 6. 内存管理

`libdexvm.so` 拥有全部 MV 内存。

| 资源 | 生命周期所有者 | 内存/映射提供者 |
| --- | --- | --- |
| Guest Object、Class、String、Array | ART `gc::Heap` | Memory Runtime |
| DexFile、DexCache、ArtMethod、ArtField | Boot: ClassLinker；App: Session DexFileSet | Memory Runtime |
| DEX/VDEX/OAT/image mapping | 对应 DexFileSet owner | Memory Runtime |
| JIT code、profile | ART Core | Memory Runtime |
| Guest Thread、ShadowFrame、coroutine stack | Coroutine Runtime | Memory Runtime |
| JNI local/global/weak references | ART JNI | Memory Runtime |
| Guest native library mapping | Native Manager | Memory Runtime |
| Surface buffer、InputChannel、Binder proxy、宿主 fd | Host Android | Host Android |

Bridge 使用 opaque handle 引用宿主资源。Guest Heap 只保存 Guest 对象和
generationed host handle。

顶层 owner 是 `libdexvm.so`。每个资源只设置一个生命周期 owner；Memory
Runtime 提供 mmap/munmap、fd backing 和释放服务，不接管 ART Heap、JIT 或
DexCache 的语义所有权。

### 6.1 Guest 对象布局

Guest Object 使用 ART `mirror` 布局：

```text
ObjectHeader {
  klass / HeapReference<Class>
  monitor / LockWord
}
```

对象实例部分由 `mirror::Class` 的 field layout 决定：

```text
object address
  + ObjectHeader
  + superclass instance fields
  + subclass instance fields
```

字段读取使用 `ArtField` 的 resolved offset：

```text
GetField(object, ArtField):
  offset = ArtField.GetOffset()
  return typed_read(
      object_address + offset,
      field_type,
      read_barrier)

SetField(object, ArtField, value):
  if reference field:
      value = read_barrier_or_to_space(value)
      write_barrier(object, offset, value)
  typed_write(object_address + offset, value)
```

reference field 使用 HeapReference 和 ART read/write barrier。primitive field
按 I/Z/C/S/J/F/D/B 的 ART 宽度读取。

### 6.2 对象分配

```text
AllocateInstance(klass):
  EnsureInitialized(klass)
  size = klass.object_size
  object = ART_heap.AllocObject(klass, size)
  initialize_reference_fields(object, null)
  initialize_primitive_fields(object, zero)
  return object
```

数组：

```text
ObjectArray:
  ObjectArrayHeader
    -> klass
    -> monitor
    -> length
    -> HeapReference<Object>[length]

PrimitiveArray:
  ArrayHeader
    -> klass
    -> monitor
    -> length
    -> primitive data[length]
```

`String` 使用 ART `mirror::String` layout 和 StringFactory/String accessor。
`String` 的 UTF-8/UTF-16/MUTF-8 转换通过 ART string API 完成。

JNI `AllocObject(String.class)` 对 String 有特判：调用
`String::AllocEmptyString` 分配合法空串，而不是未初始化对象（与 ART
`jni::AllocObject` 一致）。`AllocEmptyString` 同时是 new-instance、
`Class.allocateInstance` 等 "先分配空串再填充" 的通用路径。
`NewObject(String.class, <init>, args)` 走 ART `StringFactory` 路径。

### 6.3 Object identity 和 handle

Guest 对象地址可以在 GC moving/compacting 过程中变化。跨 coroutine、JNI、
Binder 和 Session 边界使用 generationed handle：

```text
GuestObjectHandle {
  session_generation
  object_id
  class_descriptor
  kind
}
```

`object_id` 映射到 ART indirect reference entry 或 Guest Heap entry。两种
entry 均持有 `GuestObjectHandle`；GC 更新 handle 内部的 object reference，
handle 本身保持稳定。

`IsSameObject(a, b)` 解码两个 Guest reference，比较同一个 Guest object
identity。`GetObjectClass` 从 object header 取得 `mirror::Class`，再得到一个
Guest class reference。

### 6.4 GC Heap 和 allocator

Heap 由 ART `gc::Heap` 管理，包含：

```text
image spaces
zygote space
bump pointer space
region space
rosalloc/dlmalloc space
non-moving space
large object space
```

allocator：

```text
kAllocatorTypeBumpPointer
kAllocatorTypeTLAB
kAllocatorTypeRosAlloc
kAllocatorTypeDlMalloc
kAllocatorTypeNonMoving
kAllocatorTypeLOS
kAllocatorTypeRegion
kAllocatorTypeRegionTLAB
```

普通小对象流程：

```text
Heap::AllocObject
  -> CheckPreconditionsForAllocObject
  -> current allocator
  -> TLAB fast path
  -> if insufficient:
       refill TLAB or allocate directly
  -> allocation callback / instrumentation
  -> return Guest object
```

大对象进入 LOS。不可移动对象进入 non-moving allocator。`AllocObjectWithAllocator`
统一执行尺寸、class、GC 状态和 allocator 选择。

### 6.5 GC type 和 collector

GC plan：

```text
kGcTypeSticky
kGcTypePartial
kGcTypeFull
```

collector：

```text
kCollectorTypeMS
kCollectorTypeCMS
kCollectorTypeCMC
kCollectorTypeCMCBackground
kCollectorTypeSS
kCollectorTypeCC
kCollectorTypeCCBackground
kCollectorTypeHeapTrim
kCollectorTypeHomogeneousSpaceCompact
```

实现保留 ART 的 collector：

- MarkSweep / PartialMarkSweep / StickyMarkSweep。
- Concurrent Mark Sweep。
- Mark Compact。
- Semi Space。
- Concurrent Copying。
- Homogeneous Space Compact。

GC 选择输入：

```text
allocation pressure
heap growth limit
min free / max free
background GC threshold
native allocation watermark
collector transition state
continuous GC mode
generational GC mode
```

GC cause 使用 ART `GcCause`：

```text
kGcCauseForAlloc
kGcCauseBackground
kGcCauseExplicit
kGcCauseForNativeAlloc
kGcCauseCollectorTransition
kGcCauseTrim
kGcCauseInstrumentation
kGcCauseDebugger
kGcCauseClassLinker
kGcCauseJitCodeCache
kGcCauseHprof
kGcCauseProfileSaver
```

### 6.6 GC roots

GC root visitor 覆盖：

```text
JavaVM global references
JavaVM weak-global references
JNI local references
JNI monitor references
Guest Thread roots
ShadowFrame registers
managed stack references
HandleScope roots
ClassLinker class roots
ClassTable entries
DexCache arrays
ArtMethod/ArtField roots
InternTable
Binder native strong references (MV/宿主，非 ART root)
coroutine callback/task strong references (MV，非 ART root)
JIT roots
image roots
preallocated exceptions
sentinel
runtime resolution/IMT methods
heap allocation records
```

Root 收集流程：

```text
suspend runnable GuestThreads
  -> visit thread roots
  -> visit non-thread roots
  -> visit concurrent roots
  -> mark reachable objects
  -> process weak references
```

Guest coroutine 保存 ShadowFrame、managed stack、ThreadLocal 和 JNI local
frame，GC 通过 GuestThread/task root visitor 扫描这些位置。

### 6.7 Barrier 和 moving GC

reference 写入：

```text
SetReference(object, field, value):
  value = ToSpaceIfNeeded(value)
  WriteBarrier(object, field.offset, value)
  store_value
```

reference 读取：

```text
GetReference(object, field):
  value = load_value
  return ReadBarrier(value)
```

实现保留：

- card table / mod-union table。
- remembered set。
- read barrier table。
- baker / userfaultfd read barrier。
- immune spaces。
- concurrent marking bitmap。
- mark stack / GC task queue。

moving GC 执行时更新：

- Guest Heap object address。
- ShadowFrame reference slots。
- JNI local/global/weak references。
- Class/DexCache/ArtMethod/ArtField roots。
- monitor roots。
- Binder/HandleScope 中保存的 GuestObjectHandle。

### 6.8 GC 执行算法

```text
CollectGarbageInternal(gc_type, cause, clear_soft_references, gc_num):
  transition current Guest Thread to kWaitingPerformingGc
  wait until previous GC complete
  if moving GC disabled by critical section:
      skip this GC (log warning, return kGcTypeNone)
  choose collector from Heap configuration
  suspend mutator threads / request safepoints
  visit roots
  mark reachable objects
  process weak/soft/phantom/finalizer references
  sweep or copy/compact
  update moved references and barriers
  update heap accounting
  resume suspended GuestThreads
  retry blocked allocation
```

并发 GC：

```text
start GC task
  -> capture concurrent roots
  -> concurrent mark / copy
  -> final pause
  -> process remaining roots and weak references
  -> finish GC
  -> wake allocation waiters
```

每种 collector 按 ART 源码实现对应阶段和 barrier。

### 6.9 GC 与 coroutine

GC 线程和 worker task 使用 ART `Thread` 语义。Guest logical Thread
仍然处于 suspend/checkpoint 状态：

```text
GC requests suspend
  -> GuestTask reaches safepoint
  -> task yields Carrier
  -> ThreadState becomes suspended/waiting
  -> GC visits task roots
  -> GC completes
  -> task resumes at safepoint
```

同步等待、GC complete、collector transition 和 native allocation watermark
使用 coroutine condition/deadline backend。Carrier 上运行的 native frame 在
GC 请求点检查 safepoint 或由 ART thread-suspend 机制处理。

`GetPrimitiveArrayCritical`/`GetStringCritical` 增加
`disable_moving_gc_count_`（CC/CMC 下为 `IncrementDisableThreadFlip`）；
primitive 数组恒返回 backing pointer 不复制，压缩 String 走复制。

### 6.10 Reference 和 native allocation

Weak/soft/phantom/finalizer reference 由 ART `ReferenceProcessor`、
`ReferenceQueue` 和 finalizer 路径处理。

```text
process cleared weak/soft references
  -> enqueue reference if needed
  -> finalizable object path
  -> native heap GC reporting
```

ClassLoader 使用 ART weak-root 处理。collector 支持 class unloading 时，
清除无强引用的 ClassLoader、ClassTable、ClassExt、DexCache 和关联
ArtMethod/ArtField 记录。

Native memory：

```text
VMRuntime.registerNativeAllocation / native allocation callback
  -> Heap::RegisterNativeAllocation(bytes)
  -> increase native allocation watermark
  -> trigger GC cause kGcCauseForNativeAlloc
  -> Heap::RegisterNativeFree(bytes)
```

### 6.11 Heap diagnostics

GC 指标：

```text
gc count / cause / type / collector
concurrent vs blocking
pause time
bytes allocated / freed
live bytes / heap capacity
TLAB refill count
large object bytes
non-moving bytes
GC worker task count
root visit time
reference processing time
```

### 6.12 JNI Native Frame GC 协议

Guest Java 调用 native 时：

```text
ART managed frame
  -> JNI transition to kNative
  -> create/enter JNIEnvExt local frame
  -> native C/C++ frame
  -> return
  -> pop JNI local frame
  -> transition back to managed frame
```

GC 可见引用来自：

```text
current ART Thread root
JNI local references
JNI global/weak-global references
HandleScope roots
monitor roots
shadow frame registers
```

Native C/C++ 局部指针按 JNI 规范处理：

```text
native method 返回后继续使用 Guest object
  -> 保存为 GlobalRef

只在 native method 内使用
  -> LocalRef 保持到 call local frame 退出

需要直接访问 array contents
  -> GetPrimitiveArrayCritical / Get<Type>ArrayElements
  -> release 前保持对应 critical/element 状态
```

Native Frame 与 Carrier：

```text
managed safepoint
  -> GuestTask 可在 Carrier 间迁移

native frame active
  -> Carrier pinned
  -> native frame 返回后再进入可迁移状态
```

长时间 native wait 由 Native Manager 标记为 pinned native wait。调度器可以
扩容 Carrier 让其他 GuestTask 继续运行；原 native frame 继续持有自己的
Thread/TLS/JNIEnv。

GC request：

```text
GC suspend request
  -> runnable managed task 到达 safepoint
  -> task state 变为 suspended
  -> GC visit Thread roots/JNI roots/monitor roots
  -> GC complete
  -> task resume on carrier
```

Native callback：

```text
AttachCurrentThread / guest native thread
  -> GuestThread + JNIEnvExt
  -> push JNI local frame
  -> invoke Guest Java
  -> pop local frame
  -> DetachCurrentThread when native thread exits
```

JNI reference（local/global/weak-global）的生命周期规则见
[05-jni-core.md](05-jni-core.md) §15.15。

