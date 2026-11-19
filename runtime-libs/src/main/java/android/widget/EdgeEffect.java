package android.widget;

import android.content.Context;

/**
 * 边缘效果（AOSP android.widget.EdgeEffect；§31.7 C 档 lib 骨架——过滚指示；
 * 动画归桥接渲染层）。
 */
public class EdgeEffect {

    private final Context mContext;

    public EdgeEffect(Context context) {
        this.mContext = context;
    }

    public void setSize(int width, int height) {
    }

    public boolean isFinished() {
        return true;
    }

    public void finish() {
    }

    public void onPull(float deltaDistance) {
    }

    public void onPull(float deltaDistance, float displacement) {
    }

    public void onRelease() {
    }

    public void onAbsorb(int velocity) {
    }

    public boolean draw(android.graphics.Canvas canvas) {
        return false;
    }

    public int getMaxHeight() {
        return 0;
    }
}
