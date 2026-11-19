package java.util.concurrent.atomic;

import java.util.function.IntBinaryOperator;
import java.util.function.IntUnaryOperator;

public class AtomicIntegerFieldUpdater<T> {
    protected AtomicIntegerFieldUpdater() {
    }

    public static native <U> AtomicIntegerFieldUpdater<U> newUpdater(
            Class<U> tclass,
            String fieldName);

    public native boolean compareAndSet(T obj, int expect, int update);

    public native boolean weakCompareAndSet(T obj, int expect, int update);

    public native void set(T obj, int newValue);

    public native void lazySet(T obj, int newValue);

    public native int get(T obj);

    public int getAndSet(T obj, int newValue) {
        int previous;
        do {
            previous = get(obj);
        } while (!compareAndSet(obj, previous, newValue));
        return previous;
    }

    public native int getAndIncrement(T obj);

    public native int getAndDecrement(T obj);

    public native int getAndAdd(T obj, int delta);

    public native int incrementAndGet(T obj);

    public native int decrementAndGet(T obj);

    public native int addAndGet(T obj, int delta);

    public final int getAndUpdate(
            T obj,
            IntUnaryOperator updateFunction) {
        int previous;
        int next;
        do {
            previous = get(obj);
            next = updateFunction.applyAsInt(previous);
        } while (!compareAndSet(obj, previous, next));
        return previous;
    }

    public final int updateAndGet(
            T obj,
            IntUnaryOperator updateFunction) {
        int previous;
        int next;
        do {
            previous = get(obj);
            next = updateFunction.applyAsInt(previous);
        } while (!compareAndSet(obj, previous, next));
        return next;
    }

    public final int getAndAccumulate(
            T obj,
            int value,
            IntBinaryOperator accumulatorFunction) {
        int previous;
        int next;
        do {
            previous = get(obj);
            next = accumulatorFunction.applyAsInt(previous, value);
        } while (!compareAndSet(obj, previous, next));
        return previous;
    }

    public final int accumulateAndGet(
            T obj,
            int value,
            IntBinaryOperator accumulatorFunction) {
        int previous;
        int next;
        do {
            previous = get(obj);
            next = accumulatorFunction.applyAsInt(previous, value);
        } while (!compareAndSet(obj, previous, next));
        return next;
    }
}
