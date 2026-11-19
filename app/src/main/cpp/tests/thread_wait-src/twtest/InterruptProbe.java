package twtest;

/**
 * interrupt()/isInterrupted()/interrupted() 语义面探针（M4.4）。
 * 覆盖：sleep 被中断抛 InterruptedException、wait 被中断抛且重获锁、
 * interrupted() 清标志 vs isInterrupted() 保留、预置中断标志 sleep 立即抛、
 * join 被中断抛 InterruptedException。
 */
public class InterruptProbe {

    static volatile boolean sleepInterrupted;
    static volatile boolean waitInterrupted;
    static volatile boolean waitRestoredLock;

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        // 1) sleep 被 interrupt -> InterruptedException
        Thread t1 = new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(60000);
                } catch (InterruptedException e) {
                    sleepInterrupted = true;
                }
            }
        }, "sleeper");
        t1.setDaemon(true);
        try {
            sleepInterrupted = false;
            t1.start();
            Thread.sleep(120);
            t1.interrupt();
            t1.join(3000);
            sb.append(check(sleepInterrupted, "interrupt-sleep"));
            sb.append(check(!t1.isAlive(), "interrupt-sleep-dead"));
        } catch (Throwable e) {
            sb.append("interrupt-sleep: FAIL ").append(e).append('\n');
        }

        // 2) wait 被 interrupt -> InterruptedException + 重获 monitor
        final Object lock = new Object();
        Thread t2 = new Thread(new Runnable() {
            public void run() {
                synchronized (lock) {
                    try {
                        lock.wait();
                    } catch (InterruptedException e) {
                        waitInterrupted = true;
                        waitRestoredLock = Thread.holdsLock(lock);
                    }
                }
            }
        }, "waiter");
        t2.setDaemon(true);
        try {
            waitInterrupted = false;
            t2.start();
            Thread.sleep(120);
            t2.interrupt();
            t2.join(3000);
            sb.append(check(waitInterrupted, "interrupt-wait"));
            sb.append(check(waitRestoredLock, "interrupt-wait-lock-restored"));
        } catch (Throwable e) {
            sb.append("interrupt-wait: FAIL ").append(e).append('\n');
        }

        // 3) interrupted() 清标志 / isInterrupted() 保留
        try {
            Thread.currentThread().interrupt();
            boolean a = Thread.interrupted();
            boolean b = Thread.interrupted();
            sb.append(check(a && !b, "interrupt-interrupted-clears"));
            Thread.currentThread().interrupt();
            boolean c = Thread.currentThread().isInterrupted();
            boolean d = Thread.currentThread().isInterrupted();
            boolean e2 = Thread.interrupted();  // 清理
            sb.append(check(c && d && e2, "interrupt-isInterrupted-keeps"));
        } catch (Throwable x) {
            sb.append("interrupt-flag: FAIL ").append(x).append('\n');
        }

        // 4) 预置中断标志 -> sleep 立即抛 InterruptedException 且清标志
        try {
            Thread.currentThread().interrupt();
            boolean threw = false;
            try {
                Thread.sleep(10);
            } catch (InterruptedException e) {
                threw = true;
            }
            sb.append(check(threw, "interrupt-preset-sleep"));
            sb.append(check(!Thread.interrupted(), "interrupt-preset-cleared"));
        } catch (Throwable x) {
            sb.append("interrupt-preset: FAIL ").append(x).append('\n');
        }

        // 5) join 被 interrupt -> InterruptedException
        final Thread mainT = Thread.currentThread();
        Thread helper = new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(150);
                } catch (InterruptedException e) {
                }
                mainT.interrupt();
            }
        }, "interrupter");
        Thread sleeper = new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(60000);
                } catch (InterruptedException e) {
                }
            }
        }, "long-sleeper");
        helper.setDaemon(true);
        sleeper.setDaemon(true);
        try {
            helper.start();
            sleeper.start();
            boolean threw = false;
            try {
                sleeper.join(5000);
            } catch (InterruptedException e) {
                threw = true;
            }
            sb.append(check(threw, "interrupt-join"));
            sleeper.interrupt();
            helper.join(3000);
            sleeper.join(3000);
            sb.append(check(!sleeper.isAlive(), "interrupt-join-cleanup"));
        } catch (Throwable x) {
            sb.append("interrupt-join: FAIL ").append(x).append('\n');
        }
        return sb.toString();
    }
}
