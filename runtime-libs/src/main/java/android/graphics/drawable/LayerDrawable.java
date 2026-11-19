package android.graphics.drawable;

/**
 * 图层可绘制（AOSP android.graphics.drawable.LayerDrawable 公开面；
 * §31.7 C 档 lib 骨架——层叠 Drawable 面）。
 */
public class LayerDrawable extends Drawable {

    private final java.util.ArrayList<Drawable> mLayers = new java.util.ArrayList<Drawable>();

    public LayerDrawable(Drawable[] layers) {
        if (layers != null) {
            for (Drawable d : layers) {
                this.mLayers.add(d);
            }
        }
    }

    public int getNumberOfLayers() {
        return this.mLayers.size();
    }

    public Drawable getDrawable(int index) {
        return this.mLayers.get(index);
    }

    public void setDrawableByLayerId(int id, Drawable drawable) {
    }

    public int getId(int index) {
        return 0;
    }

    public void setId(int index, int id) {
    }

    public int findDrawableByLayerId(int id) {
        return -1;
    }

    @Override
    public void draw(android.graphics.Canvas canvas) {
        for (Drawable d : this.mLayers) {
            d.draw(canvas);
        }
    }

    @Override
    public void setAlpha(int alpha) {
        for (Drawable d : this.mLayers) {
            d.setAlpha(alpha);
        }
    }

    @Override
    public void setColorFilter(android.graphics.ColorFilter cf) {
        for (Drawable d : this.mLayers) {
            d.setColorFilter(cf);
        }
    }

    @Override
    public int getIntrinsicWidth() {
        return this.mLayers.isEmpty() ? -1 : this.mLayers.get(0).getIntrinsicWidth();
    }

    @Override
    public int getIntrinsicHeight() {
        return this.mLayers.isEmpty() ? -1 : this.mLayers.get(0).getIntrinsicHeight();
    }

    @Override
    public ConstantState getConstantState() {
        return null;
    }
}
