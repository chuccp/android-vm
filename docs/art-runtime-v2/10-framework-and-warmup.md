# Android Framework 与安装预热

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](09-bridge.md) · [下一部分](11-startup-pipeline.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，third_party/art 源码快照仅作对照。

## 19. Android Framework 启动

### 19.1 Guest Framework Process State

创建：

```text
ActivityThread
main Looper
ActivityThread Handler
Instrumentation
LoadedApk
ContextImpl
Resources
```

把这些 Guest 对象绑定到 main GuestTask、Session ClassLoader、ApplicationInfo、
Resources、dataDir、cacheDir 和 nativeLibraryDir。

### 19.2 Application

```text
create LoadedApk
  -> Instrumentation.newApplication
  -> Application.attachBaseContext
  -> install Providers by descending initOrder
  -> Provider.attachInfo
  -> Provider.onCreate
  -> Application.onCreate
```

### 19.3 Activity

```text
resolve launcher Activity
  -> load class
  -> create instance
  -> Activity.attach
  -> onCreate
  -> onStart
  -> restore state / onPostCreate
  -> onResume
  -> onPostResume
  -> add Window
  -> ViewRootImpl traversal
```

`ActivityLifecycleCallbacks` 的 pre/post 方法按 AOSP 调用点穿插在对应
lifecycle 方法前后执行。

### 19.4 首帧

```text
performTraversals
  -> measure
  -> layout
  -> draw
  -> render node/display list
  -> UI Bridge PresentFrame
  -> Host Surface frame submitted
```

首帧完成后进入 Interactive：移除启动遮罩、开始转发 input、运行 Guest
Looper 和后台 task。

### 19.5 Framework Compatibility Matrix

| 组件 | Guest Runtime 实现 | Host Bridge 实现 |
| --- | --- | --- |
| ActivityThread | Guest Java object、main Looper、lifecycle 状态 | 无 |
| LoadedApk | Guest ClassLoader、ApplicationInfo、Resources、dataDir | 返回 opaque host metadata |
| ContextImpl / Resources | Guest Java state | resource fd、asset fd、configuration |
| ResourcesManager / AssetManager | Guest resource table、配置状态 | Guest resource archive fd |
| PackageManager | Guest package/component facade | Host PackageManager capability |
| WindowManager | Guest WindowManagerGlobal、ViewRootImpl | SurfaceControl、window token、focus |
| InputManager | Guest input queue/event dispatch | InputChannel、hardware event source |
| DisplayManager | Guest display configuration | Host display metadata/vsync |
| Choreographer | Guest frame callbacks/deadlines | Host VSync signal |
| SurfaceControl | Guest wrapper + opaque handle | Host SurfaceControl/BufferQueue |
| HWUI / RenderThread | Guest display list、render state | Host GPU/graphics surface backend |
| BufferQueue | Guest-visible buffer metadata | Host buffer allocation/queue/dequeue |
| InputChannel | Guest event queue + dispatch | Host InputChannel fd |
| InsetsController | Guest inset state | Host inset/resize events |

每个组件记录当前实现方式：

```text
guest-native
host-bridge
guest/host hybrid
```

### 19.6 First-frame pipeline

```text
Activity.addView
  -> WindowManagerGlobal addView
  -> UI Bridge create SurfaceControl/BufferQueue
  -> Guest Surface handle
  -> ViewRootImpl.setView
  -> Choreographer requestFrame
  -> Host VSync
  -> performTraversals
       measure
       layout
       draw
       display list/render node
  -> HWUI/RenderThread submit
  -> BufferQueue queue/dequeue
  -> Surface transaction apply
  -> SurfaceFlinger present
  -> UI Bridge frame-present callback
  -> mark first guest frame
```

其中 SurfaceFlinger present 指 host 合成器（或虚拟显示提供者）把 surface
提交到显示设备，不表示 Guest 侧存在 SurfaceFlinger 进程。

窗口状态：

```text
create -> surface-created -> visible -> focused
  -> resize/config-change -> redraw
  -> hidden -> destroyed
```

首帧完成条件：

```text
launcher Activity RESUMED
Guest Window attached
traversal complete
render submitted
Host surface frame presented
```

首帧完成后：

```text
remove startup overlay
activate InputChannel forwarding
start normal Looper/input dispatch
continue background work
```

## 20. 安装预热

### 20.1 安装输入

安装阶段读取：

```text
ZIP/APK
Manifest
resources.arsc
DEX metadata
ELF dynsym/DT_NEEDED
```

### 20.2 生成产物

生成：

```text
DEX class/method/CodeItem index
native declaration index
ELF symbol index
resource archive
APK/DEX/ELF hash
ABI/API/generation
```

### 20.3 缓存写入与发布

缓存写入流程：

```text
acquire cache lock
create temp directory
  -> write artifacts
  -> validate all hashes/sizes
  -> write manifest
  -> fsync files/manifest
  -> rename publish directory
  -> atomically publish .complete
  -> release cache lock
```

并发安装使用 cache lock，只允许一个 writer 发布同一 cache key。

### 20.4 启动恢复

启动恢复：

```text
remove stale temp directories
validate .complete + manifest hashes
missing/invalid artifact -> rebuild complete generation
valid generation -> reuse
```

### 20.5 缓存 key 与运行时使用

缓存 key：

```text
APK SHA-256
split SHA-256
runtime-libs manifest SHA-256
runtime.dex SHA-256
VM build fingerprint
ABI
resource/native schema
```

运行时使用预热索引作为 lookup hint，正式执行使用 ART DexFile/ClassLinker/
Native Manager 建立的对象。

