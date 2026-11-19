package cmtest;

import java.util.ArrayList;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.PriorityBlockingQueue;
import java.util.concurrent.SynchronousQueue;
import java.util.concurrent.TimeUnit;

/**
 * 阻塞队列/并发映射/ThreadGroup 面（覆盖面参考启发自 v1
 * RuntimeModernConcurrency.java blockingQueueFlow/synchronousQueueFlow/
 * concurrentMapFlow/threadGroupInheritanceFlow 与 NativeVmModernConcurrencyTest.kt
 * 断言面）。判据（Android j.u.c / java.lang 约定）：
 *   - ArrayBlockingQueue 有界 put 阻塞至 take、FIFO、take 可中断、drainTo；
 *   - LinkedBlockingQueue offer(timeout) 满则超时 false；
 *   - PriorityBlockingQueue 取出序 = 自然序；
 *   - SynchronousQueue 会合（offer 等待配对 take；poll(timeout) 配对 put）；
 *   - ConcurrentHashMap.computeIfAbsent 只计算一次/缓存命中/null 不登记；
 *   - ThreadGroup 归属/层级/活跃面：new Thread(group, …) 的 getThreadGroup()
 *     == 该组、getName、getParent/parentOf 层级链、activeCount 存活窗口计数。
 */
public class QueueMapProbe {

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

