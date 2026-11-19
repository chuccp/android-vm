package jdk.internal.loader;

import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.BiFunction;

/**
 * 生成补源（jdk-compat）：jdk.internal.loader.ClassLoaderValue（按 ClassLoader
 * 维度的值缓存面——java.lang.reflect.Proxy 的 proxyCache/reverseProxyCache、
 * java.lang.ModuleLayer 的 CLV 消费）。契约（按消费面）：
 *   new ClassLoaderValue&lt;V&gt;()                       root（命名空间）
 *   root.sub(key) : ClassLoaderValue&lt;V&gt;              key 定位的子视图（稳定同一实例）
 *   sub.key() : Object                               子视图的 key
 *   sub.get(loader) / putIfAbsent(loader, v) /
 *   sub.computeIfAbsent(loader, (ld, clv) -> v)      按 ClassLoader 存取
 * null loader（bootstrap）合法键。存储随本实现持有（VM 收敛语义后置）。
 */
public final class ClassLoaderValue<T> {

    private static final Object BOOTSTRAP_KEY = new Object();

    private final Object key;  // root = null
    private final ConcurrentHashMap<Object, ClassLoaderValue<T>> subs;  // 仅 root
    private final ConcurrentHashMap<Object, T> map;  // loader 键 -> 值

    /** root 构造（命名空间）。 */
    public ClassLoaderValue() {
        this.key = null;
        this.subs = new ConcurrentHashMap<>();
        this.map = new ConcurrentHashMap<>();
    }

    private ClassLoaderValue(Object subKey) {
        this.key = Objects.requireNonNull(subKey);
        this.subs = null;
        this.map = new ConcurrentHashMap<>();
    }

    /** 子视图的 key（root 上调用返回 null）。 */
    public Object key() {
        return key;
    }

    /** key 定位的子视图（root 专有；同一 key 恒返回同一实例）。 */
    public ClassLoaderValue<T> sub(Object subKey) {
        if (subs == null) {
            throw new IllegalStateException("sub on non-root ClassLoaderValue");
        }
        return subs.computeIfAbsent(subKey, ClassLoaderValue::new);
    }

    private static Object loaderKey(ClassLoader ld) {
        return ld != null ? ld : BOOTSTRAP_KEY;
    }

    public T get(ClassLoader ld) {
        return map.get(loaderKey(ld));
    }

    public T putIfAbsent(ClassLoader ld, T value) {
        return map.putIfAbsent(loaderKey(ld), value);
    }

    public T computeIfAbsent(
            ClassLoader ld,
            BiFunction<? super ClassLoader, ? super ClassLoaderValue<T>, ? extends T> f) {
        Object k = loaderKey(ld);
        T v = map.get(k);
        if (v != null) {
            return v;
        }
        T computed = f.apply(ld, this);
        if (computed == null) {
            return null;
        }
        T prev = map.putIfAbsent(k, computed);
        return prev != null ? prev : computed;
    }

    public void remove(ClassLoader ld) {
        map.remove(loaderKey(ld));
    }
}
