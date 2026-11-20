package com.dexvm.host;

/** libdexvm.so 的 JNI contract 面（与 jni/runtime_contract.cpp 对应）。 */
public final class NativeBridge {
    private NativeBridge() {}

    static {
        System.loadLibrary("dexvm");
    }

    public static native String nativeVmVersion();

    // ---- 字体桥 worker 通道（guest fiber 不直接调宿主 JNI；见 FontBridge）----
    /** 阻塞取下一条字体请求（无请求休眠）；返回请求 id，0=退出。 */
    public static native long nativeFontNextRequest();

    public static native int nativeFontOp(long id);

    public static native int nativeFontStyle(long id);

    public static native float nativeFontSize(long id);

    public static native long nativeFontBase(long id);

    public static native String nativeFontString(long id);

    public static native String nativeFontText(long id);

    public static native void nativeFontComplete(long id, long handle, float width,
                                                 int[] metrics, boolean metricsOk);

    /**
     * 宿主桥接值面：把系统服务真值快照（"k=v\n" 行编码，如 telephony.*）推入
     * VM；guest lib 类经 com.dexvm.runtime.Host.get(key) 读取（桥接优先口径）。
     */
    public static native void nativeSetHostValues(String encoded);

    /**
     * 宿主传感器桥：传感器描述表（每行 "type|name|vendor|version|maxRange|resolution|power|minDelay"）。
     * guest lib android.hardware.SensorManager 经 VmSensors.list() 读取。
     */
    public static native void nativeSensorSetList(String encoded);

    /** 宿主传感器桥：实时事件入 VM 队列（guest 主会话线程轮询派发）。 */
    public static native void nativeSensorPush(int type, float[] values, long timestamp);

    /**
     * 宿主通知桥：阻塞取一条 guest 通知请求（200ms 超时返回 null；小端线
     * 格式见 core/notification_bridge.h）。由 NotificationBridge 分发线程调用。
     */
    public static native byte[] nativeNotificationTake();

    /**
     * 宿主 Toast 桥：阻塞取一条 guest Toast 请求（200ms 超时返回 null；
     * 小端线格式见 core/toast_bridge.h）。由 ToastBridge 分发线程调用。
     */
    public static native byte[] nativeToastTake();

    /** 当前运行进度日志快照（VM/考卷线程写入；UI 轮询显示，线程安全）。 */
    public static native String nativeGetRunProgress();

    /** 清空进度日志（每次启动/跑卷前调用）。 */
    public static native void nativeClearRunProgress();

    /** M1 验收入口：引导 VM 并执行 fixture DEX，返回诊断串。 */
    public static native String nativeRunFixture(String dexPath);

    /** M4.1 设备化入口：引导 VM 跑 APK 全生命周期链 + 首帧管线，返回 "OK <phases> |log=<log> |viewtree=<n>" 或 "FAIL <error>"。 */
    public static native String nativeRunApkLifecycle(String runtimeDexPath, String apkPath);

    /**
     * 常驻会话入口：生命周期链 + 首帧后保持 VM 存活（消息泵/增量出画/点击
     * 回灌），直到 {@link #nativeSessionStop()}；返回最终 phase 报告（同
     * nativeRunApkLifecycle 形制）。
     */
    public static native String nativeRunApkSession(String runtimeDexPath, String apkPath);

    /**
     * 拉取会话帧队列：u32 count + 每帧 (u32 len + UIBridge envelope 字节)。
     * 无帧返回 null；宿主按序 apply（full 基线 + patch 增量）。
     */
    public static native byte[] nativeSessionPollFrames();

    /**
     * AOSP starting window（首帧前预览层）：VM 按 launcher Activity 主题解析
     * android:windowBackground（activity theme 优先、app theme 兜底）。
     * 协议：u32 bgColor / u32 gravity / u32 bitmapLen / bitmap bytes（LE）；
     * 未就绪或无 windowBackground 返回 null。
     */
    public static native byte[] nativeGetStartingWindow();

