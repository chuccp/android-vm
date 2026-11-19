// target-app 考卷 8 轮编排——哨兵契约共享表（宿主 target_exam_smoke 与
// 设备 runtime_contract NativeRunTargetExam 共用）。
//
// 分发契约（target-app/README）：没有 in-APK 注册表——按 (owner 描述符,
// 方法名, JVM 签名) 选择 fixture 方法并断言其返回的哨兵 int。
// 8 轮考面（R1-R8）与哨兵值（fixture 源码语义推导；VM 少实现一个语义即错值）：
//   R1 VM 语义：TargetEntry/RuntimeCapabilities=283/Collections/Suspending*
//              + 91-115 族（JSON/UUID/加密/数学/随机/时间区域/Java 库面）
//   R2 反射/Unsafe/注解泛型：93/94/109/112/113
//   R3 并发/协程：RuntimeConcurrency=75/RuntimeModernConcurrency（位集哨兵）/
//              MonitorTask（直跑线）/ 协程 301-305
//   R4 I/O·网络：FileTask/IoTask/Socket/Http 直跑线 + mappedBuffer=110
//   R5 三方库：JSON 401-404 → netty 405-410 → Rx 411-413 → netty 服务 601-623
//   R6 Android 框架：Intent=98/PackageManager=103/Binder=42/lifecycle token
//   R7 资源/媒体/首帧：布局膨胀/限定符/媒体/桥接出画（设备桥轨断言）
//   R8 加固 .so JNI：libhardened_fixture/payload 依赖链 dlopen + a-g（设备）
//
// 纪律（docs 14 章 §31.7）：零特判——表只记契约期望值，缺口一律按 Android
// 语义补内置/解释器/链接器/桥接，不为 fixture 类开洞。
#pragma once

#include <cstdint>

