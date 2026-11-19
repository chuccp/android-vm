# ART Runtime v2 实施手册

本手册是**行为规格**：Guest 运行时的执行语义严格对齐 Android/ART 原生行为，
分歧以 Android 原生为准。实现为**全自研** `libdexvm.so`（自写 DEX 解析、类
链接、解释器、JNI、GC），**不编译 ART 源码**；ART 源码快照仅作语义对照。
Guest 并发调度采用原生协程（GuestTask/Carrier）替代线程（第 14 章）。

加固 app 测试只是**语义保真度验证**：壳的解密与动态加载是普通 Android 代码，
按 Android 原生语义实现即自然通行。不做加密/对抗专项；测试失败一律归因
语义偏差并修正语义本身。

本手册按实现边界拆分为多个主题文档。章节编号沿用原始实施手册，正文内容
连续，各文档提供上一部分、下一部分和总索引跳转。

## 文档索引

| 文档 | 原章节 | 内容 |
| --- | --- | --- |
| [基础架构与交付边界](art-runtime-v2/01-foundation.md) | 1～5 | 交付目标、ART 行为对照基线、目录结构、分层职责、MVContext |
| [Guest 内存与 GC](art-runtime-v2/02-memory-gc.md) | 6 | 对象布局、分配器、对象标识、GC collector、roots、barrier、JNI native frame |
| [Bootstrap、Classpath、Session 与 ClassLinker](art-runtime-v2/03-bootstrap-and-class-loading.md) | 7～13 | Runtime 启动、boot classpath、runtime-libs、APK Session、类加载、解释器、异常 |
| [原生协程运行时](art-runtime-v2/04-coroutine-runtime.md) | 14 | GuestTask、Fiber、Carrier、调度器、IO、native pin、Guest main Looper |
| [JNI 核心接口](art-runtime-v2/05-jni-core.md) | 15.1～15.15 | 调用模型、引用、对象、类、字段、方法、数组、字符串、Monitor、JavaVM |
| [JNI 回调](art-runtime-v2/06-jni-callbacks.md) | 15.16 | callback 注册、GlobalRef、jmethodID 缓存、线程切换和异常传播 |
| [Guest Native Library](art-runtime-v2/07-native-libraries.md) | 16 | ELF、加载、RegisterNatives、namespace、ABI、native thread、卸载 |
| [Guest Binder](art-runtime-v2/08-binder.md) | 17 | Binder domain、Parcel、ServiceManager、AIDL、transaction、death、FD |
| [Host、UI 与设备 Bridge](art-runtime-v2/09-bridge.md) | 18 | Host、Surface/Input、媒体、音频、视频和传感器 Bridge |
| [Android Framework 与安装预热](art-runtime-v2/10-framework-and-warmup.md) | 19～20 | Application、Activity、首帧、兼容矩阵、静态预热 |
| [标准启动主线](art-runtime-v2/11-startup-pipeline.md) | 21 | S0～S7 启动阶段及完整启动检查范围 |
| [关键算法](art-runtime-v2/12-key-algorithms.md) | 22 | FindClass、DefineClass、DoCall、Binder、Coroutine、首帧、JNI、GC、callback |
| [诊断、测试与实施验收](art-runtime-v2/13-diagnostics-tests-and-delivery.md) | 23～26 | 诊断、语义对照测试、里程碑 M1-M5、实施顺序和完成标准 |
| [实施 API 考纲](art-runtime-v2/14-implementation-api-checklist.md) | 附录 | M1/M2 API 语义清单、壳 API 考纲、v1 差距情报 |

## 推荐阅读路径

首次阅读：

1. [基础架构与交付边界](art-runtime-v2/01-foundation.md)
2. [Guest 内存与 GC](art-runtime-v2/02-memory-gc.md)
3. [Bootstrap、Classpath、Session 与 ClassLinker](art-runtime-v2/03-bootstrap-and-class-loading.md)
4. [原生协程运行时](art-runtime-v2/04-coroutine-runtime.md)
5. [JNI 核心接口](art-runtime-v2/05-jni-core.md)

实现启动链路：

1. [Bootstrap、Classpath、Session 与 ClassLinker](art-runtime-v2/03-bootstrap-and-class-loading.md)
2. [原生协程运行时](art-runtime-v2/04-coroutine-runtime.md)
3. [JNI 回调](art-runtime-v2/06-jni-callbacks.md)
4. [Guest Native Library](art-runtime-v2/07-native-libraries.md)
5. [Android Framework 与安装预热](art-runtime-v2/10-framework-and-warmup.md)
6. [标准启动主线](art-runtime-v2/11-startup-pipeline.md)

验收和排障：

1. [关键算法](art-runtime-v2/12-key-algorithms.md)
2. [诊断、测试与实施验收](art-runtime-v2/13-diagnostics-tests-and-delivery.md)
3. [实施 API 考纲](art-runtime-v2/14-implementation-api-checklist.md)

## 权威基线

- 语义以 Android/ART 原生行为为准。ART 源码快照为对照参考：commit
  `b753cf97923c3695338d21466fa14c57b480a59a`，branch `26Q2-release`
  （本地 checkout 路径因环境而异；快照位于
  `reference/art/`，只读对照，不参与构建）。
- 实现为全自研：Guest 对象和类状态由自研 VM Core 管理，`libdexvm.so`
  持有生命周期；语义分歧以 Android 原生为准修正。
- runtime-libs 是 Guest boot classpath 的组成部分（v1 遗产，可复用为
  boot classpath，不作约束）。
- Guest 并发实体与 Guest Thread 一一对应，并由原生协程 Carrier 执行。
- Bridge 负责 MV 与宿主之间的 ABI、Binder、文件、UI、媒体和传感器交互。
- 启动完成以 launcher Activity 提交 Guest 首帧并进入 Interactive 为准。
