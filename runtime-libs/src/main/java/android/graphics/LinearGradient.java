package android.graphics;

/**
 * 线性渐变（AOSP android.graphics.LinearGradient 构造面）。
 * 生成补源：RecyclerView item 背景/骨架屏大量使用渐变，类缺失会在
 * measure 内抛 NoClassDefFoundError 中止整窗遍历。
 */
public class LinearGradient extends Shader {

    public LinearGradient(float x0, float y0, float x1, float y1,
                          int color0, int color1, TileMode tile) {
    }

    public LinearGradient(float x0, float y0, float x1, float y1,
                          int[] colors, float[] positions, TileMode tile) {
    }
}
