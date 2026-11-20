# 实施 API 考纲

[返回实施手册索引](../art-runtime-v2-implementation.md)

[上一部分](13-diagnostics-tests-and-delivery.md)

> 本手册为行为规格：实现为全自研 libdexvm.so（不编译 ART），执行语义以
> Android/ART 原生行为为准，本地 ART 源码快照仅作对照。
> 本章是 M1/M2 里程碑的实施考纲：必须实现的 API 语义清单与对照出处。

## 27. M1 考纲：核心 VM

跑通 fixture DEX（单 DEX、无加固壳）所需的最小面。语义细节见第 3/12 章。

### 27.1 DEX 解析

| 项 | 语义要点 | 对照 |
| --- | --- | --- |
| Header | magic/checksum/SHA-1/endian/map_off/各 id 表 size+offset | `libdexfile/dex/dex_file.h:134-181` |
| id 表 | StringId(MUTF-8)、TypeId、ProtoId、FieldId、MethodId | `dex_file_structs.h:52-108` |
| ClassDef/class_data | ULEB128 四段流（static/instance fields、direct/virtual methods），encoded_field/method | `class_accessor-inl.h:66-81` |
| CodeItem | registers/ins/outs/tries/debug/insns + TryItem/handler | `standard_dex_file.h:60-93` |
| 校验 | `dex::Verify` 最小合法性校验 | docs 03 章 §8.2 |

### 27.2 ClassLinker

| 项 | 语义要点 | 对照 |
| --- | --- | --- |
| FindClass | primitive → ClassTable(LookupClass→EnsureResolved) → 数组(CreateArrayClass) → boot(FindInClassPath→DefineClass) → app loader → Java loadClass 回落 → race-safe 插入 | docs 03 章 11.1、12 章 22.1 |
| DefineClass | AllocClass(kNotReady)→ClassPreDefine→RegisterDexFile→SetupClass(kIdx)→InsertClass(竞争)→LoadClass→LoadSuperAndInterfaces(kLoaded)→LinkClass(kResolved) | docs 03 章 11.2、`class_linker.cc:3505-3760` |
| LinkClass | LinkSuperClass/LinkMethods/LinkInstanceFields/LinkStaticFields、vtable/iftable/IMT、字段布局（reference 优先 + 64/32/16/8 组 + gap-fill） | docs 03 章 11.3、`class_linker.cc:6646+` |
| InitializeClass | 锁+复查、verify、clinit owner+kInitializing、super 递归、default-method 接口、static encoded values、`<clinit>`、initialized/erroneous；同线程重入成功、他线程等待 | docs 03 章 11.4 |
| DexCache | 每 DexFile 一个（按 ClassLoader 注册，跨 loader 冲突报 InternalError）；resolved_types/methods/fields/strings 双缓存 | `mirror/dex_cache.h:590-605` |
| 状态机 | kNotReady→kIdx→kLoaded→kResolved→kVerifying→kVerified→kInitializing→kInitialized→kVisiblyInitialized（kResolving/kRetired 为克隆瞬态） | `class_status.h:29-115` |

### 27.3 解释器

| 项 | 语义要点 | 对照 |
| --- | --- | --- |
| 主循环 | dex_pc→Instruction→switch opcode→next_pc | docs 03 章 11.5 |
| invoke 派发 | static/direct 直调；super=FindSuperMethodToCall；virtual=vtable；interface=IMT（miss 走 FindVirtualMethodForInterface 线性）；j.l.Object 方法 invokeinterface 直取 vtable | docs 03 章 11.6、`entrypoint_utils-inl.h:540-665` |
| DoCall | `first_dest_reg = registers_size - inputs_size`，receiver 首参 slot，J/D 双 vreg，35c/3rc 变参 | docs 03 章 11.6、`interpreter_common.cc:1192-1387` |
| StringFactory | `String.<init>` 重写为静态 StringFactory（丢 this、vregC 修正），结果写回 this vreg 及全部 alias | docs 12 章 22.5、`interpreter_common.cc:1225-1256,283-315` |
| 异常 | pending exception、FindCatchBlock（typed 顺序→catch-all）、handler 跳转/unwind 递交 caller、`<clinit>` 包装 ExceptionInInitializerError | docs 03 章 12、`art_method.cc:328-365` |

