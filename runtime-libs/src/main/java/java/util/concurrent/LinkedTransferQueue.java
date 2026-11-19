package java.util.concurrent;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.AbstractQueue;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.Queue;
import java.util.Spliterator;
import java.util.Spliterators;
import java.util.concurrent.locks.LockSupport;
import java.util.function.Consumer;
import java.util.function.Predicate;

public class LinkedTransferQueue<E> extends AbstractQueue<E>
    implements TransferQueue<E>, java.io.Serializable {
    private static final long serialVersionUID = -3223113410248163686L;

    private static final boolean MP =
        Runtime.getRuntime().availableProcessors() > 1;

    private static final int FRONT_SPINS   = 1 << 7;

    private static final int CHAINED_SPINS = FRONT_SPINS >>> 1;

    static final int SWEEP_THRESHOLD = 32;

    static final class Node {
        final boolean isData;
        volatile Object item;
        volatile Node next;
        volatile Thread waiter;

        Node(Object item) {
            ITEM.set(this, item);
            isData = (item != null);
        }

        Node() {
            isData = true;
        }

        final boolean casNext(Node cmp, Node val) {

            return NEXT.compareAndSet(this, cmp, val);
        }

        final boolean casItem(Object cmp, Object val) {

            return ITEM.compareAndSet(this, cmp, val);
        }

        final void selfLink() {

            NEXT.setRelease(this, this);
        }

        final void appendRelaxed(Node next) {

            NEXT.set(this, next);
        }

        final void forgetContents() {

            if (!isData)
                ITEM.set(this, this);
            WAITER.set(this, null);
        }

        final boolean isMatched() {
            return isData == (item == null);
        }

        final boolean tryMatch(Object cmp, Object val) {
            if (casItem(cmp, val)) {
                LockSupport.unpark(waiter);
                return true;
            }
            return false;
        }

        final boolean cannotPrecede(boolean haveData) {
            boolean d = isData;
            return d != haveData && d != (item == null);
        }

        private static final long serialVersionUID = -3375979862319811754L;
    }

    transient volatile Node head;

    private transient volatile Node tail;

    private transient volatile int sweepVotes;

    private boolean casTail(Node cmp, Node val) {

        return TAIL.compareAndSet(this, cmp, val);
    }

    private boolean casHead(Node cmp, Node val) {
        return HEAD.compareAndSet(this, cmp, val);
    }

    private int incSweepVotes() {
        return (int) SWEEPVOTES.getAndAdd(this, 1) + 1;
    }

    private boolean tryCasSuccessor(Node pred, Node c, Node p) {

        if (pred != null)
            return pred.casNext(c, p);
        if (casHead(c, p)) {
            c.selfLink();
            return true;
        }
        return false;
    }

    private Node skipDeadNodes(Node pred, Node c, Node p, Node q) {

        if (q == null) {

            if (c == p) return pred;
            q = p;
        }
        return (tryCasSuccessor(pred, c, q)
                && (pred == null || !pred.isMatched()))
            ? pred : p;
    }

    private void skipDeadNodesNearHead(Node h, Node p) {

        for (;;) {
            final Node q;
            if ((q = p.next) == null) break;
            else if (!q.isMatched()) { p = q; break; }
            else if (p == (p = q)) return;
        }
        if (casHead(h, p))
            h.selfLink();
    }

    private static final int NOW   = 0;
    private static final int ASYNC = 1;
    private static final int SYNC  = 2;
    private static final int TIMED = 3;

    @SuppressWarnings("unchecked")
    private E xfer(E e, boolean haveData, int how, long nanos) {
        if (haveData && (e == null))
            throw new NullPointerException();

        restart: for (Node s = null, t = null, h = null;;) {
            for (Node p = (t != (t = tail) && t.isData == haveData) ? t
                     : (h = head);; ) {
                final Node q; final Object item;
                if (p.isData != haveData
                    && haveData == ((item = p.item) == null)) {
                    if (h == null) h = head;
                    if (p.tryMatch(item, e)) {
                        if (h != p) skipDeadNodesNearHead(h, p);
                        return (E) item;
                    }
                }
                if ((q = p.next) == null) {
                    if (how == NOW) return e;
                    if (s == null) s = new Node(e);
                    if (!p.casNext(null, s)) continue;
                    if (p != t) casTail(t, s);
                    if (how == ASYNC) return e;
                    return awaitMatch(s, p, e, (how == TIMED), nanos);
                }
                if (p == (p = q)) continue restart;
            }
        }
    }

    private E awaitMatch(Node s, Node pred, E e, boolean timed, long nanos) {
        final long deadline = timed ? System.nanoTime() + nanos : 0L;
        Thread w = Thread.currentThread();
        int spins = -1;
        ThreadLocalRandom randomYields = null;

        for (;;) {
            final Object item;
            if ((item = s.item) != e) {

                s.forgetContents();
                @SuppressWarnings("unchecked") E itemE = (E) item;
                return itemE;
            }
            else if (w.isInterrupted() || (timed && nanos <= 0L)) {

                if (s.casItem(e, s.isData ? null : s)) {
                    unsplice(pred, s);
                    return e;
                }

            }
            else if (spins < 0) {
                if ((spins = spinsFor(pred, s.isData)) > 0)
                    randomYields = ThreadLocalRandom.current();
            }
            else if (spins > 0) {
                --spins;
                if (randomYields.nextInt(CHAINED_SPINS) == 0)
                    Thread.yield();
            }
            else if (s.waiter == null) {
                s.waiter = w;
            }
            else if (timed) {
                nanos = deadline - System.nanoTime();
                if (nanos > 0L)
                    LockSupport.parkNanos(this, nanos);
            }
            else {
                LockSupport.park(this);
            }
        }
    }

    private static int spinsFor(Node pred, boolean haveData) {
        if (MP && pred != null) {
            if (pred.isData != haveData)
                return FRONT_SPINS + CHAINED_SPINS;
            if (pred.isMatched())
                return FRONT_SPINS;
            if (pred.waiter == null)
                return CHAINED_SPINS;
        }
        return 0;
    }

    final Node firstDataNode() {
        Node first = null;
        restartFromHead: for (;;) {
            Node h = head, p = h;
            while (p != null) {
                if (p.item != null) {
                    if (p.isData) {
                        first = p;
                        break;
                    }
                }
                else if (!p.isData)
                    break;
                final Node q;
                if ((q = p.next) == null)
                    break;
                if (p == (p = q))
                    continue restartFromHead;
            }
            if (p != h && casHead(h, p))
                h.selfLink();
            return first;
        }
    }

    private int countOfMode(boolean data) {
        restartFromHead: for (;;) {
            int count = 0;
            for (Node p = head; p != null;) {
                if (!p.isMatched()) {
                    if (p.isData != data)
                        return 0;
                    if (++count == Integer.MAX_VALUE)
                        break;
                }
                if (p == (p = p.next))
                    continue restartFromHead;
            }
            return count;
        }
    }

    public String toString() {
        String[] a = null;
        restartFromHead: for (;;) {
            int charLength = 0;
            int size = 0;
            for (Node p = head; p != null;) {
                Object item = p.item;
                if (p.isData) {
                    if (item != null) {
                        if (a == null)
                            a = new String[4];
                        else if (size == a.length)
                            a = Arrays.copyOf(a, 2 * size);
                        String s = item.toString();
                        a[size++] = s;
                        charLength += s.length();
                    }
                } else if (item == null)
                    break;
                if (p == (p = p.next))
                    continue restartFromHead;
            }

            if (size == 0)
                return "[]";

            return Helpers.toString(a, size, charLength);
        }
    }

    private Object[] toArrayInternal(Object[] a) {
        Object[] x = a;
        restartFromHead: for (;;) {
            int size = 0;
            for (Node p = head; p != null;) {
                Object item = p.item;
                if (p.isData) {
                    if (item != null) {
                        if (x == null)
                            x = new Object[4];
                        else if (size == x.length)
                            x = Arrays.copyOf(x, 2 * (size + 4));
                        x[size++] = item;
                    }
                } else if (item == null)
                    break;
                if (p == (p = p.next))
                    continue restartFromHead;
            }
            if (x == null)
                return new Object[0];
            else if (a != null && size <= a.length) {
                if (a != x)
                    System.arraycopy(x, 0, a, 0, size);
                if (size < a.length)
                    a[size] = null;
                return a;
            }
            return (size == x.length) ? x : Arrays.copyOf(x, size);
        }
    }

    public Object[] toArray() {
        return toArrayInternal(null);
    }

    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        Objects.requireNonNull(a);
        return (T[]) toArrayInternal(a);
    }

    final class Itr implements Iterator<E> {
        private Node nextNode;
        private E nextItem;
        private Node lastRet;
        private Node ancestor;

        @SuppressWarnings("unchecked")
        private void advance(Node pred) {
            for (Node p = (pred == null) ? head : pred.next, c = p;
                 p != null; ) {
                final Object item;
                if ((item = p.item) != null && p.isData) {
                    nextNode = p;
                    nextItem = (E) item;
                    if (c != p)
                        tryCasSuccessor(pred, c, p);
                    return;
                }
                else if (!p.isData && item == null)
                    break;
                if (c != p && !tryCasSuccessor(pred, c, c = p)) {
                    pred = p;
                    c = p = p.next;
                }
                else if (p == (p = p.next)) {
                    pred = null;
                    c = p = head;
                }
            }
            nextItem = null;
            nextNode = null;
        }

        Itr() {
            advance(null);
        }

        public final boolean hasNext() {
            return nextNode != null;
        }

        public final E next() {
            final Node p;
            if ((p = nextNode) == null) throw new NoSuchElementException();
            E e = nextItem;
            advance(lastRet = p);
            return e;
        }

        public void forEachRemaining(Consumer<? super E> action) {
            Objects.requireNonNull(action);
            Node q = null;
            for (Node p; (p = nextNode) != null; advance(q = p))
                action.accept(nextItem);
            if (q != null)
                lastRet = q;
        }

        public final void remove() {
            final Node lastRet = this.lastRet;
            if (lastRet == null)
                throw new IllegalStateException();
            this.lastRet = null;
            if (lastRet.item == null)
                return;

            Node pred = ancestor;
            for (Node p = (pred == null) ? head : pred.next, c = p, q;
                 p != null; ) {
                if (p == lastRet) {
                    final Object item;
                    if ((item = p.item) != null)
                        p.tryMatch(item, null);
                    if ((q = p.next) == null) q = p;
                    if (c != q) tryCasSuccessor(pred, c, q);
                    ancestor = pred;
                    return;
                }
                final Object item; final boolean pAlive;
                if (pAlive = ((item = p.item) != null && p.isData)) {

                }
                else if (!p.isData && item == null)
                    break;
                if ((c != p && !tryCasSuccessor(pred, c, c = p)) || pAlive) {
                    pred = p;
                    c = p = p.next;
                }
                else if (p == (p = p.next)) {
                    pred = null;
                    c = p = head;
                }
            }

        }
    }

    final class LTQSpliterator implements Spliterator<E> {
        static final int MAX_BATCH = 1 << 25;
        Node current;
        int batch;
        boolean exhausted;
        LTQSpliterator() {}

        public Spliterator<E> trySplit() {
            Node p, q;
            if ((p = current()) == null || (q = p.next) == null)
                return null;
            int i = 0, n = batch = Math.min(batch + 1, MAX_BATCH);
            Object[] a = null;
            do {
                final Object item = p.item;
                if (p.isData) {
                    if (item != null) {
                        if (a == null)
                            a = new Object[n];
                        a[i++] = item;
                    }
                } else if (item == null) {
                    p = null;
                    break;
                }
                if (p == (p = q))
                    p = firstDataNode();
            } while (p != null && (q = p.next) != null && i < n);
            setCurrent(p);
            return (i == 0) ? null :
                Spliterators.spliterator(a, 0, i, (Spliterator.ORDERED |
                                                   Spliterator.NONNULL |
                                                   Spliterator.CONCURRENT));
        }

        public void forEachRemaining(Consumer<? super E> action) {
            Objects.requireNonNull(action);
            final Node p;
            if ((p = current()) != null) {
                current = null;
                exhausted = true;
                forEachFrom(action, p);
            }
        }

        @SuppressWarnings("unchecked")
        public boolean tryAdvance(Consumer<? super E> action) {
            Objects.requireNonNull(action);
            Node p;
            if ((p = current()) != null) {
                E e = null;
                do {
                    final Object item = p.item;
                    final boolean isData = p.isData;
                    if (p == (p = p.next))
                        p = head;
                    if (isData) {
                        if (item != null) {
                            e = (E) item;
                            break;
                        }
                    }
                    else if (item == null)
                        p = null;
                } while (p != null);
                setCurrent(p);
                if (e != null) {
                    action.accept(e);
                    return true;
                }
            }
            return false;
        }

        private void setCurrent(Node p) {
            if ((current = p) == null)
                exhausted = true;
        }

        private Node current() {
            Node p;
            if ((p = current) == null && !exhausted)
                setCurrent(p = firstDataNode());
            return p;
        }

        public long estimateSize() { return Long.MAX_VALUE; }

        public int characteristics() {
            return (Spliterator.ORDERED |
                    Spliterator.NONNULL |
                    Spliterator.CONCURRENT);
        }
    }

    public Spliterator<E> spliterator() {
        return new LTQSpliterator();
    }

    final void unsplice(Node pred, Node s) {

        s.waiter = null;

        if (pred != null && pred.next == s) {
            Node n = s.next;
            if (n == null ||
                (n != s && pred.casNext(s, n) && pred.isMatched())) {
                for (;;) {
                    Node h = head;
                    if (h == pred || h == s)
                        return;
                    if (!h.isMatched())
                        break;
                    Node hn = h.next;
                    if (hn == null)
                        return;
                    if (hn != h && casHead(h, hn))
                        h.selfLink();
                }

                if (pred.next != pred && s.next != s
                    && (incSweepVotes() & (SWEEP_THRESHOLD - 1)) == 0)
                    sweep();
            }
        }
    }

    private void sweep() {
        for (Node p = head, s, n; p != null && (s = p.next) != null; ) {
            if (!s.isMatched())

                p = s;
            else if ((n = s.next) == null)
                break;
            else if (s == n)

                p = head;
            else
                p.casNext(s, n);
        }
    }

    public LinkedTransferQueue() {
        head = tail = new Node();
    }

    public LinkedTransferQueue(Collection<? extends E> c) {
        Node h = null, t = null;
        for (E e : c) {
            Node newNode = new Node(Objects.requireNonNull(e));
            if (h == null)
                h = t = newNode;
            else
                t.appendRelaxed(t = newNode);
        }
        if (h == null)
            h = t = new Node();
        head = h;
        tail = t;
    }

    public void put(E e) {
        xfer(e, true, ASYNC, 0);
    }

    public boolean offer(E e, long timeout, TimeUnit unit) {
        xfer(e, true, ASYNC, 0);
        return true;
    }

    public boolean offer(E e) {
        xfer(e, true, ASYNC, 0);
        return true;
    }

    public boolean add(E e) {
        xfer(e, true, ASYNC, 0);
        return true;
    }

    public boolean tryTransfer(E e) {
        return xfer(e, true, NOW, 0) == null;
    }

    public void transfer(E e) throws InterruptedException {
        if (xfer(e, true, SYNC, 0) != null) {
            Thread.interrupted();
            throw new InterruptedException();
        }
    }

    public boolean tryTransfer(E e, long timeout, TimeUnit unit)
        throws InterruptedException {
        if (xfer(e, true, TIMED, unit.toNanos(timeout)) == null)
            return true;
        if (!Thread.interrupted())
            return false;
        throw new InterruptedException();
    }

    public E take() throws InterruptedException {
        E e = xfer(null, false, SYNC, 0);
        if (e != null)
            return e;
        Thread.interrupted();
        throw new InterruptedException();
    }

    public E poll(long timeout, TimeUnit unit) throws InterruptedException {
        E e = xfer(null, false, TIMED, unit.toNanos(timeout));
        if (e != null || !Thread.interrupted())
            return e;
        throw new InterruptedException();
    }

    public E poll() {
        return xfer(null, false, NOW, 0);
    }

    public int drainTo(Collection<? super E> c) {
        Objects.requireNonNull(c);
        if (c == this)
            throw new IllegalArgumentException();
        int n = 0;
        for (E e; (e = poll()) != null; n++)
            c.add(e);
        return n;
    }

    public int drainTo(Collection<? super E> c, int maxElements) {
        Objects.requireNonNull(c);
        if (c == this)
            throw new IllegalArgumentException();
        int n = 0;
        for (E e; n < maxElements && (e = poll()) != null; n++)
            c.add(e);
        return n;
    }

    public Iterator<E> iterator() {
        return new Itr();
    }

    public E peek() {
        restartFromHead: for (;;) {
            for (Node p = head; p != null;) {
                Object item = p.item;
                if (p.isData) {
                    if (item != null) {
                        @SuppressWarnings("unchecked") E e = (E) item;
                        return e;
                    }
                }
                else if (item == null)
                    break;
                if (p == (p = p.next))
                    continue restartFromHead;
            }
            return null;
        }
    }

    public boolean isEmpty() {
        return firstDataNode() == null;
    }

    public boolean hasWaitingConsumer() {
        restartFromHead: for (;;) {
            for (Node p = head; p != null;) {
                Object item = p.item;
                if (p.isData) {
                    if (item != null)
                        break;
                }
                else if (item == null)
                    return true;
                if (p == (p = p.next))
                    continue restartFromHead;
            }
            return false;
        }
    }

    public int size() {
        return countOfMode(true);
    }

    public int getWaitingConsumerCount() {
        return countOfMode(false);
    }

    public boolean remove(Object o) {
        if (o == null) return false;
        restartFromHead: for (;;) {
            for (Node p = head, pred = null; p != null; ) {
                Node q = p.next;
                final Object item;
                if ((item = p.item) != null) {
                    if (p.isData) {
                        if (o.equals(item) && p.tryMatch(item, null)) {
                            skipDeadNodes(pred, p, p, q);
                            return true;
                        }
                        pred = p; p = q; continue;
                    }
                }
                else if (!p.isData)
                    break;
                for (Node c = p;; q = p.next) {
                    if (q == null || !q.isMatched()) {
                        pred = skipDeadNodes(pred, c, p, q); p = q; break;
                    }
                    if (p == (p = q)) continue restartFromHead;
                }
            }
            return false;
        }
    }

    public boolean contains(Object o) {
        if (o == null) return false;
        restartFromHead: for (;;) {
            for (Node p = head, pred = null; p != null; ) {
                Node q = p.next;
                final Object item;
                if ((item = p.item) != null) {
                    if (p.isData) {
                        if (o.equals(item))
                            return true;
                        pred = p; p = q; continue;
                    }
                }
                else if (!p.isData)
                    break;
                for (Node c = p;; q = p.next) {
                    if (q == null || !q.isMatched()) {
                        pred = skipDeadNodes(pred, c, p, q); p = q; break;
                    }
                    if (p == (p = q)) continue restartFromHead;
                }
            }
            return false;
        }
    }

    public int remainingCapacity() {
        return Integer.MAX_VALUE;
    }

    private void writeObject(java.io.ObjectOutputStream s)
        throws java.io.IOException {
        s.defaultWriteObject();
        for (E e : this)
            s.writeObject(e);

        s.writeObject(null);
    }

    private void readObject(java.io.ObjectInputStream s)
        throws java.io.IOException, ClassNotFoundException {

        Node h = null, t = null;
        for (Object item; (item = s.readObject()) != null; ) {
            Node newNode = new Node(item);
            if (h == null)
                h = t = newNode;
            else
                t.appendRelaxed(t = newNode);
        }
        if (h == null)
            h = t = new Node();
        head = h;
        tail = t;
    }

    public boolean removeIf(Predicate<? super E> filter) {
        Objects.requireNonNull(filter);
        return bulkRemove(filter);
    }

    public boolean removeAll(Collection<?> c) {
        Objects.requireNonNull(c);
        return bulkRemove(e -> c.contains(e));
    }

    public boolean retainAll(Collection<?> c) {
        Objects.requireNonNull(c);
        return bulkRemove(e -> !c.contains(e));
    }

    public void clear() {
        bulkRemove(e -> true);
    }

    private static final int MAX_HOPS = 8;

    @SuppressWarnings("unchecked")
    private boolean bulkRemove(Predicate<? super E> filter) {
        boolean removed = false;
        restartFromHead: for (;;) {
            int hops = MAX_HOPS;

            for (Node p = head, c = p, pred = null, q; p != null; p = q) {
                q = p.next;
                final Object item; boolean pAlive;
                if (pAlive = ((item = p.item) != null && p.isData)) {
                    if (filter.test((E) item)) {
                        if (p.tryMatch(item, null))
                            removed = true;
                        pAlive = false;
                    }
                }
                else if (!p.isData && item == null)
                    break;
                if (pAlive || q == null || --hops == 0) {

                    if ((c != p && !tryCasSuccessor(pred, c, c = p))
                        || pAlive) {

                        hops = MAX_HOPS;
                        pred = p;
                        c = q;
                    }
                } else if (p == q)
                    continue restartFromHead;
            }
            return removed;
        }
    }

    @SuppressWarnings("unchecked")
    void forEachFrom(Consumer<? super E> action, Node p) {
        for (Node pred = null; p != null; ) {
            Node q = p.next;
            final Object item;
            if ((item = p.item) != null) {
                if (p.isData) {
                    action.accept((E) item);
                    pred = p; p = q; continue;
                }
            }
            else if (!p.isData)
                break;
            for (Node c = p;; q = p.next) {
                if (q == null || !q.isMatched()) {
                    pred = skipDeadNodes(pred, c, p, q); p = q; break;
                }
                if (p == (p = q)) { pred = null; p = head; break; }
            }
        }
    }

    public void forEach(Consumer<? super E> action) {
        Objects.requireNonNull(action);
        forEachFrom(action, head);
    }

    private static final VarHandle HEAD;
    private static final VarHandle TAIL;
    private static final VarHandle SWEEPVOTES;
    static final VarHandle ITEM;
    static final VarHandle NEXT;
    static final VarHandle WAITER;
    static {
        try {
            MethodHandles.Lookup l = MethodHandles.lookup();
            HEAD = l.findVarHandle(LinkedTransferQueue.class, "head",
                                   Node.class);
            TAIL = l.findVarHandle(LinkedTransferQueue.class, "tail",
                                   Node.class);
            SWEEPVOTES = l.findVarHandle(LinkedTransferQueue.class, "sweepVotes",
                                         int.class);
            ITEM = l.findVarHandle(Node.class, "item", Object.class);
            NEXT = l.findVarHandle(Node.class, "next", Node.class);
            WAITER = l.findVarHandle(Node.class, "waiter", Thread.class);
        } catch (ReflectiveOperationException e) {
            throw new ExceptionInInitializerError(e);
        }

        Class<?> ensureLoaded = LockSupport.class;
    }
}
