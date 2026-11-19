package android.provider;

import android.content.ContentResolver;

/**
 * 系统设置面（AOSP android.provider.Settings 公开面；§31.7 C 档 lib——VM 无系统
 * 设置服务：读返回缺省值、写空转（PUT 成功语义），壳/SDK 判定链走缺省分支）。
 */
public class Settings {
    public static final String ACTION_SETTINGS = "android.settings.SETTINGS";

    public static class SettingNotFoundException extends Exception {
        public SettingNotFoundException(String msg) {
            super(msg);
        }
    }

    public static class Global {
        public static String getString(ContentResolver resolver, String name) {
            return null;
        }

        public static int getInt(ContentResolver resolver, String name, int def) {
            return def;
        }

        public static long getLong(ContentResolver resolver, String name, long def) {
            return def;
        }

        public static float getFloat(ContentResolver resolver, String name, float def) {
            return def;
        }

        public static boolean getInt(ContentResolver resolver, String name, boolean def) {
            return def;
        }

        public static boolean putString(ContentResolver resolver, String name, String value) {
            return true;
        }

        public static boolean putInt(ContentResolver resolver, String name, int value) {
            return true;
        }

        public static boolean putLong(ContentResolver resolver, String name, long value) {
            return true;
        }

        public static String getDefault() {
            return null;
        }
    }

    public static class System extends Global {
    }

    public static class Secure extends Global {
        public static final String ADB_ENABLED = "adb_enabled";
        public static final String ANDROID_ID = "android_id";
        public static final String ENABLED_NOTIFICATION_LISTENERS = "enabled_notification_listeners";
        public static final String ACCESSIBILITY_ENABLED = "accessibility_enabled";

        public static String getString(ContentResolver resolver, String name) {
            return null;
        }

        public static int getInt(ContentResolver resolver, String name, int def) {
            return def;
        }

        public static boolean putString(ContentResolver resolver, String name, String value) {
            return true;
        }

        public static boolean putInt(ContentResolver resolver, String name, int value) {
            return true;
        }

        public static String getDefault() {
            return null;
        }
    }
}
