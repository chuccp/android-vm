package android.graphics;

/**
 * 区域（AOSP android.graphics.Region；§31.7 C 档 lib 骨架）。
 * 仅 Canvas.clipRect(Rect,Region$Op) 引用面载体；区域布尔语义后置。
 */
public class Region {

    /** 区域运算（AOSP Region.Op 枚举）。 */
    public enum Op {
        DIFFERENCE,
        INTERSECT,
        REPLACE,
        REVERSE_DIFFERENCE,
        UNION,
        XOR
    }

    public Region() {
    }

    public Region(Region region) {
    }

    public Region(Rect r) {
    }

    public boolean isEmpty() {
        return true;
    }

    public void setEmpty() {
    }

    public boolean set(Region region) {
        return true;
    }

    public boolean set(Rect r) {
        return true;
    }

    public boolean set(int left, int top, int right, int bottom) {
        return true;
    }

    public boolean union(Rect r) {
        return true;
    }

    public boolean op(Rect r, Op op) {
        return true;
    }

    public boolean op(Region region, Op op) {
        return true;
    }

    public boolean contains(int x, int y) {
        return false;
    }

    public void getBounds(Rect r) {
        if (r != null) r.setEmpty();
    }

    @Override
    public String toString() {
        return "Region(empty)";
    }
}
