package sun.security.jca;

import java.security.Provider;
import sun.security.provider.Sun;

/**
 * Provider 注册表接缝（§31.7 C 档 lib：OpenJDK 公开面形态）。
 *
 * <p>消费口（SecureRandom.getDefaultPRNG 链）：{@link #getProviderList()} /
 * {@link #getSunProvider()}。默认表 = SUN provider（OpenJDK 兜底同款；
 * security.provider.* 配置装载随 java.security 文件面后置）。
 */
public final class Providers {

    private static final Provider SUN = new Sun();

    private static volatile ProviderList providerList = ProviderList.of(SUN);

    private Providers() {
    }

    public static ProviderList getProviderList() {
        return providerList;
    }

    public static void setProviderList(ProviderList newList) {
        if (newList == null) {
            throw new NullPointerException("newList == null");
        }
        providerList = newList;
    }

    public static Provider getSunProvider() {
        return SUN;
    }

    public static Provider getProvider(String name) {
        return providerList.getProvider(name);
    }
}
