# 诊断、测试与实施验收

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](12-key-algorithms.md) · [下一部分](14-implementation-api-checklist.md)

## 23. 诊断

启动事件：

```text
phase
state(start|ok|fail)
elapsed_ms
session_generation
artifact_generation
Guest main task
Carrier
Guest Thread ID
GC iteration
native library/path
Binder transaction
Surface/Window ID
ART ClassStatus/ThreadState
coroutine wait reason
Bridge call/result
JNI_OnLoad/RegisterNatives count
Window/Surface/frame state
```

异常记录：

```text
Guest descriptor
method/signature
dex_pc/opcode
DexFile generation
CodeItem offset/hash
ART state
Bridge call
Host result
```

启动界面展示当前 phase、已耗时和异常上下文。

## 24. 测试

### 24.1 语义对照测试（ART oracle）

以 ART 官方测试的用例期望为 oracle，自研实现跑等价用例：

```text
dex_instruction_test
code_item_accessors_test
dex_file_verifier_test
class_linker_test
monitor_test
thread_test
runtime_test
intern_table_test
handle_scope_test
method_handles_test
subtype_check_test
reflection_test
JNI tests
```

通过基线：

```text
runner：自研 gtest（NDK on-device / host）
oracle：本地 ART 快照中对应测试的行为期望（只读对照，不编译）
通过率：100%
允许失败：仅限显式登记的 known-failure 清单（用例名、原因、issue），
进入 M5 前清零
输出：JUnit/XML 结果与每用例 stdout，归档到 25.5 日志目录
```

### 24.2 Runtime

- runtime-libs + target APK classpath。
- FindClass/DefineClass/LinkClass/InitializeClass。
- DoCall、exception、monitor、GC、safepoint。
- TLAB、RegionTLAB、RosAlloc、LOS、non-moving allocation。
- MarkSweep/CMS/CMC/SS/CC 的官方 collector 测试。
- GC roots：Thread、ShadowFrame、HandleScope、JNI、DexCache、Class、JIT。
- JIT 编译产物：热点 fixture 方法编译后 ArtMethod entrypoint 切换到 quick
  compiled code，执行结果与 interpreter 一致。
- JIT roots：JIT code cache/data root 参与 GC root 扫描，GC 后 JIT 引用可解析。
- deopt：JIT 方法触发 deopt 后回到 interpreter/nterp 恢复执行，语义一致。
- moving GC 后 frame/root/field/reference 更新。
- weak/soft/phantom/finalizer reference processing。
- weak ClassLoader 和 collector 支持的 class unloading。
- native allocation watermark 和 GC critical section。
- JNI local/global/weak reference。
- JNI NewObject/AllocObject/GetObjectClass/IsSameObject。
- JNI GetFieldID/GetMethodID 的 EnsureInitialized 和 hidden API 检查。
- JNI GetSuperclass(interface)=null、IsInstanceOf(null,class)=JNI_TRUE。
- JNI instance/static object field 和 primitive field。
- JNI object/primitive method call 与 A/V varargs。
- JNI object/primitive array、String/MUTF-8、DirectByteBuffer。
- JNI local frame、monitor、exception 和 critical array。
- JNI Java -> native -> Java synchronous callback。
- JNI native thread GetEnv/AttachCurrentThread/DetachCurrentThread。
- JNI callback class GlobalRef、jmethodID cache 和 weak target promotion。
- JNI async callback 投递 Guest main Handler 和 callback exception。
- JNI_OnLoad/RegisterNatives。

### 24.3 Framework

- Application/Provider/Activity lifecycle。
- LoadedApk/ContextImpl/Resources。
- Window/Input/frame。
- first guest frame。

### 24.4 Bridge

- Host ABI、Binder、PackageManager、fd。
- Surface/Window/Input。
- AudioRecord/AudioTrack/PCM。
- Camera2/MediaCodec/MediaProjection。
- SensorManager/SensorEvent。
- SpeechRecognizer/TextToSpeech。

### 24.5 Binder

- local/AIDL/remote transaction。
- same-domain local direct call 和 original FLAG_ONEWAY propagation。
- host synchronous/oneway。
- calling identity。
- strong/weak reference。
- DeathRecipient。
- FD transfer。
- session generation 失效。

## 25. 实施顺序

### 25.1 里程碑依赖

