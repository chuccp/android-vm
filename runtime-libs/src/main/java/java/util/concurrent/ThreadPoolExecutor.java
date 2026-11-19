package java.util.concurrent;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

public class ThreadPoolExecutor implements ExecutorService {
    private int corePoolSize;
    private int maximumPoolSize;
    private long keepAliveMillis;
    private BlockingQueue<Runnable> workQueue;
    private ThreadFactory threadFactory;
    private RejectedExecutionHandler rejectionHandler;
    private boolean allowCoreTimeout;
    private boolean shutdown;
    private int workerCount;
    private int largestPoolSize;
    private int activeCount;
    private long completedTasks;
    private long submittedTasks;
    private Worker firstWorker;
    private int threadSequence;

    public ThreadPoolExecutor(
            int corePoolSize,
            int maximumPoolSize,
            long keepAliveTime,
            TimeUnit unit,
            BlockingQueue<Runnable> workQueue) {
        this(
                corePoolSize,
                maximumPoolSize,
                keepAliveTime,
                unit,
                workQueue,
                null,
                null);
    }

    public ThreadPoolExecutor(
            int corePoolSize,
            int maximumPoolSize,
            long keepAliveTime,
            TimeUnit unit,
            BlockingQueue<Runnable> workQueue,
            ThreadFactory threadFactory) {
        this(
                corePoolSize,
                maximumPoolSize,
                keepAliveTime,
                unit,
                workQueue,
                threadFactory,
                null);
    }

    public ThreadPoolExecutor(
            int corePoolSize,
            int maximumPoolSize,
            long keepAliveTime,
            TimeUnit unit,
            BlockingQueue<Runnable> workQueue,
            RejectedExecutionHandler rejectionHandler) {
        this(
                corePoolSize,
                maximumPoolSize,
                keepAliveTime,
                unit,
                workQueue,
                null,
                rejectionHandler);
    }

    public ThreadPoolExecutor(
            int corePoolSize,
            int maximumPoolSize,
            long keepAliveTime,
            TimeUnit unit,
            BlockingQueue<Runnable> workQueue,
            ThreadFactory threadFactory,
            RejectedExecutionHandler rejectionHandler) {
        if (corePoolSize < 0
                || maximumPoolSize <= 0
                || maximumPoolSize < corePoolSize
                || keepAliveTime < 0
                || unit == null
                || workQueue == null) {
            throw new IllegalArgumentException("invalid executor configuration");
        }
        this.corePoolSize = corePoolSize;
        this.maximumPoolSize = maximumPoolSize;
        this.keepAliveMillis = unit.toMillis(keepAliveTime);
        this.workQueue = workQueue;
        this.threadFactory = threadFactory;
        this.rejectionHandler = rejectionHandler;
    }

    @Override
    public void execute(Runnable command) {
        if (command == null) {
            throw new NullPointerException("command");
        }
        synchronized (this) {
            ensureInitialized();
            if (shutdown) {
                reject(command);
                return;
            }
            submittedTasks++;
            boolean offered = workQueue.offer(command);
            // cached 池语义（core=0, SynchronousQueue）：offer 失败说明当前没有
            // 空闲 worker 在 take/poll 等待握手，必须扩容新 worker（JDK
            // ThreadPoolExecutor 的 !offer → addWorker 路径）。否则
            // FileDownloader 的多连接下载（DOWNLOAD_EXECUTOR.invokeAll 提交 3 个
            // 分段任务）会在重试超时后抛 RejectedExecutionException，invokeAll
            // 立即返回，DownloadLaunchRunnable 误判完成并命中断言
            // "sofar[x] not equal total[y]" 放弃整包。
            if (!offered && workerCount < maximumPoolSize) {
                addWorker();
            }
            if ((workerCount == 0
                    || (workerCount < corePoolSize && activeCount >= workerCount))
                    && workerCount < maximumPoolSize) {
                addWorker();
            }
            if (!offered) {
                if (workerCount >= maximumPoolSize) {
                    reject(command);
                    return;
                }
                if (workerCount == 0) {
                    addWorker();
                }
                // 无容量队列（SynchronousQueue 型 cached pool）语义：offer 需要
                // 与 worker 的 take/poll 直接握手，而 worker 启动是异步的——首次
                // offer 常常尚无消费者。旧实现此时直接 reject，导致 OkHttp
                // 连接池 cleanup 任务在 put() 内抛 RejectedExecutionException，
                // 刚建好的连接被判死（表现为 TLS 连上但请求从不写出）。
                // 有界重试：wait(1) 释放执行器锁，让 worker 进入阻塞 take/poll。
                boolean handedOff = false;
                for (int i = 0; i < 200 && !handedOff; i++) {
                    handedOff = workQueue.offer(command);
                    if (handedOff) {
                        break;
                    }
                    try {
                        wait(1);
                    } catch (InterruptedException interruption) {
                        Thread.currentThread().interrupt();
                        break;
                    }
                }
                if (!handedOff) {
                    reject(command);
                    return;
                }
            }
            notifyAll();
        }
    }