### 27.4 对象模型与 GC

| 项 | 语义要点 | 对照 |
| --- | --- | --- |
| 布局 | ObjectHeader{klass, monitor(LockWord)}；实例区=父类字段+子类字段；Array/String/DexCache 布局 | docs 02 章 6.1-6.2 |
| 分配 | AllocateInstance（初始化后置零/null）；失败链：等在跑 GC→阻塞 GC→清 SoftReference 循环（kMaxGcAttempts=5+生产率条件）→抛 OOM | docs 02 章 6.4、12 章 22.10 |
| GC | **非移动 mark-sweep 足够（M1）**；root 清单：Thread/ShadowFrame/HandleScope/ClassTable/DexCache/InternTable/preallocated exceptions/JNI 根；移动 GC/barrier/card table 后置 | docs 02 章 6.6/6.8、12 章 22.10 |
| handle | GuestObjectHandle{session_generation, object_id, ...}；GC 只更 handle 内部指针 | docs 02 章 6.3 |

### 27.5 协程最小面

GuestTask/Carrier/FiberContext 三件套、switch 断言、safepoint 暂停（单 task
也走协议）、wait/wake 防 lost wake（docs 04 章 14.2/14.5/14.8/14.12）。

### 27.6 JNI 最小面

引用表（local/global/weak）、DecodeJObject/EncodeJObject、GetMethodID/
GetFieldID、Call/Get/Set 系、异常组、Attach/Detach、generic JNI stub
（clinit 检查→MonitorEnter→参数布局→PushLocalFrame→native→PopLocalFrame）。
详见 28.3（M2 一次性补全）。

## 28. M2 考纲：动态加载 API 面

让加固壳的动态加载流程可完整走通。典型壳流程：`Application.attachBaseContext
→ System.loadLibrary(shell) → JNI_OnLoad → RegisterNatives → 解密 DEX →
DexClassLoader/InMemoryDexClassLoader/DexFile → defineClass/loadClass →
invoke 入口`。

### 28.1 ClassLoader 全家

| 类 | 语义要点 | 对照 |
| --- | --- | --- |
| ClassLoader | loadClass 双亲委派模板；VM 链不认识时回落；broken loader 返回 null 抛 NPE | `class_linker.cc:3392-3399` |
| BaseDexClassLoader | DexPathList（Element=DexFile[]+nativeLibraryDir） | `dalvik_system_BaseDexClassLoader.cc:46-78` |
| PathClassLoader | 只读已装路径，查找序同 DexClassLoader | `class_linker.cc:3093` |
| DexClassLoader | 加载任意 dex/jar/apk + optimizedDirectory | docs 05 章 15.6 |
| InMemoryDexClassLoader | ByteBuffer/byte[] 纯内存 DEX（拷入匿名映射再打开） | `dalvik_system_DexFile.cc:251-310` |
| 查找顺序 | parent 递归→shared libraries→自身 DexFile 列表→shared-after；DelegateLast 反序（可后置） | `class_linker.cc:3078-3170` |

### 28.2 dalvik.system.DexFile

| API | 语义要点 | 对照 |
| --- | --- | --- |
| openDexFileNative | 文件路径 → DexFile[] + cookie | `dalvik_system_DexFile.cc:368-400` |
| openInMemoryDexFilesNative | Direct buffer memcpy / byte[] GetByteArrayRegion → 匿名映射 | 同上 251-310 |
| defineClassNative | cookie→DexFile→FindClassDef→RegisterDexFile→DefineClass→InsertDexFileInToClassLoader | 同上 471-527 |
| getClassNameList | 全 DexFile 的 ClassDef descriptor 去重（multidex）转 dot 名 | 同上 537-563 |
| closeDexFile | 清缓存；未注册才移除；全删净才 unmap | 同上 424-469 |
| JNI DefineClass | 不支持：log warning + 返回 nullptr（保持语义） | `jni_internal.cc:707-710` |

### 28.3 JNI 全面

