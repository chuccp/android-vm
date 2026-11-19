package android.graphics.drawable;

import android.content.Context;
import android.net.Uri;

/**
 * 图标（AOSP android.graphics.drawable.Icon 公开面；§31.7 C 档 lib POJO 载体）。
 */
public final class Icon {

    public static final int TYPE_UNKNOWN = -1;
    public static final int TYPE_BITMAP = 1;
    public static final int TYPE_RESOURCE = 2;
    public static final int TYPE_DATA = 3;
    public static final int TYPE_URI = 4;
    public static final int TYPE_ADAPTIVE_BITMAP = 5;

    private final int mType;
    private Object mData;

    private Icon(int type, Object data) {
        this.mType = type;
        this.mData = data;
    }

    public static Icon createWithResource(Context context, int resId) {
        return new Icon(TYPE_RESOURCE, Integer.valueOf(resId));
    }

    public static Icon createWithResource(String pkg, int resId) {
        return new Icon(TYPE_RESOURCE, Integer.valueOf(resId));
    }

    public static Icon createWithBitmap(android.graphics.Bitmap bitmap) {
        return new Icon(TYPE_BITMAP, bitmap);
    }

    public static Icon createWithAdaptiveBitmap(android.graphics.Bitmap bitmap) {
        return new Icon(TYPE_ADAPTIVE_BITMAP, bitmap);
    }

    public static Icon createWithData(byte[] data, int offset, int length) {
        return new Icon(TYPE_DATA, data);
    }

    public static Icon createWithContentUri(Uri uri) {
        return new Icon(TYPE_URI, uri);
    }

    public static Icon createWithContentUri(String uri) {
        return new Icon(TYPE_URI, uri);
    }

    public int getType() {
        return this.mType;
    }

    public Drawable loadDrawable(Context context) {
        return null;
    }

    @Override
    public String toString() {
        return "Icon(typ=" + this.mType + ")";
    }
}
