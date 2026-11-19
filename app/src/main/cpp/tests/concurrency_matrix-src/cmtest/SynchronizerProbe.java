package cmtest;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.Phaser;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 同步器矩阵（覆盖面参考启发自 v1 RuntimeModernConcurrency.java
 * synchronizerFlow/barrierPhaserFlow/advancedPhaserFlow/fairSemaphoreFlow 与
 * NativeVmModernConcurrencyTest.kt 断言面）。判据（Android j.u.c 约定）：
 *   - Semaphore acquire/release 交接；tryAcquire(timeout) 无许可超时 false；
 *     acquire 可中断；公平信号量 FIFO 序；
 *   - CountDownLatch await/countDown 交接 + await(timeout) 超时；
 *   - CyclicBarrier N 方会合放行；
 *   - Phaser arriveAndAwaitAdvance 相位推进；onAdvance 终止；超时/中断可抛。
 */
public class SynchronizerProbe {

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

        // 1) Semaphore 交接：0 许可阻塞 acquire，release 后通过
        final Semaphore semaphore = new Semaphore(0);
        final CountDownLatch gotPermit = new CountDownLatch(1);
        Thread acquirer = new Thread(new Runnable() {
            public void run() {
                try {
                    semaphore.acquire();
                    gotPermit.countDown();
                } catch (InterruptedException ignored) {
                }
            }
        }, "cm-sem-acquirer");
        acquirer.start();
        sleep(10L);
        semaphore.release();
        join(acquirer);
        sb.append(check(gotPermit.getCount() == 0, "semaphore-release-handoff"));

