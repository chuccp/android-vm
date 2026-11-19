package android.view.accessibility;

import android.graphics.Rect;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * 无障碍窗口信息（AOSP android.view.accessibility.AccessibilityWindowInfo；
 * §31.7 C 档 lib POJO——窗口节点态）。
 */
public class AccessibilityWindowInfo implements Parcelable {

    public static final int TYPE_APPLICATION = 1;
    public static final int TYPE_INPUT_METHOD = 2;
    public static final int TYPE_SYSTEM = 3;
    public static final int TYPE_ACCESSIBILITY_OVERLAY = 4;
    public static final int TYPE_SPLIT_SCREEN_DIVIDER = 5;

    private int mId = -1;
    private int mType = TYPE_APPLICATION;
    private int mLayer;
    private boolean mActive;
    private boolean mFocused;
    private boolean mAccessibilityFocused;
    private boolean mHasParent;
    private boolean mHasChildren;
    private final Rect mBoundsInScreen = new Rect();

    public AccessibilityWindowInfo() {
    }

    public static AccessibilityWindowInfo obtain() {
        return new AccessibilityWindowInfo();
    }

    public static AccessibilityWindowInfo obtain(AccessibilityWindowInfo other) {
        AccessibilityWindowInfo info = new AccessibilityWindowInfo();
        if (other != null) {
            info.mType = other.mType;
            info.mLayer = other.mLayer;
            info.mBoundsInScreen.set(other.mBoundsInScreen);
        }
        return info;
    }

    public int getId() {
        return this.mId;
    }

    public void setType(int type) {
        this.mType = type;
    }

    public int getType() {
        return this.mType;
    }

    public void setLayer(int layer) {
        this.mLayer = layer;
    }

    public int getLayer() {
        return this.mLayer;
    }

    public void setBoundsInScreen(Rect bounds) {
        this.mBoundsInScreen.set(bounds);
    }

    public void getBoundsInScreen(Rect outBounds) {
        outBounds.set(this.mBoundsInScreen);
    }

    public boolean isActive() {
        return this.mActive;
    }

    public void setActive(boolean active) {
        this.mActive = active;
    }

    public boolean isFocused() {
        return this.mFocused;
    }

    public void setFocused(boolean focused) {
        this.mFocused = focused;
    }

    public boolean isAccessibilityFocused() {
        return this.mAccessibilityFocused;
    }

    public void setAccessibilityFocused(boolean focused) {
        this.mAccessibilityFocused = focused;
    }

    public AccessibilityWindowInfo getParent() {
        return null;
    }

    public int getChildCount() {
        return 0;
    }

    public AccessibilityWindowInfo getChild(int index) {
        return null;
    }

    public void recycle() {
    }

    @Override
    public boolean equals(Object object) {
        return this == object;
    }

    @Override
    public int hashCode() {
        return System.identityHashCode(this);
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
    }
}
