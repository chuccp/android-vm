package android.content;

import java.util.Map;
import java.util.Set;

/**
 * 键值偏好接口（Android 公开面；§31.7 B 档 lib 形态）。
 *
 * <p>实现见 {@link SharedPreferencesImpl}（VM 侧内存 Map 存储，per-name 单例；
 * xml 落盘后置）。本接口为纯 Java 面（framework.jar 形态），零 native 面。
 */
public interface SharedPreferences {
    /** 全量键值快照（值为 String/Set/Integer/Long/Float/Boolean 装箱形态）。 */
    Map<String, ?> getAll();

    String getString(String key, String defValue);

    Set<String> getStringSet(String key, Set<String> defValues);

    int getInt(String key, int defValue);

    long getLong(String key, long defValue);

    float getFloat(String key, float defValue);

    boolean getBoolean(String key, boolean defValue);

    boolean contains(String key);

    SharedPreferences$Editor edit();

    void registerOnSharedPreferenceChangeListener(
            SharedPreferences$OnSharedPreferenceChangeListener listener);

    void unregisterOnSharedPreferenceChangeListener(
            SharedPreferences$OnSharedPreferenceChangeListener listener);
}
