package sun.security.provider;

import java.security.VmDigest;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：SHA-1 MessageDigestSpi
 * （FIPS 180-4 摘要算法；OpenJDK sun.security.provider.SHA1 同算法语义，
 * 由 Sun provider "MessageDigest.SHA-1" 服务登记承接——
 * MessageDigest.getInstance("SHA-1") 消费面）。
 *
 * 计算面由 VM 原生摘要桥 java.security.VmDigest 承接（AOSP native 口径）。
 */
public final class SHA1 extends java.security.MessageDigestSpi {

    private final long handle;

    public SHA1() {
        handle = VmDigest.create("SHA-1");
        if (handle == 0) {
            throw new InternalError("SHA1: VmDigest.create failed");
        }
    }

    @Override
    protected int engineGetDigestLength() {
        return 20;
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
        return VmDigest.digest(handle);
    }

    @Override
    protected void engineReset() {
        VmDigest.reset(handle);
    }
}
