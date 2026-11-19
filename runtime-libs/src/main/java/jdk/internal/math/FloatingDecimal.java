package jdk.internal.math;

import java.io.IOException;
import java.math.BigDecimal;
import java.math.MathContext;
import java.math.RoundingMode;

/**
 * 生成补源（sun-compat）：jdk.internal.math.FloatingDecimal（JBR 25 形状漂移；
 * 消费面 = java.lang.Double/Float/AbstractStringBuilder、java.text.DigitList）。
 *
 * <p>语义（OpenJDK 公开面）：double/float ↔ Java 十进制格式串（最短往返、
 * 10^-3 ≤ |x| < 10^7 走定点形，其余科学计数 d.dddE±x；Infinity/NaN/±0.0）。
 * 实现：BigDecimal 精确二→十 + 逐精度（double 1..17 / float 1..9）最短往返
 * 试探（round-trip 位等判定），解析走 BigDecimal.doubleValue()/floatValue()
 * （IEEE 正确舍入）。
 */
public class FloatingDecimal {

    private FloatingDecimal() {
    }

    public interface BinaryToASCIIConverter {
        String toJavaFormatString();

        boolean digitsRoundedUp();

        boolean decimalDigitsExact();

        boolean isExceptional();
    }

    public static String toJavaFormatString(double d) {
        if (Double.isNaN(d)) return "NaN";
        if (Double.isInfinite(d)) return d > 0 ? "Infinity" : "-Infinity";
        if (d == 0.0) {
            return (Double.doubleToRawLongBits(d) < 0) ? "-0.0" : "0.0";
        }
        boolean neg = d < 0;
        double abs = neg ? -d : d;
        for (int p = 1; p <= 17; p++) {
            String s = format(abs, p);
            if (parseDouble(s) == abs) return neg ? "-" + s : s;
        }
        return format(abs, 17);
    }

    public static String toJavaFormatString(float f) {
        if (Float.isNaN(f)) return "NaN";
        if (Float.isInfinite(f)) return f > 0 ? "Infinity" : "-Infinity";
        if (f == 0.0f) {
            return (Float.floatToRawIntBits(f) < 0) ? "-0.0" : "0.0";
        }
        boolean neg = f < 0;
        float abs = neg ? -f : f;
        for (int p = 1; p <= 9; p++) {
            String s = format(abs, p);
            if (parseFloat(s) == abs) return neg ? "-" + s : s;
        }
        return format(abs, 9);
    }

    public static double parseDouble(String s) {
        if (s == null) throw new NumberFormatException("null");
        String t = s.trim();
        if (t.equals("NaN")) return Double.NaN;
        if (t.equals("Infinity") || t.equals("+Infinity")) return Double.POSITIVE_INFINITY;
        if (t.equals("-Infinity")) return Double.NEGATIVE_INFINITY;
        try {
            return new BigDecimal(t).doubleValue();
        } catch (NumberFormatException e) {
            throw e;
        }
    }

    public static float parseFloat(String s) {
        if (s == null) throw new NumberFormatException("null");
        String t = s.trim();
        if (t.equals("NaN")) return Float.NaN;
        if (t.equals("Infinity") || t.equals("+Infinity")) return Float.POSITIVE_INFINITY;
        if (t.equals("-Infinity")) return Float.NEGATIVE_INFINITY;
        return new BigDecimal(t).floatValue();
    }

    public static void appendTo(double d, Appendable buf) {
        try {
            buf.append(toJavaFormatString(d));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    public static void appendTo(float f, Appendable buf) {
        try {
            buf.append(toJavaFormatString(f));
        } catch (IOException e) {
            throw new RuntimeException(e);
        }
    }

    public static BinaryToASCIIConverter getBinaryToASCIIConverter(double d) {
        final String text = toJavaFormatString(d);
        final boolean exceptional = Double.isNaN(d) || Double.isInfinite(d);
        final BigDecimal exact = exceptional ? null : new BigDecimal(d < 0 ? -d : d);
        final BigDecimal asDecimal = exceptional ? null : new BigDecimal(stripSign(text));
        return new BinaryToASCIIConverter() {
            @Override
            public String toJavaFormatString() {
                return text;
            }

            @Override
            public boolean digitsRoundedUp() {
                return !exceptional && asDecimal.compareTo(exact) > 0;
            }

            @Override
            public boolean decimalDigitsExact() {
                return !exceptional && asDecimal.compareTo(exact) == 0;
            }

            @Override
            public boolean isExceptional() {
                return exceptional;
            }
        };
    }

    public static BinaryToASCIIConverter getBinaryToASCIIConverter(float f) {
        return getBinaryToASCIIConverter((double) f);
    }

    private static String stripSign(String s) {
        if (!s.isEmpty() && s.charAt(0) == '-') return s.substring(1);
        return s;
    }

    /** |x| 的 p 有效数字十进制格式（Java Double.toString 形；HALF_UP 舍入）。 */
    private static String format(double abs, int p) {
        BigDecimal rounded =
                new BigDecimal(abs).round(new MathContext(p, RoundingMode.HALF_UP));
        String digits = rounded.unscaledValue().abs().toString();
        int scale = rounded.scale();
        int q = (digits.length() - 1) - scale;  // 首位数字十进制指数
        StringBuilder sb = new StringBuilder(digits.length() + 8);
        if (q >= -3 && q <= 6) {
            // 定点形：≥1 位小数
            if (q >= 0) {
                int intLen = q + 1;
                if (digits.length() > intLen) {
                    sb.append(digits.substring(0, intLen));
                    sb.append('.');
                    sb.append(digits.substring(intLen));
                } else {
                    sb.append(digits);
                    for (int i = digits.length(); i < intLen; i++) sb.append('0');
                    sb.append(".0");
                }
            } else {
                sb.append("0.");
                for (int i = 0; i < -q - 1; i++) sb.append('0');
                sb.append(digits);
            }
        } else {
            sb.append(digits.charAt(0));
            sb.append('.');
            sb.append(digits.length() > 1 ? digits.substring(1) : "0");
            sb.append('E');
            sb.append(q);
        }
        return sb.toString();
    }
}
