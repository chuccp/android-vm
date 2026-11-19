package android.text.method;

import android.graphics.Rect;
import android.view.View;

/**
 * android.text.method.PasswordTransformationMethod（AOSP 公开面）。
 *
 * <p>真实 AOSP 将字符替换为密码点；本 VM 的文本渲染面暂不区分掩码，
 * getTransformation 原样返回（不影响布局测量语义）。单例面齐备。
 */
public class PasswordTransformationMethod implements TransformationMethod {

    private static final PasswordTransformationMethod sInstance =
            new PasswordTransformationMethod();

    public static TransformationMethod getInstance() {
        return sInstance;
    }

    @Override
    public CharSequence getTransformation(CharSequence source, View view) {
        return source;
    }

    @Override
    public void onFocusChanged(View view, CharSequence sourceText, boolean focused, int direction,
                               Rect previouslyFocusedRect) {
    }
}
