package android.util;

/**
 * 二元组（Android 公开面；§31.7 C 档 POJO→lib 形态）。
 *
 * <p>AOSP android.util.Pair 公开面收敛：first/second 终态字段 + 构造 +
 * create 静面 + equals/hashCode/toString。泛型按擦除落 Object 字段
 * （dex 形态一致）；无 VM 布局耦合（lib 派生布局）。
 */
public class Pair<F, S> {
    public final F first;
    public final S second;

    public Pair(F first, S second) {
        this.first = first;
        this.second = second;
    }

    public static <A, B> Pair<A, B> create(A a, B b) {
        return new Pair<A, B>(a, b);
    }

    private static boolean equal(Object a, Object b) {
        return (a == b) || (a != null && a.equals(b));
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) {
            return true;
        }
        if (!(o instanceof Pair)) {
            return false;
        }
        Pair<?, ?> other = (Pair<?, ?>) o;
        return equal(other.first, first) && equal(other.second, second);
    }

    @Override
    public int hashCode() {
        return (first == null ? 0 : first.hashCode()) ^ (second == null ? 0 : second.hashCode());
    }

    @Override
    public String toString() {
        return "Pair{" + String.valueOf(first) + " " + String.valueOf(second) + "}";
    }
}
