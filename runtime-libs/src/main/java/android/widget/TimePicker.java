package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 时间选择器（AOSP android.widget.TimePicker 公开面；§31.7 C 档 lib——宿主
 * 物化层换成宿主真 TimePicker 出画）。
 *
 * <p>guest 侧维护时分状态与 OnTimeChangedListener 语义；宿主时钟/输入改动经
 * onHostTimeChanged 回灌。
 */
public class TimePicker extends FrameLayout {

    public interface OnTimeChangedListener {
        void onTimeChanged(TimePicker view, int hourOfDay, int minute);
    }

    private int mHour;
    private int mMinute;
    private OnTimeChangedListener mOnTimeChangedListener;

    public TimePicker(Context context) {
        super(context);
    }

    public TimePicker(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public TimePicker(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void setOnTimeChangedListener(OnTimeChangedListener listener) {
        this.mOnTimeChangedListener = listener;
    }

    public void setCurrentHour(Integer currentHour) {
        this.mHour = currentHour == null ? 0 : currentHour;
    }

    public Integer getCurrentHour() {
        return this.mHour;
    }

    public void setCurrentMinute(Integer currentMinute) {
        this.mMinute = currentMinute == null ? 0 : currentMinute;
    }

    public Integer getCurrentMinute() {
        return this.mMinute;
    }

    public void setHour(int hour) {
        this.mHour = hour;
    }

    public int getHour() {
        return this.mHour;
    }

    public void setMinute(int minute) {
        this.mMinute = minute;
    }

    public int getMinute() {
        return this.mMinute;
    }

    public void setIs24HourView(Boolean is24HourView) {
    }

    public boolean is24HourView() {
        return true;
    }

    /** 宿主 TimePicker 改动回灌（UI 桥 intrinsic 调用，非 AOSP 公开面）。 */
    public void onHostTimeChanged(int hourOfDay, int minute) {
        this.mHour = hourOfDay;
        this.mMinute = minute;
        if (mOnTimeChangedListener != null) {
            mOnTimeChangedListener.onTimeChanged(this, hourOfDay, minute);
        }
    }

    /** 通用字段回灌入口（fields 通道；宿主按控件类型调用）。 */
    public void onHostFieldsChanged(int a, int b, int c) {
        onHostTimeChanged(a, b);
    }
}
