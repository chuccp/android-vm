package android.text.style;

import android.text.TextPaint;

/**
 * 字符级 span 基类（AOSP android.text.style.CharacterStyle 形状）。
 * 生成补源：启动期 app 工具类 <clinit> 引用 StyleSpan 依赖链
 * （CharacterStyle -> MetricAffectingSpan -> StyleSpan）。
 */
public abstract class CharacterStyle {

    public abstract void updateDrawState(TextPaint tp);

    /** AOSP：包装为可复用形态；本实现直接返回原对象。 */
    public static CharacterStyle wrap(CharacterStyle cs) {
        return cs;
    }

    /** AOSP：取底层 span；默认自身。 */
    public CharacterStyle getUnderlying() {
        return this;
    }
}
