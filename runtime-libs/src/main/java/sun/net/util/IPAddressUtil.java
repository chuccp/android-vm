package sun.net.util;

// 生成补源（sun-compat）：sun.net.IPAddressUtil（java.net 解析面字面量通道——
// InetAddress.getAllByName 字面量分流 / SocketPermission / HostPortrange 消费）。
// 语义对齐 OpenJDK：textToNumericFormatV4 严格点分四段；textToNumericFormatV6
// 全形 IPv6（"::" 压缩 + 尾部内嵌 IPv4 + %zone 后缀容忍）；字面量判定 = 解析非空。
public class IPAddressUtil {
    public static byte[] validateNumericFormatV4(String ip) {
        return textToNumericFormatV4(ip);
    }

    public static byte[] validateNumericFormatV4(String ip, boolean required) {
        return textToNumericFormatV4(ip);
    }

    public static boolean isIPv4LiteralAddress(String src) {
        return textToNumericFormatV4(src) != null;
    }

    public static boolean isIPv6LiteralAddress(String src) {
        return textToNumericFormatV6(src) != null;
    }

    /** 严格点分四段（各段 0..255 十进制；无空白/符号；前导零按十进制）。 */
    public static byte[] textToNumericFormatV4(String src) {
        if (src == null) {
            return null;
        }
        byte[] out = new byte[4];
        int pos = 0;
        for (int i = 0; i < 4; i++) {
            int val = 0;
            int digits = 0;
            while (pos < src.length()) {
                char c = src.charAt(pos);
                if (c < '0' || c > '9') {
                    break;
                }
                val = val * 10 + (c - '0');
                if (val > 255) {
                    return null;
                }
                digits++;
                pos++;
                if (digits > 3) {
                    return null;
                }
            }
            if (digits == 0) {
                return null;
            }
            out[i] = (byte) val;
            if (i < 3) {
                if (pos >= src.length() || src.charAt(pos) != '.') {
                    return null;
                }
                pos++;
            }
        }
        return pos == src.length() ? out : null;
    }

    private static int parseHexGroup(String s) {
        if (s.isEmpty() || s.length() > 4) {
            return -1;
        }
        int v = 0;
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            int d;
            if (c >= '0' && c <= '9') {
                d = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                d = c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                d = c - 'A' + 10;
            } else {
                return -1;
            }
            v = (v << 4) | d;
        }
        return v;
    }

    // 组段解析进 h[off..)；支持尾部内嵌 IPv4（占 2 组）。返回组数，非法 = -1。
    private static int parseHextets(String s, int[] h, int off, int limit) {
        if (s.isEmpty()) {
            return 0;
        }
        int n = 0;
        int pos = 0;
        while (pos <= s.length()) {
            int end = s.indexOf(':', pos);
            if (end == -1) {
                end = s.length();
            }
            String p = s.substring(pos, end);
            pos = end + 1;
            if (p.isEmpty()) {
                return -1;  // 空组（"::" 之外的连续 ':'）
            }
            if (p.indexOf('.') >= 0) {  // 尾部内嵌 IPv4
                if (end != s.length()) {
                    return -1;
                }
                byte[] v4 = textToNumericFormatV4(p);
                if (v4 == null || off + n + 2 > limit) {
                    return -1;
                }
                h[off + n] = ((v4[0] & 0xff) << 8) | (v4[1] & 0xff);
                h[off + n + 1] = ((v4[2] & 0xff) << 8) | (v4[3] & 0xff);
                return n + 2;
            }
            int v = parseHexGroup(p);
            if (v < 0 || off + n >= limit) {
                return -1;
            }
            h[off + n] = v;
            n++;
        }
        return n;
    }

    /**
     * 全形 IPv6 解析：最多一个 "::"（压缩 ≥1 组）；两端组段可含尾部内嵌 IPv4；
     * %zone / %ifname 后缀容忍（InetAddress 侧另行拆 zone）。
     */
    public static byte[] textToNumericFormatV6(String src) {
        if (src == null) {
            return null;
        }
        int pct = src.indexOf('%');
        if (pct >= 0) {
            src = src.substring(0, pct);
        }
        if (src.length() >= 2 && src.charAt(0) == '[' && src.charAt(src.length() - 1) == ']') {
            src = src.substring(1, src.length() - 1);
        }
        if (src.isEmpty()) {
            return null;
        }
        int dbl = src.indexOf("::");
        if (dbl != src.lastIndexOf("::")) {
            return null;  // 只允许一个 "::"
        }
        int[] h = new int[8];
        int hn;
        int tn = 0;
        if (dbl >= 0) {
            hn = parseHextets(src.substring(0, dbl), h, 0, 8);
            if (hn < 0) {
                return null;
            }
            int[] t = new int[8];
            tn = parseHextets(src.substring(dbl + 2), t, 0, 8);
            if (tn < 0) {
                return null;
            }
            if (hn + tn > 7) {
                return null;  // "::" 至少压缩 1 组
            }
            // 右段右对齐搬入 h[8-tn..8)
            System.arraycopy(t, 0, h, 8 - tn, tn);
            for (int i = hn; i < 8 - tn; i++) {
                h[i] = 0;
            }
        } else {
            hn = parseHextets(src, h, 0, 8);
            if (hn != 8) {
                return null;
            }
        }
        byte[] out = new byte[16];
        for (int i = 0; i < 8; i++) {
            out[i * 2] = (byte) (h[i] >> 8);
            out[i * 2 + 1] = (byte) h[i];
        }
        return out;
    }

    public static String numericToTextFormat(byte[] src) {
        if (src == null) {
            return "";
        }
        if (src.length == 4) {
            return (src[0] & 0xff) + "." + (src[1] & 0xff) + "."
                    + (src[2] & 0xff) + "." + (src[3] & 0xff);
        }
        if (src.length != 16) {
            return "";
        }
        StringBuilder sb = new StringBuilder(40);
        for (int i = 0; i < 16; i += 2) {
            if (i > 0) {
                sb.append(':');
            }
            sb.append(Integer.toHexString(((src[i] & 0xff) << 8) | (src[i + 1] & 0xff)));
        }
        return sb.toString();
    }

    public static byte[] convertFromIPv4MappedAddress(byte[] addr) {
        if (addr == null || addr.length != 16) return null;
        for (int i = 0; i < 10; i++) {
            if (addr[i] != 0) return null;
        }
        if ((addr[10] & 0xff) != 0xff || (addr[11] & 0xff) != 0xff) return null;
        byte[] out = new byte[4];
        System.arraycopy(addr, 12, out, 0, 4);
        return out;
    }


    public static int digit(char c, int radix) {
        return Character.digit(c, radix);
    }

    public static int parseAsciiDigit(char c, int radix) {
        return Character.digit(c, radix);
    }

    public static String checkExternalForm(java.net.URL url) {
        return null;
    }

    public static String checkAuthority(java.net.URL url) {
        return null;
    }

    public static String checkHostString(String host) {
        return null;
    }

    public static java.net.InetAddress toScopedAddress(java.net.InetAddress address) {
        return address;
    }

}