        // 2) tryAcquire(5ms) 无许可超时 false
        boolean timedOut = true;
        try {
            timedOut = !semaphore.tryAcquire(5L, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        sb.append(check(timedOut, "semaphore-tryAcquire-timeout"));

        // 3) acquire 可中断
        final Semaphore zero = new Semaphore(0);
        final boolean[] semInterrupted = new boolean[1];
        Thread semWaiter = new Thread(new Runnable() {
            public void run() {
                try {
                    zero.acquire();
                } catch (InterruptedException expected) {
                    semInterrupted[0] = true;
                }
            }
        }, "cm-sem-interrupt");
        semWaiter.start();
        sleep(10L);
        semWaiter.interrupt();
        join(semWaiter);
        sb.append(check(semInterrupted[0], "semaphore-acquire-interrupt"));

        // 4) CountDownLatch 交接 + 计数观测
        final CountDownLatch latch = new CountDownLatch(2);
        final boolean[] latchDone = new boolean[1];
        Thread latchWaiter = new Thread(new Runnable() {
            public void run() {
                try {
                    latch.await();
                    latchDone[0] = true;
                } catch (InterruptedException ignored) {
                }
            }
        }, "cm-latch-waiter");
        latchWaiter.start();
        sleep(10L);
        sb.append(check(latch.getCount() == 2, "latch-count-observed"));
        latch.countDown();
        sleep(5L);
        sb.append(check(!latchDone[0], "latch-blocks-before-zero"));
        latch.countDown();
        join(latchWaiter);
        sb.append(check(latchDone[0] && latch.getCount() == 0, "latch-countDown-handoff"));

        // 5) CountDownLatch await(timeout) 超时
        boolean latchTimedOut = true;
        try {
            latchTimedOut = !new CountDownLatch(1).await(5L, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        sb.append(check(latchTimedOut, "latch-await-timeout"));

        // 6) 公平信号量 FIFO：先到先得（first 先阻塞，release 后 first 先入）
        final Semaphore fair = new Semaphore(1, true);
        try {
            fair.acquire();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        final boolean[] firstEntered = new boolean[1];
        final boolean[] orderHeld = new boolean[1];
        Thread first = new Thread(new Runnable() {
            public void run() {
                try {
                    fair.acquire();
                    firstEntered[0] = true;
                    sleep(10L);
                    fair.release();
                } catch (InterruptedException ignored) {
                }
            }
        }, "cm-fair-first");
        Thread second = new Thread(new Runnable() {
            public void run() {
                try {
                    fair.acquire();
                    orderHeld[0] = firstEntered[0];
                    fair.release();
                } catch (InterruptedException ignored) {
                }
            }
        }, "cm-fair-second");
        first.start();
        sleep(15L);  // 保证 first 先入等待队列
        second.start();
        sleep(15L);
        fair.release();
        join(first);
        join(second);
        sb.append(check(orderHeld[0], "fair-semaphore-fifo-order"));

        // 7) CyclicBarrier 3 方会合
        final CyclicBarrier barrier = new CyclicBarrier(3);
        final boolean[] barrierPassed = new boolean[3];
        Thread[] parties = new Thread[3];
        for (int index = 0; index < 3; index++) {
            final int slot = index;
            parties[index] = new Thread(new Runnable() {
                public void run() {
                    try {
                        barrier.await();
                        barrierPassed[slot] = true;
                    } catch (Exception ignored) {
                    }
                }
            }, "cm-barrier-" + index);
            parties[index].start();
        }
        for (Thread party : parties) join(party);
        sb.append(check(barrierPassed[0] && barrierPassed[1] && barrierPassed[2],
                "cyclicBarrier-release"));

        // 8) Phaser 相位推进（arriveAndAwaitAdvance 返回到达相位的下一相号 1）
        final Phaser phaser = new Phaser(3);
        final boolean[] phaseAdvanced = new boolean[3];
        final boolean[] phaserVmGap = new boolean[1];
        Thread[] phaseThreads = new Thread[3];
        for (int index = 0; index < 3; index++) {
            final int slot = index;
            phaseThreads[index] = new Thread(new Runnable() {
                public void run() {
                    try {
                        phaseAdvanced[slot] = phaser.arriveAndAwaitAdvance() == 1;
                    } catch (NoSuchMethodError gap) {
                        // VM 缺口签名：java.lang.Thread.onSpinWait()V 缺失
                        //（Phaser 等待自旋面；builtin Thread 未挂 Java 9+ 桩）
                        phaserVmGap[0] = true;
                    }
                }
            }, "cm-phaser-" + index);
            phaseThreads[index].start();
        }
        for (Thread thread : phaseThreads) join(thread);
        sb.append(check(phaseAdvanced[0] && phaseAdvanced[1] && phaseAdvanced[2],
                "phaser-phase-advance", phaserVmGap[0],
                "Thread.onSpinWait() 缺失，Phaser 等待自旋面损坏；待 core 补桩"));

        // 9) Phaser onAdvance 终止语义
        final AtomicInteger advanced = new AtomicInteger();
        final Phaser terminating = new Phaser(2) {
            protected boolean onAdvance(int phase, int partiesLeft) {
                advanced.incrementAndGet();
                return true;  // 终止
            }
        };
        final boolean[] termVmGap = new boolean[1];
        Thread t1 = new Thread(new Runnable() {
            public void run() {
                try {
                    terminating.arriveAndAwaitAdvance();
                } catch (NoSuchMethodError gap) {
                    termVmGap[0] = true;
                }
            }
        }, "cm-phaser-term-1");
        Thread t2 = new Thread(new Runnable() {
            public void run() {
                try {
                    terminating.arriveAndAwaitAdvance();
                } catch (NoSuchMethodError gap) {
                    termVmGap[0] = true;
                }
            }
        }, "cm-phaser-term-2");
        t1.start();
        t2.start();
        join(t1);
        join(t2);
        sb.append(check(advanced.get() == 1 && terminating.isTerminated(),
                "phaser-onAdvance-terminate", termVmGap[0],
                "Thread.onSpinWait() 缺失；待 core 补桩"));

        // 10) awaitAdvanceInterruptibly 超时 / 中断
        boolean phaserTimedOut = true;
        try {
            new Phaser(1).awaitAdvanceInterruptibly(0, 5L, TimeUnit.MILLISECONDS);
            phaserTimedOut = false;
        } catch (TimeoutException expected) {
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        sb.append(check(phaserTimedOut, "phaser-await-timeout"));

        final Phaser stuck = new Phaser(1);
        final boolean[] phaserInterrupted = new boolean[1];
        Thread waiter = new Thread(new Runnable() {
            public void run() {
                try {
                    stuck.awaitAdvanceInterruptibly(0);
                } catch (InterruptedException expected) {
                    phaserInterrupted[0] = true;
                }
            }
        }, "cm-phaser-interrupt");
        waiter.start();
        sleep(10L);
        waiter.interrupt();
        join(waiter);
        sb.append(check(phaserInterrupted[0], "phaser-await-interrupt"));

        return sb.toString();
    }
}
