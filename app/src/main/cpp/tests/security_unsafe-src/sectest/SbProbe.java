package sectest;

/**
 * StringBuilder CS 子序列面探针（attachBaseContext 链 zonfig <clinit> 缺口回归：
 * append(CharSequence,int,int) / insert(int,CharSequence,int,int)）。
 */
public class SbProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        try {
            StringBuilder a = new StringBuilder("abc");
            a.append("defgh", 0, 4);
            sb.append(check("abcdefg".equals(a.toString()), "sb-append-cs-range"));
            StringBuilder b = new StringBuilder("ac");
            b.insert(1, "01234", 1, 4);
            sb.append(check("a123c".contentEquals(b), "sb-insert-cs-range"));
            StringBuilder c = new StringBuilder();
            c.append((CharSequence) null, 0, 4);
            sb.append(check("null".contentEquals(c), "sb-append-cs-null"));
            try {
                new StringBuilder("x").append("ab", 0, 5);
                sb.append("sb-append-cs-bounds: FAIL (no exception)\n");
            } catch (StringIndexOutOfBoundsException expected) {
                sb.append(check(true, "sb-append-cs-bounds"));
            }
        } catch (Throwable t) {
            sb.append("sb-cs-range: FAIL ").append(t).append('\n');
        }
        return sb.toString();
    }
}
