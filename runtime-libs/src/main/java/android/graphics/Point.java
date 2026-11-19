package android.graphics;

/**
 * 整型点（AOSP android.graphics.Point 公开面；§31.7 C 档 lib POJO）。
 */
public class Point {
    public int x;
    public int y;

    public Point() {
    }

    public Point(int x, int y) {
        this.x = x;
        this.y = y;
    }

    public Point(Point p) {
        if (p != null) {
            this.x = p.x;
            this.y = p.y;
        }
    }

    public void set(int x, int y) {
        this.x = x;
        this.y = y;
    }

    public void negate() {
        this.x = -this.x;
        this.y = -this.y;
    }

    public void offset(int dx, int dy) {
        this.x += dx;
        this.y += dy;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Point)) return false;
        Point p = (Point) o;
        return this.x == p.x && this.y == p.y;
    }

    @Override
    public int hashCode() {
        return 31 * this.x + this.y;
    }

    @Override
    public String toString() {
        return "Point(" + this.x + ", " + this.y + ")";
    }
}
