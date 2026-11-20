# Guest Binder

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](07-native-libraries.md) · [下一部分](09-bridge.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 17. Binder

### 17.1 Guest Binder Domain

```text
GuestBinderDomain {
  domain_id
  session_generation
  local_nodes
  remote_handles
  strong_ref_counts
  weak_ref_counts
  service_registry
  death_recipients
  binder_tasks
}
```

Handle：

```text
GuestBinderHandle {
  domain_id
  session_generation
  node_id
  object_kind
  reference_kind
}
```

Guest Java Binder wrapper 位于 Guest Heap：

- `android.os.Binder`。
- `BinderProxy`。
- AIDL `Stub` 和 `Proxy`。
- Java `android.os.Parcel` instance。
- `IBinder.DeathRecipient`。
- `ParcelFileDescriptor`。

Guest Binder Runtime 在 native owned memory 中保存：

- Guest `BBinder`/node。
- Guest `BpBinder`/remote handle entry。
- native Parcel buffer、object slots 和 FD table。
- strong/weak reference counts。
- transaction/callback queue。

### 17.2 Parcel

`Parcel` 写入和读取：

- primitive、string、array、IBinder、FD。
- length/offset 边界检查。
- Binder handle 通过 domain handle table。
- FD 使用 dup/ownership transfer。
- reply parcel 使用 transaction generation。

### 17.3 ServiceManager 和 AIDL

```text
getService(name)
  -> Guest service_registry
  -> HostBridge.lookupSystemService(name)
  -> Guest BinderProxy

addService(name, GuestBinderHandle)
  -> validate node/generation
  -> Guest service_registry[name] = node
```

AIDL Proxy 使用 `transact`，AIDL Stub 使用 `onTransact`。Stub 与 Proxy
都是 ART Guest Java 对象。

### 17.4 Transaction

```text
BinderTransact:
  resolve GuestBinderHandle
  validate generation and interface descriptor
  create TransactionContext
  same-domain local node -> invoke Stub.onTransact on current GuestTask
  cross-domain Guest node -> enqueue Guest Binder task
  host target -> HostBridge.BinderTransact
  host synchronous -> await coroutine completion
  host oneway -> return immediately
```

TransactionContext：

```text
caller_pid
caller_tid
caller_uid
caller_package
caller_user
caller_sdk
```

`clearCallingIdentity`/`restoreCallingIdentity` 使用嵌套 identity 栈。

Host Binder callback：

```text
Host Binder transaction
  -> Bridge validate handle/generation
  -> import Parcel/FD/Binder object
  -> enqueue Guest Binder task
  -> Guest Stub.onTransact
```

### 17.5 Death 和 FD

```text
linkToDeath -> register(node, generation, recipient, cookie)
death -> queue Guest binder callback
unlinkToDeath -> remove exact registration
```

FD 写入时 dup，读取时转移 ownership，parcel 释放时关闭仍由 parcel 持有的 FD。

### 17.6 Guest/Host Binder translation

Host -> Guest：

```text
Host BinderProxy / IBinder
  -> Bridge validate interface descriptor
  -> allocate HostBinderHandle
  -> create GuestBinderHandle(domain,node,generation)
  -> Guest BinderProxy
```

Guest -> Host：

```text
Guest local Binder node
  -> export strong reference
  -> Bridge create Host Binder proxy adapter
  -> host transactions enter Guest binder queue
  -> release adapter at session/death
```

引用计数：

```text
strong reference
  -> keep Guest/Host node alive

weak reference
  -> promote before transaction
  -> promotion failure returns DEAD_OBJECT
```

FD rollback：

```text
writeFileDescriptor
  -> dup fd
  -> Parcel owns duplicate

transaction success
  -> transfer ownership to reader

transaction failure
  -> close unclaimed duplicate
```

Calling identity：

```text
Host Binder callback
  -> Bridge reads driver-provided caller identity
  -> map to Guest TransactionContext

Guest transaction
  -> Guest caller identity
  -> Bridge maps to Host call window
```

Host 可信 caller identity 由 Bridge 保存在当前 Guest TransactionContext。
`clearCallingIdentity` 修改当前 Guest TransactionContext stack，
`restoreCallingIdentity` 恢复对应 token。

Oneway：

```text
same target + same caller thread
  -> preserve transaction order
```

