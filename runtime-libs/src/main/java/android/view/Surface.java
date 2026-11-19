package android.view;

import android.graphics.Canvas;
import android.graphics.Rect;
import android.graphics.SurfaceTexture;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * android.view.Surface（AOSP 公开面骨架）。
 *
 * <p>AOSP 通过 native Surface 与 BufferQueue 交互；本 VM 暂无生产/消费
 * 通道（视频/相机渲染后置），先落公开面：构造、isValid/release、
 * lockCanvas/unlockCanvasAndPost 返回 null 空面（调用方按无效 Surface 处理）。
 */
public class Surface implements Parcelable {
    public static final int ROTATION_0 = 0;
    public static final int ROTATION_90 = 1;
    public static final int ROTATION_180 = 2;
    public static final int ROTATION_270 = 3;

    public Surface() {
    }

    public Surface(SurfaceTexture surfaceTexture) {
    }

    public boolean isValid() {
        return false;
    }

    public void release() {
    }

    public Canvas lockCanvas(Rect dirty) {
        return null;
    }

    public Canvas lockHardwareCanvas() {
        return null;
    }

    public void unlockCanvasAndPost(Canvas canvas) {
    }

    public void setFrameRate(float frameRate, int compatibility) {
    }

    public String toString() {
        return "Surface (invalid)";
    }

    // ---- Parcelable ----

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }

    public static final Parcelable.Creator<Surface> CREATOR =
            new Parcelable.Creator<Surface>() {
                @Override
                public Surface createFromParcel(Parcel in) {
                    return new Surface();
                }

                @Override
                public Surface[] newArray(int size) {
                    return new Surface[size];
                }
            };
}
