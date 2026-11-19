package android.text;

/**
 * android.text.GetChars（AOSP 公开接口）：把字符区间拷到目标数组。
 */
public interface GetChars extends CharSequence {
    void getChars(int start, int end, char[] dest, int destoff);
}
