package android.animation;

import android.content.Context;

/**
 * android.animation.AnimatorInflater（AOSP 公开面）。
 *
 * <p>XML 动画资源解析归桥接动画管线（待落地）；当前返回空 Animator /
 * 空 StateListAnimator，保证 framework/material 构造链不因缺类中断。
 */
public class AnimatorInflater {

    public AnimatorInflater() {
    }

    public static Animator loadAnimator(Context context, int id) {
        return null;
    }

    public static StateListAnimator loadStateListAnimator(Context context, int id) {
        return new StateListAnimator();
    }
}
