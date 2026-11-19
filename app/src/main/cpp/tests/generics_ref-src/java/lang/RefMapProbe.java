package java.lang;

import java.lang.ref.SoftReference;

/**
 * java.lang 包内探针：直接驱动 ThreadLocal.ThreadLocalMap 继承复制路径
 * （ThreadLocal.createInheritedMap -> ThreadLocalMap(parentMap) 的
 * `(ThreadLocal) e.get()` 转型）。
 *
 * 背景缺口：ClassCastException: SoftReference -> ThreadLocal（zonfig 旁路分支）。
 * Entry extends WeakReference 链上，Entry.value 与 Reference.referent 布局混型
 * 时复制构造把「值」当「键」读，SoftReference 值被 cast 成 ThreadLocal。
 */
public class RefMapProbe {

    public static String run() {
        // 1) 建一个带 SoftReference 值的 map（Entry 键=InheritableThreadLocal——
        //    childValue 语义仅 InheritableThreadLocal 覆写；值=SoftReference）
        ThreadLocal<SoftReference<Object>> tl = new InheritableThreadLocal<SoftReference<Object>>();
        SoftReference<Object> val = new SoftReference<Object>(new Object());
        ThreadLocal.ThreadLocalMap parent = new ThreadLocal.ThreadLocalMap(tl, val);

        // 2) 继承复制：内部 (ThreadLocal<Object>) e.get()
        try {
            ThreadLocal.ThreadLocalMap copy = ThreadLocal.createInheritedMap(parent);
            boolean ok = copy != null;
            return "inherited-copy: " + (ok ? "OK" : "FAIL copy null");
        } catch (ClassCastException e) {
            return "inherited-copy: FAIL cce=" + e;
        } catch (Throwable t) {
            return "inherited-copy: FAIL ex=" + t;
        }
    }
}
