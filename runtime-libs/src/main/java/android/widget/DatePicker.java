package android.widget;

import android.content.Context;
import android.util.AttributeSet;

/**
 * 日期选择器（AOSP android.widget.DatePicker 公开面；§31.7 C 档 lib——宿主
 * 物化层换成宿主真 DatePicker 出画）。
 *
 * <p>guest 侧维护年月日状态与 OnDateChangedListener 语义；宿主日历改动经
 * onHostDateChanged 回灌（宿主侧抑制快照回写触发的回声）。
 */
public class DatePicker extends FrameLayout {

    public interface OnDateChangedListener {
        void onDateChanged(DatePicker view, int year, int monthOfYear, int dayOfMonth);
    }

    private int mYear;
    private int mMonth;
    private int mDay;
    private OnDateChangedListener mOnDateChangedListener;

    public DatePicker(Context context) {
        super(context);
    }

    public DatePicker(Context context, AttributeSet attrs) {
        super(context, attrs);
    }

    public DatePicker(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
    }

    public void init(int year, int monthOfYear, int dayOfMonth,
                     OnDateChangedListener onDateChangedListener) {
        this.mYear = year;
        this.mMonth = monthOfYear;
        this.mDay = dayOfMonth;
        this.mOnDateChangedListener = onDateChangedListener;
    }

    public void updateDate(int year, int monthOfYear, int dayOfMonth) {
        this.mYear = year;
        this.mMonth = monthOfYear;
        this.mDay = dayOfMonth;
    }

    public int getYear() {
        return this.mYear;
    }

    public int getMonth() {
        return this.mMonth;
    }

    public int getDayOfMonth() {
        return this.mDay;
    }

    public void setOnDateChangedListener(OnDateChangedListener listener) {
        this.mOnDateChangedListener = listener;
    }

    /** 宿主 DatePicker 改动回灌（UI 桥 intrinsic 调用，非 AOSP 公开面）。 */
    public void onHostDateChanged(int year, int monthOfYear, int dayOfMonth) {
        this.mYear = year;
        this.mMonth = monthOfYear;
        this.mDay = dayOfMonth;
        if (mOnDateChangedListener != null) {
            mOnDateChangedListener.onDateChanged(this, year, monthOfYear, dayOfMonth);
        }
    }

    /** 通用字段回灌入口（fields 通道；宿主按控件类型调用）。 */
    public void onHostFieldsChanged(int a, int b, int c) {
        onHostDateChanged(a, b, c);
    }
}
