package jdk.internal.misc;

// jdk.internal.misc.TerminatingThreadLocal：§31.4 排除清单（VM 内建独占），
// 源 override 供 Thread/ThreadLocal 编译。
public class TerminatingThreadLocal<T> extends ThreadLocal<T> {
    public static final class Registry {
        public boolean isPresent() {
            return false;
        }
    }

    public static final Registry REGISTRY = new Registry();

    public static void threadTerminated() {
    }

    public static void register(TerminatingThreadLocal<?> ttl) {
    }

    protected void threadTerminated(T value) {
    }

    protected void onRemoval(T value) {
    }
}
