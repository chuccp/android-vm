# 原生协程运行时

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](03-bootstrap-and-class-loading.md) · [下一部分](05-jni-core.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 14. 原生协程

### 14.1 执行实体关系

```text
GuestTask      1 : 1   ART Thread / Java Thread
GuestTask      N : M   Carrier
Carrier        0..1    active GuestTask
```

Guest Java `Thread.start` 创建的 task 与 ART Thread、Java Thread peer 一一
对应。Guest native pthread task 在 attach 后得到对应 Java Thread peer。

```text
Carrier
  -> current GuestTask
       -> FiberContext
       -> ART Thread
       -> Java Thread peer
       -> ManagedStack / ShadowFrame
       -> JNIEnvExt
       -> ThreadLocal
       -> monitor ownership
```

ART Thread 属于 GuestTask。Carrier 是宿主执行资源，在 task switch 时绑定
目标 GuestTask 的 ART Thread。

### 14.2 GuestTask

```text
GuestTask {
  task_id
  session_generation

  ART Thread* art_thread
  FiberContext fiber
  Carrier* current_carrier

  lifecycle_state
  execution_state
  wait_state
  native_state
  gc_state
  exception_state

  priority
  deadline
  statistics
}
```

状态采用正交字段：

```text
lifecycle_state:
  CREATED / RUNNABLE / COMPLETED / CANCELLED

execution_state:
  NOT_RUNNING / RUNNING_ON_CARRIER

wait_state:
  NONE / MUTEX / MONITOR / CONDITION / IO /
  DEADLINE / JOIN / BINDER / JNI_ONLOAD

native_state:
  NONE / PINNED_NATIVE

gc_state:
  RUNNABLE / SAFEPOINT_REQUESTED / SUSPENDED

exception_state:
  NONE / PENDING_GUEST_EXCEPTION
```

普通 Java exception 在 frame 内被 catch 后，GuestTask 继续 RUNNABLE。
未处理异常导致 task 结束时记录 completed-with-exception。

### 14.3 FiberContext

```text
FiberContext {
  NativeContext native_context
  RegisterState saved_registers

  stack_base
  stack_size
  stack_pointer
  guard_page
  stack_generation

  ART Thread* art_thread
  JNIEnvExt* jni_env
  ManagedStack* managed_stack
  BaseHandleScope* top_handle_scope
  ShadowFrame* top_shadow_frame

  safepoint_state
  native_transition_state
}
```

FiberContext 保存恢复执行需要的上下文和 ART 原生对象引用。ART ThreadState、
managed stack、HandleScope、JNI local frame、exception 和 monitor ownership
仍由 ART Thread 持有。

`jni_env`、`managed_stack`、`top_handle_scope` 和 `top_shadow_frame` 在 snapshot
时指向 `art_thread` 持有的 ART 原生结构，用于恢复和 root visitor；它们的
状态所有权仍属于 ART Thread。

### 14.4 Carrier

```text
Carrier {
  carrier_id
  pthread_t host_thread
  NativeContext host_context

  GuestTask* current_task
  ART Thread* installed_thread

  state
  native_pin_count
  stack_boundary
}
```

Carrier state：

```text
IDLE
READY
RUNNING_TASK
PINNED_TO_TASK
STOPPING
```

Carrier pool 根据 runnable task 数量、native pin 数量和 CPU 配额动态调整。

### 14.5 Carrier switch

```text
SwitchCarrierToTask(carrier, task):
  assert carrier.current_task == null
  assert task.execution_state == NOT_RUNNING
  assert task.native_state == NONE
  assert task.gc_state != SUSPENDED

  save carrier host context
  install task.art_thread as Thread::Current
  set pthread/Android ART TLS slot to task.art_thread
  update art_thread.tlsPtr_.pthread_self carrier binding
  install task stack boundaries
  install task thread entrypoints
  install task card table/read barrier state
  install task suspend trigger/checkpoint state
  restore task.fiber.saved_registers

  carrier.current_task = task
  carrier.installed_thread = task.art_thread
  task.execution_state = RUNNING_ON_CARRIER
  resume task
```

Switch 发生的点：

- managed interpreter safepoint。
- nterp/quick/JIT entrypoint return。
- explicit coroutine yield。
- wait/deadline/IO completion。
- native frame 已返回且 JNI local frame 已关闭。

Managed execution 使用 ART Thread 作为 `Thread::Current()` 身份。Native
execution 在 pin 后保持 Carrier 的 `pthread_self`、host TID 和 native TLS
稳定，native 代码中的 pthread/TLS 调用与该 GuestTask 生命周期一致。

`art_thread.gettid`、Java Thread peer 和 Guest Thread name 属于 GuestTask
逻辑身份；`tlsPtr_.pthread_self` 属于当前 Carrier 绑定。Thread adapter 在
每次 carrier switch 时更新 carrier binding，并保持 Guest identity 稳定。

### 14.6 Fiber stack

```text
stack mapping
  -> guard page
  -> usable stack
  -> stack top/boundary
```

切换 stack 时同步更新：

- ART Thread native stack begin/end/guard。
- current stack pointer。
- stack overflow detection。
- signal handler 可见的 active task。
- compiled frame stack map 范围。
- Java stack trace 使用的 managed stack。
- GC Thread root 扫描范围。

Carrier 使用 host alternate signal stack。信号进入时通过 Carrier TLS 找到
当前 GuestTask，再使用该 task 的 ART Thread 和 stack metadata。

### 14.7 Scheduler

```text
SchedulerLoop(carrier):
  while runtime_running:
      task = ReadyQueue.Pop()

      if task == null:
          wait_for_event_or_idle_timeout()
          continue

      SwitchCarrierToTask(carrier, task)
      reason = RunTaskQuantum(task)

      switch reason:
        YIELD -> ReadyQueue.Push(task); ReleaseCarrier(carrier)
        WAIT  -> WaitQueue.Push(task); ReleaseCarrier(carrier)
        IO    -> IOReactor.Register(task); ReleaseCarrier(carrier)
        SLEEP -> TimerHeap.Push(task.deadline); ReleaseCarrier(carrier)
        DONE  -> CompleteTask(task); ReleaseCarrier(carrier)
        PIN   -> 不释放 carrier：carrier 继续执行该 task 的 native frame，
                 调度器跳过该 carrier，由其他/新增 carrier 承担 ReadyQueue
                 （见 14.9 Native pin）；native 返回后经 LeaveNative
                 决定继续 managed 执行或回到调度器

ReleaseCarrier(carrier):
        carrier.current_task = null
        carrier.installed_thread = null
```

优先级：

```text
P0 lifecycle/startup
P1 main Looper/UI callback
P2 framework/normal Java task
P3 background task
```

ReadyQueue 使用 priority + FIFO，并进行 aging，避免高优先级任务长期占用。

Carrier pool：

```text
desired = runnable_tasks + pinned_native_tasks
min_carriers = configured_min
max_carriers = configured_max

desired > idle_carriers -> create carrier
idle_carriers > min_carriers -> retire carrier
```

### 14.8 Wait、Timer 和 IO

```text
mutex wait       -> Mutex waiter list
monitor wait     -> object monitor waiter list
condition wait   -> Condition wait queue
sleep/deadline   -> TimerHeap
join/future      -> task completion wait channel
socket/file IO   -> IOReactor
binder reply     -> Binder completion wait
JNI_OnLoad       -> SharedLibrary condition wait
```

Wake 流程：

```text
event complete
  -> locate waiting GuestTask
  -> remove from wait structure
  -> set execution_state = NOT_RUNNING
  -> set wait_state = NONE
  -> ReadyQueue.Push(task)
```

避免 lost wake 的等待顺序：

```text
lock wait_queue
while predicate not satisfied:
    register waiter task
    wait_state = target kind
    execution_state = NOT_RUNNING
    unlock wait_queue
    YieldCarrier(task)
    lock wait_queue
unlock wait_queue
```

Wake 在同一 wait queue lock 内完成：

```text
lock wait_queue
if waiter.state == WAITING_TARGET:
    waiter.wait_state = NONE
    waiter.execution_state = NOT_RUNNING
    remove waiter
    ReadyQueue.Push(waiter)
unlock wait_queue
```

### 14.9 Native pin

```text
EnterNative(task):
  task.native_state = PINNED_NATIVE
  carrier.native_pin_count++
  task.current_carrier = current_carrier

LeaveNative(task):
  pop JNI local frame
  carrier.native_pin_count--
  task.native_state = NONE

  native frame 返回 managed 继续执行（普通 native 方法返回）:
    task 继续在当前 carrier 上执行
    execution_state 保持 RUNNING_ON_CARRIER
    notify scheduler 可回收多余 carrier

  native frame 结束同时让出/结束 task（显式 yield、DetachCurrentThread、
  task 结束）:
    task.current_carrier = null
    task.execution_state = NOT_RUNNING
    wake scheduler
```

Native frame 期间 Carrier 保持 pthread、native stack、signal stack、JNIEnv 和
native TLS。调度器使用其他/新增 Carrier 执行 runnable GuestTask。pin 期间
不发生 carrier switch，因此 14.5 中 TLS/entrypoints 的重装不会打断 native
执行；"native 期间 pthread/TLS 稳定"由该约束保证。

### 14.10 Guest main Looper

Guest main task 执行：

```text
ActivityThread.main
  -> prepareMainLooper
  -> attach
  -> Looper.loop
```

Looper 实现：

```text
while active:
  message = next_message(now)
  if message == null:
      coroutine wait(next deadline / wake event)
  else:
      dispatch message on main GuestTask
```

Host Input、VSync、Binder 和 device callback 写入 Guest message queue 后，
wake main task。

### 14.11 Native pthread

Guest native library 的 `pthread_create` 通过 Native Thread Manager：

```text
Guest namespace preloads libdexvm_guest_pthread shim
  -> target library resolves pthread_create from guest shim

pthread_create(start, arg)
  -> create GuestTask
  -> create native stack/context
  -> AttachCurrentThread(GuestJavaVM)
  -> run native start routine
  -> DetachCurrentThread
  -> complete GuestTask
```

Guest shim 同时覆盖 `pthread_join`、`pthread_detach`、pthread key/TLS、
mutex/condition 中需要进入 coroutine runtime 的入口。

需要专用 OS thread 语义的 native library 使用 dedicated Carrier：

```text
Carrier.native_pin_count = 1
task.native_state = PINNED_NATIVE
```

Java `Thread.start` 创建 GuestTask 和 ART Thread peer，再由 Carrier pool
调度。

### 14.12 GC safepoint

```text
GC RequestCheckpoint(task)
  -> task.gc_state = SAFEPOINT_REQUESTED
  -> scheduler checks task at safe boundary
  -> save registers/stack roots
  -> task.gc_state = SUSPENDED
  -> GC visits Thread/JNI/Handle/monitor roots
  -> GC completes
  -> task.gc_state = RUNNABLE
  -> ReadyQueue.Push(task)
```

不同执行状态：

```text
managed RUNNABLE
  -> request checkpoint
  -> suspend at safepoint

WAITING
  -> scan Thread/JNI/Handle/monitor roots directly

PINNED_NATIVE
  -> ART Thread is kNative
  -> JNI local/global/weak references remain roots
  -> GC may complete while native function runs
  -> re-entry to managed observes pending suspend request
```

Compiled frame 使用 ART stack maps；interpreter 使用 ShadowFrame 和
register map；JNI native frame 使用 JNI reference table；native pointer
需要稳定地址时使用 critical/element access。

