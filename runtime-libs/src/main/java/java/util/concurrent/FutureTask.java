package java.util.concurrent;

public class FutureTask<V> implements RunnableFuture<V> {
    private static final int NEW = 0;
    private static final int RUNNING = 1;
    private static final int COMPLETING = 2;
    private static final int NORMAL = 3;
    private static final int EXCEPTIONAL = 4;
    private static final int CANCELLED = 5;

    private final Object completion = new Object();
    private volatile int state = NEW;
    private Callable<V> callable;
    private Object outcome;
    private volatile Thread runner;

    public FutureTask(Callable<V> callable) {
        if (callable == null) {
            throw new NullPointerException("callable");
        }
        this.callable = callable;
    }

    public FutureTask(Runnable runnable, V result) {
        if (runnable == null) {
            throw new NullPointerException("runnable");
        }
        this.callable = new RunnableAdapter<V>(runnable, result);
    }

    @Override
    public boolean isCancelled() {
        return state == CANCELLED;
    }

    @Override
    public boolean isDone() {
        int current = state;
        return current == NORMAL
                || current == EXCEPTIONAL
                || current == CANCELLED;
    }

    @Override
    public boolean cancel(boolean mayInterruptIfRunning) {
        Thread active = null;
        synchronized (completion) {
            if (state != NEW && state != RUNNING) {
                return false;
            }
            state = CANCELLED;
            callable = null;
            active = runner;
            completion.notifyAll();
        }
        if (mayInterruptIfRunning && active != null) {
            active.interrupt();
        }
        done();
        return true;
    }

    @Override
    public V get() throws InterruptedException, ExecutionException {
        synchronized (completion) {
            while (state == NEW || state == RUNNING || state == COMPLETING) {
                completion.wait();
            }
        }
        return report();
    }

    @Override
    public V get(long timeout, TimeUnit unit)
            throws InterruptedException, ExecutionException, TimeoutException {
        long remaining = unit.toNanos(timeout);
        long deadline = System.nanoTime() + remaining;
        synchronized (completion) {
            while (state == NEW || state == RUNNING || state == COMPLETING) {
                if (remaining <= 0) {
                    throw new TimeoutException();
                }
                TimeUnit.NANOSECONDS.timedWait(completion, remaining);
                remaining = deadline - System.nanoTime();
            }
        }
        return report();
    }

    @Override
    public void run() {
        synchronized (completion) {
            if (state != NEW) {
                return;
            }
            state = RUNNING;
            runner = Thread.currentThread();
        }
        try {
            V result = callable.call();
            set(result);
        } catch (Throwable failure) {
            setException(failure);
        } finally {
            synchronized (completion) {
                runner = null;
                callable = null;
            }
        }
    }

    protected void set(V value) {
        synchronized (completion) {
            if (state != RUNNING) {
                return;
            }
            outcome = value;
            state = COMPLETING;
            state = NORMAL;
            completion.notifyAll();
        }
        done();
    }

    protected void setException(Throwable failure) {
        synchronized (completion) {
            if (state != RUNNING) {
                return;
            }
            outcome = failure;
            state = COMPLETING;
            state = EXCEPTIONAL;
            completion.notifyAll();
        }
        done();
    }

    protected void done() {
    }

    protected boolean runAndReset() {
        synchronized (completion) {
            if (state != NEW) {
                return false;
            }
            state = RUNNING;
            runner = Thread.currentThread();
        }
        boolean completed = false;
        try {
            callable.call();
            completed = true;
        } catch (Throwable ignored) {
        } finally {
            synchronized (completion) {
                runner = null;
                if (completed) {
                    state = NEW;
                } else {
                    outcome = new ExecutionException();
                    state = EXCEPTIONAL;
                    completion.notifyAll();
                }
            }
        }
        return completed;
    }

    @SuppressWarnings("unchecked")
    private V report() throws ExecutionException {
        int current = state;
        if (current == CANCELLED) {
            throw new CancellationException();
        }
        if (current == EXCEPTIONAL) {
            throw new ExecutionException((Throwable) outcome);
        }
        return (V) outcome;
    }

    private static final class RunnableAdapter<T> implements Callable<T> {
        private final Runnable task;
        private final T result;

        RunnableAdapter(Runnable task, T result) {
            this.task = task;
            this.result = result;
        }

        @Override
        public T call() {
            task.run();
            return result;
        }
    }
}
