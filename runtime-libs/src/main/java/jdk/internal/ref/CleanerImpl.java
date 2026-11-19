package jdk.internal.ref;

import java.lang.ref.Cleaner;
import java.util.concurrent.ThreadFactory;
import java.util.function.Function;

/**
 * 生成补源（sun-compat）：jdk.internal.ref.CleanerImpl（JBR 25 形状漂移；
 * java.lang.ref.Cleaner 消费面：setCleanerImplAccess / start /
 * PhantomCleanableRef）。
 *
 * <p>VM 语义（referent 强语义）：PhantomReference 永不入队 → 不起清理线程
 * （start 为空实现，显式 clean() 承担回收；PhantomCleanable 注同）。
 */
public class CleanerImpl {

    private static Function<Cleaner, CleanerImpl> cleanerImplAccess;

    public static void setCleanerImplAccess(Function<Cleaner, CleanerImpl> access) {
        cleanerImplAccess = access;
    }

    /** 无清理线程（VM referent 强语义：无物可清）。 */
    public void start(Cleaner cleaner, ThreadFactory threadFactory) {
    }

    public static class PhantomCleanableRef extends PhantomCleanable<Object> {

        private final Runnable action;

        public PhantomCleanableRef(Object obj, Cleaner cleaner, Runnable action) {
            super(obj, cleaner);
            this.action = action;
        }

        @Override
        protected void performCleanup() {
            if (action != null) {
                action.run();
            }
        }
    }
}
