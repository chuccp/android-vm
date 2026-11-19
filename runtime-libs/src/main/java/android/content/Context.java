package android.content;

import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.content.res.Configuration;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.UserHandle;
import android.view.Display;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Collection;
import java.util.List;
import java.util.concurrent.Executor;
import java.util.function.IntConsumer;

public abstract class Context {
    public static final String ACCESSIBILITY_SERVICE = "accessibility";
    public static final String ACCOUNT_SERVICE = "account";
    public static final String ACTIVITY_SERVICE = "activity";
    public static final String ADVANCED_PROTECTION_SERVICE =
            "advanced_protection";
    public static final String ALARM_SERVICE = "alarm";
    public static final String APPWIDGET_SERVICE = "appwidget";
    public static final String APP_FUNCTION_SERVICE = "app_function";
    public static final String APP_OPS_SERVICE = "appops";
    public static final String APP_SEARCH_SERVICE = "app_search";
    public static final String AUDIO_SERVICE = "audio";
    public static final String AUTOFILL_SERVICE = "autofill";
    public static final String BATTERY_SERVICE = "batterymanager";
    public static final String BIOMETRIC_SERVICE = "biometric";
    public static final String BLOB_STORE_SERVICE = "blob_store";
    public static final String BLUETOOTH_SERVICE = "bluetooth";
    public static final String BUGREPORT_SERVICE = "bugreport";
    public static final String CAMERA_SERVICE = "camera";
    public static final String CAPTIONING_SERVICE = "captioning";
    public static final String CARRIER_CONFIG_SERVICE = "carrier_config";
    public static final String CHOOSER_SERVICE = "chooser";
    public static final String CLIPBOARD_SERVICE = "clipboard";
    public static final String COMPANION_DEVICE_SERVICE = "companiondevice";
    public static final String CONNECTIVITY_DIAGNOSTICS_SERVICE =
            "connectivity_diagnostics";
    public static final String CONNECTIVITY_SERVICE = "connectivity";
    public static final String CONSUMER_IR_SERVICE = "consumer_ir";
    public static final String CONTACT_KEYS_SERVICE = "contact_keys";
    public static final String CREDENTIAL_SERVICE = "credential";
    public static final String CROSS_PROFILE_APPS_SERVICE = "crossprofileapps";
    public static final String DEVICE_LOCK_SERVICE = "device_lock";
    public static final String DEVICE_POLICY_SERVICE = "device_policy";
    public static final String DISPLAY_HASH_SERVICE = "display_hash";
    public static final String DISPLAY_SERVICE = "display";
    public static final String DOMAIN_VERIFICATION_SERVICE =
            "domain_verification";
    public static final String DOWNLOAD_SERVICE = "download";
    public static final String DROPBOX_SERVICE = "dropbox";
    public static final String EUICC_SERVICE = "euicc";
    public static final String FILE_INTEGRITY_SERVICE = "file_integrity";
    public static final String FILE_SERVICE = "file";
    public static final String GAME_SERVICE = "game";
    public static final String GRAMMATICAL_INFLECTION_SERVICE =
            "grammatical_inflection";
    public static final String HARDWARE_PROPERTIES_SERVICE =
            "hardware_properties";
    public static final String HEALTHCONNECT_SERVICE = "healthconnect";
    public static final String INPUT_METHOD_SERVICE = "input_method";
    public static final String INPUT_SERVICE = "input";
    public static final String IPSEC_SERVICE = "ipsec";
    public static final String JOB_SCHEDULER_SERVICE = "jobscheduler";
    public static final String KEYGUARD_SERVICE = "keyguard";
    public static final String KEYSTORE_SERVICE = "keystore";
    public static final String LAUNCHER_APPS_SERVICE = "launcherapps";
    public static final String LAYOUT_INFLATER_SERVICE = "layout_inflater";
    public static final String LOCALE_SERVICE = "locale";
    public static final String LOCATION_SERVICE = "location";
    public static final String MEDIA_COMMUNICATION_SERVICE =
            "media_communication";
    public static final String MEDIA_METRICS_SERVICE = "media_metrics";
    public static final String MEDIA_PROJECTION_SERVICE = "media_projection";
    public static final String MEDIA_QUALITY_SERVICE = "media_quality";
    public static final String MEDIA_ROUTER_SERVICE = "media_router";
    public static final String MEDIA_SESSION_SERVICE = "media_session";
    public static final String MIDI_SERVICE = "midi";
    public static final String NETWORK_STATS_SERVICE = "netstats";
    public static final String NFC_SERVICE = "nfc";
    public static final String NOTIFICATION_SERVICE = "notification";
    public static final String NPU_SERVICE = "npu";
    public static final String NSD_SERVICE = "servicediscovery";
    public static final String OVERLAY_SERVICE = "overlay";
    public static final String PCC_SANDBOX_SERVICE = "pcc_sandbox";
    public static final String PEOPLE_SERVICE = "people";
    public static final String PERFORMANCE_HINT_SERVICE = "performance_hint";
    public static final String PERSISTENT_DATA_BLOCK_SERVICE =
            "persistent_data_block";
    public static final String POWER_SERVICE = "power";
    public static final String PRINT_SERVICE = "print";
    public static final String PROFILING_SERVICE = "profiling";
    public static final String RESTRICTIONS_SERVICE = "restrictions";
    public static final String ROLE_SERVICE = "role";
    public static final String SATELLITE_SERVICE = "satellite";
    public static final String SEARCH_SERVICE = "search";
    public static final String SECURITY_STATE_SERVICE = "security_state";
    public static final String SENSOR_SERVICE = "sensor";
    public static final String SERIAL_SERVICE = "serial";
    public static final String SHORTCUT_SERVICE = "shortcut";
    public static final String STATUS_BAR_SERVICE = "statusbar";
    public static final String STORAGE_SERVICE = "storage";
    public static final String STORAGE_STATS_SERVICE = "storagestats";
    public static final String SYSTEM_HEALTH_SERVICE = "systemhealth";
    public static final String TELECOM_SERVICE = "telecom";
    public static final String TELEPHONY_IMS_SERVICE = "telephony_ims";
    public static final String TELEPHONY_PHONE_NUMBER_SERVICE =
            "telephony_phone_number";
    public static final String TELEPHONY_SERVICE = "phone";
    public static final String TELEPHONY_SUBSCRIPTION_SERVICE =
            "telephony_subscription_service";
    public static final String TETHERING_SERVICE = "tethering";
    public static final String TEXT_CLASSIFICATION_SERVICE =
            "textclassification";
    public static final String TEXT_SERVICES_MANAGER_SERVICE = "textservices";
    public static final String TV_AD_SERVICE = "tv_ad";
    public static final String TV_INPUT_SERVICE = "tv_input";
    public static final String TV_INTERACTIVE_APP_SERVICE =
            "tv_interactive_app";
    public static final String UI_MODE_SERVICE = "uimode";
    public static final String USAGE_STATS_SERVICE = "usagestats";
    public static final String USB_SERVICE = "usb";
    public static final String USER_SERVICE = "user";
    public static final String VIBRATOR_MANAGER_SERVICE = "vibrator_manager";
    public static final String VIBRATOR_SERVICE = "vibrator";
    public static final String VIRTUAL_DEVICE_SERVICE = "virtualdevice";
    public static final String VPN_MANAGEMENT_SERVICE = "vpn_management";
    public static final String WALLPAPER_SERVICE = "wallpaper";
    public static final String WEB_APP_SERVICE = "web_app";
    public static final String WIFI_AWARE_SERVICE = "wifiaware";
    public static final String WIFI_P2P_SERVICE = "wifip2p";
    public static final String WIFI_RTT_RANGING_SERVICE = "wifirtt";
    public static final String WIFI_SERVICE = "wifi";
    public static final String WINDOW_SERVICE = "window";

