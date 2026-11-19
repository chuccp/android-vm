package android.bluetooth;

import com.dexvm.runtime.Host;

import java.util.HashSet;
import java.util.Set;

/**
 * 蓝牙适配器（AOSP android.bluetooth.BluetoothAdapter 公开面）。
 *
 * <p>宿主桥接：宿主启动时把真实 BluetoothAdapter 状态快照推入 VM
 * （com.dexvm.runtime.Host 的 bluetooth.* 键）。本类只读取宿主真值：
 * 宿主无适配器 -> getDefaultAdapter() 返回 null（AOSP 语义），有适配器时
 * isEnabled/getName/getAddress/getBondedDevices 均为宿主实际值。
 */
public final class BluetoothAdapter {
    public static final String ACTION_REQUEST_ENABLE =
            "android.bluetooth.adapter.action.REQUEST_ENABLE";
    public static final String ACTION_STATE_CHANGED =
            "android.bluetooth.adapter.action.STATE_CHANGED";
    public static final String EXTRA_STATE = "android.bluetooth.adapter.extra.STATE";

    public static final int STATE_OFF = 10;
    public static final int STATE_TURNING_ON = 11;
    public static final int STATE_ON = 12;
    public static final int STATE_TURNING_OFF = 13;

    public static final int ERROR = Integer.MIN_VALUE;

    private static BluetoothAdapter sAdapter;
    private static boolean sProbed;

    private BluetoothAdapter() {
    }

    /** 宿主真值探测：无适配器返回 null（与真机无蓝牙硬件同语义）。 */
    public static synchronized BluetoothAdapter getDefaultAdapter() {
        if (!sProbed) {
            sProbed = true;
            if ("1".equals(Host.get("bluetooth.available"))) {
                sAdapter = new BluetoothAdapter();
            }
        }
        return sAdapter;
    }

    public boolean isEnabled() {
        return "1".equals(Host.get("bluetooth.enabled"));
    }

    public int getState() {
        return isEnabled() ? STATE_ON : STATE_OFF;
    }

    public String getName() {
        return Host.get("bluetooth.name");
    }

    public String getAddress() {
        return Host.get("bluetooth.address");
    }

    /** 已配对设备（宿主快照 "name|address|majorClass;..." 解析）。 */
    public Set<BluetoothDevice> getBondedDevices() {
        Set<BluetoothDevice> out = new HashSet<>();
        String encoded = Host.get("bluetooth.devices");
        if (encoded == null || encoded.isEmpty()) return out;
        for (String item : encoded.split(";")) {
            if (item.isEmpty()) continue;
            String[] f = item.split("\\|", -1);
            if (f.length < 3) continue;
            BluetoothDevice dev = new BluetoothDevice();
            dev.name = f[0];
            dev.address = f[1];
            try {
                dev.bluetoothClassMajor = Integer.parseInt(f[2]);
            } catch (Throwable ignored) {
            }
            dev.bondState = BluetoothDevice.BOND_BONDED;
            out.add(dev);
        }
        return out;
    }

    public boolean isDiscovering() {
        return false;
    }

    public boolean startDiscovery() {
        return false;
    }

    public boolean cancelDiscovery() {
        return true;
    }
}
