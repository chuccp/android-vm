package android.graphics.drawable;

import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.PorterDuff;
import android.graphics.Rect;
import android.graphics.Region;

/**
 * 可绘制基类（AOSP android.graphics.drawable.Drawable 公开面；§31.7 C 档 lib——
 * 复杂类禁 native）。字段态 = bounds/alpha/level/state/callback/visible；
 * draw 默认空实现（子类/桥接渲染层落像素）。
 */
public abstract class Drawable {

    /** 回调接口（invalidate/schedule/unschedule 缝；AOSP Drawable.Callback）。 */
    public interface Callback {
        void invalidateDrawable(Drawable who);

        void scheduleDrawable(Drawable who, Runnable what, long when);

        void unscheduleDrawable(Drawable who, Runnable what);
    }

    /** 常量态（AOSP Drawable.ConstantState；newDrawable 面）。 */
    public abstract static class ConstantState {
        public abstract Drawable newDrawable();

        public int getChangingConfigurations() {
            return 0;
        }
    }

    private final Rect mBounds = new Rect();
    private Callback mCallback;
    private int[] mStateSet = StateSet.WILD_CARD;
    private boolean mVisible = true;
    private int mAlpha = 255;
    private int mLevel;
    private int mChangingConfigurations;
    private ColorFilter mColorFilter;

    public Drawable() {
    }

    public abstract void draw(Canvas canvas);

    public void setBounds(int left, int top, int right, int bottom) {
        this.mBounds.set(left, top, right, bottom);
        onBoundsChange(this.mBounds);
    }

    public void setBounds(Rect bounds) {
        if (bounds != null) {
            setBounds(bounds.left, bounds.top, bounds.right, bounds.bottom);
        }
    }

    public final Rect getBounds() {
        return this.mBounds;
    }

    public final Rect getDirtyBounds() {
        return this.mBounds;
    }

    protected void onBoundsChange(Rect bounds) {
    }

    public void setChangingConfigurations(int configs) {
        this.mChangingConfigurations = configs;
    }

    public int getChangingConfigurations() {
        return this.mChangingConfigurations;
    }

    public void setCallback(Callback cb) {
        this.mCallback = cb;
    }

    public Callback getCallback() {
        return this.mCallback;
    }

    public void invalidateSelf() {
        if (this.mCallback != null) {
            this.mCallback.invalidateDrawable(this);
        }
    }

    public void scheduleSelf(Runnable what, long when) {
        if (this.mCallback != null) {
            this.mCallback.scheduleDrawable(this, what, when);
        }
    }

    public void unscheduleSelf(Runnable what) {
        if (this.mCallback != null) {
            this.mCallback.unscheduleDrawable(this, what);
        }
    }

    public int getAlpha() {
        return this.mAlpha;
    }

    public void setAlpha(int alpha) {
        this.mAlpha = alpha;
    }

    public void setColorFilter(ColorFilter colorFilter) {
        this.mColorFilter = colorFilter;
    }

    public void setColorFilter(int color, PorterDuff.Mode mode) {
    }

    public ColorFilter getColorFilter() {
        return this.mColorFilter;
    }

    public void clearColorFilter() {
        this.mColorFilter = null;
    }

    public void setDither(boolean dither) {
    }

    public void setFilterBitmap(boolean filter) {
    }

    public boolean isFilterBitmap() {
        return false;
    }

    public abstract ConstantState getConstantState();

    public Drawable mutate() {
        return this;
    }

    public int[] getState() {
        return this.mStateSet;
    }

    public boolean setState(int[] stateSet) {
        this.mStateSet = stateSet != null ? stateSet : StateSet.WILD_CARD;
        return false;
    }

    public void jumpToCurrentState() {
    }

    public boolean isStateful() {
        return false;
    }

    public boolean setVisible(boolean visible, boolean restart) {
        boolean changed = this.mVisible != visible;
        this.mVisible = visible;
        return changed;
    }

    public boolean isVisible() {
        return this.mVisible;
    }

    public int getOpacity() {
        return android.graphics.PixelFormat.TRANSLUCENT;
    }

    public Region getTransparentRegion() {
        return null;
    }

    public boolean getPadding(Rect padding) {
        if (padding != null) padding.set(0, 0, 0, 0);
        return false;
    }

    public int getIntrinsicWidth() {
        return -1;
    }

    public int getIntrinsicHeight() {
        return -1;
    }

    public int getMinimumWidth() {
        return 0;
    }

    public int getMinimumHeight() {
        return 0;
    }

    public boolean getHotspotBounds(Rect outRect) {
        return false;
    }

    public void setHotspot(float x, float y) {
    }

    public void setHotspotBounds(int left, int top, int right, int bottom) {
    }

    public boolean setLevel(int level) {
        this.mLevel = level;
        return false;
    }

    public final int getLevel() {
        return this.mLevel;
    }

    public boolean onLayoutDirectionChanged(int layoutDirection) {
        return false;
    }

    public void setAutoMirrored(boolean mirrored) {
    }

    public boolean isAutoMirrored() {
        return false;
    }

    public void applyTheme(android.content.res.Resources.Theme t) {
    }

    public boolean canApplyTheme() {
        return false;
    }

    public void setTint(int tintColor) {
    }

    public void setTintList(android.content.res.ColorStateList tint) {
    }

    public void setTintMode(PorterDuff.Mode tintMode) {
    }

    @Override
    public String toString() {
        return getClass().getSimpleName() + "(" + this.mBounds.toShortString() + ")";
    }
}
