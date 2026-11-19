package apitest;

/**
 * clinit 挂起/不重放四连（覆盖面参考启发自 v1 NativePlatformMigrationTest.kt
 * resumesSuspendingClassInitializer / doesNotReplaySynchronousClassInitializer /
 * cooperativeYieldPreservesPendingInvokeResult /
 * resumesEntryMethodAfterSuspendingClassInitializer 四例 + RuntimeSuspendingInit
 * Fixture 形态）。测试本体按 Android 语义判据重写：<clinit> 全程只执行一次
 * （即使中途阻塞/协作让出），静态初值在恢复后可见，入口调用结果不被挂起污染。
 * 经 RunGuestMain（调度器）执行，Thread.sleep 为真实挂起点。
 */
public final class ClinitChecks {
    private ClinitChecks() {}

    /** clinit 内阻塞（sleep 挂起）后赋值；恢复后初值必须可见。 */
    private static final class SuspendingHolder {
        static final String VALUE;

        static {
            try {
                Thread.sleep(1);
            } catch (InterruptedException failure) {
                Thread.currentThread().interrupt();
            }
            VALUE = "ready";
        }
    }

    /** 同步 clinit 带副作用计数；访问多次也不得重放。 */
    private static final class SyncHolder {
        static int touches;

        static final String VALUE = create();

        static String create() {
            touches++;
            return "sync";
        }
    }

    private static final class EntryHolder {
        static final String VALUE;

        static {
            try {
                Thread.sleep(1);
            } catch (InterruptedException failure) {
                Thread.currentThread().interrupt();
            }
            VALUE = "entry-ready";
        }
    }

    public static String checkSuspendingClinit() {
        return "ready".equals(SuspendingHolder.VALUE)
                ? "clinitSuspend:ok" : "clinitSuspend:fail";
    }

    public static String checkClinitNotReplayed() {
        String first = SyncHolder.VALUE;
        String second = SyncHolder.VALUE;
        int touches = SyncHolder.touches;
        return ("sync".equals(first) && "sync".equals(second) && touches == 1)
                ? "clinitOnce:ok" : "clinitOnce:fail:" + touches;
    }

    public static String checkCooperativeResult() {
        int result = longComputation() + 1;
        return result == 42 ? "yieldResult:ok" : "yieldResult:fail:" + result;
    }

    public static String checkEntryAfterSuspendingClinit() {
        // 入口方法所在类的 clinit 在本次入口调用中挂起并恢复
        return "entry-ready".equals(EntryHolder.VALUE)
                ? "entryClinit:ok" : "entryClinit:fail";
    }

    private static int longComputation() {
        int value = 0;
        for (int index = 0; index < 12_000; index++) {
            value = (value * 31 + index) & 0x7fffffff;
        }
        return value == 0 ? 39 : 41;
    }
}