```text
M1 核心 VM（DEX/ClassLinker/Interpreter/对象模型/GC/协程最小/JNI 最小）
  -> M2 动态加载 API 面（ClassLoader 全家/DexFile/JNI 全面/引用/反射）
  -> M3 语义保真验证（加固 APK 全程）
  -> M4 Framework 深度（生命周期/首帧链路）
  -> M5 稳定化/回归
```

M1 完成对象模型、解释器和协程最小面后，M2 才能推进。M3 是语义保真度
验证关口：加固壳的解密与动态加载是普通 Android 代码，失败一律归因语义
偏差并修正语义，不做加密/对抗专项。M4 在 M3 通过后接入 Framework 与
首帧链路；Binder（第 17 章）可在 M2 后与 M4 并行。

### 25.2 可量化验收

Runtime：

```text
100 个独立 isolated worker 依次执行 Runtime::Create/Init/Start
每轮均生成 main Thread peer、system classloader 和完整 root set
```

Thread/Fiber：

```text
4096 GuestTask / 1..16 Carrier
无 deadlock、lost wakeup、Carrier leak
Native frame pin/unpin 计数平衡
mutex/monitor/condition wait-wake 各 100,000 次
deadline/IO reactor wake 各 10,000 次
priority aging 下 P3 task 持续前进：P0/P1 满载时每个 P3 task 的两次完成
间隔 P99 ≤ 5,000 ms
Guest main Looper 在高负载下保持 P0/P1 响应：消息入队到调度延迟
P99 ≤ 50 ms，单条消息处理超过 1,000 ms 的计数为 0
native pthread attach/detach 计数平衡
每个 GuestTask 最多一个 active Carrier
RUNNING / PINNED_NATIVE / SAFEPOINT_REQUESTED 正交状态组合覆盖
```

GC：

```text
至少 10,000 轮年轻/完整 GC 压测
mark-sweep、moving collector、weak reference、finalizer 路径各覆盖
GC 后所有 frame/JNI/Handle/DexCache 引用可解析：每轮 GC 后执行全量 root
校验扫描（逐项 Resolve），gc_root_verify_fail_count == 0
30 分钟 idle 运行 RSS/PSS 无持续单调增长：每 30 秒采样共 60 点，
RSS/PSS 线性回归斜率 ≤ 0.5 MB/分钟，末 10 分钟均值与首 10 分钟均值
差 ≤ 8 MB
```

JNI：

```text
local/global/weak reference stress
JNI call/return 后 local frame 深度回到入口值
Attach/Detach 配对
JNI_OnLoad/OnUnload 计数与 library 生命周期一致
```

Native ABI：

```text
arm64-v8a 与 x86_64 fixture 各通过 ELF load、relocation、TLS、JNI_OnLoad、
RegisterNatives、native callback 和 unload
```

Binder：

```text
synchronous/oneway/identity/death/FD 各 1,000 次压力调用
handle 引用计数最终归零
session destroy 后旧 handle 全部返回 DEAD_OBJECT
```

Framework/UI：

```text
Application/Provider/Activity fixture 连续冷启动 20 次
lifecycle 顺序一致
launcher Activity 每次提交真实 Host Surface frame
首帧后 InputChannel、Looper 和 resize 路径可用
```

预热：

```text
同一 cache key 并发 8 个 importer
只产生一个 published generation
进程中断后恢复时清理 stale temp
```

### 25.3 验收预算表

下表汇总第 21–25 章的量化阈值，取值为工程默认基线，均可在联调阶段按宿主
性能和目标应用画像校准；调整时同步更新 25.2 与本表。

