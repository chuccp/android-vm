package sun.net;

// 生成补源（sun-compat）：sun.net.InetAddressCachePolicy（java.net.InetAddress
// 解析缓存策略——NameServiceAddresses.get 成功/失败路径都引用 get()/getNegative()/
// NEVER/FOREVER）。java.base 本体有此类；runtime.dex 需源覆盖入包（patch-module
// 口径同 java.net.* vendored 源），否则 DNS 解析面 NoClassDefFoundError。
// 语义（java.net 系统属性口径）：
//   networkaddress.cache.ttl           成功缓存秒数（缺省 30）
//   networkaddress.cache.negative.ttl  失败缓存秒数（缺省 0 = 不缓存）
//   -1 = FOREVER（永不过期）/ 0 = NEVER（不缓存）。
public final class InetAddressCachePolicy {

    public static final int FOREVER = -1;
    public static final int NEVER = 0;

    private static final int cachePolicy;
    private static final int negativeCachePolicy;

    static {
        cachePolicy = init("networkaddress.cache.ttl", 30);
        negativeCachePolicy = init("networkaddress.cache.negative.ttl", 0);
    }

    private InetAddressCachePolicy() {}

    private static int init(String prop, int def) {
        String v = null;
        try {
            v = System.getProperty(prop);
        } catch (Throwable t) {
            // 无属性面：取缺省
        }
        if (v == null) return def;
        try {
            int n = Integer.parseInt(v.trim());
            return n < 0 ? FOREVER : n;
        } catch (NumberFormatException e) {
            return def;
        }
    }

    public static int get() {
        return cachePolicy;
    }

    public static int getNegative() {
        return negativeCachePolicy;
    }
}
