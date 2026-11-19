package sun.security.jca;

import java.security.Provider;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;

/**
 * Provider 有序表（§31.7 C 档 lib：OpenJDK 公开面形态——providers() 消费口）。
 *
 * <p>OpenJDK ProviderList.fromSecurityProperties（java.security 配置面）
 * 后置：Security.&#60;clinit&#62; 依赖文件 IO 链，本表先按显式构造形态承接
 * SecureRandom.getDefaultPRNG 的 providers() 迭代。
 */
public final class ProviderList implements Iterable<Provider> {

    private final List<Provider> list;

    private ProviderList(List<Provider> providers) {
        this.list = Collections.unmodifiableList(new ArrayList<>(providers));
    }

    public static ProviderList of(Provider... providers) {
        List<Provider> out = new ArrayList<>();
        if (providers != null) {
            for (Provider p : providers) {
                if (p != null) {
                    out.add(p);
                }
            }
        }
        return new ProviderList(out);
    }

    /** 优先级序 provider 表（不可变视图）。 */
    public List<Provider> providers() {
        return list;
    }

    public Provider getProvider(String name) {
        if (name == null) {
            return null;
        }
        for (Provider p : list) {
            if (name.equals(p.getName())) {
                return p;
            }
        }
        return null;
    }

    public int size() {
        return list.size();
    }

    @Override
    public Iterator<Provider> iterator() {
        return list.iterator();
    }
}
