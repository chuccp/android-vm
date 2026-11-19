package android.bluetooth;

/**
 * 蓝牙设备（AOSP android.bluetooth.BluetoothDevice 公开面；实例来自
 * BluetoothAdapter.getBondedDevices() 的宿主快照，不伪造设备身份）。
 */
public final class BluetoothDevice {
    public static final int BOND_NONE = 10;
    public static final int BOND_BONDING = 11;
    public static final int BOND_BONDED = 12;

    public static final int DEVICE_TYPE_UNKNOWN = 0;
    public static final int DEVICE_TYPE_CLASSIC = 1;
    public static final int DEVICE_TYPE_LE = 2;
    public static final int DEVICE_TYPE_DUAL = 3;

    public String name;
    public String address;
    public int bluetoothClassMajor;
    public int bondState = BOND_NONE;
    public int type = DEVICE_TYPE_UNKNOWN;

    BluetoothDevice() {
    }

    public String getName() {
        return name;
    }

    public String getAddress() {
        return address;
    }

    public int getBondState() {
        return bondState;
    }

    public int getType() {
        return type;
    }

    public BluetoothClass getBluetoothClass() {
        return null;
    }

    @Override
    public boolean equals(Object o) {
        if (!(o instanceof BluetoothDevice)) return false;
        String a = ((BluetoothDevice) o).address;
        return address == null ? a == null : address.equals(a);
    }

    @Override
    public int hashCode() {
        return address == null ? 0 : address.hashCode();
    }

    @Override
    public String toString() {
        return "BluetoothDevice{name=" + name + ", address=" + address + "}";
    }
}
