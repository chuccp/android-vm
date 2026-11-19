package java.nio.charset;

public final class StandardCharsets {

    private StandardCharsets() {
        throw new AssertionError("No java.nio.charset.StandardCharsets instances for you!");
    }

    public static final Charset US_ASCII;

    public static final Charset ISO_8859_1;

    public static final Charset UTF_8;

    public static final Charset UTF_16BE;

    public static final Charset UTF_16LE;

    public static final Charset UTF_16;

    static {
        // 先触发 Charset.<clinit>（创建 sun.nio.cs.StandardCharsets provider 并
        // 登记全部 charset 实例），再读各 INSTANCE 字段。
        //
        // 若直接先读 US_ASCII.INSTANCE：US_ASCII 在途初始化 -> 其父类 Charset
        // 初始化 -> provider 静态块登记 US_ASCII.INSTANCE 时 US_ASCII 仍在
        // initializing（同线程重入按 JVM 语义直通）-> 读到 null 并永久登记，
        // Charset.forName("US-ASCII") 从此恒 UnsupportedCharsetException
        //（实测拖垮 Kotlin 文本工具类 clinit -> 网络/账户/配置链）。
        // 先初始化 Charset/provider 则每个 charset 的 INSTANCE 均已就绪。
        try {
            Charset.forName("UTF-8");
        } catch (Throwable ignored) {
        }
        US_ASCII = sun.nio.cs.US_ASCII.INSTANCE;
        ISO_8859_1 = sun.nio.cs.ISO_8859_1.INSTANCE;
        UTF_8 = sun.nio.cs.UTF_8.INSTANCE;
        UTF_16BE = new sun.nio.cs.UTF_16BE();
        UTF_16LE = new sun.nio.cs.UTF_16LE();
        UTF_16 = new sun.nio.cs.UTF_16();
    }
}