namespace dexvm::tests {

struct TargetExamProbe {
  int round;               // 1..8
  const char* owner;       // 类描述符
  const char* method;
  const char* p0;          // 参数描述符（最多 2 个 int/引用槽；nullptr=无）
  const char* p1;
  int32_t a0;              // int 形参值（p0/p1 == "I" 时生效）
  int32_t a1;
  const char* ret;         // 返回类型描述符（"I"）
  bool is_static;
  int32_t expect;          // 哨兵 int
  const char* note;
};

// ---- R1 VM 语义（21 探针）----
inline constexpr TargetExamProbe kTargetExamProbes[] = {
    // TargetEntry：<clinit> 计数 + DEX 形状；run()=42
    {1, "Lcom/coke/target/TargetEntry;", "run", nullptr, nullptr, 0, 0, "I", true, 42,
     "clinit/entry"},
    // RuntimeCapabilities：DEX 形状大矩阵；run()=283
    {1, "Lcom/coke/target/RuntimeCapabilities;", "run", nullptr, nullptr, 0, 0, "I", true, 283,
     "dex-shape matrix"},
    // RuntimeCollections：集合/原子/arraycopy/sort
    {1, "Lcom/coke/target/RuntimeCollections;", "mapFlow", nullptr, nullptr, 0, 0, "I", true, 15,
     "HashMap"},
    {1, "Lcom/coke/target/RuntimeCollections;", "listLengthFlow", nullptr, nullptr, 0, 0, "I",
     true, 9, "List length"},
    {1, "Lcom/coke/target/RuntimeCollections;", "listPrefixFlow", nullptr, nullptr, 0, 0, "I",
     true, 16, "List prefix"},
    {1, "Lcom/coke/target/RuntimeCollections;", "concurrentFlow", nullptr, nullptr, 0, 0, "I",
     true, 32, "ConcurrentHashMap"},
    {1, "Lcom/coke/target/RuntimeCollections;", "atomicFlow", nullptr, nullptr, 0, 0, "I", true,
     192, "Atomic CAS + System.arraycopy"},
    {1, "Lcom/coke/target/RuntimeCollections;", "arrayFlow", nullptr, nullptr, 0, 0, "I", true, 5,
     "Arrays.sort"},
    // 挂起式 <clinit>（协作调度保返回值）
    {1, "Lcom/coke/target/RuntimeSuspendingInitFixture;", "flow", nullptr, nullptr, 0, 0, "I",
     false, 107, "suspending clinit"},
    {1, "Lcom/coke/target/RuntimeSuspendingEntryFixture;", "flow", nullptr, nullptr, 0, 0, "I",
     false, 108, "suspending entry"},
    // 91-115 族（R2/R4 抽走 93/94/109/112/113/110）
    {1, "Lcom/coke/target/RuntimePlatformMigrationFixture;", "uuidAndPropertiesFlow", nullptr,
     nullptr, 0, 0, "I", false, 91, "org.json + UUID"},
    {1, "Lcom/coke/target/RuntimePlatformMigrationFixture;", "jsonFlow", nullptr, nullptr, 0, 0,
     "I", false, 92, "org.json"},
    {1, "Lcom/coke/target/RuntimeDigestFixture;", "digestFlow", nullptr, nullptr, 0, 0, "I", false,
     95, "SHA-1/SHA-256/MD5"},
    {1, "Lcom/coke/target/RuntimeBigIntegerFixture;", "bigIntegerFlow", nullptr, nullptr, 0, 0,
     "I", false, 96, "BigInteger"},
    {1, "Lcom/coke/target/RuntimeRandomFixture;", "randomFlow", nullptr, nullptr, 0, 0, "I", false,
     97, "Random/SecureRandom"},
    {1, "Lcom/coke/target/RuntimeTimeLocaleFixture;", "timeLocaleFlow", nullptr, nullptr, 0, 0,
     "I", false, 99, "Locale/SimpleDateFormat/Date"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "regexFlow", nullptr, nullptr, 0, 0,
     "I", false, 101, "java.util.regex"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "stringReaderWriterFlow", nullptr,
     nullptr, 0, 0, "I", false, 102, "StringReader/Writer"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "localDateFlow", nullptr, nullptr,
     0, 0, "I", false, 104, "java.time"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "calendarFlow", nullptr, nullptr, 0,
     0, "I", false, 105, "Calendar"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "mapFlow", nullptr, nullptr, 0, 0,
     "I", false, 106, "map family"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "delayedQueueFlow", nullptr,
     nullptr, 0, 0, "I", false, 111, "DelayedQueue"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "androidValueLibraryFlow", nullptr,
     nullptr, 0, 0, "I", false, 114, "android value libs"},
    {1, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "valueUtilitiesFlow", nullptr,
     nullptr, 0, 0, "I", false, 115, "Objects/Optional/value utils"},

    // ---- R2 反射/Unsafe/注解泛型（5 探针）----
    {2, "Lcom/coke/target/RuntimeReflectionFixture;", "reflectionFlow", nullptr, nullptr, 0, 0,
     "I", false, 93, "forName/invoke/setAccessible"},
    {2, "Lcom/coke/target/RuntimeUnsafeFixture;", "unsafeFlow", nullptr, nullptr, 0, 0, "I",
     false, 94, "sun.misc.Unsafe CAS/allocateInstance"},
    {2, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "arrayAndGenericReflectionFlow",
     nullptr, nullptr, 0, 0, "I", false, 109, "generic reflection"},
    {2, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "methodAnnotationFlow", nullptr,
     nullptr, 0, 0, "I", false, 112, "annotations"},
    {2, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "objectsAndModifierFlow", nullptr,
     nullptr, 0, 0, "I", false, 113, "Objects/Modifier"},

    // ---- R3 并发/协程（19 探针；MonitorTask 直跑线另计）----
    // run()=7+7+7+3+3+48（sleep/lock/monitor/park/join/manySleepers 位集和）
    {3, "Lcom/coke/target/RuntimeConcurrency;", "run", nullptr, nullptr, 0, 0, "I", false, 75,
     "sleep/lock/monitor/park/join/sleepers"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "interruptFlow", nullptr, nullptr, 0, 0,
     "I", false, 1215, "interrupt matrix (1+2+4+8+16+992+64+128)"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "timeoutFlow", nullptr, nullptr, 0, 0, "I",
     false, 7, "tryLock/join timeouts + fair"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "conditionFlow", nullptr, nullptr, 0, 0,
     "I", false, 1, "Condition await/signalAll"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "synchronizerFlow", nullptr, nullptr, 0, 0,
     "I", false, 63, "Semaphore/Latch (1+2+4+8+16+32)"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "barrierPhaserFlow", nullptr, nullptr, 0, 0,
     "I", false, 3, "CyclicBarrier/Phaser"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "futureFlow", nullptr, nullptr, 0, 0, "I",
     false, 63, "Future/cancel/ExecutionException (1+2+4+16+32+8)"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "concurrentMapFlow", nullptr, nullptr, 0, 0,
     "I", false, 7, "ConcurrentHashMap/EnumMap/Spliterator"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "ioTimeoutFlow", nullptr, nullptr, 0, 0,
     "I", false, 1, "ServerSocket SO_TIMEOUT"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "blockingQueueFlow", nullptr, nullptr, 0, 0,
     "I", false, 127, "BlockingQueue family (1+2+4+8+16+32+64)"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "completableFutureFlow", nullptr, nullptr, 0,
     0, "I", false, 8191, "CompletableFuture 13 位全集"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "advancedPhaserFlow", nullptr, nullptr, 0, 0,
     "I", false, 15, "Phaser terminate/timeout/interrupt/cyclic"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "advancedSynchronizerFlow", nullptr, nullptr,
     0, 0, "I", false, 15, "SynchronousQueue+fair+ThreadGroup"},
    {3, "Lcom/coke/target/RuntimeModernConcurrency;", "synchronousQueueFlow", nullptr, nullptr, 0,
     0, "I", false, 3, "SynchronousQueue handoff"},
    {3, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "structuredConcurrencyFlow",
     nullptr, nullptr, 0, 0, "I", false, 301, "runBlocking/async/launch"},
    {3, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "channelFlow", nullptr, nullptr, 0,
     0, "I", false, 302, "Channel"},
    {3, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "flowPipelineFlow", nullptr,
     nullptr, 0, 0, "I", false, 303, "flow pipe/reduce/cancel"},
    {3, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "timeoutAndCancellationFlow",
     nullptr, nullptr, 0, 0, "I", false, 304, "withTimeoutOrNull"},
    {3, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "supervisorIsolationFlow", nullptr,
     nullptr, 0, 0, "I", false, 305, "supervisorScope"},

    // ---- R4 I/O·网络（哨兵面：mmap；File/Io/Socket/Http 直跑线另计）----
    {4, "Lcom/coke/target/RuntimeLibraryCompatibilityFixture;", "mappedBufferFlow", nullptr,
     nullptr, 0, 0, "I", false, 110, "mmap MappedByteBuffer"},

    // ---- R5 三方库（16 探针）----
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "thirdPartyLibrariesFlow", nullptr,
     nullptr, 0, 0, "I", false, 401, "jackson+gson+fastjson-face+okio+commons-lang3"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "jacksonInitializationFlow",
     nullptr, nullptr, 0, 0, "I", false, 402, "Separators/DefaultPrettyPrinter init"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "fastJsonFlow", nullptr, nullptr, 0,
     0, "I", false, 403, "fastjson 1.2.83"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "fastJson2Flow", nullptr, nullptr,
     0, 0, "I", false, 404, "fastjson2 + JSONPath"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "nettyByteBufFlow", nullptr,
     nullptr, 0, 0, "I", false, 405, "ByteBuf read/write/copy/refCnt"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "nettyPromiseFlow", nullptr,
     nullptr, 0, 0, "I", false, 406, "DefaultPromise/listener"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "nettyEmbeddedChannelFlow",
     nullptr, nullptr, 0, 0, "I", false, 407, "EmbeddedChannel codec"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "nettyEventLoopFlow", nullptr,
     nullptr, 0, 0, "I", false, 408, "DefaultEventLoopGroup"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "nettyNioEventLoopFlow", nullptr,
     nullptr, 0, 0, "I", false, 409, "NioEventLoop submit"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "nettyNioTcpFlow", nullptr,
     nullptr, 0, 0, "I", false, 410, "NIO TCP echo"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "rxJavaObservableFlow", nullptr,
     nullptr, 0, 0, "I", false, 411, "RxJava3 Observable/Single/Maybe/Completable"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;",
     "rxJavaSchedulersAndBackpressureFlow", nullptr, nullptr, 0, 0, "I", false, 412,
     "Schedulers/backpressure"},
    {5, "Lcom/coke/target/RuntimeThirdPartyCoroutineFixture;", "rxJavaErrorAndCancellationFlow",
     nullptr, nullptr, 0, 0, "I", false, 413, "Rx error/cancel"},
    {5, "Lcom/coke/target/RuntimeNettyServerFixture;", "serverFlow", nullptr, nullptr, 0, 0, "I",
     true, 610, "NIO echo + custom codec (601-610)"},
    {5, "Lcom/coke/target/RuntimeNettyHttpServerFixture;", "httpServerFlow", nullptr, nullptr, 0,
     0, "I", true, 620, "HttpServerCodec service (DEXVM_HTTP_SECONDS)"},
    {5, "Lcom/coke/target/RuntimeNettyFastHttpServerFixture;", "fastHttpServerFlow", "I", "I", 0,
     1, "I", true, 622, "raw ByteBuf HTTP (port=0, seconds=1)"},

