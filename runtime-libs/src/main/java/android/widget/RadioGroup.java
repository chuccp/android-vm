package android.widget;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;

/**
 * 单选组（AOSP android.widget.RadioGroup 公开面；§31.7 C 档 lib）。
 *
 * 垂直 LinearLayout 语义（AOSP 默认 VERTICAL）；互斥检查状态由子
 * RadioButton 的 OnCheckedChangeListener 维护。子项登记走 ViewGroup
 * addView 的 onViewAdded 钩子（inflater/程序化 addView 同一条路径）。
 */
public class RadioGroup extends LinearLayout {

    public interface OnCheckedChangeListener {
        void onCheckedChanged(RadioGroup group, int checkedId);
    }

    private int mCheckedId = -1;
    private OnCheckedChangeListener mOnCheckedChangeListener;

    private final CompoundButton.OnCheckedChangeListener mChildListener =
            new CompoundButton.OnCheckedChangeListener() {
                @Override
                public void onCheckedChanged(CompoundButton buttonView, boolean isChecked) {
                    if (isChecked) {
                        // AOSP 语义：子按钮选中 -> RadioGroup.check 反选旧的选中项。
                        check(buttonView.getId());
                    }
                }
            };

    public RadioGroup(Context context) {
        super(context);
        setOrientation(VERTICAL);
    }

    public RadioGroup(Context context, AttributeSet attrs) {
        super(context, attrs);
        setOrientation(VERTICAL);
    }

    public RadioGroup(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        setOrientation(VERTICAL);
    }

    @Override
    public void onViewAdded(View child) {
        super.onViewAdded(child);
        if (child instanceof RadioButton) {
            RadioButton radio = (RadioButton) child;
            radio.setOnCheckedChangeListener(this.mChildListener);
            if (this.mCheckedId == -1 && radio.isChecked()) {
                this.mCheckedId = radio.getId();
            }
        }
    }

    public void check(int id) {
        if (id != -1 && id == this.mCheckedId) {
            return;
        }
        if (this.mCheckedId != -1) {
            RadioButton previous = findRadio(this.mCheckedId);
            if (previous != null) {
                previous.setChecked(false);
            }
        }
        RadioButton next = id != -1 ? findRadio(id) : null;
        if (next != null) {
            if (!next.isChecked()) {
                next.setChecked(true);
            } else {
                setCheckedId(id);
            }
        } else {
            setCheckedId(-1);
        }
    }

    public void clearCheck() {
        check(-1);
    }

    public int getCheckedRadioButtonId() {
        return this.mCheckedId;
    }

    public void setOnCheckedChangeListener(OnCheckedChangeListener listener) {
        this.mOnCheckedChangeListener = listener;
    }

    private RadioButton findRadio(int id) {
        int count = getChildCount();
        for (int i = 0; i < count; i++) {
            View child = getChildAt(i);
            if (child instanceof RadioButton && child.getId() == id) {
                return (RadioButton) child;
            }
        }
        return null;
    }

    private void setCheckedId(int id) {
        this.mCheckedId = id;
        if (this.mOnCheckedChangeListener != null) {
            this.mOnCheckedChangeListener.onCheckedChanged(this, id);
        }
    }
}