    public static final int BIND_AUTO_CREATE = 1;
    public static final int BIND_DEBUG_UNBIND = 2;
    public static final int BIND_NOT_FOREGROUND = 4;
    public static final int BIND_ABOVE_CLIENT = 8;
    public static final int BIND_ALLOW_OOM_MANAGEMENT = 16;
    public static final int BIND_WAIVE_PRIORITY = 32;
    public static final int BIND_IMPORTANT = 64;
    public static final int BIND_ADJUST_WITH_ACTIVITY = 128;
    public static final int BIND_NOT_PERCEPTIBLE = 256;
    public static final int BIND_ALLOW_ACTIVITY_STARTS = 512;
    public static final int BIND_INCLUDE_CAPABILITIES = 4096;
    public static final int BIND_SHARED_ISOLATED_PROCESS = 8192;
    public static final int BIND_PACKAGE_ISOLATED_PROCESS = 16384;
    public static final int BIND_EXTERNAL_SERVICE = 0x80000000;
    public static final long BIND_EXTERNAL_SERVICE_LONG =
            0x4000000000000000L;

    public static final int CONTEXT_INCLUDE_CODE = 1;
    public static final int CONTEXT_IGNORE_SECURITY = 2;
    public static final int CONTEXT_RESTRICTED = 4;

