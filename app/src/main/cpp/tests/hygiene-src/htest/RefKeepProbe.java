package htest;

import java.lang.ref.WeakReference;

/**
 * Reference 继承槽保活口径（docs：referent 视为强引用（GC 不清扫））。
 *
 * 形态：WeakReference 子类实例经【继承自 Reference 的 referent/queue 槽】
 * 持目标；GC 穿插后 get() 必须仍读到活对象（强引用口径），槽残骸读出
 * = 死对象形态（RefGet intrinsic 返回卫生或后续 cast 消费面检出）。
 */
public final class RefKeepProbe {
    public static class Marker {
        public String tag = "ref-keep";
    }

    /** 只经 referent 槽持 Marker（出方法后无局部引用）。 */
    public static Object make() {
        Marker m = new Marker();
        return new WeakReference<Object>(m);
    }

    public static String consume(Object o) {
        try {
            WeakReference<?> wr = (WeakReference<?>) o;
            Object r = wr.get();             // RefGet：referent 槽（继承自 Reference）
            if (r == null) return "refkeep: FAIL referent=null";
            Marker m = (Marker) r;
            if (!"ref-keep".equals(m.tag)) return "refkeep: FAIL tag=" + m.tag;
            return "refkeep: OK";
        } catch (Throwable t) {
            return "refkeep: FAIL " + t;
        }
    }
}
