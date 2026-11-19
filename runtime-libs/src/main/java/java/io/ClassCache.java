package java.io;

import java.lang.ref.Reference;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.SoftReference;
import java.util.Objects;

abstract class ClassCache<T> {

    private static class CacheRef<T> extends SoftReference<T> {
        private final Class<?> type;
        private T strongReferent;

        CacheRef(T referent, ReferenceQueue<T> queue, Class<?> type) {
            super(referent, queue);
            this.type = type;
            this.strongReferent = referent;
        }

        Class<?> getType() {
            return type;
        }

        T getStrong() {
            return strongReferent;
        }

        void clearStrong() {
            strongReferent = null;
        }
    }

    private final ReferenceQueue<T> queue;
    private final ClassValue<CacheRef<T>> map;

    protected abstract T computeValue(Class<?> cl);

    protected ClassCache() {
        queue = new ReferenceQueue<>();
        map = new ClassValue<>() {
            @Override
            protected CacheRef<T> computeValue(Class<?> type) {
                T v = ClassCache.this.computeValue(type);
                Objects.requireNonNull(v);
                return new CacheRef<>(v, queue, type);
            }
        };
    }

    T get(Class<?> cl) {
        while (true) {
            processQueue();

            CacheRef<T> ref = map.get(cl);

            T strongVal = ref.getStrong();
            if (strongVal != null) {
                ref.clearStrong();
                return strongVal;
            }

            T val = ref.get();
            if (val != null) {
                return val;
            }

            map.remove(cl);
        }
    }

    private void processQueue() {
        Reference<? extends T> ref;
        while((ref = queue.poll()) != null) {
            CacheRef<? extends T> cacheRef = (CacheRef<? extends T>)ref;
            map.remove(cacheRef.getType());
        }
    }
}
