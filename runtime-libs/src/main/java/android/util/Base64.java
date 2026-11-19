package android.util;

/**
 * AOSP android.util.Base64 的 lib 实现（flags 语义对齐 Android）：
 * DEFAULT 默认每 76 字符换行并以换行结束；NO_WRAP 去换行；NO_PADDING 去
 * '=' 填充；CRLF 换行用 \r\n；URL_SAFE 用 -_ 字母表。
 *
 * 知乎签名拦截器（com.zhihu.android.net.e.s）与 OkHttp cache 拦截器都直接
 * 使用本类；缺失时 NoClassDefFoundError 会打断整条请求链。
 */
public final class Base64 {

    public static final int DEFAULT = 0;
    public static final int NO_PADDING = 1;
    public static final int NO_WRAP = 2;
    public static final int CRLF = 4;
    public static final int URL_SAFE = 8;

    private static final char[] STD =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/".toCharArray();
    private static final char[] URL =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_".toCharArray();
    private static final int LINE_LEN = 76;

    private Base64() {
    }

    public static byte[] encode(byte[] input, int flags) {
        if (input == null) return null;
        return encode(input, 0, input.length, flags);
    }

    public static byte[] encode(byte[] input, int offset, int len, int flags) {
        if (input == null) return null;
        if (offset < 0 || len < 0 || offset > input.length - len) {
            throw new ArrayIndexOutOfBoundsException(offset);
        }
        char[] raw = encodeChars(input, offset, len, flags);
        byte[] out = new byte[raw.length];
        for (int i = 0; i < raw.length; i++) out[i] = (byte) raw[i];
        return out;
    }

    public static String encodeToString(byte[] input, int flags) {
        if (input == null) return null;
        return new String(encodeChars(input, 0, input.length, flags));
    }

    public static String encodeToString(byte[] input, int offset, int len, int flags) {
        if (input == null) return null;
        if (offset < 0 || len < 0 || offset > input.length - len) {
            throw new ArrayIndexOutOfBoundsException(offset);
        }
        return new String(encodeChars(input, offset, len, flags));
    }

    private static char[] encodeChars(byte[] input, int offset, int len, int flags) {
        boolean urlSafe = (flags & URL_SAFE) != 0;
        boolean pad = (flags & NO_PADDING) == 0;
        boolean wrap = (flags & NO_WRAP) == 0;
        boolean crlf = (flags & CRLF) != 0;
        char[] alpha = urlSafe ? URL : STD;

        StringBuilder sb = new StringBuilder((len + 2) / 3 * 4 + 8);
        int i = offset;
        int end = offset + len;
        while (i < end) {
            int b0 = input[i++] & 0xFF;
            int b1 = i < end ? input[i++] & 0xFF : -1;
            int b2 = i < end ? input[i++] & 0xFF : -1;
            sb.append(alpha[b0 >> 2]);
            sb.append(alpha[((b0 & 0x03) << 4) | (b1 < 0 ? 0 : (b1 >> 4))]);
            if (b1 < 0) {
                if (pad) sb.append('=').append('=');
            } else {
                sb.append(alpha[((b1 & 0x0F) << 2) | (b2 < 0 ? 0 : (b2 >> 6))]);
                if (b2 < 0) {
                    if (pad) sb.append('=');
                } else {
                    sb.append(alpha[b2 & 0x3F]);
                }
            }
        }

        String raw = sb.toString();
        if (!wrap) {
            char[] out = new char[raw.length()];
            raw.getChars(0, raw.length(), out, 0);
            return out;
        }
        String nl = crlf ? "\r\n" : "\n";
        StringBuilder wrapped = new StringBuilder(raw.length() + raw.length() / LINE_LEN * 2 + 2);
        for (int k = 0; k < raw.length(); k++) {
            if (k > 0 && k % LINE_LEN == 0) wrapped.append(nl);
            wrapped.append(raw.charAt(k));
        }
        wrapped.append(nl);  // AOSP：默认以行终止符结束
        char[] out = new char[wrapped.length()];
        wrapped.getChars(0, wrapped.length(), out, 0);
        return out;
    }

    public static byte[] decode(String str, int flags) {
        if (str == null) return null;
        int len = str.length();
        byte[] input = new byte[len];
        for (int i = 0; i < len; i++) input[i] = (byte) str.charAt(i);
        return decode(input, 0, len, flags);
    }

    public static byte[] decode(byte[] input, int flags) {
        if (input == null) return null;
        return decode(input, 0, input.length, flags);
    }

    public static byte[] decode(byte[] input, int offset, int len, int flags) {
        if (input == null) return null;
        if (offset < 0 || len < 0 || offset > input.length - len) {
            throw new ArrayIndexOutOfBoundsException(offset);
        }
        byte[] scratch = new byte[(len / 4 + 1) * 3];
        int outLen = 0;
        int n = 0;
        int bits = 0;
        for (int i = offset; i < offset + len; i++) {
            int b = input[i] & 0xFF;
            if (b == '=') break;          // 填充结束
            if (b <= ' ') continue;        // 空白/换行忽略
            int d = digit((char) b);
            if (d < 0) throw new IllegalArgumentException("bad base-64");
            bits = (bits << 6) | d;
            n++;
            if (n == 4) {
                scratch[outLen++] = (byte) (bits >> 16);
                scratch[outLen++] = (byte) (bits >> 8);
                scratch[outLen++] = (byte) bits;
                n = 0;
                bits = 0;
            }
        }
        if (n == 1) throw new IllegalArgumentException("bad base-64");
        if (n == 2) {
            scratch[outLen++] = (byte) (bits >> 4);
        } else if (n == 3) {
            scratch[outLen++] = (byte) (bits >> 10);
            scratch[outLen++] = (byte) (bits >> 2);
        }
        byte[] out = new byte[outLen];
        System.arraycopy(scratch, 0, out, 0, outLen);
        return out;
    }

    private static int digit(char c) {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    }
}
