package java.util;

public class LinkedHashMap<K, V> extends HashMap<K, V> {
    public LinkedHashMap() {
        super();
    }

    public LinkedHashMap(int capacity) {
        super(capacity);
    }

    public LinkedHashMap(int capacity, float loadFactor) {
        super(capacity, loadFactor);
    }

    public LinkedHashMap(int capacity, float loadFactor, boolean accessOrder) {
        super(capacity, loadFactor);
    }

    public LinkedHashMap(Map<? extends K, ? extends V> source) {
        super(source);
    }

    // ---- overlay：removeEldestEntry 钩子（OpenJDK 保护面）----
    protected boolean removeEldestEntry(Map.Entry<K, V> eldest) {
        return false;
    }

}
