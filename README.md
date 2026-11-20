# android-vm — ART Runtime v2 开发总结

在 Android 应用内嵌一个**全自研 DEX 运行时** `libdexvm.so`，直接加载并运行第三方加固 APK。
当前考卷：某大型三方加固 APK（arm64-v8a，18 个 dex）；UI 兼容考卷：`target-app`（`com.coke.target`）。
首期里程碑：**加固壳解密 → 动态加载 → Application/Activity 生命周期 → Activity 首帧出画**。
当前阶段：**P1 UI 物化桥 + 原生 widget 兼容**（宿主真控件出画 + 双向交互回灌）。

更新时间：2026-11-20（分支 `main`）

---

## 0. 当前进度

### 0.1 核心 VM 与加固 APK 主链（2026-11-19 起已验证）

| 阶段 | 状态 | 设备侧证据 |
| --- | --- | --- |
| 加固壳解密 / 动态加载 | ✅ 通过 | `phase activity.launch[<launcher Activity>] ok`（16–21 s） |
| Application / Provider / Activity 生命周期 | ✅ 通过 | 35 个 ContentProvider 后 `activity.onPostResume ok` |
| 首帧 measure → layout → submit → present | 🟡 链路可用（target-app 全量通过） | `phase firstFrame.present ok`、`present root=…; nodes=236`；个别组件 measure 失败时按现树降级 present |
| 宿主物化真实 View 出画 | ✅ 通过（target-app 全量验证） | 真 `android.view.View` 树 HWUI 出画；`DexVmUiBridge: full nodes=N`，设备验证截图在本地 `build/shots/`（不入库） |
| 基础框架 UI（断网也有界面） | ✅ target-app 达成 | 「DexVM Gallery」全量 widget 画廊（侧滑菜单/Toolbar/列表/选择器/弹层） |
| 会话 keep-alive / 增量 present / 交互回灌 | ✅ 闭环 | session pump + ViewPatch diff；点击/文本/长按/SeekBar/Rating/抽屉/菜单/日期时间/弹层回灌 |

### 0.2 UI 兼容批次（2026-11-20，target-app 验证）

| 批次 | 内容 | 验证 |
| --- | --- | --- |
| 基础画廊 | ListView/GridView/Spinner/Dialog/OkHttp 输出卡 | MuMu 截图 + logcat |
| 交互回灌 | EditText 输入/长按/SeekBar 拖动 → guest listener | `input afterTextChanged`、`seek-phase` 日志 |
| 原生容器 | androidx `DrawerLayout` 侧滑（手势/按钮双向同步）、第二批 widget | 开合状态 `kPropDrawerOpen` 回灌 |
| 原生控件 | 真 `Toolbar`（标题/子标题/汉堡/溢出菜单）、AutoCompleteTextView、DatePicker/TimePicker | 菜单点击 `nativeDispatchViewMenuClick`、日期时间字段回灌 |
| 窗口类弹层 | Dialog/AlertDialog（dim 叠加根）、Toast 宿主桥、PopupMenu（宿主真弹层菜单）、PopupWindow（elevation 浮层 + 外点关闭） | 点击/外点回灌 `nativeDispatchPopupEvent` |

**结论**：核心 VM 主链与 P1 UI 物化桥均已落地。宿主 `UiBridgeMaterializer` 把
guest View 快照物化为真原生控件（HWUI 出画），交互经 `nativeDispatch*` 回灌 guest
listener；加固 APK 的业务页面按同一桥轨逐屏回归。下一步是业务页面回归、宿主
insets/无障碍/输入法接线、Surface/GL 与动画资源管线（见 §10）。

---

## 1. 项目定位与开发原则

### 1.1 定位

- 运行时**全自研**：自写 DEX 解析、类链接、解释器、JNI、GC、Binder、协程调度；
  不编译 ART 源码，`reference/art/` 只作语义对照。
- 形态是 **Android app 内嵌 native 运行时**（`com.dexvm.host` + `libdexvm.so`），
  不是桌面模拟器。
- Guest 侧 Android Framework 语义由 **runtime-libs（Java boot classpath）**
  与 **VM 内建类（builtin）** 共同承接；两者边界见 §4。

### 1.2 原则

