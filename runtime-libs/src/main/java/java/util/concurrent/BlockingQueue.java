package java.util.concurrent;

import java.util.Collection;

public interface BlockingQueue<E> extends java.util.Queue<E> {
    void put(E value) throws InterruptedException;
    boolean offer(E value);
    boolean offer(E value, long timeout, TimeUnit unit)
            throws InterruptedException;
    E take() throws InterruptedException;
    E poll(long timeout, TimeUnit unit) throws InterruptedException;
    int remainingCapacity();
    boolean remove(Object value);
    boolean contains(Object value);
    int drainTo(Collection<? super E> target);
    int drainTo(Collection<? super E> target, int maxElements);
}
