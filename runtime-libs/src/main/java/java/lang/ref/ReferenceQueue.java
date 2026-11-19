package java.lang.ref;

import java.util.function.Consumer;
import jdk.internal.misc.VM;

public class ReferenceQueue<T> {

    public ReferenceQueue() { }

    private static class Null extends ReferenceQueue<Object> {
        boolean enqueue(Reference<?> r) {
            return false;
        }
    }

    static final ReferenceQueue<Object> NULL = new Null();
    static final ReferenceQueue<Object> ENQUEUED = new Null();

    private static class Lock { };
    private final Lock lock = new Lock();
    private volatile Reference<? extends T> head;
    private long queueLength = 0;

    boolean enqueue(Reference<? extends T> r) {
        synchronized (lock) {

            ReferenceQueue<?> queue = r.queue;
            if ((queue == NULL) || (queue == ENQUEUED)) {
                return false;
            }
            assert queue == this;

            r.next = (head == null) ? r : head;
            head = r;
            queueLength++;

            r.queue = ENQUEUED;
            if (r instanceof FinalReference) {
                VM.addFinalRefCount(1);
            }
            lock.notifyAll();
            return true;
        }
    }

    private Reference<? extends T> reallyPoll() {
        Reference<? extends T> r = head;
        if (r != null) {
            r.queue = NULL;

            @SuppressWarnings("unchecked")
            Reference<? extends T> rn = r.next;

            head = (rn == r) ? null : rn;

            r.next = r;
            queueLength--;
            if (r instanceof FinalReference) {
                VM.addFinalRefCount(-1);
            }
            return r;
        }
        return null;
    }

    public Reference<? extends T> poll() {
        if (head == null)
            return null;
        synchronized (lock) {
            return reallyPoll();
        }
    }

    public Reference<? extends T> remove(long timeout)
        throws IllegalArgumentException, InterruptedException
    {
        if (timeout < 0) {
            throw new IllegalArgumentException("Negative timeout value");
        }
        synchronized (lock) {
            Reference<? extends T> r = reallyPoll();
            if (r != null) return r;
            long start = (timeout == 0) ? 0 : System.nanoTime();
            for (;;) {
                lock.wait(timeout);
                r = reallyPoll();
                if (r != null) return r;
                if (timeout != 0) {
                    long end = System.nanoTime();
                    timeout -= (end - start) / 1000_000;
                    if (timeout <= 0) return null;
                    start = end;
                }
            }
        }
    }

    public Reference<? extends T> remove() throws InterruptedException {
        return remove(0);
    }

    void forEach(Consumer<? super Reference<? extends T>> action) {
        for (Reference<? extends T> r = head; r != null;) {
            action.accept(r);
            @SuppressWarnings("unchecked")
            Reference<? extends T> rn = r.next;
            if (rn == r) {
                if (r.queue == ENQUEUED) {

                    r = null;
                } else {

                    r = head;
                }
            } else {

                r = rn;
            }
        }
    }
}
