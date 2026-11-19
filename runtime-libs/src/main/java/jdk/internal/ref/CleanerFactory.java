package jdk.internal.ref;

import java.lang.ref.Cleaner;

/**
 * 生成补源（sun-compat）：jdk.internal.ref.CleanerFactory（JBR 25 形状漂移）。
 *
 * <p>公共 cleaner 面（Cleaner.create()；清理线程语义见 PhantomCleanable 注）。
 */
public class CleanerFactory {

    private static class CleanerHolder {
        static final Cleaner cleaner = Cleaner.create();
    }

    public static Cleaner cleaner() {
        return CleanerHolder.cleaner;
    }
}
