package java.util.concurrent;

public final class Executors {
    private static final ThreadFactory DEFAULT_FACTORY = new ThreadFactory() {
        @Override
        public Thread newThread(Runnable task) {
            Thread thread = new Thread(task);
            thread.setDaemon(true);
            return thread;
        }
    };

    private Executors() {
    }

    public static ExecutorService newFixedThreadPool(int threads) {
        return newFixedThreadPool(threads, DEFAULT_FACTORY);
    }

    public static ExecutorService newFixedThreadPool(
            int threads,
            ThreadFactory threadFactory) {
        return new ThreadPoolExecutor(
                threads,
                threads,
                0L,
                TimeUnit.MILLISECONDS,
                new LinkedBlockingQueue<Runnable>(),
                requiredFactory(threadFactory));
    }

    public static ExecutorService newSingleThreadExecutor() {
        return newFixedThreadPool(1);
    }

    public static ExecutorService newSingleThreadExecutor(
            ThreadFactory threadFactory) {
        return newFixedThreadPool(1, threadFactory);
    }

    public static ExecutorService newCachedThreadPool() {
        return newCachedThreadPool(DEFAULT_FACTORY);
    }

    public static ExecutorService newCachedThreadPool(
            ThreadFactory threadFactory) {
        return new ThreadPoolExecutor(
                0,
                Integer.MAX_VALUE,
                60L,
                TimeUnit.SECONDS,
                new LinkedBlockingQueue<Runnable>(),
                requiredFactory(threadFactory));
    }

    public static ScheduledExecutorService newScheduledThreadPool(int threads) {
        return newScheduledThreadPool(threads, DEFAULT_FACTORY);
    }

    public static ScheduledExecutorService newScheduledThreadPool(
            int threads,
            ThreadFactory threadFactory) {
        return new ScheduledThreadPoolExecutor(
                threads,
                requiredFactory(threadFactory));
    }

    public static ScheduledExecutorService newSingleThreadScheduledExecutor() {
        return newScheduledThreadPool(1);
    }

    public static ScheduledExecutorService
            newSingleThreadScheduledExecutor(ThreadFactory threadFactory) {
        return newScheduledThreadPool(1, threadFactory);
    }

    public static ExecutorService newWorkStealingPool() {
        return newFixedThreadPool(
                Math.max(1, Runtime.getRuntime().availableProcessors()));
    }

    public static ExecutorService newWorkStealingPool(int parallelism) {
        return newFixedThreadPool(parallelism);
    }

    public static ExecutorService unconfigurableExecutorService(
            ExecutorService executor) {
        if (executor == null) {
            throw new NullPointerException("executor");
        }
        return executor;
    }

    public static ScheduledExecutorService unconfigurableScheduledExecutorService(
            ScheduledExecutorService executor) {
        if (executor == null) {
            throw new NullPointerException("executor");
        }
        return executor;
    }

    public static ThreadFactory defaultThreadFactory() {
        return DEFAULT_FACTORY;
    }

    public static ThreadFactory privilegedThreadFactory() {
        return DEFAULT_FACTORY;
    }

    private static ThreadFactory requiredFactory(ThreadFactory factory) {
        if (factory == null) {
            throw new NullPointerException("threadFactory");
        }
        return factory;
    }

    public static <T> Callable<T> callable(Runnable task, T result) {
        if (task == null) {
            throw new NullPointerException("task");
        }
        return new Callable<T>() {
            @Override
            public T call() {
                task.run();
                return result;
            }
        };
    }

    public static Callable<Object> callable(Runnable task) {
        return callable(task, null);
    }

    public static Callable<Object> callable(
            java.security.PrivilegedAction<?> action) {
        return new Callable<Object>() {
            @Override
            public Object call() {
                return action.run();
            }
        };
    }
}