| 组 | 语义要点 | 对照 |
| --- | --- | --- |
| 引用生命周期 | local frame 栈（Push/Pop/Ensure、native 返回自动释放、DeleteLocalRef 跨 frame no-op+WARNING）、global 表+强根、weak 清除+NewLocalRef 提升 | docs 05 章 15.15 |
| FindClass 规则 | nativeLoad 覆盖 loader → 当前方法声明类 loader → system → boot | `jni_internal.cc:393-420` |
| RegisterNatives | 继承链逐类先 native 同名同签名再任意同名；'!'=FastNative；SetEntryPointFromJni | `jni_internal.cc:2701-2830` |
| loadLibrary/JNI_OnLoad | 同 path/allocator 幂等、冲突拒绝；SetClassLoaderOverride 包裹 OnLoad；pending OnLoad 条件等待；OnUnload | docs 07 章、`java_vm_ext.cc:952-1170` |
| 反射 ID | FromReflectedMethod/Field、ToReflectedMethod/Field、NormalizeJniClassDescriptor | docs 05 章 15.8 |
| String 特判 | AllocObject(String)→AllocEmptyString；NewObject(String)→StringFactory | docs 02 章 6.2 |

### 28.4 壳专用加固项（v1 缺口补齐清单）

见 29 章。

## 29. 加固壳 API 考纲（语义保真验证）

加固 app 测试只是语义保真度验证：壳是普通 Android 代码，按 Android 原生
语义实现即自然通行。失败归因语义偏差，修正语义；不做加密/对抗专项。

**零壳适配原则**：不识别具体壳、不写壳专用路径/时序特判。任何
「针对某加固产品的适配代码」都视为语义保真不足的 bug，修语义而非加特判
（容器方案逐壳适配的教训——NestSpace 每种加固都要单独适配，不可取）。
兼容性差异只允许归因到语义偏差或宿主特权边界两类。

壳的加载链与考察点（逐项打勾作为 M3 验收）：

