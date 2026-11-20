# target-app — v2 顶层测试 app（旧 guest 考卷继任）

## 定位

**项目测试 app 模块**：v1 的 guest 测试 APK 模块 `target-apk`（package `com.coke.target`）归位为
v2 顶层模块（与 `app` / `runtime-libs` 同级），**源码入库、APK 由本仓库构建产出**，
为唯一常驻 guest 考卷（收官迁移轮起：测试脚本已全部迁入，旧 guest 考卷模块已删除）。

- 来源：v1 `target-apk` 模块源码（52 个文件原样入库：Java/Kotlin/C++/res/assets/manifest）；
  构建壳的已验证版本组合来自原 `tests/target-apk/build-shell/gradle/libs.versions.toml`
- 产物：`target-app/build/outputs/apk/debug/target-app-debug.apk`（8,693,706 字节，debug 签名，
  5 个 dex 约 11.7k 类，4 个 .so）。**APK 二进制不入 git**（根 `.gitignore` 含 `*.apk`），
  一律现构现出
- 构建配置：`target-app/build.gradle.kts`——三方库版本内联（AGP 9.3.0 / Gradle 9.6 / JDK 25
  实证组合），`compileSdk { version = release(37) }` 新 DSL，CMake 3.22.1 构建加固 .so
  （abiFilters x86_64 + arm64-v8a），`META-INF/io.netty.versions.properties` 排除

## 考面清单

36 个 fixture 类（35 Java + 1 Kotlin，`com.coke.target.*`）+ 2 个加固模拟 .so + 12 个三方库。
分发契约：**没有 in-APK 注册表**——按 (owner 描述符 `Lcom/coke/target/<Class>;`, 方法名, JVM 签名) 选择
fixture 方法并断言其返回的**哨兵 int**（VM 少实现一个语义就返回错值）；`MainActivity` 另有一条
设备侧直跑线（构造 Runnable 线程 + 静态调用，覆盖 UI/媒体/服务场景）。

### fixture 类目（按考面分组）

