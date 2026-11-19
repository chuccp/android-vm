package twtest;

import java.util.concurrent.locks.LockSupport;

/**
 * LockSupport.park/unpark 探针（jdk.internal.misc.Unsafe.park/unpark 协程
 * wait/wake 承接；AQS 链前置）。覆盖：park 阻塞到 unpark 放行、permit 语义
 * （先 unpark 后 park 立即返回）、parkNanos 超时返回、unpark(null/已死) no-op。
 */
public class ParkProbe {

    static volatile boolean unparked;

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        // 1) park 阻塞 -> unpark 放行
        Thread t = new Thread(new Runnable() {
            public void run() {
                LockSupport.park();
                unparked = true;
            }
        }, "parker");
        t.setDaemon(true);
        try {
            unparked = false;
            t.start();
            Thread.sleep(120);
            sb.append(check(!unparked, "park-blocked"));
            LockSupport.unpark(t);
            t.join(3000);
            sb.append(check(unparked, "park-unpark-wakes"));
        } catch (Throwable e) {
            sb.append("park-basic: FAIL ").append(e).append('\n');
        }

        // 2) permit 语义：先 unpark 后 park 立即返回
        try {
            LockSupport.unpark(Thread.currentThread());
            long t0 = System.currentTimeMillis();
            LockSupport.park();
            long dt = System.currentTimeMillis() - t0;
            sb.append(check(dt < 50, "park-permit-sticky"));
        } catch (Throwable e) {
            sb.append("park-permit: FAIL ").append(e).append('\n');
        }

        // 3) parkNanos 超时返回
        try {
            long t0 = System.currentTimeMillis();
            LockSupport.parkNanos(150L * 1000000L);
            long dt = System.currentTimeMillis() - t0;
            sb.append(check(dt >= 100, "park-nanos-timeout"));
        } catch (Throwable e) {
            sb.append("park-nanos: FAIL ").append(e).append('\n');
        }

        // 4) unpark(null) / unpark(已死线程) 安全 no-op
        try {
            LockSupport.unpark(null);
            LockSupport.unpark(t);
            sb.append(check(true, "park-unpark-safe"));
        } catch (Throwable e) {
            sb.append("park-unpark-safe: FAIL ").append(e).append('\n');
        }
        return sb.toString();
    }
}