| 加载链环节 | 考察的 API 面 | 语义要求 |
| --- | --- | --- |
| 壳启动 | Application.attachBaseContext、Context 类族 | docs 19 章生命周期 |
| native 库 | System.loadLibrary、JNI_OnLoad、RegisterNatives、多参 JNI 调用约定 | 28.3 |
| 解密产物加载 | DexClassLoader/InMemoryDexClassLoader/DexFile.openDexFile、defineClassNative | 28.1/28.2 |
| 运行时对象构造 | sun.misc.Unsafe（allocateInstance/CAS/park）、java.lang.reflect 全套（Method/Constructor/setAccessible） | VM 内建（见 30 章） |
| 动态调用 | invoke-custom/invoke-polymorphic、java.lang.invoke（MethodHandle/MethodType/CallSite） | 解释器覆盖 + VM 内建 |
| 指令面 | 全 opcode（含 packed/sparse switch、fill-array-data、宽指令、混淆后的非典型组合） | 27.3 |
| 并发 | synchronized/monitor、LockSupport.park/unpark、真实多线程语义 | 协程模型下语义等价（docs 04/14 章） |
| 大分配 | 数 MB~数十 MB byte[]（解密 payload） | 堆弹性/可配置（30 章定容堆教训） |
| 环境读取 | /proc/self/*、System 属性、文件 IO | 语义对齐宿主 Android |

### 29.1 壳加载路径六型（考纲细化）

| # | 路径 | 关键 API |
| --- | --- | --- |
| A | DexClassLoader/PathClassLoader 落地加载 | DexPathList.makeDexElements→DexFile；壳常反射改 dexElements |
| B | DexFile cookie | openDexFileNative/loadClass/getCookie；cookie 结构语义需对齐 ART（vector<DexFile*>），壳会反射改写 |
| C | InMemoryDexClassLoader | ByteBuffer/byte[] 内存加载（Android 8+ 壳主通道） |
| D | ClassLoader.defineClass | 反射/JNI 形态；内部接 ClassLinker.defineClass；抽取壳会主动调用 |
| E | dlsym 直捅 libart.so 符号 | art::DexFile::OpenFile/OpenMemory、ClassLinker::DefineClass 等；dlopen/dlsym 按 Android 语义返回 |
| F | 壳自解析 DEX + mmap | 仅需 mmap/memcmp/fstat 等 libc 语义（壳 hook libc PLT，VM 对壳可见的 IO/映射必须走 libc PLT） |

### 29.2 知乎现场情报（2026-11 调研）

**考卷实测指纹（2026-11-15，模拟器 NestSpace 容器取样）**：知乎 APK 171MB/18 dex，
命中 libDexHelper.so + libbangcle_crypto_tool.so + libmsaoaidsec.so，无
classes.des（DEX 完整），Application=真实类名 ZhihuApplication——梆梆系
伪加固特征全部吻合。

- 壳栈：梆梆企业版**伪加固**（libDexHelper.so、classes.des；方法体未抽取、DEX
  完整）+ libbangcle_crypto_tool.so（so 加固/白盒 AES）+ libmsaoaidsec.so
  （强反调试/反 frida）。
- 含义：**无需处理指令抽取/VMP**；跑通加载链即可见完整业务 DEX。
- 环境检测（maps/属性/模拟器探测）不作对抗专项：失败按语义偏差归因；
  模拟器检测类硬失败记录为环境限制，留待真机阶段验证。
- 语义要求（NestSpace 实测提炼；每条都是 Android 语义，不是壳特判）：
  ① SecNeo/Bangcle 在类加载期经 JNI_OnLoad 做包检查——虚拟身份
  （包名/ApplicationInfo/dataDir）须在类加载**前**就绪；
  ② 壳的 DEX 注入走 `AppComponentFactory.instantiateClassLoader()`，
  容器/VM 环境须显式调用该路径（LoadedApk 框架路径之外）；
  ③ 壳会重置 hidden-API policy，之后需按 Android 语义恢复；
  ④ Tinker/ApplicationLike 链按**构造器签名关系**解析内嵌 Application，
  解析失败输出诊断、不静默回退壳。

### 29.3 NestSpace 对照验证流程（M3 基线）

只移植 NestSpace 的**验证闭环**（其逐壳适配模式是反面教材，不移植——见
零壳适配原则）：

| 环节 | NestSpace 实现 | VM M3 用法 |
| --- | --- | --- |
| 能力探针 | RuntimeCapabilityProbe：UID 需 Java/native/syscall 三读数一致才发布能力 | VM 能力分级；能力≠承诺 |
| 跨观察面断言 | VirtualEnvironmentValidator：包名/UID/dataDir 多观察面一致性错误码 | 解释器/JNI/GC 多观察面对照 |
| 诊断快照 | RuntimeDiagnostics JSON：hook 三态（verified/unverified/available-unexercised），不伪造命中 | VM trace/保真报告格式 |
| 启动阶段 trace | AndroidStartupCoordinator 14 阶段 outcome/duration | 知乎启动链路逐阶段 diff |
| 用例集 | testapp（组件矩阵/PM 一致性/native 探针）+ eval 语料 + 录制回放 | M3 验收用例直接搬 |

知乎实测边界（对照时勿误判为语义失败）：headless 下 Fresco 图片不提交真实
请求（占位）；TelephonyManager.getNetworkType() 的 SecurityException 属
宿主特权边界（不伪造）；enableOnBackInvokedCallback 需按 Android 语义处理
（否则 ViewRootImpl 返回键栈溢出）；primaryCpuAbi 须回填（动态 native
加载器读取）。

## 30. v1 差距情报与复用

v1（com.coke.vm）= 原生 DEX 解释器 + 框架虚拟化桥 + runtime-libs guest
boot classpath，普通 app 可跑；加固失败根因是 guest 世界是演示级封闭宇宙。

### 30.1 v1 缺口（新 VM 必须补齐，按优先级）

1. **sun.misc.Unsafe**：v1 报 "theUnsafe is unavailable"；壳普遍依赖
   allocateInstance/CAS——必须内建完整 Unsafe 语义。
2. **JNI 完整调用约定**：v1 "marshalling at most one parameter"、对象 ID
   空间有限；需要全参数 marshalling + 真实 loadLibrary/dlopen 策略。
3. **dalvik.system.* 加载器**：v1 无（靠 host 桥）；必须 VM 内建
   DexClassLoader/InMemoryDexClassLoader/BaseDexClassLoader/DexFile/PathClassLoader。
4. **解释器 opcode 覆盖**：invoke-custom/invoke-polymorphic/
   const-method-handle/const-method-type、switch、fill-array-data、宽指令。
5. **并发语义**：v1 monitor 争用仅协程挂起；需要 LockSupport.park、
   真实 native 线程与协程混合（docs 04 章 pthread shim）。
6. **堆容量**：v1 定容堆（"capacity exhausted"）；堆需弹性。
7. **反射/java.lang.invoke**：v1 只有 reflect.{Array,Field,Modifier}；
   Method/Constructor/Proxy/InvocationHandler 与 java.lang.invoke.* 是刚需。
8. 零散但致命：Runtime.exec/Process、instrument、System.exit、文件 IO 语义。

### 30.2 可直接复用

1. **runtime-libs 整模块**：OpenJDK 源树、android API 适配源码、d8→runtime.dex
   →manifest.json 流水线（打包排除集现由 `builtin_classes.def` 自动派生）。
   适配点：v1 的 24 类内建名单按 §31 边界重新分档（Java 本体下沉 lib，VM 只留
   native 面）；Context ~120 个 native 方法接宿主桥。
2. **v1 夹具矩阵**（flow 清单）：类初始化/异常/数组/弱引用+finalize+GC/
   monitor/文件 IO/静态调用/Runtime——直接作 conformance 测试集蓝本。
3. **进程模型**：isolated worker（:vm_worker）+ AIDL 协议 + 能力协商 +
   runtime.dex sha256 校验（docs 01 章 MVContext 即此设计）。
4. **占位桩手法**：高版本 API "touch 即 NoClassDefFoundError" 的确定性
   失败守卫（classes7，832 类生成式桩）。

## 31. VM 内建 vs runtime-libs 边界决策准则（一次性定界）

VM 内建面（`core/builtins.cc` + `core/framework_builtins.cc`）是多轮考卷缺口
修补长出来的"整类 C++ 化"形态。本节按 ART/libcore 真实分层模型一次性定死
边界：新增/扩充任何内建面必须按 31.6 checklist 过审，避免再犯。

### 31.1 混合模型：ART 的三层现实

ART 上"基础类"从来不是整类 VM 内建，而是三层混合：

1. **mirror 布局**（`runtime/mirror/*`）：对象布局的 C++ 视图与字段契约；
2. **native 方法面**（`runtime/native/java_lang_*.cc`、`dalvik_system_*.cc`）：
   Java 类声明的 native 方法的 VM 实现；
3. **Java 逻辑本体**（libcore）：方法实现以 Java 源码编进 boot classpath，
   在解释器/JIT 上执行。

对照例子（快照 `reference/art`）：

| 类 | mirror/布局 | native 面（java_lang_*.cc） | Java 本体（libcore） |
| --- | --- | --- | --- |
| Object | mirror/object.h 头部 | identityHashCodeNative、internalClone、notify、notifyAll、wait（5 个） | equals/toString 琐碎本体 |
| String | mirror/string.h（count_+hash_code_+内联 value_） | charAt/compareTo/concat/doRepeat/doReplace/fastSubstring/getCharsNoCheck/fillBytes*/intern/toCharArray（11 个） | split/format/equals/regionMatches 等全部算法本体 |
| Thread | peer/字段槽契约 | nativeCreate/currentThread/sleep/yield0/holdsLock/interrupt0/isInterrupted/interrupted/setPriority0/setNativeName 等（18 个） | start/join/interrupt()/UEH/构造器/ThreadLocal 接线本体 |
| Class | mirror/class.h | classForName/getDeclared*Internal/newInstance/getPrimitiveClass 等（38 个） | getDeclaredMethod 等包装本体 |
| Throwable | detailMessage/cause/stackTrace 布局 | nativeFillInStackTrace/nativeGetStackTrace（2 个） | initCause/printStackTrace/getStackTrace 本体 |
| System | in/out/err 接线 | arraycopy 族（9 个） | getProperty/getenv/lineSeparator 本体 |
| StringFactory | AllocString | newStringFromBytes/Chars/String/Utf16/Utf8（5 个） | —（纯 native 类） |

