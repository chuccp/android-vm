package java.security;

/**
 * VM 摘要钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_digest.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 *
 * 放在 java 桶（java.base patch 源）而非 android 桶：sun.security.provider
 * 摘要 SPI 与 bridge 必须同桶编译（runtime-libs 先编 java 桶、后编 android
 * 桶，跨桶编译期不可见，否则整源被自动剔除——见 uncompilable.txt）。
 *
 * AOSP 口径：MessageDigest 由 native（Conscrypt/BoringSSL）承接。这里
 * lib sun.security.provider.{MD5,SHA1,SHA256} 保留 MessageDigestSpi 形状，
 * 计算全部下沉到 VM C++ 摘要实现（启动期 JarVerifier 全 APK 校验热点）。
 */
public final class VmDigest {
    private VmDigest() {}

    /** 创建摘要上下文并返回不透明句柄（算法不支持返回 0）。 */
    public static long create(String algorithm) {
        throw new UnsupportedOperationException("VmDigest.create: VM intrinsic");
    }

    /** 追加 input[offset, offset+len)。 */
    public static void update(long handle, byte[] input, int offset, int len) {
        throw new UnsupportedOperationException("VmDigest.update: VM intrinsic");
    }

    /** 完成摘要并复位上下文（JDK engineDigest 语义）。 */
    public static byte[] digest(long handle) {
        throw new UnsupportedOperationException("VmDigest.digest: VM intrinsic");
    }

    /** 复位上下文（保留句柄）。 */
    public static void reset(long handle) {
        throw new UnsupportedOperationException("VmDigest.reset: VM intrinsic");
    }

    /** 释放上下文。 */
    public static void close(long handle) {
        throw new UnsupportedOperationException("VmDigest.close: VM intrinsic");
    }
}
