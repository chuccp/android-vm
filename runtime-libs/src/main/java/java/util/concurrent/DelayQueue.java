package java.util.concurrent;

import java.util.AbstractQueue;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Iterator;
import java.util.NoSuchElementException;

public class DelayQueue<E extends Delayed> extends AbstractQueue<E>
        implements BlockingQueue<E> {
    private final ArrayList<E> elements = new ArrayList<E>();

    private int nextIndex() {
        int selected = -1;
        long selectedDelay = Long.MAX_VALUE;
        for (int index = 0; index < elements.size(); index++) {
            long delay = Math.max(
                    0L,
                    elements.get(index).getDelay(TimeUnit.MILLISECONDS));
            if (delay < selectedDelay) {
                selected = index;
                selectedDelay = delay;
            }
        }
        return selected;
    }

    private E removeAt(int index) {
        E value = elements.get(index);
        elements.remove(index);
        notifyAll();
        return value;
    }

    private E takeExpired() {
        int index = nextIndex();
        if (index < 0) return null;
        long delay = elements.get(index).getDelay(TimeUnit.MILLISECONDS);
        return delay <= 0 ? removeAt(index) : null;
    }

    private long nextDelayMillis() {
        int index = nextIndex();
        if (index < 0) return 0L;
        long millis = Math.max(
                0L,
                elements.get(index).getDelay(TimeUnit.MILLISECONDS));
        return Math.max(1L, millis);
    }

    @Override
    public synchronized boolean add(E value) {
        return offer(value);
    }

    @Override
    public synchronized boolean offer(E value) {
        if (value == null) throw new NullPointerException("value");
        elements.add(value);
        notifyAll();
        return true;
    }

    @Override
    public synchronized void put(E value) {
        offer(value);
    }

    @Override
    public synchronized boolean offer(
            E value, long timeout, TimeUnit unit) {
        if (value == null) throw new NullPointerException("value");
        return offer(value);
    }

    @Override
    public synchronized E poll() {
        return takeExpired();
    }

    @Override
    public E poll(long timeout, TimeUnit unit)
            throws InterruptedException {
        long remaining = unit.toMillis(timeout);
        long deadline = System.currentTimeMillis() + remaining;
        while (true) {
            long delay;
            synchronized (this) {
                E value = takeExpired();
                if (value != null) return value;
                delay = nextDelayMillis();
            }
            remaining = deadline - System.currentTimeMillis();
            if (remaining <= 0L) return null;
            Thread.sleep(Math.max(1L, Math.min(remaining, delay)));
        }
    }

    @Override
    public E take() throws InterruptedException {
        while (true) {
            long delay;
            synchronized (this) {
                E value = takeExpired();
                if (value != null) return value;
                delay = nextDelayMillis();
            }
            Thread.sleep(Math.max(1L, delay));
        }
    }

    @Override
    public synchronized E peek() {
        int index = nextIndex();
        return index < 0 ? null : elements.get(index);
    }

    @Override
    public synchronized int size() {
        return elements.size();
    }

    @Override
    public synchronized boolean isEmpty() {
        return elements.isEmpty();
    }

    @Override
    public synchronized int remainingCapacity() {
        return Integer.MAX_VALUE;
    }

    @Override
    public synchronized boolean remove(Object value) {
        for (int index = 0; index < elements.size(); index++) {
            if (elements.get(index).equals(value)) {
                elements.remove(index);
                notifyAll();
                return true;
            }
        }
        return false;
    }

    @Override
    public synchronized boolean contains(Object value) {
        return elements.contains(value);
    }

    @Override
    public synchronized void clear() {
        elements.clear();
        notifyAll();
    }

    @Override
    public synchronized int drainTo(Collection<? super E> target) {
        return drainTo(target, Integer.MAX_VALUE);
    }

    @Override
    public synchronized int drainTo(
            Collection<? super E> target, int maxElements) {
        int moved = 0;
        while (moved < maxElements) {
            E value = takeExpired();
            if (value == null) break;
            target.add(value);
            moved++;
        }
        return moved;
    }

    @Override
    public Iterator<E> iterator() {
        final ArrayList<E> snapshot =
                new ArrayList<E>(elements.size());
        synchronized (this) {
            for (int index = 0; index < elements.size(); index++) {
                snapshot.add(elements.get(index));
            }
        }
        return new Iterator<E>() {
            private int index;

            @Override
            public boolean hasNext() {
                return index < snapshot.size();
            }

            @Override
            public E next() {
                if (!hasNext()) throw new NoSuchElementException();
                return snapshot.get(index++);
            }

            @Override
            public void remove() {
                throw new UnsupportedOperationException();
            }
        };
    }
}