结论判据：**VM 侧只允许三种存在**——① mirror 布局类；② native 方法面；
③ 选择性热 intrinsic（实现 ≲100 行才做，等价 @IntrinsicCandidate）。
**Java 逻辑本体一律在 lib**（runtime-libs vendored 源 → runtime.dex）；即使
VM 必需类（Thread/System/ClassLoader/Runtime）也是**混合形态**：lib 的 Java
类 + VM 提供 native 方法，而不是整类 C++ 化。

### 31.2 四象限判定

| 象限 | 判据 | 处置 |
| --- | --- | --- |
| Q1 mirror/VM 接线 | 对象布局由 VM 直接分配/解析（AllocString/ThrowNew/AllocClass、反射镜像指针、JNI ID），或 VM 状态被直接读写 | 布局与接线留 VM；类定义能拆则拆 |
| Q2 native 方法面 | 语义必须触达 VM 内部（线程/monitor/类加载/GC/解释器/JNI/调度） | 方法实现留 VM（等价 java_lang_*.cc）；Java 声明与其余方法在 lib |
| Q3 热 intrinsic | 高频且实现 ≲100 行（arraycopy/charAt/CAS…） | 可留 VM 直调（VMethod::native_fn）；否则不留 |
| Q4 纯 Java 逻辑 | 不触达 VM 内部状态的算法/数据结构/协议 | 一律 lib（vendored 源放行），VM 不得内建 |