| 验收项 | 阈值（可校准基线） | 判定方式 |
| --- | --- | --- |
| Runtime isolated worker 轮次 | 100 轮 | 25.2，每轮 root set 完整 |
| GuestTask / Carrier 规模 | 4096 GuestTask / 1..16 Carrier | 25.2，无 deadlock/lost wakeup/leak |
| wait-wake 压测 | mutex/monitor/condition 各 100,000 次 | 25.2，计数平衡 |
| deadline/IO reactor wake | 各 10,000 次 | 25.2，wake 与注册对齐 |
| P3 前进 | 完成间隔 P99 ≤ 5,000 ms | 25.2，调度器采样 |
| main Looper 响应 | 调度延迟 P99 ≤ 50 ms；单消息 > 1,000 ms 计数 = 0 | 25.2，Looper 事件采样 |
| GC 压测轮次 | ≥ 10,000 轮 | 25.2，年轻/完整 GC |
| GC 后引用可解析 | gc_root_verify_fail_count == 0 | 25.2，每轮 GC 后全量 root 校验 |
| idle 内存增长 | RSS/PSS 斜率 ≤ 0.5 MB/分钟；首尾 10 分钟均值差 ≤ 8 MB | 25.2，30 分钟 60 点采样 |
| JNI local frame 深度 | call/return 后回到入口值 | 25.2，压测采样 |
| Native ABI fixture | arm64-v8a 与 x86_64 各 1 组全链路 | 25.2，ELF load 到 unload |
| Binder 压力 | synchronous/oneway/identity/death/FD 各 1,000 次 | 25.2，handle 计数归零 |
| 冷启动一致性 | 20 次 | 25.2，lifecycle 顺序一致 + 真实 frame |
| 预热并发 | 8 importer / 1 published generation | 25.2，cache lock |
| 启动阶段超时预算 | S0–S7 合计 17,500 ms（分阶段见 21.9 表） | 21.9，启动事件 elapsed_ms |
| JIT 编译产物一致性 | 100 个热点 fixture 方法结果一致率 100% | 24.2，JIT 与 interpreter diff |
| JIT roots | JIT root 校验失败数 == 0 | 24.2，GC root 校验 |
| deopt | 触发 ≥ 1,000 次，恢复执行结果一致 | 24.2，deopt 后继续执行 |
| 语义对照测试通过率 | 100%；known-failure 清单进入 M5 前清零 | 24.1 通过基线 |

### 25.4 回归失败判定方法

```text
对比基线：与上一通过版本的 golden 指标和事件序列对比，超出 25.3 阈值
判失败
重跑规则：失败项自动重跑 1 次，仍失败判回归失败；重跑通过登记为 flaky
启动回归：lifecycle/诊断事件序列与 golden 比对，缺步骤、乱序或阶段超时
判失败
性能回归：阶段耗时超过 21.9 超时预算或 25.3 阈值判失败
判定输出：RegressionVerdict{pass|fail|flaky}，附 BootTraceId、超限项、
实测值和阈值
```

### 25.5 日志格式与存储约定

```text
格式：JSON Lines（UTF-8），每行一个结构化事件
字段：沿用 §23 启动事件/异常记录字段，公共字段含 BootTraceId、phase、
state、elapsed_ms、session_generation、wall_time（ISO 8601）
目录：<log_root>/<BootTraceId>/boot.jsonl、exception.jsonl、
regression.jsonl
轮转：保留最近 20 个 BootTraceId 目录，更旧压缩为 .jsonl.gz 归档
脱敏：不记录凭据和用户私有内容，路径/descriptor 按 §23 异常记录字段
```

### M1：核心 VM

- DEX 解析与校验（Header/id 表/class_data/CodeItem）。
- ClassLinker：Find/Define/Link/Initialize、DexCache/ClassTable、状态机。
- switch 解释器：invoke 五种派发、DoCall、StringFactory 重写、异常投递。
- 对象模型：Object/Class/Array/String、InternTable。
- 非移动 mark-sweep GC 与根扫描（含 JNI 根）。
- 协程最小面：GuestTask/Carrier/safepoint/wait-wake。
- JNI 最小面：引用表、GetMethodID/Call 系、generic JNI stub。
- 跑通 fixture DEX（单 DEX、无加固壳）。

### M2：动态加载 API 面

- ClassLoader 全家：BaseDex/Path/Dex/InMemoryDexClassLoader、双亲委派、
  DexPathList.Element。
- dalvik.system.DexFile 全语义（openDexFile/openInMemory/defineClassNative/
  getClassNameList/closeDexFile、cookie）。
- JNI 全面：RegisterNatives、System.loadLibrary/JNI_OnLoad/
  SetClassLoaderOverride、引用生命周期全套、反射 ID 面。
- 加固器 DEX 再生成路径（DexFileLoader+Verify 重建 DexFile/DexCache）。
- 壳的动态加载流程可完整走通（fixture 壳用例）。

### M3：语义保真验证

- 加固 APK（如知乎/B站）全程跑通：壳解密、动态加载、进入真实 Application。
- 失败归因语义偏差并修正语义（不做加密/对抗专项）。
- 对照第 14 章壳 API 考纲逐项打勾。

### M4：Framework 深度

- ActivityThread/LoadedApk/ContextImpl。
- Application/Provider/Activity 生命周期。
- Window/Input/frame、首帧链路。
- Binder（可与 M4 并行）：Guest Binder domain/Parcel/ServiceManager/AIDL。