    public static final int MODE_PRIVATE = 0;
    public static final int MODE_WORLD_READABLE = 1;
    public static final int MODE_WORLD_WRITEABLE = 2;
    public static final int MODE_MULTI_PROCESS = 4;
    public static final int MODE_ENABLE_WRITE_AHEAD_LOGGING = 8;
    public static final int MODE_NO_LOCALIZED_COLLATORS = 16;
    public static final int MODE_APPEND = 32768;

    public static final int RECEIVER_VISIBLE_TO_INSTANT_APPS = 1;
    public static final int RECEIVER_EXPORTED = 2;
    public static final int RECEIVER_NOT_EXPORTED = 4;

    public static final int DEVICE_ID_DEFAULT = 0;
    public static final int DEVICE_ID_INVALID = -1;

    public static final class BindServiceFlags {
        private final long value;

        private BindServiceFlags(long value) {
            this.value = value;
        }

        public static native BindServiceFlags of(long flags);
    }

    public static final class UpdateBindingParams {
        private ServiceConnection connection;
        private BindServiceFlags flags;

        private UpdateBindingParams() {
        }

        public ServiceConnection getConnection() {
            return connection;
        }

        public BindServiceFlags getFlags() {
            return flags;
        }

        public boolean isRebind() {
            return flags != null;
        }

        public boolean isUnbind() {
            return flags == null;
        }

        public void setRebind(BindServiceFlags flags) {
            this.flags = flags;
        }

        public void setUnbind() {
            this.flags = null;
        }
    }

    public Context() {
    }

    public native boolean bindService(
            Intent intent,
            ServiceConnection connection,
            int flags);

    public boolean bindService(
            Intent intent,
            int flags,
            Executor executor,
            ServiceConnection connection) {
        return bindService(intent, connection, flags);
    }

    public boolean bindIsolatedService(
            Intent intent,
            int flags,
            String instanceName,
            Executor executor,
            ServiceConnection connection) {
        return bindService(intent, connection, flags);
    }

    public boolean bindIsolatedService(
            Intent intent,
            BindServiceFlags flags,
            String instanceName,
            Executor executor,
            ServiceConnection connection) {
        return bindService(intent, connection, 0);
    }

    public boolean bindService(
            Intent intent,
            BindServiceFlags flags,
            Executor executor,
            ServiceConnection connection) {
        return bindService(intent, connection, 0);
    }

    public boolean bindService(
            Intent intent,
            ServiceConnection connection,
            BindServiceFlags flags) {
        return bindService(intent, connection, 0);
    }

    public boolean bindServiceAsUser(
            Intent intent,
            ServiceConnection connection,
            int flags,
            UserHandle user) {
        return bindService(intent, connection, flags);
    }

