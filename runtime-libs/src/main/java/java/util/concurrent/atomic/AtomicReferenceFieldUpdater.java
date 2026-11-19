package java.util.concurrent.atomic;

import java.util.function.BinaryOperator;
import java.util.function.UnaryOperator;

public class AtomicReferenceFieldUpdater<T, V> {
    protected AtomicReferenceFieldUpdater() {
    }

    public static native <U, W> AtomicReferenceFieldUpdater<U, W> newUpdater(
            Class<U> tclass,
            Class<W> vclass,
            String fieldName);

    public native boolean compareAndSet(T obj, V expect, V update);

    public native boolean weakCompareAndSet(T obj, V expect, V update);

    public native void set(T obj, V newValue);

    public native void lazySet(T obj, V newValue);

    public native V get(T obj);

    public V getAndSet(T obj, V newValue) {
        V previous;
        do {
            previous = get(obj);
        } while (!compareAndSet(obj, previous, newValue));
        return previous;
    }

    public final V getAndUpdate(
            T obj,
            UnaryOperator<V> updateFunction) {
        V previous;
        V next;
        do {
            previous = get(obj);
            next = updateFunction.apply(previous);
        } while (!compareAndSet(obj, previous, next));
        return previous;
    }

    public final V updateAndGet(
            T obj,
            UnaryOperator<V> updateFunction) {
        V previous;
        V next;
        do {
            previous = get(obj);
            next = updateFunction.apply(previous);
        } while (!compareAndSet(obj, previous, next));
        return next;
    }

    public final V getAndAccumulate(
            T obj,
            V value,
            BinaryOperator<V> accumulatorFunction) {
        V previous;
        V next;
        do {
            previous = get(obj);
            next = accumulatorFunction.apply(previous, value);
        } while (!compareAndSet(obj, previous, next));
        return previous;
    }

    public final V accumulateAndGet(
            T obj,
            V value,
            BinaryOperator<V> accumulatorFunction) {
        V previous;
        V next;
        do {
            previous = get(obj);
            next = accumulatorFunction.apply(previous, value);
        } while (!compareAndSet(obj, previous, next));
        return next;
    }
}
