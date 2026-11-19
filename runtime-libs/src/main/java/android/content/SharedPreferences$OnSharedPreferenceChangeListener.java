package android.content;

/**
 * 键值变更回调接口（Android 公开面骨架）。
 * SharedPreferencesImpl 在 commit（同步）/ apply（后置）时按变更键序回调。
 */
public interface SharedPreferences$OnSharedPreferenceChangeListener {
    void onSharedPreferenceChanged(SharedPreferences sharedPreferences, String key);
}
