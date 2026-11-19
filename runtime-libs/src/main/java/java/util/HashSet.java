package java.util;

import java.io.Serializable;

public class HashSet<E> implements Set<E>, Cloneable, Serializable {
    private Object[] elements = new Object[8];
    private int size;

    public HashSet() {
    }

    public HashSet(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException();
        elements = new Object[capacity == 0 ? 1 : capacity];
    }

    public HashSet(int capacity, float loadFactor) {
        if (capacity < 0 || loadFactor <= 0) throw new IllegalArgumentException();
        elements = new Object[capacity == 0 ? 1 : capacity];
    }

    public HashSet(Collection<? extends E> source) {
        this();
        for (E value : source) add(value);
    }

    private static boolean equal(Object left, Object right) {
        return left == right || (left != null && left.equals(right));
    }

    private int indexOf(Object value) {
        for (int index = 0; index < size; index++) {
            if (equal(elements[index], value)) return index;
        }
        return -1;
    }

    private void grow() {
        if (size != elements.length) return;
        Object[] expanded = new Object[elements.length * 2];
        System.arraycopy(elements, 0, expanded, 0, size);
        elements = expanded;
    }

    @Override
    public int size() {
        return size;
    }

    @Override
    public boolean isEmpty() {
        return size == 0;
    }

    @Override
    public boolean contains(Object value) {
        return indexOf(value) >= 0;
    }

    @Override
    public Iterator<E> iterator() {
        return new Iterator<E>() {
            private int cursor;
            private int lastReturned = -1;

            @Override
            public boolean hasNext() {
                return cursor < size;
            }

            @Override
            @SuppressWarnings("unchecked")
            public E next() {
                if (cursor >= size) throw new NoSuchElementException();
                lastReturned = cursor;
                return (E) elements[cursor++];
            }

            @Override
            public void remove() {
                if (lastReturned < 0) {
                    throw new IllegalStateException();
                }
                int moved = size - lastReturned - 1;
                if (moved > 0) {
                    System.arraycopy(
                            elements,
                            lastReturned + 1,
                            elements,
                            lastReturned,
                            moved);
                }
                elements[--size] = null;
                cursor = lastReturned;
                lastReturned = -1;
            }
        };
    }

    @Override
    public Spliterator<E> spliterator() {
        return new Spliterator<E>() {
            private int cursor;

            @Override
            public boolean tryAdvance(
                    java.util.function.Consumer<? super E> action) {
                if (cursor >= size) return false;
                @SuppressWarnings("unchecked")
                E value = (E) elements[cursor++];
                action.accept(value);
                return true;
            }

            @Override
            public Spliterator<E> trySplit() {
                return null;
            }

            @Override
            public long estimateSize() {
                return size - cursor;
            }

            @Override
            public int characteristics() {
                return DISTINCT | SIZED;
            }
        };
    }

    @Override
    public Object[] toArray() {
        Object[] result = new Object[size];
        System.arraycopy(elements, 0, result, 0, size);
        return result;
    }

    @Override
    @SuppressWarnings("unchecked")
    public <T> T[] toArray(T[] target) {
        Object[] result =
                target.length >= size ? target : new Object[size];
        System.arraycopy(elements, 0, result, 0, size);
        if (result.length > size) result[size] = null;
        return (T[]) result;
    }

    @Override
    public boolean add(E value) {
        if (contains(value)) return false;
        grow();
        elements[size++] = value;
        return true;
    }

    @Override
    public boolean remove(Object value) {
        int index = indexOf(value);
        if (index < 0) return false;
        int moved = size - index - 1;
        if (moved > 0) {
            System.arraycopy(elements, index + 1, elements, index, moved);
        }
        elements[--size] = null;
        return true;
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
        boolean changed = false;
        for (Object value : values) changed |= remove(value);
        return changed;
    }

    @Override
    public boolean retainAll(Collection<?> values) {
        boolean changed = false;
        for (int index = size - 1; index >= 0; index--) {
            if (!values.contains(elements[index])) {
                remove(elements[index]);
                changed = true;
            }
        }
        return changed;
    }

    @Override
    public void clear() {
        for (int index = 0; index < size; index++) elements[index] = null;
        size = 0;
    }

    // ---- overlay：HashSet(int,float,boolean) 构造（OpenJDK 11 形状）----
    HashSet(int initialCapacity, float loadFactor, boolean dummy) {
        this(initialCapacity);
    }

}
