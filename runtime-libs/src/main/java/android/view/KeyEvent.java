package android.view;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * 按键事件（AOSP android.view.KeyEvent 公开面；§31.7 C 档 lib POJO）。
 */
public class KeyEvent implements Parcelable {
    public static final int ACTION_DOWN = 0;
    public static final int ACTION_UP = 1;
    public static final int ACTION_MULTIPLE = 2;

    public static final int KEYCODE_UNKNOWN = 0;
    public static final int KEYCODE_SOFT_LEFT = 1;
    public static final int KEYCODE_HOME = 3;
    public static final int KEYCODE_BACK = 4;
    public static final int KEYCODE_CALL = 5;
    public static final int KEYCODE_ENDCALL = 6;
    public static final int KEYCODE_VOLUME_UP = 24;
    public static final int KEYCODE_VOLUME_DOWN = 25;
    public static final int KEYCODE_POWER = 26;
    public static final int KEYCODE_CAMERA = 27;
    public static final int KEYCODE_TAB = 61;
    public static final int KEYCODE_ENTER = 66;
    public static final int KEYCODE_DEL = 67;
    public static final int KEYCODE_SPACE = 62;
    public static final int KEYCODE_MENU = 82;
    public static final int KEYCODE_SEARCH = 84;
    public static final int KEYCODE_MEDIA_PLAY_PAUSE = 85;
    public static final int KEYCODE_MEDIA_STOP = 86;
    public static final int KEYCODE_MEDIA_NEXT = 87;
    public static final int KEYCODE_MEDIA_PREVIOUS = 88;
    public static final int KEYCODE_ESCAPE = 111;
    public static final int KEYCODE_F1 = 131;

    public static final int META_SHIFT_ON = 0x1;
    public static final int META_ALT_ON = 0x2;
    public static final int META_CTRL_ON = 0x1000;
    public static final int META_META_ON = 0x10000;

    private final int mAction;
    private final int mKeyCode;
    private final String mCharacters;
    private final long mDownTime;
    private final long mEventTime;
    private final int mMetaState;
    private final int mRepeatCount;
    private final int mDeviceId = -1;
    private final int mScanCode;
    private final int mFlags;

    public KeyEvent(int action, int code) {
        this(0, 0, action, code, 0);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat) {
        this(downTime, eventTime, action, code, repeat, 0);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat,
                    int metaState) {
        this(downTime, eventTime, action, code, repeat, metaState, 0, 0, 0);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat,
                    int metaState, int deviceId, int scancode, int flags) {
        this(downTime, eventTime, action, code, repeat, metaState, deviceId, scancode, flags,
                0);
    }

    public KeyEvent(long downTime, long eventTime, int action, int code, int repeat,
                    int metaState, int deviceId, int scancode, int flags, int source) {
        this.mDownTime = downTime;
        this.mEventTime = eventTime;
        this.mAction = action;
        this.mKeyCode = code;
        this.mRepeatCount = repeat;
        this.mMetaState = metaState;
        this.mScanCode = scancode;
        this.mFlags = flags;
        this.mCharacters = null;
    }

    public KeyEvent(long downTime, String characters, int deviceId, int flags) {
        this.mDownTime = downTime;
        this.mEventTime = downTime;
        this.mAction = ACTION_MULTIPLE;
        this.mKeyCode = KEYCODE_UNKNOWN;
        this.mCharacters = characters;
        this.mRepeatCount = 0;
        this.mMetaState = 0;
        this.mScanCode = 0;
        this.mFlags = flags;
    }

    public static KeyEvent changeFlags(KeyEvent event, int flags) {
        return event;
    }

    public static KeyEvent changeAction(KeyEvent event, int action) {
        return event;
    }

    public static KeyEvent changeTimeRepeat(KeyEvent event, long downTime, int newRepeat) {
        return event;
    }

    public static KeyEvent changeTimeRepeat(KeyEvent event, long downTime, int newRepeat,
                                            int newFlags) {
        return event;
    }

    public static boolean isModifierKey(int keyCode) {
        return keyCode == KEYCODE_SHIFT_LEFT || keyCode == KEYCODE_SHIFT_RIGHT;
    }

    // 供 isModifierKey 的补充键码（省略全集；按需扩）
    public static final int KEYCODE_SHIFT_LEFT = 59;
    public static final int KEYCODE_SHIFT_RIGHT = 60;

    public final int getAction() {
        return this.mAction;
    }

    public boolean isDown() {
        return this.mAction == ACTION_DOWN;
    }

    public boolean isUp() {
        return this.mAction == ACTION_UP;
    }

    public boolean isLongPress() {
        return getRepeatCount() > 0;
    }

    public final int getKeyCode() {
        return this.mKeyCode;
    }

    public final String getCharacters() {
        return this.mCharacters;
    }

    public final int getScanCode() {
        return this.mScanCode;
    }

    public final int getMetaState() {
        return this.mMetaState;
    }

    public final int getRepeatCount() {
        return this.mRepeatCount;
    }

    public final long getDownTime() {
        return this.mDownTime;
    }

    public final long getEventTime() {
        return this.mEventTime;
    }

    public final int getDeviceId() {
        return this.mDeviceId;
    }

    public final int getSource() {
        return 0x101;  // InputDevice.SOURCE_KEYBOARD
    }

    public final int getFlags() {
        return this.mFlags;
    }

    public boolean isSystem() {
        switch (this.mKeyCode) {
            case KEYCODE_HOME:
            case KEYCODE_BACK:
            case KEYCODE_MENU:
            case KEYCODE_POWER:
            case KEYCODE_VOLUME_UP:
            case KEYCODE_VOLUME_DOWN:
            case KEYCODE_CAMERA:
                return true;
            default:
                return false;
        }
    }

    public char getUnicodeChar() {
        return (char) 0;
    }

    public final boolean dispatch(Callback receiver) {
        return false;
    }

    public final boolean isAltPressed() {
        return (this.mMetaState & META_ALT_ON) != 0;
    }

    public final boolean isShiftPressed() {
        return (this.mMetaState & META_SHIFT_ON) != 0;
    }

    public final boolean isCtrlPressed() {
        return (this.mMetaState & META_CTRL_ON) != 0;
    }

    public final boolean isMetaPressed() {
        return (this.mMetaState & META_META_ON) != 0;
    }

    /** 按键分发回调（AOSP KeyEvent.Callback）。 */
    public interface Callback {
        boolean onKeyDown(int keyCode, KeyEvent event);

        boolean onKeyLongPress(int keyCode, KeyEvent event);

        boolean onKeyUp(int keyCode, KeyEvent event);

        boolean onKeyMultiple(int keyCode, int count, KeyEvent event);
    }

    @Override
    public String toString() {
        return "KeyEvent{" + this.mAction + " code=" + this.mKeyCode + "}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
    }
}
