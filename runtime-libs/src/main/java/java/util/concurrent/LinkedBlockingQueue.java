package java.util.concurrent;

import java.util.Collection;
import java.lang.reflect.Array;

public class LinkedBlockingQueue<E> implements BlockingQueue<E> {
    private static final class Node<E> {
        E value;
        Node<E> next;

        Node(E value) {
            this.value = value;
        }
    }

    private Node<E> head = new Node<E>(null);
    private Node<E> tail = head;
    private int size;
    private final int capacity;

    public LinkedBlockingQueue() {
        this(Integer.MAX_VALUE);
    }

    public LinkedBlockingQueue(int capacity) {
        if (capacity <= 0) {
            throw new IllegalArgumentException("capacity must be positive");
        }
        this.capacity = capacity;
    }

    @Override
    public void put(E value) throws InterruptedException {
        if (value == null) {
            throw new NullPointerException("value");
        }
        synchronized (this) {
            while (size == capacity) {
                wait();
            }
            enqueue(value);
        }
    }

    @Override
    public boolean offer(E value) {
        if (value == null) {
            throw new NullPointerException("value");
        }
        synchronized (this) {
            if (size == capacity) {
                return false;
            }
            enqueue(value);
            return true;
        }
    }

    @Override
    public boolean offer(E value, long timeout, TimeUnit unit)
            throws InterruptedException {
        if (value == null) {
            throw new NullPointerException("value");
        }
        long remaining = unit.toNanos(timeout);
        long deadline = System.nanoTime() + remaining;
        synchronized (this) {
            while (size == capacity) {
                if (remaining <= 0) {
                    return false;
                }
                TimeUnit.NANOSECONDS.timedWait(this, remaining);
                remaining = deadline - System.nanoTime();
            }
            enqueue(value);
            return true;
        }
    }

    @Override
    public E take() throws InterruptedException {
        synchronized (this) {
            while (size == 0) {
                wait();
            }
            return dequeue();
        }
    }

    @Override
    public E poll() {
        synchronized (this) {
            return size == 0 ? null : dequeue();
        }
    }

    @Override
    public E poll(long timeout, TimeUnit unit) throws InterruptedException {
        long remaining = unit.toNanos(timeout);
        long deadline = System.nanoTime() + remaining;
        synchronized (this) {
            while (size == 0) {
                if (remaining <= 0) {
                    return null;
                }
                TimeUnit.NANOSECONDS.timedWait(this, remaining);
                remaining = deadline - System.nanoTime();
            }
            return dequeue();
        }
    }

    @Override
    public int remainingCapacity() {
        synchronized (this) {
            return capacity - size;
        }
    }

    @Override
    public boolean remove(Object value) {
        synchronized (this) {
            Node<E> previous = head;
            Node<E> current = head.next;
            while (current != null) {
                if (value == null ? current.value == null
                        : value.equals(current.value)) {
                    previous.next = current.next;
                    if (tail == current) {
                        tail = previous;
                    }
                    current.next = null;
                    current.value = null;
                    size--;
                    notifyAll();
                    return true;
                }
                previous = current;
                current = current.next;
            }
            return false;
        }
    }

    @Override
    public boolean contains(Object value) {
        synchronized (this) {
            for (Node<E> node = head.next; node != null; node = node.next) {
                if (value == null ? node.value == null
                        : value.equals(node.value)) {
                    return true;
                }
            }
            return false;
        }
    }

    @Override
    public boolean containsAll(Collection<?> values) {
        for (Object value : values) {
            if (!contains(value)) return false;
        }
        return true;
    }

    @Override
    public boolean addAll(Collection<? extends E> values) {
        boolean changed = false;
        for (E value : values) changed |= add(value);
        return changed;
    }

    @Override
    public boolean removeAll(Collection<?> values) {
        return removeMatching(values, false);
    }

    @Override
    public boolean retainAll(Collection<?> values) {
        return removeMatching(values, true);
    }

    private boolean removeMatching(
            Collection<?> values, boolean retain) {
        synchronized (this) {
            boolean changed = false;
            Node<E> previous = head;
            Node<E> current = head.next;
            while (current != null) {
                Node<E> next = current.next;
                if (values.contains(current.value) != retain) {
                    previous.next = next;
                    if (tail == current) tail = previous;
                    current.next = null;
                    current.value = null;
                    size--;
                    changed = true;
                } else {
                    previous = current;
                }
                current = next;
            }
            if (changed) notifyAll();
            return changed;
        }
    }

    @Override
    public int drainTo(Collection<? super E> target) {
        return drainTo(target, Integer.MAX_VALUE);
    }

    @Override
    public int drainTo(Collection<? super E> target, int maxElements) {
        if (target == null) {
            throw new NullPointerException("target");
        }
        if (target == this) {
            throw new IllegalArgumentException("target is this queue");
        }
        if (maxElements <= 0) {
            return 0;
        }
        synchronized (this) {
            int moved = 0;
            while (size > 0 && moved < maxElements) {
                target.add(dequeue());
                moved++;
            }
            return moved;
        }
    }

    @Override
    public boolean add(E value) {
        if (!offer(value)) {
            throw new IllegalStateException("queue is full");
        }
        return true;
    }

    @Override
    public E remove() {
        E value = poll();
        if (value == null) {
            throw new java.util.NoSuchElementException();
        }
        return value;
    }

    @Override
    public E element() {
        E value = peek();
        if (value == null) {
            throw new java.util.NoSuchElementException();
        }
        return value;
    }

    @Override
    public E peek() {
        synchronized (this) {
            return size == 0 ? null : head.next.value;
        }
    }

    @Override
    public int size() {
        synchronized (this) {
            return size;
        }
    }

    @Override
    public void clear() {
        synchronized (this) {
            while (size > 0) {
                dequeue();
            }
        }
    }

    @Override
    public boolean isEmpty() {
        return size() == 0;
    }

    @Override
    public java.util.Iterator<E> iterator() {
        return new java.util.Iterator<E>() {
            private Node<E> nextNode;

            {
                synchronized (LinkedBlockingQueue.this) {
                    nextNode = head.next;
                }
            }

            @Override
            public boolean hasNext() {
                return nextNode != null;
            }

            @Override
            public E next() {
                if (nextNode == null) {
                    throw new java.util.NoSuchElementException();
                }
                E value = nextNode.value;
                nextNode = nextNode.next;
                return value;
            }
        };
    }

    @Override
    public Object[] toArray() {
        synchronized (this) {
            Object[] values = new Object[size];
            int index = 0;
            for (Node<E> node = head.next; node != null; node = node.next) {
                values[index++] = node.value;
            }
            return values;
        }
    }

    @Override
    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] values) {
        synchronized (this) {
            T[] result = values.length >= size
                    ? values
                    : (T[]) Array.newInstance(
                            values.getClass().getComponentType(),
                            size);
            int index = 0;
            for (Node<E> node = head.next; node != null; node = node.next) {
                result[index++] = (T) node.value;
            }
            if (result.length > size) {
                result[size] = null;
            }
            return result;
        }
    }

    private void enqueue(E value) {
        Node<E> node = new Node<E>(value);
        tail.next = node;
        tail = node;
        size++;
        notifyAll();
    }

    private E dequeue() {
        Node<E> first = head.next;
        head.next = first.next;
        if (tail == first) {
            tail = head;
        }
        E value = first.value;
        first.value = null;
        first.next = null;
        size--;
        notifyAll();
        return value;
    }
}
