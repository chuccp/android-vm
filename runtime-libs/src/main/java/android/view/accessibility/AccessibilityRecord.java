package android.view.accessibility;

import android.os.Parcel;
import android.os.Parcelable;
import android.view.View;

/**
 * 无障碍记录（AOSP android.view.accessibility.AccessibilityRecord；§31.7 C 档 lib
 * POJO——记录字段态）。
 */
public class AccessibilityRecord implements Parcelable {

    boolean mSealed;
    int mSourceWindowId = -1;
    int mSourceNodeId = -1;
    int mSourceNodeVirtualDescendantId = -1;
    int mWindowId = -1;
    int mMaxScrollX;
    int mMaxScrollY;
    int mScrollX;
    int mScrollY;
    boolean mScrollXSet;
    boolean mScrollYSet;
    boolean mEnabled = true;
    boolean mPassword;
    boolean mFullScreen;
    boolean mScrollable;
    boolean mChecked;
    boolean mFocusable;
    boolean mFocused;
    boolean mSelected;
    boolean mClickable;
    boolean mLongClickable;
    boolean mContextClickable;
    boolean mEnabledSet;
    boolean mPasswordSet;
    boolean mFullScreenSet;
    boolean mScrollableSet;
    boolean mCheckedSet;
    boolean mFocusableSet;
    boolean mFocusedSet;
    boolean mSelectedSet;
    boolean mClickableSet;
    boolean mLongClickableSet;
    boolean mContextClickableSet;
    int mItemCount;
    int mCurrentItemIndex;
    CharSequence mClassName;
    CharSequence mPackageName;
    CharSequence mContentDescription;
    CharSequence mBeforeText;
    java.util.List<CharSequence> mText = new java.util.ArrayList<CharSequence>();
    android.os.Bundle mAdditionalData;

    public AccessibilityRecord() {
    }

    public AccessibilityRecord(AccessibilityRecord record) {
        init(record);
    }

    public void init(AccessibilityRecord record) {
        if (record == null) return;
        this.mEnabled = record.mEnabled;
        this.mPassword = record.mPassword;
        this.mFullScreen = record.mFullScreen;
        this.mScrollable = record.mScrollable;
        this.mChecked = record.mChecked;
        this.mFocusable = record.mFocusable;
        this.mFocused = record.mFocused;
        this.mSelected = record.mSelected;
        this.mClickable = record.mClickable;
        this.mClassName = record.mClassName;
        this.mPackageName = record.mPackageName;
        this.mContentDescription = record.mContentDescription;
    }

    public void setSource(View source) {
    }

    public void setSource(View root, int virtualDescendantId) {
    }

    public AccessibilityNodeInfo getSource() {
        return null;
    }

    public int getWindowId() {
        return this.mWindowId;
    }

    public boolean isChecked() {
        return this.mChecked;
    }

    public void setChecked(boolean isChecked) {
        this.mChecked = isChecked;
    }

    public boolean isEnabled() {
        return this.mEnabled;
    }

    public void setEnabled(boolean isEnabled) {
        this.mEnabled = isEnabled;
    }

    public boolean isPassword() {
        return this.mPassword;
    }

    public void setPassword(boolean isPassword) {
        this.mPassword = isPassword;
    }

    public boolean isFullScreen() {
        return this.mFullScreen;
    }

    public void setFullScreen(boolean isFullScreen) {
        this.mFullScreen = isFullScreen;
    }

    public boolean isScrollable() {
        return this.mScrollable;
    }

    public void setScrollable(boolean isScrollable) {
        this.mScrollable = isScrollable;
    }

    public int getItemCount() {
        return this.mItemCount;
    }

    public void setItemCount(int itemCount) {
        this.mItemCount = itemCount;
    }

    public int getCurrentItemIndex() {
        return this.mCurrentItemIndex;
    }

    public void setCurrentItemIndex(int currentItemIndex) {
        this.mCurrentItemIndex = currentItemIndex;
    }

    public int getScrollX() {
        return this.mScrollX;
    }

    public void setScrollX(int scrollX) {
        this.mScrollX = scrollX;
    }

    public int getScrollY() {
        return this.mScrollY;
    }

    public void setScrollY(int scrollY) {
        this.mScrollY = scrollY;
    }

    public int getMaxScrollX() {
        return this.mMaxScrollX;
    }

    public void setMaxScrollX(int maxScrollX) {
        this.mMaxScrollX = maxScrollX;
    }

    public int getMaxScrollY() {
        return this.mMaxScrollY;
    }

    public void setMaxScrollY(int maxScrollY) {
        this.mMaxScrollY = maxScrollY;
    }

    public int getAddedCount() {
        return 0;
    }

    public void setAddedCount(int addedCount) {
    }

    public int getRemovedCount() {
        return 0;
    }

    public void setRemovedCount(int removedCount) {
    }

    public CharSequence getClassName() {
        return this.mClassName;
    }

    public void setClassName(CharSequence className) {
        this.mClassName = className;
    }

    public CharSequence getPackageName() {
        return this.mPackageName;
    }

    public void setPackageName(CharSequence packageName) {
        this.mPackageName = packageName;
    }

    public CharSequence getText() {
        return null;
    }

    public void setText(CharSequence text) {
    }

    public CharSequence getBeforeText() {
        return this.mBeforeText;
    }

    public void setBeforeText(CharSequence beforeText) {
        this.mBeforeText = beforeText;
    }

    public CharSequence getContentDescription() {
        return this.mContentDescription;
    }

    public void setContentDescription(CharSequence contentDescription) {
        this.mContentDescription = contentDescription;
    }

    public android.os.Bundle getExtras() {
        return this.mAdditionalData;
    }

    public void recycle() {
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
    }
}
