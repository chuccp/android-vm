package twtest;

/**
 * Object.wait/notify/notifyAll 语义面探针（docs 04 章 14.8 monitor waiter list）。
 * 覆盖：wait/notify 配对、wait(long) deadline 到期静默返回、notifyAll 全唤醒、
 * 无锁 wait/notify 抛 IllegalMonitorStateException、嵌套 synchronized 重入计数
 * 经 wait 释放并恢复。
 */
public class WaitNotifyProbe {

    static final Object lock = new Object();
    static volatile int stage;
    static volatile int observed;

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        // 1) wait/notify 基本面（while 谓词防虚假唤醒）
        stage = 0;
        observed = 0;
        Thread waiter = new Thread(new Runnable() {
            public void run() {
                synchronized (lock) {
                    try {
                        while (stage == 0) {
                            lock.wait();
                        }
                        observed = stage;
                    } catch (InterruptedException e) {
                    }
                }
            }
        }, "waiter");
        waiter.setDaemon(true);
        try {
            waiter.start();
            Thread.sleep(120);
            synchronized (lock) {
                stage = 7;
                lock.notify();
            }
            waiter.join(3000);
            sb.append(check(observed == 7, "waitnotify-basic"));
        } catch (Throwable e) {
            sb.append("waitnotify-basic: FAIL ").append(e).append('\n');
        }

        // 2) wait(long) deadline 到期静默返回
        try {
            long t0 = System.currentTimeMillis();
            synchronized (lock) {
                lock.wait(150);
            }
            long dt = System.currentTimeMillis() - t0;
            sb.append(check(dt >= 100, "waitnotify-timeout"));
        } catch (Throwable e) {
            sb.append("waitnotify-timeout: FAIL ").append(e).append('\n');
        }

        // 3) notifyAll 全唤醒（两个 waiter）
        final Object lock3 = new Object();
        final boolean[] flags = new boolean[2];
        Thread w1 = new Thread(new Runnable() {
            public void run() {
                synchronized (lock3) {
                    try {
                        lock3.wait(3000);
                        flags[0] = true;
                    } catch (InterruptedException e) {
                    }
                }
            }
        }, "w1");
        Thread w2 = new Thread(new Runnable() {
            public void run() {
                synchronized (lock3) {
                    try {
                        lock3.wait(3000);
                        flags[1] = true;
                    } catch (InterruptedException e) {
                    }
                }
            }
        }, "w2");
        w1.setDaemon(true);
        w2.setDaemon(true);
        try {
            w1.start();
            w2.start();
            Thread.sleep(150);
            synchronized (lock3) {
                lock3.notifyAll();
            }
            w1.join(3000);
            w2.join(3000);
            sb.append(check(flags[0] && flags[1], "waitnotify-all"));
        } catch (Throwable e) {
            sb.append("waitnotify-all: FAIL ").append(e).append('\n');
        }

        // 4) 无 monitor 调用 -> IllegalMonitorStateException
        boolean imseWait = false;
        boolean imseNotify = false;
        boolean imseNotifyAll = false;
        try {
            lock.wait(1);
        } catch (IllegalMonitorStateException e) {
            imseWait = true;
        } catch (Throwable e) {
        }
        try {
            lock.notify();
        } catch (IllegalMonitorStateException e) {
            imseNotify = true;
        } catch (Throwable e) {
        }
        try {
            lock.notifyAll();
        } catch (IllegalMonitorStateException e) {
            imseNotifyAll = true;
        } catch (Throwable e) {
        }
        sb.append(check(imseWait, "waitnotify-imse-wait"));
        sb.append(check(imseNotify, "waitnotify-imse-notify"));
        sb.append(check(imseNotifyAll, "waitnotify-imse-notifyAll"));

        // 5) 嵌套 synchronized 重入计数经 wait 释放/恢复（退出不得抛 IMSE）
        final Object lock5 = new Object();
        Thread nt = new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(150);
                } catch (InterruptedException e) {
                }
                synchronized (lock5) {
                    lock5.notify();
                }
            }
        }, "notifier");
        nt.setDaemon(true);
        try {
            nt.start();
            synchronized (lock5) {
                synchronized (lock5) {
                    lock5.wait(3000);
                    sb.append(check(Thread.holdsLock(lock5), "waitnotify-reentrant-holds"));
                }
            }
            nt.join(3000);
            sb.append(check(true, "waitnotify-reentrant-exit"));
        } catch (Throwable e) {
            sb.append("waitnotify-reentrant: FAIL ").append(e).append('\n');
        }
        return sb.toString();
    }
}
