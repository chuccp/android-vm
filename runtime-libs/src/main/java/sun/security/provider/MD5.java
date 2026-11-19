package sun.security.provider;

import java.security.VmDigest;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：MD5 MessageDigestSpi
 * （RFC 1321 摘要算法；OpenJDK sun.security.provider.MD5 同算法语义，
 * 由 Sun provider "MessageDigest.MD5" 服务登记承接——
 * MessageDigest.getInstance("MD5") 消费面）。
 *
 * 计算面由 VM 原生摘要桥 java.security.VmDigest 承接（AOSP 口径：MessageDigest
 * 是 native 实现）。启动期 JarVerifier 对整个 APK 逐 entry 做 MD5 校验，
 * 纯 Java 解释执行曾是首帧前最大热点（见 core/framework_digest.cc）。
 */
public final class MD5 extends java.security.MessageDigestSpi {

    private final long handle;

    public MD5() {
        handle = VmDigest.create("MD5");
        if (handle == 0) {
            throw new InternalError("MD5: VmDigest.create failed");
        }
    }

    @Override
    protected int engineGetDigestLength() {
        return 16;
    }

    @Override
    protected void engineUpdate(byte input) {
        byte[] one = new byte[] { input };
        VmDigest.update(handle, one, 0, 1);
    }

    @Override
    protected void engineUpdate(byte[] input, int offset, int len) {
        if (input == null) {
            throw new NullPointerException("input == null");
        }
        if (offset < 0 || len < 0 || offset > input.length - len) {
            throw new ArrayIndexOutOfBoundsException();
        }
        VmDigest.update(handle, input, offset, len);
    }

    @Override
    protected byte[] engineDigest() {
        // VmDigest.digest 完成 padding/输出并复位上下文（JDK engineDigest 语义）。
        return VmDigest.digest(handle);
    }

    @Override
    protected void engineReset() {
        VmDigest.reset(handle);
    }
}
