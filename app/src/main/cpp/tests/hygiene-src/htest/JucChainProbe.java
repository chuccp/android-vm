package htest;

import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.locks.ReentrantLock;

/**
 * juc 线程池/锁链死对象形态（设备停点同族：ThreadPoolExecutor.execute →
 * ensureInitialized → iget-object workQueue -> dirty reference）。
 *
 * 形态：RxJava/调度面的 executor 多为【子类实例】（ScheduledThreadPoolExecutor
 * 形态）——workQueue/sync 等槽声明在超类；GC 穿插后继承槽目标被清扫，
 * ensureInitialized/lock 的 iget-object 读到悬挂指针。
 *
 * 覆盖：PoolSub（ThreadPoolExecutor 子类，设备同族）/ LockSub（ReentrantLock
 * 子类 -> sync 继承槽）/ 纯 ThreadPoolExecutor 对照（自家槽口径，任何 GC
 * 口径都必须保活）。
 */
public final class JucChainProbe {
    /** ThreadPoolExecutor 子类（设备 RxJava ScheduledThreadPoolExecutor 形态）。 */
    public static class PoolSub extends ThreadPoolExecutor {
        public Object own = new Object();

        public PoolSub() {
            super(1, 2, 30, TimeUnit.SECONDS, new LinkedBlockingQueue<Runnable>());
        }
    }

    /** ReentrantLock 子类：sync/AQS 槽全在继承链上。 */
    public static class LockSub extends ReentrantLock {
        public Object own = new Object();
    }

    public static Object makePoolSub() {
        return new PoolSub();
    }

    public static Object makeLockSub() {
        return new LockSub();
    }

    /** 对照：纯 ThreadPoolExecutor（workQueue 为自家声明字段）。 */
    public static Object makePlainPool() {
        return new ThreadPoolExecutor(1, 2, 30, TimeUnit.SECONDS,
                new LinkedBlockingQueue<Runnable>());
    }

    /** ensureInitialized + iget-object workQueue（设备停点同字节码面）。 */
    public static String consumePool(Object o) {
        try {
            ThreadPoolExecutor p = (ThreadPoolExecutor) o;
            int size = p.getQueue().size();  // ensureInitialized: iget-object workQueue
            int pool = p.getPoolSize();      // iget workerCount（继承 int 槽）
            long tasks = p.getTaskCount();   // iget submittedTasks（继承 long 槽）
            if (size < 0 || pool < 0 || tasks < 0) return "pool: FAIL size=" + size;
            return "pool: OK";
        } catch (Throwable t) {
            return "pool: FAIL " + t;
        }
    }

    /** lock/unlock/tryLock：iget-object sync（ReentrantLock 继承槽）+ AQS 链。 */
    public static String consumeLock(Object o) {
        try {
            ReentrantLock l = (ReentrantLock) o;
            l.lock();
            boolean held = l.isLocked();     // iget-object sync
            l.unlock();
            boolean got = l.tryLock();
            if (got) l.unlock();
            if (!held || !got) return "lock: FAIL held=" + held + " got=" + got;
            return "lock: OK";
        } catch (Throwable t) {
            return "lock: FAIL " + t;
        }
    }

    /** 对照消费（纯 executor：自家槽口径）。 */
    public static String consumePlain(Object o) {
        return consumePool(o).replace("pool:", "plain:");
    }
}
