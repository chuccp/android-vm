package android.view;

import android.content.Context;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * android.view.PointerIcon（API 24 公开面，§31.7 B 档 POJO→lib）。
 *
 * <p>AOSP 该类真正加载系统光标位图，本 VM 无宿主光标通道：类型常量与
 * create/load/getSystemIcon 构造面按 Android 签名保留，内部仅携带
 * style type（androidx.core.view.PointerIconCompat 等以类型/id 传递，
 * 不做位图）。宿主 View 侧 setPointerIcon 语义为无害空操作。
 */
public final class PointerIcon implements Parcelable {

    /** Type constant: Null icon. It has no bitmap. */
    public static final int TYPE_NULL = 0;
    /** Type constant: Arrow icon. (Default) */
    public static final int TYPE_DEFAULT = 1000;
    /** Type constant: Arrow icon. */
    public static final int TYPE_ARROW = 1000;
    /** Type constant: Context menu icon. */
    public static final int TYPE_CONTEXT_MENU = 1001;
    /** Type constant: Hand icon. */
    public static final int TYPE_HAND = 1002;
    /** Type constant: Help icon. */
    public static final int TYPE_HELP = 1003;
    /** Type constant: Wait icon (usually a spinning circle). */
    public static final int TYPE_WAIT = 1004;
    /** Type constant: Cell icon. */
    public static final int TYPE_CELL = 1006;
    /** Type constant: Crosshair icon. */
    public static final int TYPE_CROSSHAIR = 1007;
    /** Type constant: Text icon (I-beam). */
    public static final int TYPE_TEXT = 1008;
    /** Type constant: Vertical text icon. */
    public static final int TYPE_VERTICAL_TEXT = 1009;
    /** Type constant: Alias (link) icon. */
    public static final int TYPE_ALIAS = 1010;
    /** Type constant: Copy icon. */
    public static final int TYPE_COPY = 1011;
    /** Type constant: "No drop" icon. */
    public static final int TYPE_NO_DROP = 1012;
    /** Type constant: "All scroll" icon. */
    public static final int TYPE_ALL_SCROLL = 1013;
    /** Type constant: Horizontal double arrow icon. */
    public static final int TYPE_HORIZONTAL_DOUBLE_ARROW = 1014;
    /** Type constant: Vertical double arrow icon. */
    public static final int TYPE_VERTICAL_DOUBLE_ARROW = 1015;
    /** Type constant: Diagonal double arrow icon (top-right to bottom-left). */
    public static final int TYPE_TOP_RIGHT_DIAGONAL_DOUBLE_ARROW = 1016;
    /** Type constant: Diagonal double arrow icon (top-left to bottom-right). */
    public static final int TYPE_TOP_LEFT_DIAGONAL_DOUBLE_ARROW = 1017;
    /** Type constant: Zoom in icon. */
    public static final int TYPE_ZOOM_IN = 1018;
    /** Type constant: Zoom out icon. */
    public static final int TYPE_ZOOM_OUT = 1019;
    /** Type constant: Grab icon. */
    public static final int TYPE_GRAB = 1020;
    /** Type constant: Grabbing icon. */
    public static final int TYPE_GRABBING = 1021;

    private int mType;
    private Bitmap mBitmap;
    private float mHotSpotX;
    private float mHotSpotY;

    private PointerIcon(int type) {
        mType = type;
    }

    /**
     * Gets a system pointer icon for the given type.
     * If type is not recognized, returns the default pointer icon.
     */
    public static PointerIcon getSystemIcon(Context context, int type) {
        return new PointerIcon(type);
    }

    /** Creates a custom pointer icon from a bitmap and hot spot. */
    public static PointerIcon create(Bitmap bitmap, float hotSpotX, float hotSpotY) {
        PointerIcon icon = new PointerIcon(TYPE_CUSTOM);
        icon.mBitmap = bitmap;
        icon.mHotSpotX = hotSpotX;
        icon.mHotSpotY = hotSpotY;
        return icon;
    }

    /** Loads a pointer icon from drawable resources. */
    public static PointerIcon load(Resources resources, int resourceId) {
        return new PointerIcon(TYPE_CUSTOM);
    }

    /** AOSP internal: custom bitmap-backed pointer type. */
    private static final int TYPE_CUSTOM = -1;

    public int getType() {
        return mType;
    }

    // ---- Parcelable 面 ----

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
        out.writeInt(mType);
    }

    public static final Parcelable.Creator<PointerIcon> CREATOR =
            new Parcelable.Creator<PointerIcon>() {
                @Override
                public PointerIcon createFromParcel(Parcel in) {
                    return new PointerIcon(in.readInt());
                }

                @Override
                public PointerIcon[] newArray(int size) {
                    return new PointerIcon[size];
                }
            };

    @Override
    public boolean equals(Object other) {
        if (this == other) {
            return true;
        }
        if (other == null || getClass() != other.getClass()) {
            return false;
        }
        return mType == ((PointerIcon) other).mType;
    }

    @Override
    public int hashCode() {
        return mType;
    }
}
