package android.os;

/**
 * 取消信号（AOSP android.os.CancellationSignal；§31.7 C 档 lib——取消态 + 监听）。
 */
public class CancellationSignal {

    public interface OnCancelListener {
        void onCancel();
    }

    private boolean mIsCanceled;
    private OnCancelListener mOnCancelListener;

    public CancellationSignal() {
    }

    public boolean isCanceled() {
        return this.mIsCanceled;
    }

    public void cancel() {
        synchronized (this) {
            if (this.mIsCanceled) return;
            this.mIsCanceled = true;
            if (this.mOnCancelListener != null) this.mOnCancelListener.onCancel();
        }
    }

    public void setOnCancelListener(OnCancelListener listener) {
        synchronized (this) {
            this.mOnCancelListener = listener;
        }
    }

    public void throwIfCanceled() {
        if (this.mIsCanceled) {
            throw new OperationCanceledException("canceled");
        }
    }
}