| 组 | 类 | 考点 / 哨兵 |
| --- | --- | --- |
| VM 语义 | `TargetEntry` | `<clinit>` 计数、内部类对象流、`Process.myUid/myPid`；`run()`=42 |
| VM 语义 | `RuntimeCapabilities` | switch/继承/接口/lambda/try-finally/instanceof/泛型/枚举/浮点 NaN/varargs/try-with-resources 等 DEX 形状大矩阵；`run()`=283 |
| VM 语义 | `RuntimeCollections` | HashMap/ConcurrentHashMap/AtomicReference CAS/`System.arraycopy`/`Arrays.sort` |
| VM 语义 | `RuntimeSuspendingInitFixture` / `RuntimeSuspendingEntryFixture` | 挂起式 `<clinit>` 恰好一次、协作调度保返回值；107 / 108 |
| 并发 | `RuntimeConcurrency` / `RuntimeModernConcurrency` (883 行) / `RuntimeMonitorTask` / `RuntimeTask` | ReentrantLock/Condition/monitor wait-notify/LockSupport/中断语义/Semaphore/Latch/Barrier/Phaser/Executor/Future/CompletableFuture/BlockingQueue 全家桶（作为 guest 协程等待） |
| I/O·文件 | `RuntimeFileTask` / `RuntimeIoTask` / (`RuntimeLibraryCompatibilityFixture.mappedBufferFlow`=110) | mkdirs/RAF/channel.force、`ParcelFileDescriptor.createPipe` 阻塞读写、mmap MappedByteBuffer |
| 网络 | `RuntimeSocketTask` / `RuntimeHttpTask` | 裸 Socket accept/connect、手写 HTTP/1.1 server+client |
| 网络/Netty | `RuntimeNettyServerFixture` / `RuntimeNettyHttpServerFixture` / `RuntimeNettyFastHttpServerFixture` | NIO echo + 自定义 codec、`HttpServerCodec` HTTP 服务、raw ByteBuf HTTP；601-610 / 620-623 |
| 反射 | `RuntimeReflectionFixture`(=93) / `RuntimeUnsafeFixture`(=94) | forName/newInstance/invoke/setAccessible/Modifier；`sun.misc.Unsafe` CAS/allocateInstance |
| 数学/加密 | `RuntimeBigIntegerFixture`(=96) / `RuntimeDigestFixture`(=95) / `RuntimeRandomFixture`(=97) | BigInteger 全运算面、SHA-1/SHA-256/MD5 已知向量、种子化 Random/SecureRandom |
| 时间/区域 | `RuntimeTimeLocaleFixture`(=99) / (`RuntimeLibraryCompatibilityFixture` LocalDate=104, Calendar=105) | Locale.forLanguageTag/SimpleDateFormat+UTC/Date；java.time |
| 三方 JSON | `RuntimeThirdPartyCoroutineFixture.kt` + `RuntimeFastJsonValue` + `RuntimePlatformMigrationFixture` | jackson-databind（含 `Separators`/`DefaultPrettyPrinter` 类初始化面）=402、gson、fastjson=403、fastjson2=404、okio `Buffer.sha256`、commons-lang3 `ArrayUtils`；org.json+UUID=91/92；合并面=401 |
| 三方 Rx | 同上 `.kt` | RxJava3 Observable/Flowable/Single/Maybe/Completable、Schedulers、背压、错误/取消；411-413 |
| Kotlin 协程 | 同上 `.kt` | runBlocking/async/Channel/flow 管道/withTimeoutOrNull/supervisorScope；301-305 |
| Android 框架 | `MainActivity`（LAUNCHER）/ `RuntimeService` / `RuntimeBoundService` | Activity 全生命周期+触摸+Handler+动态视图+媒体（MediaPlayer/VideoView/HLS/AudioManager/AudioDeviceInfo）；started Service；bound Service（独立进程 `:bound`、自定义权限、Binder.onTransact 位集）。**生命周期 fixture 面**：onCreate/onStart/onResume 追加 lifecycleLog token；设备直跑线（UI/媒体/服务）VM 缺口容错落便携首帧树（TextView "target ok"，桥接出画输入） |
| 生命周期 fixture | `RuntimeLifecycleApp` / `RuntimeLifecycleProvider` | Application 子类持静态 `lifecycleLog`（**字段名契约**：runtime_contract 按名读取）+ ContentProvider：`attachBaseContext;provider.onCreate;onCreate;activity.*;` token 语义同 AOSP 顺序（lifecycle_smoke 序断言面） |
| Android 框架 | `RuntimeBindingFixture` / `RuntimeBinderFixture` / `RuntimeIntentFixture`(=98) / `RuntimeAndroidSpecials` | bindService/linkToDeath/transact 跨任务派发；Intent/Bundle 往返；Bundle=127/Intent=15 计分面 |
| Android 框架 | `RuntimeLibraryCompatibilityFixture` (583 行) | Context 全家（files/cache/dataDir/createDeviceContext…）、PackageManager(=103)、SharedPreferences、动态 BroadcastReceiver、SQLite、资源 getColor/getDrawable、assets 读取、正则=101、StringReader/Writer=102、泛型/注解/Objects/Optional=112/113/115、`androidx.constraintlayout`（runtime_primary 布局根） |

### 加固模拟 .so（CMake/NDK，C++20，x86_64 + arm64-v8a）

- `libhardened_payload.so`：`__attribute__((constructor))` cookie、导出 `hp_mix_7f` / `hp_alive_7f`（跨库活性/完整性）
- `libhardened_fixture.so`：DT_NEEDED 直链 payload/log/dl（**链接器带依赖 dlopen**，非手动 dlopen）；
  constructor cookie 反篡改、`/proc/self/status` TracerPid 反调试、`getpid()` 密钥、滚动 XOR 字符串解密、
  `dladdr` 自省、`__android_log_print`；`JNI_OnLoad` 校验 cookie/`reserved==NULL`/版本协商返回 `JNI_VERSION_1_6`
- JNI 面（绑定 host 侧 `com.coke.vm.HardenedNativeFixture`，预期值见 v1 `GuestNativeLibraryTest.kt`）：
  静态导出 `a/b/c` + `Java_com_coke_vm_HardenedNativeFixture_register` 走 `RegisterNatives` 注册 a-g 共 7 个方法：
  `a(I)I` 跨库混合、`b()Ljava/lang/String;`="vm-hardened-so"、`c()I` dladdr+pid 键、`d([B)[B` 滚动 XOR+数组 JNI 拷贝+空指针 ThrowNew、
  `e()V` ThrowNew(`IllegalArgumentException`,"hardened-native-failure")、`f()I` 跨库完整性、`g()I` JNI 上调 Java 静态回调(0x4f21)
- 注意：**target-app 的 Java 侧没有 `System.loadLibrary`**——加载考面属于 host/VM 侧
  （VM 的 guest-native 加载器 dlopen + JNI_OnLoad，或 `System.load`），.so 从 APK `lib/<abi>/` 提取

### 三方库（依赖面 = VM 内 classpath/链接考面）

