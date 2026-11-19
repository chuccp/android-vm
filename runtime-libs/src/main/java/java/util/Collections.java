package java.util;

import java.io.Serializable;

public class Collections {
    // 稳定插入排序（直接 compareTo，不经 TimSort/ArrayList.sort）。
    // 语义：Collections.sort 保证稳定；列表多为启动期小集合（Okio Options
    // 等），复杂度可接受。TimSort 路径在当前 VM 实测产出未排序列表。
    public static <T extends Comparable<? super T>>
            void sort(List<T> list) {
        sort(list, null);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public static <T> void sort(
            List<T> list, Comparator<? super T> comparator) {
        Objects.requireNonNull(list);
        int size = list.size();
        for (int i = 1; i < size; i++) {
            T key = list.get(i);
            int j = i - 1;
            while (j >= 0) {
                T previous = list.get(j);
                int comparison = comparator != null
                        ? comparator.compare(previous, key)
                        : ((Comparable) previous).compareTo(key);
                if (comparison <= 0) break;
                list.set(j + 1, previous);
                j--;
            }
            list.set(j + 1, key);
        }
    }

    public static void reverse(List<?> list) {
        int left = 0;
        int right = list.size() - 1;
        while (left < right) {
            Object value = list.get(left);
            @SuppressWarnings("unchecked")
            List<Object> values = (List<Object>) list;
            values.set(left, values.get(right));
            values.set(right, value);
            left++;
            right--;
        }
    }

    @SuppressWarnings("unchecked")
    public static <T> int binarySearch(
            List<? extends Comparable<? super T>> list, T key) {
        // 直调 compareTo（不经 lambda comparator）：自然序搜索语义不变，
        // 同时避开 VM lambda 参数序/链接面差异（Okio Options.of 依赖本路径）。
        Objects.requireNonNull(list);
        int low = 0;
        int high = list.size() - 1;
        while (low <= high) {
            int middle = (low + high) >>> 1;
            @SuppressWarnings("unchecked")
            Comparable<? super T> middleValue =
                    (Comparable<? super T>) list.get(middle);
            int comparison = middleValue.compareTo(key);
            if (comparison < 0) {
                low = middle + 1;
            } else if (comparison > 0) {
                high = middle - 1;
            } else {
                return middle;
            }
        }
        return -(low + 1);
    }

    public static <T> int binarySearch(
            List<? extends T> list,
            T key,
            Comparator<? super T> comparator) {
        Objects.requireNonNull(list);
        Objects.requireNonNull(comparator);
        int low = 0;
        int high = list.size() - 1;
        while (low <= high) {
            int middle = (low + high) >>> 1;
            int comparison = comparator.compare(
                    list.get(middle), key);
            if (comparison < 0) {
                low = middle + 1;
            } else if (comparison > 0) {
                high = middle - 1;
            } else {
                return middle;
            }
        }
        return -(low + 1);
    }

    public static <T extends Comparable<? super T>>
            Comparator<T> reverseOrder() {
        return (first, second) -> second.compareTo(first);
    }

    public static <T> Comparator<T> reverseOrder(
            Comparator<T> comparator) {
        Objects.requireNonNull(comparator);
        return (first, second) -> comparator.compare(second, first);
    }

    @SafeVarargs
    public static <T> boolean addAll(
            Collection<? super T> target,
            T... values) {
        boolean changed = false;
        for (T value : values) changed |= target.add(value);
        return changed;
    }

    public static final Map<Object, Object> EMPTY_MAP =
            new EmptyMap<>();
    public static final List<Object> EMPTY_LIST =
            new EmptyList<>();
    public static final Set<Object> EMPTY_SET =
            new EmptyImmutableContainer<>();

    private Collections() {
    }

    // 空 Sorted/Navigable 容器（JDK 为 VM 侧单例；本实现以不可变包装承接）
    private static final SortedMap<Object, Object> EMPTY_SORTED_MAP =
            new UnmodifiableNavigableMap<Object, Object>(new java.util.TreeMap<Object, Object>());
    private static final SortedSet<Object> EMPTY_SORTED_SET =
            new UnmodifiableNavigableSet<Object>(new java.util.TreeSet<Object>());

    private static SortedMap<Object, Object> emptySortedMapNative() {
        return EMPTY_SORTED_MAP;
    }

    private static SortedSet<Object> emptySortedSetNative() {
        return EMPTY_SORTED_SET;
    }

    @SuppressWarnings("unchecked")
    public static <K, V> Map<K, V> emptyMap() {
        return (Map<K, V>) EMPTY_MAP;
    }

    @SuppressWarnings("unchecked")
    public static <T> List<T> emptyList() {
        return (List<T>) EMPTY_LIST;
    }

    @SuppressWarnings("unchecked")
    public static <T> Set<T> emptySet() {
        return (Set<T>) EMPTY_SET;
    }

    @SuppressWarnings("unchecked")
    public static <K, V> SortedMap<K, V> emptySortedMap() {
        return (SortedMap<K, V>) (Object) emptySortedMapNative();
    }

    @SuppressWarnings("unchecked")
    public static <T> SortedSet<T> emptySortedSet() {
        return (SortedSet<T>) (Object) emptySortedSetNative();
    }

    @SuppressWarnings("unchecked")
    public static <K, V> NavigableMap<K, V> emptyNavigableMap() {
        return (NavigableMap<K, V>) (Object) emptySortedMapNative();
    }

    @SuppressWarnings("unchecked")
    public static <T> NavigableSet<T> emptyNavigableSet() {
        return (NavigableSet<T>) (Object) emptySortedSetNative();
    }

    public static <T> Collection<T> unmodifiableCollection(
            Collection<? extends T> collection) {
        return new UnmodifiableCollection<T>(collection);
    }

    public static <T> List<T> unmodifiableList(
            List<? extends T> list) {
        return new UnmodifiableList<T>(list);
    }

    public static <T> Set<T> unmodifiableSet(
            Set<? extends T> set) {
        return new UnmodifiableSet<T>(set);
    }

    public static <K, V> Map<K, V> unmodifiableMap(
            Map<? extends K, ? extends V> map) {
        return new UnmodifiableMap<K, V>(map);
    }

    public static <T> SortedSet<T> unmodifiableSortedSet(
            SortedSet<T> set) {
        return new UnmodifiableSortedSet<T>(set);
    }

    @SuppressWarnings("unchecked")
    public static <K, V> SortedMap<K, V> unmodifiableSortedMap(
            SortedMap<K, ? extends V> map) {
        return (SortedMap<K, V>) (SortedMap<?, ?>) new UnmodifiableSortedMap<Object, Object>(
                (SortedMap<Object, Object>) map);
    }

    public static <T> NavigableSet<T> unmodifiableNavigableSet(
            NavigableSet<T> set) {
        return new UnmodifiableNavigableSet<T>(set);
    }

    @SuppressWarnings("unchecked")
    public static <K, V> NavigableMap<K, V> unmodifiableNavigableMap(
            NavigableMap<K, ? extends V> map) {
        return (NavigableMap<K, V>) (NavigableMap<?, ?>) new UnmodifiableNavigableMap<Object, Object>(
                (NavigableMap<Object, Object>) map);
    }

    // ---- 不可变视图（JDK/Android 同款包装；写操作 UOE）----
    static class UnmodifiableCollection<E> implements Collection<E>, Serializable {
        private static final long serialVersionUID = 1L;
        final Collection<? extends E> c;

        UnmodifiableCollection(Collection<? extends E> c) {
            this.c = Objects.requireNonNull(c);
        }

        @Override public int size() { return c.size(); }
        @Override public boolean isEmpty() { return c.isEmpty(); }
        @Override public boolean contains(Object o) { return c.contains(o); }
        @Override public Object[] toArray() { return c.toArray(); }
        @Override public <T> T[] toArray(T[] a) { return c.toArray(a); }
        @Override public String toString() { return c.toString(); }

        @Override
        public Iterator<E> iterator() {
            return new Iterator<E>() {
                private final Iterator<? extends E> i = c.iterator();
                @Override public boolean hasNext() { return i.hasNext(); }
                @Override public E next() { return i.next(); }
                @Override public void remove() {
                    throw new UnsupportedOperationException();
                }
            };
        }

        @Override public boolean add(E e) {
            throw new UnsupportedOperationException();
        }
        @Override public boolean remove(Object o) {
            throw new UnsupportedOperationException();
        }
        @Override public boolean containsAll(Collection<?> coll) { return c.containsAll(coll); }
        @Override public boolean addAll(Collection<? extends E> coll) {
            throw new UnsupportedOperationException();
        }
        @Override public boolean removeAll(Collection<?> coll) {
            throw new UnsupportedOperationException();
        }
        @Override public boolean retainAll(Collection<?> coll) {
            throw new UnsupportedOperationException();
        }
        @Override public void clear() {
            throw new UnsupportedOperationException();
        }
    }

    static class UnmodifiableSet<E> extends UnmodifiableCollection<E> implements Set<E> {
        private static final long serialVersionUID = 1L;

        UnmodifiableSet(Set<? extends E> s) {
            super(s);
        }

        @Override public boolean equals(Object o) { return o == this || c.equals(o); }
        @Override public int hashCode() { return c.hashCode(); }
    }

    static class UnmodifiableList<E> extends UnmodifiableCollection<E> implements List<E> {
        private static final long serialVersionUID = 1L;
        final List<? extends E> list;

        UnmodifiableList(List<? extends E> list) {
            super(list);
            this.list = list;
        }

        @Override public boolean equals(Object o) { return o == this || list.equals(o); }
        @Override public int hashCode() { return list.hashCode(); }
        @Override public E get(int index) { return list.get(index); }
        @Override public int indexOf(Object o) { return list.indexOf(o); }
        @Override public int lastIndexOf(Object o) { return list.lastIndexOf(o); }
        @Override public E set(int index, E element) {
            throw new UnsupportedOperationException();
        }
        @Override public void add(int index, E element) {
            throw new UnsupportedOperationException();
        }
        @Override public E remove(int index) {
            throw new UnsupportedOperationException();
        }
        @Override public boolean addAll(int index, Collection<? extends E> c) {
            throw new UnsupportedOperationException();
        }

        @Override
        public ListIterator<E> listIterator() {
            return listIterator(0);
        }

        @Override
        public ListIterator<E> listIterator(final int index) {
            return new ListIterator<E>() {
                private final ListIterator<? extends E> i = list.listIterator(index);
                @Override public boolean hasNext() { return i.hasNext(); }
                @Override public E next() { return i.next(); }
                @Override public boolean hasPrevious() { return i.hasPrevious(); }
                @Override public E previous() { return i.previous(); }
                @Override public int nextIndex() { return i.nextIndex(); }
                @Override public int previousIndex() { return i.previousIndex(); }
                @Override public void remove() {
                    throw new UnsupportedOperationException();
                }
                @Override public void set(E e) {
                    throw new UnsupportedOperationException();
                }
                @Override public void add(E e) {
                    throw new UnsupportedOperationException();
                }
            };
        }

        @Override
        public List<E> subList(int fromIndex, int toIndex) {
            return new UnmodifiableList<E>(list.subList(fromIndex, toIndex));
        }
    }

    static class UnmodifiableMap<K, V> implements Map<K, V>, Serializable {
        private static final long serialVersionUID = 1L;
        final Map<? extends K, ? extends V> m;

        UnmodifiableMap(Map<? extends K, ? extends V> m) {
            this.m = Objects.requireNonNull(m);
        }

        @Override public int size() { return m.size(); }
        @Override public boolean isEmpty() { return m.isEmpty(); }
        @Override public boolean containsKey(Object key) { return m.containsKey(key); }
        @Override public boolean containsValue(Object value) { return m.containsValue(value); }
        @Override public V get(Object key) { return m.get(key); }
        @Override public String toString() { return m.toString(); }
        @Override public boolean equals(Object o) { return o == this || m.equals(o); }
        @Override public int hashCode() { return m.hashCode(); }

        @Override
        public Set<K> keySet() {
            return new UnmodifiableSet<K>(m.keySet());
        }

        @Override
        public Collection<V> values() {
            return new UnmodifiableCollection<V>(m.values());
        }

        @Override
        public Set<Map.Entry<K, V>> entrySet() {
            return new UnmodifiableEntrySet<K, V>(m.entrySet());
        }

        @Override public V put(K key, V value) {
            throw new UnsupportedOperationException();
        }
        @Override public V remove(Object key) {
            throw new UnsupportedOperationException();
        }
        @Override public void putAll(Map<? extends K, ? extends V> map) {
            throw new UnsupportedOperationException();
        }
        @Override public void clear() {
            throw new UnsupportedOperationException();
        }
    }

    static class UnmodifiableEntrySet<K, V> extends AbstractSet<Map.Entry<K, V>> {
        private final Set<? extends Map.Entry<? extends K, ? extends V>> entries;

        UnmodifiableEntrySet(Set<? extends Map.Entry<? extends K, ? extends V>> entries) {
            this.entries = entries;
        }

        @Override public int size() { return entries.size(); }
        @Override public boolean isEmpty() { return entries.isEmpty(); }
        @Override public boolean contains(Object o) { return entries.contains(o); }

        @Override
        public Iterator<Map.Entry<K, V>> iterator() {
            return new Iterator<Map.Entry<K, V>>() {
                private final Iterator<? extends Map.Entry<? extends K, ? extends V>> i =
                        entries.iterator();
                @Override public boolean hasNext() { return i.hasNext(); }
                @Override public Map.Entry<K, V> next() {
                    return new UnmodifiableEntry<K, V>(i.next());
                }
                @Override public void remove() {
                    throw new UnsupportedOperationException();
                }
            };
        }
    }

    static class UnmodifiableEntry<K, V> implements Map.Entry<K, V> {
        private final Map.Entry<? extends K, ? extends V> e;

        UnmodifiableEntry(Map.Entry<? extends K, ? extends V> e) {
            this.e = e;
        }

        @Override public K getKey() { return e.getKey(); }
        @Override public V getValue() { return e.getValue(); }
        @Override public V setValue(V value) {
            throw new UnsupportedOperationException();
        }
        @Override public boolean equals(Object o) { return e.equals(o); }
        @Override public int hashCode() { return e.hashCode(); }
        @Override public String toString() { return e.toString(); }
    }

    static class UnmodifiableSortedSet<E> extends UnmodifiableSet<E> implements SortedSet<E> {
        private static final long serialVersionUID = 1L;
        private final SortedSet<E> ss;

        UnmodifiableSortedSet(SortedSet<E> s) {
            super(s);
            ss = s;
        }

        @Override public Comparator<? super E> comparator() { return ss.comparator(); }
        @Override public SortedSet<E> subSet(E from, E to) {
            return new UnmodifiableSortedSet<E>(ss.subSet(from, to));
        }
        @Override public SortedSet<E> headSet(E to) {
            return new UnmodifiableSortedSet<E>(ss.headSet(to));
        }
        @Override public SortedSet<E> tailSet(E from) {
            return new UnmodifiableSortedSet<E>(ss.tailSet(from));
        }
        @Override public E first() { return ss.first(); }
        @Override public E last() { return ss.last(); }
    }

    static class UnmodifiableNavigableSet<E> extends UnmodifiableSortedSet<E>
            implements NavigableSet<E> {
        private static final long serialVersionUID = 1L;
        private final NavigableSet<E> ns;

        UnmodifiableNavigableSet(NavigableSet<E> s) {
            super(s);
            ns = s;
        }

        @Override public E lower(E e) { return ns.lower(e); }
        @Override public E floor(E e) { return ns.floor(e); }
        @Override public E ceiling(E e) { return ns.ceiling(e); }
        @Override public E higher(E e) { return ns.higher(e); }
        @Override public E pollFirst() {
            throw new UnsupportedOperationException();
        }
        @Override public E pollLast() {
            throw new UnsupportedOperationException();
        }
        @Override public NavigableSet<E> descendingSet() {
            return new UnmodifiableNavigableSet<E>(ns.descendingSet());
        }
        @Override public Iterator<E> descendingIterator() {
            return descendingSet().iterator();
        }
        @Override public NavigableSet<E> subSet(E from, boolean fromInclusive, E to,
                boolean toInclusive) {
            return new UnmodifiableNavigableSet<E>(
                    ns.subSet(from, fromInclusive, to, toInclusive));
        }
        @Override public NavigableSet<E> headSet(E to, boolean inclusive) {
            return new UnmodifiableNavigableSet<E>(ns.headSet(to, inclusive));
        }
        @Override public NavigableSet<E> tailSet(E from, boolean inclusive) {
            return new UnmodifiableNavigableSet<E>(ns.tailSet(from, inclusive));
        }
    }

    static class UnmodifiableSortedMap<K, V> extends UnmodifiableMap<K, V>
            implements SortedMap<K, V> {
        private static final long serialVersionUID = 1L;
        private final SortedMap<K, ? extends V> sm;

        UnmodifiableSortedMap(SortedMap<K, ? extends V> m) {
            super(m);
            sm = m;
        }

        @Override public Comparator<? super K> comparator() { return sm.comparator(); }
        @Override public K firstKey() { return sm.firstKey(); }
        @Override public K lastKey() { return sm.lastKey(); }

        @Override
        public SortedMap<K, V> subMap(K fromKey, K toKey) {
            return unmodifiableSortedMap(sm.subMap(fromKey, toKey));
        }

        @Override
        public SortedMap<K, V> headMap(K toKey) {
            return unmodifiableSortedMap(sm.headMap(toKey));
        }

        @Override
        public SortedMap<K, V> tailMap(K fromKey) {
            return unmodifiableSortedMap(sm.tailMap(fromKey));
        }
    }

    static class UnmodifiableNavigableMap<K, V> extends UnmodifiableSortedMap<K, V>
            implements NavigableMap<K, V> {
        private static final long serialVersionUID = 1L;
        private final NavigableMap<K, ? extends V> nm;

        UnmodifiableNavigableMap(NavigableMap<K, ? extends V> m) {
            super(m);
            nm = m;
        }

        @Override public Map.Entry<K, V> lowerEntry(K key) { return wrapEntry(nm.lowerEntry(key)); }
        @Override public K lowerKey(K key) { return nm.lowerKey(key); }
        @Override public Map.Entry<K, V> floorEntry(K key) { return wrapEntry(nm.floorEntry(key)); }
        @Override public K floorKey(K key) { return nm.floorKey(key); }
        @Override public Map.Entry<K, V> ceilingEntry(K key) {
            return wrapEntry(nm.ceilingEntry(key));
        }
        @Override public K ceilingKey(K key) { return nm.ceilingKey(key); }
        @Override public Map.Entry<K, V> higherEntry(K key) {
            return wrapEntry(nm.higherEntry(key));
        }
        @Override public K higherKey(K key) { return nm.higherKey(key); }
        @Override public Map.Entry<K, V> firstEntry() { return wrapEntry(nm.firstEntry()); }
        @Override public Map.Entry<K, V> lastEntry() { return wrapEntry(nm.lastEntry()); }
        @Override public Map.Entry<K, V> pollFirstEntry() {
            throw new UnsupportedOperationException();
        }
        @Override public Map.Entry<K, V> pollLastEntry() {
            throw new UnsupportedOperationException();
        }

        @Override
        public NavigableMap<K, V> descendingMap() {
            return new UnmodifiableNavigableMap<K, V>(nm.descendingMap());
        }

        @Override
        public NavigableSet<K> navigableKeySet() {
            return new UnmodifiableNavigableSet<K>(nm.navigableKeySet());
        }

        @Override
        public NavigableSet<K> descendingKeySet() {
            return new UnmodifiableNavigableSet<K>(nm.descendingKeySet());
        }

        @Override
        public NavigableMap<K, V> subMap(K fromKey, boolean fromInclusive, K toKey,
                boolean toInclusive) {
            return new UnmodifiableNavigableMap<K, V>(
                    nm.subMap(fromKey, fromInclusive, toKey, toInclusive));
        }

        @Override
        public NavigableMap<K, V> headMap(K toKey, boolean inclusive) {
            return new UnmodifiableNavigableMap<K, V>(nm.headMap(toKey, inclusive));
        }

        @Override
        public NavigableMap<K, V> tailMap(K fromKey, boolean inclusive) {
            return new UnmodifiableNavigableMap<K, V>(nm.tailMap(fromKey, inclusive));
        }

        private static <K, V> Map.Entry<K, V> wrapEntry(Map.Entry<K, ? extends V> e) {
            return e == null ? null : new UnmodifiableEntry<K, V>(e);
        }
    }

    private static final class EmptyIterator<T>
            implements Iterator<T>, ListIterator<T> {
        @Override
        public boolean hasNext() {
            return false;
        }

        @Override
        public T next() {
            throw new NoSuchElementException();
        }

        @Override
        public boolean hasPrevious() {
            return false;
        }

        @Override
        public T previous() {
            throw new NoSuchElementException();
        }

        @Override
        public int nextIndex() {
            return 0;
        }

        @Override
        public int previousIndex() {
            return -1;
        }

        @Override
        public void remove() {
            throw new IllegalStateException();
        }

        @Override
        public void set(T value) {
            throw new IllegalStateException();
        }

        @Override
        public void add(T value) {
            throw new UnsupportedOperationException();
        }

        public void forEachRemaining(
                java.util.function.Consumer<? super T> action) {
            Objects.requireNonNull(action);
        }
    }

    private static final class EmptyList<E>
            implements List<E>, RandomAccess, Serializable {
        private static final long serialVersionUID = 1L;

        @Override
        public int size() {
            return 0;
        }

        @Override
        public boolean isEmpty() {
            return true;
        }

        @Override
        public boolean contains(Object value) {
            return false;
        }

        @Override
        public Iterator<E> iterator() {
            return new EmptyIterator<>();
        }

        @Override
        public Object[] toArray() {
            return new Object[0];
        }

        @Override
        public <T> T[] toArray(T[] array) {
            Objects.requireNonNull(array);
            if (array.length != 0) {
                array[0] = null;
            }
            return array;
        }

        @Override
        public boolean add(E value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean remove(Object value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean containsAll(Collection<?> values) {
            return Objects.requireNonNull(values).isEmpty();
        }

        @Override
        public boolean addAll(Collection<? extends E> values) {
            if (!Objects.requireNonNull(values).isEmpty()) {
                throw new UnsupportedOperationException();
            }
            return false;
        }

        @Override
        public boolean addAll(int index, Collection<? extends E> values) {
            if (index < 0 || index > 0) {
                throw new IndexOutOfBoundsException(
                        "Index: " + index + ", Size: 0");
            }
            return addAll(values);
        }

        @Override
        public boolean removeAll(Collection<?> values) {
            Objects.requireNonNull(values);
            return false;
        }

        @Override
        public boolean retainAll(Collection<?> values) {
            Objects.requireNonNull(values);
            return false;
        }

        @Override
        public void clear() {
        }

        @Override
        public E get(int index) {
            throw new IndexOutOfBoundsException(
                    "Index: " + index + ", Size: 0");
        }

        @Override
        public E set(int index, E value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public void add(int index, E value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public E remove(int index) {
            throw new UnsupportedOperationException();
        }

        @Override
        public int indexOf(Object value) {
            return -1;
        }

        @Override
        public int lastIndexOf(Object value) {
            return -1;
        }

        @Override
        public ListIterator<E> listIterator() {
            return new EmptyIterator<>();
        }

        @Override
        public ListIterator<E> listIterator(int index) {
            if (index != 0) {
                throw new IndexOutOfBoundsException(
                        "Index: " + index + ", Size: 0");
            }
            return new EmptyIterator<>();
        }

        @Override
        public List<E> subList(int fromIndex, int toIndex) {
            if (fromIndex != 0 || toIndex != 0) {
                throw new IndexOutOfBoundsException(
                        "fromIndex: " + fromIndex +
                                ", toIndex: " + toIndex +
                                ", Size: 0");
            }
            return this;
        }

        @Override
        public boolean equals(Object other) {
            return other instanceof List &&
                    ((List<?>) other).isEmpty();
        }

        @Override
        public int hashCode() {
            return 1;
        }

        @Override
        public String toString() {
            return "[]";
        }
    }

    private static final class EmptyImmutableContainer<E>
            implements Set<E>, Serializable {
        private static final long serialVersionUID = 1L;

        @Override
        public int size() {
            return 0;
        }

        @Override
        public boolean isEmpty() {
            return true;
        }

        @Override
        public boolean contains(Object value) {
            return false;
        }

        @Override
        public Iterator<E> iterator() {
            return new EmptyIterator<>();
        }

        @Override
        public Object[] toArray() {
            return new Object[0];
        }

        @Override
        public <T> T[] toArray(T[] array) {
            Objects.requireNonNull(array);
            if (array.length != 0) {
                array[0] = null;
            }
            return array;
        }

        @Override
        public boolean add(E value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean remove(Object value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public boolean containsAll(Collection<?> values) {
            return Objects.requireNonNull(values).isEmpty();
        }

        @Override
        public boolean addAll(Collection<? extends E> values) {
            if (!Objects.requireNonNull(values).isEmpty()) {
                throw new UnsupportedOperationException();
            }
            return false;
        }

        @Override
        public boolean removeAll(Collection<?> values) {
            Objects.requireNonNull(values);
            return false;
        }

        @Override
        public boolean retainAll(Collection<?> values) {
            Objects.requireNonNull(values);
            return false;
        }

        @Override
        public void clear() {
        }

        @Override
        public boolean equals(Object other) {
            return other instanceof Set &&
                    ((Set<?>) other).isEmpty();
        }

        @Override
        public int hashCode() {
            return 0;
        }

        @Override
        public String toString() {
            return "{}";
        }
    }

    private static final class EmptyMap<K, V>
            implements Map<K, V>, Serializable {
        private static final long serialVersionUID = 1L;

        @Override
        public int size() {
            return 0;
        }

        @Override
        public boolean isEmpty() {
            return true;
        }

        @Override
        public boolean containsKey(Object key) {
            return false;
        }

        @Override
        public boolean containsValue(Object value) {
            return false;
        }

        @Override
        public V get(Object key) {
            return null;
        }

        @Override
        public V put(K key, V value) {
            throw new UnsupportedOperationException();
        }

        @Override
        public V remove(Object key) {
            return null;
        }

        @Override
        public void putAll(Map<? extends K, ? extends V> values) {
            if (!Objects.requireNonNull(values).isEmpty()) {
                throw new UnsupportedOperationException();
            }
        }

        @Override
        public void clear() {
        }

        @Override
        public Set<K> keySet() {
            return emptySet();
        }

        @Override
        public Collection<V> values() {
            return emptyList();
        }

        @Override
        public Set<Entry<K, V>> entrySet() {
            return emptySet();
        }

        @Override
        public boolean equals(Object other) {
            return other instanceof Map &&
                    ((Map<?, ?>) other).isEmpty();
        }

        @Override
        public int hashCode() {
            return 0;
        }

        @Override
        public String toString() {
            return "{}";
        }
    }

    // ---- overlay：emptyEnumeration/emptyIterator/enumeration/nCopies/
    // indexOfSubList/newSetFromMap/singleton*/checkedMap/synchronized* API 补全
    //（OpenJDK 11 语义；供 ServiceLoader、Hashtable、Security、Collectors 等编译）----
    @SuppressWarnings("unchecked")
    public static <T> Enumeration<T> emptyEnumeration() {
        return (Enumeration<T>) EmptyEnumeration.EMPTY;
    }

    @SuppressWarnings("unchecked")
    public static <T> Iterator<T> emptyIterator() {
        return (Iterator<T>) new EmptyIterator<>();
    }

    @SuppressWarnings("unchecked")
    public static <T> ListIterator<T> emptyListIterator() {
        return (ListIterator<T>) new EmptyIterator<>();
    }

    private static final class EmptyEnumeration<E> implements Enumeration<E> {
        static final EmptyEnumeration<Object> EMPTY = new EmptyEnumeration<>();

        public boolean hasMoreElements() {
            return false;
        }

        public E nextElement() {
            throw new NoSuchElementException();
        }
    }

    public static <T> Enumeration<T> enumeration(Collection<T> values) {
        return new Enumeration<T>() {
            private final Iterator<T> it = values.iterator();

            public boolean hasMoreElements() {
                return it.hasNext();
            }

            public T nextElement() {
                return it.next();
            }
        };
    }

    public static <T> List<T> nCopies(int count, T value) {
        if (count < 0) {
            throw new IllegalArgumentException("count: " + count);
        }
        List<T> list = new ArrayList<>(count);
        for (int i = 0; i < count; i++) {
            list.add(value);
        }
        return list;
    }

    public static int indexOfSubList(List<?> source, List<?> target) {
        int sourceSize = source.size();
        int targetSize = target.size();
        if (sourceSize < targetSize) {
            return -1;
        }
        int max = sourceSize - targetSize;
        for (int i = 0; i <= max; i++) {
            int j = 0;
            while (j < targetSize && source.get(i + j).equals(target.get(j))) {
                j++;
            }
            if (j == targetSize) {
                return i;
            }
        }
        return -1;
    }

    public static <T> Set<T> newSetFromMap(Map<T, Boolean> map) {
        return new Set<T>() {
            @Override public int size() { return map.size(); }
            @Override public boolean isEmpty() { return map.isEmpty(); }
            @Override public boolean contains(Object o) { return map.containsKey(o); }
            @Override public boolean add(T t) { return map.put(t, Boolean.TRUE) == null; }
            @Override public boolean remove(Object o) { return map.remove(o) != null; }
            @Override public void clear() { map.clear(); }
            @Override public Iterator<T> iterator() { return map.keySet().iterator(); }
            @Override public Object[] toArray() { return map.keySet().toArray(); }
            @Override public <U> U[] toArray(U[] a) { return map.keySet().toArray(a); }
            @Override public boolean containsAll(Collection<?> c) {
                return map.keySet().containsAll(c);
            }
            @Override public boolean addAll(Collection<? extends T> c) {
                boolean changed = false;
                for (T t : c) {
                    changed |= add(t);
                }
                return changed;
            }
            @Override public boolean retainAll(Collection<?> c) {
                return map.keySet().retainAll(c);
            }
            @Override public boolean removeAll(Collection<?> c) {
                return map.keySet().removeAll(c);
            }
            @Override public int hashCode() { return map.keySet().hashCode(); }
            @Override public boolean equals(Object o) {
                return o == this || map.keySet().equals(o);
            }
        };
    }

    public static <T> List<T> singletonList(T value) {
        return new java.util.AbstractList<T>() {
            @Override public int size() { return 1; }
            @Override public T get(int index) {
                if (index != 0) {
                    throw new IndexOutOfBoundsException("Index: " + index);
                }
                return value;
            }
        };
    }

    public static <K, V> Map<K, V> singletonMap(final K key, final V value) {
        return new java.util.AbstractMap<K, V>() {
            @Override public int size() { return 1; }
            @Override public boolean containsKey(Object k) { return key == null ? k == null : key.equals(k); }
            @Override public V get(Object k) { return containsKey(k) ? value : null; }
            @Override public Set<Entry<K, V>> entrySet() {
                return new java.util.AbstractSet<Entry<K, V>>() {
                    @Override public int size() { return 1; }
                    @Override public Iterator<Entry<K, V>> iterator() {
                        Entry<K, V> entry = new java.util.AbstractMap.SimpleEntry<>(key, value);
                        return java.util.Collections.singletonList(entry).iterator();
                    }
                };
            }
        };
    }

    public static <T> Set<T> singleton(final T value) {
        // AOSP libcore SingletonSet 语义：null 元素安全（contains 用 eq() 而非
        // value.equals(o)）——androidx FragmentStore.clearActiveFragments 等链上
        // 会构造 Collections.singleton(null) 再 removeAll，value 为 null 时
        // 直接 .equals 会 NPE。
        return new java.util.AbstractSet<T>() {
            @Override public int size() { return 1; }
            @Override public boolean contains(Object o) { return value == null ? o == null : value.equals(o); }
            @Override public Iterator<T> iterator() {
                return java.util.Collections.singletonList(value).iterator();
            }
        };
    }

    public static <K, V> Map<K, V> checkedMap(
            Map<K, V> map, Class<K> keyType, Class<V> valueType) {
        return map;
    }

    public static <T> List<T> synchronizedList(List<T> values) {
        return values;
    }

    public static <T> List<T> synchronizedList(List<T> values, Object mutex) {
        return values;
    }

    public static <T> SortedSet<T> synchronizedSortedSet(SortedSet<T> values) {
        return values;
    }

    public static <K, V> SortedMap<K, V> synchronizedSortedMap(SortedMap<K, V> values) {
        return values;
    }

    public static <T> NavigableSet<T> synchronizedNavigableSet(NavigableSet<T> values) {
        return values;
    }

    public static <K, V> NavigableMap<K, V> synchronizedNavigableMap(NavigableMap<K, V> values) {
        return values;
    }

    public static <T> Collection<T> synchronizedCollection(Collection<T> values) {
        return synchronizedCollection(values, new Object());
    }

    public static <T> Collection<T> synchronizedCollection(
            Collection<T> values, Object mutex) {
        return values;
    }

    public static <T> Set<T> synchronizedSet(Set<T> values) {
        return values;
    }

    public static <T> Set<T> synchronizedSet(Set<T> values, Object mutex) {
        return values;
    }

    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> values) {
        return values;
    }

    public static <K, V> Map<K, V> synchronizedMap(Map<K, V> values, Object mutex) {
        return values;
    }
}
