package sun.util.locale;

// 生成补源（sun-compat）：sun.util.locale.LocaleObjectCache（ResourceBundle
// 候选链缓存基类）。OpenJDK 11 语义子集（弱键缓存）。
public abstract class LocaleObjectCache<K, V> {
    private final java.util.Map<K, java.lang.ref.SoftReference<V>> cache =
        new java.util.concurrent.ConcurrentHashMap<>();

    protected abstract V createObject(K key);

    public V get(K key) {
        java.lang.ref.SoftReference<V> ref = cache.get(key);
        V value = ref == null ? null : ref.get();
        if (value == null) {
            value = createObject(key);
            cache.put(key, new java.lang.ref.SoftReference<>(value));
        }
        return value;
    }

    public V put(K key, V value) {
        java.lang.ref.SoftReference<V> ref = cache.put(
            key, new java.lang.ref.SoftReference<>(value));
        return ref == null ? null : ref.get();
    }

    public void remove(K key) {
        cache.remove(key);
    }
}