判定流程：新类默认 Q4（lib）→ 触达 VM 状态才升 Q2 → VM 直接分配对象才升
Q1 → 只有"简单且热"才追加 Q3。整类内建只允许两种例外：

- **镜像同体类**：java.lang.Class（VClass 即类对象；拆分=重构对象模型，收益低）；
- **Java 本体≈无 的壳**：异常族构造器链、Object 琐碎方法。

### 31.3 混合拆分流程（VM 必需类的标准形态）

对 VM 必需类的处置 = **拆分**，不是整类内建/补全：

1. **下沉**：Java 逻辑迁 lib（放行对应 vendored 源；无源则补源码改写），类
   定义由 runtime.dex 提供；
2. **残留**：VM 内建收缩为该类的 native 方法面（对照 ART 同名
   java_lang_*.cc 清单）+ mirror 布局契约；
3. **绑定**：拆分前置工程——类链接期按"类名.方法名→IntrinsicFn"清单把 C++
   实现绑到 dex 类的 native 方法（VMethod::native_fn 槽）。当前仅
   AddIntrinsicMethod 对 builtin 类生效，需补 dex 类绑定面（等价 ART
   RegisterNativeMethods/runtime-native 注册）；
4. **布局契约**：lib 类源码字段声明必须与 VM 布局一致（ART mirror ↔ libcore
   字段约定）；String/StringBuilder 等需 VString 布局适配（见 31.5）；
5. **翻转**：拆分完成同一提交内原子变更——删 builtin 注册、runtime-components
   记录同步（打包排除集随 `builtin_classes.def` 自动收缩）。

### 31.4 打包排除集口径（自动派生，无手工清单）

旧口径（v1 "24 类 VM 内建不打包"、allowlist / jre-excluded.txt）已失效。
当前实现（`runtime-libs/build.gradle.kts`）：

- 打包排除集 = **`core/builtin_classes.def` 的类描述符全集**（构建期由
  X 宏清单自动派生；`name` 前缀规则覆盖内部类）。该文件是内建类集的唯一
  权威表达，`builtins.cc` / `framework_builtins.cc` 注册面与
  `tools/runtime_coverage_tests.sh` 同源读取并做一致性断言。
- `runtime-components.json` 退化为纯元数据（组件/版本/外部依赖记录）；
  打包集合不再由它决定，`externalClasses` 仅用于第三方 jar 提取口径。
- VM 必需（拆分）类在拆分完成前登记为 builtin（自动排除，防遮蔽死副本），
  拆分完成随 31.3 第 5 步删除 builtin 注册，排除集自动收缩。
- v1 名单中未内建/未完整内建的类（reflect.Proxy、InvocationHandler、
  LockSupport、FileDescriptor、InputStream、FileInputStream、
  FileOutputStream、Socket、ServerSocket、Process、ProcessBuilder、
  sun.misc.Unsafe 等）按 C 档处理：类定义由 lib 承担；其 VM 语义
  （文件 IO/网络/park/进程）以 Q2 native 面补进 lib 类，不再整类内建。

### 31.5 迁移注意（布局耦合点）

- **VString**（length+内联 uint16 chars）：lib String.java 需按 mirror 布局
  适配（AOSP 形态：字符访问全部走 native 面 charAt/getCharsNoCheck/
  fastSubstring/…）；OpenJDK 形态（byte[] value+coder+StringLatin1/UTF16）
  直接放行会字段错位。VString 无 hash_code_ 槽，需扩布局或无缓存适配。
  StringBuilder（SBState）与 StringFactory/`<init>(StringBuilder)` 接缝同理。