    // ---- R6 Android 框架（哨兵面：Intent；Binder/PM/lifecycle/服务直跑线另计）----
    {6, "Lcom/coke/target/RuntimeIntentFixture;", "intentFlow", nullptr, nullptr, 0, 0, "I",
     false, 98, "Intent/Bundle roundtrip"},
    {6, "Lcom/coke/target/RuntimeBinderFixture;", "roundTrip", nullptr, nullptr, 0, 0, "I", false,
     42, "Binder.transact 跨任务派发"},

    // ---- R8 加固 .so JNI（设备轮；真实 app 路径：guest System.loadLibrary ->
    //      VM LoadLibraryCommon -> dlopen -> JNI_OnLoad -> RegisterNatives a-g）----
    {8, "Lcom/coke/vm/HardenedNativeLoader;", "loadAndRegister", nullptr, nullptr, 0, 0, "I",
     true, 580, "System.loadLibrary payload+fixture + register()"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeMixed", nullptr, nullptr, 0, 0, "I", true,
     581, "a(7) cross-lib mix"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeDecrypted", nullptr, nullptr, 0, 0, "I",
     true, 582, "b() rolling-XOR string"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeSelfIntegrity", nullptr, nullptr, 0, 0, "I",
     true, 583, "c() dladdr + pid key"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeArray", nullptr, nullptr, 0, 0, "I", true,
     584, "d() byte[] copy + ThrowNew NPE"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeThrow", nullptr, nullptr, 0, 0, "I", true,
     585, "e() ThrowNew IllegalArgumentException"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeIntegrity", nullptr, nullptr, 0, 0, "I",
     true, 586, "f() cross-lib integrity"},
    {8, "Lcom/coke/vm/HardenedNativeFixture;", "probeCallback", nullptr, nullptr, 0, 0, "I", true,
     587, "g() JNI upcall 0x4f21"},
};

inline constexpr int kTargetExamProbeCount =
    static_cast<int>(sizeof(kTargetExamProbes) / sizeof(kTargetExamProbes[0]));

inline constexpr const char* kRoundNames[9] = {
    "", "R1 vm-semantics", "R2 reflection-unsafe", "R3 concurrency-coroutines",
    "R4 io-network", "R5 third-party", "R6 android-framework",
    "R7 resources-media-firstframe", "R8 hardened-jni",
};

}  // namespace dexvm::tests