    public boolean bindServiceAsUser(
            Intent intent,
            ServiceConnection connection,
            BindServiceFlags flags,
            UserHandle user) {
        return bindService(intent, connection, 0);
    }

    public native int checkCallingOrSelfPermission(String permission);

    public native int checkCallingOrSelfUriPermission(Uri uri, int modeFlags);

    public int[] checkCallingOrSelfUriPermissions(
            List<Uri> uris,
            int modeFlags) {
        int[] result = new int[uris.size()];
        for (int index = 0; index < result.length; index++) {
            result[index] = checkCallingOrSelfUriPermission(
                    uris.get(index),
                    modeFlags);
        }
        return result;
    }

    public native int checkCallingPermission(String permission);

    public native int checkCallingUriPermission(Uri uri, int modeFlags);

    public int[] checkCallingUriPermissions(
            List<Uri> uris,
            int modeFlags) {
        int[] result = new int[uris.size()];
        for (int index = 0; index < result.length; index++) {
            result[index] = checkCallingUriPermission(
                    uris.get(index),
                    modeFlags);
        }
        return result;
    }

    public native int checkPermission(String permission, int pid, int uid);

    public native int checkSelfPermission(String permission);

    public native int checkUriPermission(
            Uri uri,
            int pid,
            int uid,
            int modeFlags);

    public native int checkUriPermission(
            Uri uri,
            String readPermission,
            String writePermission,
            int pid,
            int uid,
            int modeFlags);

    public native int checkContentUriPermissionFull(
            Uri uri,
            int pid,
            int uid,
            int modeFlags);

    public int[] checkUriPermissions(
            List<Uri> uris,
            int pid,
            int uid,
            int modeFlags) {
        int[] result = new int[uris.size()];
        for (int index = 0; index < result.length; index++) {
            result[index] = checkUriPermission(
                    uris.get(index),
                    pid,
                    uid,
                    modeFlags);
        }
        return result;
    }

    public native void clearWallpaper() throws IOException;

    public Context createAttributionContext(String attributionTag) {
        return nativeCreateContext("attribution", attributionTag);
    }

    public native Context createConfigurationContext(
            Configuration overrideConfiguration);

    public native Context createDeviceProtectedStorageContext();

    public native Context createDisplayContext(Display display);

    public native Context createContext(ContextParams params);

    public native Context createContextForSplit(String splitName)
            throws PackageManager.NameNotFoundException;

    public Context createDeviceContext(int deviceId) {
        return nativeCreateContext("device", Integer.toString(deviceId));
    }

    public native Context createWindowContext(
            Display display,
            int type,
            Bundle options);

    public native Context createWindowContext(int type, Bundle options);

    protected native Context nativeCreateContext(
            String kind,
            String value);

    public native Context createPackageContext(String packageName, int flags)
            throws PackageManager.NameNotFoundException;

    public native String[] databaseList();

    public native boolean deleteDatabase(String name);

    public native boolean deleteFile(String name);

    public native boolean deleteSharedPreferences(String name);

    public native void enforceCallingOrSelfPermission(
            String permission,
            String message);

    public native void enforceCallingOrSelfUriPermission(
            Uri uri,
            int modeFlags,
            String message);

    public native void enforceCallingPermission(
            String permission,
            String message);

    public native void enforceCallingUriPermission(
            Uri uri,
            int modeFlags,
            String message);

    public native void enforcePermission(
            String permission,
            int pid,
            int uid,
            String message);

    public native void enforceUriPermission(
            Uri uri,
            int pid,
            int uid,
            int modeFlags,
            String message);

    public native void enforceUriPermission(
            Uri uri,
            String readPermission,
            String writePermission,
            int pid,
            int uid,
            int modeFlags,
            String message);

    public native String[] fileList();

    public native Context getApplicationContext();

    public native ApplicationInfo getApplicationInfo();

    public native AssetManager getAssets();

    public native File getCacheDir();

    public native ClassLoader getClassLoader();

    public native File getCodeCacheDir();

    public native ContentResolver getContentResolver();

    public native File getDataDir();

