package java.util;

import java.util.function.UnaryOperator;

public interface List<E> extends Collection<E> {
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

    boolean addAll(int index, Collection<? extends E> values);

    boolean removeAll(Collection<?> values);

    boolean retainAll(Collection<?> values);

    void clear();

    boolean equals(Object other);

    int hashCode();

    E get(int index);

    E set(int index, E value);

    void add(int index, E value);

    E remove(int index);

    int indexOf(Object value);

    int lastIndexOf(Object value);

    ListIterator<E> listIterator();

    ListIterator<E> listIterator(int index);

    List<E> subList(int fromIndex, int toIndex);

    default void replaceAll(UnaryOperator<E> operator) {
        Objects.requireNonNull(operator);
        ListIterator<E> values = listIterator();
        while (values.hasNext()) values.set(operator.apply(values.next()));
    }

    @SuppressWarnings("unchecked")
    default void sort(Comparator<? super E> comparator) {
        Object[] values = toArray();
        Arrays.sort((E[]) values, comparator);
        ListIterator<E> target = listIterator();
        for (Object value : values) target.set((E) value);
    }

    // ---- overlay（API 补全，供 ServiceLoader/ImmutableCollections/Collectors 等
    // 全量源编译闭包）：java.util.List.of / copyOf（OpenJDK 11 语义，委托
    // ImmutableCollections 不可变实现）----
    @SafeVarargs
    static <E> List<E> of(E... elements) {
        switch (elements.length) {
            case 0:
                @SuppressWarnings("unchecked")
                List<E> empty = (List<E>) ImmutableCollections.emptyList();
                return empty;
            case 1:
                return new ImmutableCollections.List12<>(elements[0]);
            case 2:
                return new ImmutableCollections.List12<>(elements[0], elements[1]);
            default:
                return new ImmutableCollections.ListN<>(elements);
        }
    }

    static <E> List<E> copyOf(Collection<? extends E> coll) {
        return ImmutableCollections.listCopy(coll);
    }
}
