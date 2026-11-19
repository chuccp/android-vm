package java.util.concurrent;

import java.util.ArrayList;
import java.util.List;

public class ScheduledThreadPoolExecutor extends ThreadPoolExecutor
        implements ScheduledExecutorService {
    private ScheduleNode firstSchedule;
    private Thread schedulerThread;
    private boolean schedulerShutdown;
    private boolean removeOnCancel;
    private boolean executeDelayedAfterShutdown;
    private boolean continuePeriodicAfterShutdown;

    public ScheduledThreadPoolExecutor(int corePoolSize) {
        this(corePoolSize, null, null);
    }

    public ScheduledThreadPoolExecutor(
            int corePoolSize,
            ThreadFactory threadFactory) {
        this(corePoolSize, threadFactory, null);
    }

    public ScheduledThreadPoolExecutor(
            int corePoolSize,
            RejectedExecutionHandler handler) {
        this(corePoolSize, null, handler);
    }

    public ScheduledThreadPoolExecutor(
            int corePoolSize,
            ThreadFactory threadFactory,
            RejectedExecutionHandler handler) {
        super(
                corePoolSize,
                Integer.MAX_VALUE,
                10L,
                TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>(),
                threadFactory,
                handler);
    }

    @Override
    public ScheduledFuture<?> schedule(
            Runnable command,
            long delay,
            TimeUnit unit) {
        ScheduledTask task = new ScheduledTask(command, null, delay, unit, 0L);
        enqueue(task);
        return task;
    }

    @Override
    public <V> ScheduledFuture<V> schedule(
            Callable<V> callable,
            long delay,
            TimeUnit unit) {
        ScheduledTask<V> task = new ScheduledTask<V>(callable, delay, unit);
        enqueue(task);
        return task;
    }

    @Override
    public ScheduledFuture<?> scheduleAtFixedRate(
            Runnable command,
            long initialDelay,
            long period,
            TimeUnit unit) {
        if (period <= 0) {
            throw new IllegalArgumentException("period must be positive");
        }
        ScheduledTask task = new ScheduledTask(
                command,
                null,
                initialDelay,
                unit,
                period,
                true);
        enqueue(task);
        return task;
    }

    @Override
    public ScheduledFuture<?> scheduleWithFixedDelay(
            Runnable command,
            long initialDelay,
            long delay,
            TimeUnit unit) {
        if (delay <= 0) {
            throw new IllegalArgumentException("delay must be positive");
        }
        ScheduledTask task = new ScheduledTask(
                command,
                null,
                initialDelay,
                unit,
                delay,
                false);
        enqueue(task);
        return task;
    }

    @Override
    public synchronized void shutdown() {
        schedulerShutdown = true;
        if (schedulerThread != null) {
            schedulerThread.interrupt();
        }
        super.shutdown();
        notifyAll();
    }

    @Override
    public synchronized List<Runnable> shutdownNow() {
        schedulerShutdown = true;
        if (schedulerThread != null) {
            schedulerThread.interrupt();
        }
        ArrayList<Runnable> cancelled = new ArrayList<Runnable>();
        for (ScheduleNode node = firstSchedule; node != null; node = node.next) {
            if (!node.task.isCancelled()) {
                node.task.cancel(false);
                cancelled.add(node.task);
            }
        }
        firstSchedule = null;
        List<Runnable> drained = super.shutdownNow();
        drained.addAll(cancelled);
        notifyAll();
        return drained;
    }

    public synchronized void setRemoveOnCancelPolicy(boolean value) {
        removeOnCancel = value;
    }

    public synchronized boolean getRemoveOnCancelPolicy() {
        return removeOnCancel;
    }

    public synchronized void setExecuteExistingDelayedTasksAfterShutdownPolicy(
            boolean value) {
        executeDelayedAfterShutdown = value;
    }

    public synchronized boolean
            getExecuteExistingDelayedTasksAfterShutdownPolicy() {
        return executeDelayedAfterShutdown;
    }

    public synchronized void setContinueExistingPeriodicTasksAfterShutdownPolicy(
            boolean value) {
        continuePeriodicAfterShutdown = value;
    }

    public synchronized boolean
            getContinueExistingPeriodicTasksAfterShutdownPolicy() {
        return continuePeriodicAfterShutdown;
    }

    private synchronized void enqueue(ScheduledTask<?> task) {
        if (isSchedulerShutdown()) {
            throw new RejectedExecutionException("executor is shut down");
        }
        ScheduleNode node = new ScheduleNode(task);
        if (firstSchedule == null
                || node.dueNanos < firstSchedule.dueNanos) {
            node.next = firstSchedule;
            firstSchedule = node;
        } else {
            ScheduleNode current = firstSchedule;
            while (current.next != null
                    && current.next.dueNanos <= node.dueNanos) {
                current = current.next;
            }
            node.next = current.next;
            current.next = node;
        }
        task.node = node;
        ensureScheduler();
        notifyAll();
    }

    private synchronized boolean isSchedulerShutdown() {
        return schedulerShutdown || isShutdown();
    }

    private synchronized void ensureScheduler() {
        if (schedulerThread != null) {
            return;
        }
        schedulerThread = new Thread(new Runnable() {
            @Override
            public void run() {
                runScheduler();
            }
        });
        schedulerThread.setName("guest-scheduled-dispatcher");
        schedulerThread.start();
    }

    private void runScheduler() {
        while (true) {
            ScheduledTask<?> due = null;
            synchronized (this) {
                while (!schedulerShutdown && firstSchedule == null) {
                    try {
                        wait();
                    } catch (InterruptedException interruption) {
                        if (schedulerShutdown) {
                            return;
                        }
                    }
                }
                if (schedulerShutdown) {
                    return;
                }
                long now = System.nanoTime();
                if (firstSchedule.dueNanos > now) {
                    try {
                        TimeUnit.NANOSECONDS.timedWait(
                                this,
                                firstSchedule.dueNanos - now);
                    } catch (InterruptedException interruption) {
                        if (schedulerShutdown) {
                            return;
                        }
                    }
                    continue;
                }
                ScheduleNode node = firstSchedule;
                firstSchedule = node.next;
                node.next = null;
                node.task.node = null;
                due = node.task;
            }
            if (due != null
                    && !due.isCancelled()
                    && !schedulerShutdown) {
                super.execute(due);
            }
        }
    }

    private static final class ScheduleNode {
        final ScheduledTask<?> task;
        final long dueNanos;
        ScheduleNode next;

        ScheduleNode(ScheduledTask<?> task) {
            this.task = task;
            this.dueNanos = task.dueNanos;
        }
    }

    private final class ScheduledTask<T> extends FutureTask<T>
            implements ScheduledFuture<T> {
        private long dueNanos;
        private final long periodNanos;
        private final boolean fixedRate;
        private final boolean periodic;
        private ScheduleNode node;

        ScheduledTask(
                Runnable command,
                T result,
                long delay,
                TimeUnit unit,
                long period) {
            this(command, result, delay, unit, period, false, false);
        }

        ScheduledTask(
                Runnable command,
                T result,
                long delay,
                TimeUnit unit,
                long period,
                boolean fixedRate) {
            this(command, result, delay, unit, period, fixedRate, true);
        }

        private ScheduledTask(
                Runnable command,
                T result,
                long delay,
                TimeUnit unit,
                long period,
                boolean fixedRate,
                boolean periodic) {
            super(command, result);
            dueNanos = System.nanoTime() + unit.toNanos(delay);
            periodNanos = unit.toNanos(period);
            this.fixedRate = fixedRate;
            this.periodic = periodic;
        }

        ScheduledTask(Callable<T> callable, long delay, TimeUnit unit) {
            super(callable);
            dueNanos = System.nanoTime() + unit.toNanos(delay);
            periodNanos = 0L;
            fixedRate = false;
            periodic = false;
        }

        @Override
        public void run() {
            if (!periodic) {
                super.run();
                return;
            }
            if (isCancelled()) {
                return;
            }
            runAndReset();
            if (!isCancelled() && !schedulerShutdown) {
                dueNanos = fixedRate
                        ? dueNanos + periodNanos
                        : System.nanoTime() + periodNanos;
                enqueue(this);
            }
        }

        @Override
        public long getDelay(TimeUnit unit) {
            return unit.convert(
                    dueNanos - System.nanoTime(),
                    TimeUnit.NANOSECONDS);
        }

        @Override
        public int compareTo(Delayed other) {
            long difference = getDelay(TimeUnit.NANOSECONDS)
                    - other.getDelay(TimeUnit.NANOSECONDS);
            return difference < 0 ? -1 : difference > 0 ? 1 : 0;
        }

        @Override
        public boolean cancel(boolean mayInterruptIfRunning) {
            synchronized (ScheduledThreadPoolExecutor.this) {
                if (removeOnCancel && node != null) {
                    removeNode(node);
                    node = null;
                }
            }
            return super.cancel(mayInterruptIfRunning);
        }
    }

    private void removeNode(ScheduleNode target) {
        ScheduleNode previous = null;
        ScheduleNode current = firstSchedule;
        while (current != null) {
            if (current == target) {
                if (previous == null) {
                    firstSchedule = current.next;
                } else {
                    previous.next = current.next;
                }
                current.next = null;
                notifyAll();
                return;
            }
            previous = current;
            current = current.next;
        }
    }
}