1. **AOSP 语义是唯一裁判**：任何失败先归因"我们与 Android 原生语义的偏差"，
   修正语义本身；不做加固壳/加密对抗专项，不针对某个 APK 打补丁。
2. **lib-first**：能用 runtime-libs Java 源码表达的，不放 native；
   只有 VM 必须亲自管理的（解释器、类链接、对象/堆/GC、JNI 核心、协程）
   才进 `app/src/main/cpp/core`。
3. **桥接优先于空桩**：framework 里需要真实系统能力的面（文字测量、动画资源管线、
   无障碍、系统栏、TextureView/SurfaceTexture/GL、输入法）一律桥接到宿主真实现，
   不用 null/NoOp 假实现蒙混（见 §7）。
4. **视图层桥接后，布局归宿主**：guest 只负责产生 View 树与语义；
   一旦节点在宿主物化为真 View，measure/layout 由宿主 Android 完成。
5. **单一事实源**：内建类清单 `builtin_classes.def` 同时供注册、打包排除、
   覆盖率测试消费；注册集与清单不一致直接 abort。

---

## 2. 仓库结构

| 路径 | 内容 |
| --- | --- |
| `app/` | 宿主 Android app（`com.dexvm.host`）+ 全部 C++ VM 源码 + `libdexvm.so` 构建 |
| `app/src/main/cpp/dex/` | DEX 解析、校验、leb128/mutf8、opcode 表 |
| `app/src/main/cpp/memory/` | 非移动 mark-sweep 堆、句柄表 |
| `app/src/main/cpp/core/` | ClassLinker、解释器、对象模型、异常、内建类、framework 语义面 |
| `app/src/main/cpp/coroutine/` | GuestTask / Fiber / Carrier 协作调度 |
| `app/src/main/cpp/jni/` | JNI 环境、native 方法管理、反射、`runtime_contract.cpp`（宿主契约） |
| `app/src/main/cpp/session/` | APK Session、ClassLoader / DexPathList |
| `app/src/main/cpp/binder/` | Guest Binder domain |
| `app/src/main/cpp/framework/` | 生命周期驱动、首帧渲染管线（measure/layout/draw/present） |
| `app/src/main/cpp/bridge/` | UI 桥协议（ViewSnapshot/ViewPatch + DrawOps）、UI JNI |
| `app/src/main/java/com/dexvm/host/` | 宿主入口、`UiBridgeMaterializer`、`PopupMenuBridge`/`ToastBridge`/`NotificationBridge`/`SensorBridge`/`TlsBridge`/`FontBridge` |
| `runtime-libs/` | Guest boot classpath Java 源码（1805 个文件，其中 269 个 `android.*`），编译为 `runtime.dex` |
| `target-app/` | 设备侧语义考卷 + UI 兼容考卷 app（36 个 fixture，见其 README） |
| `docs/art-runtime-v2/` | 行为规格/实施手册（权威设计文档，15 篇） |
| `tools/` | 各层测试入口脚本（host/device） |
| `tests/`、`app/src/main/cpp/tests/` | C++ 冒烟测试与 fixture 源码 |
| `reference/art/` | Android/ART 源码只读快照（本地 checkout，不入库），仅供语义对照 |

代码量：C++ 约 8.9 万行（含测试与头文件），runtime-libs Java 约 27.8 万行，
宿主 Java 约 4.7 千行，target-app 约 5.9 千行。

---

## 3. 架构总览

```
┌────────────────────────── 宿主 Android app (com.dexvm.host) ──────────────────────────┐
│  MainActivity / GridAdapter                                                           │
│    │  JNI (NativeBridge)                                                              │
│    ▼                                                                                  │
│  libdexvm.so                                                                          │
│  ┌──────────────────────────────────────────────────────────────────────────────────┐  │
│  │ runtime_contract.cpp  ── session/class_loader  ──  APK 提取与多 dex 装载          │  │
│  │        │                                                                         │  │
│  │        ├─ ClassLinker ─ Interpreter ─ Object/Heap/GC ─ InternTable               │  │
│  │        ├─ JNI env / native 库加载 / Binder domain / 协程调度                      │  │
│  │        ├─ framework builtins（View / Activity / Resources / ...，119 类）          │  │
│  │        └─ guest boot classpath：runtime.dex（runtime-libs）                        │  │
│  └──────────────────────────────────────────────────────────────────────────────────┘  │
│        │                                                                              │
│        │ ViewSnapshot / ViewPatch / DrawCommand（UI 桥协议，小端二进制）                │
│        ▼                                                                              │
│  UiBridgeMaterializer ── 真 android.view.View 树 ── HWUI 出画                          │
│        │                                                                              │
│        └─ 交互回灌：objectId/token → nativeDispatch* → guest listener                  │
└───────────────────────────────────────────────────────────────────────────────────────┘

其它宿主通道：通知（NotificationBridge）、Toast（ToastBridge）、弹层（PopupMenuBridge）、
             传感器（SensorBridge）、TLS（TlsBridge）、字体度量（FontBridge）。
网络（URLConnection/Socket）由 VM 内建语义直接走宿主内核网络栈。
```

