package android.view.accessibility;

import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * 无障碍事件（AOSP android.view.accessibility.AccessibilityEvent；§31.7 C 档 lib
 * POJO——事件字段态；派发归无障碍桥）。
 */
public class AccessibilityEvent extends AccessibilityRecord {

    public static final int TYPE_VIEW_CLICKED = 0x00000001;
    public static final int TYPE_VIEW_LONG_CLICKED = 0x00000002;
    public static final int TYPE_VIEW_SELECTED = 0x00000004;
    public static final int TYPE_VIEW_FOCUSED = 0x00000008;
    public static final int TYPE_VIEW_TEXT_CHANGED = 0x00000010;
    public static final int TYPE_VIEW_SCROLLED = 0x00000100;
    public static final int TYPE_WINDOW_STATE_CHANGED = 0x00000200;
    public static final int TYPE_NOTIFICATION_STATE_CHANGED = 0x00004000;
    public static final int TYPE_VIEW_HOVER_ENTER = 0x00008000;
    public static final int TYPE_VIEW_HOVER_EXIT = 0x00010000;
    public static final int TYPE_VIEW_ACCESSIBILITY_FOCUSED = 0x00020000;
    public static final int TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED = 0x00040000;
    public static final int TYPE_VIEW_TEXT_TRAVERSED_AT_MOVEMENT_GRANULARITY = 0x00080000;
    public static final int TYPE_GESTURE_DETECTION_START = 0x00100000;
    public static final int TYPE_GESTURE_DETECTION_END = 0x00200000;
    public static final int TYPE_TOUCH_INTERACTION_START = 0x00400000;
    public static final int TYPE_TOUCH_INTERACTION_END = 0x00800000;
    public static final int TYPE_WINDOWS_CHANGED = 0x01000000;
    public static final int TYPE_ANNOUNCEMENT = 0x02000000;

    public static final int CONTENT_CHANGE_TYPE_UNDEFINED = 0x00000000;
    public static final int CONTENT_CHANGE_TYPE_SUBTREE = 0x00000001;
    public static final int CONTENT_CHANGE_TYPE_TEXT = 0x00000002;
    public static final int CONTENT_CHANGE_TYPE_CONTENT_DESCRIPTION = 0x00000004;

    private int mEventType;
    private int mContentChangeTypes;
    private CharSequence mBeforeText;

    public AccessibilityEvent() {
    }

    public AccessibilityEvent(AccessibilityEvent event) {
        super(event);
        this.mEventType = event.mEventType;
    }

    public static AccessibilityEvent obtain(int eventType) {
        AccessibilityEvent e = new AccessibilityEvent();
        e.mEventType = eventType;
        return e;
    }

    public static AccessibilityEvent obtain() {
        return new AccessibilityEvent();
    }

    public static AccessibilityEvent obtain(AccessibilityEvent event) {
        return new AccessibilityEvent(event);
    }

    public void setEventType(int eventType) {
        this.mEventType = eventType;
    }

    public int getEventType() {
        return this.mEventType;
    }

    public void setContentChangeTypes(int changeTypes) {
        this.mContentChangeTypes = changeTypes;
    }

    public int getContentChangeTypes() {
        return this.mContentChangeTypes;
    }

    public CharSequence getBeforeText() {
        return this.mBeforeText;
    }

    public void setBeforeText(CharSequence beforeText) {
        this.mBeforeText = beforeText;
    }

    public void init(AccessibilityEvent event) {
        super.init(event);
        this.mEventType = event.mEventType;
    }

    @Override
    public void recycle() {
    }

    @Override
    public String toString() {
        return "AccessibilityEvent{" + Integer.toHexString(this.mEventType) + "}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
    }
}
