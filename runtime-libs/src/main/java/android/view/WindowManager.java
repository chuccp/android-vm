package android.view;

import android.os.Parcelable;

/**
 * 窗口管理器接口（AOSP android.view.WindowManager；§31.7 C 档 lib 接口面）。
 */
public interface WindowManager extends ViewManager {

    Display getDefaultDisplay();

    void removeViewImmediate(View view);

    void requestAppKeyboardShortcuts(KeyboardShortcutsReceiver receiver, int deviceId);

    /** 键盘快捷键接收器（AOSP WindowManager.KeyboardShortcutsReceiver）。 */
    interface KeyboardShortcutsReceiver {
        void onKeyboardShortcutsReceived(java.util.List<KeyboardShortcutGroup> result);
    }

    /** 键盘快捷键组（AOSP 骨架）。 */
    class KeyboardShortcutGroup {
        public KeyboardShortcutGroup() {
        }
    }

    /** 窗口布局参数（AOSP WindowManager.LayoutParams）。 */
    class LayoutParams extends ViewGroup.LayoutParams implements Parcelable {
        public int x;
        public int y;
        public float horizontalWeight;
        public float verticalWeight;
        public int type;
        public int flags;
        public int privateFlags;
        public int softInputMode;
        public int gravity = -1;
        public float horizontalMargin;
        public float verticalMargin;
        public int windowAnimations;
        public float alpha = 1.0f;
        public float screenBrightness = -1f;
        public int buttonBrightness = -1;
        public int format = -3;  // PixelFormat.TRANSLUCENT
        public CharSequence title = "android";

        public static final int FIRST_APPLICATION_WINDOW = 1;
        public static final int TYPE_BASE_APPLICATION = 1;
        public static final int TYPE_APPLICATION = 2;
        public static final int TYPE_APPLICATION_STARTING = 3;
        public static final int TYPE_APPLICATION_PANEL = 1000;
        public static final int TYPE_APPLICATION_MEDIA = 1001;
        public static final int TYPE_APPLICATION_SUB_PANEL = 1002;
        public static final int TYPE_APPLICATION_ATTACHED_DIALOG = 1003;
        public static final int TYPE_STATUS_BAR = 2000;
        public static final int TYPE_SEARCH_BAR = 2001;
        public static final int TYPE_PHONE = 2002;
        public static final int TYPE_SYSTEM_ALERT = 2003;
        public static final int TYPE_KEYGUARD = 2004;
        public static final int TYPE_TOAST = 2005;
        public static final int TYPE_SYSTEM_OVERLAY = 2006;
        public static final int TYPE_PRIORITY_PHONE = 2007;
        public static final int TYPE_SYSTEM_DIALOG = 2008;
        public static final int TYPE_KEYGUARD_DIALOG = 2009;
        public static final int TYPE_SYSTEM_ERROR = 2010;
        public static final int TYPE_INPUT_METHOD = 2011;
        public static final int TYPE_INPUT_METHOD_DIALOG = 2012;
        public static final int TYPE_WALLPAPER = 2013;
        public static final int TYPE_APPLICATION_OVERLAY = 2038;

        public static final int FLAG_ALLOW_LOCK_WHILE_SCREEN_ON = 0x00000001;
        public static final int FLAG_DIM_BEHIND = 0x00000002;
        public static final int FLAG_NOT_FOCUSABLE = 0x00000008;
        public static final int FLAG_NOT_TOUCHABLE = 0x00000010;
        public static final int FLAG_NOT_TOUCH_MODAL = 0x00000020;
        public static final int FLAG_TOUCHABLE_WHEN_WAKING = 0x00000040;
        public static final int FLAG_KEEP_SCREEN_ON = 0x00000080;
        public static final int FLAG_LAYOUT_IN_SCREEN = 0x00000100;
        public static final int FLAG_LAYOUT_NO_LIMITS = 0x00000200;
        public static final int FLAG_FULLSCREEN = 0x00000400;
        public static final int FLAG_FORCE_NOT_FULLSCREEN = 0x00000800;
        public static final int FLAG_SECURE = 0x00002000;
        public static final int FLAG_SCALED = 0x00004000;
        public static final int FLAG_IGNORE_CHEEK_PRESSES = 0x00008000;
        public static final int FLAG_LAYOUT_INSET_DECOR = 0x00010000;
        public static final int FLAG_HARDWARE_ACCELERATED = 0x01000000;

        // 刘海屏（API 28+）：AppCompat/inset 链常用面
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT = 0;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES = 1;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER = 2;
        public static final int LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS = 3;
        public int layoutInDisplayCutoutMode = LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT;
        public int fitInsetsTypes;
        public int fitInsetsSides;
        public boolean fitInsetsIgnoringVisibility;
        public int windowLayoutInDisplayCutoutMode;
        public boolean layoutDoesNotFitSystemWindows;
        public int rotationAnimation;
        public int preferredRefreshRate;

        public static final int SOFT_INPUT_MASK_STATE = 0x0f;
        public static final int SOFT_INPUT_STATE_UNSPECIFIED = 0;
        public static final int SOFT_INPUT_STATE_UNCHANGED = 1;
        public static final int SOFT_INPUT_STATE_HIDDEN = 2;
        public static final int SOFT_INPUT_STATE_ALWAYS_HIDDEN = 3;
        public static final int SOFT_INPUT_STATE_VISIBLE = 4;
        public static final int SOFT_INPUT_STATE_ALWAYS_VISIBLE = 5;
        public static final int SOFT_INPUT_MASK_ADJUST = 0xf0;
        public static final int SOFT_INPUT_ADJUST_UNSPECIFIED = 0x00;
        public static final int SOFT_INPUT_ADJUST_RESIZE = 0x10;
        public static final int SOFT_INPUT_ADJUST_PAN = 0x20;
        public static final int SOFT_INPUT_ADJUST_NOTHING = 0x30;
        public static final int SOFT_INPUT_IS_FORWARD_NAVIGATION = 0x100;

        public LayoutParams() {
            super(0, 0);
        }

        public LayoutParams(int type) {
            super(0, 0);
            this.type = type;
        }

        public LayoutParams(int type, int flags) {
            super(0, 0);
            this.type = type;
            this.flags = flags;
        }

        public LayoutParams(int type, int flags, int format) {
            super(0, 0);
            this.type = type;
            this.flags = flags;
            this.format = format;
        }

        public LayoutParams(int w, int h, int type, int flags, int format) {
            super(w, h);
            this.type = type;
            this.flags = flags;
            this.format = format;
        }

        public LayoutParams(int w, int h, int xpos, int ypos, int type, int flags, int format) {
            super(w, h);
            this.x = xpos;
            this.y = ypos;
            this.type = type;
            this.flags = flags;
            this.format = format;
        }

        public LayoutParams(WindowManager.LayoutParams source) {
            super(source);
            copyFrom(source);
        }

        public LayoutParams(ViewGroup.LayoutParams source) {
            super(source);
        }

        public int copyFrom(LayoutParams o) {
            if (o == null) return 0;
            this.x = o.x;
            this.y = o.y;
            this.type = o.type;
            this.flags = o.flags;
            this.gravity = o.gravity;
            this.alpha = o.alpha;
            this.title = o.title;
            return 0;
        }

        public void setTitle(CharSequence title) {
            this.title = title != null ? title : "";
        }

        @Override
        public int describeContents() {
            return 0;
        }

        @Override
        public void writeToParcel(android.os.Parcel out, int flags) {
        }
    }

}