- **Thread 字段槽**（name/priority/group/target/tid/daemon、threadLocals@48、
  inheritableThreadLocals@56）是 lib ThreadLocal 的读写契约；拆分时
  Thread.java 字段声明保持 offset 一致（ART thread.cc 同款契约）。
- **反射镜像**（artMethod/artField@0、declaringClass@8、accessible@16）是
  jni/reflection.cc 的指针契约；reflect.* 拆分时字段布局不得变动。
- **遮蔽语义**（docs 03 章 8.4）：class_table_ 优先、无方法级合并，builtin
  全类遮蔽 dex 同名类；"builtin 一份、lib 一份"= dex 副本死代码 + 语义漂移，
  禁止长期并存。

### 31.6 防再犯：内建准入 checklist

新增/扩充 VM 内建面必须在评审里回答：

1. 该类/方法的 Java 本体为什么不放 lib？（只有 Q1/Q2/Q3 判据成立才准内建）
2. 对照 ART：对应 java_lang_*.cc 的哪个 native？多出来的部分是不是 Q4？
3. 布局契约声明了吗（vm_types.h ↔ lib 源字段 offset 表）？
4. `builtin_classes.def` 注册改了吗？（打包排除集自动派生；删注册即收缩，
   31.3 第 5 步）
5. 有双实现吗（builtin 与 runtime-components 同名类并存 = 构建该失败）？

### 31.7 分界定稿（lib 优先，2026-01-16）

用户口径定稿：**默认 lib，native 是例外**——「别老想着 native 实现，有时候 lib 更方便」
「复杂类 native 实现太麻烦」。凡新需求先问 lib 能不能做；复杂类禁止 native 实现。
最终分界三档：

**A 档：native 必留（VM 内建，仅三类）**

| 类别 | 类 | 理由 |
| --- | --- | --- |
| Mirror 布局类 | Object、Class、String(VString)、Throwable+异常壳（抛出面）、ref.Reference 族 | 对象布局即 VM 数据结构 |
| VM 接缝类 | ClassLoader 全家/DexFile、reflect.{Method,Constructor,Field}、Thread（GuestTask 槽）、System/Runtime、Unsafe、VMRuntime、Enum | 行为必须钩 VM 内部状态 |
| 简单热路径 | arraycopy 族、Float/Double bit-cast、identityHashCode | 实现 <~100 行且热 |

**B 档：framework 钩 VM（内建只留钩子）**：ActivityThread/Instrumentation（生命
周期泵）、Looper/Handler/MessageQueue（消息泵缝）、View/render 桥（draw 缝）、
Build 身份、Context 服务桥。其接口/POJO/纯逻辑部分（SharedPreferences、
Intent 数据面、Bundle 等）一律 lib Java 源。

**C 档：lib（默认档，其余全部）**：java.*/javax.*/jdk.* 全量、framework 接口/
POJO/纯逻辑、以及**目标态迁移**（P2 拆分）——String 复杂面（format/split/regex）、
StringBuilder、Thread Java 面（start/join/UEH）、Throwable Java 面（cause/
printStackTrace）、reflect Java 面下沉为「lib Java + native 缝」（ART 形态，
java_lang_*.cc 同构），native 侧随之收缩。

**新增类判定流程（防再犯）**：
1. lib 能做吗？→ 能就 lib（默认答案）。
2. 需要 VM 内部状态/布局吗？→ 否 → lib。
3. 热路径且实现简单？→ 是 → native intrinsic；否 → lib。
4. 复杂类（方法面多/语义重）→ 永远 lib。

## 32. UI 兼容批次落地清单（as-built，2026-11-20）

按 31.7「C 档：lib 默认」落地 UI 兼容批次：widget 语义在 runtime-libs
（runtime.dex），出画/交互在宿主物化桥。验证设备 = MuMu 模拟器，
考卷 = `target-app`（`com.coke.target` Gallery 画廊）。

### 32.1 runtime-libs 新增/扩展（guest boot classpath）

