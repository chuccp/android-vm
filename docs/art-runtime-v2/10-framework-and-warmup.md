# Android Framework 与安装预热

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](09-bridge.md) · [下一部分](11-startup-pipeline.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

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

> 当前实现对 19.4 的落地形态见 19.7：PresentFrame/HWUI 规格路径暂未接线，
> 首帧通过 **View 树快照 + 宿主真控件物化** 完成出画；生命周期与 traversal
> 语义不变。

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

> 当前已落地组件的实现方式见 19.7（View 物化 + 控件字段态回灌）；表中
> SurfaceControl/BufferQueue/InputChannel/VSync 行是规格目标，尚未接线。

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

### 19.7 当前实现：UI 语义物化（as-built，2026-11-20）

19.4/19.6 的 Surface/PresentFrame 是规格路径；当前实现走 **View 树快照 +
宿主真控件物化**（详见 09 章 18.8）：

```text
Activity.onCreate/setContentView
  -> guest View 树（runtime-libs lib 控件 + framework builtin 语义）
  -> performTraversals（measure/layout；允许降级）
  -> BuildSnapshotFromGuest（ViewSnapshot + DrawCommand）
  -> SerializeFull -> 宿主 UiBridgeMaterializer
  -> 真 android.view.View 树（HWUI 出画）
  -> 后续帧：DiffSnapshots -> ViewPatch 增量
```

已落地的 framework / widget 语义面：

| 组 | guest 实现 | 宿主物化 |
| --- | --- | --- |
| Activity / Window / Resources | framework builtins + runtime-libs | — |
| 基础控件 | lib/builtin TextView/Button/EditText/ImageView... | 真控件（AOSP 默认 minHeight：Button/EditText 48dp） |
| 容器 | LinearLayout/FrameLayout/RelativeLayout/ScrollView | 真容器；有 guest 几何按 bounds，无几何宿主 measure/layout |
| 列表/选择 | ListView/GridView/Spinner（item 行快照展开） | 真控件 |
| 原生容器 | lib `androidx.drawerlayout.widget.DrawerLayout` 垫片 | 真 androidx DrawerLayout（手势/动画/遮罩） |
| 应用栏 | lib `android.widget.Toolbar` + `MenuBuilder` | 真 `android.widget.Toolbar`（标题/子标题/汉堡/溢出菜单） |
| 选择器 | lib AutoCompleteTextView/DatePicker/TimePicker | 真控件；建议项/日期/时分双向回灌 |
| 弹层 | lib Dialog/AlertDialog/PopupWindow/PopupMenu | Dialog 叠加根（dim）、PopupWindow 浮层（elevation）、真 PopupMenu |
| 反馈 | lib Toast / Notification | 真 Toast / 真通知 |

交互回灌通道（`nativeDispatch*`）覆盖点击、长按、文本、SeekBar、Rating、
Drawer、Toolbar 菜单、Date/Time 字段、PopupMenu 点击与外点关闭；guest
listener 在会话线程上派发（GC 外部根持有）。

验收口径：MuMu 模拟器 `uiautomator dump` 断言真控件树 + logcat 事件；
UI 兼容考卷为 `target-app`（Gallery 画廊）。Surface/TextureView/GL、
动画资源管线、insets/无障碍/输入法仍待接线（见根 README §10）。

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

