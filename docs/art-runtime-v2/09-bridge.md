# Host、UI 与设备 Bridge

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](08-binder.md) · [下一部分](10-framework-and-warmup.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。

## 18. Bridge

Bridge 是 Guest Runtime 与宿主 Android 的唯一交互层。MVContext 的
Bridge Capability Table 在 S1/S4 启动阶段绑定 Host、UI、Device 三组
capability。所有跨 Bridge 引用宿主资源的对象都是 generationed opaque
handle，语义见 18.7。

本章签名使用 C 风格描述。约定：

- 返回 `int32_t` 的函数返回 `0` 表示成功，负值为 `-errno` 风格错误码，
  映射见 18.5。
- 句柄参数全部是 opaque struct，Guest 不解引用内部字段。
- 同步调用的阻塞等待使用 coroutine wait，不长期占用 Carrier。
- Host 回调不直接进入 Guest，一律经 Bridge callback queue 投递，见 18.6。

### 18.1 总览与能力表

```text
BridgeCapabilityTable {
  session_generation
  host_bridge          // HostBridge*
  ui_bridge            // UIBridge*
  device_bridge        // DeviceBridge*
  callback_queue       // Bridge callback queue
}
```

绑定流程：

```text
CreateMVContext(bootstrap)
  -> store Host/UI/Device capabilities
  -> bind Bridge capabilities (S1)
  -> bind Bridge capabilities (S4 Framework State)
```

通用句柄形态，各 Bridge 的具体句柄都是它的展开：

```text
BridgeHandle {
  kind                 // HOST_SERVICE / SURFACE / INPUT_CHANNEL / SESSION ...
  session_generation
  object_id
}
```

分层职责：

| Bridge | 职责 | 对应宿主能力 |
| --- | --- | --- |
| HostBridge | Binder、系统服务、文件/FD、属性、进程、宿主 JavaVM attach | 宿主 Android 系统 API |
| UIBridge | SurfaceControl/BufferQueue、InputChannel、Vsync、PresentFrame、遮罩 | SurfaceFlinger/InputManager/Choreographer |
| DeviceBridge | Media/Audio/Video/Sensor/Speech session | Camera2/MediaCodec/AudioTrack/SensorManager/Speech |

权限与策略判断不在 Bridge：Guest Framework Runtime 负责 permission、
AudioFocus、camera policy、sensor sampling policy、Activity/Application
状态和设备选择；Bridge 只执行对应的 Host 调用与回调传递。

### 18.2 Host Bridge

Host Bridge 提供：

- JNI/native ABI 调用：宿主经 `libdexvm.so` 的 JNI contract symbol 进入
  Guest；Guest 经 HostJNIEnv 调用宿主 Android Java API。
- Binder transaction：host target 的 `HostBridge.BinderTransact`。
- PackageManager、文件、属性、进程和系统服务。
- 宿主 JavaVM attach/detach。
- Host callback 进入 Guest。

结构与核心签名：

```text
HostBridge {
  host_vm              // 宿主 JavaVM*
  host_binder          // HostBinderContext*
  session_generation
}

int32_t HostBridge.lookupSystemService(const char* name,
                                       HostBinderHandle* out_handle);

int32_t HostBridge.BinderTransact(HostBinderHandle target,
                                  uint32_t code,
                                  const HostParcel* in,
                                  HostParcel* out,
                                  uint32_t flags);
```

`lookupSystemService` 与 17.3 的 `getService(name)` 对接：

```text
getService(name)
  -> Guest service_registry
  -> HostBridge.lookupSystemService(name)
  -> Guest BinderProxy
```

- 未注册服务返回 `-ENOENT`，Guest 不创建 `BinderProxy`。
- 成功返回的 `HostBinderHandle` 由 Bridge 分配并记录 interface descriptor，
  17.6 的 Guest/Host Binder translation 在其上建立 `GuestBinderHandle`。
- 查找结果可缓存在 Guest `service_registry`；`session_generation` 失效后
  缓存整体作废。

`HostBridge.BinderTransact` 与 17.4 的 host target 分支对接：

```text
host target -> HostBridge.BinderTransact
host synchronous -> await coroutine completion
host oneway -> return immediately
```

- 同步 transaction 期间 Guest 侧 coroutine wait，完成后恢复 GuestTask。
- flags 携带 `FLAG_ONEWAY` 时立即返回，reply 不写入 `out`。
- caller identity 按 17.6 映射：Guest `TransactionContext` 由 Bridge 映射到
  Host call window；Host 回调方向由 Bridge 读取可信 caller identity 并写入
  Guest `TransactionContext`。
- `in`/`out` 中的 FD 遵守 17.2/17.6 的 dup/ownership 规则，见 18.7。
- 弱引用 promotion 失败返回 `DEAD_OBJECT`（17.6）。

宿主 JavaVM attach：

```text
int32_t HostBridge.AttachHostThread(HostJNIEnv** out_env);
void    HostBridge.DetachHostThread(void);
```

- Bridge 宿主 worker 线程调用宿主 Android Java API（PackageManager、
  AudioRecord 等）前必须 attach 宿主 JavaVM。
- 宿主 JNI callback 线程进入 Bridge 时通常已 attach，Bridge 复用其
  HostJNIEnv。
- 线程退出前 `DetachHostThread`，Bridge 保证 attach 计数归零后不残留
  TLS 状态。

文件/FD 访问：

```text
int32_t HostBridge.OpenFile(const char* path, uint32_t flags, int32_t* out_fd);
int32_t HostBridge.DupFd(int32_t fd, int32_t* out_fd);
int32_t HostBridge.ReadFile(int32_t fd, void* buf, size_t len, size_t* out_read);
int32_t HostBridge.WriteFile(int32_t fd, const void* buf, size_t len,
                             size_t* out_written);
int32_t HostBridge.CloseFd(int32_t fd);
```

属性与进程/包信息：

```text
int32_t HostBridge.GetProperty(const char* key, char* out_value, size_t cap);
int32_t HostBridge.SetProperty(const char* key, const char* value);
int32_t HostBridge.GetPackageInfo(const char* package_name,
                                  HostPackageInfo* out_info);
int32_t HostBridge.GetProcessInfo(HostProcessInfo* out_info);
```

- `HostPackageInfo` 为 opaque host metadata（19.5 LoadedApk 行），包含
  versionCode、targetSdk、split 名称和 nativeLibraryDir。
- 文件路径限定在 Session 数据目录和宿主授权目录内，越界返回 `-EACCES`。

Host callback 进入 Guest 沿用 17.4 的通道：

```text
Host Binder transaction
  -> Bridge validate handle/generation
  -> import Parcel/FD/Binder object
  -> enqueue Guest Binder task
  -> Guest Stub.onTransact
```

### 18.3 UI Bridge

UI Bridge 提供：

- Surface/Window 创建、更新和销毁。
- Window 尺寸、位置、可见性和焦点。
- InputEvent、触摸、按键和窗口事件。
- Choreographer/vsync 和 frame callback。
- frame 提交。
- 启动遮罩、首帧和错误视图。

结构：

```text
UIBridge {
  host_composer        // Host SurfaceFlinger 适配
  host_input           // Host InputManager 适配
  session_generation
}
```

SurfaceControl 与 BufferQueue：

```text
SurfaceControlDesc {
  parent               // 父 SurfaceControlHandle，可为 invalid
  x, y, width, height
  format
  flags
}

SurfaceControlHandle {
  session_generation
  object_id
  buffer_queue_id      // 与 Host BufferQueue 的对应 id
}

int32_t UIBridge.SurfaceControl.create(const SurfaceControlDesc* desc,
                                       SurfaceControlHandle* out_sc);
int32_t UIBridge.SurfaceControl.resize(SurfaceControlHandle sc,
                                       int32_t width, int32_t height);
int32_t UIBridge.SurfaceControl.destroy(SurfaceControlHandle sc);
```

- `create` 同时创建 Host SurfaceControl 和对应 BufferQueue，返回
  Guest 侧 wrapper 使用的 opaque handle（19.5 SurfaceControl/BufferQueue 行）。
- `resize` 触发 Host inset/resize 事件，经 Bridge queue 投递 Guest
  `InsetsController`（19.5 InsetsController 行）。
- `destroy` 释放 Host SurfaceControl/BufferQueue 并使 handle 失效。
- Guest Surface buffer 元数据可见，buffer 分配/queue/dequeue 由 Host
  BufferQueue 执行（19.5 BufferQueue 行）。

Window 状态：

```text
int32_t UIBridge.setWindowBounds(SurfaceControlHandle sc,
                                 int32_t x, int32_t y,
                                 int32_t width, int32_t height);
int32_t UIBridge.setWindowVisibility(SurfaceControlHandle sc, bool visible);
int32_t UIBridge.setWindowFocus(SurfaceControlHandle sc, bool focused);
```

窗口状态机与 19.6 一致：

```text
create -> surface-created -> visible -> focused
  -> resize/config-change -> redraw
  -> hidden -> destroyed
```

InputChannel：

```text
InputChannelHandle {
  session_generation
  object_id
  event_fd             // Host InputChannel fd 的 dup
}

int32_t UIBridge.InputChannel.create(SurfaceControlHandle sc,
                                     InputChannelHandle* out_ch);
int32_t UIBridge.InputChannel.enable(InputChannelHandle ch);
int32_t UIBridge.InputChannel.destroy(InputChannelHandle ch);
```

- Host InputChannel fd 的生命周期所有者是 Host Android（02 章资源表）；
  Bridge 读取事件并导入 Guest input queue，Guest InputManager 负责
  event dispatch（19.5 InputManager/InputChannel 行）。
- `enable` 激活 input 转发，只允许在首帧完成后调用（19.6
  `activate InputChannel forwarding`）；此前事件由启动遮罩阶段丢弃。
- 触摸、按键和窗口事件统一编码为 Guest InputEvent 后经 callback queue
  投递到目标 Guest Handler。

Vsync：

```text
int32_t UIBridge.Vsync.schedule(const VsyncCallback* callback,
                                uint64_t* out_token);
int32_t UIBridge.Vsync.cancel(uint64_t token);
```

- Choreographer frame deadline 来自 Host VSync signal（19.5
  Choreographer/DisplayManager 行）。
- Vsync 回调不直接执行 Guest 代码，按 18.6 经 Bridge queue 投递到
  main GuestTask 的 message queue。

PresentFrame：

```text
int32_t UIBridge.PresentFrame(SurfaceControlHandle surface,
                              BufferHandle buffer,
                              FenceHandle fence);
```

- 语义与 19.4/22.8 一致：Guest `ViewRootImpl` traversal 产生 render
  node/display list，HWUI/RenderThread 提交后调用
  `UIBridge.PresentFrame(surface, buffer, fence)`，Host Surface 提交
  frame，等待 Host Surface present 确认后标记首帧。
- `buffer` 来自 Host BufferQueue dequeue 的 buffer handle；`fence` 是
  acquire fence，invalid fence 表示内容立即可用。
- present 确认以 `frame-present` 回调形式经 Bridge queue 投递
  （19.6 `UI Bridge frame-present callback -> mark first guest frame`）。

首帧遮罩与错误视图：

```text
int32_t UIBridge.Overlay.show(const OverlayDesc* desc);
int32_t UIBridge.Overlay.hide(void);
int32_t UIBridge.ErrorView.show(const ErrorViewDesc* desc);
int32_t UIBridge.ErrorView.hide(void);
```

- `Overlay.show` 对应 S0 的 `create Host window/loading overlay`；
  `Overlay.hide` 对应 S7 首帧后的 `remove loading overlay`。
- 首帧完成条件（19.6）：launcher Activity RESUMED、Guest Window attached、
  traversal complete、render submitted、Host surface frame presented。
  满足后移除遮罩、激活 InputChannel 转发、启动正常 Looper/input dispatch。
- `ErrorView` 在启动失败或运行期致命错误时显示宿主错误视图，session
  销毁时一并移除。

### 18.4 Media/Audio/Video/Sensor Bridge

设备数据使用 generationed session。统一生命周期：

```text
check Guest permission/API availability
  -> select device/audio mode
create
  -> configure
  -> start
  -> data/callback
  -> stop
  -> release
```

Session 句柄与生命周期 API：

```text
SessionKind {
  AUDIO_TRACK
  AUDIO_RECORD
  CAMERA
  MEDIA_CODEC
  MEDIA_PLAYER
  MEDIA_PROJECTION
  SENSOR
  SPEECH_RECOGNIZER
  TEXT_TO_SPEECH
}

DeviceSessionHandle {
  kind
  session_generation
  session_id
  config_generation
}

int32_t DeviceBridge.create(SessionKind kind, const SessionConfig* config,
                            DeviceSessionHandle* out_session);
int32_t DeviceBridge.configure(DeviceSessionHandle session,
                               const SessionConfig* config);
int32_t DeviceBridge.start(DeviceSessionHandle session);
int32_t DeviceBridge.stop(DeviceSessionHandle session);
int32_t DeviceBridge.release(DeviceSessionHandle session);
```

`SessionConfig` 是 per-kind union：音频为 sampleRate/channelMask/format/
bufferSize，camera 为 cameraId/direction/size，MediaCodec 为 mime/profile/
surface，sensor 为 sensorType/samplingPeriodUs/maxReportLatencyUs。
`configure` 仅允许在 `start` 前调用；重复 `release` 返回 `-EINVAL`。

音频：

```text
Guest AudioTrack write -> PCM ring -> Host AudioTrack
Host AudioRecord -> PCM ring -> Guest AudioRecord read

int32_t DeviceBridge.AudioTrack.write(DeviceSessionHandle session,
                                      const void* pcm, size_t len,
                                      size_t* out_written);
int32_t DeviceBridge.AudioRecord.read(DeviceSessionHandle session,
                                      void* pcm, size_t cap,
                                      size_t* out_read);
```

- PCM ring 由 Bridge 管理，写满/读空返回短写/短读，调用方 coroutine wait
  后重试。
- AudioFocus 判定由 Guest Framework Runtime 完成，Bridge 不做策略。

视频：

```text
Guest Camera/MediaCodec/MediaPlayer
  -> Video Bridge session
  -> Host Camera2/MediaCodec/MediaProjection
  -> Surface/ImageReader/buffer handle
  -> Guest callback
```

```text
VideoBufferHandle {
  session_generation
  session_id
  buffer_id
}
```

- Host 侧 Surface/ImageReader 产出的 buffer 以 `VideoBufferHandle` 引用，
  帧回调签名 `onFrame(session, VideoBufferHandle buffer, FenceHandle fence)`。
- buffer 读取完成后 Guest 交还 Bridge 归还 Host buffer 池，超期未归还由
  Bridge 强制回收并投递错误事件。

传感器：

```text
Guest SensorManager.registerListener
  -> Sensor Bridge
  -> Host SensorManager
  -> SensorEvent batch
  -> Guest Handler

int32_t DeviceBridge.Sensor.registerListener(int32_t sensor_type,
                                             int64_t sampling_period_us,
                                             int64_t max_report_latency_us,
                                             const SensorCallback* callback);
int32_t DeviceBridge.Sensor.unregisterListener(int32_t sensor_type);
```

- Host SensorManager 按 batch 收集 `SensorEvent`，整批经 callback queue
  投递到注册时指定的 Guest Handler。
- sampling policy 由 Guest Framework Runtime 决定，Bridge 只传递参数。

语音：

```text
microphone PCM / SpeechRecognizer / TextToSpeech
  -> Audio/Speech Bridge
  -> Guest callback queue
  -> Guest Handler

int32_t DeviceBridge.Speech.startListening(DeviceSessionHandle session,
                                           const SpeechConfig* config);
int32_t DeviceBridge.Speech.stopListening(DeviceSessionHandle session);
int32_t DeviceBridge.Speech.speak(DeviceSessionHandle session,
                                  const char* text,
                                  const SpeechConfig* config);
```

Host 回调先进入 Bridge callback queue，再投递到对应 Guest task/Handler
（18.6）。session 释放顺序固定 `stop -> release`；`DestroyMVContext` 时对
未 release 的 session 执行强制 `stop + release`。

### 18.5 错误映射

Bridge C API 返回负 errno 风格错误码；Host Java 层异常和 binder status
由 Bridge 归一化后再翻译。Guest JNI 层按最后一列抛出 Java 异常或返回码。

| Host errno/status | Bridge 返回码 | Guest 异常/返回码 | 典型场景 |
| --- | --- | --- | --- |
| 0 / STATUS_OK | 0 | 成功 | — |
| ENOENT | -ENOENT | `FileNotFoundException` | 服务/文件不存在，lookupSystemService 未命中 |
| EACCES / EPERM / SECURITY | -EACCES | `SecurityException` | 越权路径、permission 拒绝 |
| EBUSY | -EBUSY | `IllegalStateException` | camera/AudioFocus/设备占用 |
| EAGAIN / EWOULDBLOCK | -EAGAIN | 短读/短写返回码 | ring 满/空，coroutine wait 后重试 |
| EINTR | -EINTR | 不暴露，Bridge 内部重试 | 宿主信号中断 |
| ENOMEM | -ENOMEM | `OutOfMemoryError` | 宿主 buffer 分配失败 |
| EPIPE / ECONNRESET / DEAD_OBJECT | -EPIPE | `DeadObjectException` | 连接断开、handle 失效 |
| ETIMEDOUT | -ETIMEDOUT | `TimeoutException` | 同步调用/设备启动超时 |
| EINVAL | -EINVAL | `IllegalArgumentException` | 参数非法、重复 release |
| E2BIG / TRANSACTION_TOO_LARGE | -E2BIG | `TransactionTooLargeException` | Parcel/transaction 超限 |
| ENODEV / ENXIO | -ENODEV | `UnsupportedOperationException` | 设备不存在、API 不可用 |
| ENOSPC | -ENOSPC | `IOException` | 空间不足 |
| EROFS / EIO / 其他 | -EIO | `IOException` | 通用 IO 失败 |

规则：

- Host Java 异常先映射为 errno/status：`SecurityException` -> SECURITY，
  `DeadObjectException` -> DEAD_OBJECT，`IllegalStateException` -> EBUSY，
  其余 `IOException` -> EIO。
- 同步 Guest 调用把返回码翻译为 Java 异常；oneway 与回调路径的错误投递到
  目标 Guest Handler，无人处理时进入 uncaught handler（06 章 Async
  callback 语义）。
- handle `session_generation` 不匹配统一返回 `DEAD_OBJECT`（18.7）。

### 18.6 线程模型

| API 类别 | 允许调用线程 | 等待/投递方式 |
| --- | --- | --- |
| `HostBridge.lookupSystemService` | 任意 GuestTask | coroutine await |
| `HostBridge.BinderTransact`（sync） | 任意 GuestTask | coroutine await 完成 |
| `HostBridge.BinderTransact`（oneway） | 任意 GuestTask | 立即返回 |
| 文件/FD/属性/包信息 | 任意 GuestTask | coroutine await |
| `UIBridge.SurfaceControl.*`、Window 状态 | 任意 GuestTask（通常 main） | coroutine await |
| `UIBridge.PresentFrame` | Guest HWUI/RenderThread 提交线程 | present 确认回调 |
| `UIBridge.InputChannel.enable` | main GuestTask（首帧后） | 立即生效 |
| `UIBridge.Vsync.schedule/cancel` | main GuestTask（Choreographer） | 回调投递 |
| `DeviceBridge.*` session 操作 | 任意 GuestTask | coroutine await |
| 各 Bridge 回调 | 不可由 Guest 线程直接调用 | Bridge queue -> Guest Handler |

约定：

- 阻塞宿主调用在 Bridge 宿主 worker 线程执行，Guest 侧 coroutine wait，
  唤醒后恢复原 GuestTask（08 章 `host synchronous -> await coroutine
  completion`）。
- Host 回调统一路径（06 章 Async callback）：

```text
Host/Device callback
  -> Bridge generation check
  -> create CallbackEvent(target_thread, target_handler, global refs, args)
  -> Guest callback queue
  -> wake target GuestTask
  -> attach/bind JNIEnv
  -> PushLocalFrame
  -> invoke Guest callback method
  -> deliver exception to Guest Handler/uncaught handler
  -> pop local frame
```

- 回调目标是 main Looper 时写入 Guest main message queue 并 wake main
  GuestTask；目标是 native-created thread 时 `GetEnv` 失败则
  `AttachCurrentThread`（07 章 16.7）。
- Host Input、VSync、Binder 和 device callback 写入 Guest message queue 后
  wake 对应 task（14.10 Looper 实现），Bridge 不持有 Guest 对象强引用之外
  的执行权。
- 回调在 Bridge queue 线程只做 generation 校验和参数 import；Guest 方法
  执行严格发生在目标 GuestTask 上，保证 Java Monitor/HLooper 线程约束。

### 18.7 句柄生命周期与 FD ownership

session_generation 失效语义：

```text
create
  -> handle = {kind, session_generation, object_id}

use
  -> validate handle.session_generation == MVContext generation
  -> mismatch -> DEAD_OBJECT

DestroyMVContext / session destroy
  -> invalidate generation
  -> all outstanding handles -> DEAD_OBJECT
```

- 所有 Bridge handle（HostBinderHandle、SurfaceControlHandle、
  InputChannelHandle、DeviceSessionHandle、VideoBufferHandle）都带
  `session_generation`。
- session destroy 后旧 handle 全部返回 `DEAD_OBJECT`，不再触碰宿主资源。
- 弱引用 promotion 失败同样返回 `DEAD_OBJECT`（17.6），与 handle 失效
  路径一致。

FD ownership/dup 规则与 17.2/17.6 的 Parcel FD 规则呼应：

```text
Host 返回 FD（OpenFile、InputChannel event_fd）
  -> Bridge dup fd
  -> Guest 拥有 duplicate
  -> Guest CloseFd 释放 duplicate

Guest 传入 FD（WriteFile、transaction 内 FD）
  -> dup fd
  -> 调用方拥有 duplicate
  -> 调用成功 -> ownership 转移给接收方
  -> 调用失败 -> 关闭未认领的 duplicate

Parcel FD（17.2/17.6）
  -> writeFileDescriptor 时 dup，Parcel 持有 duplicate
  -> transaction 成功 -> ownership 转移给 reader
  -> transaction 失败 -> 关闭未认领的 duplicate
  -> parcel 释放 -> 关闭仍由 parcel 持有的 FD
```

要点：

- 每个 FD 在任一时刻只有一个 owner；Bridge 不共享未 dup 的 fd 跨
  Guest/Host 边界。
- `Surface buffer、InputChannel、Binder proxy、宿主 fd` 的宿主侧生命周期
  由 Host Android 管理（02 章资源表），Guest 只通过 generationed handle
  或 dup 出的 fd 访问。
- Overlay/错误视图生命周期归 UIBridge：`Overlay.show` 在 S0，`hide` 在
  首帧完成后；session 销毁时强制隐藏并释放宿主视图。
- `DestroyMVContext` 顺序（01 章 5.3）：release media/audio/video/sensor
  sessions -> invalidate Binder handles -> 关闭 Guest roots，Bridge 据此
  保证先停回调再失效 handle，避免回调落在已失效 generation 上。

### 18.8 P1 UI 物化桥（当前实现，as-built）

> 18.1–18.7 是 UIBridge 的**行为规格**（SurfaceControl / BufferQueue /
> InputChannel / PresentFrame 路径）。P1 阶段先落地了更低成本的
> **宿主真 View 物化**路径：Guest 只产生 View 树与字段态快照，宿主把每个
> 节点换成真 `android.view.View`（HWUI 出画），命中测试/交互回灌走桥接 API。
> SurfaceControl/PresentFrame 规格路径尚未接线；两者语义目标一致（宿主真
> 出画 + 首帧确认 + 输入回灌），实现入口不同。

**传输协议**（`bridge/ui_bridge_protocol.{h,cc}`，小端二进制）：

```text
Envelope := magic "UIBR" u32 | u16 version | u8 kind | u8 reserved
          | u32 revision | u32 snapshot_bytes | u32 draw_bytes
          | snapshot 段 | draw 段
kind: 1 = 全量快照（新基线），2 = 增量 patch（base_revision -> revision）

ViewNode { handle{object_id, kind}, guest_view_id, class_name,
           parent_id, child_index, props[], children[] }
PropKey 1..38（PropValue 泛型：int/float/bool/string/rect/point/handle/bytes）
```

关键属性（完整清单见协议头文件）：

| 组 | PropKey |
| --- | --- |
| 基础 | text、visibility、bounds、padding、textSize、textColor、backgroundColor、alpha、scroll、measured、layoutParams、translation、gravity |
| 图片/播放 | imageResource、imageBytes、progress、videoPath/seek/playing |
| 布局 | layoutGravity、orientation、margins、weight、layoutKind |
| 控件状态 | checked、hasClickListener、hasLongClickListener、rating、numStars |
| 容器/窗口 | drawerOpen、subtitle、navClick、menuItems、suggestions、date、time、windowDim、windowPopupToken |

**宿主物化**（`app/src/main/java/com/dexvm/host/UiBridgeMaterializer.java`）：

- 按类名后缀映射为真控件（guest 自定义子类也能命中）：TextView/ImageView/
  Button/ImageButton/EditText/AutoCompleteTextView/CheckBox/RadioButton/
  ToggleButton/Switch/CheckedTextView/SeekBar/RatingBar/ProgressBar/ListView/
  GridView/Spinner/ScrollView/LinearLayout/FrameLayout/RelativeLayout/
  Toolbar/DrawerLayout（真 androidx）/DatePicker/TimePicker/VideoView。
- 布局所有权：有 guest 绝对几何的节点按 `kPropBounds` 绝对布局；无几何
  节点由宿主按 `layoutKind/orientation/gravity/weight/margins` measure/layout。
- 窗口类叠加（多根快照）：Dialog 内容根带 `kPropWindowDim`（dim 遮罩 +
  多根托管 wrapper）；PopupWindow 内容根带 `kPropWindowPopupToken`
  （elevation 浮层卡片 + 透明外点关闭层）。
- 相等值跳过（textSize/padding/alpha/translation/background/layoutParams），
  避免每帧无谓 requestLayout；`pendingHostEdits` 窗口防止在途旧快照覆盖
  宿主刚键入的文本。

**交互回灌**（`bridge/ui_bridge_input.{h}` 实现体在 `core/framework_view.cc`；
JNI 出口 `bridge/ui_bridge_jni.cc`；常驻会话单槽在 `jni/runtime_contract.cpp`）：

```text
nativeDispatchViewClick(viewId) / nativeDispatchViewObjectClick(objectId)
nativeDispatchViewLongClick(objectId)
nativeDispatchViewText(objectId, text)
nativeDispatchViewSeek(objectId, progress, phase)
nativeDispatchViewRating(objectId, milliRating)
nativeDispatchViewDrawer(objectId, open)
nativeDispatchViewMenuClick(objectId, itemIndex)
nativeDispatchViewFields(objectId, a, b, c)        // Date/TimePicker
nativeDispatchPopupEvent(token, kind, index)      // PopupMenu click / 外点关闭
```

**宿主弹层桥**（`core/popup_bridge.{h,cc}` + `PopupMenuBridge.java`）：

- Guest lib `PopupMenu.show()` -> `VmLayoutHost.showPopupMenu` intrinsic ->
  队列（i32 kind | i32 anchorViewId | u32 token | str titles）-> 宿主
  `nativePopupTake` -> 真 `android.widget.PopupMenu` 出画。
- 菜单项点击 `(token,0,index)` -> guest `onHostMenuItemClick`；外点关闭
  `(token,1,0)` -> guest `onHostDismissed`。token -> guest 弹层对象映射
  在 VM 侧登记表（GC 外部根），宿主只回传 token。

**其它宿主桥（当前实现）**：

| 能力 | VM 侧 | 宿主侧 |
| --- | --- | --- |
| Toast | `android.widget.Toast` builtin + `core/toast_bridge.cc` | `ToastBridge.java`（真系统 Toast） |
| 通知 | `android.app.VmNotifications` + `core/notification_bridge.cc` | `NotificationBridge.java` |
| 传感器 | `android.hardware.VmSensors` + `core/sensor_bridge.cc` | `SensorBridge.java` |
| TLS | `core/tls_bridge.cc` | `TlsBridge.java`（宿主 TLS 栈） |
| 字体度量 | `core/framework_font.cc` 字体工作线程 | `FontBridge.java`（真 Paint/Typeface） |
| 弹层菜单 | `core/popup_bridge.cc` | `PopupMenuBridge.java` |

**回归口径**：MuMu 模拟器 + `uiautomator dump`（真控件树）+ logcat
`DexVmUiBridge/DexVmDialog/DexVmPopup` 事件断言；验证记录见根 README §0、
target-app README「冒烟验收记录」。
