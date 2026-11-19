package untest;

/**
 * Throwable 抛出面（设备缺口推进护栏：messagePump 链 getStackTrace 停点）。
 * 覆盖：getStackTrace 构造捕获/克隆、fillInStackTrace 重捕获、setStackTrace、
 * addSuppressed/getSuppressed（含 self 抑制 IAE）、toString、printStackTrace
 * （System.err 面；格式断言由 harness stderr 截获校验）。
 */
public class ThrowableFaceProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        try {
            Throwable t = new IllegalStateException("boom");
            StackTraceElement[] st = t.getStackTrace();
            sb.append(check(st != null && st.length > 0, "getStackTrace-nonempty"));
            boolean found = false;
            for (StackTraceElement e : st) {
                if ("untest.ThrowableFaceProbe".equals(e.getClassName())
                        && "run".equals(e.getMethodName())) {
                    found = true;
                    break;
                }
            }
            sb.append(check(found, "getStackTrace-frame-run"));

            sb.append(check(t.fillInStackTrace() == t, "fillInStackTrace-this"));
            sb.append(check(t.getStackTrace().length > 0, "fillInStackTrace-recapture"));

            t.setStackTrace(new StackTraceElement[0]);
            sb.append(check(t.getStackTrace().length == 0, "setStackTrace-empty"));

            Throwable sup = new RuntimeException("suppressed");
            t.addSuppressed(sup);
            Throwable[] arr = t.getSuppressed();
            sb.append(check(arr.length == 1 && arr[0] == sup, "addSuppressed-roundtrip"));
            try {
                t.addSuppressed(t);
                sb.append("addSuppressed-self: FAIL no throw\n");
            } catch (IllegalArgumentException expected) {
                sb.append("addSuppressed-self: OK\n");
            }

            sb.append(check("java.lang.IllegalStateException: boom".equals(t.toString()),
                    "toString"));

            // printStackTrace()（System.err 面）：格式断言由 harness 截获 stderr 校验
            // （"at untest.ThrowableFaceProbe.run" / "Suppressed: ..." / "Caused by: ..."）。
            // printStackTrace(PrintWriter) 形依赖 java.io lib 栈（StringWriter ->
            // StringBuffer/AbstractStringBuilder——OpenJDK byte[] value 形与 VString
            // 布局错位，docs 14 章 §31.5 P2 迁移面），本探针不走。
            Throwable cause = new IllegalArgumentException("root");
            Throwable wrapped = new IllegalStateException("wrap", cause);
            wrapped.addSuppressed(new RuntimeException("suppressed"));
            wrapped.printStackTrace();
        } catch (Throwable err) {
            sb.append("throwface-thrown: FAIL " + err + "\n");
        }
        return sb.toString();
    }
}
