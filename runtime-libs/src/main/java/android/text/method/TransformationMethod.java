package android.text.method;

import android.graphics.Rect;
import android.view.View;

/** android.text.method.TransformationMethod（AOSP 公开接口）。 */
public interface TransformationMethod {
    CharSequence getTransformation(CharSequence source, View view);

    void onFocusChanged(View view, CharSequence sourceText, boolean focused, int direction,
                        Rect previouslyFocusedRect);
}
