package android.util;

import java.util.HashMap;
import java.util.Map;

/**
 * 轻量键值映射（AOSP android.util.ArrayMap 公开面；VM 以 HashMap 承载存储，
 * ArrayMap 特有的按索引访问面在 entry 迭代上实现——迭代序不保证插入序）。
 */
public class ArrayMap<K, V> extends HashMap<K, V> {
    public ArrayMap() {
        super();
    }

    public ArrayMap(int capacity) {
        super(Math.max(capacity, 0));
    }

    public ArrayMap(ArrayMap<K, V> map) {
        super(map);
    }

    public K keyAt(int index) {
        int i = 0;
        for (Map.Entry<K, V> e : entrySet()) {
            if (i++ == index) return e.getKey();
        }
        return null;
    }

    public V valueAt(int index) {
        int i = 0;
        for (Map.Entry<K, V> e : entrySet()) {
            if (i++ == index) return e.getValue();
        }
        return null;
    }

    public void setValueAt(int index, V value) {
        K key = keyAt(index);
        if (key != null) put(key, value);
    }

    public int indexOfKey(Object key) {
        int i = 0;
        for (K k : keySet()) {
            if (k == key || (k != null && k.equals(key))) return i;
            i++;
        }
        return -1;
    }

    public int indexOfValue(V value) {
        int i = 0;
        for (V v : values()) {
            if (v == value || (v != null && v.equals(value))) return i;
            i++;
        }
        return -1;
    }

    public void removeAt(int index) {
        K key = keyAt(index);
        if (key != null) remove(key);
    }
}