    public native File getDatabasePath(String name);

    public native File getDir(String name, int mode);

    public native Display getDisplay();

    public native File getExternalCacheDir();

    public native File[] getExternalCacheDirs();

    public native File getExternalFilesDir(String type);

    public native File[] getExternalFilesDirs(String type);

    public native File[] getExternalMediaDirs();

    public native File getFileStreamPath(String name);

    public native File getFilesDir();

    public native Looper getMainLooper();

    public native File getNoBackupFilesDir();

    public native File getObbDir();

    public native File[] getObbDirs();

    public native String getPackageCodePath();

    public native PackageManager getPackageManager();

    public native String getPackageName();

    public native String getPackageResourcePath();

    public native ContextParams getParams();

    public native Resources getResources();

    public native String getAttributionTag();

    public native AttributionSource getAttributionSource();

    public native int getDeviceId();

    public Executor getMainExecutor() {
        return command -> new Handler(getMainLooper()).post(command);
    }

    public native String getOpPackageName();

    public native BindServiceFlags getUpdateableFlags();

    public native SharedPreferences getSharedPreferences(
            String name,
            int mode);

    public native String getString(int resId);

    public native String getString(int resId, Object... formatArgs);

    public native Object getSystemService(String name);

    public native String getSystemServiceName(Class<?> serviceClass);

    public native CharSequence getText(int resId);

    public native Resources.Theme getTheme();

    public native Drawable getWallpaper();

    public native int getWallpaperDesiredMinimumHeight();

    public native int getWallpaperDesiredMinimumWidth();

    public native void grantUriPermission(
            String toPackage,
            Uri uri,
            int modeFlags);

    public native boolean isDeviceProtectedStorage();

    public native boolean isRestricted();

    public native boolean isUiContext();

    public native boolean moveDatabaseFrom(Context sourceContext, String name);

    public native boolean moveSharedPreferencesFrom(
            Context sourceContext,
            String name);

    public native FileInputStream openFileInput(String name)
            throws FileNotFoundException;

    public native FileOutputStream openFileOutput(String name, int mode)
            throws FileNotFoundException;

    public native android.database.sqlite.SQLiteDatabase openOrCreateDatabase(
            String name,
            int mode,
            android.database.sqlite.SQLiteDatabase.CursorFactory factory);

    public native android.database.sqlite.SQLiteDatabase openOrCreateDatabase(
            String name,
            int mode,
            android.database.sqlite.SQLiteDatabase.CursorFactory factory,
            android.database.DatabaseErrorHandler errorHandler);

    public native Drawable peekWallpaper();

    public native void registerComponentCallbacks(
            ComponentCallbacks callbacks);

    public native Intent registerReceiver(
            BroadcastReceiver receiver,
            IntentFilter filter);

    public native Intent registerReceiver(
            BroadcastReceiver receiver,
            IntentFilter filter,
            int flags);

    public native Intent registerReceiver(
            BroadcastReceiver receiver,
            IntentFilter filter,
            String broadcastPermission,
            Handler scheduler);

    public native Intent registerReceiver(
            BroadcastReceiver receiver,
            IntentFilter filter,
            String broadcastPermission,
            Handler scheduler,
            int flags);

    public native void registerDeviceIdChangeListener(
            Executor executor,
            IntConsumer listener);

    public native void rebindService(
            ServiceConnection connection,
            BindServiceFlags flags);

    public native void removeStickyBroadcast(Intent intent);

    public native void removeStickyBroadcastAsUser(
            Intent intent,
            UserHandle user);

    public native void revokeUriPermission(Uri uri, int modeFlags);

    public native void revokeUriPermission(
            String targetPackage,
            Uri uri,
            int modeFlags);

    public native void revokeSelfPermissionOnKill(String permission);

    public native void revokeSelfPermissionsOnKill(
            Collection<String> permissions);

    public native void sendBroadcast(Intent intent);

    public native void sendBroadcast(Intent intent, String receiverPermission);

    public native void sendBroadcast(
            Intent intent,
            String receiverPermission,
            Bundle options);

