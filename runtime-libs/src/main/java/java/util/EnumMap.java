package java.util;

import java.io.Serializable;

public class EnumMap<K extends Enum<K>, V>
        implements Map<K, V>, Serializable {
    private final Class<K> keyType;
    private Object[] values = new Object[0];
    private int size;

    public EnumMap(Class<K> keyType) {
        if (keyType == null) throw new NullPointerException("keyType");
        this.keyType = keyType;
    }

    public EnumMap(EnumMap<K, ? extends V> source) {
        this(source.keyType);
        putAll(source);
    }

    public EnumMap(Map<K, ? extends V> source) {
        if (source instanceof EnumMap) {
            EnumMap<K, ? extends V> enumMap =
                    (EnumMap<K, ? extends V>) source;
            keyType = enumMap.keyType;
        } else {
            if (source.isEmpty()) {
                throw new IllegalArgumentException("Specified map is empty");
            }
            @SuppressWarnings("unchecked")
            Class<K> inferred =
                    (Class<K>) source.keySet().iterator().next().getClass();
            keyType = inferred;
        }
        putAll(source);
    }

    private void ensureCapacity(int capacity) {
        if (capacity <= values.length) return;
        Object[] expanded = new Object[capacity];
        System.arraycopy(values, 0, expanded, 0, values.length);
        values = expanded;
    }

    @Override
    public int size() {
        return size;
    }

    @Override
    public boolean isEmpty() {
        return size == 0;
    }

    private K checked(Object key) {
        if (!keyType.isInstance(key)) {
            throw new ClassCastException("Invalid key type");
        }
        @SuppressWarnings("unchecked")
        K typed = (K) key;
        return typed;
    }

    @Override
    public boolean containsKey(Object key) {
        if (!keyType.isInstance(key)) return false;
        int ordinal = checked(key).ordinal();
        return ordinal < values.length && values[ordinal] != null;
    }

    @Override
    public boolean containsValue(Object value) {
        for (int index = 0; index < values.length; index++) {
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
    public V get(Object key) {
        if (!keyType.isInstance(key)) return null;
        int ordinal = checked(key).ordinal();
        return ordinal < values.length ? (V) values[ordinal] : null;
    }

    @Override
    public V put(K key, V value) {
        if (key == null || value == null) {
            throw new NullPointerException();
        }
        int ordinal = checked(key).ordinal();
        ensureCapacity(ordinal + 1);
        @SuppressWarnings("unchecked")
        V previous = (V) values[ordinal];
        if (previous == null) size++;
        values[ordinal] = value;
        return previous;
    }

    @Override
    @SuppressWarnings("unchecked")
    public V remove(Object key) {
        if (!keyType.isInstance(key)) return null;
        int ordinal = checked(key).ordinal();
        if (ordinal >= values.length) return null;
        V previous = (V) values[ordinal];
        if (previous != null) {
            values[ordinal] = null;
            size--;
        }
        return previous;
    }

    @Override
    public void putAll(Map<? extends K, ? extends V> source) {
        for (Entry<? extends K, ? extends V> entry : source.entrySet()) {
            put(entry.getKey(), entry.getValue());
        }
    }

    @Override
    public void clear() {
        values = new Object[0];
        size = 0;
    }

    @Override
    public Set<K> keySet() {
        HashSet<K> result = new HashSet<K>(size);
        for (int index = 0; index < values.length; index++) {
            if (values[index] != null) {
                result.add(keyForOrdinal(index));
            }
        }
        return result;
    }

    @Override
    public Collection<V> values() {
        HashSet<V> result = new HashSet<V>(size);
        for (Object value : values) {
            if (value != null) {
                @SuppressWarnings("unchecked")
                V typed = (V) value;
                result.add(typed);
            }
        }
        return result;
    }

    @Override
    public Set<Entry<K, V>> entrySet() {
        HashSet<Entry<K, V>> result = new HashSet<Entry<K, V>>(size);
        for (int index = 0; index < values.length; index++) {
            if (values[index] != null) {
                @SuppressWarnings("unchecked")
                V value = (V) values[index];
                result.add(new EntryImpl<K, V>(
                        keyForOrdinal(index), value));
            }
        }
        return result;
    }

    private K keyForOrdinal(int ordinal) {
        K[] constants = keyType.getEnumConstants();
        if (constants == null || ordinal < 0 || ordinal >= constants.length) {
            throw new IllegalArgumentException("Invalid enum ordinal");
        }
        return constants[ordinal];
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
            return key.equals(entry.getKey()) &&
                    (value == null
                            ? entry.getValue() == null
                            : value.equals(entry.getValue()));
        }

        @Override
        public int hashCode() {
            return key.hashCode() ^
                    (value == null ? 0 : value.hashCode());
        }
    }
}
