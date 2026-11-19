package android.graphics;

import java.io.File;
import java.io.FileDescriptor;
import java.io.InputStream;

/**
 * 位图解码（AOSP android.graphics.BitmapFactory；§31.7 C 档 lib 骨架）。
 * 真解码后置：返回 1×1 语义占位（调用方可测宽高/像素；缺口显式记录在报告）。
 */
public class BitmapFactory {

    public static class Options {
        public Bitmap.Config inPreferredConfig = Bitmap.Config.ARGB_8888;
        public boolean inJustDecodeBounds;
        public int inSampleSize = 1;
        public boolean inScaled = true;
        public boolean inPremultiplied = true;
        public int outWidth;
        public int outHeight;
        public String outMimeType;
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length) {
        return placeholder();
    }

    public static Bitmap decodeByteArray(byte[] data, int offset, int length, Options opts) {
        return placeholder();
    }

    public static Bitmap decodeFile(String pathName) {
        return placeholder();
    }

    public static Bitmap decodeFile(String pathName, Options opts) {
        return placeholder();
    }

    public static Bitmap decodeStream(InputStream is) {
        return placeholder();
    }

    public static Bitmap decodeStream(InputStream is, Rect outPadding, Options opts) {
        return placeholder();
    }

    public static Bitmap decodeFileDescriptor(FileDescriptor fd) {
        return placeholder();
    }

    public static Bitmap decodeFileDescriptor(FileDescriptor fd, Rect outPadding, Options opts) {
        return placeholder();
    }

    public static Bitmap decodeResource(android.content.res.Resources res, int id) {
        return placeholder();
    }

    public static Bitmap decodeResource(android.content.res.Resources res, int id, Options opts) {
        return placeholder();
    }

    public static Bitmap decodeResourceStream(android.content.res.Resources res,
                                              android.util.TypedValue value,
                                              InputStream is, Rect pad, Options opts) {
        return placeholder();
    }

    private static Bitmap placeholder() {
        return Bitmap.createBitmap(1, 1, Bitmap.Config.ARGB_8888);
    }
}
