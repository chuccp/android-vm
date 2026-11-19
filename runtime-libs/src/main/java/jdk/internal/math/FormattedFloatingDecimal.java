package jdk.internal.math;

// 生成补源（sun-compat）：jdk.internal.math.FormattedFloatingDecimal（Formatter
// 浮点格式化）。JBR 25 已移除该内嵌 API；OpenJDK 11 语义子集（BigDecimal 实现）。
public class FormattedFloatingDecimal {
    public enum Form {
        SCIENTIFIC,
        DECIMAL_FLOAT,
        GENERAL,
        HEXADECIMAL,
    }

    private final char[] mantissa;
    private final char[] exponent;
    private final char[] exponentRounded;

    private FormattedFloatingDecimal(char[] mantissa, char[] exponent,
                                     char[] exponentRounded) {
        this.mantissa = mantissa;
        this.exponent = exponent;
        this.exponentRounded = exponentRounded;
    }

    public static FormattedFloatingDecimal valueOf(double value, int precision, Form form) {
        if (Double.isNaN(value) || Double.isInfinite(value)) {
            String s = Double.toString(value);
            return new FormattedFloatingDecimal(s.toCharArray(), new char[0], new char[0]);
        }
        java.math.BigDecimal decimal = new java.math.BigDecimal(value);
        switch (form) {
            case DECIMAL_FLOAT: {
                String plain = decimal.setScale(Math.max(precision, 0),
                        java.math.RoundingMode.HALF_UP).toPlainString();
                return new FormattedFloatingDecimal(plain.toCharArray(), new char[0], new char[0]);
            }
            case SCIENTIFIC: {
                String plain = decimal.abs().toPlainString();
                int dot = plain.indexOf('.');
                String digits = plain.replace(".", "");
                int leadZeros = 0;
                while (leadZeros < digits.length() - 1 && digits.charAt(leadZeros) == '0') {
                    leadZeros++;
                }
                digits = digits.substring(leadZeros);
                int exp10 = (dot < 0 ? plain.length() : dot) - 1 - leadZeros;
                StringBuilder mant = new StringBuilder();
                if (precision <= 0) {
                    mant.append(digits);
                } else {
                    mant.append(digits.charAt(0));
                    mant.append('.');
                    for (int i = 1; i <= precision; i++) {
                        mant.append(i < digits.length() ? digits.charAt(i) : '0');
                    }
                }
                if (value < 0) {
                    mant.insert(0, '-');
                }
                String exp = (exp10 < 0 ? "-" : "+")
                    + String.format("%02d", Math.abs(exp10));
                return new FormattedFloatingDecimal(
                    mant.toString().toCharArray(),
                    exp.toCharArray(),
                    exp.toCharArray());
            }
            case GENERAL:
            default: {
                String s = Double.toString(value);
                int e = s.indexOf('E');
                if (e < 0) {
                    return new FormattedFloatingDecimal(
                        s.toCharArray(), new char[0], new char[0]);
                }
                String mant = s.substring(0, e);
                String exp = s.substring(e + 1);
                if (exp.startsWith("+")) {
                    exp = exp.substring(1);
                }
                return new FormattedFloatingDecimal(
                    mant.toCharArray(),
                    ("E" + exp).toCharArray(),
                    ("E" + exp).toCharArray());
            }
        }
    }

    public char[] getMantissa() {
        return mantissa;
    }

    public char[] getExponent() {
        return exponent;
    }

    public int getExponentRounded() {
        try {
            return Integer.parseInt(new String(exponentRounded).replace("+", ""));
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    public char[] getExponentRoundedChars() {
        return exponentRounded;
    }

    public String toJavaFormatString() {
        return new String(mantissa) + new String(exponent);
    }
}