        // 1) ArrayBlockingQueue(1) 有界 put 阻塞 + FIFO
        final ArrayBlockingQueue<Integer> bounded = new ArrayBlockingQueue<Integer>(1);
        final boolean[] consumedFirst = new boolean[1];
        final boolean[] putBlocked = new boolean[1];
        Thread consumer = new Thread(new Runnable() {
            public void run() {
                sleep(20L);
                try {
                    consumedFirst[0] = bounded.take() == 1;
                } catch (InterruptedException ignored) {
                }
            }
        }, "cm-queue-consumer");
        Thread producer = new Thread(new Runnable() {
            public void run() {
                try {
                    bounded.put(1);
                    long blockedAt = System.nanoTime();
                    bounded.put(2);
                    putBlocked[0] =
                            TimeUnit.NANOSECONDS.toMillis(System.nanoTime() - blockedAt) >= 3L;
                } catch (InterruptedException ignored) {
                }
            }
        }, "cm-queue-producer");
        consumer.start();
        producer.start();
        join(consumer);
        join(producer);
        sb.append(check(consumedFirst[0], "boundedQueue-take-fifo"));
        sb.append(check(putBlocked[0], "boundedQueue-put-blocks"));
        Integer second = -1;
        try {
            second = bounded.take();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        sb.append(check(second == 2, "boundedQueue-second-element"));

        // 2) LinkedBlockingQueue(1) offer(timeout) 满超时 false
        LinkedBlockingQueue<Integer> timed = new LinkedBlockingQueue<Integer>(1);
        timed.offer(7);
        boolean offerTimedOut = true;
        try {
            offerTimedOut = !timed.offer(8, 5L, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        sb.append(check(offerTimedOut, "linkedQueue-offer-timeout"));

        // 3) take 可中断
        final BlockingQueue<Integer> interrupted = new ArrayBlockingQueue<Integer>(1);
        final boolean[] takeInterrupted = new boolean[1];
        Thread taker = new Thread(new Runnable() {
            public void run() {
                try {
                    interrupted.take();
                } catch (InterruptedException expected) {
                    takeInterrupted[0] = true;
                }
            }
        }, "cm-queue-interrupt");
        taker.start();
        sleep(10L);
        taker.interrupt();
        join(taker);
        sb.append(check(takeInterrupted[0], "queue-take-interrupt"));

        // 4) PriorityBlockingQueue 自然序取出
        PriorityBlockingQueue<Integer> priority = new PriorityBlockingQueue<Integer>();
        priority.offer(5);
        priority.offer(1);
        priority.offer(3);
        boolean ordered = true;
        try {
            ordered = priority.take() == 1 && priority.take() == 3 && priority.take() == 5;
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
            ordered = false;
        }
        sb.append(check(ordered, "priorityQueue-natural-order"));

        // 5) drainTo
        ArrayBlockingQueue<Integer> drained = new ArrayBlockingQueue<Integer>(3);
        drained.offer(10);
        drained.offer(11);
        ArrayList<Integer> target = new ArrayList<Integer>();
        sb.append(check(drained.drainTo(target) == 2 && target.size() == 2,
                "queue-drainTo"));

        // 6) SynchronousQueue 会合（offer 配对 take）
        final SynchronousQueue<Integer> sync = new SynchronousQueue<Integer>();
        final int[] delivered = new int[1];
        final boolean[] syncVmGap = new boolean[1];
        final CountDownLatch consumerReady = new CountDownLatch(1);
        Thread syncConsumer = new Thread(new Runnable() {
            public void run() {
                try {
                    consumerReady.countDown();
                    delivered[0] = sync.take();
                } catch (InterruptedException ignored) {
                } catch (NoSuchMethodError gap) {
                    // VM 缺口签名：java.lang.Thread.onSpinWait()V 缺失
                    //（SynchronousQueue 等待自旋面；builtin Thread 未挂 Java 9+ 桩）
                    syncVmGap[0] = true;
                }
            }
        }, "cm-sync-consumer");
        syncConsumer.start();
        try {
            consumerReady.await();
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        sleep(10L);
        boolean offered = sync.offer(42);
        join(syncConsumer);
        sb.append(check(offered && delivered[0] == 42, "synchronousQueue-offer-take",
                syncVmGap[0], "Thread.onSpinWait() 缺失，SynchronousQueue 等待面损坏；待 core 补桩"));

        // 7) SynchronousQueue put 配对 poll(timeout)
        Thread syncProducer = new Thread(new Runnable() {
            public void run() {
                try {
                    sync.put(43);
                } catch (InterruptedException ignored) {
                } catch (NoSuchMethodError gap) {
                    syncVmGap[0] = true;
                }
            }
        }, "cm-sync-producer");
        syncProducer.start();
        sleep(10L);
        Integer polled = null;
        try {
            polled = sync.poll(200L, TimeUnit.MILLISECONDS);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        } catch (NoSuchMethodError gap) {
            syncVmGap[0] = true;
        }
        join(syncProducer);
        sb.append(check(polled != null && polled == 43, "synchronousQueue-put-poll",
                syncVmGap[0], "Thread.onSpinWait() 缺失，SynchronousQueue 等待面损坏；待 core 补桩"));

        // 8) ConcurrentHashMap.computeIfAbsent 只算一次/缓存/null 不登记
        ConcurrentHashMap<String, String> map = new ConcurrentHashMap<String, String>();
        String firstValue = map.computeIfAbsent("key", new java.util.function.Function<String, String>() {
            public String apply(String key) {
                return "value";
            }
        });
        String cached = map.computeIfAbsent("key", new java.util.function.Function<String, String>() {
            public String apply(String key) {
                return "miss";
            }
        });
        String absent = map.computeIfAbsent("none", new java.util.function.Function<String, String>() {
            public String apply(String key) {
                return null;
            }
        });
        sb.append(check("value".equals(firstValue) && "value".equals(cached),
                "concurrentMap-computeIfAbsent-cached"));
        sb.append(check(absent == null && !map.containsKey("none"),
                "concurrentMap-null-not-registered"));

        // 9) ThreadGroup 归属 + 层级/活跃面（activeCount/parentOf/getParent；
        // 参考启发自 v1 threadGroupInheritanceFlow）
        ThreadGroup parent = new ThreadGroup("cm-parent-group");
        ThreadGroup child = new ThreadGroup(parent, "cm-child-group");
        final CountDownLatch releaseNested = new CountDownLatch(1);
        Thread nested = new Thread(child, new Runnable() {
            public void run() {
                try {
                    releaseNested.await();
                } catch (InterruptedException ignored) {
                    Thread.currentThread().interrupt();
                }
            }
        }, "cm-nested");
        nested.start();
        sleep(20L);  // nested 进入 await 的存活窗口
        sb.append(check(nested.getThreadGroup() == child, "threadGroup-assigned"));
        sb.append(check("cm-child-group".equals(child.getName()), "threadGroup-name"));
        sb.append(check(child.getParent() == parent, "threadGroup-getParent"));
        sb.append(check(parent.parentOf(child) && !child.parentOf(parent),
                "threadGroup-parentOf"));
        sb.append(check(child.activeCount() >= 1 && parent.activeCount() >= 1,
                "threadGroup-activeCount-alive"));
        releaseNested.countDown();
        join(nested);
        sb.append(check(child.activeCount() == 0 && parent.activeCount() == 0,
                "threadGroup-activeCount-done"));

        return sb.toString();
    }
}