### 3.1 分层职责

- **VM 层**：DEX/类/对象/解释执行/异常/GC/JNI/协程/Binder。Guest 线程以协程
  （GuestTask + Carrier）执行，对 guest 呈现 Thread/Looper/Handler 语义。
- **Framework 语义层**：`core/framework_*.cc` 以 AOSP 逻辑实现 Activity/Window/
  View/Resources/Inflater 等行为，补齐 runtime-libs 无法自洽的部分。
- **runtime-libs**：guest boot classpath 的 Java 公开面；凡是可以纯 Java 实现的
  Android 类都放这里，VM 只提供底层 intrinsic。
- **桥层**：宿主对象（View、通知、传感器、TLS/网络等）通过句柄/协议暴露给 guest。

---

## 4. 内建类与 runtime-libs 的协作

- `core/builtin_classes.def` 是**单一事实源**：当前 76 个 core 内建类 +
  119 个 framework 内建类。三类消费者：
  1. `builtins.cc` / `framework_builtins.cc` 的注册表；
  2. `runtime-libs` 打包排除集（同名类由 VM 独占，其 vendored 源仍参与编译，
     作 source-override 提供构造器/字段面）；
  3. `tools/runtime_coverage_tests.sh` 的一致性断言。
- `core/api_stubs.inc`（约 2600 条 `DEXVM_STUB`）只为**缺失方法**兜底：
  精确签名匹配，仅作用于 dex 类；NoOp / ReturnNull / ReturnZero / ReturnFalse
  等默认值。它保证加固壳/三方库的类初始化不因个别冷门方法中断，但**不是**真实语义。
- **runtime-libs 编译陷阱**（血泪教训）：该模块只编译**自身源码树 + 工程内
  runtimeAndroidJar**，不引用 SDK `android.jar`。lib 中新增类若引用了不在
  源码树中的类，javac 会失败，构建脚本**静默跳过**并写入
  `runtime-libs/build/runtime-packaging/uncompilable.txt`。
  新增类必须同时补齐它引用的所有类（如 `AccessibilityServiceInfo`），
  构建后检查该文件：**新增类不得出现在其中**（当前 26 个 vendored JDK
  源为已知基线例外）。

---

## 5. 启动主链与首帧管线

### 5.1 生命周期 phase（设备实测顺序）

```
frameworkState
  → application.create
  → application.attachBaseContext
  → provider[0..34].create / attachInfo / onCreate
  → application.onCreate
  → activity.create
  → activity.attach
  → activity.onCreate
  → activity.onStart
  → activity.onPostCreate
  → activity.onResume
  → activity.onPostResume
```

### 5.2 首帧 phase

```
window.attach
  → firstFrame.traversal     (performTraversals: measure → layout → draw)
  → firstFrame.submit        (View 树快照 + draw 命令流提交)
  → firstFrame.present       (宿主物化/envelope 发布)
  → firstFrame.marked        (标记 guest 首帧完成)
```

关键行为：

- `traversal` 允许**降级**：个别组件 measure 抛异常时清异常、跳过后续步骤，
  仍以当前树 present（否则整窗永远不出画）。实现见 `framework/render.cc`。
- 首帧 present 后进入 **session keep-alive**：泵 guest Looper 队列、dirty 时
  重新 traversal、diff 出 ViewPatch、交互回灌。
- 节点几何优先使用 **guest 绝对坐标**（`mLeft/mTop/mRight/mBottom`）；无有效
  几何的节点由宿主按布局语义 measure/layout（layoutKind/gravity/weight/margins
  随快照下发），不再依赖 guest 几何全量收敛。
