package sun.security.util;

import sun.security.action.GetPropertyAction;

/**
 * 安全调试面（§31.7 C 档 lib：OpenJDK 公开面形态）。
 *
 * <p>面（SecureRandom.&#60;clinit&#62; pdebug 面等消费口）：
 * {@link #getInstance(String)} / {@link #getInstance(String, String)} /
 * {@link #isOn(String)} / {@link #println()} / {@link #println(String)} /
 * {@link #toHexString(byte[])}；输出语义 = {@code System.err}（OpenJDK 同款）。
 *
 * <p>配置驱动：{@code java.security.debug} 系统属性（OpenJDK 双源之一；
 * 安全属性源随 java.security 文件面后置——Security.&#60;clinit&#62; 依赖文件 IO 链，
 * 本类保持零依赖以免拖链）。逗号分隔类别表，{@code all} 全开；查询
 * {@code "x="} 前缀形对配置项 {@code "x=..."} 生效（engine= 消费口）。
 */
public class Debug {

    private String prefix;
    private static String args;

    static {
        args = GetPropertyAction.privilegedGetProperty("java.security.debug");
    }

    private Debug() {
    }

    /** 调试开启时返回调试面实例，否则 null（OpenJDK 语义）。 */
    public static Debug getInstance(String option) {
        return getInstance(option, option);
    }

    /** 同 {@link #getInstance(String)}，输出前缀取 {@code prefix}。 */
    public static Debug getInstance(String option, String prefix) {
        if (isOn(option)) {
            Debug d = new Debug();
            d.prefix = prefix;
            return d;
        }
        return null;
    }

    /**
     * 该调试类别是否开启：{@code all} 全开；逗号分隔表逐项匹配——
     * 精确名命中，或查询为 {@code "x="} 前缀形且配置项以 {@code "x="} 起头。
     */
    public static boolean isOn(String option) {
        if (args == null) {
            return false;
        }
        if (args.equals("all")) {
            return true;
        }
        int n = args.length();
        int start = 0;
        while (start <= n) {
            int end = args.indexOf(',', start);
            if (end < 0) end = n;
            String s = args.substring(start, end).trim();
            if (s.equals(option) || (option.endsWith("=") && s.startsWith(option))) {
                return true;
            }
            start = end + 1;
        }
        return false;
    }

    /** 输出 {@code "<prefix>:"} 到 System.err。 */
    public void println() {
        if (args != null) {
            System.err.println(prefix + ":");
        }
    }

    /** 输出 {@code "<prefix>: <message>"} 到 System.err。 */
    public void println(String message) {
        if (args != null) {
            System.err.println(prefix + ": " + message);
        }
    }

    @Override
    public String toString() {
        return prefix;
    }

    /** 字节组 → 小写十六进制（证书/指纹诊断面）。 */
    public static String toHexString(byte[] b) {
        if (b == null) {
            return "(null)";
        }
        StringBuilder sb = new StringBuilder(b.length * 2);
        for (int i = 0; i < b.length; i++) {
            int v = b[i] & 0xff;
            sb.append(Character.forDigit(v >>> 4, 16));
            sb.append(Character.forDigit(v & 0xf, 16));
        }
        return sb.toString();
    }
}