    /** 请求结束常驻会话（会话线程退出后 nativeRunApkSession 返回）。 */
    public static native void nativeSessionStop();

    /** 会话是否存活（首帧后 active=true，结束/失败 false）。 */
    public static native boolean nativeSessionIsRunning();

    /** 调试：guest 方法入口 trace（类 descriptor 子串，逗号分隔；空串关闭）。 */
    public static native void nativeSetGuestTrace(String patterns);

    /** 会话窗口逻辑尺寸（启动前设置；宽度通常 540，高度按宿主内容区比例）。 */
    public static native void nativeSetGuestWindowSize(int width, int height);

    /**
     * UI 桥接 P1：取桥轨 View 树 envelope（ViewSnapshot/ViewPatch + draw 命令流，
     * 小端线格式见 bridge/ui_bridge_protocol.h）。宿主物化器（UiBridgeMaterializer）
     * 解析后物化真 android.view.View 树（HWUI 出画）。无快照返回 null。
     * （符号按 JNI 命名约定由 bridge/ui_bridge_jni.cc 导出。）
     */
    public static native byte[] nativeGetViewSnapshot();

    /**
     * UI 桥接 P1：点击回灌通路（host 点击 -> viewId -> guest listener 注入）。
     * 返回通路状态串（"dispatched viewId=N listener=…" / "channel-ok viewId=N
     * guest-session=absent" 等；见 bridge/ui_bridge_input.h）。
     */
    public static native String nativeDispatchViewClick(int viewId);

    /**
     * UI 桥接：按快照 handle.object_id 的点击回灌（推荐路径）。object_id 是
     * guest View 身份（与 mId 无关），无 id 的视图（mId==0）也能精确寻址。
     */
    public static native String nativeDispatchViewObjectClick(int objectId);

    /**
     * M3 加固考卷设备化：exam_smoke 同款引导 + LifecycleDriver 驱动 + 逐 phase/缺口
     * 诊断；System.loadLibrary 设备钩子接真 loader（APK 内 lib/&lt;abi&gt;/*.so 解压到
     * files/nativeLibDir -> dlopen -> JNI_OnLoad(VM JNIEnvExt) -> RegisterNatives 生效）。
     * 返回结构化报告：phase 序列、停点、缺口清单（分类+guest 栈）、native 库执行结果。
     */
    public static native String nativeRunExam(String runtimeDexPath, String apkPath);

    /**
     * target-app 考卷 8 轮（R1 VM 语义 → R8 加固 .so JNI）：
     * tests/target_exam_table.h 哨兵契约表 + R4 直跑线任务 + R6 生命周期链 +
     * R7 桥接出画 + R8 真实加载路径（guest System.loadLibrary）。
     * roundSel: "all" / "1,3" / "R2"。返回结构化报告（逐轮 [Rn] PASS/FAIL）。
     */
    public static native String nativeRunTargetExam(String runtimeDexPath, String apkPath,
            String roundSel);

    // ---- guest TLS -> 宿主 TLS 栈桥接（TlsBridge.java 消费；线格式见 core/tls_bridge.h）----

    /** 阻塞取一条 guest TLS 请求；返回 null = 桥接退出。 */
    public static native byte[] nativeTlsTake();

    /**
     * 投递 TLS 请求结果（宿主 TLS 线程调用）：
     * id = 请求 id；status = core/tls_bridge.h 状态；handle = OPEN 成功连接句柄；
     * data = 读数据/OPEN 元数据/错误诊断。
     */
    public static native void nativeTlsReply(long id, int status, long handle, byte[] data);

    /**
     * 设备 TLS 桥接自检：引导 runtime.dex 并调用 guest
     * {@code com.dexvm.runtime.TlsSelfTest.run(host,443)}，走
     * TrustManagerFactory/SSLContext/createSocket(wrap) -> 宿主 TlsBridge 全链路。
     */
    public static native String nativeRunTlsSelfTest(String runtimeDexPath, String host);
}
