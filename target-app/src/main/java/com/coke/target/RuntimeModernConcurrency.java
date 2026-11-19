package com.coke.target;

import java.util.concurrent.Callable;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.CyclicBarrier;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.Phaser;
import java.util.concurrent.PriorityBlockingQueue;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.Semaphore;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;
import java.util.EnumMap;
import java.util.HashSet;
import java.util.Iterator;
import java.util.Spliterator;
import java.net.ServerSocket;
import java.net.SocketTimeoutException;

/** Advanced Java concurrency semantics that must remain Guest coroutine waits. */
public final class RuntimeModernConcurrency {
    private static final ReentrantLock LOCK = new ReentrantLock();
    private static final Condition CONDITION = LOCK.newCondition();

    private static volatile boolean sleepInterrupted;
    private static volatile boolean parkInterrupted;

    private enum MapKey {
        FIRST,
        SECOND
    }
    private static volatile boolean joinInterrupted;
    private static volatile boolean lockInterrupted;
    private static volatile boolean startRejected;
    private static volatile boolean groupMatched;
    private static volatile boolean waitInterrupted;
    private static volatile boolean conditionInterrupted;

    public int interruptFlow() {
        sleepInterrupted = false;
        parkInterrupted = false;
        joinInterrupted = false;
        lockInterrupted = false;
        startRejected = false;
        groupMatched = false;
        waitInterrupted = false;
        conditionInterrupted = false;

        Thread sleeper = new Thread(() -> {
            try {
                Thread.sleep(5_000L);
            } catch (InterruptedException expected) {
                sleepInterrupted = true;
            }
        }, "ModernInterruptSleeper");
        sleeper.start();
        sleep(5L);
        sleeper.interrupt();
        join(sleeper);

        Thread parker = new Thread(() -> {
            java.util.concurrent.locks.LockSupport.park();
            parkInterrupted = Thread.currentThread().isInterrupted();
        }, "ModernInterruptParker");
        parker.start();
        sleep(5L);
        parker.interrupt();
        join(parker);

        Thread joined = new Thread(() -> sleep(1_000L), "ModernJoinTarget");
        Thread joiner = new Thread(() -> {
            try {
                joined.join();
            } catch (InterruptedException expected) {
                joinInterrupted = true;
            }
        }, "ModernInterruptJoiner");
        joined.start();
        joiner.start();
        sleep(5L);
        joiner.interrupt();
        join(joiner);

        Thread lockOwner = new Thread(() -> {
            LOCK.lock();
            try {
                sleep(1_000L);
            } finally {
                LOCK.unlock();
            }
        }, "ModernLockOwner");
        Thread lockWaiter = new Thread(() -> {
            try {
                LOCK.lockInterruptibly();
                LOCK.unlock();
            } catch (InterruptedException expected) {
                lockInterrupted = true;
            }
        }, "ModernLockWaiter");
        lockOwner.start();
        lockWaiter.start();
        sleep(5L);
        lockWaiter.interrupt();
        join(lockWaiter);
        lockOwner.interrupt();
        join(lockOwner);

        Thread rejected = new Thread(() -> {
        }, "ModernStartTwice");
        rejected.start();
        join(rejected);
        try {
            rejected.start();
        } catch (IllegalThreadStateException expected) {
            startRejected = true;
        }

        ThreadGroup group = new ThreadGroup("ModernThreadGroup");
        Thread grouped = new Thread(
                group,
                () -> sleep(1L),
                "ModernGroupedThread");
        grouped.setPriority(8);
        grouped.start();
        join(grouped);
        int groupScore =
                (grouped.getThreadGroup() == group ? 32 : 0)
                + ("ModernThreadGroup".equals(group.getName()) ? 64 : 0)
                + (group.activeCount() == 0 ? 128 : 0)
                + (grouped.getPriority() == 8 ? 256 : 0)
                + (grouped.getState().ordinal() == 5 ? 512 : 0);
        groupMatched = groupScore == 992;

        Object monitor = new Object();
        Thread waitWaiter = new Thread(() -> {
            synchronized (monitor) {
                try {
                    monitor.wait();
                } catch (InterruptedException expected) {
                    waitInterrupted = true;
                }
            }
        }, "ModernInterruptWait");
        waitWaiter.start();
        sleep(5L);
        waitWaiter.interrupt();
        join(waitWaiter);

        Thread conditionWaiter = new Thread(() -> {
            LOCK.lock();
            try {
                CONDITION.await();
            } catch (InterruptedException expected) {
                conditionInterrupted = true;
            } finally {
                LOCK.unlock();
            }
        }, "ModernInterruptCondition");
        conditionWaiter.start();
        sleep(5L);
        conditionWaiter.interrupt();
        join(conditionWaiter);

        return (sleepInterrupted ? 1 : 0)
                + (parkInterrupted ? 2 : 0)
                + (joinInterrupted ? 4 : 0)
                + (lockInterrupted ? 8 : 0)
                + (startRejected ? 16 : 0)
                + groupScore
                + (waitInterrupted ? 64 : 0)
                + (conditionInterrupted ? 128 : 0);
    }

