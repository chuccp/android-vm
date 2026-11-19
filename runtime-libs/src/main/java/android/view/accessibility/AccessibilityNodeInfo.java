package android.view.accessibility;
import android.view.View;

import android.graphics.Rect;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;

import java.util.ArrayList;
import java.util.List;

/**
 * 无障碍节点信息（AOSP android.view.accessibility.AccessibilityNodeInfo；
 * §31.7 C 档 lib POJO——节点树字段态；采集归无障碍桥）。
 */
public class AccessibilityNodeInfo implements Parcelable {

    public static final int ACTION_FOCUS = 0x00000001;
    public static final int ACTION_CLEAR_FOCUS = 0x00000002;
    public static final int ACTION_SELECT = 0x00000004;
    public static final int ACTION_CLEAR_SELECTION = 0x00000008;
    public static final int ACTION_CLICK = 0x00000010;
    public static final int ACTION_LONG_CLICK = 0x00000020;
    public static final int ACTION_ACCESSIBILITY_FOCUS = 0x00000040;
    public static final int ACTION_CLEAR_ACCESSIBILITY_FOCUS = 0x00000080;
    public static final int ACTION_NEXT_AT_MOVEMENT_GRANULARITY = 0x00000100;
    public static final int ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY = 0x00000200;
    public static final int ACTION_NEXT_HTML_ELEMENT = 0x00000400;
    public static final int ACTION_PREVIOUS_HTML_ELEMENT = 0x00000800;
    public static final int ACTION_SCROLL_FORWARD = 0x00001000;
    public static final int ACTION_SCROLL_BACKWARD = 0x00002000;
    public static final int ACTION_COPY = 0x00004000;
    public static final int ACTION_PASTE = 0x00008000;
    public static final int ACTION_CUT = 0x00010000;
    public static final int ACTION_SET_SELECTION = 0x00020000;
    public static final int ACTION_EXPAND = 0x00040000;
    public static final int ACTION_COLLAPSE = 0x00080000;
    public static final int ACTION_DISMISS = 0x00100000;
    public static final int ACTION_SET_TEXT = 0x00200000;

    public static final int FOCUS_INPUT = 1;
    public static final int FOCUS_ACCESSIBILITY = 2;

    public static final class AccessibilityAction {
        private final int mId;
        private final CharSequence mLabel;

