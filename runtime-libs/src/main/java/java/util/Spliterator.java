package java.util;

public interface Spliterator<T> {
    int ORDERED = 0x00000010;
    int DISTINCT = 0x00000001;
    int SORTED = 0x00000004;
    int SIZED = 0x00000040;
    int NONNULL = 0x00000100;
    int IMMUTABLE = 0x00000400;
    int CONCURRENT = 0x00001000;
    int SUBSIZED = 0x00004000;

    boolean tryAdvance(java.util.function.Consumer<? super T> action);

    Spliterator<T> trySplit();

    long estimateSize();

    int characteristics();

    default void forEachRemaining(
            java.util.function.Consumer<? super T> action) {
        while (tryAdvance(action)) {
        }
    }

    default long getExactSizeIfKnown() {
        return (characteristics() & SIZED) == 0 ? -1L : estimateSize();
    }

    default boolean hasCharacteristics(int values) {
        return (characteristics() & values) == values;
    }

    interface OfPrimitive<T, T_CONS,
            T_SPLITR extends OfPrimitive<T, T_CONS, T_SPLITR>>
            extends Spliterator<T> {
        @Override
        T_SPLITR trySplit();

        boolean tryAdvance(T_CONS action);

        default void forEachRemaining(T_CONS action) {
            while (tryAdvance(action)) {
            }
        }
    }

    interface OfInt
            extends OfPrimitive<Integer, java.util.function.IntConsumer, OfInt> {
        @Override
        OfInt trySplit();

        @Override
        boolean tryAdvance(java.util.function.IntConsumer action);

        @Override
        default void forEachRemaining(
                java.util.function.IntConsumer action) {
            while (tryAdvance(action)) {
            }
        }

        @Override
        default boolean tryAdvance(
                java.util.function.Consumer<? super Integer> action) {
            java.util.Objects.requireNonNull(action);
            if (action instanceof java.util.function.IntConsumer) {
                return tryAdvance(
                        (java.util.function.IntConsumer) action);
            }
            return tryAdvance(
                    (java.util.function.IntConsumer) action::accept);
        }
    }

    interface OfLong
            extends OfPrimitive<Long, java.util.function.LongConsumer, OfLong> {
        @Override
        OfLong trySplit();

        @Override
        boolean tryAdvance(java.util.function.LongConsumer action);

        @Override
        default void forEachRemaining(
                java.util.function.LongConsumer action) {
            while (tryAdvance(action)) {
            }
        }

        @Override
        default boolean tryAdvance(
                java.util.function.Consumer<? super Long> action) {
            java.util.Objects.requireNonNull(action);
            if (action instanceof java.util.function.LongConsumer) {
                return tryAdvance(
                        (java.util.function.LongConsumer) action);
            }
            return tryAdvance(
                    (java.util.function.LongConsumer) action::accept);
        }
    }

    interface OfDouble
            extends OfPrimitive<Double, java.util.function.DoubleConsumer, OfDouble> {
        @Override
        OfDouble trySplit();

        @Override
        boolean tryAdvance(java.util.function.DoubleConsumer action);

        @Override
        default void forEachRemaining(
                java.util.function.DoubleConsumer action) {
            while (tryAdvance(action)) {
            }
        }

        @Override
        default boolean tryAdvance(
                java.util.function.Consumer<? super Double> action) {
            java.util.Objects.requireNonNull(action);
            if (action instanceof java.util.function.DoubleConsumer) {
                return tryAdvance(
                        (java.util.function.DoubleConsumer) action);
            }
            return tryAdvance(
                    (java.util.function.DoubleConsumer) action::accept);
        }
    }

    // ---- overlay：getComparator 默认方法（OpenJDK 11）----
    default java.util.Comparator<? super T> getComparator() {
        throw new IllegalStateException();
    }

}
