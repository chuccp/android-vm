package android.preference;

import android.content.Context;
import android.content.SharedPreferences;

// android.preference.PreferenceManager（framework POJO/纯逻辑面，§31.7 C 档
// lib：与 SharedPreferences 同归「逻辑面 lib Java」）：AOSP 默认
// SharedPreferences 命名/模式面（getDefaultSharedPreferences 及实例名/模式
// 管理）。Preference 屏管理/资源膨胀面（PreferenceScreen/PreferenceFragment/
// setDefaultValues XML 膨胀链）后置——需要 Preference 屏机器时按缺口补。
public class PreferenceManager {

    /** AOSP 契约：setDefaultValues 已灌标记键。 */
    public static final String KEY_HAS_SET_DEFAULT_VALUES = "has_set_default_values";

    private static final int FIRST_REQUEST_CODE = 100;

    private final Context mContext;
    private SharedPreferences mSharedPreferences;
    private String mSharedPreferencesName;
    private int mSharedPreferencesMode;

    public PreferenceManager(Context context) {
        this(context, FIRST_REQUEST_CODE);
    }

    PreferenceManager(Context context, int firstRequestCode) {
        mContext = context;
        mSharedPreferencesName = getDefaultSharedPreferencesName(context);
        mSharedPreferencesMode = getDefaultSharedPreferencesMode();
    }

    public static SharedPreferences getDefaultSharedPreferences(Context context) {
        return context.getSharedPreferences(getDefaultSharedPreferencesName(context),
                getDefaultSharedPreferencesMode());
    }

    private static String getDefaultSharedPreferencesName(Context context) {
        return context.getPackageName() + "_preferences";
    }

    private static int getDefaultSharedPreferencesMode() {
        return Context.MODE_PRIVATE;
    }

    public SharedPreferences getSharedPreferences() {
        if (mSharedPreferences == null) {
            mSharedPreferences = mContext.getSharedPreferences(mSharedPreferencesName,
                    mSharedPreferencesMode);
        }
        return mSharedPreferences;
    }

    public void setSharedPreferencesName(String sharedPreferencesName) {
        mSharedPreferencesName = sharedPreferencesName;
        mSharedPreferences = null;
    }

    public void setSharedPreferencesMode(int sharedPreferencesMode) {
        mSharedPreferencesMode = sharedPreferencesMode;
        mSharedPreferences = null;
    }
}