    @Override
    public <T> Future<T> submit(Callable<T> task) {
        FutureTask<T> future = new FutureTask<T>(task);
        execute(future);
        return future;
    }

    @Override
    public <T> Future<T> submit(Runnable task, T result) {
        FutureTask<T> future = new FutureTask<T>(task, result);
        execute(future);
        return future;
    }

    @Override
    public Future<?> submit(Runnable task) {
        return submit(task, null);
    }

    @Override
    public <T> List<Future<T>> invokeAll(
            Collection<? extends Callable<T>> tasks) throws InterruptedException {
        if (tasks == null) {
            throw new NullPointerException("tasks");
        }
        ArrayList<Future<T>> futures = new ArrayList<Future<T>>(tasks.size());
        for (Callable<T> task : tasks) {
            futures.add(submit(task));
        }
        for (Future<T> future : futures) {
            if (!future.isDone()) {
                try {
                    future.get();
                } catch (ExecutionException ignored) {
                } catch (CancellationException ignored) {
                }
            }
        }
        return futures;
    }

    @Override
    public <T> T invokeAny(
            Collection<? extends Callable<T>> tasks)
            throws InterruptedException, ExecutionException {
        List<Future<T>> futures = invokeAll(tasks);
        ExecutionException last = null;
        for (Future<T> future : futures) {
            try {
                return future.get();
            } catch (ExecutionException failure) {
                last = failure;
            }
        }
        if (last != null) {
            throw last;
        }
        throw new ExecutionException(new IllegalStateException("no task"));
    }

    @Override
    public synchronized void shutdown() {
        if (shutdown) {
            return;
        }
        shutdown = true;
        interruptWorkers();
        notifyAll();
    }

    @Override
    public synchronized List<Runnable> shutdownNow() {
        shutdown = true;
        ArrayList<Runnable> drained = new ArrayList<Runnable>();
        ensureInitialized();
        workQueue.drainTo(drained);
        interruptWorkers();
        notifyAll();
        return drained;
    }

    @Override
    public synchronized boolean isShutdown() {
        return shutdown;
    }

    @Override
    public synchronized boolean isTerminated() {
        return shutdown && workerCount == 0;
    }

    @Override
    public synchronized boolean awaitTermination(long timeout, TimeUnit unit)
            throws InterruptedException {
        long remaining = unit.toNanos(timeout);
        long deadline = System.nanoTime() + remaining;
        while (!isTerminated()) {
            if (remaining <= 0) {
                return false;
            }
            TimeUnit.NANOSECONDS.timedWait(this, remaining);
            remaining = deadline - System.nanoTime();
        }
        return true;
    }

    public synchronized int getCorePoolSize() {
        return corePoolSize;
    }

    public synchronized int getMaximumPoolSize() {
        return maximumPoolSize;
    }

    public synchronized int getPoolSize() {
        return workerCount;
    }

    public synchronized int getLargestPoolSize() {
        return largestPoolSize;
    }

    public synchronized int getActiveCount() {
        return activeCount;
    }

    public synchronized long getTaskCount() {
        return submittedTasks;
    }

    public synchronized long getCompletedTaskCount() {
        return completedTasks;
    }

    public synchronized BlockingQueue<Runnable> getQueue() {
        ensureInitialized();
        return workQueue;
    }

    public synchronized boolean remove(Runnable task) {
        ensureInitialized();
        return workQueue.remove(task);
    }

    public synchronized void setCorePoolSize(int size) {
        if (size < 0 || size > maximumPoolSize) {
            throw new IllegalArgumentException("invalid core pool size");
        }
        corePoolSize = size;
        notifyAll();
    }

    public synchronized void setMaximumPoolSize(int size) {
        if (size <= 0 || size < corePoolSize) {
            throw new IllegalArgumentException("invalid maximum pool size");
        }
        maximumPoolSize = size;
        notifyAll();
    }

    public synchronized void allowCoreThreadTimeOut(boolean value) {
        allowCoreTimeout = value;
        notifyAll();
    }

    public synchronized boolean allowsCoreThreadTimeOut() {
        return allowCoreTimeout;
    }

    public synchronized boolean prestartCoreThread() {
        if (workerCount >= corePoolSize) {
            return false;
        }
        addWorker();
        return true;
    }

    public synchronized int prestartAllCoreThreads() {
        int started = 0;
        while (workerCount < corePoolSize) {
            addWorker();
            started++;
        }
        return started;
    }

    public void purge() {
    }

    protected void beforeExecute(Thread thread, Runnable task) {
    }

