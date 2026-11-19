package android.media;

/**
 * 音频服务（AOSP android.media.AudioManager 公开面；§31.7 C 档 lib——VM 无音频
 * 播放桥，收口为可调用面：设备表空、音量读写本地字段）。
 */
public class AudioManager {
    public static final String ACTION_AUDIO_BECOMING_NOISY =
            "android.media.AUDIO_BECOMING_NOISY";
    public static final int GET_DEVICES_ALL = 3;
    public static final int GET_DEVICES_INPUTS = 1;
    public static final int GET_DEVICES_OUTPUTS = 2;
    public static final int STREAM_MUSIC = 3;
    public static final int STREAM_VOICE_CALL = 0;
    public static final int STREAM_SYSTEM = 1;
    public static final int STREAM_RING = 2;
    public static final int STREAM_ALARM = 4;
    public static final int STREAM_NOTIFICATION = 5;
    public static final int AUDIOFOCUS_GAIN = 1;
    public static final int AUDIOFOCUS_LOSS = -1;
    public static final int AUDIOFOCUS_LOSS_TRANSIENT = -2;
    public static final int AUDIOFOCUS_REQUEST_GRANTED = 1;
    public static final int AUDIOFOCUS_REQUEST_FAILED = 0;

    private final int[] mVolumes = new int[16];

    public AudioManager() {
        for (int i = 0; i < mVolumes.length; i++) mVolumes[i] = 8;
    }

    public AudioDeviceInfo[] getDevices(int flags) {
        return new AudioDeviceInfo[0];
    }

    public int getStreamVolume(int streamType) {
        return streamType >= 0 && streamType < mVolumes.length ? mVolumes[streamType] : 0;
    }

    public int getStreamMaxVolume(int streamType) {
        return 15;
    }

    public void setStreamVolume(int streamType, int index, int flags) {
        if (streamType >= 0 && streamType < mVolumes.length) mVolumes[streamType] = index;
    }

    public boolean isMusicActive() {
        return false;
    }

    public boolean isWiredHeadsetOn() {
        return false;
    }

    public boolean isSpeakerphoneOn() {
        return true;
    }

    public void setSpeakerphoneOn(boolean on) {
    }

    public int requestAudioFocus(Object listener, int streamType, int durationHint) {
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    public int abandonAudioFocus(Object listener) {
        return AUDIOFOCUS_REQUEST_GRANTED;
    }

    public int getMode() {
        return 0;
    }

    public void setMode(int mode) {
    }

    /** AOSP AudioManager.OnAudioFocusChangeListener（播放器链接面）。 */
    public interface OnAudioFocusChangeListener {
        void onAudioFocusChange(int focusChange);
    }
}