fastjson 1.2.83、fastjson2 2.0.26、gson 2.11.0、jackson-databind 2.18.2、
kotlinx-coroutines-core 1.10.2、netty 4.1.118.Final（buffer/codec/codec-http/handler/transport）、
okio 3.18.2、okhttp 4.12.0、rxjava 3.1.10、slf4j-api 2.0.16、commons-lang3 3.17.0、androidx.constraintlayout 2.0.4。
dex 规模 5 个 classes*.dex（三方占大头）。

### 资源/媒体面

- `res/layout/runtime_primary.xml`：**ConstraintLayout 根 + `<include>`**、约 15 个 id；`runtime_footer.xml`（ScrollView 面板）；`runtime_compiled.xml`（最小编译二进制 XML 探针）
- `runtime_footer.xml` 的 **Extended widget gallery**：CheckBox / RadioGroup+RadioButton / ToggleButton /
  SeekBar / ImageButton（XML 膨胀 + 点击 performClick 回灌 + checked/progress 状态桥接）；ListView
  （`ArrayAdapter` item 行物化 + 测量/堆叠）与 Spinner（点击循环选中项 + OnItemSelectedListener）；
  `Fetch Baidu HTML (OkHttp)` 按钮经 OkHttp 4.12（lib `javax.net.ssl` → 宿主 `TlsBridge` 真 TLS）
  请求 https://www.baidu.com/，把响应源码（HTTP 状态 + 截断后的 HTML）写进下方输出文本框；
  `Show VM dialog` 按钮经 lib `android.app.Dialog` + `Window` 弹出宿主叠加层（dim 遮罩 + 多根物化 +
  Dismiss 回灌）；`Show AlertDialog` 经 lib `android.app.AlertDialog.Builder`（标题/正文/三键）弹出
  叠加弹窗，按钮点击回灌 `DialogInterface.OnClickListener`；`Show Toast` 经 lib `android.widget.Toast`
  → 宿主 `ToastBridge` 用真系统 Toast 呈现。footer 的 ScrollView 由宿主物化为真
  `android.widget.ScrollView`（内容可滚动）
- `res/layout-land/runtime_primary.xml`：横屏限定符变体；`values-zh`、`values-night`、`values/styles.xml`：区域/夜间/样式限定符
- `res/raw/runtime_tone.wav` + `runtime_video.mp4`、`res/drawable/runtime_photo.jpg`：MediaPlayer/VideoView/资源解码
- `assets/activity_runtime.xml`：AssetManager 流读取

## 对照旧 guest 考卷的覆盖（收官迁移轮已收敛）

| 旧考面 | target-app 对应 | 结论 |
| --- | --- | --- |
| `:ok` 串自检 7 项（classInit/catch/nested/array/string/monitor/inherit） | 语义面由 `RuntimeCapabilities`/`RuntimeCollections`/`RuntimeSuspending*`/`RuntimeConcurrency` 等类覆盖得更宽，断言口径 = **哨兵 int**（apk_run_smoke 断言表） | **超出**覆盖面；断言口径已迁移 |
| lifecycleLog 顺序（`attachBaseContext;provider.onCreate;onCreate;activity.onCreate;activity.onStart;activity.onResume;`，Application+Provider+Activity 三件套） | `RuntimeLifecycleApp` + `RuntimeLifecycleProvider` + `MainActivity` 三件套（token 语义同 AOSP 顺序；`lifecycleLog` 字段名契约保留） | **等价**（lifecycle_smoke 序断言已迁入） |
| 首帧（单 TextView，textSize 16/padding 48，像素金样） | 便携首帧树（TextView "target ok"，textSize 16/padding 48）+ 桥轨结构断言（ViewSnapshot 字段/revision/首帧标记；自绘像素轨已退役） | **口径迁移**（render_smoke 桥接断言形） |

## 怎么用

1. **VM 侧（考卷主用法）**：按 `app/src/main/cpp/tests/core/apk_run_smoke.cc` 的引导链加载——
   boot classpath = runtime.dex，app loader = 本 APK（`Session::Open` + `AddAppDexFile` + 绑定 `default_app()` LoaderState）。
   fixture 选取 = `Lcom/coke/target/<Class>;` + 方法名 + 签名，`InvokeMethod` 后断言哨兵 int
   （代表性哨兵：reflection=93、unsafe=94、digest=95、bigInteger=96、random=97、intent=98、timeLocale=99、
   regex=101、stringIO=102、packageManager=103、localDate=104、calendar=105、map=106、suspendingInit=107、
   susEntry=108、genericReflection=109、mappedBuffer=110、delayedQueue=111、annotation=112、objects=113、
   androidValue=114、valueUtil=115、capabilities.run=283、协程 301-305、三方 401-413、netty 601-623）。
