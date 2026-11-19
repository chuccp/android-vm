package android.animation;

/**
 * 对象属性动画（AOSP android.animation.ObjectAnimator 公开面；§31.7 C 档 lib 骨架）。
 */
public class ObjectAnimator extends ValueAnimator {

    public ObjectAnimator() {
    }

    public static ObjectAnimator ofFloat(Object target, String propertyName, float... values) {
        ObjectAnimator a = new ObjectAnimator();
        a.setTarget(target);
        return a;
    }

    public static ObjectAnimator ofInt(Object target, String propertyName, int... values) {
        ObjectAnimator a = new ObjectAnimator();
        a.setTarget(target);
        return a;
    }

    public static ObjectAnimator ofObject(Object target, String propertyName,
                                          TypeEvaluator evaluator, Object... values) {
        ObjectAnimator a = new ObjectAnimator();
        a.setTarget(target);
        return a;
    }

    public void setPropertyName(String propertyName) {
    }

    public String getPropertyName() {
        return null;
    }

    @Override
    public void setTarget(Object target) {
    }
}
