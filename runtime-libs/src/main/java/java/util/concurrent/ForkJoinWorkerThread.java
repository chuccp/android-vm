package java.util.concurrent;

import java.security.AccessControlContext;
import java.security.AccessController;
import java.security.PrivilegedAction;
import java.security.ProtectionDomain;

public class ForkJoinWorkerThread extends Thread {

    final ForkJoinPool pool;
    final ForkJoinPool.WorkQueue workQueue;

    private static final AccessControlContext INNOCUOUS_ACC =
        new AccessControlContext(
            new ProtectionDomain[] { new ProtectionDomain(null, null) });

    protected ForkJoinWorkerThread(ForkJoinPool pool) {

        super("aForkJoinWorkerThread");
        this.pool = pool;
        this.workQueue = pool.registerWorker(this);
    }

    ForkJoinWorkerThread(ForkJoinPool pool, ClassLoader ccl) {
        super("aForkJoinWorkerThread");
        super.setContextClassLoader(ccl);
        ThreadLocalRandom.setInheritedAccessControlContext(this, INNOCUOUS_ACC);
        this.pool = pool;
        this.workQueue = pool.registerWorker(this);
    }

    ForkJoinWorkerThread(ForkJoinPool pool,
                         ClassLoader ccl,
                         ThreadGroup threadGroup,
                         AccessControlContext acc) {
        super(threadGroup, null, "aForkJoinWorkerThread");
        super.setContextClassLoader(ccl);
        ThreadLocalRandom.setInheritedAccessControlContext(this, acc);
        ThreadLocalRandom.eraseThreadLocals(this);
        this.pool = pool;
        this.workQueue = pool.registerWorker(this);
    }

    public ForkJoinPool getPool() {
        return pool;
    }

    public int getPoolIndex() {
        return workQueue.getPoolIndex();
    }

    protected void onStart() {
    }

    protected void onTermination(Throwable exception) {
    }

    public void run() {
        if (workQueue.array == null) {
            Throwable exception = null;
            try {
                onStart();
                pool.runWorker(workQueue);
            } catch (Throwable ex) {
                exception = ex;
            } finally {
                try {
                    onTermination(exception);
                } catch (Throwable ex) {
                    if (exception == null)
                        exception = ex;
                } finally {
                    pool.deregisterWorker(this, exception);
                }
            }
        }
    }

    void afterTopLevelExec() {
    }

    static final class InnocuousForkJoinWorkerThread extends ForkJoinWorkerThread {

        private static final ThreadGroup innocuousThreadGroup =
            AccessController.doPrivileged(new PrivilegedAction<>() {
                public ThreadGroup run() {
                    ThreadGroup group = Thread.currentThread().getThreadGroup();
                    for (ThreadGroup p; (p = group.getParent()) != null; )
                        group = p;
                    return new ThreadGroup(
                        group, "InnocuousForkJoinWorkerThreadGroup");
                }});

        InnocuousForkJoinWorkerThread(ForkJoinPool pool) {
            super(pool,
                  ClassLoader.getSystemClassLoader(),
                  innocuousThreadGroup,
                  INNOCUOUS_ACC);
        }

        @Override
        void afterTopLevelExec() {
            ThreadLocalRandom.eraseThreadLocals(this);
        }

        @Override
        public void setUncaughtExceptionHandler(UncaughtExceptionHandler x) { }

        @Override
        public void setContextClassLoader(ClassLoader cl) {
            throw new SecurityException("setContextClassLoader");
        }
    }
}
