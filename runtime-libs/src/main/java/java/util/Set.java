package java.util;

public interface Set<E> extends Collection<E> {

    // ---- overlay：java.util.Set.of / copyOf（OpenJDK 11 语义，委托 ImmutableCollections）----
    @SafeVarargs
    static <E> Set<E> of(E... elements) {
        switch (elements.length) {
            case 0:
                @SuppressWarnings("unchecked")
                Set<E> empty = (Set<E>) ImmutableCollections.emptySet();
                return empty;
            case 1:
                return new ImmutableCollections.Set12<>(elements[0]);
            case 2:
                return new ImmutableCollections.Set12<>(elements[0], elements[1]);
            default:
                return new ImmutableCollections.SetN<>(elements);
        }
    }

    static <E> Set<E> copyOf(Collection<? extends E> coll) {
        Object[] array = coll.toArray();
        @SuppressWarnings("unchecked")
        E[] elements = (E[]) array;
        return Set.of(elements);
    }
}