- AOSP 默认尺寸语义在 VM measure 中补齐：Button/EditText 最小高度 48dp
  （guest density=2 → 96px），修复“按钮过扁/输入框不可点”。
- 宿主输入焦点不再被会话日志 `fullScroll(FOCUS_DOWN)` 抢走（改 `scrollTo`），
  EditText 键入与 AutoCompleteTextView 下拉保持稳定。

---

## 6. UI 桥接协议（P1）

实现文件：

- 协议定义/序列化：`app/src/main/cpp/bridge/ui_bridge_protocol.{h,cc}`
- JNI 出口：`app/src/main/cpp/bridge/ui_bridge_jni.cc`
- 宿主物化器：`app/src/main/java/com/dexvm/host/UiBridgeMaterializer.java`
- 弹层桥队列：`app/src/main/cpp/core/popup_bridge.{h,cc}` + `PopupMenuBridge.java`

线格式（小端）：

```
Envelope := magic "UIBR" | u16 version | u8 kind | u8 reserved
          | u32 revision | u32 snapshot_bytes | u32 draw_bytes
          | snapshot 段 | draw 段
kind: 1 = 全量快照（新基线），2 = 增量 patch（base_revision → revision）
```

- `ViewNode`：`HostHandle{object_id, kind}`、guest viewId、类名、父/子索引、
  `Prop` 列表、children。
- 属性（`PropKey`，1–38）：text、visibility、bounds、padding、textSize/textColor、
  backgroundColor、alpha、scroll、measured、layoutParams、hasClickListener、
  translation、gravity、imageResource/imageBytes、progress、video path/seek/playing、
  layout gravity/orientation/margins/weight/kind、windowDim、checked、
  hasLongClickListener、rating/numStars、drawerOpen、subtitle/navClick/menuItems、
  suggestions、date/time、windowPopupToken。
- `HostObjectKind` 预留：`kView`（已实现）、`kWindow`、`kContextService`、
  `kResource`——同一句柄/属性/patch 骨架可扩展非 View framework 对象。
- `DrawCommand`：save/restore、translate、matrix、clip、fillRect、drawText、
  drawBitmap、drawLine、drawCircle、frameBegin（缝 A 绘制命令流）。
- 宿主物化映射：TextView/ImageView/Button/ImageButton/EditText/AutoCompleteTextView/
  CheckBox/RadioButton/ToggleButton/Switch/CheckedTextView/SeekBar/RatingBar/
  ProgressBar/ListView/GridView/Spinner/ScrollView/LinearLayout/FrameLayout/
  RelativeLayout/Toolbar/DrawerLayout（androidx）/DatePicker/TimePicker/VideoView
  等映射为真控件；APK 内图片字节直接交宿主解码。
- 窗口类叠加（多根快照）：Dialog 内容根带 `kPropWindowDim`（宿主加 dim 遮罩）；
  PopupWindow 内容根带 `kPropWindowPopupToken`（宿主 elevation 浮层卡片 +
  透明外点关闭层）。
- 交互回灌：点击/长按/文本/SeekBar/Rating/Drawer/MenuClick/Date-Time 字段经
  `nativeDispatchView*` 按 objectId 回灌；PopupMenu 菜单项与外点关闭经
  `nativeDispatchPopupEvent(token, kind, index)` 回灌；Toast 经宿主真 Toast 呈现。

---

## 7. 桥接清单与当前缺口

### 7.1 已实现的宿主通道

- UI 树快照/增量/交互回灌（点击/长按/文本/Seek/Rating/Drawer/Menu/字段/弹层）；
- 窗口类弹层：Dialog/AlertDialog 叠加根（dim 遮罩）、PopupWindow 浮层、
  PopupMenu（宿主真菜单）；
- Toast（`ToastBridge`）、通知（`NotificationBridge`）、传感器（`SensorBridge`）；
- 文字测量/字体度量（`core/framework_font.cc` 字体工作线程 ↔ `FontBridge.java`，
  `Paint.measureText`/`FontMetrics` 走宿主真字体）；
- TLS（`TlsBridge`）；网络、文件、Binder、MediaPlayer 等 VM 内建/系统服务面。

### 7.2 已立项、待接线的桥（用户明确要求，不做空桩）

