package android.graphics.drawable;

import android.content.res.ColorStateList;

/**
 * 状态列表可绘制（AOSP android.graphics.drawable.StateListDrawable 公开面；
 * §31.7 C 档 lib 骨架——状态→Drawable 表）。
 */
public class StateListDrawable extends Drawable {

    private final java.util.ArrayList<int[]> mStateSets = new java.util.ArrayList<int[]>();
    private final java.util.ArrayList<Drawable> mDrawables = new java.util.ArrayList<Drawable>();

    public StateListDrawable() {
    }

    public void addState(int[] stateSet, Drawable drawable) {
        this.mStateSets.add(stateSet != null ? stateSet : StateSet.WILD_CARD);
        this.mDrawables.add(drawable);
    }

    @Override
    public boolean isStateful() {
        return true;
    }

    @Override
    public boolean setState(int[] stateSet) {
        int idx = 0;
        for (int i = 0; i < this.mStateSets.size(); i++) {
            if (StateSet.stateMatches(this.mStateSets.get(i), stateSet)) {
                idx = i;
                break;
            }
        }
        return super.setState(stateSet);
    }

    public Drawable getCurrent() {
        return this.mDrawables.isEmpty() ? null : this.mDrawables.get(0);
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
        Drawable d = getCurrent();
        if (d != null) d.draw(canvas);
    }

    @Override
    public void setAlpha(int alpha) {
    }

    @Override
    public void setColorFilter(android.graphics.ColorFilter cf) {
    }

    @Override
    public ConstantState getConstantState() {
        return null;
    }

    public void setColorFilter(int color, android.graphics.PorterDuff.Mode mode) {
    }

    public void addState(int[] stateSet, ColorStateList colorList) {
    }
}
