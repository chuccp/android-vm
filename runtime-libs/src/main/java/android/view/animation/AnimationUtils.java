package android.view.animation;

import android.content.Context;
import android.os.SystemClock;

/**
 * android.view.animation.AnimationUtils（AOSP 公开面）。
 *
 * <p>时间基走 SystemClock.uptimeMillis（AOSP 同口径）；load* 动画资源在
 * 本 VM 无动画资源管线时返回 null（调用方按「无动画」处理）。
 */
public class AnimationUtils {

    public static long currentAnimationTimeMillis() {
        return SystemClock.uptimeMillis();
    }

    public static Animation loadAnimation(Context context, int id) {
        return null;
    }

    public static Interpolator loadInterpolator(Context context, int id) {
        return null;
    }

    public static LayoutAnimationController loadLayoutAnimation(Context context, int id) {
        return null;
    }

    public static Animation makeInAnimation(Context c, boolean fromLeft) {
        return null;
    }

    public static Animation makeOutAnimation(Context c, boolean toRight) {
        return null;
    }

    public static Animation makeInChildBottomAnimation(Context c) {
        return null;
    }
}
