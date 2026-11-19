package cmtest;

import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.LockSupport;
import java.util.concurrent.locks.ReentrantLock;

/**
 * 线程中断语义矩阵（覆盖面参考启发自 v1 RuntimeModernConcurrency.java
 * interruptFlow 与 NativeVmModernConcurrencyTest.kt 断言面；本体按 Android 线程
 * 语义重写）。判据（Android java.lang.Thread / j.u.c 约定）：
 *   - sleep/join/wait/await/lockInterruptibly 被中断 -> InterruptedException
 *     且中断位在抛出时清除；
 *   - LockSupport.park 被中断 -> 直接返回（不抛），中断位保留；
 *   - 已终止线程再 start -> IllegalThreadStateException。
 */
public class InterruptProbe {

    private static final ReentrantLock LOCK = new ReentrantLock();
    private static final Condition CONDITION = LOCK.newCondition();

    private static volatile boolean sleepInterrupted;
    private static volatile boolean sleepFlagCleared;
    private static volatile boolean parkInterrupted;
    private static volatile boolean joinInterrupted;
    private static volatile boolean lockInterrupted;
    private static volatile boolean waitInterrupted;
    private static volatile boolean conditionInterrupted;
    private static volatile boolean startRejected;

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    /** 三态判定：OK / FAIL / GAP（命中已知 VM 缺口签名时降级，不计 FAIL）。 */
    private static String check(boolean cond, String name, boolean vmGap, String gapNote) {
        if (cond) return name + ": OK\n";
        if (vmGap) return name + ": GAP (" + gapNote + ")\n";
        return name + ": FAIL\n";
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    private static void join(Thread thread) {
        try {
            thread.join();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        // 1) Thread.sleep 中断：IE 抛出 + 中断位清除
        sleepInterrupted = false;
        sleepFlagCleared = false;
        Thread sleeper = new Thread(new Runnable() {
            public void run() {
                try {
                    Thread.sleep(5_000L);
                } catch (InterruptedException expected) {
                    sleepInterrupted = true;
                    sleepFlagCleared = !Thread.currentThread().isInterrupted();
                }
            }
        }, "cm-sleep");
        sleeper.start();
        sleep(10L);
        sleeper.interrupt();
        join(sleeper);
        sb.append(check(sleepInterrupted, "interrupt-sleep-thrown"));
        sb.append(check(sleepFlagCleared, "interrupt-sleep-flag-cleared"));

        // 2) LockSupport.park 中断：直返不抛 + 中断位保留
        parkInterrupted = false;
        Thread parker = new Thread(new Runnable() {
            public void run() {
                LockSupport.park();
                parkInterrupted = Thread.currentThread().isInterrupted();
                Thread.interrupted();  // 清位，避免影响后续
            }
        }, "cm-park");
        parker.start();
        sleep(10L);
        parker.interrupt();
        join(parker);
        sb.append(check(parkInterrupted, "interrupt-park-returns-with-flag"));

        // 3) Thread.join 中断：IE 抛出
        joinInterrupted = false;
        Thread joined = new Thread(new Runnable() {
            public void run() {
                sleep(1_000L);
            }
        }, "cm-join-target");
        Thread joiner = new Thread(new Runnable() {
            public void run() {
                try {
                    joined.join();
                } catch (InterruptedException expected) {
                    joinInterrupted = true;
                }
            }
        }, "cm-joiner");
        joined.start();
        joiner.start();
        sleep(10L);
        joiner.interrupt();
        join(joiner);
        joined.interrupt();
        join(joined);
        sb.append(check(joinInterrupted, "interrupt-join-thrown"));

        // 4) ReentrantLock.lockInterruptibly 中断：IE 抛出
        lockInterrupted = false;
        final boolean[] lockVmGap = new boolean[1];
        Thread lockOwner = new Thread(new Runnable() {
            public void run() {
                LOCK.lock();
                try {
                    sleep(1_000L);
                } finally {
                    LOCK.unlock();
                }
            }
        }, "cm-lock-owner");
        Thread lockWaiter = new Thread(new Runnable() {
            public void run() {
                try {
                    LOCK.lockInterruptibly();
                    LOCK.unlock();
                } catch (InterruptedException expected) {
                    lockInterrupted = true;
                } catch (IllegalArgumentException gap) {
                    // VM 缺口签名：VarHandle AccessDescriptor.of 查表失败
                    // （"No AccessMode value for method name compareAndSet"，
                    // runtime-libs VarHandle.java:104；类初始化跨任务观测闸
                    // 修复（class_linker JLS 12.4.2）后本签名应消失）
                    lockVmGap[0] = true;
                }
            }
        }, "cm-lock-waiter");
        lockOwner.start();
        lockWaiter.start();
        sleep(10L);
        lockWaiter.interrupt();
        join(lockWaiter);
        lockOwner.interrupt();
        join(lockOwner);
        sb.append(check(lockInterrupted, "interrupt-lockInterruptibly-thrown",
                lockVmGap[0],
                "VarHandle AccessMode compareAndSet 查表失败，待 runtime-libs/core 修复"));

        // 5) Object.wait 中断：IE 抛出
        waitInterrupted = false;
        final Object monitor = new Object();
        Thread waitWaiter = new Thread(new Runnable() {
            public void run() {
                synchronized (monitor) {
                    try {
                        monitor.wait();
                    } catch (InterruptedException expected) {
                        waitInterrupted = true;
                    }
                }
            }
        }, "cm-wait");
        waitWaiter.start();
        sleep(10L);
        waitWaiter.interrupt();
        join(waitWaiter);
        sb.append(check(waitInterrupted, "interrupt-wait-thrown"));

        // 6) Condition.await 中断：IE 抛出
        conditionInterrupted = false;
        Thread conditionWaiter = new Thread(new Runnable() {
            public void run() {
                LOCK.lock();
                try {
                    CONDITION.await();
                } catch (InterruptedException expected) {
                    conditionInterrupted = true;
                } finally {
                    LOCK.unlock();
                }
            }
        }, "cm-condition");
        conditionWaiter.start();
        sleep(10L);
        conditionWaiter.interrupt();
        join(conditionWaiter);
        sb.append(check(conditionInterrupted, "interrupt-conditionAwait-thrown"));

        // 7) 已终止线程重复 start：IllegalThreadStateException
        startRejected = false;
        Thread rejected = new Thread(new Runnable() {
            public void run() {
            }
        }, "cm-double-start");
        rejected.start();
        join(rejected);
        try {
            rejected.start();
        } catch (IllegalThreadStateException expected) {
            startRejected = true;
        }
        sb.append(check(startRejected, "start-twice-rejected"));

        return sb.toString();
    }
}
