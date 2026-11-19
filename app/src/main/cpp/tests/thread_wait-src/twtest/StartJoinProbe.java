package twtest;

/**
 * Thread.start/join/isAlive 语义面探针（M4.4 GuestTask 1:1 接线）。
 * 覆盖：target 形 run()、子类覆盖 run() 虚分派、start 后 isAlive、join 完成语义、
 * 重复 start IllegalThreadStateException、未启动 join 直返、per-GuestTask 身份
 * （currentThread/getId/getName）、主线程身份恒活。
 */
public class StartJoinProbe {

    static volatile int counter;
    static volatile String workerName;
    static volatile long workerId;
    static volatile boolean workerAliveSeen;
    static volatile boolean workerIsMain;

    static class MyThread extends Thread {
        volatile int ran;

        public void run() {
            ran = 1;
        }
    }

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        Thread main = Thread.currentThread();
        final Thread mainRef = main;

        // 1) target 形线程：start -> run(target) -> join
        counter = 0;
        Thread t = new Thread(new Runnable() {
            public void run() {
                counter = 42;
                workerName = Thread.currentThread().getName();
                workerId = Thread.currentThread().getId();
                workerAliveSeen = Thread.currentThread().isAlive();
                workerIsMain = Thread.currentThread() == mainRef;
            }
        }, "worker-a");
        sb.append(check(!t.isAlive(), "startjoin-pre-notalive"));
        try {
            t.start();
            sb.append(check(t.isAlive(), "startjoin-alive-after-start"));
            t.join();
            sb.append(check(counter == 42, "startjoin-target-ran"));
            sb.append(check(!t.isAlive(), "startjoin-dead-after-join"));
            sb.append(check("worker-a".equals(workerName), "startjoin-thread-name"));
            sb.append(check(workerId != main.getId(), "startjoin-distinct-id"));
            sb.append(check(workerAliveSeen, "startjoin-self-alive"));
            sb.append(check(!workerIsMain, "startjoin-distinct-identity"));
        } catch (Throwable e) {
            sb.append("startjoin-basic: FAIL ").append(e).append('\n');
        }

        // 2) 子类覆盖 run()（FindInterfaceImpl 虚分派）
        MyThread mt = new MyThread();
        try {
            mt.start();
            mt.join();
            sb.append(check(mt.ran == 1, "startjoin-subclass-run"));
        } catch (Throwable e) {
            sb.append("startjoin-subclass: FAIL ").append(e).append('\n');
        }

        // 3) 重复 start -> IllegalThreadStateException
        boolean threw = false;
        try {
            mt.start();
        } catch (IllegalThreadStateException e) {
            threw = true;
        } catch (Throwable e) {
            sb.append("startjoin-double-start: FAIL wrong exception ").append(e).append('\n');
        }
        sb.append(check(threw, "startjoin-double-start"));

        // 4) 未启动线程 join 直返；当前线程恒活 + 身份稳定
        Thread never = new Thread(new Runnable() {
            public void run() {
            }
        }, "never");
        try {
            never.join(50);
            sb.append(check(true, "startjoin-join-unstarted"));
        } catch (Throwable e) {
            sb.append("startjoin-join-unstarted: FAIL ").append(e).append('\n');
        }
        sb.append(check(main.isAlive(), "startjoin-main-alive"));
        sb.append(check(main == Thread.currentThread(), "startjoin-main-identity"));
        sb.append(check("main".equals(main.getName()), "startjoin-main-name"));

        // 5) join(long) 到期返回（线程仍存活 -> 静默返回，Java 语义）
        Thread slow = new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(600);
                } catch (InterruptedException e) {
                }
            }
        }, "slow");
        slow.setDaemon(true);
        try {
            slow.start();
            long t0 = System.currentTimeMillis();
            slow.join(120);
            long dt = System.currentTimeMillis() - t0;
            sb.append(check(slow.isAlive() && dt >= 80, "startjoin-join-timeout"));
            slow.interrupt();
            slow.join(3000);
            sb.append(check(!slow.isAlive(), "startjoin-join-cleanup"));
        } catch (Throwable e) {
            sb.append("startjoin-join-timeout: FAIL ").append(e).append('\n');
        }
        return sb.toString();
    }
}
