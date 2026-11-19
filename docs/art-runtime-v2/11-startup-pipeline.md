# 标准启动主线

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](10-framework-and-warmup.md) · [下一部分](12-key-algorithms.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，third_party/art 源码快照仅作对照。

## 21. 启动主线

### 21.1 S0 Host Preflight

```text
create Host window/loading overlay
validate APK/split/runtime-libs/ABI
retain fds
read minimal Manifest
create BootTraceId
report "准备运行环境"
```

失败处置：

```text
失败判定：APK/split 不可读或 hash 不符、runtime-libs/ABI 与宿主不匹配、
Manifest 解析失败、fd 保留失败、预热缓存 manifest 校验失败
回滚动作：关闭已保留 fd、丢弃预热 temp、移除 loading overlay 并展示错误；
本阶段不创建 MVContext，无 APK session
可重试：是。修正 APK/ABI/预热缓存后可重新发起启动，次数不限
```

### 21.2 S1 MVContext

```text
create MVContext/generation
create Memory Manager
create Coroutine Runtime/main GuestTask
build ordered runtime-libs BootClassPathDexList
bind Bridge capabilities
initialize diagnostics
```

失败处置：

```text
失败判定：CreateMVContext 失败、Memory Manager/Coroutine Runtime/main
GuestTask 创建失败、runtime-libs BootClassPathDexList 构建失败
回滚动作：DestroyMVContext（停止 Carrier、释放映射、generation 失效）；
APK session 尚未建立，无需 session 清理
可重试：是。整体重建 MVContext，最多重试 2 次，不做部分复用
```

### 21.3 S2 ART Runtime

```text
Runtime::Create
Runtime::Init
Runtime::Start
verify runtime-libs boot classpath
verify JNI/main Thread peer/system classloader
```

失败处置：

```text
失败判定：Runtime::Create/Init/Start 抛错或超时、runtime-libs boot
classpath 校验失败、JNI main Thread peer/system classloader 缺失
回滚动作：销毁 MVContext（含 ART Runtime shutdown），丢弃本 session
全部 ART 状态，释放预热缓存引用
可重试：Create/Init/Start 的资源型失败可整体重建一次；boot classpath
校验失败为 runtime-libs 构建错误，不可重试
```

### 21.4 S3 APK Session

```text
open target DEX
verify DEX generation
create application BaseDexClassLoader
create LoadedApk/ApplicationInfo/Resources
prepare native entries/FD/ABI
```

失败处置：

```text
失败判定：target DEX 打开或 verifier 失败、DEX generation/hash 与预热
索引不一致、BaseDexClassLoader 创建失败、native entry/FD/ABI 准备失败
回滚动作：APK session 清理（关闭 DEX/split 映射、回收 ClassLoader、
释放 native entries/fd），MVContext 可保留或随后销毁
可重试：generation/hash 不一致时丢弃预热缓存并重建后重试一次；
verifier 失败为 APK 内容错误，不可重试
```

### 21.5 S4 Framework State

```text
create Guest ActivityThread/main Looper/Handler
create Instrumentation/LoadedApk/ContextImpl/Resources
bind Bridge capabilities
```

失败处置：

```text
失败判定：ActivityThread/main Looper 创建失败、Instrumentation/LoadedApk/
ContextImpl/Resources 构建失败、Bridge capability 绑定失败
回滚动作：销毁 Framework state 与 APK session，销毁 MVContext
可重试：是。整体重启一次；重复失败直接判定启动失败
```

### 21.6 S5 Application/Provider

```text
create Application
attachBaseContext
install Providers
Provider.onCreate
Application.onCreate
```

失败处置：

```text
失败判定：Application 创建/attachBaseContext/Provider.onCreate/
Application.onCreate 抛 Guest 异常或超过阶段超时预算
回滚动作：停止 Provider、销毁 Application/LoadedApk、清理 APK session、
销毁 MVContext
可重试：Guest 代码异常不可重试（记录异常后终止启动）；纯资源型超时
可整体重启一次
```

### 21.7 S6 Activity

```text
create launcher Activity
attach
onCreate
onStart
onResume
onPostResume
add Window
```

失败处置：

```text
失败判定：launcher Activity 创建/attach/onCreate/onStart/onResume 抛异常、
Window 添加失败
回滚动作：移除 Window 并 finish Activity、销毁 Framework state、清理
APK session、销毁 MVContext
可重试：Guest 代码异常不可重试（记录异常后终止启动）；资源型失败可
整体重启一次
```

### 21.8 S7 First Frame

```text
ViewRootImpl traversal
UI Bridge frame submission
Host Surface present confirmation
remove loading overlay
enable input
```

失败处置：

```text
失败判定：ViewRootImpl traversal 抛异常、UI Bridge frame 提交失败、
Host Surface present 确认超过阶段超时预算
回滚动作：移除 Window/Activity、清理 APK session、销毁 MVContext、
丢弃本 session 生成但未发布的预热 temp
可重试：Surface/合成超时可整体重启一次；Guest 绘制异常不可重试
```

### 21.9 阶段超时预算

下表为 S0–S7 的建议超时预算，取值为可校准基线。超时判定使用 §23 启动
事件的 `elapsed_ms`；任一阶段超时按该阶段失败处置执行。目标应用画像或
宿主性能变化时在联调阶段校准。

| 阶段 | 建议超时预算（可校准基线） | 主要开销 |
| --- | --- | --- |
| S0 Host Preflight | 2,000 ms | APK/split/runtime-libs/ABI 校验、Manifest/预热缓存校验 |
| S1 MVContext | 1,000 ms | MVContext、Memory/Coroutine 组件创建 |
| S2 ART Runtime | 4,000 ms | Runtime::Create/Init/Start、boot classpath 校验 |
| S3 APK Session | 2,500 ms | DEX 打开/verifier、ClassLoader、native entries/FD/ABI |
| S4 Framework State | 1,500 ms | ActivityThread/Looper/Instrumentation/Resources 构建 |
| S5 Application/Provider | 3,000 ms | Application/Provider onCreate |
| S6 Activity | 2,000 ms | launcher Activity lifecycle 到 Window attach |
| S7 First Frame | 1,500 ms | ViewRootImpl traversal 到 Surface present |
| S0–S7 合计 | 17,500 ms | 端到端冷启动上限 |

### 21.10 “启动应用”的检查范围

本文中的“启动应用”指完整通用启动链，而不是某一个包名的专用启动流程。
目标 APK 用来覆盖以下关键动作：

```text
APK / split install snapshot
Runtime::Create / Init / Start
runtime-libs boot classpath
DEX / DexCache / ClassLoader
ClassLinker / Interpreter
native library / JNI_OnLoad / RegisterNatives
Guest Heap / GC / coroutine Carrier
Application / Provider / Application.onCreate
Binder / PackageManager / system service
Activity / Resources / Window
Input / VSync / HWUI / RenderThread
SurfaceControl / BufferQueue / first frame
Guest main Looper / post-first-frame callbacks
```

启动检查输出：

```text
BootTraceId
session_generation
DEX generation/hash
runtime-libs generation
native library path/ABI/load count
ClassLinker load/link/init count
JNI_OnLoad / RegisterNatives count
GuestTask / Carrier / native pin count
Provider/lifecycle sequence
Surface/Window/Input state
first-frame timestamp
post-first-frame Looper state
```

通用验收条件：

```text
Runtime Init/Start 完成
全部 Guest DEX 由 ART DexFileLoader + dex::Verify 建立
Application/Provider/Activity lifecycle 按 AOSP 顺序执行
launcher Activity Window attached
Host Surface 接收真实 Guest frame
InputChannel 可用
main Looper 在首帧后继续处理消息
后台 network/IO/media/sensor task 使用 coroutine wait
```

目标应用的具体类名、加固壳和 native library 只进入启动观察记录，不进入
Runtime 分支规则。所有 App 使用同一套启动链。

### 21.11 验收度量口径

通用验收条件中的以下两项使用可观测的诊断事件/计数器判定（事件字段沿用
§23 启动事件）：

| 验收项 | 诊断事件/计数器 | 判定口径 |
| --- | --- | --- |
| 全部 Guest DEX 由 ART DexFileLoader + dex::Verify 建立 | `dex_file_load` 事件（phase、artifact_generation、CodeItem offset/hash）+ 计数器 `dex_load_ok_count` / `dex_verify_fail_count` | session_generation 内 `dex_load_ok_count` 等于预热索引中 Guest DEX 总数，且每条 `dex_file_load` 事件带 `loader=ART::DexFileLoader`、`verify=ok`；`dex_verify_fail_count > 0` 即判定不满足 |
| 后台 network/IO/media/sensor task 使用 coroutine wait | `coroutine_wait` 事件（coroutine wait reason）+ 计数器 `coroutine_wait_count{reason=io/timer/binder}` 与 `native_block_count` | 采样窗口内后台 GuestTask 的阻塞全部计入 `coroutine_wait_count`（reason 为 io/timer/binder 之一），`native_block_count == 0`（不允许直接 OS 阻塞） |

