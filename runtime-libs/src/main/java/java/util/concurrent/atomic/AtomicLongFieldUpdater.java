package java.util.concurrent.atomic;

import java.util.function.LongBinaryOperator;
import java.util.function.LongUnaryOperator;

public class AtomicLongFieldUpdater<T> {
    protected AtomicLongFieldUpdater() {
    }

    public static native <U> AtomicLongFieldUpdater<U> newUpdater(
            Class<U> tclass,
            String fieldName);

    public native boolean compareAndSet(T obj, long expect, long update);

    public native boolean weakCompareAndSet(T obj, long expect, long update);

    public native void set(T obj, long newValue);

    public native void lazySet(T obj, long newValue);

    public native long get(T obj);

    public long getAndSet(T obj, long newValue) {
        long previous;
        do {
            previous = get(obj);
        } while (!compareAndSet(obj, previous, newValue));
        return previous;
    }

    public native long getAndIncrement(T obj);

    public native long getAndDecrement(T obj);

    public native long getAndAdd(T obj, long delta);

    public native long incrementAndGet(T obj);

    public native long decrementAndGet(T obj);

    public native long addAndGet(T obj, long delta);

    public final long getAndUpdate(
            T obj,
            LongUnaryOperator updateFunction) {
        long previous;
        long next;
        do {
            previous = get(obj);
            next = updateFunction.applyAsLong(previous);
        } while (!compareAndSet(obj, previous, next));
        return previous;
    }

    public final long updateAndGet(
            T obj,
            LongUnaryOperator updateFunction) {
        long previous;
        long next;
        do {
            previous = get(obj);
            next = updateFunction.applyAsLong(previous);
        } while (!compareAndSet(obj, previous, next));
        return next;
    }

    public final long getAndAccumulate(
            T obj,
            long value,
            LongBinaryOperator accumulatorFunction) {
        long previous;
        long next;
        do {
            previous = get(obj);
            next = accumulatorFunction.applyAsLong(previous, value);
        } while (!compareAndSet(obj, previous, next));
        return previous;
    }

    public final long accumulateAndGet(
            T obj,
            long value,
            LongBinaryOperator accumulatorFunction) {
        long previous;
        long next;
        do {
            previous = get(obj);
            next = accumulatorFunction.applyAsLong(previous, value);
        } while (!compareAndSet(obj, previous, next));
        return next;
    }
}