| 桥 | 目标 | 现状 |
| --- | --- | --- |
| 系统栏 | `WindowInsetsController.show/hide/Appearance` → 宿主系统栏 | `core/host_bridge.h` 的 `HostBridgeOps` 缝已定义，未接线 |
| 无障碍 | 真实 enabled / touch exploration 状态、监听器注册 | 同上 |
| 输入法 | showSoftInput/hideSoftInput、编辑事件回灌 | 骨架已定，待接线 |
| 动画资源管线 | XML 动画解析 + Choreographer 帧循环 + 属性回写 | 未实现 |
| TextureView/SurfaceTexture/Surface | 宿主对象代理、GL 帧回调、native 直通、lockCanvas | 未实现；PAG native 因此暂时被阻断 |
| 宿主布局 | 物化后的真 View 自行 measure/layout，guest 不再算几何 | **已落地**：有 guest 绝对几何按 bounds 绝对布局，无几何节点宿主 measure/layout |

> `org.libpag.*` native 当前在 `InvokeNativeTrampoline` 中主动抛
> `UnsatisfiedLinkError`（Surface/GL 桥未就绪时，native 会在垃圾 Surface
> 句柄上 SEGV）。Surface 桥落地后移除该临时阻断。

---

## 8. 构建与运行

### 8.1 构建

```bash
# 仅宿主/VM 改动
./gradlew :app:assembleDebug

# 改了 runtime-libs（guest boot classpath）时，必须先重出 runtime.dex
./gradlew :runtime-libs:dexRuntimeSdk :app:assembleDebug

# 设备语义考卷 app（可选）
./gradlew :target-app:assembleDebug
```

产物：

- `app/build/outputs/apk/debug/app-debug.apk`
- `runtime.dex` 由 `:runtime-libs:dexRuntimeSdk` 生成并复制进
  `app/build/generated/runtimeAssets`（**禁止手抄** `app/src/main/assets/runtime.dex`）
- `target-app-debug.apk` 由 app 的 `preBuild` 依赖自动生成并落 asset

