# Bootstrap、Classpath、Session 与 ClassLinker

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](02-memory-gc.md) · [下一部分](04-coroutine-runtime.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 7. Runtime 初始化与启动

### 7.1 Runtime::Create / Runtime::Init

调用 ART：

```text
Runtime::Create
  -> Runtime::Init
```

实现顺序：

1. 保存 environment、runtime options、boot class path 和 class path。
2. 初始化 `MemMap`，创建 `OatFileManager` 和 `JniIdManager` 实例。
3. 初始化 `ThreadList`、`InternTable`、monitor pool/list。
4. 创建 Heap 和 GC spaces。
5. 初始化 signal handlers 和 fault handlers（CPU/ISA features 检测在步骤 4
   创建 Heap 之前执行）。
6. 创建 `JavaVMExt` 及 global/weak-global reference tables。
7. 执行 `Thread::Startup()`。
8. 执行 `Thread::Attach("main", create_peer=false)`，创建 main `JNIEnvExt`。
9. 把 attached main ART Thread、main `JNIEnvExt` 和 carrier TLS 绑定到
   启动流水线 S1（MVContext，见
   [11-startup-pipeline.md](11-startup-pipeline.md)）创建的 main GuestTask。
10. 创建 `ClassLinker`。
11. 执行 `InitFromBootImage` 或 `InitWithoutImage`。
12. 建立 class roots、boot DexCache、primitive/array/reflection/method-handle
    roots。
13. 创建 preallocated exceptions。
14. 执行 `JniIdManager::Init`。
15. 加载 plugins、native bridge 和 startup agents。

输出：

```text
ART Runtime INITIALIZED
main ART Thread ATTACHED_WITHOUT_PEER
main JNIEnvExt READY
ClassLinker READY
Guest Heap READY
```

### 7.2 Runtime::Start

`Runtime::Init` 完成后执行：

```text
Runtime::Start
  -> started_ = true
  -> RegisterRuntimeNativeMethods
  -> RunEarlyRootClinits
  -> InitializeIntrinsics
  -> transition main Thread to kNative
  -> InitNativeMethods
  -> InitializeCorePlatformApiPrivateFields
  -> InitThreadGroups
  -> Thread::FinishStartup
  -> CreateJit when JIT or profile saving is enabled
  -> RuntimePhase::kStart
  -> CreateSystemClassLoader
  -> InitNonZygoteOrPostFork
  -> StartDaemonThreads
  -> RuntimePhase::kInit
  -> finished_starting_ = true
```

`InitNativeMethods` 按顺序加载：

```text
libicu_jni.so
libjavacore.so
libopenjdk.so
```

debug build 使用 `libopenjdkd.so`。

这三个 core JNI library 使用 null Guest ClassLoader、`java.lang.Object` caller
和 ART APEX 的 `com_android_art` linker namespace。

`Thread::FinishStartup` 创建 main Java Thread peer，执行
`RunRootClinits`，把 main Thread 加入 ThreadGroup，并更新 main GuestTask 的
Java Thread peer/TID/ThreadLocal binding。

## 8. Guest Classpath

### 8.1 顺序

```text
ART system boot classpath
  + runtime-libs/runtime.dex
  + runtime-libs/runtime2.dex ... runtimeN.dex
  -> target APK classes.dex
  -> target APK classes2.dex ... classesN.dex
  -> target split classpath
```

### 8.2 Boot classpath 注入

`runtime-libs` 在 `Runtime::Init` 前组装为有序
`BootClassPathDexList`。

流程：

```text
Read runtime-libs manifest
  -> open DexFile with ART DexFileLoader
  -> verify with dex::Verify (DexFileVerifier)
  -> preserve order and generation
  -> pass to Runtime options
  -> ClassLinker InitFromBootImage/InitWithoutImage
```

boot image 未包含全部 runtime-libs component 时，使用
`Runtime::AddExtraBootDexFiles` 接入。

runtime-libs 各 DEX 按 manifest 顺序追加在 system boot components 之后。
构建阶段对 runtime-libs 内部 class descriptor 做重复检查，并把最终 boot
classpath 顺序写入 manifest。

### 8.3 Boot Image 集成模式

实现选择一种构建模式：

#### Mode A：匹配 Boot Image

```text
same ART commit
  -> same boot image format/version
  -> same instruction set/pointer size
  -> boot component checksum validation
  -> runtime-libs integrated into image or extra boot components
```

启动时校验：

```text
image fingerprint
ART source/build fingerprint
ABI
boot classpath component order
boot checksum
class root set
DexCache locations
```

#### Mode B：InitWithoutImage

```text
full ordered BootClassPathDexList
  -> Runtime::Init
  -> ClassLinker::InitWithoutImage
```

该模式显式包含 system boot DEX 和 runtime-libs DEX，构建时记录完整
classpath checksum。

#### Runtime Extra Boot DEX

Boot Image 不包含全部 component 时：

```text
Runtime::AddExtraBootDexFiles(
    filename,
    location,
    additional_dex_files)
```

调用前后检查：

- DexFile location 唯一。
- multidex location 顺序正确。
- BootClassLoader DexCache 已建立。
- descriptor 与已有 boot components 不冲突。
- class roots 和 descriptor lookup 可见。

参与 ClassLinker 早期 root bootstrap 的 runtime-libs 类使用 Mode B 的初始
`BootClassPathDexList`。`Runtime::AddExtraBootDexFiles` 用于 boot image
初始化后追加额外 boot components。

### 8.4 Boot classpath descriptor 规则

构建期生成：

```text
descriptor
defining boot component
component order
class status expectation
```

同名 descriptor 出现在多个 boot component 时，`FindInClassPath` 使用
classpath 顺序中的第一个定义。由于 runtime-libs 排在 ART system boot
classpath 之后（见 8.1），其 descriptor 不得与 system boot classpath 重名：
构建期 excluded-classes filter 排除重名项，manifest 记录每个 descriptor 的
最终定义方，并检查 runtime-libs 内部重复项；出现无法排除的冲突时构建失败。

### 8.5 ClassLoader

```text
BootClassLoader (system classes + runtime-libs)
  -> target APK ClassLoader
```

app ClassLoader 使用 APK `classes.dex`、`classes2.dex` 至 `classesN.dex` 和
split classpath 创建。

## 9. runtime-libs

runtime-libs 为 v1 遗产：可复用为 Guest boot classpath（vendored OpenJDK
源码、组件清单与 runtime.dex 构建流水线沿用），不作为实现约束。打包排除集
由内建类清单**自动派生**（`core/builtin_classes.def` 为唯一权威；旧
allowlist / excluded 表单已废除），内建类不入 runtime.dex（详见 14 章 §31.4）。

### 9.1 源码

```text
runtime-libs/   (仓库内相对路径)
```

输入：

```text
src/main/java
runtime-components.json        （纯元数据：组件/版本/外部依赖记录）
legal/openjdk11
Android API 适配源码
app/src/main/cpp/core/builtin_classes.def   （打包排除集来源；单一事实源）
```

### 9.2 输出

```text
build/runtime-dex/runtime.dex
build/runtime-dex/runtime2.dex ... runtimeN.dex
build/runtime-dex/manifest.json
build/runtime-packaging/report.txt
```

构建流程：

```text
compile runtime Java/Android sources
  -> filter excluded classes
  -> D8/desugar with target API
     or core-library DEX build mode for java.* / jdk.* sources
  -> dex
  -> split only when required
  -> generate deterministic manifest
  -> record source/DEX hash, API level and license metadata
  -> validate duplicate descriptors and excluded classes
```

core-library 模式允许编译 `java.*`/`jdk.*` 源；这些类只补充 ART system boot
classpath 未定义的 descriptor，不替换已有定义（见 8.4）。

### 9.3 运行

- runtime-libs 类由 ART BootClassLoader 查找。
- 高层 Java 行为由 runtime-libs DEX 实现。
- 底层线程、内存、JNI、IO 和系统原语通过 ART intrinsic 或 Guest JNI 实现。
- runtime-libs 使用 Guest Heap、ART Interpreter 和 Guest JNIEnv。

## 10. APK Session

### 10.1 输入

```text
APK fd
split fds
Manifest
resources.arsc
Guest data dir
Guest nativeLibraryDir
target API
runtime-libs generation
split name/version/signature set
```

### 10.2 DEX generation

每个 DEX 记录：

```text
file location
dex generation
checksum
signature
CodeItem hash
```

ArtMethod 绑定所属 DexFile/DexCache。加固器产生新 DEX 版本时，使用 ART
DexFileLoader + dex::Verify 重新建立 DexFile、DexCache 和 class index。

### 10.3 Session 创建

```text
CreateSession:
  open APK/splits
  validate split set and base APK consistency
  validate manifest/resource generation
  parse Manifest/resources
  open target DEX files
  create BaseDexClassLoader(parent=system/boot)
  create LoadedApk/ApplicationInfo metadata
  prepare Guest directories
  build native entry/fd/ABI table
```

## 11. ClassLinker

### 11.1 FindClass

```text
FindClass(descriptor, loader):
  primitive -> FindPrimitiveClass
  lookup ClassTable(loader, descriptor)
  existing -> EnsureResolved
  array -> CreateArrayClass
  boot loader -> FindClassInBootClassLoaderClassPath -> DefineClass
  app loader -> FindClassInBaseDexClassLoader
  result -> verify descriptor + insert with race handling
```

### 11.2 DefineClass

```text
AllocClass kNotReady
  -> ClassPreDefine callback
  -> RegisterDexFile
  -> SetupClass -> kIdx
  -> insert ClassTable
  -> LoadClass
  -> LoadSuperAndInterfaces -> kLoaded
  -> LinkClass
  -> kResolved
```

与 ART 状态机一致：super/interface 的解析在 `LoadSuperAndInterfaces` 内部
完成并直接落 `kLoaded`；`kResolving` 只是临时类克隆为正确大小新类时的瞬态
（旧类同时置 `kRetired`）。验证发生在 `InitializeClass`（见 11.4）：
`kResolved → kVerifying → kVerified`。

### 11.3 LinkClass

```text
LinkSuperClass
  -> LinkMethods
  -> LinkInstanceFields
  -> LinkStaticFields
  -> populate vtable/iftable/IMT
  -> publish kResolved
```

方法链接：

1. interface 分配 interface method index。
2. class 复制 superclass vtable。
3. override 按 name + prototype 匹配。
4. 新 virtual 方法追加 vtable slot。
5. 解析 default/conflict/miranda/copied methods。
6. 建立 iftable、IMT 和 conflict table。

字段布局：

1. instance offset 从 superclass object size 开始。
2. reference 字段优先。
3. 依次处理 long、double、int、float、char、short、boolean、byte。
4. 使用 ART 对齐和 gap-fill 规则。
5. static fields 使用 ART static storage 起点。

### 11.4 InitializeClass

```text
InitializeClass(klass):
  already initialized -> return
  check CanWeInitializeClass
  lock klass
  recheck initialized/erroneous/initializing
  verify class
  validate superclass descriptors
  set clinit owner and kInitializing
  unlock klass
  if class and has superclass:
      initialize superclass
  if class:
      initialize default-method interfaces recursively
  cache static fields and apply encoded values
  invoke <clinit>
  lock klass
  mark initialized or store failure
  unlock
  make visibly initialized
```

并发初始化使用 coroutine monitor wait/wake。同一个 Guest Thread 重入时返回
成功；其他 Guest Thread 等待 owner 完成。

## 12. Interpreter

执行流程：

```text
frame.dex_pc
  -> Instruction::At(insns + dex_pc)
  -> fetch inst_data
  -> switch opcode
  -> execute with ART accessors
  -> next_pc = dex_pc + instruction width
```

invoke 流程：

1. 通过 DexCache 解析 method。
2. 根据 invoke kind 计算 called ArtMethod。
3. 使用 `DoCallCommon` 计算 inputs 和 callee registers。
4. 参数复制到 `registers_size - inputs_size` 起始位置。
5. receiver 复制到第一个参数 slot。
6. long/double 使用两个 DEX register slot。
7. 创建 ShadowFrame 并执行。
8. return 写入 caller pending result。
9. exception 进入 ART exception delivery。

`String.<init>` 使用 ART StringFactory 重写路径，并按 ART `DoCallCommon`
更新原 `this` vreg 及其 alias 的 String 结果。

invoke 派发：

- static/direct 使用 resolved ArtMethod。
- super 使用 `FindSuperMethodToCall`。
- virtual 使用 receiver class vtable。
- interface 使用 receiver class IMT，runtime entry 时走
  `FindVirtualMethodForInterface`。
- custom/polymorphic 使用 ART method-handle/call-site 路径。

执行 entrypoint 选择：

```text
ArtMethod.entrypoint
  -> quick compiled code
  -> nterp
  -> switch interpreter
  -> generic JNI stub
```

选择由 ART `ArtMethod`、JIT/deoptimization 和 runtime entrypoint 状态决定。
JIT 编译完成后更新 ArtMethod entrypoint；deopt 时回到 interpreter/nterp。

## 13. Exception

```text
pending exception
  -> FindCatchBlock(exception.class, current_dex_pc)
  -> typed catch 顺序匹配
  -> catch-all
  -> handler code-unit PC
```

当前 frame 有 handler 时：

```text
frame.dex_pc = handler_pc
resume interpreter
```

当前 frame 无 handler 时：

```text
notify unwind
pop frame
deliver same Guest exception to caller
```

`<clinit>` 异常包装为 `ExceptionInInitializerError`，Class 记录 erroneous
状态。

跨边界异常：

```text
Guest -> JNI
  -> Thread.pending_exception
  -> JNIEnv ExceptionCheck/ExceptionOccurred

Guest Binder reply
  -> exception parcel
  -> reconstruct Guest Throwable

Host Bridge failure
  -> typed HostResult
  -> Guest framework maps to IOException/RemoteException/
     SecurityException/ServiceSpecificException
```

