package android.text;

/**
 * android.text.TextWatcher（AOSP 公开接口）：文本编辑观察者。
 */
public interface TextWatcher extends NoCopySpan {
    void beforeTextChanged(CharSequence s, int start, int count, int after);

    void onTextChanged(CharSequence s, int start, int before, int count);

    void afterTextChanged(Editable s);
}
