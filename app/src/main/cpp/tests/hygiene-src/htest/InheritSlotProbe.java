package htest;

/**
 * 继承链引用槽死对象形态（dirty reference 卫生墙复现/回归）。
 *
 * 形态：容器对象经【继承字段槽】持引用 → GC 穿插 → 容器存活、目标被清扫
 * → iget-object 读槽得悬挂指针（reclaimed-block 位型）。
 *
 * 覆盖：2 级（Base->Sub）+ 3 级（A->B->C）继承链；自家槽对照（extra 在
 * 精确类自家字段，任何 GC 口径都必须保活——对照面）。
 */
public final class InheritSlotProbe {
    public static class Base {
        public Object payload;
        public String tag = "keep";
    }

    public static class Sub extends Base {
        public Object extra;
    }

    // 3 级链：字段在最顶层（C 实例上 iget A.payload = 继承两级）
    public static class A {
        public Object deepPayload;
    }

    public static class B extends A {
        public Object mid;
    }

    public static class C extends B {
        public Object own;
    }

    /** 构造容器：payload/deepPayload 只经继承槽可达（出方法后无局部引用）。 */
    public static Object make() {
        Sub s = new Sub();
        s.payload = new java.util.concurrent.LinkedBlockingQueue<Runnable>();
        s.extra = new Object();
        return s;
    }

    public static Object makeDeep() {
        C c = new C();
        c.deepPayload = new java.util.concurrent.LinkedBlockingQueue<Runnable>();
        c.mid = new Object();
        c.own = new Object();
        return c;
    }

    /** iget 消费：读继承槽（缺口面）+ 自家槽（对照面）。 */
    public static String consume(Object o) {
        try {
            Sub s = (Sub) o;
            Object p = s.payload;      // 继承槽 iget-object（死对象槽检出点）
            Object e = s.extra;        // 自家槽对照
            String t = s.tag;          // 继承槽（String 引用）
            if (p == null) return "inherit2: FAIL payload=null (stale slot)";
            if (e == null) return "inherit2: FAIL extra=null (own slot lost)";
            if (t == null || !"keep".equals(t)) return "inherit2: FAIL tag=" + t;
            return "inherit2: OK";
        } catch (Throwable t) {
            return "inherit2: FAIL " + t;
        }
    }

    public static String consumeDeep(Object o) {
        try {
            C c = (C) o;
            Object p = c.deepPayload;  // 跨 2 级继承槽
            Object m = c.mid;          // 跨 1 级继承槽
            Object w = c.own;          // 自家槽对照
            if (p == null) return "inherit3: FAIL deepPayload=null (stale slot)";
            if (m == null) return "inherit3: FAIL mid=null (stale slot)";
            if (w == null) return "inherit3: FAIL own=null (own slot lost)";
            return "inherit3: OK";
        } catch (Throwable t) {
            return "inherit3: FAIL " + t;
        }
    }
}
