# Guest Native Library

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](06-jni-callbacks.md) · [下一部分](08-binder.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，third_party/art 源码快照仅作对照。

## 16. Native library

### 16.1 输入

Session 向 Native Manager 传入：

```text
guest nativeLibraryDir
guest native entry -> fd/path/hash
ABI
platform resolver
platform namespace
preloaded handle table
```

### 16.2 加载

```text
System.loadLibrary
  -> Runtime.nativeLoad(path, class_loader, caller_class)
  -> JavaVMExt::LoadNativeLibrary(
       env, path, class_loader, caller_class, error_msg)
  -> caller_location from caller DexCache
  -> lookup SharedLibrary by path
  -> android::OpenNativeLibrary(...)
  -> map ELF, read DT_NEEDED
  -> resolve/load dependencies first (recursive)
  -> relocate (DT_RELA/DT_RELR/DT_JMPREL/IFUNC/TLS)
  -> dependency DT_INIT/DT_INIT_ARRAY (依赖先于本库)
  -> library DT_INIT/DT_INIT_ARRAY
  -> JNI_OnLoad (由 ART 调用)
```

其中 map/依赖解析/重定位/DT_INIT 顺序是 dlopen（bionic linker）行为，不是
ART 代码：ART 侧实现止于 `android::OpenNativeLibrary`（libnativeloader），
返回后由 ART 查找并调用 `JNI_OnLoad`。

SharedLibrary 保存：

```text
path
handle
weak class_loader
class_loader_allocator
JNI_OnLoad state
```

其他 Guest Thread 遇到 pending JNI_OnLoad 时进入 coroutine condition wait。
JNI_OnLoad 返回状态写入 SharedLibrary。

### 16.3 RegisterNatives

```text
for each JNINativeMethod:
  require name/signature/functionPtr
  find matching native ArtMethod in class hierarchy
  optional Host Bridge ABI thunk
  ClassLinker::RegisterNative(self, method, native_method)
```

`RegisterNative` 设置 ArtMethod native entrypoint（`SetEntryPointFromJni`）；
`@CriticalNative` 且类未 visibly initialized 时延迟设置。返回 `const void*`，
可能被 JVMTI MethodCallback 改写。

### 16.4 ELF 校验

加载前读取 ELF header 和 program headers（由 linker/dlopen 内完成，非 ART
代码）：

```text
EI_MAG
EI_CLASS
EI_DATA
e_machine
e_flags
e_type
PT_LOAD / PT_DYNAMIC / PT_TLS / PT_GNU_RELRO
DT_NEEDED
DT_SONAME
DT_RELA / DT_REL / DT_RELR
DT_JMPREL / R_*_IRELATIVE / IFUNC
TLS relocations
DT_INIT / DT_INIT_ARRAY
```

ABI 选择：

```text
host arm64-v8a  -> guest AArch64 ELF
host x86_64     -> guest x86-64 ELF
```

ELF 与 Host ABI 匹配后，function pointer、JNI 参数和 native stack 使用同一
平台 ABI。ABI 不匹配时，Native Manager 返回 `UnsupportedAbi`，并记录
path、machine、class 和 expected ABI。

"同平台 ABI" 仅保证 JNI 边界的平台 ABI 一致；guest ELF 直接执行的前提是
宿主提供 ELF 加载能力且 guest/host ABI 细节一致。macOS 宿主不提供 ELF
加载能力，需要独立 ELF loader 支持；Apple arm64 与 AAPCS64 在 varargs、
小聚合体传参等细节上存在差异，此类平台需要在 Host Bridge ABI thunk 中做
ABI 适配，不能假定裸 ABI 直通。

### 16.5 Namespace 和依赖

```text
Guest library namespace
  -> Guest APK lib/<abi>
  -> DT_NEEDED dependencies
  -> Android system libraries
  -> Host platform namespace
```

Native Manager 保存：

```text
library path
namespace id
DT_NEEDED list
dependency refcount
source fd/path/hash
loaded handle
ClassLoader allocator
```

命名空间搜索顺序由 Guest Session 的 ABI、library path 和 platform resolver
生成。Guest 库的 libc/libm/libdl 等一律解析到 Guest Bionic 版本，不落到
Host platform namespace 的同名库；Host platform namespace 仅暴露桥接库和
无冲突的宿主能力，避免 guest 库与宿主 libc 混用。

### 16.6 ABI 和 JNI invoker

Native method signature 决定 JNI 参数布局：

```text
receiver
primitive/object arguments
JNIEnv*
jclass/jobject
wide registers
floating-point registers
primitive return register
```

JNI signature 只包含 JNI 类型，不包含 C struct。Native library 内部的
struct/HFA 调用由该 library 自身的 native code、编译器和 linker ABI 处理；
Guest Runtime 负责 JNI 边界和动态链接。

ART generic JNI trampoline（真实名称 quick generic JNI stub：汇编入口
`art_quick_generic_jni_trampoline` → `artQuickGenericJniTrampoline` →
`GenericJniMethodEnd`）负责：

```text
ArtMethod native entrypoint
  -> clinit check
  -> synchronized method MonitorEnter
  -> prepare JNI ABI arguments
  -> PushLocalReferenceFrame
  -> GuestJniEnv
  -> native function address
  -> return JValue
  -> PopLocalReferences / restore ART native/managed state
```

ARM64 与 x86_64 的寄存器、栈对齐、浮点参数和返回布局由对应 ART quick JNI
stub/entrypoint 实现。

### 16.7 Native thread

Guest native library 创建 thread 时：

```text
native pthread entry
  -> create GuestTask/native stack
  -> attach to Guest JavaVM
  -> create JNIEnvExt and Java Thread peer
  -> execute native entry
  -> DetachCurrentThread
  -> destroy GuestTask
```

Native thread 进入 Guest Java 前 attach，返回 native 后保留 native context；
退出时清理 JNI local frame、monitor 和 ThreadList entry。

### 16.8 Library unload

```text
class loader weak reference cleared
  -> Libraries::UnloadNativeLibraries (收集失效 library；boot loader 跳过)
  -> find JNI_OnUnload
  -> call JNI_OnUnload(GuestJavaVM, nullptr)
  -> delete SharedLibrary (DeleteWeakGlobalRef)
  -> CloseNativeLibrary (dlclose，释放映射并递减依赖引用)
```

与真实 ART 一致：unload 不做 native frame 计数等待（竞争时短暂重试），
也不清理 RegisterNatives 注册项（dlclose 后函数指针由加载器语义兜底）。

MV 扩展：Session 销毁时对 Native Manager 请求 unload；仍有 native frame 时
把 library 标记为 pending-unload，frame 返回后完成释放。

