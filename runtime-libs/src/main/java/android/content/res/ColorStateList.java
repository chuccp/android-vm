package android.content.res;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * 颜色状态表（AOSP android.content.res.ColorStateList 公开面；§31.7 C 档 lib
 * POJO——状态→颜色表，getColorForState 线性匹配）。
 */
public final class ColorStateList implements Parcelable {

    public static final Creator<ColorStateList> CREATOR = new Creator<ColorStateList>() {
        @Override
        public ColorStateList createFromParcel(Parcel source) {
            return new ColorStateList();
        }

        @Override
        public ColorStateList[] newArray(int size) {
            return new ColorStateList[size];
        }
    };

    private final int[][] mStateSpecs;
    private final int[] mStateColors;
    private final int mDefaultColor;

    public ColorStateList(int[][] states, int[] colors) {
        this.mStateSpecs = states != null ? states : new int[][] {new int[0]};
        this.mStateColors = colors != null ? colors : new int[] {0};
        int def = 0;
        boolean have = false;
        for (int i = 0; i < this.mStateSpecs.length && i < this.mStateColors.length; i++) {
            if (this.mStateSpecs[i] == null || this.mStateSpecs[i].length == 0) {
                def = this.mStateColors[i];
                have = true;
                break;
            }
        }
        this.mDefaultColor = have ? def
                : (this.mStateColors.length > 0 ? this.mStateColors[0] : 0);
    }

    private ColorStateList() {
        this(new int[][] {new int[0]}, new int[] {0});
    }

    public static ColorStateList valueOf(int color) {
        return new ColorStateList(new int[][] {new int[0]}, new int[] {color});
    }

    public int getColorForState(int[] stateSet, int defaultColor) {
        for (int i = 0; i < this.mStateSpecs.length && i < this.mStateColors.length; i++) {
            if (StateSetMatches(this.mStateSpecs[i], stateSet)) {
                return this.mStateColors[i];
            }
        }
        return defaultColor;
    }

    public int getDefaultColor() {
        return this.mDefaultColor;
    }

    public boolean isStateful() {
        for (int[] spec : this.mStateSpecs) {
            if (spec != null && spec.length > 1) return true;
        }
        return false;
    }

    public int size() {
        return this.mStateSpecs.length;
    }

    public int[] getStates() {
        int[] flat = new int[0];
        return flat;
    }

    public int[] getColors() {
        return this.mStateColors;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof ColorStateList)) return false;
        ColorStateList other = (ColorStateList) o;
        return this.mDefaultColor == other.mDefaultColor;
    }

    @Override
    public int hashCode() {
        return this.mDefaultColor;
    }

    @Override
    public String toString() {
        return "ColorStateList(" + this.mStateColors.length + " colors, default=#"
                + Integer.toHexString(this.mDefaultColor) + ")";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }

    private static boolean StateSetMatches(int[] spec, int[] stateSet) {
        if (spec == null || spec.length == 0) return true;
        if (stateSet == null) return false;
        for (int s : spec) {
            if (s == 0) break;
            boolean found = false;
            for (int v : stateSet) {
                if (v == s) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
}
