package java.util.concurrent;

import java.lang.ref.WeakReference;
import java.util.AbstractQueue;
import java.util.Arrays;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;
import java.util.Objects;
import java.util.Spliterator;
import java.util.Spliterators;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.ReentrantLock;
import java.util.function.Consumer;
import java.util.function.Predicate;

public class ArrayBlockingQueue<E> extends AbstractQueue<E>
        implements BlockingQueue<E>, java.io.Serializable {

    private static final long serialVersionUID = -817911632652898426L;

    final Object[] items;

    int takeIndex;

    int putIndex;

    int count;

    final ReentrantLock lock;

    private final Condition notEmpty;

    private final Condition notFull;

    transient Itrs itrs;

    static final int inc(int i, int modulus) {
        if (++i >= modulus) i = 0;
        return i;
    }

    static final int dec(int i, int modulus) {
        if (--i < 0) i = modulus - 1;
        return i;
    }

    @SuppressWarnings("unchecked")
    final E itemAt(int i) {
        return (E) items[i];
    }

    @SuppressWarnings("unchecked")
    static <E> E itemAt(Object[] items, int i) {
        return (E) items[i];
    }

    private void enqueue(E e) {

        final Object[] items = this.items;
        items[putIndex] = e;
        if (++putIndex == items.length) putIndex = 0;
        count++;
        notEmpty.signal();
    }

    private E dequeue() {

        final Object[] items = this.items;
        @SuppressWarnings("unchecked")
        E e = (E) items[takeIndex];
        items[takeIndex] = null;
        if (++takeIndex == items.length) takeIndex = 0;
        count--;
        if (itrs != null)
            itrs.elementDequeued();
        notFull.signal();
        return e;
    }

    void removeAt(final int removeIndex) {

        final Object[] items = this.items;
        if (removeIndex == takeIndex) {

            items[takeIndex] = null;
            if (++takeIndex == items.length) takeIndex = 0;
            count--;
            if (itrs != null)
                itrs.elementDequeued();
        } else {

            for (int i = removeIndex, putIndex = this.putIndex;;) {
                int pred = i;
                if (++i == items.length) i = 0;
                if (i == putIndex) {
                    items[pred] = null;
                    this.putIndex = pred;
                    break;
                }
                items[pred] = items[i];
            }
            count--;
            if (itrs != null)
                itrs.removedAt(removeIndex);
        }
        notFull.signal();
    }

    public ArrayBlockingQueue(int capacity) {
        this(capacity, false);
    }

    public ArrayBlockingQueue(int capacity, boolean fair) {
        if (capacity <= 0)
            throw new IllegalArgumentException();
        this.items = new Object[capacity];
        lock = new ReentrantLock(fair);
        notEmpty = lock.newCondition();
        notFull =  lock.newCondition();
    }

    public ArrayBlockingQueue(int capacity, boolean fair,
                              Collection<? extends E> c) {
        this(capacity, fair);

        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            final Object[] items = this.items;
            int i = 0;
            try {
                for (E e : c)
                    items[i++] = Objects.requireNonNull(e);
            } catch (ArrayIndexOutOfBoundsException ex) {
                throw new IllegalArgumentException();
            }
            count = i;
            putIndex = (i == capacity) ? 0 : i;
        } finally {
            lock.unlock();
        }
    }

    public boolean add(E e) {
        return super.add(e);
    }

    public boolean offer(E e) {
        Objects.requireNonNull(e);
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            if (count == items.length)
                return false;
            else {
                enqueue(e);
                return true;
            }
        } finally {
            lock.unlock();
        }
    }

    public void put(E e) throws InterruptedException {
        Objects.requireNonNull(e);
        final ReentrantLock lock = this.lock;
        lock.lockInterruptibly();
        try {
            while (count == items.length)
                notFull.await();
            enqueue(e);
        } finally {
            lock.unlock();
        }
    }

    public boolean offer(E e, long timeout, TimeUnit unit)
        throws InterruptedException {

        Objects.requireNonNull(e);
        long nanos = unit.toNanos(timeout);
        final ReentrantLock lock = this.lock;
        lock.lockInterruptibly();
        try {
            while (count == items.length) {
                if (nanos <= 0L)
                    return false;
                nanos = notFull.awaitNanos(nanos);
            }
            enqueue(e);
            return true;
        } finally {
            lock.unlock();
        }
    }

    public E poll() {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            return (count == 0) ? null : dequeue();
        } finally {
            lock.unlock();
        }
    }

    public E take() throws InterruptedException {
        final ReentrantLock lock = this.lock;
        lock.lockInterruptibly();
        try {
            while (count == 0)
                notEmpty.await();
            return dequeue();
        } finally {
            lock.unlock();
        }
    }

    public E poll(long timeout, TimeUnit unit) throws InterruptedException {
        long nanos = unit.toNanos(timeout);
        final ReentrantLock lock = this.lock;
        lock.lockInterruptibly();
        try {
            while (count == 0) {
                if (nanos <= 0L)
                    return null;
                nanos = notEmpty.awaitNanos(nanos);
            }
            return dequeue();
        } finally {
            lock.unlock();
        }
    }

    public E peek() {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            return itemAt(takeIndex);
        } finally {
            lock.unlock();
        }
    }

    public int size() {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            return count;
        } finally {
            lock.unlock();
        }
    }

    public int remainingCapacity() {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            return items.length - count;
        } finally {
            lock.unlock();
        }
    }

    public boolean remove(Object o) {
        if (o == null) return false;
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            if (count > 0) {
                final Object[] items = this.items;
                for (int i = takeIndex, end = putIndex,
                         to = (i < end) ? end : items.length;
                     ; i = 0, to = end) {
                    for (; i < to; i++)
                        if (o.equals(items[i])) {
                            removeAt(i);
                            return true;
                        }
                    if (to == end) break;
                }
            }
            return false;
        } finally {
            lock.unlock();
        }
    }

    public boolean contains(Object o) {
        if (o == null) return false;
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            if (count > 0) {
                final Object[] items = this.items;
                for (int i = takeIndex, end = putIndex,
                         to = (i < end) ? end : items.length;
                     ; i = 0, to = end) {
                    for (; i < to; i++)
                        if (o.equals(items[i]))
                            return true;
                    if (to == end) break;
                }
            }
            return false;
        } finally {
            lock.unlock();
        }
    }

    public Object[] toArray() {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            final Object[] items = this.items;
            final int end = takeIndex + count;
            final Object[] a = Arrays.copyOfRange(items, takeIndex, end);
            if (end != putIndex)
                System.arraycopy(items, 0, a, items.length - takeIndex, putIndex);
            return a;
        } finally {
            lock.unlock();
        }
    }

    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] a) {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            final Object[] items = this.items;
            final int count = this.count;
            final int firstLeg = Math.min(items.length - takeIndex, count);
            if (a.length < count) {
                a = (T[]) Arrays.copyOfRange(items, takeIndex, takeIndex + count,
                                             a.getClass());
            } else {
                System.arraycopy(items, takeIndex, a, 0, firstLeg);
                if (a.length > count)
                    a[count] = null;
            }
            if (firstLeg < count)
                System.arraycopy(items, 0, a, firstLeg, putIndex);
            return a;
        } finally {
            lock.unlock();
        }
    }

    public String toString() {
        return Helpers.collectionToString(this);
    }

    public void clear() {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            int k;
            if ((k = count) > 0) {
                circularClear(items, takeIndex, putIndex);
                takeIndex = putIndex;
                count = 0;
                if (itrs != null)
                    itrs.queueIsEmpty();
                for (; k > 0 && lock.hasWaiters(notFull); k--)
                    notFull.signal();
            }
        } finally {
            lock.unlock();
        }
    }

    private static void circularClear(Object[] items, int i, int end) {

        for (int to = (i < end) ? end : items.length;
             ; i = 0, to = end) {
            for (; i < to; i++) items[i] = null;
            if (to == end) break;
        }
    }

    public int drainTo(Collection<? super E> c) {
        return drainTo(c, Integer.MAX_VALUE);
    }

    public int drainTo(Collection<? super E> c, int maxElements) {
        Objects.requireNonNull(c);
        if (c == this)
            throw new IllegalArgumentException();
        if (maxElements <= 0)
            return 0;
        final Object[] items = this.items;
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            int n = Math.min(maxElements, count);
            int take = takeIndex;
            int i = 0;
            try {
                while (i < n) {
                    @SuppressWarnings("unchecked")
                    E e = (E) items[take];
                    c.add(e);
                    items[take] = null;
                    if (++take == items.length) take = 0;
                    i++;
                }
                return n;
            } finally {

                if (i > 0) {
                    count -= i;
                    takeIndex = take;
                    if (itrs != null) {
                        if (count == 0)
                            itrs.queueIsEmpty();
                        else if (i > take)
                            itrs.takeIndexWrapped();
                    }
                    for (; i > 0 && lock.hasWaiters(notFull); i--)
                        notFull.signal();
                }
            }
        } finally {
            lock.unlock();
        }
    }

    public Iterator<E> iterator() {
        return new Itr();
    }

    class Itrs {

        private class Node extends WeakReference<Itr> {
            Node next;

            Node(Itr iterator, Node next) {
                super(iterator);
                this.next = next;
            }
        }

        int cycles;

        private Node head;

        private Node sweeper;

        private static final int SHORT_SWEEP_PROBES = 4;
        private static final int LONG_SWEEP_PROBES = 16;

        Itrs(Itr initial) {
            register(initial);
        }

        void doSomeSweeping(boolean tryHarder) {

            int probes = tryHarder ? LONG_SWEEP_PROBES : SHORT_SWEEP_PROBES;
            Node o, p;
            final Node sweeper = this.sweeper;
            boolean passedGo;

            if (sweeper == null) {
                o = null;
                p = head;
                passedGo = true;
            } else {
                o = sweeper;
                p = o.next;
                passedGo = false;
            }

            for (; probes > 0; probes--) {
                if (p == null) {
                    if (passedGo)
                        break;
                    o = null;
                    p = head;
                    passedGo = true;
                }
                final Itr it = p.get();
                final Node next = p.next;
                if (it == null || it.isDetached()) {

                    probes = LONG_SWEEP_PROBES;

                    p.clear();
                    p.next = null;
                    if (o == null) {
                        head = next;
                        if (next == null) {

                            itrs = null;
                            return;
                        }
                    }
                    else
                        o.next = next;
                } else {
                    o = p;
                }
                p = next;
            }

            this.sweeper = (p == null) ? null : o;
        }

        void register(Itr itr) {

            head = new Node(itr, head);
        }

        void takeIndexWrapped() {

            cycles++;
            for (Node o = null, p = head; p != null;) {
                final Itr it = p.get();
                final Node next = p.next;
                if (it == null || it.takeIndexWrapped()) {

                    p.clear();
                    p.next = null;
                    if (o == null)
                        head = next;
                    else
                        o.next = next;
                } else {
                    o = p;
                }
                p = next;
            }
            if (head == null)
                itrs = null;
        }

        void removedAt(int removedIndex) {
            for (Node o = null, p = head; p != null;) {
                final Itr it = p.get();
                final Node next = p.next;
                if (it == null || it.removedAt(removedIndex)) {

                    p.clear();
                    p.next = null;
                    if (o == null)
                        head = next;
                    else
                        o.next = next;
                } else {
                    o = p;
                }
                p = next;
            }
            if (head == null)
                itrs = null;
        }

        void queueIsEmpty() {

            for (Node p = head; p != null; p = p.next) {
                Itr it = p.get();
                if (it != null) {
                    p.clear();
                    it.shutdown();
                }
            }
            head = null;
            itrs = null;
        }

        void elementDequeued() {

            if (count == 0)
                queueIsEmpty();
            else if (takeIndex == 0)
                takeIndexWrapped();
        }
    }

    private class Itr implements Iterator<E> {

        private int cursor;

        private E nextItem;

        private int nextIndex;

        private E lastItem;

        private int lastRet;

        private int prevTakeIndex;

        private int prevCycles;

        private static final int NONE = -1;

        private static final int REMOVED = -2;

        private static final int DETACHED = -3;

        Itr() {
            lastRet = NONE;
            final ReentrantLock lock = ArrayBlockingQueue.this.lock;
            lock.lock();
            try {
                if (count == 0) {

                    cursor = NONE;
                    nextIndex = NONE;
                    prevTakeIndex = DETACHED;
                } else {
                    final int takeIndex = ArrayBlockingQueue.this.takeIndex;
                    prevTakeIndex = takeIndex;
                    nextItem = itemAt(nextIndex = takeIndex);
                    cursor = incCursor(takeIndex);
                    if (itrs == null) {
                        itrs = new Itrs(this);
                    } else {
                        itrs.register(this);
                        itrs.doSomeSweeping(false);
                    }
                    prevCycles = itrs.cycles;

                }
            } finally {
                lock.unlock();
            }
        }

        boolean isDetached() {

            return prevTakeIndex < 0;
        }

        private int incCursor(int index) {

            if (++index == items.length) index = 0;
            if (index == putIndex) index = NONE;
            return index;
        }

        private boolean invalidated(int index, int prevTakeIndex,
                                    long dequeues, int length) {
            if (index < 0)
                return false;
            int distance = index - prevTakeIndex;
            if (distance < 0)
                distance += length;
            return dequeues > distance;
        }

        private void incorporateDequeues() {

            final int cycles = itrs.cycles;
            final int takeIndex = ArrayBlockingQueue.this.takeIndex;
            final int prevCycles = this.prevCycles;
            final int prevTakeIndex = this.prevTakeIndex;

            if (cycles != prevCycles || takeIndex != prevTakeIndex) {
                final int len = items.length;

                long dequeues = (long) (cycles - prevCycles) * len
                    + (takeIndex - prevTakeIndex);

                if (invalidated(lastRet, prevTakeIndex, dequeues, len))
                    lastRet = REMOVED;
                if (invalidated(nextIndex, prevTakeIndex, dequeues, len))
                    nextIndex = REMOVED;
                if (invalidated(cursor, prevTakeIndex, dequeues, len))
                    cursor = takeIndex;

                if (cursor < 0 && nextIndex < 0 && lastRet < 0)
                    detach();
                else {
                    this.prevCycles = cycles;
                    this.prevTakeIndex = takeIndex;
                }
            }
        }

        private void detach() {

            if (prevTakeIndex >= 0) {

                prevTakeIndex = DETACHED;

                itrs.doSomeSweeping(true);
            }
        }

        public boolean hasNext() {
            if (nextItem != null)
                return true;
            noNext();
            return false;
        }

        private void noNext() {
            final ReentrantLock lock = ArrayBlockingQueue.this.lock;
            lock.lock();
            try {

                if (!isDetached()) {

                    incorporateDequeues();
                    if (lastRet >= 0) {
                        lastItem = itemAt(lastRet);

                        detach();
                    }
                }

            } finally {
                lock.unlock();
            }
        }

        public E next() {
            final E e = nextItem;
            if (e == null)
                throw new NoSuchElementException();
            final ReentrantLock lock = ArrayBlockingQueue.this.lock;
            lock.lock();
            try {
                if (!isDetached())
                    incorporateDequeues();

                lastRet = nextIndex;
                final int cursor = this.cursor;
                if (cursor >= 0) {
                    nextItem = itemAt(nextIndex = cursor);

                    this.cursor = incCursor(cursor);
                } else {
                    nextIndex = NONE;
                    nextItem = null;
                    if (lastRet == REMOVED) detach();
                }
            } finally {
                lock.unlock();
            }
            return e;
        }

        public void forEachRemaining(Consumer<? super E> action) {
            Objects.requireNonNull(action);
            final ReentrantLock lock = ArrayBlockingQueue.this.lock;
            lock.lock();
            try {
                final E e = nextItem;
                if (e == null) return;
                if (!isDetached())
                    incorporateDequeues();
                action.accept(e);
                if (isDetached() || cursor < 0) return;
                final Object[] items = ArrayBlockingQueue.this.items;
                for (int i = cursor, end = putIndex,
                         to = (i < end) ? end : items.length;
                     ; i = 0, to = end) {
                    for (; i < to; i++)
                        action.accept(itemAt(items, i));
                    if (to == end) break;
                }
            } finally {

                cursor = nextIndex = lastRet = NONE;
                nextItem = lastItem = null;
                detach();
                lock.unlock();
            }
        }

        public void remove() {
            final ReentrantLock lock = ArrayBlockingQueue.this.lock;
            lock.lock();

            try {
                if (!isDetached())
                    incorporateDequeues();
                final int lastRet = this.lastRet;
                this.lastRet = NONE;
                if (lastRet >= 0) {
                    if (!isDetached())
                        removeAt(lastRet);
                    else {
                        final E lastItem = this.lastItem;

                        this.lastItem = null;
                        if (itemAt(lastRet) == lastItem)
                            removeAt(lastRet);
                    }
                } else if (lastRet == NONE)
                    throw new IllegalStateException();

                if (cursor < 0 && nextIndex < 0)
                    detach();
            } finally {
                lock.unlock();

            }
        }

        void shutdown() {

            cursor = NONE;
            if (nextIndex >= 0)
                nextIndex = REMOVED;
            if (lastRet >= 0) {
                lastRet = REMOVED;
                lastItem = null;
            }
            prevTakeIndex = DETACHED;

        }

        private int distance(int index, int prevTakeIndex, int length) {
            int distance = index - prevTakeIndex;
            if (distance < 0)
                distance += length;
            return distance;
        }

        boolean removedAt(int removedIndex) {

            if (isDetached())
                return true;

            final int takeIndex = ArrayBlockingQueue.this.takeIndex;
            final int prevTakeIndex = this.prevTakeIndex;
            final int len = items.length;

            final int removedDistance =
                len * (itrs.cycles - this.prevCycles
                       + ((removedIndex < takeIndex) ? 1 : 0))
                + (removedIndex - prevTakeIndex);

            int cursor = this.cursor;
            if (cursor >= 0) {
                int x = distance(cursor, prevTakeIndex, len);
                if (x == removedDistance) {
                    if (cursor == putIndex)
                        this.cursor = cursor = NONE;
                }
                else if (x > removedDistance) {

                    this.cursor = cursor = dec(cursor, len);
                }
            }
            int lastRet = this.lastRet;
            if (lastRet >= 0) {
                int x = distance(lastRet, prevTakeIndex, len);
                if (x == removedDistance)
                    this.lastRet = lastRet = REMOVED;
                else if (x > removedDistance)
                    this.lastRet = lastRet = dec(lastRet, len);
            }
            int nextIndex = this.nextIndex;
            if (nextIndex >= 0) {
                int x = distance(nextIndex, prevTakeIndex, len);
                if (x == removedDistance)
                    this.nextIndex = nextIndex = REMOVED;
                else if (x > removedDistance)
                    this.nextIndex = nextIndex = dec(nextIndex, len);
            }
            if (cursor < 0 && nextIndex < 0 && lastRet < 0) {
                this.prevTakeIndex = DETACHED;
                return true;
            }
            return false;
        }

        boolean takeIndexWrapped() {

            if (isDetached())
                return true;
            if (itrs.cycles - prevCycles > 1) {

                shutdown();
                return true;
            }
            return false;
        }

    }

    public Spliterator<E> spliterator() {
        return Spliterators.spliterator
            (this, (Spliterator.ORDERED |
                    Spliterator.NONNULL |
                    Spliterator.CONCURRENT));
    }

    public void forEach(Consumer<? super E> action) {
        Objects.requireNonNull(action);
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            if (count > 0) {
                final Object[] items = this.items;
                for (int i = takeIndex, end = putIndex,
                         to = (i < end) ? end : items.length;
                     ; i = 0, to = end) {
                    for (; i < to; i++)
                        action.accept(itemAt(items, i));
                    if (to == end) break;
                }
            }
        } finally {
            lock.unlock();
        }
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

    private boolean bulkRemove(Predicate<? super E> filter) {
        final ReentrantLock lock = this.lock;
        lock.lock();
        try {
            if (itrs == null) {
                if (count > 0) {
                    final Object[] items = this.items;

                    for (int i = takeIndex, end = putIndex,
                             to = (i < end) ? end : items.length;
                         ; i = 0, to = end) {
                        for (; i < to; i++)
                            if (filter.test(itemAt(items, i)))
                                return bulkRemoveModified(filter, i);
                        if (to == end) break;
                    }
                }
                return false;
            }
        } finally {
            lock.unlock();
        }

        return super.removeIf(filter);
    }

    private static long[] nBits(int n) {
        return new long[((n - 1) >> 6) + 1];
    }
    private static void setBit(long[] bits, int i) {
        bits[i >> 6] |= 1L << i;
    }
    private static boolean isClear(long[] bits, int i) {
        return (bits[i >> 6] & (1L << i)) == 0;
    }

    private int distanceNonEmpty(int i, int j) {
        if ((j -= i) <= 0) j += items.length;
        return j;
    }

    private boolean bulkRemoveModified(
        Predicate<? super E> filter, final int beg) {
        final Object[] es = items;
        final int capacity = items.length;
        final int end = putIndex;
        final long[] deathRow = nBits(distanceNonEmpty(beg, putIndex));
        deathRow[0] = 1L;
        for (int i = beg + 1, to = (i <= end) ? end : es.length, k = beg;
             ; i = 0, to = end, k -= capacity) {
            for (; i < to; i++)
                if (filter.test(itemAt(es, i)))
                    setBit(deathRow, i - k);
            if (to == end) break;
        }

        int w = beg;
        for (int i = beg + 1, to = (i <= end) ? end : es.length, k = beg;
             ; w = 0) {

            for (; i < to; i++)
                if (isClear(deathRow, i - k))
                    es[w++] = es[i];
            if (to == end) break;

            for (i = 0, to = end, k -= capacity; i < to && w < capacity; i++)
                if (isClear(deathRow, i - k))
                    es[w++] = es[i];
            if (i >= to) {
                if (w == capacity) w = 0;
                break;
            }
        }
        count -= distanceNonEmpty(w, end);
        circularClear(es, putIndex = w, end);
        return true;
    }

    void checkInvariants() {

        if (!invariantsSatisfied()) {
            String detail = String.format(
                "takeIndex=%d putIndex=%d count=%d capacity=%d items=%s",
                takeIndex, putIndex, count, items.length,
                Arrays.toString(items));
            System.err.println(detail);
            throw new AssertionError(detail);
        }
    }

    private boolean invariantsSatisfied() {

        int capacity = items.length;
        return capacity > 0
            && items.getClass() == Object[].class
            && (takeIndex | putIndex | count) >= 0
            && takeIndex <  capacity
            && putIndex  <  capacity
            && count     <= capacity
            && (putIndex - takeIndex - count) % capacity == 0
            && (count == 0 || items[takeIndex] != null)
            && (count == capacity || items[putIndex] == null)
            && (count == 0 || items[dec(putIndex, capacity)] != null);
    }

    private void readObject(java.io.ObjectInputStream s)
        throws java.io.IOException, ClassNotFoundException {

        s.defaultReadObject();

        if (!invariantsSatisfied())
            throw new java.io.InvalidObjectException("invariants violated");
    }
}
