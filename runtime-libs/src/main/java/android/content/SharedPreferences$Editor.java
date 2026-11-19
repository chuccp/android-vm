package android.content;

import java.util.Set;

/**
 * SharedPreferences 编辑器接口（Android 公开面；putXXX 链式自返）。
 *
 * <p>{@code commit()} 同步生效并返回 true；{@code apply()} 内存立即生效、
 * listener 回调后置（下次 commit/apply 前落下）——xml 落盘后置。
 */
public interface SharedPreferences$Editor {
    /** null 值等价 {@link #remove}（AOSP 语义）。 */
    SharedPreferences$Editor putString(String key, String value);

    SharedPreferences$Editor putStringSet(String key, Set<String> values);

    SharedPreferences$Editor putInt(String key, int value);

    SharedPreferences$Editor putLong(String key, long value);

    SharedPreferences$Editor putFloat(String key, float value);

    SharedPreferences$Editor putBoolean(String key, boolean value);

    SharedPreferences$Editor remove(String key);

    SharedPreferences$Editor clear();

    boolean commit();

    void apply();
}
