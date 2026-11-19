package com.coke.target;

import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.LockSupport;
import java.util.concurrent.locks.ReentrantLock;

/**
 * Stress fixture for Guest concurrency primitives. Every Thread below is a
 * logical Guest coroutine; waiting must preserve the DEX frame and task id.
 */
public final class RuntimeConcurrency {
    private static final ReentrantLock LOCK = new ReentrantLock();
    private static final Object MONITOR = new Object();
    private static final AtomicInteger ATOMIC_COUNT = new AtomicInteger();

    private static volatile boolean sleepDone;
    private static volatile boolean busyObservedSleep;
    private static volatile boolean lockHeld;
    private static volatile boolean lockAcquiredByWaiter;
    private static volatile boolean monitorHeld;
    private static volatile boolean monitorAcquiredByWaiter;
    private static volatile boolean parkReady;
    private static volatile boolean parkReturned;
    private static volatile boolean joinReturned;
    private static int criticalCount;
    private static int monitorCount;

    public int run() {
        return sleepAndYieldFlow()
                + lockAcrossSleepFlow()
                + monitorAcrossSleepFlow()
                + parkUnparkFlow()
                + joinFlow()
                + manySleepersFlow();
    }

    public int waitOnCurrentGuestThread(int millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            return -1;
        }
        return 91;
    }

    private static int sleepAndYieldFlow() {
        sleepDone = false;
        busyObservedSleep = false;
        Thread sleeper = new Thread(() -> {
            try {
                Thread.sleep(20L);
            } catch (InterruptedException ignored) {
                return;
            }
            sleepDone = true;
        }, "ConcurrencySleeper");
        Thread busy = new Thread(() -> {
            while (!sleepDone) {
                busyObservedSleep = true;
                Thread.yield();
            }
        }, "ConcurrencyBusy");
        sleeper.start();
        busy.start();
        join(sleeper);
        join(busy);
        return (sleepDone ? 1 : 0)
                + (busyObservedSleep ? 2 : 0)
                + (isTerminated(sleeper) && isTerminated(busy) ? 4 : 0);
    }

    private static int lockAcrossSleepFlow() {
        lockHeld = false;
        lockAcquiredByWaiter = false;
        criticalCount = 0;
        Thread owner = new Thread(() -> {
            LOCK.lock();
            lockHeld = true;
            try {
                Thread.sleep(15L);
            } catch (InterruptedException ignored) {
            } finally {
                LOCK.unlock();
            }
        }, "ConcurrencyLockOwner");
        Thread waiter = new Thread(() -> {
            while (!lockHeld) {
                Thread.yield();
            }
            LOCK.lock();
            try {
                lockAcquiredByWaiter = true;
                criticalCount++;
            } finally {
                LOCK.unlock();
            }
        }, "ConcurrencyLockWaiter");
        owner.start();
        waiter.start();
        join(owner);
        join(waiter);
        return (lockHeld ? 1 : 0)
                + (lockAcquiredByWaiter ? 2 : 0)
                + (criticalCount == 1 ? 4 : 0);
    }

    private static int monitorAcrossSleepFlow() {
        monitorHeld = false;
        monitorAcquiredByWaiter = false;
        monitorCount = 0;
        Thread owner = new Thread(() -> {
            synchronized (MONITOR) {
                monitorHeld = true;
                try {
                    Thread.sleep(15L);
                } catch (InterruptedException ignored) {
                }
            }
        }, "ConcurrencyMonitorOwner");
        Thread waiter = new Thread(() -> {
            while (!monitorHeld) {
                Thread.yield();
            }
            synchronized (MONITOR) {
                monitorAcquiredByWaiter = true;
                monitorCount++;
            }
        }, "ConcurrencyMonitorWaiter");
        owner.start();
        waiter.start();
        join(owner);
        join(waiter);
        return (monitorHeld ? 1 : 0)
                + (monitorAcquiredByWaiter ? 2 : 0)
                + (monitorCount == 1 ? 4 : 0);
    }

    private static int parkUnparkFlow() {
        parkReady = false;
        parkReturned = false;
        Thread parker = new Thread(() -> {
            parkReady = true;
            LockSupport.park();
            parkReturned = true;
        }, "ConcurrencyParker");
        Thread unparker = new Thread(() -> {
            while (!parkReady) {
                Thread.yield();
            }
            LockSupport.unpark(parker);
        }, "ConcurrencyUnparker");
        parker.start();
        unparker.start();
        join(parker);
        join(unparker);
        return (parkReady ? 1 : 0) + (parkReturned ? 2 : 0);
    }

    private static int joinFlow() {
        joinReturned = false;
        Thread worker = new Thread(() -> {
            try {
                Thread.sleep(10L);
            } catch (InterruptedException ignored) {
            }
        }, "ConcurrencyJoinWorker");
        Thread joiner = new Thread(() -> {
            join(worker);
            joinReturned = true;
        }, "ConcurrencyJoiner");
        worker.start();
        joiner.start();
        join(joiner);
        join(worker);
        return (joinReturned ? 1 : 0)
                + (isTerminated(joiner) ? 2 : 0);
    }

    private static int manySleepersFlow() {
        final int count = 48;
        ATOMIC_COUNT.set(0);
        Thread[] threads = new Thread[count];
        for (int index = 0; index < count; index++) {
            threads[index] = new Thread(() -> {
                try {
                    Thread.sleep(4L);
                } catch (InterruptedException ignored) {
                }
                ATOMIC_COUNT.incrementAndGet();
            }, "ConcurrencySleeper-" + index);
        }
        for (Thread thread : threads) {
            thread.start();
        }
        for (Thread thread : threads) {
            join(thread);
        }
        return ATOMIC_COUNT.get();
    }

    private static void join(Thread thread) {
        try {
            thread.join();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    private static boolean isTerminated(Thread thread) {
        return !thread.isAlive();
    }
}
