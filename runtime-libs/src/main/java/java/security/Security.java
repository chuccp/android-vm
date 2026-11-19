package java.security;

import java.util.HashSet;
import java.util.Properties;
import java.util.Set;

import sun.security.jca.Providers;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：java.security.Security 最小面
 * ——属性面（getProperty/setProperty，GetPropertyAction 先例同口径：System
 * properties 承接，java.security 文件装载后置）+ JCA 转发面（getImpl/
 * getProvider/getProviders/getAlgorithms，经 sun.security.jca.Providers 服务
 * 表）。vendored OpenJDK 11 Security.java 依赖 jdk.internal.event.* 等
 * JBR 25 已漂移面（构建期自动跳过 uncompilable.txt），本类按消费面语义重写：
 * MessageDigest/AlgorithmParameters/KeyStore 的 getImpl 解析链自此可跑
 * （okio Buffer.sha256 消费面）。
 *
 * <p>getImpl 返回 {impl, provider}（OpenJDK Security.getImpl 同形状）：
 * impl 由 Provider.Service.newInstance(null) 例化（SPI 或引擎自实现）。
 */
public final class Security {

    private Security() {
    }

    // ---- 属性面（OpenJDK security property 最小承接：System property 优先）----

    public static String getProperty(String key) {
        return System.getProperty(key);
    }

    public static void setProperty(String key, String datum) {
        System.setProperty(key, datum);
    }

    // ---- JCA 面（Providers 服务表转发；表构建见 sun.security.jca.Providers）----

    public static Provider[] getProviders() {
        return Providers.getProviderList().providers().toArray(new Provider[0]);
    }

    public static Provider getProvider(String name) {
        return Providers.getProvider(name);
    }

    /** 优先级序首个提供该服务的 provider 上例化（OpenJDK 无指定 provider 语义）。 */
    public static Object[] getImpl(String algorithm, String type, String provider)
            throws NoSuchAlgorithmException, NoSuchProviderException {
        Provider p = (provider == null) ? null : Providers.getProvider(provider);
        if (provider != null && p == null) {
            throw new NoSuchProviderException(provider);
        }
        return getImpl(algorithm, type, p);
    }

    public static Object[] getImpl(String algorithm, String type, Provider provider)
            throws NoSuchAlgorithmException {
        if (provider != null) {
            Provider.Service s = provider.getService(type, algorithm);
            if (s == null) {
                throw new NoSuchAlgorithmException(algorithm + " " + type + " not available");
            }
            return new Object[] {s.newInstance(null), provider};
        }
        for (Provider p : Providers.getProviderList()) {
            Provider.Service s = p.getService(type, algorithm);
            if (s != null) {
                return new Object[] {s.newInstance(null), p};
            }
        }
        throw new NoSuchAlgorithmException(algorithm + " " + type + " not available");
    }

    /** 注册 provider 表内指定服务的全部算法名（OpenJDK 同名面）。 */
    public static Set<String> getAlgorithms(String serviceName) {
        Set<String> result = new HashSet<>();
        for (Provider p : Providers.getProviderList()) {
            for (Provider.Service s : p.getServices()) {
                if (s.getType().equalsIgnoreCase(serviceName)) {
                    result.add(s.getAlgorithm());
                }
            }
        }
        return result;
    }

    /** 遗留属性镜像（OpenJDK getSecurityProperties 同语义；属性面最小承接）。 */
    static Properties getSecurityProperties() {
        return System.getProperties();
    }
}
