package android.text;

/**
 * android.text.Editable（AOSP 公开接口的最小语义面）。
 *
 * <p>AOSP 完整面含 replace/insert/delete/append 链与 Factory；本 VM 先落
 * 层级（CharSequence/GetChars/Spannable/Appendable）+ 常用编辑方法签名，
 * 具体编辑行为由实现类（SpannableStringBuilder 等）承接。链上暴露更多
 * 方法面时按同法逐轮补。
 */
public interface Editable extends CharSequence, GetChars, Spannable, Appendable {
    Editable replace(CharSequence source, int start, int end, int destStart, int destEnd);

    Editable replace(int st, int en, CharSequence text);

    Editable insert(int where, CharSequence text, int start, int end);

    Editable insert(int where, CharSequence text);

    Editable delete(int st, int en);

    @Override
    Editable append(CharSequence text);

    @Override
    Editable append(CharSequence text, int start, int end);

    @Override
    Editable append(char text);

    void clearSpans();

    /**
     * AOSP：Editable 工厂（默认实现创建 SpannableStringBuilder）。
     * 壳层 ClickableSpan 工厂（b0.e.a.b 等）直接继承本类并覆写
     * newEditable——缺本类会让整条继承链 define/link 失败。
     */
    class Factory {
        public Factory() {
        }

        public Editable newEditable(CharSequence source) {
            return new SpannableStringBuilder(source);
        }
    }
}
