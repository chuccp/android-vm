package android.telephony;

import java.util.List;
import java.util.concurrent.Executor;

/**
 * 电话状态监听（AOSP android.telephony.PhoneStateListener 公开面；§31.7 C 档
 * lib——SDK 指纹库以它为基类；VM 无电话服务，回调默认空转）。
 */
public class PhoneStateListener {
    public static final int LISTEN_NONE = 0;
    public static final int LISTEN_SERVICE_STATE = 0x00000001;
    public static final int LISTEN_SIGNAL_STRENGTHS = 0x00000002;
    public static final int LISTEN_CALL_STATE = 0x00000004;
    public static final int LISTEN_DATA_CONNECTION_STATE = 0x00000008;
    public static final int LISTEN_DATA_ACTIVITY = 0x00000010;
    public static final int LISTEN_CELL_LOCATION = 0x00000020;
    public static final int LISTEN_CELL_INFO = 0x00000040;

    public PhoneStateListener() {
    }

    public PhoneStateListener(Executor executor) {
    }

    public void onCallStateChanged(int state, String phoneNumber) {
    }

    public void onSignalStrengthsChanged(SignalStrength signalStrength) {
    }

    public void onServiceStateChanged(ServiceState serviceState) {
    }

    public void onDataConnectionStateChanged(int state) {
    }

    public void onDataConnectionStateChanged(int state, int networkType) {
    }

    public void onDataActivity(int direction) {
    }

    public void onCellLocationChanged(CellLocation location) {
    }

    public void onCellInfoChanged(List<CellInfo> cellInfo) {
    }
}
