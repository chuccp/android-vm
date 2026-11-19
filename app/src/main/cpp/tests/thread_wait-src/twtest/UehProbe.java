package twtest;

/**
 * UncaughtExceptionHandler 最小骨架探针（M4.4）。覆盖：实例 UEH 收到
 * uncaughtException(Thread, Throwable)、默认 UEH set/get 往返、默认 UEH
 * 承接未捕获异常、异常后线程死亡。
 */
public class UehProbe {

    static volatile boolean handlerSaw;
    static volatile boolean handlerRight;

    static class H implements Thread.UncaughtExceptionHandler {
        public void uncaughtException(Thread t, Throwable e) {
            handlerRight = (t != null && e instanceof RuntimeException);
            handlerSaw = true;
        }
    }

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        // 1) 实例 UEH
        try {
            handlerSaw = false;
            handlerRight = false;
            Thread t = new Thread(new Runnable() {
                public void run() {
                    throw new RuntimeException("boom");
                }
            }, "thrower");
            t.setDaemon(true);
            t.setUncaughtExceptionHandler(new H());
            t.start();
            t.join(3000);
            sb.append(check(handlerSaw, "ueh-handler-called"));
            sb.append(check(handlerRight, "ueh-handler-args"));
            sb.append(check(!t.isAlive(), "ueh-dead-after-uncaught"));
        } catch (Throwable e) {
            sb.append("ueh-instance: FAIL ").append(e).append('\n');
        }

        // 2) 默认 UEH：set/get 往返 + 承接
        try {
            sb.append(check(Thread.getDefaultUncaughtExceptionHandler() == null,
                    "ueh-default-null"));
            Thread.setDefaultUncaughtExceptionHandler(new H());
            sb.append(check(Thread.getDefaultUncaughtExceptionHandler() != null,
                    "ueh-default-set"));
            handlerSaw = false;
            handlerRight = false;
            Thread t2 = new Thread(new Runnable() {
                public void run() {
                    throw new IllegalStateException("boom2");
                }
            }, "thrower2");
            t2.setDaemon(true);
            t2.start();
            t2.join(3000);
            sb.append(check(handlerSaw && handlerRight, "ueh-default-called"));
            Thread.setDefaultUncaughtExceptionHandler(null);
        } catch (Throwable e) {
            sb.append("ueh-default: FAIL ").append(e).append('\n');
        }
        return sb.toString();
    }
}
