package apitest;

import java.lang.ref.Reference;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;

/**
 * WeakReference/ReferenceQueue/GC 面宿主编排辅助（覆盖面参考启发自 v1
 * DexFixture.java makeWeakReference/weakGet/weakEnqueued/referenceQueue/
 * requestGc/guestFreeMemory 与 GuestHeapGcTest.kt 断言面）。
 * 断言判据 = v2 规格（docs 02 章 6.10 当前口径：referent 视为强引用，GC 不清扫；
 * ReferenceProcessor/入队语义目标态后置）——GC 后清除/入队行为由宿主侧仅作
 * 观测记录（NOTE），不作断言，避免阻塞未来向 Android 全语义翻转。
 * 方法为宿主编排钩子（非 check* 形态）：返回对象句柄/布尔，断言在宿主侧。
 */
public final class WeakRefChecks {
    private WeakRefChecks() {}

    public static Object newObject() {
        return new Object();
    }

    public static Object makeWeak(Object target) {
        return new WeakReference<Object>(target);
    }

    public static Object weakGet(Object reference) {
        return ((WeakReference<?>) reference).get();
    }

    public static void clearRef(Object reference) {
        ((Reference<?>) reference).clear();
    }

    public static Object makeQueue() {
        return new ReferenceQueue<Object>();
    }

    public static Object makeWeakWithQueue(Object target, Object queue) {
        return new WeakReference<Object>(
                target, (ReferenceQueue<Object>) queue);
    }

    public static Object queuePoll(Object queue) {
        return ((ReferenceQueue<?>) queue).poll();
    }

    public static void requestGc() {
        System.gc();
    }

    public static long freeMemory() {
        return Runtime.getRuntime().freeMemory();
    }

    public static long totalMemory() {
        return Runtime.getRuntime().totalMemory();
    }
}
