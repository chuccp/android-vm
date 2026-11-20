# 基础架构与交付边界

[返回实施手册索引](../art-runtime-v2-implementation.md)

[下一部分](02-memory-gc.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 1. 交付目标

实现一个只输出 `libdexvm.so` 的全自研 Guest Android Runtime：自写 DEX
解析、类链接、解释器、JNI 和 GC，执行语义严格对齐 Android/ART 原生行为，
调度使用原生协程（GuestTask/Carrier）替代线程。

交付物：

```text
DEX VM Core（对象模型/ClassLinker/Interpreter/GC）
Guest Memory Manager
Native Coroutine Runtime
JNI 与 Guest Native Manager
Guest runtime-libs classpath
APK Session
Guest Android Framework Runtime
Guest Binder Runtime
Host/UI/Device Bridge
```

运行路径：

```text
Guest DEX
  -> DEX VM Core
  -> Runtime Services
  -> Host/UI/Device Bridge
  -> Host Android
```

运行时通过 `MVContext` 管理一个 Guest 应用实例、一个 DEX VM Runtime、一个
Guest Heap、一个 Coroutine Scheduler 和一组 Bridge capability。

## 2. ART 行为对照基线

实现**不编译 ART 源码**。ART 源码快照仅作**语义对照**：行为分歧时以
Android/ART 原生行为为准，对照快照中的实现裁定。

```text
<ART_SRC>  (本地 checkout 路径因环境而异)
commit b753cf97923c3695338d21466fa14c57b480a59a
branch 26Q2-release
```

快照位置（只读，不参与构建；本地 checkout，不入库）：

```text
reference/art/      （路径因环境而异；仓库内不含快照）
```

对照方式：实现某行为前查快照对应实现（类名、状态机、调用顺序、边界语义），
实现后以对照语义写等价测试。快照不复制源码进实现。

### 2.1 语义对照源码入口

| 行为 | 对照源码 |
| --- | --- |
| Object/Class/Array/String | `runtime/mirror/object.h`、`class.h`、`array.h`、`string.h` |
| Class loading/linking/init | `runtime/class_linker.cc` |
| Method/Field | `runtime/art_method.*`、`runtime/art_field.*` |
| Interpreter/DoCall | `runtime/interpreter/*` |
| Thread/Monitor | `runtime/thread*`、`runtime/monitor*`、`runtime/lock_word*` |
| GC/roots/barriers | `runtime/gc/*`、`runtime/handle*`、`runtime/gc_root*` |
| JNI | `runtime/jni/*` |
| Native library | `runtime/jni/java_vm_ext.cc`、`libnativeloader/*` |
| DEX/instructions | `libdexfile/dex/*` |
| OAT/image/JIT（远期） | `runtime/oat/*`、`runtime/runtime_image*`、`runtime/jit/*` |

## 3. 目录结构

```text
app/
  src/main/cpp/               C++ 自研运行时（cpp 即 runtime）
    CMakeLists.txt
    dex/                      DEX 解析
    core/                     对象模型、ClassLinker、Interpreter、异常、framework 语义面
    memory/                   Guest Heap、GC、handles
    coroutine/                GuestTask/Carrier/调度器
    jni/                      JNI、Native Manager、SharedLibrary、contract
    session/                  APK Session、ClassLoader、resources
    binder/                   Guest Binder
    framework/                ActivityThread/Application/Activity/首帧管线
    bridge/                   Host/UI Bridge（ViewSnapshot/ViewPatch + UI JNI）
    tests/                    host/device 测试与 fixture
  src/main/java/              Android 宿主壳（MainActivity、NativeBridge、
                              UiBridgeMaterializer、Toast/通知/弹层/传感器/TLS/字体桥）

runtime-libs/                 guest boot classpath（1805 个 Java 源文件 -> runtime.dex）
target-app/                   设备侧语义考卷 + UI 兼容考卷 app（com.coke.target）
docs/                         本手册
tools/                        fixture/测试入口脚本
reference/art/                ART 行为对照快照（本地 checkout，只读，不入库）
```

include 风格：以 `app/src/main/cpp/` 为根（如
`#include "core/class_linker.h"`）。

构建目标：

```text
dexvm_dex           DEX 解析与校验
dexvm_runtime       对象模型、ClassLinker、Interpreter、异常
dexvm_memory        Guest Heap、GC、handles
dexvm_coroutine     GuestTask/Carrier/调度器
dexvm_jni           JNI、Native Manager、SharedLibrary
dexvm_session       APK Session、ClassLoader、resources
dexvm_binder        Guest Binder
dexvm_framework     ActivityThread/Application/Activity
dexvm_bridge        Host/UI/Device Bridge
dexvm               合成输出 libdexvm.so
```

最终 `dexvm` 静态组合以上模块并输出 `libdexvm.so`。

构建依赖：

```text
dexvm_dex/runtime/memory/coroutine/jni/session/binder/framework/bridge
  (自研静态库，无 ART/AOSP 编译依赖)
  -> libdexvm.so
```

`libdexvm.so` 对 App 只导出 JNI contract symbol；内部 C++ symbol 使用 hidden
visibility。构建产物覆盖 `arm64-v8a` 和 `x86_64`。

## 4. 分层职责

| 层 | 实现内容 |
| --- | --- |
| DEX VM Core | DexFile、ClassLinker、Method、Field、Interpreter、GC、JNI、Verifier、Monitor、Reflection（语义对齐 ART 原生） |
| Memory Runtime | Guest Heap backing、DEX mmap、coroutine stack、native mapping |
| Coroutine Runtime | GuestTask、Carrier、wait queue、deadline、IO reactor、native pin |
| Session | APK、split、runtime-libs、resources、Guest 文件目录、generation |
| Native Manager | dlopen、namespace、SharedLibrary cache、JNI_OnLoad、RegisterNatives、ABI invoker |
| Binder Runtime | Guest Binder domain、Parcel、ServiceManager、AIDL、transaction、death、identity |
| Framework Runtime | ActivityThread、Looper、LoadedApk、ContextImpl、Application、Provider、Activity、Window |
| Bridge | MV 与宿主交互，以及 MV 的 UI、音频、视频、传感器调用 |

## 5. MVContext

### 5.1 结构

```text
MVContext {
  session_id
  generation
  state

  DEX VM Runtime*
  Guest Heap*
  Coroutine Scheduler*
  Native Manager*
  Binder Domain*

  RuntimeSession
  Framework Runtime
  Bridge Capability Table
}
```

`MVContext` 由 `libdexvm.so` 创建和销毁。

一个 isolated worker 进程绑定一个活动 MVContext 和一个 DEX VM Runtime。
不同 Guest 应用实例使用不同 worker 进程。

### 5.2 初始化

```text
CreateMVContext(bootstrap):
  validate bootstrap schema
  allocate MVContext
  generate session_id/generation
  duplicate or retain input fds
  create Guest Memory Manager
  create Coroutine Runtime
  create main GuestTask
  store Host/UI/Device capabilities
  initialize diagnostics
```

### 5.3 销毁

```text
DestroyMVContext:
  stop Guest Activity and Looper
  release media/audio/video/sensor sessions
  invalidate Binder handles and JNI references
  close Guest roots and native libraries
  close Session/ClassLoader/DEX mappings
  stop coroutine Carriers
  shutdown DEX VM Runtime
  invalidate MVContext generation
  release owned mappings
```