    public int timeoutFlow() {
        ReentrantLock lock = new ReentrantLock();
        Thread owner = new Thread(() -> {
            lock.lock();
            try {
                sleep(40L);
            } finally {
                lock.unlock();
            }
        }, "ModernTimedLockOwner");
        owner.start();
        sleep(2L);
        boolean acquired = false;
        try {
            acquired = lock.tryLock(5L, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
        }
        join(owner);

        Thread longTask = new Thread(() -> sleep(80L), "ModernTimedJoinWorker");
        longTask.start();
        long start = System.nanoTime();
        joinFor(longTask, 5L);
        long elapsedMs =
                TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - start);
        join(longTask);
        ReentrantLock fair = new ReentrantLock(true);
        return (!acquired ? 1 : 0)
                + (elapsedMs >= 3 && elapsedMs < 70 ? 2 : 0)
                + (fair.isFair() ? 4 : 0);
    }

    public int conditionFlow() {
        final boolean[] first = new boolean[1];
        Thread waiter = new Thread(() -> {
            LOCK.lock();
            try {
                while (!first[0]) {
                    try {
                        CONDITION.await();
                    } catch (InterruptedException ignored) {
                        return;
                    }
                }
            } finally {
                LOCK.unlock();
            }
        }, "ModernConditionWaiter");
        waiter.start();
        sleep(10L);
        LOCK.lock();
        try {
            first[0] = true;
            CONDITION.signalAll();
        } finally {
            LOCK.unlock();
        }
        join(waiter);
        return first[0] ? 1 : 0;
    }