### M5：稳定化和回归

- static prewarm/cache generation。
- 语义对照测试（24.1）known-failure 清零。
- generic fixture APK 与目标应用启动/首帧回归。
- media/audio/video/sensor Bridge 收尾。

## 26. 完成标准

- `libdexvm.so` 包含一个行为对齐 Android/ART 原生的全自研运行时。
- ClassLinker、Interpreter、JNI、GC 语义与 Android 原生一致（对照源码裁定）。
- Guest Memory、DexCache、JIT（远期）和 coroutine stack 由 MV 管理。
- GC roots、moving reference 更新（远期）、weak reference 和 collector 路径可用。
- runtime-libs 作为 Guest boot classpath 工作。
- Guest Thread 使用 FiberContext、Carrier TLS 切换和 coroutine wait queue。
- Native frame 使用 pinned Carrier，JNI local/global/weak 生命周期完整。
- Native Manager 完成 ELF、ABI、namespace、JNI_OnLoad/OnUnload 和依赖释放。
- Application/Provider/Activity 按 AOSP 启动顺序执行。
- Binder Guest/Host translation、calling identity、death 和 FD 路径可用。
- Framework compatibility matrix 中所需组件均有 Guest/ Bridge 实现。
- SurfaceControl/BufferQueue/VSync/HWUI/InputChannel 首帧链路可用。
- launcher Activity 提交 Guest 首帧后进入 Interactive。
- 启动诊断包含 phase、generation、ART state 和 Bridge result。

## 26.1 当前实施状态（as-built，2026-11-20）

本节记录截至 2026-11-20 的里程碑实际状态；上面 23–26 章仍是验收规格。

| 里程碑 | 状态 | 说明 / 设备侧证据 |
| --- | --- | --- |
| M1 核心 VM | ✅ 通过 | DEX/ClassLinker/解释器/对象/GC/协程/JNI 最小面跑通 fixture；host/device 冒烟长期回归 |
| M2 动态加载 API | ✅ 通过 | ClassLoader 全家/DexFile/JNI 全面/反射；加固 APK 动态加载走通 |
| M3 语义保真 | 🟡 主链通过 | 加固 APK 壳解密/动态加载/Application/Activity 生命周期通过；业务页面按 UI 物化桥逐屏回归 |
| M4 Framework 深度 | 🟡 已落地（规格子集） | 生命周期 + 首帧 traversal + ViewSnapshot/ViewPatch 物化出画；Surface/PresentFrame/InputChannel 规格路径未接线（以宿主真 View 物化替代，见 09 章 18.8 / 10 章 19.7） |
| M5 稳定化/回归 | 🟡 进行中 | 设备冷启动/会话回归、target-app widget 画廊持续扩展；启动速度与清理项见根 README §10 |

UI 桥批次设备验证记录（MuMu 模拟器，host `com.dexvm.host`，guest
`com.coke.target`；断言 = uiautomator 真控件树 + logcat）：

| 功能 | 验证结果 |
| --- | --- |
| androidx DrawerLayout 侧滑 | 边缘手势/导航按钮开合，`kPropDrawerOpen` 与 `nativeDispatchViewDrawer` 双向同步 |
| Toolbar | 真 `android.widget.Toolbar` 标题/子标题/汉堡/溢出菜单；`nativeDispatchViewMenuClick` 回灌 guest |
| AutoCompleteTextView | 宿主真下拉，建议项来自 `kPropSuggestions`；键入回灌 `TextWatcher` |
| DatePicker/TimePicker | 宿主真选择器，年月日/时分经 `nativeDispatchViewFields` 双向同步 |
| Dialog / AlertDialog | 叠加根 + dim 遮罩；按钮点击回灌 `DialogInterface.OnClickListener` |
| Toast | 宿主真系统 Toast |
| PopupMenu | 宿主真弹层菜单；点击 `(token,0,index)` 回灌 `OnMenuItemClickListener`，外点 `(token,1,0)` 回灌 `OnDismissListener` |
| PopupWindow | 浮层卡片（elevation）+ 透明外点关闭层；外点关闭摘除叠加根并回灌 `OnDismissListener` |

新增诊断 tag：`DexVmDialog`（窗口叠加）、`DexVmPopup`（弹层桥）、
`DexVmToast`、`DexVmNotify`、`DexVmSensors`、`DexVmTlsBridge`。
