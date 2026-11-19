package android.graphics;

/**
 * android.graphics.SurfaceTexture（AOSP 公开面骨架）。
 *
 * <p>纹理/帧回调由 GL 消费面提供；本 VM 无 GL 生产通道（视频渲染后置），
 * 先落公开面：构造、监听注册、尺寸/矩阵/时间戳查询空面语义。
 */
public class SurfaceTexture {

    public interface OnFrameAvailableListener {
        void onFrameAvailable(SurfaceTexture surfaceTexture);
    }

    public interface OnAttachedToWindowListener {
        void onAttachedToWindow(SurfaceTexture surfaceTexture);
    }

    private OnFrameAvailableListener mOnFrameAvailableListener;
    private int mWidth;
    private int mHeight;

    public SurfaceTexture(int texName) {
    }

    public SurfaceTexture(int texName, boolean singleBufferMode) {
    }

    public SurfaceTexture(boolean singleBufferMode) {
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener) {
        mOnFrameAvailableListener = listener;
    }

    public void setOnFrameAvailableListener(OnFrameAvailableListener listener,
                                            android.os.Handler handler) {
        mOnFrameAvailableListener = listener;
    }

    public void setDefaultBufferSize(int width, int height) {
        mWidth = width;
        mHeight = height;
    }

    public void updateTexImage() {
    }

    public void releaseTexImage() {
    }

    public void detachFromGLContext() {
    }

    public void attachToGLContext(int texName) {
    }

    public void getTransformMatrix(float[] mtx) {
        if (mtx != null && mtx.length >= 16) {
            for (int i = 0; i < 16; i++) mtx[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        }
    }

    public long getTimestamp() {
        return 0L;
    }

    public void release() {
    }

    public boolean isReleased() {
        return false;
    }
}
