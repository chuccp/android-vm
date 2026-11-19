package android.media;

/**
 * 音频设备信息（AOSP android.media.AudioDeviceInfo 公开面；§31.7 C 档 lib——
 * VM 无音频设备桥，设备表恒空，本类为类型面骨架）。
 */
public final class AudioDeviceInfo {
    public static final int TYPE_UNKNOWN = 0;
    public static final int TYPE_BUILTIN_EARPIECE = 1;
    public static final int TYPE_BUILTIN_SPEAKER = 2;
    public static final int TYPE_WIRED_HEADSET = 3;
    public static final int TYPE_WIRED_HEADPHONES = 4;
    public static final int TYPE_BLUETOOTH_SCO = 7;
    public static final int TYPE_BLUETOOTH_A2DP = 8;
    public static final int TYPE_HDMI = 9;
    public static final int TYPE_USB_DEVICE = 11;

    private final int mType;

    AudioDeviceInfo(int type) {
        this.mType = type;
    }

    public int getType() {
        return mType;
    }

    public int getId() {
        return 0;
    }

    public String getProductName() {
        return null;
    }

    public boolean isSource() {
        return false;
    }

    public boolean isSink() {
        return true;
    }
}
