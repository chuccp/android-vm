package java.util.concurrent;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.locks.LockSupport;
import java.util.function.BiConsumer;
import java.util.function.BiPredicate;
import java.util.function.Consumer;
import static java.util.concurrent.Flow.Publisher;
import static java.util.concurrent.Flow.Subscriber;
import static java.util.concurrent.Flow.Subscription;

public class SubmissionPublisher<T> implements Publisher<T>,
                                               AutoCloseable {

    static final int BUFFER_CAPACITY_LIMIT = 1 << 30;

    static final int INITIAL_CAPACITY = 32;

    static final int roundCapacity(int cap) {
        int n = cap - 1;
        n |= n >>> 1;
        n |= n >>> 2;
        n |= n >>> 4;
        n |= n >>> 8;
        n |= n >>> 16;
        return (n <= 0) ? 1 :
            (n >= BUFFER_CAPACITY_LIMIT) ? BUFFER_CAPACITY_LIMIT : n + 1;
    }

    private static final Executor ASYNC_POOL =
        (ForkJoinPool.getCommonPoolParallelism() > 1) ?
        ForkJoinPool.commonPool() : new ThreadPerTaskExecutor();

    private static final class ThreadPerTaskExecutor implements Executor {
        ThreadPerTaskExecutor() {}
        public void execute(Runnable r) { new Thread(r).start(); }
    }

    BufferedSubscription<T> clients;

    volatile boolean closed;

    boolean subscribed;

    Thread owner;

    volatile Throwable closedException;

    final Executor executor;
    final BiConsumer<? super Subscriber<? super T>, ? super Throwable> onNextHandler;
    final int maxBufferCapacity;

    public SubmissionPublisher(Executor executor, int maxBufferCapacity,
                               BiConsumer<? super Subscriber<? super T>, ? super Throwable> handler) {
        if (executor == null)
            throw new NullPointerException();
        if (maxBufferCapacity <= 0)
            throw new IllegalArgumentException("capacity must be positive");
        this.executor = executor;
        this.onNextHandler = handler;
        this.maxBufferCapacity = roundCapacity(maxBufferCapacity);
    }

    public SubmissionPublisher(Executor executor, int maxBufferCapacity) {
        this(executor, maxBufferCapacity, null);
    }

    public SubmissionPublisher() {
        this(ASYNC_POOL, Flow.defaultBufferSize(), null);
    }

    public void subscribe(Subscriber<? super T> subscriber) {
        if (subscriber == null) throw new NullPointerException();
        int max = maxBufferCapacity;
        Object[] array = new Object[max < INITIAL_CAPACITY ?
                                    max : INITIAL_CAPACITY];
        BufferedSubscription<T> subscription =
            new BufferedSubscription<T>(subscriber, executor, onNextHandler,
                                        array, max);
        synchronized (this) {
            if (!subscribed) {
                subscribed = true;
                owner = Thread.currentThread();
            }
            for (BufferedSubscription<T> b = clients, pred = null;;) {
                if (b == null) {
                    Throwable ex;
                    subscription.onSubscribe();
                    if ((ex = closedException) != null)
                        subscription.onError(ex);
                    else if (closed)
                        subscription.onComplete();
                    else if (pred == null)
                        clients = subscription;
                    else
                        pred.next = subscription;
                    break;
                }
                BufferedSubscription<T> next = b.next;
                if (b.isClosed()) {
                    b.next = null;
                    if (pred == null)
                        clients = next;
                    else
                        pred.next = next;
                }
                else if (subscriber.equals(b.subscriber)) {
                    b.onError(new IllegalStateException("Duplicate subscribe"));
                    break;
                }
                else
                    pred = b;
                b = next;
            }
        }
    }

    private int doOffer(T item, long nanos,
                        BiPredicate<Subscriber<? super T>, ? super T> onDrop) {
        if (item == null) throw new NullPointerException();
        int lag = 0;
        boolean complete, unowned;
        synchronized (this) {
            Thread t = Thread.currentThread(), o;
            BufferedSubscription<T> b = clients;
            if ((unowned = ((o = owner) != t)) && o != null)
                owner = null;
            if (b == null)
                complete = closed;
            else {
                complete = false;
                boolean cleanMe = false;
                BufferedSubscription<T> retries = null, rtail = null, next;
                do {
                    next = b.next;
                    int stat = b.offer(item, unowned);
                    if (stat == 0) {
                        b.nextRetry = null;
                        if (rtail == null)
                            retries = b;
                        else
                            rtail.nextRetry = b;
                        rtail = b;
                    }
                    else if (stat < 0)
                        cleanMe = true;
                    else if (stat > lag)
                        lag = stat;
                } while ((b = next) != null);

                if (retries != null || cleanMe)
                    lag = retryOffer(item, nanos, onDrop, retries, lag, cleanMe);
            }
        }
        if (complete)
            throw new IllegalStateException("Closed");
        else
            return lag;
    }

    private int retryOffer(T item, long nanos,
                           BiPredicate<Subscriber<? super T>, ? super T> onDrop,
                           BufferedSubscription<T> retries, int lag,
                           boolean cleanMe) {
        for (BufferedSubscription<T> r = retries; r != null;) {
            BufferedSubscription<T> nextRetry = r.nextRetry;
            r.nextRetry = null;
            if (nanos > 0L)
                r.awaitSpace(nanos);
            int stat = r.retryOffer(item);
            if (stat == 0 && onDrop != null && onDrop.test(r.subscriber, item))
                stat = r.retryOffer(item);
            if (stat == 0)
                lag = (lag >= 0) ? -1 : lag - 1;
            else if (stat < 0)
                cleanMe = true;
            else if (lag >= 0 && stat > lag)
                lag = stat;
            r = nextRetry;
        }
        if (cleanMe)
            cleanAndCount();
        return lag;
    }

    private int cleanAndCount() {
        int count = 0;
        BufferedSubscription<T> pred = null, next;
        for (BufferedSubscription<T> b = clients; b != null; b = next) {
            next = b.next;
            if (b.isClosed()) {
                b.next = null;
                if (pred == null)
                    clients = next;
                else
                    pred.next = next;
            }
            else {
                pred = b;
                ++count;
            }
        }
        return count;
    }

    public int submit(T item) {
        return doOffer(item, Long.MAX_VALUE, null);
    }

    public int offer(T item,
                     BiPredicate<Subscriber<? super T>, ? super T> onDrop) {
        return doOffer(item, 0L, onDrop);
    }

    public int offer(T item, long timeout, TimeUnit unit,
                     BiPredicate<Subscriber<? super T>, ? super T> onDrop) {
        long nanos = unit.toNanos(timeout);

        if (nanos == Long.MAX_VALUE) --nanos;
        return doOffer(item, nanos, onDrop);
    }

    public void close() {
        if (!closed) {
            BufferedSubscription<T> b;
            synchronized (this) {

                b = clients;
                clients = null;
                owner = null;
                closed = true;
            }
            while (b != null) {
                BufferedSubscription<T> next = b.next;
                b.next = null;
                b.onComplete();
                b = next;
            }
        }
    }

    public void closeExceptionally(Throwable error) {
        if (error == null)
            throw new NullPointerException();
        if (!closed) {
            BufferedSubscription<T> b;
            synchronized (this) {
                b = clients;
                if (!closed) {
                    closedException = error;
                    clients = null;
                    owner = null;
                    closed = true;
                }
            }
            while (b != null) {
                BufferedSubscription<T> next = b.next;
                b.next = null;
                b.onError(error);
                b = next;
            }
        }
    }

    public boolean isClosed() {
        return closed;
    }

    public Throwable getClosedException() {
        return closedException;
    }

    public boolean hasSubscribers() {
        boolean nonEmpty = false;
        synchronized (this) {
            for (BufferedSubscription<T> b = clients; b != null;) {
                BufferedSubscription<T> next = b.next;
                if (b.isClosed()) {
                    b.next = null;
                    b = clients = next;
                }
                else {
                    nonEmpty = true;
                    break;
                }
            }
        }
        return nonEmpty;
    }

    public int getNumberOfSubscribers() {
        synchronized (this) {
            return cleanAndCount();
        }
    }

    public Executor getExecutor() {
        return executor;
    }

    public int getMaxBufferCapacity() {
        return maxBufferCapacity;
    }

    public List<Subscriber<? super T>> getSubscribers() {
        ArrayList<Subscriber<? super T>> subs = new ArrayList<>();
        synchronized (this) {
            BufferedSubscription<T> pred = null, next;
            for (BufferedSubscription<T> b = clients; b != null; b = next) {
                next = b.next;
                if (b.isClosed()) {
                    b.next = null;
                    if (pred == null)
                        clients = next;
                    else
                        pred.next = next;
                }
                else {
                    subs.add(b.subscriber);
                    pred = b;
                }
            }
        }
        return subs;
    }

    public boolean isSubscribed(Subscriber<? super T> subscriber) {
        if (subscriber == null) throw new NullPointerException();
        if (!closed) {
            synchronized (this) {
                BufferedSubscription<T> pred = null, next;
                for (BufferedSubscription<T> b = clients; b != null; b = next) {
                    next = b.next;
                    if (b.isClosed()) {
                        b.next = null;
                        if (pred == null)
                            clients = next;
                        else
                            pred.next = next;
                    }
                    else if (subscriber.equals(b.subscriber))
                        return true;
                    else
                        pred = b;
                }
            }
        }
        return false;
    }

    public long estimateMinimumDemand() {
        long min = Long.MAX_VALUE;
        boolean nonEmpty = false;
        synchronized (this) {
            BufferedSubscription<T> pred = null, next;
            for (BufferedSubscription<T> b = clients; b != null; b = next) {
                int n; long d;
                next = b.next;
                if ((n = b.estimateLag()) < 0) {
                    b.next = null;
                    if (pred == null)
                        clients = next;
                    else
                        pred.next = next;
                }
                else {
                    if ((d = b.demand - n) < min)
                        min = d;
                    nonEmpty = true;
                    pred = b;
                }
            }
        }
        return nonEmpty ? min : 0;
    }

    public int estimateMaximumLag() {
        int max = 0;
        synchronized (this) {
            BufferedSubscription<T> pred = null, next;
            for (BufferedSubscription<T> b = clients; b != null; b = next) {
                int n;
                next = b.next;
                if ((n = b.estimateLag()) < 0) {
                    b.next = null;
                    if (pred == null)
                        clients = next;
                    else
                        pred.next = next;
                }
                else {
                    if (n > max)
                        max = n;
                    pred = b;
                }
            }
        }
        return max;
    }

    public CompletableFuture<Void> consume(Consumer<? super T> consumer) {
        if (consumer == null)
            throw new NullPointerException();
        CompletableFuture<Void> status = new CompletableFuture<>();
        subscribe(new ConsumerSubscriber<T>(status, consumer));
        return status;
    }

    static final class ConsumerSubscriber<T> implements Subscriber<T> {
        final CompletableFuture<Void> status;
        final Consumer<? super T> consumer;
        Subscription subscription;
        ConsumerSubscriber(CompletableFuture<Void> status,
                           Consumer<? super T> consumer) {
            this.status = status; this.consumer = consumer;
        }
        public final void onSubscribe(Subscription subscription) {
            this.subscription = subscription;
            status.whenComplete((v, e) -> subscription.cancel());
            if (!status.isDone())
                subscription.request(Long.MAX_VALUE);
        }
        public final void onError(Throwable ex) {
            status.completeExceptionally(ex);
        }
        public final void onComplete() {
            status.complete(null);
        }
        public final void onNext(T item) {
            try {
                consumer.accept(item);
            } catch (Throwable ex) {
                subscription.cancel();
                status.completeExceptionally(ex);
            }
        }
    }

    @SuppressWarnings("serial")
    static final class ConsumerTask<T> extends ForkJoinTask<Void>
        implements Runnable, CompletableFuture.AsynchronousCompletionTask {
        final BufferedSubscription<T> consumer;
        ConsumerTask(BufferedSubscription<T> consumer) {
            this.consumer = consumer;
        }
        public final Void getRawResult() { return null; }
        public final void setRawResult(Void v) {}
        public final boolean exec() { consumer.consume(); return false; }
        public final void run() { consumer.consume(); }
    }

    @SuppressWarnings("serial")
    @jdk.internal.vm.annotation.Contended
    static final class BufferedSubscription<T>
        implements Subscription, ForkJoinPool.ManagedBlocker {
        long timeout;
        int head;
        int tail;
        final int maxCapacity;
        volatile int ctl;
        Object[] array;
        final Subscriber<? super T> subscriber;
        final BiConsumer<? super Subscriber<? super T>, ? super Throwable> onNextHandler;
        Executor executor;
        Thread waiter;
        Throwable pendingError;
        BufferedSubscription<T> next;
        BufferedSubscription<T> nextRetry;

        @jdk.internal.vm.annotation.Contended("c")
        volatile long demand;
        @jdk.internal.vm.annotation.Contended("c")
        volatile int waiting;

        static final int CLOSED   = 0x01;
        static final int ACTIVE   = 0x02;
        static final int REQS     = 0x04;
        static final int ERROR    = 0x08;
        static final int COMPLETE = 0x10;
        static final int RUN      = 0x20;
        static final int OPEN     = 0x40;

        static final long INTERRUPTED = -1L;

        BufferedSubscription(Subscriber<? super T> subscriber,
                             Executor executor,
                             BiConsumer<? super Subscriber<? super T>,
                             ? super Throwable> onNextHandler,
                             Object[] array,
                             int maxBufferCapacity) {
            this.subscriber = subscriber;
            this.executor = executor;
            this.onNextHandler = onNextHandler;
            this.array = array;
            this.maxCapacity = maxBufferCapacity;
        }

        final boolean weakCasCtl(int cmp, int val) {
            return CTL.weakCompareAndSet(this, cmp, val);
        }

        final int getAndBitwiseOrCtl(int bits) {
            return (int)CTL.getAndBitwiseOr(this, bits);
        }

        final long subtractDemand(int k) {
            long n = (long)(-k);
            return n + (long)DEMAND.getAndAdd(this, n);
        }

        final boolean casDemand(long cmp, long val) {
            return DEMAND.compareAndSet(this, cmp, val);
        }

        final boolean isClosed() {
            return (ctl & CLOSED) != 0;
        }

        final int estimateLag() {
            int c = ctl, n = tail - head;
            return ((c & CLOSED) != 0) ? -1 : (n < 0) ? 0 : n;
        }

        final int offer(T item, boolean unowned) {
            Object[] a;
            int stat = 0, cap = ((a = array) == null) ? 0 : a.length;
            int t = tail, i = t & (cap - 1), n = t + 1 - head;
            if (cap > 0) {
                boolean added;
                if (n >= cap && cap < maxCapacity)
                    added = growAndOffer(item, a, t);
                else if (n >= cap || unowned)
                    added = QA.compareAndSet(a, i, null, item);
                else {
                    QA.setRelease(a, i, item);
                    added = true;
                }
                if (added) {
                    tail = t + 1;
                    stat = n;
                }
            }
            return startOnOffer(stat);
        }

        final boolean growAndOffer(T item, Object[] a, int t) {
            int cap = 0, newCap = 0;
            Object[] newArray = null;
            if (a != null && (cap = a.length) > 0 && (newCap = cap << 1) > 0) {
                try {
                    newArray = new Object[newCap];
                } catch (OutOfMemoryError ex) {
                }
            }
            if (newArray == null)
                return false;
            else {
                int newMask = newCap - 1;
                newArray[t-- & newMask] = item;
                for (int mask = cap - 1, k = mask; k >= 0; --k) {
                    Object x = QA.getAndSet(a, t & mask, null);
                    if (x == null)
                        break;
                    else
                        newArray[t-- & newMask] = x;
                }
                array = newArray;
                VarHandle.releaseFence();
                return true;
            }
        }

        final int retryOffer(T item) {
            Object[] a;
            int stat = 0, t = tail, h = head, cap;
            if ((a = array) != null && (cap = a.length) > 0 &&
                QA.compareAndSet(a, (cap - 1) & t, null, item))
                stat = (tail = t + 1) - h;
            return startOnOffer(stat);
        }

        final int startOnOffer(int stat) {
            int c;
            if (((c = ctl) & (REQS | ACTIVE)) == REQS &&
                ((c = getAndBitwiseOrCtl(RUN | ACTIVE)) & (RUN | CLOSED)) == 0)
                tryStart();
            else if ((c & CLOSED) != 0)
                stat = -1;
            return stat;
        }

        final void tryStart() {
            try {
                Executor e;
                ConsumerTask<T> task = new ConsumerTask<T>(this);
                if ((e = executor) != null)
                    e.execute(task);
            } catch (RuntimeException | Error ex) {
                getAndBitwiseOrCtl(ERROR | CLOSED);
                throw ex;
            }
        }

        final void startOnSignal(int bits) {
            if ((ctl & bits) != bits &&
                (getAndBitwiseOrCtl(bits) & (RUN | CLOSED)) == 0)
                tryStart();
        }

        final void onSubscribe() {
            startOnSignal(RUN | ACTIVE);
        }

        final void onComplete() {
            startOnSignal(RUN | ACTIVE | COMPLETE);
        }

        final void onError(Throwable ex) {
            int c; Object[] a;
            if (ex != null)
                pendingError = ex;
            if (((c = getAndBitwiseOrCtl(ERROR | RUN | ACTIVE)) & CLOSED) == 0) {
                if ((c & RUN) == 0)
                    tryStart();
                else if ((a = array) != null)
                    Arrays.fill(a, null);
            }
        }

        public final void cancel() {
            onError(null);
        }

        public final void request(long n) {
            if (n > 0L) {
                for (;;) {
                    long p = demand, d = p + n;
                    if (casDemand(p, d < p ? Long.MAX_VALUE : d))
                        break;
                }
                startOnSignal(RUN | ACTIVE | REQS);
            }
            else
                onError(new IllegalArgumentException(
                            "non-positive subscription request"));
        }

        final void consume() {
            Subscriber<? super T> s;
            if ((s = subscriber) != null) {
                subscribeOnOpen(s);
                long d = demand;
                for (int h = head, t = tail;;) {
                    int c, taken; boolean empty;
                    if (((c = ctl) & ERROR) != 0) {
                        closeOnError(s, null);
                        break;
                    }
                    else if ((taken = takeItems(s, d, h)) > 0) {
                        head = h += taken;
                        d = subtractDemand(taken);
                    }
                    else if ((d = demand) == 0L && (c & REQS) != 0)
                        weakCasCtl(c, c & ~REQS);
                    else if (d != 0L && (c & REQS) == 0)
                        weakCasCtl(c, c | REQS);
                    else if (t == (t = tail)) {
                        if ((empty = (t == h)) && (c & COMPLETE) != 0) {
                            closeOnComplete(s);
                            break;
                        }
                        else if (empty || d == 0L) {
                            int bit = ((c & ACTIVE) != 0) ? ACTIVE : RUN;
                            if (weakCasCtl(c, c & ~bit) && bit == RUN)
                                break;
                        }
                    }
                }
            }
        }

        final int takeItems(Subscriber<? super T> s, long d, int h) {
            Object[] a;
            int k = 0, cap;
            if ((a = array) != null && (cap = a.length) > 0) {
                int m = cap - 1, b = (m >>> 3) + 1;
                int n = (d < (long)b) ? (int)d : b;
                for (; k < n; ++h, ++k) {
                    Object x = QA.getAndSet(a, h & m, null);
                    if (waiting != 0)
                        signalWaiter();
                    if (x == null)
                        break;
                    else if (!consumeNext(s, x))
                        break;
                }
            }
            return k;
        }

        final boolean consumeNext(Subscriber<? super T> s, Object x) {
            try {
                @SuppressWarnings("unchecked") T y = (T) x;
                if (s != null)
                    s.onNext(y);
                return true;
            } catch (Throwable ex) {
                handleOnNext(s, ex);
                return false;
            }
        }

        final void handleOnNext(Subscriber<? super T> s, Throwable ex) {
            BiConsumer<? super Subscriber<? super T>, ? super Throwable> h;
            try {
                if ((h = onNextHandler) != null)
                    h.accept(s, ex);
            } catch (Throwable ignore) {
            }
            closeOnError(s, ex);
        }

        final void subscribeOnOpen(Subscriber<? super T> s) {
            if ((ctl & OPEN) == 0 && (getAndBitwiseOrCtl(OPEN) & OPEN) == 0)
                consumeSubscribe(s);
        }

        final void consumeSubscribe(Subscriber<? super T> s) {
            try {
                if (s != null)
                    s.onSubscribe(this);
            } catch (Throwable ex) {
                closeOnError(s, ex);
            }
        }

        final void closeOnComplete(Subscriber<? super T> s) {
            if ((getAndBitwiseOrCtl(CLOSED) & CLOSED) == 0)
                consumeComplete(s);
        }

        final void consumeComplete(Subscriber<? super T> s) {
            try {
                if (s != null)
                    s.onComplete();
            } catch (Throwable ignore) {
            }
        }

        final void closeOnError(Subscriber<? super T> s, Throwable ex) {
            if ((getAndBitwiseOrCtl(ERROR | CLOSED) & CLOSED) == 0) {
                if (ex == null)
                    ex = pendingError;
                pendingError = null;
                executor = null;
                signalWaiter();
                consumeError(s, ex);
            }
        }

        final void consumeError(Subscriber<? super T> s, Throwable ex) {
            try {
                if (ex != null && s != null)
                    s.onError(ex);
            } catch (Throwable ignore) {
            }
        }

        final void signalWaiter() {
            Thread w;
            waiting = 0;
            if ((w = waiter) != null)
                LockSupport.unpark(w);
        }

        public final boolean isReleasable() {
            Object[] a; int cap;
            return ((ctl & CLOSED) != 0 ||
                    ((a = array) != null && (cap = a.length) > 0 &&
                     QA.getAcquire(a, (cap - 1) & tail) == null));
        }

        final void awaitSpace(long nanos) {
            if (!isReleasable()) {
                ForkJoinPool.helpAsyncBlocker(executor, this);
                if (!isReleasable()) {
                    timeout = nanos;
                    try {
                        ForkJoinPool.managedBlock(this);
                    } catch (InterruptedException ie) {
                        timeout = INTERRUPTED;
                    }
                    if (timeout == INTERRUPTED)
                        Thread.currentThread().interrupt();
                }
            }
        }

        public final boolean block() {
            long nanos = timeout;
            boolean timed = (nanos < Long.MAX_VALUE);
            long deadline = timed ? System.nanoTime() + nanos : 0L;
            while (!isReleasable()) {
                if (Thread.interrupted()) {
                    timeout = INTERRUPTED;
                    if (timed)
                        break;
                }
                else if (timed && (nanos = deadline - System.nanoTime()) <= 0L)
                    break;
                else if (waiter == null)
                    waiter = Thread.currentThread();
                else if (waiting == 0)
                    waiting = 1;
                else if (timed)
                    LockSupport.parkNanos(this, nanos);
                else
                    LockSupport.park(this);
            }
            waiter = null;
            waiting = 0;
            return true;
        }

        static final VarHandle CTL;
        static final VarHandle DEMAND;
        static final VarHandle QA;

        static {
            try {
                MethodHandles.Lookup l = MethodHandles.lookup();
                CTL = l.findVarHandle(BufferedSubscription.class, "ctl",
                                      int.class);
                DEMAND = l.findVarHandle(BufferedSubscription.class, "demand",
                                         long.class);
                QA = MethodHandles.arrayElementVarHandle(Object[].class);
            } catch (ReflectiveOperationException e) {
                throw new ExceptionInInitializerError(e);
            }

            Class<?> ensureLoaded = LockSupport.class;
        }
    }
}
