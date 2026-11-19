package java.net;

import java.io.UnsupportedEncodingException;
import java.nio.charset.Charset;
import java.util.Objects;

public class URLEncoder {
    private static final char[] HEX = "0123456789ABCDEF".toCharArray();

    private URLEncoder() {}

    // Android/OpenJDK8 兼容面：单参 encode 等价 UTF-8 变体（Android app 的
    // 旧 API 调用点大量存在；缺失会在链接期 NoSuchMethodError）。
    @Deprecated
    public static String encode(String text) throws UnsupportedEncodingException {
        return encode(text, "UTF-8");
    }

    public static String encode(String text, String charsetName)
            throws UnsupportedEncodingException {
        Objects.requireNonNull(charsetName);
        return encode(text, Charset.forName(charsetName));
    }

    public static String encode(String text, Charset charset) {
        Objects.requireNonNull(text);
        Objects.requireNonNull(charset);
        byte[] source = text.getBytes(charset);

        StringBuilder result = new StringBuilder(source.length * 3);
        for (byte signed : source) {
            int value = signed & 0xff;
            if (isSafe(value)) {
                result.append((char) value);
            } else if (value == ' ') {
                result.append('+');
            } else {
                result.append('%');
                result.append(HEX[value >> 4]);
                result.append(HEX[value & 0x0f]);
            }
        }
        return result.toString();
    }

    private static boolean isSafe(int value) {
        return (value >= 'a' && value <= 'z') ||
                (value >= 'A' && value <= 'Z') ||
                (value >= '0' && value <= '9') ||
                value == '-' || value == '_' ||
                value == '.' || value == '*';
    }
}
