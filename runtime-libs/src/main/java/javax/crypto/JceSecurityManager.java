package javax.crypto;

import java.security.Provider;

/**
 * JCE 安全管理器接缝（§31.7 C 档 lib：OpenJDK SecurityManager 形态裁剪）。
 *
 * <p>vendored OpenJDK 版本继承 SecurityManager 并以 getClassContext()/Module/
 * 签章策略判定调用方——本 VM 无这些面（且 SecurityManager.getClassContext
 * 为宿主 native 面）。Cipher 仅消费两个入口：
 * <ul>
 *   <li>{@link #isCallerTrusted(Provider)}：chooseFirstProvider 的提供方信任
 *       检查 → 恒 true（无签章概念）；</li>
 *   <li>{@link #getCryptoPermission(String)}：仅在受限策略下走
 *       （JceSecurity.isRestricted() == false 时不可达）→ 恒
 *       CryptoAllPermission。</li>
 * </ul>
 */
final class JceSecurityManager {

    static final JceSecurityManager INSTANCE = new JceSecurityManager();

    private JceSecurityManager() {
    }

    boolean isCallerTrusted(Provider provider) {
        return true;
    }

    CryptoPermission getCryptoPermission(String alg) {
        return CryptoAllPermission.INSTANCE;
    }
}
