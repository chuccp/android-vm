package android.view;

/**
 * 速度追踪（AOSP android.view.VelocityTracker；§31.7 C 档 lib 骨架——速度估计）。
 */
public class VelocityTracker {

    private VelocityTracker() {
    }

    public static VelocityTracker obtain() {
        return new VelocityTracker();
    }

    public void recycle() {
    }

    public void clear() {
    }

    public void addMovement(MotionEvent ev) {
    }

    public void computeCurrentVelocity(int units) {
    }

    public void computeCurrentVelocity(int units, float maxVelocity) {
    }

    public float getXVelocity() {
        return 0;
    }

    public float getYVelocity() {
        return 0;
    }

    public float getXVelocity(int id) {
        return 0;
    }

    public float getYVelocity(int id) {
        return 0;
    }
}