        // AOSP 标准动作实例（androidx AccessibilityActionCompat <clinit> sget 面）。
        public static final AccessibilityAction ACTION_FOCUS =
                new AccessibilityAction(0x00000001, null);
        public static final AccessibilityAction ACTION_CLEAR_FOCUS =
                new AccessibilityAction(0x00000002, null);
        public static final AccessibilityAction ACTION_SELECT =
                new AccessibilityAction(0x00000004, null);
        public static final AccessibilityAction ACTION_CLEAR_SELECTION =
                new AccessibilityAction(0x00000008, null);
        public static final AccessibilityAction ACTION_CLICK =
                new AccessibilityAction(0x00000010, null);
        public static final AccessibilityAction ACTION_LONG_CLICK =
                new AccessibilityAction(0x00000020, null);
        public static final AccessibilityAction ACTION_ACCESSIBILITY_FOCUS =
                new AccessibilityAction(0x00000040, null);
        public static final AccessibilityAction ACTION_CLEAR_ACCESSIBILITY_FOCUS =
                new AccessibilityAction(0x00000080, null);
        public static final AccessibilityAction ACTION_NEXT_AT_MOVEMENT_GRANULARITY =
                new AccessibilityAction(0x00000100, null);
        public static final AccessibilityAction ACTION_PREVIOUS_AT_MOVEMENT_GRANULARITY =
                new AccessibilityAction(0x00000200, null);
        public static final AccessibilityAction ACTION_NEXT_HTML_ELEMENT =
                new AccessibilityAction(0x00000400, null);
        public static final AccessibilityAction ACTION_PREVIOUS_HTML_ELEMENT =
                new AccessibilityAction(0x00000800, null);
        public static final AccessibilityAction ACTION_COPY =
                new AccessibilityAction(0x00004000, null);
        public static final AccessibilityAction ACTION_PASTE =
                new AccessibilityAction(0x00008000, null);
        public static final AccessibilityAction ACTION_CUT =
                new AccessibilityAction(0x00010000, null);
        public static final AccessibilityAction ACTION_SET_SELECTION =
                new AccessibilityAction(0x00020000, null);
        public static final AccessibilityAction ACTION_EXPAND =
                new AccessibilityAction(0x00040000, null);
        public static final AccessibilityAction ACTION_COLLAPSE =
                new AccessibilityAction(0x00080000, null);
        public static final AccessibilityAction ACTION_DISMISS =
                new AccessibilityAction(0x00100000, null);
        public static final AccessibilityAction ACTION_SCROLL_FORWARD =
                new AccessibilityAction(0x00001000, null);
        public static final AccessibilityAction ACTION_SCROLL_BACKWARD =
                new AccessibilityAction(0x00002000, null);
        public static final AccessibilityAction ACTION_SET_TEXT =
                new AccessibilityAction(0x00200000, null);
        public static final AccessibilityAction ACTION_SHOW_ON_SCREEN =
                new AccessibilityAction(0x01020006, null);
        public static final AccessibilityAction ACTION_SCROLL_TO_POSITION =
                new AccessibilityAction(0x01020007, null);
        public static final AccessibilityAction ACTION_SCROLL_UP =
                new AccessibilityAction(0x01020008, null);
        public static final AccessibilityAction ACTION_SCROLL_LEFT =
                new AccessibilityAction(0x01020009, null);
        public static final AccessibilityAction ACTION_SCROLL_DOWN =
                new AccessibilityAction(0x0102000a, null);
        public static final AccessibilityAction ACTION_SCROLL_RIGHT =
                new AccessibilityAction(0x0102000b, null);
        public static final AccessibilityAction ACTION_CONTEXT_CLICK =
                new AccessibilityAction(0x0102003c, null);
        public static final AccessibilityAction ACTION_SET_PROGRESS =
                new AccessibilityAction(0x0102003d, null);
        public static final AccessibilityAction ACTION_MOVE_WINDOW =
                new AccessibilityAction(0x01020042, null);
        public static final AccessibilityAction ACTION_SHOW_TOOLTIP =
                new AccessibilityAction(0x01020044, null);
        public static final AccessibilityAction ACTION_HIDE_TOOLTIP =
                new AccessibilityAction(0x01020045, null);
        public static final AccessibilityAction ACTION_PRESS_AND_HOLD =
                new AccessibilityAction(0x01020046, null);
        public static final AccessibilityAction ACTION_IME_ENTER =
                new AccessibilityAction(0x01020047, null);
        public static final AccessibilityAction ACTION_PAGE_UP =
                new AccessibilityAction(0x01020048, null);
        public static final AccessibilityAction ACTION_PAGE_DOWN =
                new AccessibilityAction(0x01020049, null);
        public static final AccessibilityAction ACTION_PAGE_LEFT =
                new AccessibilityAction(0x0102004a, null);
        public static final AccessibilityAction ACTION_PAGE_RIGHT =
                new AccessibilityAction(0x0102004b, null);
        public static final AccessibilityAction ACTION_DRAG_START =
                new AccessibilityAction(0x01020055, null);
        public static final AccessibilityAction ACTION_DRAG_ENTER =
                new AccessibilityAction(0x01020056, null);
        public static final AccessibilityAction ACTION_DRAG_EXIT =
                new AccessibilityAction(0x01020057, null);
        public static final AccessibilityAction ACTION_DRAG_END =
                new AccessibilityAction(0x01020058, null);
        public static final AccessibilityAction ACTION_DRAG_CANCEL =
                new AccessibilityAction(0x01020059, null);
        public static final AccessibilityAction ACTION_SHOW_TEXT_SUGGESTIONS =
                new AccessibilityAction(0x0102005a, null);
        public static final AccessibilityAction ACTION_DRAG_DROP =
                new AccessibilityAction(0x0102005b, null);

        public AccessibilityAction(int id, CharSequence label) {
            this.mId = id;
            this.mLabel = label;
        }

