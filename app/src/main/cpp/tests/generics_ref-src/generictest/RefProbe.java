package generictest;

import java.lang.ref.SoftReference;
import java.lang.ref.WeakReference;

/**
 * java.lang.ref × ThreadLocal 压力用例：set/get 深链 + SoftReference 混用 + 类型转换。
 * 目标缺口：ClassCastException: SoftReference -> ThreadLocal（zonfig 旁路分支）。
 * 每行输出 "name: OK" 或 "name: FAIL ...";harness 按行判定。
 */
public class RefProbe {

    private static StringBuilder out = new StringBuilder();

    private static void check(String name, boolean ok, String detail) {
        out.append(name).append(": ").append(ok ? "OK" : "FAIL ").append(ok ? "" : detail).append('\n');
    }

    /** ThreadLocal 语义：set 后 get 必须取回同一值（Entry.value/referent 布局完整性）。 */
    public static String run() {
        out = new StringBuilder();

        // R1: String 值 roundtrip
        {
            ThreadLocal<String> tl = new ThreadLocal<>();
            tl.set("hello");
            String v = tl.get();
            check("tl.basic", "hello".equals(v), "got " + v);
            tl.set("world");
            v = tl.get();
            check("tl.reset", "world".equals(v), "got " + v);
        }

        // R2: SoftReference 值 roundtrip（身份相等）
        {
            Object sentinel = new Object();
            SoftReference<Object> sr = new SoftReference<>(sentinel);
            ThreadLocal<SoftReference<Object>> tl = new ThreadLocal<>();
            tl.set(sr);
            SoftReference<Object> back = tl.get();
            check("tl.softrefIdentity", back == sr, "got " + back);
            check("tl.softrefReferent", back != null && back.get() == sentinel, "bad referent");
        }

        // R3: 深链——多个 ThreadLocal 各存上一个的 SoftReference，再逐层解链
        {
            Object base = new Object();
            SoftReference<Object> prev = new SoftReference<>(base);
            ThreadLocal<?>[] tls = new ThreadLocal<?>[8];
            SoftReference<?>[] refs = new SoftReference<?>[8];
            boolean ok = true;
            String detail = "";
            for (int i = 0; i < 8; i++) {
                ThreadLocal<SoftReference<Object>> tl = new ThreadLocal<>();
                tls[i] = tl;
                refs[i] = prev;
                tl.set(prev);
                prev = new SoftReference<Object>(prev);
            }
            for (int i = 7; i >= 0; i--) {
                @SuppressWarnings("unchecked")
                ThreadLocal<SoftReference<Object>> tl = (ThreadLocal<SoftReference<Object>>) tls[i];
                SoftReference<Object> got = tl.get();
                if (got != refs[i]) {
                    ok = false;
                    detail = "level " + i + ": got " + got + " want " + refs[i];
                    break;
                }
            }
            check("tl.deepChain", ok, detail);
        }

        // R4: 多槽位混用（hash 冲突/线性探测路径）+ SoftReference/WeakReference 交错
        {
            ThreadLocal<Object>[] tls = new ThreadLocal[32];
            Object[] vals = new Object[32];
            boolean ok = true;
            String detail = "";
            for (int i = 0; i < 32; i++) {
                Object v = (i % 3 == 0) ? new SoftReference<Object>(new Object())
                        : (i % 3 == 1) ? new WeakReference<Object>(new Object()) : new Object();
                vals[i] = v;
                ThreadLocal<Object> tl = new ThreadLocal<>();
                tls[i] = tl;
                tl.set(v);
                // 交错分配扰动堆相邻布局
                new SoftReference<Object>(new Object());
            }
            for (int i = 0; i < 32 && ok; i++) {
                Object got = tls[i].get();
                if (got != vals[i]) {
                    ok = false;
                    detail = "slot " + i + ": got " + got + " want " + vals[i];
                }
            }
            check("tl.mixedRefs", ok, detail);
        }

        // R5: ref 语义本体（referent roundtrip）
        {
            Object o = new Object();
            WeakReference<Object> wr = new WeakReference<>(o);
            SoftReference<Object> sr = new SoftReference<>(o);
            check("ref.weakRoundtrip", wr.get() == o, "got " + wr.get());
            check("ref.softRoundtrip", sr.get() == o, "got " + sr.get());
            wr.clear();
            check("ref.weakClear", wr.get() == null, "got " + wr.get());
        }

        // R6: 深 set/get 压力（每轮同 key 覆盖写,读回必须一致;暴露 map 膨胀/错读）
        {
            ThreadLocal<Object> tl = new ThreadLocal<>();
            boolean ok = true;
            String detail = "";
            for (int i = 0; i < 100; i++) {
                Object v = (i % 2 == 0) ? new SoftReference<Object>(new Object()) : "s" + i;
                tl.set(v);
                Object got = tl.get();
                if (got != v) {
                    ok = false;
                    detail = "iter " + i + ": got " + got + " want " + v;
                    break;
                }
            }
            check("tl.stress100", ok, detail);
        }

        // R7: String.split 委托面（lib java.util.regex.Pattern;设备考卷
        // zonfig TarsConstants <clinit> 链补缺后的回归锚点）
        {
            String[] parts = "a,b,c".split(",");
            check("str.split", parts != null && parts.length == 3 && "b".equals(parts[1]),
                    "len=" + (parts == null ? -1 : parts.length));
            String[] limited = "a,b,c".split(",", 2);
            check("str.splitLimit",
                    limited != null && limited.length == 2 && "b,c".equals(limited[1]),
                    "len=" + (limited == null ? -1 : limited.length));
        }

        return out.toString();
    }
}
