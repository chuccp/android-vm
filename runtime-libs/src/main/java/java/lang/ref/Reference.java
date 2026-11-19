package java.lang.ref;

import jdk.internal.vm.annotation.ForceInline;
import jdk.internal.HotSpotIntrinsicCandidate;
import jdk.internal.misc.JavaLangRefAccess;
import jdk.internal.misc.SharedSecrets;
import jdk.internal.ref.Cleaner;

public abstract class Reference<T> {

    private T referent;

    volatile ReferenceQueue<? super T> queue;

    @SuppressWarnings("rawtypes")
    volatile Reference next;

    private transient Reference<T> discovered;

    private static class ReferenceHandler extends Thread {

        private static void ensureClassInitialized(Class<?> clazz) {
            try {
                Class.forName(clazz.getName(), true, clazz.getClassLoader());
            } catch (ClassNotFoundException e) {
                throw (Error) new NoClassDefFoundError(e.getMessage()).initCause(e);
            }
        }

        static {

            ensureClassInitialized(Cleaner.class);
        }

        ReferenceHandler(ThreadGroup g, String name) {
            super(g, null, name, 0, false);
        }

        public void run() {
            while (true) {
                processPendingReferences();
            }
        }
    }

    private static native Reference<Object> getAndClearReferencePendingList();

    private static native boolean hasReferencePendingList();

    private static native void waitForReferencePendingList();

    private static final Object processPendingLock = new Object();
    private static boolean processPendingActive = false;

    private static void processPendingReferences() {

        waitForReferencePendingList();
        Reference<Object> pendingList;
        synchronized (processPendingLock) {
            pendingList = getAndClearReferencePendingList();
            processPendingActive = true;
        }
        while (pendingList != null) {
            Reference<Object> ref = pendingList;
            pendingList = ref.discovered;
            ref.discovered = null;

            if (ref instanceof Cleaner) {
                ((Cleaner)ref).clean();

                synchronized (processPendingLock) {
                    processPendingLock.notifyAll();
                }
            } else {
                ReferenceQueue<? super Object> q = ref.queue;
                if (q != ReferenceQueue.NULL) q.enqueue(ref);
            }
        }

        synchronized (processPendingLock) {
            processPendingActive = false;
            processPendingLock.notifyAll();
        }
    }

    private static boolean waitForReferenceProcessing()
        throws InterruptedException
    {
        synchronized (processPendingLock) {
            if (processPendingActive || hasReferencePendingList()) {

                processPendingLock.wait();
                return true;
            } else {
                return false;
            }
        }
    }

    static {
        ThreadGroup tg = Thread.currentThread().getThreadGroup();
        for (ThreadGroup tgn = tg;
             tgn != null;
             tg = tgn, tgn = tg.getParent());
        Thread handler = new ReferenceHandler(tg, "Reference Handler");

        handler.setPriority(Thread.MAX_PRIORITY);
        handler.setDaemon(true);
        handler.start();

        SharedSecrets.setJavaLangRefAccess(new JavaLangRefAccess() {
            @Override
            public boolean waitForReferenceProcessing()
                throws InterruptedException
            {
                return Reference.waitForReferenceProcessing();
            }

            @Override
            public void runFinalization() {
                Finalizer.runFinalization();
            }
        });
    }

    @HotSpotIntrinsicCandidate
    public T get() {
        return this.referent;
    }

    public void clear() {
        this.referent = null;
    }

    public boolean isEnqueued() {
        return (this.queue == ReferenceQueue.ENQUEUED);
    }

    public boolean enqueue() {
        this.referent = null;
        return this.queue.enqueue(this);
    }

    @Override
    protected Object clone() throws CloneNotSupportedException {
        throw new CloneNotSupportedException();
    }

    Reference(T referent) {
        this(referent, null);
    }

    Reference(T referent, ReferenceQueue<? super T> queue) {
        this.referent = referent;
        this.queue = (queue == null) ? ReferenceQueue.NULL : queue;
    }

    @ForceInline
    public static void reachabilityFence(Object ref) {

    }
}
