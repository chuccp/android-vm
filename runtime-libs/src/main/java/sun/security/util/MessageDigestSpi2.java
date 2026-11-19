package sun.security.util;

import java.security.InvalidKeyException;

import javax.crypto.SecretKey;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：sun.security.util.MessageDigestSpi2
 * 最小面（JBR java.base 内部接口；运行面缺类时 MessageDigest$Delegate 链接失败
 * ——okio MessageDigest.getInstance 链的 SPI2 桥接面）。形状对照 JBR 类
 * （engineUpdate(SecretKey) 单方法）。
 */
public interface MessageDigestSpi2 {

    void engineUpdate(SecretKey key) throws InvalidKeyException;
}
