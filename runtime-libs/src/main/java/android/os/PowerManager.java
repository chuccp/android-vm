package android.os;

/**
 * 电源管理器（AOSP android.os.PowerManager；§31.7 C 档 lib 骨架——唤醒锁态；真
 * 电源语义归桥接层）。
 */
public class PowerManager {

    public static final int PARTIAL_WAKE_LOCK = 1;
    public static final int SCREEN_DIM_WAKE_LOCK = 6;
    public static final int SCREEN_BRIGHT_WAKE_LOCK = 10;
    public static final int FULL_WAKE_LOCK = 26;
    public static final int PROXIMITY_SCREEN_OFF_WAKE_LOCK = 32;
    public static final int ON_AFTER_RELEASE = 536870912;
    public static final int ACQUIRE_CAUSES_WAKEUP = 268435456;

    /** 唤醒锁（AOSP PowerManager.WakeLock）。 */
    public final class WakeLock {
        private final int mLevelAndFlags;
        private boolean mHeld;

        WakeLock(int levelAndFlags) {
            this.mLevelAndFlags = levelAndFlags;
        }

        public void acquire() {
            this.mHeld = true;
        }

        public void acquire(long timeout) {
            this.mHeld = true;
        }

        public void release() {
            this.mHeld = false;
        }

        public boolean isHeld() {
            return this.mHeld;
        }

        public void setReferenceCounted(boolean value) {
        }

        public void setWorkSource(WorkSource ws) {
        }

        public String toString() {
            return "WakeLock{" + Integer.toHexString(this.mLevelAndFlags) + "}";
        }
    }

    public WakeLock newWakeLock(int levelAndFlags, String tag) {
        return new WakeLock(levelAndFlags);
    }

    public boolean isScreenOn() {
        return true;
    }

    public boolean isInteractive() {
        return true;
    }

    public void userActivity(long when, boolean noChangeLights) {
    }

    public void goToSleep(long time) {
    }

    public void reboot(String reason) {
    }

    public boolean isPowerSaveMode() {
        return false;
    }
}
