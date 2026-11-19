package java.util;

import java.io.Serializable;

public class HashMap<K, V> implements Map<K, V>, Cloneable, Serializable {
    private Object[] keys = new Object[8];
    private Object[] values = new Object[8];
    private int size;

    public HashMap() {
    }

    public HashMap(int capacity) {
        if (capacity < 0) throw new IllegalArgumentException();
        int length = capacity == 0 ? 1 : capacity;
        keys = new Object[length];
        values = new Object[length];
    }

    public HashMap(int capacity, float loadFactor) {
        this(capacity);
        if (loadFactor <= 0 || Float.isNaN(loadFactor)) {
            throw new IllegalArgumentException();
        }
    }

    public HashMap(Map<? extends K, ? extends V> source) {
        this();
        putAll(source);
    }

    protected static boolean equal(Object left, Object right) {
        return left == right || (left != null && left.equals(right));
    }

    protected final int find(Object key) {
        for (int index = 0; index < size; index++) {
            if (equal(keys[index], key)) return index;
        }
        return -1;
    }

    private void grow() {
        if (size != keys.length) return;
        Object[] expandedKeys = new Object[keys.length * 2];
        Object[] expandedValues = new Object[values.length * 2];
        System.arraycopy(keys, 0, expandedKeys, 0, size);
        System.arraycopy(values, 0, expandedValues, 0, size);
        keys = expandedKeys;
        values = expandedValues;
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
    public boolean containsKey(Object key) {
        return find(key) >= 0;
    }

    @Override
    public boolean containsValue(Object value) {
        for (int index = 0; index < size; index++) {
            if (equal(values[index], value)) return true;
        }
        return false;
    }

    @Override
    @SuppressWarnings("unchecked")
    public V get(Object key) {
        int index = find(key);
        return index < 0 ? null : (V) values[index];
    }

    public V getOrDefault(Object key, V defaultValue) {
        int index = find(key);
        return index < 0 ? defaultValue : valueAt(index);
    }

    @Override
    @SuppressWarnings("unchecked")
    public V put(K key, V value) {
        int index = find(key);
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

    public V putIfAbsent(K key, V value) {
        int index = find(key);
        if (index >= 0) return valueAt(index);
        put(key, value);
        return null;
    }

    @Override
    @SuppressWarnings("unchecked")
    public V remove(Object key) {
        int index = find(key);
        if (index < 0) return null;
        V previous = (V) values[index];
        removeAt(index);
        return previous;
    }

    public boolean remove(Object key, Object value) {
        int index = find(key);
        if (index < 0 || !equal(values[index], value)) return false;
        removeAt(index);
        return true;
    }

    public boolean replace(K key, V oldValue, V newValue) {
        int index = find(key);
        if (index < 0 || !equal(values[index], oldValue)) return false;
        values[index] = newValue;
        return true;
    }

    public V replace(K key, V value) {
        int index = find(key);
        if (index < 0) return null;
        V previous = valueAt(index);
        values[index] = value;
        return previous;
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

    private Object keyAt(int index) {
        return keys[index];
    }

    private void setAt(int index, V value) {
        values[index] = value;
    }

    @Override
    public void putAll(Map<? extends K, ? extends V> source) {
        for (Entry<? extends K, ? extends V> entry : source.entrySet()) {
            put(entry.getKey(), entry.getValue());
        }
    }

    @Override
    public void clear() {
        for (int index = 0; index < size; index++) {
            keys[index] = null;
            values[index] = null;
        }
        size = 0;
    }

    // ---- 视图语义（JDK Map 契约）----
    // keySet/values/entrySet 必须是【活视图】：size/contains 反映当前映射，
    // iterator().remove() 删除底层条目（Jackson _renameProperties 依此把显式
    // 重命名后的属性从 map 摘除；快照集合会让 remove 落空，导致同一 builder
    // 以「原键 + 新名」双重存在并在 addAll 时把 ctor 参数链自并成双节点）。
    @Override
    public Set<K> keySet() {
        return new KeySetView();
    }

    @Override
    public Collection<V> values() {
        return new ValuesView();
    }

    @Override
    public Set<Entry<K, V>> entrySet() {
        return new EntrySetView();
    }

    private final class KeySetView extends AbstractSet<K> {
        @Override
        public Iterator<K> iterator() {
            return new Iterator<K>() {
                private int next;
                private int last = -1;

                @Override
                public boolean hasNext() {
                    return next < HashMap.this.size;
                }

                @Override
                @SuppressWarnings("unchecked")
                public K next() {
                    if (next >= HashMap.this.size) throw new NoSuchElementException();
                    last = next++;
                    return (K) keys[last];
                }

                @Override
                public void remove() {
                    if (last < 0) throw new IllegalStateException();
                    removeAt(last);
                    if (last < next) next--;
                    last = -1;
                }
            };
        }

        @Override
        public int size() {
            return HashMap.this.size;
        }

        @Override
        public boolean contains(Object key) {
            return containsKey(key);
        }

        @Override
        public boolean remove(Object key) {
            int index = find(key);
            if (index < 0) return false;
            removeAt(index);
            return true;
        }

        @Override
        public void clear() {
            HashMap.this.clear();
        }
    }

    private final class ValuesView extends AbstractCollection<V> {
        @Override
        public Iterator<V> iterator() {
            return new Iterator<V>() {
                private int next;
                private int last = -1;

                @Override
                public boolean hasNext() {
                    return next < HashMap.this.size;
                }

                @Override
                public V next() {
                    if (next >= HashMap.this.size) throw new NoSuchElementException();
                    last = next++;
                    return valueAt(last);
                }

                @Override
                public void remove() {
                    if (last < 0) throw new IllegalStateException();
                    removeAt(last);
                    if (last < next) next--;
                    last = -1;
                }
            };
        }

        @Override
        public int size() {
            return HashMap.this.size;
        }

        @Override
        public boolean contains(Object value) {
            return containsValue(value);
        }

        @Override
        public void clear() {
            HashMap.this.clear();
        }
    }

    private final class EntrySetView extends AbstractSet<Entry<K, V>> {
        @Override
        public Iterator<Entry<K, V>> iterator() {
            return new Iterator<Entry<K, V>>() {
                private int next;
                private int last = -1;

                @Override
                public boolean hasNext() {
                    return next < HashMap.this.size;
                }

                @Override
                public Entry<K, V> next() {
                    if (next >= HashMap.this.size) throw new NoSuchElementException();
                    last = next++;
                    return new EntryImpl<K, V>(HashMap.this, last);
                }

                @Override
                public void remove() {
                    if (last < 0) throw new IllegalStateException();
                    removeAt(last);
                    if (last < next) next--;
                    last = -1;
                }
            };
        }

        @Override
        public int size() {
            return HashMap.this.size;
        }

        @Override
        public boolean contains(Object other) {
            if (!(other instanceof Entry)) return false;
            Entry<?, ?> entry = (Entry<?, ?>) other;
            int index = find(entry.getKey());
            return index >= 0 && equal(valueAt(index), entry.getValue());
        }

        @Override
        public void clear() {
            HashMap.this.clear();
        }
    }

    @Override
    public String toString() {
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

    // 条目视图（entrySet 迭代器产出）：getValue/setValue 写回底层槽位。
    private static final class EntryImpl<K, V> implements Entry<K, V> {
        private final HashMap<K, V> owner;
        private final int index;

        EntryImpl(HashMap<K, V> owner, int index) {
            this.owner = owner;
            this.index = index;
        }

        @Override
        @SuppressWarnings("unchecked")
        public K getKey() {
            return (K) owner.keyAt(index);
        }

        @Override
        public V getValue() {
            return owner.valueAt(index);
        }

        @Override
        public V setValue(V value) {
            V previous = getValue();
            owner.setAt(index, value);
            return previous;
        }

        @Override
        public boolean equals(Object other) {
            if (!(other instanceof Entry)) return false;
            Entry<?, ?> entry = (Entry<?, ?>) other;
            return HashMap.equal(getKey(), entry.getKey()) &&
                HashMap.equal(getValue(), entry.getValue());
        }

        @Override
        public int hashCode() {
            K key = getKey();
            V value = getValue();
            return (key == null ? 0 : key.hashCode()) ^
                (value == null ? 0 : value.hashCode());
        }
    }

    // ---- overlay：tableSizeFor + clone（OpenJDK 内部面）----
    static final int tableSizeFor(int cap) {
        int n = cap - 1;
        n |= n >>> 1;
        n |= n >>> 2;
        n |= n >>> 4;
        n |= n >>> 8;
        n |= n >>> 16;
        return (n < 0) ? 1 : (n >= 1 << 30) ? 1 << 30 : n + 1;
    }

    @Override
    public Object clone() {
        try {
            return super.clone();
        } catch (CloneNotSupportedException e) {
            throw new AssertionError(e);
        }
    }

}