2. **设备侧**：`adb install -r target-app-debug.apk && adb shell am start -W -n com.coke.target/.MainActivity`——
   冷启动即跑一轮线程/I/O/socket/HTTP/服务/媒体场景（直跑线契约见上文「分发契约」）。
3. **加固 .so**：从 APK `lib/<abi>/` 提取 `libhardened_payload.so` + `libhardened_fixture.so`，
   经 VM guest-native 加载器（dlopen + `JNI_OnLoad` 版本协商）或 `System.load`（先 payload 后 fixture）；
   JNI 方法经 host 侧 `com.coke.vm.HardenedNativeFixture` + `register()`（`RegisterNatives`）驱动，预期值参考 v1 `GuestNativeLibraryTest.kt`。

## 构建说明

前置：Android SDK（platforms `android-37.0`、build-tools 36.0.0、NDK 27.2+/28.2、CMake 3.22.1）、JDK 25、Gradle 9.6.0。

```bash
# 仓库根工程内直接构建（AGP 9.3.0 由根 build.gradle.kts apply false 备好）
gradle :target-app:assembleDebug            # 或 ./gradlew :target-app:assembleDebug
# 产物：target-app/build/outputs/apk/debug/target-app-debug.apk
```

- Kotlin 由 AGP 9 内建 Kotlin 编译（无需声明 kotlin 插件，`.kt` 自动处理）
- 三方依赖版本在 `target-app/build.gradle.kts` 内联（已验证组合：fastjson 1.2.83 / fastjson2 2.0.26 /
  gson 2.11.0 / jackson-databind 2.18.2 / kotlinx-coroutines-core 1.10.2 / netty 4.1.118.Final /
  okio 3.18.2 / okhttp 4.12.0 / rxjava 3.1.10 / slf4j-api 2.0.16 / commons-lang3 3.17.0 / constraintlayout 2.0.4）
- 如需代理拉取依赖：`export GRADLE_OPTS="-DsocksProxyHost=127.0.0.1 -DsocksProxyPort=10808 -DsocksProxyVersion=5"`

## 迁移纪要（收官迁移轮，已完成）

1. **模块归位**（前轮）：target-apk 源码入顶层 `target-app/`，接根工程出 APK；旧定位产物删除。
2. **测试迁移**（本收官轮）：
   - apk_run_smoke → 哨兵 int 断言表（34 类分发契约；VM 语义面一组 + 已知缺口面 GAP 观测，api_matrix 口径）
   - lifecycle_smoke → lifecycle fixture（`RuntimeLifecycleApp`/`RuntimeLifecycleProvider`/`MainActivity`
     三件套，lifecycleLog 序断言迁移；`lifecycleLog` 字段名契约保留）
   - render_smoke → 桥接断言（首帧管线 phase 序保留；像素金样改桥轨结构断言：协议侧
     ViewSnapshot 字段/revision/首帧标记）
   - 设备考/assets：宿主 MainActivity copyAsset 指 target-app（app 构建经 copyTargetApkAssets
     落生成 assets）；tools 脚本考卷 APK 路径指 target-app 产出（缺则 `:target-app:assembleDebug`）
3. **自绘渲染轨退役**：删像素光栅化输出（FrameBuffer 产出、8x8 字体光栅、PresentFrame 位图轨、
   nativeGetLastFrame 位图链）；保留 traversal/Canvas/measure/layout 与 draw 命令记录（缝A/缝B 输入引擎）。
4. **旧 guest 考卷模块下线**：模块目录、settings include、assets 二进制、一切残留引用清零。

## 冒烟验收记录

- 2026-11-17（旧定位，`/tmp` 壳构建，emulator-5554 arm64-v8a / API 36）：`adb install` +
  `am start -W` 冷启动成功、34/34 类 `Class.forName` 加载通过、加固 .so 依次 `System.load`
  （dlopen + `JNI_OnLoad` 返回 1.6）通过。
- 2026-11-17（归位后，`gradle -p <repo> :target-app:assembleDebug`）：构建产物与 `/tmp` 壳产物
  冒烟等价——同为 8,693,706 字节、5 dex、4 so（逐条目仅 classes3.dex 未压缩差 +4 字节）；设备侧
  重装冒烟（启动 / fixture 类加载 / 加固 .so dlopen）通过。
