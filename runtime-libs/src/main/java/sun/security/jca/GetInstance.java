package sun.security.jca;

import java.security.NoSuchAlgorithmException;
import java.security.Provider;
import java.util.ArrayList;
import java.util.List;

/**
 * JCA 服务解析接缝（§31.7 C 档 lib：OpenJDK GetInstance 公开面形态）。
 *
 * <p>原生成的空桩（恒空服务表/空 Instance）使 javax.crypto.Mac、
 * KeyGenerator、KeyAgreement、SecretKeyFactory 等全部 no-such-algorithm
 * （实测知乎 zcloud 账户链 HmacSHA1/HmacSHA256 因此中断，currentAccount
 * 恒 null）。现按 OpenJDK 语义落到 {@link Providers} 服务表：
 * getServices 收集全部命中服务（优先级序），getInstance 取首个命中并经
 * Provider.Service.newInstance 例化。
 *
 * <p>provider 名不存在报 NoSuchAlgorithmException（不抛受检
 * NoSuchProviderException，避免既有调用方 throws 面兼容漂移）。
 */
public class GetInstance {

    public static class Instance<T> {
        public Provider provider;
        public T impl;
        public Provider.Service service;

        public Object[] toArray() {
            return new Object[] {provider, impl, service};
        }
    }

    private GetInstance() {
    }

    // ---- 服务表查询 ----

    public static List<Provider.Service> getServices(String type, String algorithm) {
        List<Provider.Service> out = new ArrayList<>();
        if (type == null || algorithm == null) {
            return out;
        }
        for (Provider p : Providers.getProviderList()) {
            Provider.Service s = p.getService(type, algorithm);
            if (s != null) {
                out.add(s);
            }
        }
        return out;
    }

    public static List<Provider.Service> getServices(String type, List<String> algorithms) {
        List<Provider.Service> out = new ArrayList<>();
        if (algorithms == null) {
            return out;
        }
        for (String a : algorithms) {
            out.addAll(getServices(type, a));
        }
        return out;
    }

    /** ServiceId 列表形（OpenJDK Signature rsaIds 链同构）。 */
    public static List<Provider.Service> getServices(List<?> ids) {
        List<Provider.Service> out = new ArrayList<>();
        if (ids == null) {
            return out;
        }
        for (Object id : ids) {
            ServiceId sid = (ServiceId) id;
            out.addAll(getServices(sid.service, sid.algorithm));
        }
        return out;
    }

    // ---- 服务例化 ----

    public static <T> Instance<T> getInstance(String type, Class<T> clazz, String algorithm)
            throws NoSuchAlgorithmException {
        return resolve(type, clazz, algorithm, null, null, null);
    }

    public static <T> Instance<T> getInstance(String type, Class<T> clazz, String algorithm,
                                              String provider)
            throws NoSuchAlgorithmException {
        return resolve(type, clazz, algorithm, provider, null, null);
    }

    public static <T> Instance<T> getInstance(String type, Class<T> clazz, String algorithm,
                                              Provider provider)
            throws NoSuchAlgorithmException {
        return resolve(type, clazz, algorithm, null, provider, null);
    }

    public static <T> Instance<T> getInstance(String type, Class<T> clazz, String algorithm,
                                              Object param)
            throws NoSuchAlgorithmException {
        return resolve(type, clazz, algorithm, null, null, param);
    }

    public static <T> Instance<T> getInstance(String type, Class<T> clazz, String algorithm,
                                              Object param, String provider)
            throws NoSuchAlgorithmException {
        return resolve(type, clazz, algorithm, provider, null, param);
    }

    public static <T> Instance<T> getInstance(String type, Class<T> clazz, String algorithm,
                                              Object param, Provider provider)
            throws NoSuchAlgorithmException {
        return resolve(type, clazz, algorithm, null, provider, param);
    }

    public static <T> Instance<T> getInstance(Provider.Service service, Class<T> clazz)
            throws NoSuchAlgorithmException {
        return instantiate(service, clazz, null);
    }

    private static <T> Instance<T> resolve(String type, Class<T> clazz, String algorithm,
                                           String providerName, Provider provider, Object param)
            throws NoSuchAlgorithmException {
        if (providerName != null) {
            Provider p = Providers.getProvider(providerName);
            if (p == null) {
                throw new NoSuchAlgorithmException("Provider " + providerName + " not found");
            }
            return resolveFrom(p, type, algorithm, clazz, param);
        }
        if (provider != null) {
            return resolveFrom(provider, type, algorithm, clazz, param);
        }
        for (Provider p : Providers.getProviderList()) {
            Provider.Service s = p.getService(type, algorithm);
            if (s != null) {
                return instantiate(s, clazz, param);
            }
        }
        throw new NoSuchAlgorithmException(algorithm + " " + type + " not available");
    }

    private static <T> Instance<T> resolveFrom(Provider p, String type, String algorithm,
                                               Class<T> clazz, Object param)
            throws NoSuchAlgorithmException {
        Provider.Service s = p.getService(type, algorithm);
        if (s == null) {
            throw new NoSuchAlgorithmException(
                    algorithm + " " + type + " not available from " + p.getName());
        }
        return instantiate(s, clazz, param);
    }

    @SuppressWarnings("unchecked")
    private static <T> Instance<T> instantiate(Provider.Service service, Class<T> clazz,
                                               Object param)
            throws NoSuchAlgorithmException {
        Instance<T> out = new Instance<>();
        out.provider = service.getProvider();
        out.service = service;
        out.impl = (T) service.newInstance(param);
        return out;
    }
}