        public int getId() {
            return this.mId;
        }

        public CharSequence getLabel() {
            return this.mLabel;
        }

        @Override
        public String toString() {
            return "AccessibilityAction(id=" + this.mId + ")";
        }
    }

    int mWindowId = -1;
    long mSourceNodeId = -1;
    int mParentNodeId = -1;
    boolean mSealed;
    int mBoundsInParentSet;
    int mBoundsInScreenSet;
    boolean mEnabled = true;
    boolean mFocusable;
    boolean mFocused;
    boolean mSelected;
    boolean mClickable;
    boolean mLongClickable;
    boolean mContextClickable;
    boolean mCheckable;
    boolean mChecked;
    boolean mFocusableSet;
    boolean mFocusedSet;
    boolean mSelectedSet;
    boolean mClickableSet;
    boolean mLongClickableSet;
    boolean mContextClickableSet;
    boolean mCheckableSet;
    boolean mCheckedSet;
    boolean mPassword;
    boolean mScrollable;
    boolean mVisibleToUser;
    CharSequence mClassName;
    CharSequence mPackageName;
    CharSequence mText;
    CharSequence mContentDescription;
    CharSequence mError;
    int mMaxTextLength = -1;
    int mMovementGranularities;
    int mActions;
    final Rect mBoundsInParent = new Rect();
    final Rect mBoundsInScreen = new Rect();
    List<AccessibilityAction> mActionsList;
    Bundle mExtras;
    List<AccessibilityNodeInfo> mChildren;

    public AccessibilityNodeInfo() {
    }

    public AccessibilityNodeInfo(AccessibilityNodeInfo other) {
        copyFrom(other);
    }

    public static AccessibilityNodeInfo obtain(View source) {
        AccessibilityNodeInfo info = new AccessibilityNodeInfo();
        return info;
    }

    public static AccessibilityNodeInfo obtain(AccessibilityNodeInfo other) {
        return new AccessibilityNodeInfo(other);
    }

    public static AccessibilityNodeInfo obtain() {
        return new AccessibilityNodeInfo();
    }

    public void copyFrom(AccessibilityNodeInfo other) {
        if (other == null) return;
        this.mEnabled = other.mEnabled;
        this.mClassName = other.mClassName;
        this.mPackageName = other.mPackageName;
        this.mText = other.mText;
        this.mContentDescription = other.mContentDescription;
    }

    public void setSource(View source) {
    }

    public void setSource(View root, int virtualDescendantId) {
    }

    public AccessibilityNodeInfo findFocus(int focus) {
        return null;
    }

    public AccessibilityNodeInfo focusSearch(int direction) {
        return null;
    }

    public int getWindowId() {
        return this.mWindowId;
    }

    public int getChildCount() {
        return this.mChildren != null ? this.mChildren.size() : 0;
    }

    public AccessibilityNodeInfo getChild(int index) {
        return this.mChildren != null ? this.mChildren.get(index) : null;
    }

    public void addChild(View child) {
    }

    public void addChild(AccessibilityNodeInfo child) {
        if (this.mChildren == null) this.mChildren = new ArrayList<AccessibilityNodeInfo>();
        this.mChildren.add(child);
    }

    public void removeChild(View child) {
    }

    public void removeChild(AccessibilityNodeInfo child) {
        if (this.mChildren != null) this.mChildren.remove(child);
    }

    public AccessibilityNodeInfo getParent() {
        return null;
    }

    public void setParent(View parent) {
    }

    public void setParent(View root, int virtualDescendantId) {
    }

    public void setParent(AccessibilityNodeInfo parent) {
    }

    public void getBoundsInParent(Rect outBounds) {
        outBounds.set(this.mBoundsInParent);
    }

    public void setBoundsInParent(Rect bounds) {
        this.mBoundsInParent.set(bounds);
    }

    public void getBoundsInScreen(Rect outBounds) {
        outBounds.set(this.mBoundsInScreen);
    }

    public void setBoundsInScreen(Rect bounds) {
        this.mBoundsInScreen.set(bounds);
    }

