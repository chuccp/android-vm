package android.graphics;

/**
 * 嵌入矩形（AOSP android.graphics.Insets；§31.7 C 档 lib POJO）。
 */
public final class Insets {
    public static final Insets NONE = new Insets(0, 0, 0, 0);

    public final int left;
    public final int top;
    public final int right;
    public final int bottom;

    private Insets(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public static Insets of(int left, int top, int right, int bottom) {
        return new Insets(left, top, right, bottom);
    }

    public static Insets of(Rect r) {
        return r != null ? new Insets(r.left, r.top, r.right, r.bottom) : NONE;
    }

    public static Insets add(Insets a, Insets b) {
        return new Insets(a.left + b.left, a.top + b.top, a.right + b.right, a.bottom + b.bottom);
    }

    public static Insets subtract(Insets a, Insets b) {
        return new Insets(a.left - b.left, a.top - b.top, a.right - b.right, a.bottom - b.bottom);
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Insets)) return false;
        Insets i = (Insets) o;
        return this.left == i.left && this.top == i.top && this.right == i.right
                && this.bottom == i.bottom;
    }

    @Override
    public int hashCode() {
        return ((this.left * 31 + this.top) * 31 + this.right) * 31 + this.bottom;
    }

    @Override
    public String toString() {
        return "Insets{" + this.left + ", " + this.top + ", " + this.right + ", "
                + this.bottom + "}";
    }
}
