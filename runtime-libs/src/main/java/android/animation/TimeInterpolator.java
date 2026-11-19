package android.animation;

/**
 * 时间插值器接口（AOSP android.animation.TimeInterpolator；§31.7 C 档 lib 接口面）。
 */
public interface TimeInterpolator {
    float getInterpolation(float input);
}