    public native void sendBroadcastAsUser(
            Intent intent,
            UserHandle user);

    public native void sendBroadcastAsUser(
            Intent intent,
            UserHandle user,
            String receiverPermission);

    public void sendBroadcastWithMultiplePermissions(
            Intent intent,
            String[] receiverPermissions) {
        sendBroadcast(intent);
    }

    public native void sendOrderedBroadcast(
            Intent intent,
            String receiverPermission);

    public native void sendOrderedBroadcast(
            Intent intent,
            String receiverPermission,
            Bundle options);

    public native void sendOrderedBroadcastAsUser(
            Intent intent,
            UserHandle user,
            String receiverPermission,
            BroadcastReceiver resultReceiver,
            Handler scheduler,
            int initialCode,
            String initialData,
            Bundle initialExtras);

    public native void sendStickyBroadcast(Intent intent);

    public native void sendStickyBroadcast(Intent intent, Bundle options);

    public native void sendStickyBroadcastAsUser(
            Intent intent,
            UserHandle user);

    public native void sendStickyOrderedBroadcast(
            Intent intent,
            BroadcastReceiver resultReceiver,
            Handler scheduler,
            int initialCode,
            String initialData,
            Bundle initialExtras);

    public native void sendStickyOrderedBroadcastAsUser(
            Intent intent,
            UserHandle user,
            BroadcastReceiver resultReceiver,
            Handler scheduler,
            int initialCode,
            String initialData,
            Bundle initialExtras);

    public native void setTheme(int resId);

    public native void setWallpaper(Bitmap bitmap) throws IOException;

    public native void setWallpaper(InputStream data) throws IOException;

    public native void startActivities(Intent[] intents);

    public native void startActivities(Intent[] intents, Bundle options);

    public native void startActivity(Intent intent);

    public native void startActivity(Intent intent, Bundle options);

    public native boolean startInstrumentation(
            ComponentName className,
            String profileFile,
            Bundle arguments);

    public native ComponentName startForegroundService(Intent service);

    public native void startIntentSender(
            android.content.IntentSender intent,
            Intent fillInIntent,
            int flagsMask,
            int flagsValues,
            int extraFlags)
            throws android.content.IntentSender.SendIntentException;

    public native void startIntentSender(
            android.content.IntentSender intent,
            Intent fillInIntent,
            int flagsMask,
            int flagsValues,
            int extraFlags,
            Bundle options)
            throws android.content.IntentSender.SendIntentException;

    public native ComponentName startService(Intent service);

    public native boolean stopService(Intent service);

    public native void unbindService(ServiceConnection connection);

    public native void unregisterComponentCallbacks(
            ComponentCallbacks callbacks);

    public native void unregisterDeviceIdChangeListener(
            IntConsumer listener);

    public native void unregisterReceiver(BroadcastReceiver receiver);

    public native void updateServiceBindings(
            Collection<UpdateBindingParams> params);

    public native void updateServiceGroup(
            ServiceConnection connection,
            int group,
            int importance);

    public native int getColor(int resId);

    public native android.content.res.ColorStateList getColorStateList(
            int resId);

    public native Drawable getDrawable(int resId);

    public native android.content.res.TypedArray obtainStyledAttributes(
            int[] attrs);

    public native android.content.res.TypedArray obtainStyledAttributes(
            int resId,
            int[] attrs);

    public native android.content.res.TypedArray obtainStyledAttributes(
            android.util.AttributeSet set,
            int[] attrs);

    public native android.content.res.TypedArray obtainStyledAttributes(
            android.util.AttributeSet set,
            int[] attrs,
            int defStyleAttr,
            int defStyleRes);

    public final <T> T getSystemService(Class<T> serviceClass) {
        if (serviceClass == null) {
            throw new NullPointerException("serviceClass == null");
        }
        String name = getSystemServiceName(serviceClass);
        @SuppressWarnings("unchecked")
        T service = (T) getSystemService(name);
        return service;
    }
}