    protected void afterExecute(Runnable task, Throwable failure) {
    }

    protected void terminated() {
    }

    private void ensureInitialized() {
        if (workQueue == null) {
            int processors = Runtime.getRuntime().availableProcessors();
            corePoolSize = 0;
            maximumPoolSize = Math.max(1, processors);
            keepAliveMillis = 60_000L;
            workQueue = new LinkedBlockingQueue<Runnable>();
        }
    }

    private void addWorker() {
        Worker worker = new Worker();
        worker.next = firstWorker;
        firstWorker = worker;
        workerCount++;
        largestPoolSize = Math.max(largestPoolSize, workerCount);
        Thread thread = threadFactory == null
                ? new Thread(worker)
                : threadFactory.newThread(worker);
        if (thread == null) {
            removeWorker(worker);
            throw new RejectedExecutionException("thread factory returned null");
        }
        thread.setName("guest-pool-" + (++threadSequence));
        worker.thread = thread;
        thread.start();
    }

    private void interruptWorkers() {
        for (Worker worker = firstWorker; worker != null; worker = worker.next) {
            if (worker.thread != null) {
                worker.thread.interrupt();
            }
        }
    }

    private void removeWorker(Worker worker) {
        Worker previous = null;
        Worker current = firstWorker;
        while (current != null) {
            if (current == worker) {
                if (previous == null) {
                    firstWorker = current.next;
                } else {
                    previous.next = current.next;
                }
                current.next = null;
                workerCount--;
                notifyAll();
                return;
            }
            previous = current;
            current = current.next;
        }
    }

    private void reject(Runnable command) {
        RejectedExecutionHandler handler = rejectionHandler;
        if (handler == null) {
            handler = new AbortPolicy();
        }
        handler.rejectedExecution(command, this);
    }

    private final class Worker implements Runnable {
        Thread thread;
        Worker next;

        @Override
        public void run() {
            try {
                while (true) {
                    Runnable task = null;
                    boolean blockingTake = false;
                    synchronized (ThreadPoolExecutor.this) {
                        ensureInitialized();
                        if (shutdown) {
                            task = workQueue.poll();
                        } else {
                            blockingTake =
                                    workerCount <= corePoolSize
                                            && !allowCoreTimeout;
                        }
                    }
                    if (task == null && !shutdown) {
                        try {
                            task = blockingTake
                                    ? workQueue.take()
                                    : workQueue.poll(
                                            keepAliveMillis,
                                            TimeUnit.MILLISECONDS);
                        } catch (InterruptedException interruption) {
                            synchronized (ThreadPoolExecutor.this) {
                                if (!shutdown) {
                                    throw interruption;
                                }
                            }
                            continue;
                        }
                    }
                    if (task == null) {
                        synchronized (ThreadPoolExecutor.this) {
                            if (shutdown) {
                                return;
                            }
                            if (workerCount > corePoolSize) {
                                return;
                            }
                        }
                        continue;
                    }
                    synchronized (ThreadPoolExecutor.this) {
                        if (shutdown && task == null) {
                            return;
                        }
                        activeCount++;
                    }
                    Throwable failure = null;
                    try {
                        beforeExecute(thread, task);
                        task.run();
                    } catch (Throwable thrown) {
                        failure = thrown;
                    } finally {
                        synchronized (ThreadPoolExecutor.this) {
                            activeCount--;
                            completedTasks++;
                            afterExecute(task, failure);
                        }
                    }
                }
            } catch (InterruptedException interruption) {
                if (!shutdown) {
                    Thread.currentThread().interrupt();
                }
            } finally {
                synchronized (ThreadPoolExecutor.this) {
                    removeWorker(this);
                    if (shutdown && workerCount == 0) {
                        terminated();
                    }
                }
            }
        }
    }

    public static class AbortPolicy implements RejectedExecutionHandler {
        @Override
        public void rejectedExecution(
                Runnable task,
                ThreadPoolExecutor executor) {
            throw new RejectedExecutionException("task rejected");
        }
    }

    public static class CallerRunsPolicy implements RejectedExecutionHandler {
        @Override
        public void rejectedExecution(
                Runnable task,
                ThreadPoolExecutor executor) {
            if (!executor.isShutdown()) {
                task.run();
            }
        }
    }

    public static class DiscardPolicy implements RejectedExecutionHandler {
        @Override
        public void rejectedExecution(
                Runnable task,
                ThreadPoolExecutor executor) {
        }
    }

    public static class DiscardOldestPolicy
            implements RejectedExecutionHandler {
        @Override
        public void rejectedExecution(
                Runnable task,
                ThreadPoolExecutor executor) {
            if (!executor.isShutdown()) {
                executor.getQueue().poll();
                executor.execute(task);
            }
        }
    }
}
