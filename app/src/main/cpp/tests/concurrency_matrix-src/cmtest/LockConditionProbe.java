package cmtest;

import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;

/**
 * 锁/条件面超时与公平语义（覆盖面参考启发自 v1 RuntimeModernConcurrency.java
 * timeoutFlow/conditionFlow 与 NativeVmModernConcurrencyTest.kt 断言面）。判据
 * （Android j.u.c.locks 约定）：
 *   - join(millis) 超时返回（不等目标终止）；
 *   - ReentrantLock.tryLock(timeout) 对被持锁超时返回 false；
 *   - ReentrantLock(true).isFair()；
 *   - Condition await/signalAll 握手（谓词循环防虚假唤醒）。
 */
public class LockConditionProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
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

        // 1) tryLock(5ms) 对被持锁超时 false
        ReentrantLock lock = new ReentrantLock();
        Thread owner = new Thread(new Runnable() {
            public void run() {
                lock.lock();
                try {
                    sleep(80L);
                } finally {
                    lock.unlock();
                }
            }
        }, "cm-timed-owner");
        owner.start();
        sleep(10L);
        boolean acquired = false;
        try {
            acquired = lock.tryLock(5L, TimeUnit.MILLISECONDS);
            if (acquired) lock.unlock();
        } catch (InterruptedException ignored) {
        }
        join(owner);
        sb.append(check(!acquired, "tryLock-timeout-false"));

        // 2) join(5ms) 超时直返（目标仍存活即为判据；不做墙上时钟断言）
        Thread longTask = new Thread(new Runnable() {
            public void run() {
                sleep(120L);
            }
        }, "cm-timed-join");
        longTask.start();
        try {
            longTask.join(5L);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        boolean aliveAfterTimedJoin = longTask.isAlive();
        join(longTask);
        sb.append(check(aliveAfterTimedJoin, "timedJoin-returns-early"));

        // 3) 公平锁标记
        sb.append(check(new ReentrantLock(true).isFair(), "fairLock-flag"));

        // 4) Condition await/signalAll 握手（谓词循环）
        final ReentrantLock clock = new ReentrantLock();
        final Condition condition = clock.newCondition();
        final boolean[] done = new boolean[1];
        Thread waiter = new Thread(new Runnable() {
            public void run() {
                clock.lock();
                try {
                    while (!done[0]) {
                        try {
                            condition.await();
                        } catch (InterruptedException ignored) {
                            return;
                        }
                    }
                } finally {
                    clock.unlock();
                }
            }
        }, "cm-cond-waiter");
        waiter.start();
        sleep(10L);
        clock.lock();
        try {
            done[0] = true;
            condition.signalAll();
        } finally {
            clock.unlock();
        }
        join(waiter);
        sb.append(check(done[0], "condition-signalAll-handoff"));

        return sb.toString();
    }
}
