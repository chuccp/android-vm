package android.bluetooth;

import java.util.Collections;
import java.util.List;

/**
 * 蓝牙管理服务（AOSP android.bluetooth.BluetoothManager 公开面）。
 * 适配器身份走宿主桥（BluetoothAdapter.getDefaultAdapter 真值探测）；
 * VM 无蓝牙 profile 连接面，连接表恒空。
 */
public final class BluetoothManager {
    public BluetoothAdapter getAdapter() {
        return BluetoothAdapter.getDefaultAdapter();
    }

    public List<BluetoothDevice> getConnectedDevices(int profile) {
        return Collections.emptyList();
    }

    public List<BluetoothDevice> getDevicesMatchingConnectionStates(int[] states) {
        return Collections.emptyList();
    }

    public int getConnectionState(BluetoothDevice device, int profile) {
        return 0;
    }
}