### 8.2 安装与启动

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am force-stop com.dexvm.host
adb shell am start -n com.dexvm.host/.MainActivity
```

在宿主界面点选目标 APK 启动 guest 会话。Guest 数据目录：

```
/data/user/0/com.dexvm.host/files/apps/<package>
```

把 target-app 放进宿主桌面（UI 兼容考卷，需先构建 target-app）：

```bash
adb push target-app/build/outputs/apk/debug/target-app-debug.apk /data/local/tmp/target-app-debug.apk
adb shell run-as com.dexvm.host cp /data/local/tmp/target-app-debug.apk files/apps/target-app-debug.apk
# 考卷直跑线（nativeRunTargetExam）从 files/ 根读 asset 副本：
adb shell run-as com.dexvm.host cp /data/local/tmp/target-app-debug.apk files/target-app-debug.apk
```

桌面点选「Minimal Android」即启动 Gallery 画廊会话；日志断言见 §11。

### 8.3 主机侧测试

```bash
tools/host_tests.sh          # DEX/核心 VM/GC/JNI/协程/ClassLoader/APK/lifecycle/render 一条龙
tools/render_tests.sh        # 首帧/traversal 专项
tools/runtime_coverage_tests.sh
```

设备侧测试脚本在 `tools/*_tests.sh`（需要已连接设备/模拟器）。

---

## 9. 开发过程中的关键问题与修复（摘要）

| 问题 | 根因 | 修复 / 结论 |
| --- | --- | --- |
| SEGV_ACCERR 恰好落在堆 region 末尾 | `Alloc(0)` 返回 region 末指针，写对象头越界 | Alloc 强制 `AlignUp(max(size,1))`；堆分配校验 region 归属、丢弃坏 free-list 块 |
| 偶发 `AllocObject` 崩溃 / 0 尺寸类 | `DefineClassImpl` 失败时把半成品类留在 class_table_（object_size=0） | 失败即 erase（ART ERRONEOUS 语义）；LinkClass 校验 super 已链接；AllocObject 校验 status/object_size |
| `findViewById` 强转 `ZHTextView` 失败 | 未按 AOSP 语义咨询 LayoutInflater.Factory2 | inflate 每节点先走 Factory2/Factory 的 `onCreateView`，失败再回落默认类 |
| `LinearLayout$LayoutParams cannot be cast to AppBarLayout$LayoutParams` 链 | ① `inflate(res, root, false)` 丢弃根 LP；② 协变返回类型方法查找未命中；③ 泛型 LP 生成只在 builtin 返回 null | 根 LP 由 root.generateLayoutParams 生成；FindCovariantMethod 按 name+params 匹配；generateLayoutParams 走真实 intrinsic + 祖先 `$LayoutParams` 回溯 |
| ViewPager2 构造/挂载 Chain 反复失败 | `addView` 未维护 `child.mParent`；AccessibilityManager/Scroller/Interpolator 等缺类 | VGAddView/remove 维护 mParent 不变量（attach/detach 同口径）；补齐 framework/lib 类 |
| `WindowInsetsController.show` NPE | `Window` 由 `AllocObject` 分配，构造器与字段初始化不执行 | lib `Window.getInsetsController()` 惰性创建；`View.getWindowInsetsController` 提供内建实现 |
| lib 新增类莫名"NoClassDefFound" | runtime-libs 只编译自身源码树，引用缺失类时 javac 失败且**静默跳过** | 构建后检查 `uncompilable.txt`；缺类必须补进源码树（或用 api_stub） |
| PAG native SEGV（mutex 锁垃圾句柄） | `Surface` 是空壳，native 收到无效 handle | 暂时对 `org.libpag.*` 抛 `UnsatisfiedLinkError`，等 Surface/GL 桥 |
| measure 失败导致整窗不出画 | traversal 一失败就终止，首帧永远 present 不了 | traversal 可降级：清异常、跳过后续步骤、按现树 present；无几何节点由宿主 measure/layout |
| `CaptureThrowBacktrace` 在宿主泵上下文 SEGV | 跨任务残帧无 owner，读 `m->declaring` 崩溃 | owner 为空直接返回 |
| 启动耗时被放大 | Debug 默认 -O0，switch 解释器启动链被放大数倍 | Debug 也强制 `-O2`；诊断日志限流（JniStr ≤300、FindMiss 每描述符一次、LP/F2 只记失败） |
| 按钮过扁、EditText 不可点 | VM `TextView.onMeasure` 缺 AOSP 默认 minHeight | Button/EditText 族补 48dp 下限（guest density=2 → 96px） |
| ACTV 下拉 13–535ms 内自动关闭 | 宿主日志 `fullScroll(FOCUS_DOWN)` 触发跨窗口 `requestFocus`，抢走 guest 焦点 | 宿主日志改为 `scrollTo`；`applyProps` 等值跳过 + `pendingHostEdits` 抑制旧快照覆盖键入 |
| Drawer 宿主态不回灌 | 抽屉子 View gravity 与 `Gravity.LEFT` 掩码比较错误 | `(gravity & HORIZONTAL_GRAVITY_MASK) == Gravity.LEFT` |
| Toolbar 导航点击 NoSuchMethodError | guest `performClick` 覆写经 invoke-interface 调 `OnClickListener.onClick`，内建接口无方法骨架 | builtin `View$OnClickListener` 补 `onClick(Landroid/view/View;)V` 骨架 |
| PopupMenu 点击事件丢失 | 宿主菜单点击后紧随的 dismiss 覆盖 session 单槽中的点击事件 | 点击路径不再回送 dismiss（guest lib 在 `onHostMenuItemClick` 内自行关闭） |

---

## 10. 已知缺口与下一步

按优先级：

1. **加固 APK 业务页面逐屏回归（P0）**
   - UI 物化桥与 widget 画廊已在 target-app 全量验证；下一步用同一桥轨回归加固
     APK 的真实业务页面（列表/图片/复杂布局），按 AOSP 语义修偏差。
   - 验收：断网冷启动后业务首页出现非空界面，`DexVmUiBridge: ... full nodes=N`
     且关键节点尺寸非 0。
2. **HostBridgeOps 剩余面（P0）**
   - 文字测量/字体度量已接线（`framework_font.cc` ↔ `FontBridge.java`）；
     待接线 insets / accessibility / IME（`core/host_bridge.h` 缝）。
3. **Surface/TextureView/GL 桥（P1）**
   - 宿主 Surface/SurfaceTexture 代理 + 帧回调 + native 直通；完成后移除
     `org.libpag.*` 临时阻断。
4. **动画资源管线（P1）**
   - AnimationUtils/XML 解析 → Choreographer 帧循环 → View 属性回写 → diff present。
5. **启动速度（P1）**
   - launch 期间派发 IdleHandler，消除 APM `LaunchIdleDispatcher` 的 6×15 s 等待。
6. **清理与回归（P2）**
   - 移除 TEMP DIAG；更新 `render_smoke` 对"降级 traversal"的旧断言；
   - runtime-libs `uncompilable.txt` 不新增条目（26 个 vendored JDK 基线例外保持稳定）；
   - 设备侧连续冷启动回归。

---

## 11. 日志与排障

关键 logcat tag：

| Tag | 内容 |
| --- | --- |
| `DexVmProgress` | `phase <name> ok/FAIL (<ms>)`，启动/首帧阶段进度 |
| `DexVmRender` | `traversal degraded`、measure/layout 失败原因 |
| `DexVmViewTree` | `present root=<descriptor> nodes=<N>` |
| `DexVmUiBridge` | 宿主物化：revision / full vs patch / 节点数 / 窗口叠加根 |
| `DexVmDialog` | Dialog / PopupWindow 窗口登记与叠加根布局 |
| `DexVmPopup` | PopupMenu 弹层桥（show/click/dismiss 回灌） |
| `DexVmToast` | 宿主 Toast 桥 |
| `DexVmNotify` / `DexVmSensors` | 通知 / 传感器宿主桥 |
| `DexVmTlsBridge` | guest TLS ↔ 宿主 TLS 栈 |
| `DexVmHeap` | 堆越界 / BAD-FREE-BLOCK / BAD-ALLOC |
| `DexVmFindMiss` / `DexVmDefineFail` | 类查找/定义失败（已限流） |
| `DexVmJniStr` | JNI 字符串桥诊断（封顶 300 条） |

常用命令：

```bash
adb logcat -s DexVmProgress DexVmRender DexVmViewTree DexVmUiBridge DexVmDialog DexVmPopup DexVmHeap
adb shell uiautomator dump /sdcard/ui.xml && adb pull /sdcard/ui.xml
```

宿主内也可通过 `NativeBridge.nativeGetRunProgress()` 取阶段进度、
`nativeGetViewSnapshot()` 取最近一次 present 的树快照。

排障顺序建议：`phase 失败点` → `DexVmFindMiss/DefineFail`（缺类）→
`DexVmThrow`（guest 异常）→ `DexVmRender`（traversal）→ `DexVmUiBridge`（物化）
→ `DexVmDialog/DexVmPopup`（窗口叠加与弹层回灌）。

---

## 12. 参考文档

- 权威行为规格：[docs/art-runtime-v2-implementation.md](docs/art-runtime-v2-implementation.md)
- 基础架构：[01-foundation.md](docs/art-runtime-v2/01-foundation.md)
- 类加载/解释器：[03-bootstrap-and-class-loading.md](docs/art-runtime-v2/03-bootstrap-and-class-loading.md)
- 协程运行时：[04-coroutine-runtime.md](docs/art-runtime-v2/04-coroutine-runtime.md)
- JNI：[05-jni-core.md](docs/art-runtime-v2/05-jni-core.md)、[06-jni-callbacks.md](docs/art-runtime-v2/06-jni-callbacks.md)
- Native 库：[07-native-libraries.md](docs/art-runtime-v2/07-native-libraries.md)
- Binder：[08-binder.md](docs/art-runtime-v2/08-binder.md)
- Bridge：[09-bridge.md](docs/art-runtime-v2/09-bridge.md)
- Framework 与首帧：[10-framework-and-warmup.md](docs/art-runtime-v2/10-framework-and-warmup.md)
- 启动主线：[11-startup-pipeline.md](docs/art-runtime-v2/11-startup-pipeline.md)
- 关键算法：[12-key-algorithms.md](docs/art-runtime-v2/12-key-algorithms.md)
- 测试与验收：[13-diagnostics-tests-and-delivery.md](docs/art-runtime-v2/13-diagnostics-tests-and-delivery.md)
- API 考纲：[14-implementation-api-checklist.md](docs/art-runtime-v2/14-implementation-api-checklist.md)
- 设备考卷 app：[target-app/README.md](target-app/README.md)
