package jdk.internal.ref;

import java.lang.ref.Cleaner;

/**
 * 生成补源（sun-compat）：jdk.internal.ref.PhantomCleanable（JBR 25 形状漂移；
 * vendored java.io.FileCleanable / java.net.SocketCleanable / FileDescriptor
 * 消费面）。
 *
 * <p>VM 语义（referent 强语义，core/builtins ref.Reference 面：单线程 VM 无
 * Reference Handler，PhantomReference 永不入队）→ cleaner 线程无物可清；
 * 注册簿记 + 显式 {@link #clean()} / {@link #clear()}（FileDescriptor 关闭路径）
 * 承担回收。GC 兜底清理随 referent 弱化语义后置。
 */
public abstract class PhantomCleanable<T> implements Cleaner.Cleanable {

    protected final T obj;
    private boolean registered = true;

    protected PhantomCleanable(T obj, Cleaner cleaner) {
        this.obj = obj;
    }

    /** 显式清理：摘除注册并执行回收体。 */
    @Override
    public void clean() {
        if (registered) {
            registered = false;
            performCleanup();
        }
    }

    /** 摘除注册（不执行回收体；FileDescriptor.set/close 路径）。 */
    public void clear() {
        registered = false;
    }

    protected abstract void performCleanup();
}