    public int synchronizerFlow() {
        Semaphore semaphore = new Semaphore(0);
        CountDownLatch semaphoreReady = new CountDownLatch(1);
        Thread acquirer = new Thread(() -> {
            try {
                semaphore.acquire();
                semaphoreReady.countDown();
            } catch (InterruptedException ignored) {
            }
        }, "ModernSemaphoreAcquirer");
        acquirer.start();
        sleep(10L);
        semaphore.release();

        CountDownLatch latch = new CountDownLatch(1);
        final boolean[] latchReady = new boolean[1];
        Thread awaiter = new Thread(() -> {
            try {
                latch.await();
                latchReady[0] = true;
            } catch (InterruptedException ignored) {
            }
        }, "ModernLatchAwaiter");
        awaiter.start();
        sleep(10L);
        latch.countDown();
        join(acquirer);
        join(awaiter);
        boolean semaphoreTimedOut;
        try {
            semaphoreTimedOut = !semaphore.tryAcquire(
                    5L,
                    TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            semaphoreTimedOut = false;
        }
        Semaphore interruptible = new Semaphore(0);
        final boolean[] semaphoreInterrupted = new boolean[1];
        Thread interruptWaiter = new Thread(() -> {
            try {
                interruptible.acquire();
            } catch (InterruptedException expected) {
                semaphoreInterrupted[0] = true;
            }
        }, "ModernSemaphoreInterrupt");
        interruptWaiter.start();
        sleep(5L);
        interruptWaiter.interrupt();
        join(interruptWaiter);
        CountDownLatch timedLatch = new CountDownLatch(1);
        boolean latchTimedOut;
        try {
            latchTimedOut = !timedLatch.await(
                    5L,
                    TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            latchTimedOut = false;
        }
        return (semaphore.availablePermits() == 0 ? 1 : 0)
                + (semaphoreReady.getCount() == 0 ? 2 : 0)
                + (latchReady[0] ? 4 : 0)
                + (semaphoreTimedOut ? 8 : 0)
                + (semaphoreInterrupted[0] ? 16 : 0)
                + (latchTimedOut ? 32 : 0);
    }

    public int barrierPhaserFlow() {
        final CyclicBarrier barrier = new CyclicBarrier(3);
        final boolean[] barrierPassed = new boolean[3];
        Thread[] parties = new Thread[3];
        for (int index = 0; index < parties.length; index++) {
            final int slot = index;
            parties[index] = new Thread(() -> {
                try {
                    barrier.await();
                    barrierPassed[slot] = true;
                } catch (Exception ignored) {
                }
            }, "ModernBarrier-" + index);
            parties[index].start();
        }
        for (Thread party : parties) {
            join(party);
        }

        final Phaser phaser = new Phaser(3);
        final boolean[] phaserPassed = new boolean[3];
        Thread[] phaseThreads = new Thread[3];
        for (int index = 0; index < phaseThreads.length; index++) {
            final int slot = index;
            phaseThreads[index] = new Thread(() -> {
                int phase = phaser.arriveAndAwaitAdvance();
                phaserPassed[slot] = phase == 1;
            }, "ModernPhaser-" + index);
            phaseThreads[index].start();
        }
        for (Thread thread : phaseThreads) {
            join(thread);
        }
        int barrierCount = 0;
        int phaserCount = 0;
        for (boolean value : barrierPassed) {
            if (value) barrierCount++;
        }
        for (boolean value : phaserPassed) {
            if (value) phaserCount++;
        }
        return (barrierCount == 3 ? 1 : 0)
                + (phaserCount == 3 ? 2 : 0);
    }

    public int futureFlow() {
        ExecutorService executor = Executors.newFixedThreadPool(2);
        int score = 0;
        try {
            Future<Integer> future = executor.submit(new Callable<Integer>() {
                @Override
                public Integer call() throws Exception {
                    sleep(10L);
                    return 42;
                }
            });
            try {
                if (future.get() == 42) score += 1;
            } catch (Exception ignored) {
            }

            Future<Integer> timed = executor.submit(new Callable<Integer>() {
                @Override
                public Integer call() throws Exception {
                    sleep(60L);
                    return 7;
                }
            });
            try {
                timed.get(5L, TimeUnit.MILLISECONDS);
            } catch (TimeoutException expected) {
                score += 2;
            } catch (Exception ignored) {
            }
            if (timed.cancel(true)) score += 4;

            Future<Integer> failing = executor.submit(new Callable<Integer>() {
                @Override
                public Integer call() {
                    throw new IllegalArgumentException("expected");
                }
            });
            try {
                failing.get();
            } catch (ExecutionException expected) {
                score += 16;
            } catch (Exception ignored) {
            }
            executor.shutdown();
            try {
                if (executor.awaitTermination(100L, TimeUnit.MILLISECONDS)) {
                    score += 32;
                }
            } catch (InterruptedException ignored) {
            }
        } finally {
            executor.shutdownNow();
        }
        ScheduledExecutorService scheduled =
                Executors.newScheduledThreadPool(1, new ThreadFactory() {
                    @Override
                    public Thread newThread(Runnable task) {
                        Thread thread = new Thread(task);
                        thread.setName("target-scheduled");
                        return thread;
                    }
                });
        AtomicInteger periodicRuns = new AtomicInteger();
        ScheduledFuture<?> periodic = scheduled.scheduleAtFixedRate(
                periodicRuns::incrementAndGet,
                0L,
                4L,
                TimeUnit.MILLISECONDS);
        sleep(20L);
        periodic.cancel(false);
        if (periodicRuns.get() >= 2) score += 8;
        scheduled.shutdownNow();
        return score;
    }

    public int concurrentMapFlow() {
        ConcurrentHashMap<String, String> values =
                new ConcurrentHashMap<>();
        int score = 0;
        String first = values.computeIfAbsent("key", key -> "value");
        if ("value".equals(first)) score += 1;
        String cached = values.computeIfAbsent("key", key -> "miss");
        if ("value".equals(cached)) score += 2;
        String absent = values.computeIfAbsent("none", key -> null);
        if (absent == null && !values.containsKey("none")) score += 4;
        EnumMap<MapKey, Integer> enumValues =
                new EnumMap<>(MapKey.class);
        enumValues.put(MapKey.FIRST, 1);
        enumValues.put(MapKey.SECOND, 2);
        if (enumValues.get(MapKey.FIRST) == 1 &&
                enumValues.size() == 2) {
            score += 0;
        } else {
            return -1;
        }
        HashSet<String> set = new HashSet<>();
        set.add("one");
        set.add("two");
        AtomicInteger visited = new AtomicInteger();
        Spliterator<String> iterator = set.spliterator();
        iterator.forEachRemaining(value -> visited.incrementAndGet());
        if (visited.get() != 2) return -2;
        HashSet<Integer> removable = new HashSet<>();
        removable.add(1);
        removable.add(2);
        Iterator<Integer> removing = removable.iterator();
        removing.next();
        removing.remove();
        if (removable.size() != 1) return -2;
        if (!Boolean.TRUE.equals(Boolean.valueOf(true)) ||
                Boolean.FALSE.equals(Boolean.valueOf(true))) {
            return -3;
        }
        return score;
    }

    public int ioTimeoutFlow() {
        ServerSocket listener = null;
        try {
            listener = new ServerSocket(0);
            listener.setSoTimeout(5);
            listener.accept();
            return 0;
        } catch (SocketTimeoutException expected) {
            return 1;
        } catch (Exception ignored) {
            return 0;
        } finally {
            if (listener != null) {
                try {
                    listener.close();
                } catch (Exception ignored) {
                }
            }
        }
    }

    public int blockingQueueFlow() {
        int score = 0;
        final ArrayBlockingQueue<Integer> bounded =
                new ArrayBlockingQueue<>(1);
        final boolean[] consumed = new boolean[1];
        final boolean[] putBlocked = new boolean[1];
        Thread consumer = new Thread(() -> {
            sleep(10L);
            try {
                consumed[0] = bounded.take() == 1;
            } catch (InterruptedException ignored) {
            }
        }, "ModernQueueConsumer");
        Thread producer = new Thread(() -> {
            try {
                bounded.put(1);
                long blockedAt = System.nanoTime();
                bounded.put(2);
                putBlocked[0] =
                        TimeUnit.NANOSECONDS.toMillis(
                                System.nanoTime() - blockedAt) >= 3L;
            } catch (InterruptedException ignored) {
            }
        }, "ModernQueueProducer");
        consumer.start();
        producer.start();
        join(consumer);
        join(producer);
        try {
            if (consumed[0]) score += 1;
            if (putBlocked[0]) score += 2;
            if (bounded.take() == 2) score += 4;
        } catch (InterruptedException ignored) {
        }

        LinkedBlockingQueue<Integer> timed =
                new LinkedBlockingQueue<>(1);
        timed.offer(7);
        try {
            if (!timed.offer(8, 5L, TimeUnit.MILLISECONDS)) {
                score += 8;
            }
        } catch (InterruptedException ignored) {
        }

        final BlockingQueue<Integer> interrupted =
                new ArrayBlockingQueue<>(1);
        final boolean[] takeInterrupted = new boolean[1];
        Thread taker = new Thread(() -> {
            try {
                interrupted.take();
            } catch (InterruptedException expected) {
                takeInterrupted[0] = true;
            }
        }, "ModernQueueInterrupt");
        taker.start();
        sleep(5L);
        taker.interrupt();
        join(taker);
        if (takeInterrupted[0]) score += 16;

        PriorityBlockingQueue<Integer> priority =
                new PriorityBlockingQueue<>();
        priority.offer(5);
        priority.offer(1);
        priority.offer(3);
        try {
            if (priority.take() == 1
                    && priority.take() == 3
                    && priority.take() == 5) {
                score += 32;
            }
        } catch (InterruptedException ignored) {
        }

        ArrayBlockingQueue<Integer> drained =
                new ArrayBlockingQueue<>(3);
        drained.offer(10);
        drained.offer(11);
        java.util.ArrayList<Integer> target =
                new java.util.ArrayList<>();
        if (drained.drainTo(target) == 2
                && target.size() == 2) {
            score += 64;
        }
        return score;
    }

    public int completableFutureFlow() {
        int score = 0;
        CompletableFuture<Integer> pipeline =
                CompletableFuture.completedFuture(20)
                        .thenApply(value -> value + 1)
                        .thenApply(value -> value + 1);
        if (pipeline.join() == 22) score += 1;

        CompletableFuture<Integer> composed =
                CompletableFuture.completedFuture(20)
                        .thenCompose(value ->
                                CompletableFuture.completedFuture(
                                        value * 2));
        if (composed.join() == 40) score += 2;

        int combined = CompletableFuture.completedFuture(5)
                .thenCombine(
                        CompletableFuture.completedFuture(7),
                        (left, right) -> left + right)
                .join();
        if (combined == 12) score += 4;

        int recovered = CompletableFuture
                .<Integer>failedFuture(
                        new IllegalArgumentException("expected"))
                .exceptionally(error -> 9)
                .join();
        if (recovered == 9) score += 8;

        int handled = CompletableFuture.completedFuture(4)
                .handle((value, error) ->
                        error == null ? value + 1 : -1)
                .join();
        if (handled == 5) score += 16;

        final AtomicInteger completedStage = new AtomicInteger();
        CompletableFuture.completedFuture(1)
                .whenComplete((value, error) ->
                        completedStage.set(value))
                .join();
        if (completedStage.get() == 1) score += 32;

        CompletableFuture<Integer> async =
                CompletableFuture.supplyAsync(() -> {
                    sleep(5L);
                    return 30;
                }).thenApply(value -> value * 2);
        if (async.join() == 60) score += 64;

        CompletableFuture<Void> all = CompletableFuture.allOf(
                CompletableFuture.completedFuture(1),
                CompletableFuture.completedFuture(2),
                CompletableFuture.supplyAsync(() -> 3));
        all.join();
        if (all.isDone() && !all.isCompletedExceptionally()) {
            score += 128;
        }

        int first = CompletableFuture.anyOf(
                CompletableFuture.completedFuture(41),
                CompletableFuture.completedFuture(42))
                .thenApply(value -> (Integer) value)
                .join();
        if (first == 41 || first == 42) score += 256;

        CompletableFuture<Integer> timeout =
                new CompletableFuture<>();
        timeout.orTimeout(5L, TimeUnit.MILLISECONDS);
        try {
            timeout.join();
        } catch (CompletionException expected) {
            score += 512;
        }

        int fallback = new CompletableFuture<Integer>()
                .completeOnTimeout(77, 5L, TimeUnit.MILLISECONDS)
                .join();
        if (fallback == 77) score += 1024;

        CompletableFuture<Integer> cancelled =
                new CompletableFuture<>();
        if (cancelled.cancel(false)
                && cancelled.isCancelled()) {
            score += 2048;
        }

        ExecutorService executor =
                Executors.newFixedThreadPool(2);
        Future<Integer> future = executor.submit(
                new Callable<Integer>() {
                    @Override
                    public Integer call() {
                        sleep(5L);
                        return 3;
                    }
                });
        executor.shutdown();
        try {
            if (executor.awaitTermination(
                    200L, TimeUnit.MILLISECONDS)
                    && future.get() == 3) {
                score += 4096;
            }
        } catch (Exception ignored) {
        }
        return score;
    }

    public int advancedPhaserFlow() {
        int score = 0;
        final AtomicInteger advanced = new AtomicInteger();
        final Phaser terminating = new Phaser(2) {
            @Override
            protected boolean onAdvance(int phase, int parties) {
                advanced.incrementAndGet();
                return true;
            }
        };
        Thread first = new Thread(
                () -> terminating.arriveAndAwaitAdvance(),
                "ModernTerminatingPhaser-1");
        Thread second = new Thread(
                () -> terminating.arriveAndAwaitAdvance(),
                "ModernTerminatingPhaser-2");
        first.start();
        second.start();
        join(first);
        join(second);
        if (advanced.get() == 1 && terminating.isTerminated()) {
            score += 1;
        }

        Phaser timed = new Phaser(1);
        try {
            timed.awaitAdvanceInterruptibly(
                    0, 5L, TimeUnit.MILLISECONDS);
        } catch (TimeoutException expected) {
            score += 2;
        } catch (InterruptedException ignored) {
        }

        final Phaser interrupted = new Phaser(1);
        final boolean[] awaitInterrupted = new boolean[1];
        Thread waiter = new Thread(() -> {
            try {
                interrupted.awaitAdvanceInterruptibly(0);
            } catch (InterruptedException expected) {
                awaitInterrupted[0] = true;
            }
        }, "ModernInterruptiblePhaser");
        waiter.start();
        sleep(5L);
        waiter.interrupt();
        join(waiter);
        if (awaitInterrupted[0]) score += 4;

        final Phaser cyclic = new Phaser(2);
        final boolean[] phaseAdvanced = new boolean[2];
        Thread[] parties = new Thread[2];
        for (int index = 0; index < parties.length; index++) {
            final int slot = index;
            parties[index] = new Thread(() -> {
                phaseAdvanced[slot] =
                        cyclic.arriveAndAwaitAdvance() == 1;
            }, "ModernCyclicPhaser-" + index);
            parties[index].start();
        }
        for (Thread party : parties) {
            join(party);
        }
        if (phaseAdvanced[0] && phaseAdvanced[1]) score += 8;
        return score;
    }

    public int advancedSynchronizerFlow() {
        return synchronousQueueFlow()
                + (fairSemaphoreFlow() ? 4 : 0)
                + (threadGroupInheritanceFlow() ? 8 : 0);
    }

    public int synchronousQueueFlow() {
        int score = 0;
        final SynchronousQueue<Integer> synchronous =
                new SynchronousQueue<>();
        final int[] delivered = new int[1];
        final CountDownLatch consumerReady =
                new CountDownLatch(1);
        Thread consumer = new Thread(() -> {
            try {
                consumerReady.countDown();
                delivered[0] = synchronous.take();
            } catch (InterruptedException ignored) {
            }
        }, "ModernSynchronousConsumer");
        consumer.start();
        try {
            consumerReady.await();
        } catch (InterruptedException ignored) {
        }
        sleep(5L);
        if (synchronous.offer(42)) score += 1;
        join(consumer);
        if (delivered[0] != 42) score -= 1;

        Thread producer = new Thread(() -> {
            try {
                synchronous.put(43);
            } catch (InterruptedException ignored) {
            }
        }, "ModernSynchronousProducer");
        producer.start();
        sleep(5L);
        try {
            if (synchronous.poll(
                    50L, TimeUnit.MILLISECONDS) == 43) {
                score += 2;
            }
        } catch (InterruptedException ignored) {
        }
        join(producer);
        return score;
    }

    public boolean fairSemaphoreFlow() {
        final Semaphore fair = new Semaphore(1, true);
        try {
            fair.acquire();
        } catch (InterruptedException ignored) {
        }
        final boolean[] firstAcquired = new boolean[1];
        final boolean[] secondAcquired = new boolean[1];
        Thread first = new Thread(() -> {
            try {
                fair.acquire();
                firstAcquired[0] = true;
                sleep(2L);
                fair.release();
            } catch (InterruptedException ignored) {
            }
        }, "ModernFairSemaphoreFirst");
        Thread second = new Thread(() -> {
            try {
                fair.acquire();
                secondAcquired[0] = firstAcquired[0];
                fair.release();
            } catch (InterruptedException ignored) {
            }
        }, "ModernFairSemaphoreSecond");
        first.start();
        sleep(3L);
        second.start();
        sleep(3L);
        fair.release();
        join(first);
        join(second);
        return secondAcquired[0];
    }

    public boolean threadGroupInheritanceFlow() {
        ThreadGroup parent =
                new ThreadGroup("ModernParentGroup");
        ThreadGroup child =
                new ThreadGroup(parent, "ModernChildGroup");
        Thread nested = new Thread(
                child,
                () -> sleep(1L),
                "ModernNestedThread");
        nested.start();
        join(nested);
        return child.getParent() == parent
                && parent.parentOf(child)
                && nested.getThreadGroup() == child;
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

    private static void joinFor(Thread thread, long millis) {
        try {
            thread.join(millis);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }
}