| 类 | 形态 | 语义面 |
| --- | --- | --- |
| `android.widget.Toolbar` | lib（extends FrameLayout） | 标题/子标题/导航点击/`MenuBuilder` 菜单；宿主真 Toolbar 物化 |
| `android.widget.AutoCompleteTextView` | lib（extends EditText） | setAdapter 建议项镜像、threshold、showDropDown/dismissDropDown |
| `android.widget.DatePicker` / `TimePicker` | lib | 年月日/时分状态 + OnDateChanged/OnTimeChanged，`onHostFieldsChanged` 回灌 |
| `android.widget.PopupMenu` | lib | MenuBuilder 菜单 + OnMenuItemClickListener/OnDismissListener；`VmLayoutHost.showPopupMenu` 宿主真弹层 |
| `android.widget.PopupWindow` | lib | showAsDropDown/showAtLocation（父链绝对坐标）、dismiss、OnDismissListener；叠加根经 `kPropWindowPopupToken` |
| `androidx.drawerlayout.widget.DrawerLayout` | lib 垫片 | 开合状态 + DrawerListener；宿主真 androidx DrawerLayout 手势/动画 |
| `android.view.MenuBuilder` | lib | Menu/MenuItem 全语义 + `mTitleArray` 标题镜像 |
| `android.view.VmLayoutHost` | 编译脚手架 | VM 内建 intrinsic 入口（Dialog/窗口/弹层，方法体抛 UnsupportedOperationException） |

### 32.2 宿主物化映射（UiBridgeMaterializer.java）

```text
TextView/Button/ImageButton/EditText/AutoCompleteTextView/CheckBox/
RadioButton/ToggleButton/Switch/CheckedTextView/SeekBar/RatingBar/
ProgressBar/ImageView/ListView/GridView/Spinner/ScrollView/
LinearLayout/FrameLayout/RelativeLayout/VideoView
  -> 真 android.view.* 控件
Toolbar         -> 真 android.widget.Toolbar（标题/子标题/汉堡/菜单重建）
DrawerLayout    -> 真 androidx.drawerlayout.widget.DrawerLayout（elevation/手势）
DatePicker/TimePicker -> 真控件（字段双向同步 + 回声抑制）
Dialog          -> 多根叠加 wrapper + dim 遮罩
PopupWindow     -> 叠加根 + elevation 浮层 + 透明外点关闭层
```

### 32.3 交互回灌 API（JNI contract）

```text
nativeDispatchViewClick / ObjectClick / LongClick / Text / Seek / Rating /
Drawer / MenuClick / Fields
nativeDispatchPopupEvent(token, kind, index)
```

会话线程单槽派发；guest listener 经 `MarkClickListenerRoots` /
`MarkPopupRoots` 登记 GC 外部根；弹层 token -> guest 对象映射在 VM 内。

### 32.4 验收（设备侧，MuMu）

- DrawerLayout 手势/按钮开合双向同步；Toolbar 菜单点击回灌；
- ACTV 下拉持久显示（修复宿主 `fullScroll` 抢焦点）；EditText 键入回灌；
- Date/TimePicker 字段回灌；Dialog/AlertDialog/Toast；
- PopupMenu 点击 `Copy/Share/Delete` 回灌 + 外点关闭；PopupWindow 浮层
  外点关闭回灌。
- logcat tag：`DexVmUiBridge`、`DexVmDialog`、`DexVmPopup`、`DexVmToast`。

## 附录：语义对照源文件索引

```text
runtime/class_linker.cc        FindClass 3266 / DefineClass 3505 / SetupClass 3963
                               LoadClass 4491 / InsertClass 5105 / LinkClass 6646
libdexfile/dex/dex_file.h      Header 134-181；dex_file_structs.h 全套
runtime/interpreter/interpreter_common.cc
                               MoveToExceptionHandler 172 / DoCallCommon 1192
runtime/entrypoints/entrypoint_utils-inl.h  483-665
runtime/jni/jni_internal.cc    GetClassLoader 393 / FindClass 712 / RegisterNatives 2701
runtime/jni/java_vm_ext.cc     LoadNativeLibrary 952 / JNI_OnLoad 1124
runtime/native/dalvik_system_DexFile.cc     251/368/471/537
runtime/mirror/object.h        807-809；class.h；dex_cache.h 590-605
runtime/class_status.h         29-115
runtime/art_method.cc          FindCatchBlock 328-365
```
