package android.view;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.SurfaceTexture;
import android.util.AttributeSet;

/**
 * android.view.TextureView（AOSP 公开面骨架）。
 *
 * <p>AOSP 经 GL 将内容绘制进 SurfaceTexture；本 VM 无 GL 生产通道
 * （视频/PAG 渲染后置），先落公开面：构造、SurfaceTextureListener 注册与
 * 可用性查询（恒不可用）。org.libpag.PAGView 等为其子类。
 */
public class TextureView extends View {

    public interface SurfaceTextureListener {
        void onSurfaceTextureAvailable(SurfaceTexture surface, int width, int height);

        void onSurfaceTextureSizeChanged(SurfaceTexture surface, int width, int height);

        boolean onSurfaceTextureDestroyed(SurfaceTexture surface);

        void onSurfaceTextureUpdated(SurfaceTexture surface);
    }

    private SurfaceTextureListener mListener;
    private SurfaceTexture mSurface;
    private boolean mOpaque = true;

    public TextureView(Context context) {
        super(context);
    }

    public TextureView(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public TextureView(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setSurfaceTextureListener(SurfaceTextureListener listener) {
        mListener = listener;
    }

    public SurfaceTextureListener getSurfaceTextureListener() {
        return mListener;
    }

    public SurfaceTexture getSurfaceTexture() {
        return mSurface;
    }

    public void setSurfaceTexture(SurfaceTexture surfaceTexture) {
        mSurface = surfaceTexture;
    }

    public boolean isAvailable() {
        return false;
    }

    public void setOpaque(boolean opaque) {
        mOpaque = opaque;
    }

    public boolean isOpaque() {
        return mOpaque;
    }

    public Bitmap getBitmap(Bitmap bitmap) {
        return bitmap;
    }

    public Bitmap getBitmap() {
        return null;
    }
}
