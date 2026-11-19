package java.util;

import java.util.Objects;
public interface Map<K, V> {
    int size();

    boolean isEmpty();

    boolean containsKey(Object key);

    boolean containsValue(Object value);

    V get(Object key);

    V put(K key, V value);

    V remove(Object key);

    void putAll(Map<? extends K, ? extends V> source);

    void clear();

    Set<K> keySet();

    Collection<V> values();

    Set<Entry<K, V>> entrySet();

    interface Entry<K, V> {
        static <K extends Comparable<? super K>, V> java.util.Comparator<Entry<K, V>> comparingByKey() {
            return (java.util.Comparator<Entry<K, V>> & java.io.Serializable)
                (c1, c2) -> c1.getKey().compareTo(c2.getKey());
        }

        static <K, V extends Comparable<? super V>> java.util.Comparator<Entry<K, V>> comparingByValue() {
            return (java.util.Comparator<Entry<K, V>> & java.io.Serializable)
                (c1, c2) -> c1.getValue().compareTo(c2.getValue());
        }

        static <K, V> java.util.Comparator<Entry<K, V>> comparingByKey(
                java.util.Comparator<? super K> cmp) {
            java.util.Objects.requireNonNull(cmp);
            return (java.util.Comparator<Entry<K, V>> & java.io.Serializable)
                (c1, c2) -> cmp.compare(c1.getKey(), c2.getKey());
        }

        static <K, V> java.util.Comparator<Entry<K, V>> comparingByValue(
                java.util.Comparator<? super V> cmp) {
            java.util.Objects.requireNonNull(cmp);
            return (java.util.Comparator<Entry<K, V>> & java.io.Serializable)
                (c1, c2) -> cmp.compare(c1.getValue(), c2.getValue());
        }

        K getKey();

        V getValue();

        V setValue(V value);

        boolean equals(Object other);

        int hashCode();
    }

    // ---- overlay：java.util.Map.of / ofEntries / entry + Java 8 默认方法
    //（OpenJDK 11 语义；不可变实现委托 ImmutableCollections）----
    static <K, V> Map<K, V> of() {
        @SuppressWarnings("unchecked")
        Map<K, V> empty = (Map<K, V>) ImmutableCollections.emptyMap();
        return empty;
    }

    static <K, V> Map<K, V> of(K k1, V v1) {
        return new ImmutableCollections.Map1<>(k1, v1);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4,
                              K k5, V v5) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4,
                              K k5, V v5, K k6, V v6) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4,
                              K k5, V v5, K k6, V v6, K k7, V v7) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6, k7, v7);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4,
                              K k5, V v5, K k6, V v6, K k7, V v7, K k8, V v8) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5, k6, v6, k7, v7, k8, v8);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4,
                              K k5, V v5, K k6, V v6, K k7, V v7, K k8, V v8,
                              K k9, V v9) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5,
                k6, v6, k7, v7, k8, v8, k9, v9);
    }

    static <K, V> Map<K, V> of(K k1, V v1, K k2, V v2, K k3, V v3, K k4, V v4,
                              K k5, V v5, K k6, V v6, K k7, V v7, K k8, V v8,
                              K k9, V v9, K k10, V v10) {
        return new ImmutableCollections.MapN<>(k1, v1, k2, v2, k3, v3, k4, v4, k5, v5,
                k6, v6, k7, v7, k8, v8, k9, v9, k10, v10);
    }

    static <K, V> Map<K, V> ofEntries(Entry<? extends K, ? extends V>... entries) {
        Object[] array = new Object[entries.length * 2];
        int n = 0;
        for (Entry<? extends K, ? extends V> entry : entries) {
            array[n++] = entry.getKey();
            array[n++] = entry.getValue();
        }
        return new ImmutableCollections.MapN<>(array);
    }

    static <K, V> Entry<K, V> entry(K key, V value) {
        return new KeyValueHolder<>(key, value);
    }

    default V getOrDefault(Object key, V defaultValue) {
        V value;
        return (((value = get(key)) != null) || containsKey(key))
                ? value
                : defaultValue;
    }

    default void forEach(java.util.function.BiConsumer<? super K, ? super V> action) {
        Objects.requireNonNull(action);
        for (Entry<K, V> entry : entrySet()) {
            K key;
            V value;
            try {
                key = entry.getKey();
                value = entry.getValue();
            } catch (IllegalStateException ex) {
                continue;
            }
            action.accept(key, value);
        }
    }

    default void replaceAll(java.util.function.BiFunction<? super K, ? super V, ? extends V> fn) {
        Objects.requireNonNull(fn);
        for (Entry<K, V> entry : entrySet()) {
            entry.setValue(fn.apply(entry.getKey(), entry.getValue()));
        }
    }

    default V putIfAbsent(K key, V value) {
        V existing = get(key);
        if (existing == null) {
            existing = put(key, value);
        }
        return existing;
    }

    default boolean remove(Object key, Object value) {
        Object cur = get(key);
        if (!Objects.equals(cur, value)
                || (cur == null && !containsKey(key))) {
            return false;
        }
        remove(key);
        return true;
    }

    default boolean replace(K key, V oldValue, V newValue) {
        Object cur = get(key);
        if (!Objects.equals(cur, oldValue)
                || (cur == null && !containsKey(key))) {
            return false;
        }
        put(key, newValue);
        return true;
    }

    default V replace(K key, V value) {
        V cur;
        if (((cur = get(key)) != null) || containsKey(key)) {
            cur = put(key, value);
        }
        return cur;
    }

    default V computeIfAbsent(K key,
            java.util.function.Function<? super K, ? extends V> mappingFn) {
        Objects.requireNonNull(mappingFn);
        V value;
        if ((value = get(key)) == null) {
            V newValue;
            if ((newValue = mappingFn.apply(key)) != null) {
                put(key, newValue);
                return newValue;
            }
        }
        return value;
    }

    default V computeIfPresent(K key,
            java.util.function.BiFunction<? super K, ? super V, ? extends V> remapFn) {
        Objects.requireNonNull(remapFn);
        V oldValue;
        if ((oldValue = get(key)) != null) {
            V newValue = remapFn.apply(key, oldValue);
            if (newValue != null) {
                put(key, newValue);
                return newValue;
            } else {
                remove(key);
                return null;
            }
        }
        return oldValue;
    }

    default V compute(K key,
            java.util.function.BiFunction<? super K, ? super V, ? extends V> remapFn) {
        Objects.requireNonNull(remapFn);
        V oldValue = get(key);
        V newValue = remapFn.apply(key, oldValue);
        if (oldValue != null) {
            if (newValue != null) {
                put(key, newValue);
                return newValue;
            } else {
                remove(key);
                return null;
            }
        } else {
            if (newValue != null) {
                put(key, newValue);
                return newValue;
            } else {
                return null;
            }
        }
    }

    default V merge(K key, V value,
            java.util.function.BiFunction<? super V, ? super V, ? extends V> remapFn) {
        Objects.requireNonNull(remapFn);
        Objects.requireNonNull(value);
        V oldValue = get(key);
        V newValue = (oldValue == null) ? value
                : remapFn.apply(oldValue, value);
        if (newValue == null) {
            remove(key);
        } else {
            put(key, newValue);
        }
        return newValue;
    }
}
