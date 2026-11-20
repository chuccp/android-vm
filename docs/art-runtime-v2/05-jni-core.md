# JNI 核心接口

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](04-coroutine-runtime.md) · [下一部分](06-jni-callbacks.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 15. JNI

### 15.1 初始化

```text
Runtime::Init
  -> JavaVMExt::Create
  -> Thread::Attach
       -> JNIEnvExt::Create
  -> ClassLinker roots
  -> JniIdManager::Init

Runtime::Start
  -> RegisterRuntimeNativeMethods
  -> InitNativeMethods
  -> core JNI libraries
  -> WellKnownClasses::LateInit
  -> Thread::FinishStartup
```

### 15.2 调用

```text
Guest Java native method
  -> ART native entrypoint
  -> Host Bridge ABI thunk
  -> native function
  -> Guest value/reference/exception
  -> ART
```

JNI reference 使用 ART indirect reference table。Guest JNIEnv 使用 ART
function table。每个 GuestTask 保存自己的 JNI local frame 和 exception
状态。

### 15.3 Reference

```text
local reference -> current JNI local frame
global reference -> ART globals table
weak global reference -> ART weak globals table
```

Reference 解析为 Guest mirror object。Host 对象只在 Bridge host handle table
中保存。

### 15.4 jobject / jclass 转换

JNI 参数中的 `jobject` 是 ART indirect reference：

```text
DecodeJObject(env, ref):
  entry = env.reference_table.Get(ref)   // Thread::DecodeJObject + Local/IndirectReferenceTable::Get
  validate entry generation              // MV 层；ART 对应 serial number/CheckJNI 校验
  return entry.GuestObjectHandle
```

编码 Guest object：

```text
EncodeJObject(env, object):
  env.reference_table.Add(object)        // JNIEnvExt::AddLocalReference -> LocalReferenceTable::Add
  return expected JNI type
```

JNI 类型检查根据函数和 descriptor 执行：

```text
jobject
jstring    -> java.lang.String
jclass     -> java.lang.Class
jarray     -> array class
jthrowable -> java.lang.Throwable
```

### 15.5 对象 JNI

对象创建：

```text
NewObject(class, constructorID, args):
  EnsureInitialized(class)          // ClassLinker::EnsureInitialized
  convert JNI args to JValues
  invoke constructor                // String 时改写为 StringFactory
  return local jobject

AllocObject(class):
  EnsureInitialized(class)
  allocate Guest object             // String 特判走 String::AllocEmptyString
  return local jobject
```

对象操作：

```text
GetObjectClass(object)
  -> object.header.klass -> local jclass

GetSuperclass(class)
  -> interface ? null : class.super_class -> local jclass

IsInstanceOf(object, class)
  -> object == null ? JNI_TRUE : class.IsAssignableFrom(object.class)

IsSameObject(a, b)
  -> DecodeJObject(a) == DecodeJObject(b)
```

Reference 管理函数（reference 语义见 15.3/15.4）：

```text
NewLocalRef
NewGlobalRef
DeleteLocalRef
DeleteGlobalRef
NewWeakGlobalRef
DeleteWeakGlobalRef
GetObjectRefType
```

### 15.6 Class JNI

```text
FindClass(utf8Name):
  descriptor = dot_to_slash(utf8Name)
  use initiating ClassLoader from JNIEnv/ClassLoader override
  FindClass(descriptor)
  return local jclass
```

当前 ART 版本的 JNI `DefineClass(name, loader, byteBuffer, length)` 实现为：

```text
log warning "JNI DefineClass is not supported"
return nullptr
```

需要动态定义类时，Guest 代码使用 `DexClassLoader`/`InMemoryDexClassLoader`
或 ART 对应的标准 ClassLoader 路径。

`JNI_OnLoad` 和 `Runtime.nativeLoad` 执行期间，JNIEnv 使用当前 library 对应
的 ClassLoader override。

类关系函数：

```text
GetSuperclass
IsInstanceOf
```

异常函数：

```text
Throw / ThrowNew
ExceptionOccurred
ExceptionDescribe
ExceptionClear
FatalError
```

`Class.isAssignableFrom` 通过 Guest Java/ART method invocation 实现。

### 15.7 Field JNI

解析 field ID：

```text
GetFieldID(class, name, signature):
  EnsureInitialized(class)
  resolve signature type using declaring ClassLoader
  FindInstanceField(name, exact type descriptor)
  verify access/hidden API policy
  return resolved ArtField*
```

实例 field：

```text
GetObjectField
SetObjectField
GetBooleanField / SetBooleanField
GetByteField / SetByteField
GetCharField / SetCharField
GetShortField / SetShortField
GetIntField / SetIntField
GetLongField / SetLongField
GetFloatField / SetFloatField
GetDoubleField / SetDoubleField
```

Object field 读写使用 Guest Heap reference accessor 和 ART barrier。primitive
field 使用 ArtField offset 和字段宽度。

静态 field：

```text
GetStaticFieldID
GetStatic<Type>Field
SetStatic<Type>Field
```

静态存储由定义 Class 持有。设置 object static field 执行 write barrier。
static final field 的写入应用 ART `EnsureModifiable` 规则。

### 15.8 Method JNI

解析 method ID：

```text
GetMethodID(class, name, signature):
  EnsureInitialized(class)
  FindClassMethod or FindInterfaceMethod
  match name + exact signature and static flag
  verify access/hidden API policy
  return resolved ArtMethod*

GetStaticMethodID(class, name, signature):
  EnsureInitialized(class)
  FindClassMethod or FindInterfaceMethod
  match name + exact signature and static=true
  verify access/hidden API policy
  return resolved ArtMethod*
```

调用族：

```text
CallObjectMethod
CallBooleanMethod
CallByteMethod
CallCharMethod
CallShortMethod
CallIntMethod
CallLongMethod
CallFloatMethod
CallDoubleMethod
CallVoidMethod

CallStaticObjectMethod
CallStaticBooleanMethod
CallStaticByteMethod
CallStaticCharMethod
CallStaticShortMethod
CallStaticIntMethod
CallStaticLongMethod
CallStaticFloatMethod
CallStaticDoubleMethod
CallStaticVoidMethod

Nonvirtual variants:
CallNonvirtualObjectMethod ... CallNonvirtualVoidMethod

Argument forms:
...MethodA(jvalue[])
...MethodV(va_list)
...Method(...)
```

调用流程：

```text
JNI call
  -> validate receiver/method
  -> EnsureInitialized(static declaring class)
  -> convert JNI arguments to JValue
  -> invoke ArtMethod
  -> convert return JValue
  -> create local reference for object return
```

Exception 留在 Guest ART Thread：

```text
native JNI call -> Guest exception pending
JNIEnv.ExceptionOccurred -> local Throwable ref
JNIEnv.ExceptionClear -> clear Guest exception
```

反射 ID 转换：

```text
FromReflectedMethod / ToReflectedMethod
FromReflectedField / ToReflectedField
```

method ID 本体对应 `ArtMethod`（`jni::EncodeArtMethod`/JniIdManager 索引），
field ID 对应 `ArtField`（`jni::EncodeArtField`）；`mirror::Method`/
`mirror::Constructor`/`mirror::Field` 只是 `java.lang.reflect` 反射包装对象。

### 15.9 Array JNI

对象数组：

```text
GetArrayLength(array)

NewObjectArray(length, elementClass, initialValue):
  allocate ObjectArray
  set component type
  fill initial reference

GetObjectArrayElement(array, index):
  bounds check + read_barrier -> local jobject

SetObjectArrayElement(array, index, value):
  bounds check
  ensure value is assignable to component class
  write_barrier + store HeapReference
```

primitive array：

```text
New<Type>Array
Get<Type>ArrayElements
Release<Type>ArrayElements
Get<Type>ArrayRegion
Set<Type>ArrayRegion
```

`GetPrimitiveArrayCritical` 对 primitive 数组恒返回 backing pointer
（`is_copy = JNI_FALSE`，无复制回退），并进入 GC critical 状态
（`IncrementDisableMovingGC` 或 CC/CMC 的 `IncrementDisableThreadFlip`）；
`ReleasePrimitiveArrayCritical` 退出并同步修改。

Direct buffer：

```text
NewDirectByteBuffer(address, capacity)
  -> create Guest java.nio.DirectByteBuffer
  -> attach owned native memory handle

GetDirectBufferAddress
GetDirectBufferCapacity
  -> read Guest DirectByteBuffer state
```

Native memory handle 记录 owner、capacity 和 generation，由 Bridge/Memory
Runtime 在 buffer 释放时回收。

### 15.10 String JNI

```text
NewString / GetStringLength
GetStringChars / ReleaseStringChars
GetStringRegion
GetStringUTFChars / ReleaseStringUTFChars
GetStringUTFRegion
NewStringUTF
GetStringUTFLength
```

字符串转换：

```text
UTF-16 JNI chars <-> Guest java.lang.String
Modified UTF-8 JNI <-> Guest java.lang.String
```

`NewStringUTF` 按 ART `NewStringUTFVisitor` 处理 modified UTF-8、空字符和
历史 4-byte UTF-8 输入，创建 Guest String。

### 15.11 Monitor 和 critical JNI

```text
MonitorEnter(env, object):
  resolve Guest object
  MonitorEnter using ART monitor algorithm
  wait through coroutine backend if blocked

MonitorExit(env, object):
  verify owner
  decrement monitor recursion
  wake monitor waiters
```

### 15.12 JNI local frame

```text
PushLocalFrame(capacity)
PopLocalFrame(result)
EnsureLocalCapacity(capacity)
```

Native method 返回时释放 call local frame。每个 GuestTask 保存独立 JNI local
frame 栈。Global/weak-global reference 由 JavaVMExt table 管理。

### 15.13 JNI 对象算法

```text
JNIObjectOperation(env, operation, ref, args):
  entry = env.reference_table.Get(ref)
  if invalid or generation mismatch:
      return error/dead reference

  object = entry.GuestObjectHandle.Resolve()
  result = DispatchARTJNIImplementation(
      operation,
      object,
      args)

  if result.type == OBJECT:
      return env.reference_table.Add(result.object)
  return result.value
```

### 15.14 JavaVM / JNIEnv

```text
GetJavaVM
GetVersion -> JNI_VERSION_1_6
GetEnv
AttachCurrentThread
AttachCurrentThreadAsDaemon
DetachCurrentThread
```

`AttachCurrentThread` 创建 Guest Thread peer 和 `JNIEnvExt`，并把 task 加入
ART ThreadList。Coroutine Carrier 只提供执行上下文，Guest Thread identity
由 GuestTask 保存。

### 15.15 JNI reference 生命周期

Local reference：

```text
NewLocalRef / JNI return object
  -> add to current JNIEnvExt local frame
  -> valid until DeleteLocalRef or frame pop
  -> tied to current GuestThread/task
```

`DeleteLocalRef` 删除当前 thread local frame 中的 entry；reference 属于其他
local frame 时执行 no-op（ART 对找不到的 entry 打 JNI WARNING 并 dump 栈，
不抛异常）。`PopLocalFrame(survivor)` 把 survivor 转成外层 frame
的 local reference 后释放当前 frame。Native method 返回时自动释放该 call
的所有 local reference。

Global reference：

```text
NewGlobalRef
  -> JavaVMExt global table lock
  -> add strong root
  -> valid until DeleteGlobalRef or JavaVM shutdown
```

Global/weak-global reference 可由多个 attached GuestThread 使用；local
reference 保持在创建它的 JNIEnv local frame。

Weak global：

```text
NewWeakGlobalRef
  -> JavaVMExt weak-global table
  -> GC may clear entry

NewLocalRef(weak)
  -> read barrier / weak clear check
  -> cleared ? null : local reference
```

Native 保存 jobject 的规则（GlobalRef/LocalRef/跨 GuestThread 用法）以及
JNI native frame 与 GC、Carrier 的完整协作协议见
[02-memory-gc.md](02-memory-gc.md) §6.12。

GC root visitor 扫描 JavaVMExt globals、weak globals、JNIEnvExt locals 和
monitor roots。JNI reference 只指向 ART indirect entry，Guest Heap address
由 GC 更新。

Critical array/string：

```text
GetPrimitiveArrayCritical / GetStringCritical
  -> enter GC critical state
  -> pin or copy backing
  -> release critical
```

