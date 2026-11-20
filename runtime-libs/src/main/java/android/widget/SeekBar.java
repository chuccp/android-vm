package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 拖动条（AOSP android.widget.SeekBar 公开面；§31.7 C 档 lib）。
 *
 * 骨架面 = ProgressBar 进度状态 + max + OnSeekBarChangeListener；轨道/thumb
 * 绘制由宿主物化（Android 原生 SeekBar）承担。宿主拖拽经 UI 桥回灌到
 * onHost* 方法：进度按 fromUser=true 通知，起止跟踪按 AOSP 语义通知。
 */
public class SeekBar extends ProgressBar {

    public interface OnSeekBarChangeListener {
        void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser);

        void onStartTrackingTouch(SeekBar seekBar);

        void onStopTrackingTouch(SeekBar seekBar);
    }

    private int mMax = 100;
    private OnSeekBarChangeListener mOnSeekBarChangeListener;

    public SeekBar(Context context) {
        super(context);
    }

    public SeekBar(Context context, AttributeSet attrs) {
        this(context);
    }

    public SeekBar(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public void setMax(int max) {
        if (max < 0) {
            max = 0;
        }
        this.mMax = max;
        if (getProgress() > max) {
            setProgress(max);
        }
    }

    public int getMax() {
        return this.mMax;
    }

    @Override
    public void setProgress(int progress) {
        int clamped = progress;
        if (clamped < 0) {
            clamped = 0;
        }
        if (clamped > this.mMax) {
            clamped = this.mMax;
        }
        if (clamped != getProgress()) {
            super.setProgress(clamped);
            if (this.mOnSeekBarChangeListener != null) {
                this.mOnSeekBarChangeListener.onProgressChanged(this, clamped, false);
            }
        }
    }

    public void setOnSeekBarChangeListener(OnSeekBarChangeListener listener) {
        this.mOnSeekBarChangeListener = listener;
    }

    /**
     * 宿主拖拽回灌（UI 桥 intrinsic 调用，非 AOSP 公开面）：按 AOSP
     * onProgressChanged(fromUser=true) 语义通知 guest 监听器。
     */
    public void onHostProgressChanged(int progress) {
        int clamped = progress;
        if (clamped < 0) {
            clamped = 0;
        }
        if (clamped > this.mMax) {
            clamped = this.mMax;
        }
        if (clamped != getProgress()) {
            super.setProgress(clamped);
        }
        if (this.mOnSeekBarChangeListener != null) {
            this.mOnSeekBarChangeListener.onProgressChanged(this, clamped, true);
        }
    }

    /** 宿主拖拽开始回灌（AOSP onStartTrackingTouch）。 */
    public void onHostStartTrackingTouch() {
        if (this.mOnSeekBarChangeListener != null) {
            this.mOnSeekBarChangeListener.onStartTrackingTouch(this);
        }
    }

    /** 宿主拖拽结束回灌（AOSP onStopTrackingTouch）。 */
    public void onHostStopTrackingTouch() {
        if (this.mOnSeekBarChangeListener != null) {
            this.mOnSeekBarChangeListener.onStopTrackingTouch(this);
        }
    }
}