    public boolean isVisibleToUser() {
        return this.mVisibleToUser;
    }

    public void setVisibleToUser(boolean visibleToUser) {
        this.mVisibleToUser = visibleToUser;
    }

    public CharSequence getPackageName() {
        return this.mPackageName;
    }

    public void setPackageName(CharSequence packageName) {
        this.mPackageName = packageName;
    }

    public CharSequence getClassName() {
        return this.mClassName;
    }

    public void setClassName(CharSequence className) {
        this.mClassName = className;
    }

    public CharSequence getText() {
        return this.mText;
    }

    public void setText(CharSequence text) {
        this.mText = text;
    }

    public CharSequence getContentDescription() {
        return this.mContentDescription;
    }

    public void setContentDescription(CharSequence contentDescription) {
        this.mContentDescription = contentDescription;
    }

    public CharSequence getError() {
        return this.mError;
    }

    public void setError(CharSequence error) {
        this.mError = error;
    }

    public int getMaxTextLength() {
        return this.mMaxTextLength;
    }

    public void setMaxTextLength(int max) {
        this.mMaxTextLength = max;
    }

    public boolean isEnabled() {
        return this.mEnabled;
    }

    public void setEnabled(boolean enabled) {
        this.mEnabled = enabled;
    }

    public boolean isCheckable() {
        return this.mCheckable;
    }

    public void setCheckable(boolean checkable) {
        this.mCheckable = checkable;
    }

    public boolean isChecked() {
        return this.mChecked;
    }

    public void setChecked(boolean checked) {
        this.mChecked = checked;
    }

    public boolean isFocusable() {
        return this.mFocusable;
    }

    public void setFocusable(boolean focusable) {
        this.mFocusable = focusable;
    }

    public boolean isFocused() {
        return this.mFocused;
    }

    public void setFocused(boolean focused) {
        this.mFocused = focused;
    }

    public boolean isSelected() {
        return this.mSelected;
    }

    public void setSelected(boolean selected) {
        this.mSelected = selected;
    }

    public boolean isClickable() {
        return this.mClickable;
    }

    public void setClickable(boolean clickable) {
        this.mClickable = clickable;
    }

    public boolean isLongClickable() {
        return this.mLongClickable;
    }

    public void setLongClickable(boolean longClickable) {
        this.mLongClickable = longClickable;
    }

    public boolean isContextClickable() {
        return this.mContextClickable;
    }

    public void setContextClickable(boolean contextClickable) {
        this.mContextClickable = contextClickable;
    }

    public boolean isPassword() {
        return this.mPassword;
    }

    public void setPassword(boolean password) {
        this.mPassword = password;
    }

    public boolean isScrollable() {
        return this.mScrollable;
    }

    public void setScrollable(boolean scrollable) {
        this.mScrollable = scrollable;
    }

    public void addAction(int action) {
        this.mActions |= action;
    }

    public void addAction(AccessibilityAction action) {
        if (this.mActionsList == null) this.mActionsList = new ArrayList<AccessibilityAction>();
        this.mActionsList.add(action);
    }

    public void removeAction(AccessibilityAction action) {
        if (this.mActionsList != null) this.mActionsList.remove(action);
    }

    public List<AccessibilityAction> getActionList() {
        return this.mActionsList != null ? this.mActionsList : new ArrayList<AccessibilityAction>();
    }

    public int getActions() {
        return this.mActions;
    }

    public void setMovementGranularities(int granularities) {
        this.mMovementGranularities = granularities;
    }

    public int getMovementGranularities() {
        return this.mMovementGranularities;
    }

    public boolean performAction(int action) {
        return false;
    }

    public List<Integer> findAccessibilityNodeInfosByText(String text) {
        return new ArrayList<Integer>();
    }

    public List<Integer> findAccessibilityNodeInfosByViewId(String viewId) {
        return new ArrayList<Integer>();
    }

    public Bundle getExtras() {
        if (this.mExtras == null) this.mExtras = new Bundle();
        return this.mExtras;
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
    public String toString() {
        return "AccessibilityNodeInfo{" + this.mClassName + "}";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
    }
}
