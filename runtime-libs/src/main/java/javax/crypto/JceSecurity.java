package javax.crypto;

import java.security.NoSuchAlgorithmException;
import java.security.NoSuchProviderException;
import java.security.Provider;

import sun.security.jca.GetInstance;

/**
 * JCE 安全接缝（§31.7 C 档 lib）。vendor 的 javax.crypto.* 全部引用 OpenJDK
 * 内部类 JceSecurity——该类的 .java 不在源码树（仅 JceSecurity.java.template），
 * javac 编译期从宿主 JDK 的 java.base 解析到符号，运行期必须由本类承接，
 * 否则 Mac/Cipher/KeyGenerator 首次解析即 NoClassDefFoundError。
 *
 * <p>语义裁剪：本 VM 无签章 jar / 加密强度策略概念 → canUseProvider 恒 true
 * （provider 可用性检查直接通过）、getVerificationResult 恒 null（Cipher
 * 拒绝面不触发）、isRestricted 恒 false（无界策略，CryptoAllPermission）。
 */
final class JceSecurity {

    private JceSecurity() {
    }

    static boolean canUseProvider(Provider p) {
        return true;
    }

    static Exception getVerificationResult(Provider p) {
        return null;
    }

    static boolean isRestricted() {
        return false;
    }

    static GetInstance.Instance getInstance(String type, Class<?> clazz, String algorithm,
                                            String provider)
            throws NoSuchAlgorithmException, NoSuchProviderException {
        return GetInstance.getInstance(type, clazz, algorithm, provider);
    }

    static GetInstance.Instance getInstance(String type, Class<?> clazz, String algorithm,
                                            Provider provider)
            throws NoSuchAlgorithmException {
        return GetInstance.getInstance(type, clazz, algorithm, provider);
    }
}
