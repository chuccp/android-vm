package android.content.res;

import android.graphics.drawable.Drawable;
import android.util.TypedValue;

/**
 * 类型化属性表（AOSP android.content.res.TypedArray 公开面；§31.7 C 档 lib——
 * 复杂类禁 native：值表数据载体 + 类型化取值；由 Resources.obtainStyledAttributes
 * 资源缝装配）。
 */
public class TypedArray {

    Resources mResources;
    int[] mRsrcs;
    int[] mData;
    int mLength;
    boolean mRecycled;

    TypedArray(Resources resources, int[] rsrcs, int[] data, int length) {
        this.mResources = resources;
        this.mRsrcs = rsrcs;
        this.mData = data;
        this.mLength = length;
    }

    public int getIndexCount() {
        int count = 0;
        for (int i = 0; i < this.mLength; i++) {
            if (this.mRsrcs[i] != 0) count++;
        }
        return count;
    }

    public int getIndex(int at) {
        int count = 0;
        for (int i = 0; i < this.mLength; i++) {
            if (this.mRsrcs[i] != 0) {
                if (count == at) return i;
                count++;
            }
        }
        return -1;
    }

    public Resources getResources() {
        return this.mResources;
    }

    public CharSequence getText(int index) {
        return getString(index);
    }

    public String getString(int index) {
        Object v = getValueOrNull(index);
        return v instanceof String ? (String) v : (v != null ? String.valueOf(v) : null);
    }

    public int getLength(int index) {
        String s = getString(index);
        return s != null ? s.length() : -1;
    }

    public String getNonResourceString(int index) {
        return getString(index);
    }

    public int getResourceId(int index, int defValue) {
        return getInt(index, defValue);
    }

    public int getColor(int index, int defValue) {
        return getInt(index, defValue);
    }

    public ColorStateList getColorStateList(int index) {
        return ColorStateList.valueOf(getColor(index, 0));
    }

    public float getFloat(int index, float defValue) {
        Object v = getValueOrNull(index);
        if (v instanceof Float) return (Float) v;
        if (v instanceof Integer) return (Integer) v;
        return defValue;
    }

    public boolean getBoolean(int index, boolean defValue) {
        return getInt(index, defValue ? 1 : 0) != 0;
    }

    public int getInt(int index, int defValue) {
        Object v = getValueOrNull(index);
        if (v instanceof Integer) return (Integer) v;
        if (v instanceof Float) return (int) (float) (Float) v;
        return defValue;
    }

    public int getInteger(int index, int defValue) {
        return getInt(index, defValue);
    }

    public float getDimension(int index, float defValue) {
        return getFloat(index, defValue);
    }

    public int getDimensionPixelOffset(int index, int defValue) {
        return getInt(index, defValue);
    }

    public int getDimensionPixelSize(int index, int defValue) {
        int v = getInt(index, defValue);
        return v == 0 ? defValue : v;
    }

    public int getLayoutDimension(int index, int defValue) {
        return getDimensionPixelSize(index, defValue);
    }

    public int getLayoutDimension(int index, String name) {
        return getDimensionPixelSize(index, -1);
    }

    public float getFraction(int index, int base, int pbase, float defValue) {
        return getFloat(index, defValue);
    }

    public int getType(int index) {
        Object v = getValueOrNull(index);
        if (v == null) return TypedValue.TYPE_NULL;
        if (v instanceof Integer) return TypedValue.TYPE_INT_DEC;
        if (v instanceof Float) return TypedValue.TYPE_FLOAT;
        if (v instanceof String) return TypedValue.TYPE_STRING;
        return TypedValue.TYPE_NULL;
    }

    public boolean getValue(int index, TypedValue outValue) {
        Object v = getValueOrNull(index);
        if (v == null) return false;
        if (outValue != null) {
            if (v instanceof Integer) {
                outValue.type = TypedValue.TYPE_INT_DEC;
                outValue.data = (Integer) v;
            } else if (v instanceof Float) {
                outValue.type = TypedValue.TYPE_FLOAT;
                outValue.data = Float.floatToRawIntBits((Float) v);
            } else if (v instanceof String) {
                outValue.type = TypedValue.TYPE_STRING;
                outValue.string = (String) v;
            }
        }
        return true;
    }

    public TypedValue peekValue(int index) {
        TypedValue tv = new TypedValue();
        return getValue(index, tv) ? tv : null;
    }

    public CharSequence[] getTextArray(int index) {
        Object v = getValueOrNull(index);
        return v instanceof CharSequence[] ? (CharSequence[]) v : null;
    }

    public String[] getStringArray(int index) {
        CharSequence[] cs = getTextArray(index);
        if (cs == null) return null;
        String[] out = new String[cs.length];
        for (int i = 0; i < cs.length; i++) {
            out[i] = cs[i] != null ? cs[i].toString() : null;
        }
        return out;
    }

    public int[] getIntArray(int index) {
        Object v = getValueOrNull(index);
        return v instanceof int[] ? (int[]) v : null;
    }

    public boolean hasValue(int index) {
        return index >= 0 && index < this.mLength && this.mRsrcs[index] != 0;
    }

    public Drawable getDrawable(int index) {
        // 资源缝后置：返回 null（AOSP 无资源同值口径）。
        return null;
    }

    public CharSequence getChangingConfigurations() {
        return null;
    }

    public void recycle() {
        this.mRecycled = true;
    }

    public boolean getRecycled() {
        return this.mRecycled;
    }

    @Override
    public String toString() {
        return "TypedArray{" + this.mLength + " attrs}";
    }

    private Object getValueOrNull(int index) {
        if (index < 0 || index >= this.mLength || this.mData == null) return null;
        // 数据布局（简化缝）：每 6 槽一个属性（type,data,...）；type=TYPE_STRING 时
        // data 为字符串表下标（表由资源缝装入 mStrings）。
        int base = index * 6;
        if (base + 1 >= this.mData.length) return null;
        int type = this.mData[base];
        int data = this.mData[base + 1];
        switch (type) {
            case TypedValue.TYPE_STRING:
                return this.mStrings != null && data >= 0 && data < this.mStrings.length
                        ? this.mStrings[data] : null;
            case TypedValue.TYPE_FLOAT:
                return Float.valueOf(Float.intBitsToFloat(data));
            case TypedValue.TYPE_NULL:
                return null;
            default:
                return Integer.valueOf(data);
        }
    }

    String[] mStrings;
}
