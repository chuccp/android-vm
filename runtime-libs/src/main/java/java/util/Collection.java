package java.util;

import java.util.function.Predicate;

public interface Collection<E> extends Iterable<E> {
    int size();

    boolean isEmpty();

    boolean contains(Object value);

    Iterator<E> iterator();

    Object[] toArray();

    <T> T[] toArray(T[] values);

    boolean add(E value);

    boolean remove(Object value);

    boolean containsAll(Collection<?> values);

    boolean addAll(Collection<? extends E> values);

    boolean removeAll(Collection<?> values);

    boolean retainAll(Collection<?> values);

    void clear();

    default boolean removeIf(Predicate<? super E> filter) {
        Objects.requireNonNull(filter);
        boolean removed = false;
        Iterator<E> values = iterator();
        while (values.hasNext()) {
            if (filter.test(values.next())) {
                values.remove();
                removed = true;
            }
        }
        return removed;
    }

    // ---- overlay：stream/parallelStream/spliterator/toArray(IntFunction)
    //（OpenJDK 11 默认方法；供 ServiceLoader.stream()、Collectors、Nodes 等编译）----
    default java.util.stream.Stream<E> stream() {
        return java.util.stream.StreamSupport.stream(spliterator(), false);
    }

    default java.util.stream.Stream<E> parallelStream() {
        return java.util.stream.StreamSupport.stream(spliterator(), true);
    }

    default Spliterator<E> spliterator() {
        return Spliterators.spliterator(this, 0);
    }

    default <T> T[] toArray(java.util.function.IntFunction<T[]> generator) {
        return toArray(generator.apply(size()));
    }
}
