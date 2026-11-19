# 关键算法

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](11-startup-pipeline.md) · [下一部分](13-diagnostics-tests-and-delivery.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，third_party/art 源码快照仅作对照。

## 22. 关键算法

### 22.1 FindClass

```text
primitive -> primitive class
ClassTable lookup
existing -> EnsureResolved
array -> CreateArrayClass
boot path -> FindClassInBootClassLoaderClassPath -> DefineClass
app path -> FindClassInBaseDexClassLoader -> race-safe table insert
```

race-safe table insert 对应 03 章 FindClass 的 `verify descriptor + insert
with race handling`：并发查找同一 descriptor 时先校验再插入 ClassTable，
输掉竞争的定义方丢弃自身 Class 并复用胜者结果，保证同一
loader/descriptor 在 ClassTable 中只存在一个条目。

### 22.2 DefineClass

```text
AllocClass(kNotReady)
SetupClass(kIdx)
InsertClass
LoadClass
LoadSuperAndInterfaces(kLoaded)
LinkClass(kResolved)
```

### 22.3 LinkClass

```text
LinkSuperClass
LinkMethods
LinkInstanceFields
LinkStaticFields
publish vtable/iftable/field layout
```

### 22.4 InitializeClass

```text
lock class
verify/resolve state
set initializing + owner
initialize superclass
initialize default-method interfaces
apply encoded static values
invoke <clinit>
mark initialized or store failure
notify waiters
make visibly initialized
```

### 22.5 DoCall

```text
decode invoke inputs
resolve called ArtMethod
if called method is String.<init>:
    StringFactory rewrite path
    call StringFactory entry -> new String result
    write String result into original this vreg and its alias registers
    skip constructor execution and return
compute registers_size - inputs_size
create ShadowFrame
copy receiver and arguments
execute callee
write pending result
deliver exception
```

StringFactory 重写与 03 章 invoke 流程一致：`String.<init>` 不执行真实构造，
改走 ART StringFactory 路径生成 String，并按 ART `DoCallCommon` 更新原
`this` vreg 及其 alias 的 String 结果。

### 22.6 Binder transaction

```text
resolve GuestBinderHandle
push TransactionContext
  same-domain local -> direct Stub.onTransact with original flags
  cross-domain Guest -> Guest binder queue
host target -> Host Bridge transaction
synchronous -> coroutine await reply
oneway -> return
pop TransactionContext
```

### 22.7 Coroutine

```text
pop highest-priority RUNNABLE task
  -> bind ART Thread/FiberContext to Carrier
  -> execution_state = RUNNING_ON_CARRIER
  -> run quantum

yield:
  -> execution_state = NOT_RUNNING
  -> ReadyQueue.Push

wait:
  -> lock wait_queue
  -> register waiter / wait_state = target wait kind
  -> execution_state = NOT_RUNNING
  -> unlock wait_queue
  -> YieldCarrier -> WaitQueue.Push

wake:
  -> lock same wait_queue
  -> if waiter still WAITING_TARGET:
       wait_state = NONE
       execution_state = NOT_RUNNING
       remove from wait structure
       ReadyQueue.Push(task)
  -> unlock wait_queue
  -> Scheduler ReadyQueue.Pop
  -> bind ART Thread/FiberContext to Carrier
  -> resume execution from wait point

native:
  -> native_state = PINNED_NATIVE
  -> keep Carrier

complete:
  -> lifecycle_state = COMPLETED
```

wait/wake 在同一 wait_queue 锁内完成 waiter 注册与唤醒检查，避免 lost
wake；被唤醒 task 经 ReadyQueue 按 priority + FIFO（含 aging）重新调度后
恢复执行，与 04 章 Wait、Timer 和 IO 的调度描述一致。

### 22.8 First frame

```text
ViewRootImpl traversal
record draw commands
UI Bridge PresentFrame
wait Host Surface present
mark Interactive
```

### 22.9 JNI Object

```text
ResolveJObject(env, ref, expected_type):
  entry = env.reference_table.Get(ref)
  validate entry.generation
  if entry.kind == WEAK and cleared:
      return null
  object = entry.GuestObjectHandle.Resolve()
  verify object.class matches expected JNI type
  return object

CreateJObject(env, object):
  entry = env.local_frame.Add(object)
  return entry.jobject
```

Object operation：

```text
JNICall(env, function, jobjectArgs, primitiveArgs):
  resolve every jobject/jclass/array/string argument
  resolve fieldID/methodID to ArtField/ArtMethod
  execute ART JNI implementation
  convert object result to local reference
  preserve pending Guest exception
  return JNI result
```

Field operation：

```text
GetField(object, fieldID):
  DecodeJObject(object)
  field = DecodeArtField(fieldID)
  if field.static:
      address = field.declaring_class.static_storage + field.offset
  else:
      address = object + field.offset
  return typed_read(address, field.type, barrier)
```

Method operation：

```text
CallMethod(object, methodID, args):
  receiver = DecodeJObject(object)
  method = DecodeArtMethod(methodID)
  values = ConvertJNIArguments(args, method.signature)
  result = InvokeART(receiver, method, values)
  return ConvertJNIResult(result)
```

### 22.10 GC

Allocation：

```text
AllocateObject(klass, size):
  check class allocability
  allocator = Heap.current_allocator
  object = allocator.Alloc(klass, size)      // AllocObjectWithAllocator
  if object == null:
      // Heap::AllocateInternalWithGc 多阶段策略
      wait for in-flight GC (kGcCauseForAlloc) and retry once
      run one blocking GC (next_gc_type_)
      loop full-heap GC clearing SoftReferences
        while freed bytes productive and attempts < kMaxGcAttempts (5)
      object = allocator.Alloc(klass, size)
  if object == null:
      throw OutOfMemoryError
  initialize object fields
  return object
```

分配失败进入 `Heap::AllocateInternalWithGc` 多阶段重试（与 ART 一致）：
等待在跑 GC 后重试 → 按 `next_gc_type_` 跑一次阻塞 GC → 清 SoftReference
的全堆 GC 循环（`kMaxGcAttempts = 5`，并要求释放量满足生产率条件）；仍失败
抛 OutOfMemoryError，不返回 null。MV 实现中等待 GC 经由 coroutine wait 完成。

Collection：

```text
Collect(gc_type, cause):
  acquire gc_complete_lock_
  wait for collector_type_running_ == kCollectorTypeNone
  choose collector
  request suspend/checkpoint
  visit roots
  collector.Mark / Copy / Sweep / Compact
  process references
  release critical restrictions
  broadcast gc_complete_cond_
```

`gc_complete_lock_` 是保护 GC 完成条件的互斥锁，`collector_type_running_`
记录当前正在运行的 collector 类型（`kCollectorTypeNone` 表示无收集器运行），
`gc_complete_cond_` 为对应条件变量；三者沿用 ART `gc::Heap` 成员名。

Moving GC reference update：

```text
UpdateReference(ref):   // 对应 MarkCompact::UpdateRef / ReadBarrier 路径
  if collector is moving:
      if ref in from-space/compaction range:
          to_object = PostCompactAddress(ref) / ReadBarrier::Mark(ref)
          write to_object into root/frame/field (UpdateRoot)
          return to_object
      return ref
  else:
      non-moving collector: object address unchanged
      return ref unchanged
```

非移动收集器（MarkSweep/non-moving space）下对象地址不变，不重写
root/frame/field 中的引用，仅返回原值。moving 路径对应 ART 实现：
CMC 压缩期为 `MarkCompact::UpdateRef`/`UpdateRoot`/`PostCompactAddress`
（按 `[begin,end)` 范围判定 from-space），CC 运行期为
`ReadBarrier::Barrier`/`BarrierForRoot`/`Mark`。

Coroutine task 在 safepoint 保存：

```text
ART ThreadState
ShadowFrame register map
managed stack roots
JNI local frame
monitor ownership
```

### 22.11 JNI callback

Synchronous native -> Java：

```text
GetEnv(vm)
  -> if detached: AttachCurrentThread
  -> PushLocalFrame
  -> resolve/cached jclass + jmethodID
  -> convert callback args
  -> Call<Type>Method
  -> preserve pending exception
  -> PopLocalFrame
  -> DetachCurrentThread when callback owns the attached thread
```

Async callback：

```text
callback := CallbackEvent(
    session_generation,
    target GuestTask,
    target Handler,
    GlobalRef callback target,
    method id,
    callback args)

enqueue callback
  -> ReadyQueue.Push(target)
  -> bind JNIEnv
  -> PushLocalFrame
  -> Call<Type>Method
  -> deliver exception to Guest handler
  -> PopLocalFrame
```

Callback cache：

```text
class -> GlobalRef
method -> jmethodID
target object -> GlobalRef or WeakGlobalRef
session generation -> callback context generation
```

Weak callback target：

```text
value = NewLocalRef(weak_target)
if value == null:
    skip callback
else:
    invoke callback(value)
```

