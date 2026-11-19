package android.telephony;

import com.dexvm.runtime.Host;

/**
 * 电话服务（AOSP android.telephony.TelephonyManager 公开面；§31.7 C 档 lib——
 * 纯 API 面。设备真值经宿主桥接注入：com.dexvm.runtime.Host.get("telephony.*")
 * 由宿主把真实 TelephonyManager 快照推入 VM；无快照回落 AOSP 缺省语义）。
 */
public class TelephonyManager {
    public static final int PHONE_TYPE_NONE = 0;
    public static final int PHONE_TYPE_GSM = 1;
    public static final int PHONE_TYPE_CDMA = 2;
    public static final int PHONE_TYPE_SIP = 3;
    public static final int SIM_STATE_UNKNOWN = 0;
    public static final int SIM_STATE_ABSENT = 1;
    public static final int SIM_STATE_PIN_REQUIRED = 2;
    public static final int SIM_STATE_PUK_REQUIRED = 3;
    public static final int SIM_STATE_NETWORK_LOCKED = 4;
    public static final int SIM_STATE_READY = 5;
    public static final int SIM_STATE_NOT_READY = 6;
    public static final int SIM_STATE_PERM_DISABLED = 7;
    public static final int SIM_STATE_CARD_IO_ERROR = 8;
    public static final int NETWORK_TYPE_UNKNOWN = 0;
    public static final int NETWORK_TYPE_GPRS = 1;
    public static final int NETWORK_TYPE_EDGE = 2;
    public static final int NETWORK_TYPE_UMTS = 3;
    public static final int NETWORK_TYPE_CDMA = 4;
    public static final int NETWORK_TYPE_EVDO_0 = 5;
    public static final int NETWORK_TYPE_EVDO_A = 6;
    public static final int NETWORK_TYPE_1xRTT = 7;
    public static final int NETWORK_TYPE_HSDPA = 8;
    public static final int NETWORK_TYPE_HSUPA = 9;
    public static final int NETWORK_TYPE_HSPA = 10;
    public static final int NETWORK_TYPE_IDEN = 11;
    public static final int NETWORK_TYPE_EVDO_B = 12;
    public static final int NETWORK_TYPE_LTE = 13;
    public static final int NETWORK_TYPE_EHRPD = 14;
    public static final int NETWORK_TYPE_HSPAP = 15;
    public static final int NETWORK_TYPE_GSM = 16;
    public static final int NETWORK_TYPE_TD_SCDMA = 17;
    public static final int NETWORK_TYPE_IWLAN = 18;
    public static final int NETWORK_TYPE_NR = 20;
    public static final int CALL_STATE_IDLE = 0;
    public static final int CALL_STATE_RINGING = 1;
    public static final int CALL_STATE_OFFHOOK = 2;
    public static final int DATA_CONNECTED = 2;
    public static final int DATA_DISCONNECTED = 0;
    public static final int DATA_CONNECTING = 1;
    public static final int DATA_SUSPENDED = 3;

    TelephonyManager() {
    }

    private static String s(String key, String def) {
        String v = Host.get("telephony." + key);
        return v != null ? v : def;
    }

    private static int i(String key, int def) {
        String v = Host.get("telephony." + key);
        if (v == null || v.isEmpty()) return def;
        try {
            return Integer.parseInt(v);
        } catch (NumberFormatException e) {
            return def;
        }
    }

    private static boolean b(String key, boolean def) {
        String v = Host.get("telephony." + key);
        if (v == null || v.isEmpty()) return def;
        return "1".equals(v) || "true".equalsIgnoreCase(v);
    }

    public String getNetworkOperator() {
        return s("networkOperator", "");
    }

    public String getNetworkOperatorName() {
        return s("networkOperatorName", "");
    }

    public String getSimOperator() {
        return s("simOperator", "");
    }

    public String getSimOperatorName() {
        return s("simOperatorName", "");
    }

    public String getSimCountryIso() {
        return s("simCountryIso", "");
    }

    public String getNetworkCountryIso() {
        return s("networkCountryIso", "");
    }

    public int getPhoneType() {
        return i("phoneType", PHONE_TYPE_GSM);
    }

    public int getSimState() {
        return i("simState", SIM_STATE_READY);
    }

    public int getNetworkType() {
        return i("networkType", NETWORK_TYPE_LTE);
    }

    public int getDataState() {
        return i("dataState", DATA_CONNECTED);
    }

    public int getDataActivity() {
        return 0;
    }

    public int getCallState() {
        return CALL_STATE_IDLE;
    }

    public boolean isNetworkRoaming() {
        return b("networkRoaming", false);
    }

    public String getDeviceSoftwareVersion() {
        return s("deviceSoftwareVersion", "");
    }

    public String getDeviceId() {
        return s("deviceId", null);
    }

    public String getImei() {
        return s("imei", null);
    }

    public String getMeid() {
        return s("meid", null);
    }

    public String getSubscriberId() {
        return s("subscriberId", null);
    }

    public String getLine1Number() {
        return s("line1Number", null);
    }

    public String getSimSerialNumber() {
        return s("simSerialNumber", null);
    }

    public int getPhoneCount() {
        return i("phoneCount", 1);
    }

    public CellLocation getCellLocation() {
        return null;
    }

    public java.util.List<CellInfo> getAllCellInfo() {
        return new java.util.ArrayList<CellInfo>();
    }

    public SignalStrength getSignalStrength() {
        return null;
    }

    public void listen(PhoneStateListener listener, int events) {
    }

    public boolean isDataEnabled() {
        return true;
    }

    public int getDataNetworkType() {
        return getNetworkType();
    }

    public int getVoiceNetworkType() {
        return getNetworkType();
    }
}
