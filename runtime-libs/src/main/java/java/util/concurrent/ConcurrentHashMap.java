package java.util.concurrent;

import java.io.Serializable;
import java.util.Collection;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;
import java.util.function.BiFunction;
import java.util.function.Function;

public class ConcurrentHashMap<K, V>
        implements ConcurrentMap<K, V>, Serializable {
    private Object[] keys;
    private Object[] values;
    private int size;

    public ConcurrentHashMap() {
    }

    public ConcurrentHashMap(int initialCapacity) {
        if (initialCapacity < 0) {
            throw new IllegalArgumentException();
        }
        allocate(initialCapacity == 0 ? 1 : initialCapacity);
    }

    public ConcurrentHashMap(int initialCapacity, float loadFactor) {
        this(initialCapacity);
        if (loadFactor <= 0 || Float.isNaN(loadFactor)) {
            throw new IllegalArgumentException();
        }
    }

    public ConcurrentHashMap(Map<? extends K, ? extends V> source) {
        putAll(source);
    }

    private void ensureStorage() {
        if (keys == null) allocate(8);
    }

    private void allocate(int capacity) {
        int length = 1;
        while (length < capacity) length <<= 1;
        keys = new Object[length];
        values = new Object[length];
    }

    private int indexOf(Object key) {
        ensureStorage();
        for (int index = 0; index < size; index++) {
            Object candidate = keys[index];
            if (candidate == key ||
                    (candidate != null && candidate.equals(key))) {
                return index;
            }
        }
        return -1;
    }

    private void grow() {
        ensureStorage();
        if (size != keys.length) return;
        Object[] expandedKeys = new Object[keys.length * 2];
        Object[] expandedValues = new Object[values.length * 2];
        System.arraycopy(keys, 0, expandedKeys, 0, size);
        System.arraycopy(values, 0, expandedValues, 0, size);
        keys = expandedKeys;
        values = expandedValues;
    }

    private void removeAt(int index) {
        int moved = size - index - 1;
        if (moved > 0) {
            System.arraycopy(keys, index + 1, keys, index, moved);
            System.arraycopy(values, index + 1, values, index, moved);
        }
        keys[--size] = null;
        values[size] = null;
    }

    @SuppressWarnings("unchecked")
    private V valueAt(int index) {
        return (V) values[index];
    }

    @Override
    public synchronized int size() {
        return size;
    }

    @Override
    public synchronized boolean isEmpty() {
        return size == 0;
    }

    @Override
    public synchronized boolean containsKey(Object key) {
        return indexOf(key) >= 0;
    }

    @Override
    public synchronized boolean containsValue(Object value) {
        for (int index = 0; index < size; index++) {
            Object candidate = values[index];
            if (candidate == value ||
                    (candidate != null && candidate.equals(value))) {
                return true;
            }
        }
        return false;
    }

    @Override
    @SuppressWarnings("unchecked")
    public synchronized V get(Object key) {
        int index = indexOf(key);
        return index < 0 ? null : (V) values[index];
    }

    public synchronized V getOrDefault(Object key, V defaultValue) {
        V value = get(key);
        return value == null ? defaultValue : value;
    }

    @Override
    @SuppressWarnings("unchecked")
    public synchronized V put(K key, V value) {
        if (key == null || value == null) {
            throw new NullPointerException();
        }
        int index = indexOf(key);
        if (index >= 0) {
            V previous = (V) values[index];
            values[index] = value;
            return previous;
        }
        grow();
        keys[size] = key;
        values[size] = value;
        size++;
        return null;
    }

    @Override
    public synchronized V putIfAbsent(K key, V value) {
        V previous = get(key);
        if (previous == null) put(key, value);
        return previous;
    }

    @Override
    @SuppressWarnings("unchecked")
    public synchronized V remove(Object key) {
        int index = indexOf(key);
        if (index < 0) return null;
        V previous = (V) values[index];
        removeAt(index);
        return previous;
    }

    @Override
    public synchronized boolean remove(Object key, Object value) {
        int index = indexOf(key);
        if (index < 0 || !equal(values[index], value)) return false;
        removeAt(index);
        return true;
    }

    @Override
    public synchronized boolean replace(
            K key, V oldValue, V newValue) {
        int index = indexOf(key);
        if (index < 0 ||
                !equal(values[index], oldValue) ||
                newValue == null) {
            return false;
        }
        values[index] = newValue;
        return true;
    }

    @Override
    @SuppressWarnings("unchecked")
    public synchronized V replace(K key, V value) {
        if (value == null) throw new NullPointerException();
        int index = indexOf(key);
        if (index < 0) return null;
        V previous = (V) values[index];
        values[index] = value;
        return previous;
    }

    public synchronized V computeIfAbsent(
            K key,
            Function<? super K, ? extends V> mappingFunction) {
        if (key == null || mappingFunction == null) {
            throw new NullPointerException();
        }
        V previous = get(key);
        if (previous != null) return previous;
        V computed = mappingFunction.apply(key);
        if (computed != null) put(key, computed);
        return computed;
    }

    public synchronized V computeIfPresent(
            K key,
            BiFunction<? super K, ? super V, ? extends V> remappingFunction) {
        if (key == null || remappingFunction == null) {
            throw new NullPointerException();
        }
        V previous = get(key);
        if (previous == null) return null;
        V computed = remappingFunction.apply(key, previous);
        if (computed == null) {
            remove(key);
        } else {
            put(key, computed);
        }
        return computed;
    }

    public synchronized V compute(
            K key,
            BiFunction<? super K, ? super V, ? extends V> remappingFunction) {
        if (key == null || remappingFunction == null) {
            throw new NullPointerException();
        }
        V computed = remappingFunction.apply(key, get(key));
        if (computed == null) {
            remove(key);
        } else {
            put(key, computed);
        }
        return computed;
    }

    public synchronized V merge(
            K key,
            V value,
            BiFunction<? super V, ? super V, ? extends V> remappingFunction) {
        if (key == null || value == null || remappingFunction == null) {
            throw new NullPointerException();
        }
        V previous = get(key);
        V computed = previous == null
                ? value
                : remappingFunction.apply(previous, value);
        if (computed == null) {
            remove(key);
        } else {
            put(key, computed);
        }
        return computed;
    }

    @Override
    public synchronized void putAll(Map<? extends K, ? extends V> source) {
        for (Entry<? extends K, ? extends V> entry : source.entrySet()) {
            put(entry.getKey(), entry.getValue());
        }
    }

    @Override
    public synchronized void clear() {
        ensureStorage();
        for (int index = 0; index < size; index++) {
            keys[index] = null;
            values[index] = null;
        }
        size = 0;
    }

    @Override
    public synchronized Set<K> keySet() {
        HashSet<K> result = new HashSet<K>(size);
        for (int index = 0; index < size; index++) {
            @SuppressWarnings("unchecked")
            K key = (K) keys[index];
            result.add(key);
        }
        return result;
    }

    @Override
    public synchronized Collection<V> values() {
        HashSet<V> result = new HashSet<V>(size);
        for (int index = 0; index < size; index++) {
            result.add(valueAt(index));
        }
        return result;
    }

    @Override
    public synchronized Set<Entry<K, V>> entrySet() {
        HashSet<Entry<K, V>> result = new HashSet<Entry<K, V>>(size);
        for (int index = 0; index < size; index++) {
            @SuppressWarnings("unchecked")
            K key = (K) keys[index];
            result.add(new EntryImpl<K, V>(key, valueAt(index)));
        }
        return result;
    }

    public synchronized long mappingCount() {
        return size;
    }

    @Override
    public synchronized String toString() {
        StringBuilder result = new StringBuilder("{");
        boolean first = true;
        for (Entry<K, V> entry : entrySet()) {
            if (!first) result.append(", ");
            first = false;
            result.append(entry.getKey()).append('=')
                    .append(entry.getValue());
        }
        return result.append('}').toString();
    }

    private static boolean equal(Object left, Object right) {
        return left == right ||
                (left != null && left.equals(right));
    }

    private static final class EntryImpl<K, V>
            implements Entry<K, V> {
        private final K key;
        private V value;

        EntryImpl(K key, V value) {
            this.key = key;
            this.value = value;
        }

        @Override
        public K getKey() {
            return key;
        }

        @Override
        public V getValue() {
            return value;
        }

        @Override
        public V setValue(V value) {
            V previous = this.value;
            this.value = value;
            return previous;
        }

        @Override
        public boolean equals(Object other) {
            if (!(other instanceof Entry)) return false;
            Entry<?, ?> entry = (Entry<?, ?>) other;
            return equal(key, entry.getKey()) &&
                    equal(value, entry.getValue());
        }

        @Override
        public int hashCode() {
            return (key == null ? 0 : key.hashCode()) ^
                    (value == null ? 0 : value.hashCode());
        }
    }

    // ---- overlay：3 参 ctor/contains/keys/elements/forEach/replaceAll API 补全 ----
    public ConcurrentHashMap(int initialCapacity, float loadFactor,
                             int concurrencyLevel) {
        this(initialCapacity, loadFactor);
    }

    public boolean contains(Object value) {
        return containsValue(value);
    }

    public synchronized java.util.Enumeration<K> keys() {
        return java.util.Collections.enumeration(keySet());
    }

    public synchronized java.util.Enumeration<V> elements() {
        return java.util.Collections.enumeration(values());
    }

    @Override
    public synchronized void forEach(java.util.function.BiConsumer<? super K, ? super V> action) {
        java.util.Objects.requireNonNull(action);
        for (Entry<K, V> entry : entrySet()) {
            action.accept(entry.getKey(), entry.getValue());
        }
    }

    @Override
    public synchronized void replaceAll(
            java.util.function.BiFunction<? super K, ? super V, ? extends V> fn) {
        java.util.Objects.requireNonNull(fn);
        for (Entry<K, V> entry : entrySet()) {
            entry.setValue(fn.apply(entry.getKey(), entry.getValue()));
        }
    }
}
