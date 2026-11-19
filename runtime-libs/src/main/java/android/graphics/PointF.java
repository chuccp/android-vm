package android.graphics;

/**
 * 浮点点（AOSP android.graphics.PointF 公开面；§31.7 C 档 lib POJO）。
 */
public class PointF {
    public float x;
    public float y;

    public PointF() {
    }

    public PointF(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public PointF(PointF p) {
        if (p != null) {
            this.x = p.x;
            this.y = p.y;
        }
    }

    public PointF(Point p) {
        if (p != null) {
            this.x = p.x;
            this.y = p.y;
        }
    }

    public void set(float x, float y) {
        this.x = x;
        this.y = y;
    }

    public void set(PointF p) {
        if (p != null) set(p.x, p.y);
    }

    public void set(Point p) {
        if (p != null) set(p.x, p.y);
    }

    public void negate() {
        this.x = -this.x;
        this.y = -this.y;
    }

    public void offset(float dx, float dy) {
        this.x += dx;
        this.y += dy;
    }

    public void scale(float scale) {
        this.x *= scale;
        this.y *= scale;
    }

    public final float length() {
        return (float) Math.sqrt(this.x * this.x + this.y * this.y);
    }

    public static float length(float x, float y) {
        return (float) Math.sqrt(x * x + y * y);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof PointF)) return false;
        PointF p = (PointF) o;
        return this.x == p.x && this.y == p.y;
    }

    @Override
    public int hashCode() {
        return 31 * Float.floatToIntBits(this.x) + Float.floatToIntBits(this.y);
    }

    @Override
    public String toString() {
        return "PointF(" + this.x + ", " + this.y + ")";
    }
}
