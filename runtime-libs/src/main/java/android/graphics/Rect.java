package android.graphics;

/**
 * 整型矩形（AOSP android.graphics.Rect 公开面；§31.7 C 档 lib POJO）。
 *
 * <p>公开字段 left/top/right/bottom 直读直写（业务码 iget/iput 同名同型）；
 * 几何方法按 AOSP 语义（width=right-left 等）。
 */
public final class Rect {
    public int left;
    public int top;
    public int right;
    public int bottom;

    public Rect() {
    }

    public Rect(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public Rect(Rect r) {
        if (r != null) {
            this.left = r.left;
            this.top = r.top;
            this.right = r.right;
            this.bottom = r.bottom;
        }
    }

    public void set(int left, int top, int right, int bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public void set(Rect src) {
        if (src != null) {
            this.left = src.left;
            this.top = src.top;
            this.right = src.right;
            this.bottom = src.bottom;
        }
    }

    public void setEmpty() {
        this.left = 0;
        this.top = 0;
        this.right = 0;
        this.bottom = 0;
    }

    public int width() {
        return this.right - this.left;
    }

    public int height() {
        return this.bottom - this.top;
    }

    public int centerX() {
        return (this.left + this.right) >> 1;
    }

    public int centerY() {
        return (this.top + this.bottom) >> 1;
    }

    public float exactCenterX() {
        return (this.left + this.right) * 0.5f;
    }

    public float exactCenterY() {
        return (this.top + this.bottom) * 0.5f;
    }

    public boolean isEmpty() {
        return this.left >= this.right || this.top >= this.bottom;
    }

    public boolean contains(int x, int y) {
        return this.left < this.right && this.top < this.bottom
                && x >= this.left && x < this.right && y >= this.top && y < this.bottom;
    }

    public boolean contains(int left, int top, int right, int bottom) {
        return this.left < this.right && this.top < this.bottom
                && this.left <= left && this.top <= top
                && this.right >= right && this.bottom >= bottom;
    }

    public boolean contains(Rect r) {
        return r != null && contains(r.left, r.top, r.right, r.bottom);
    }

    public void inset(int dx, int dy) {
        this.left += dx;
        this.top += dy;
        this.right -= dx;
        this.bottom -= dy;
    }

    /** AOSP 语义：left+=l, top+=t, right-=r, bottom-=b。 */
    public void inset(int l, int t, int r, int b) {
        this.left += l;
        this.top += t;
        this.right -= r;
        this.bottom -= b;
    }

    public void inset(Rect inset) {
        if (inset != null) inset(inset.left, inset.top, -inset.right, -inset.bottom);
    }

    public boolean intersect(int left, int top, int right, int bottom) {
        if (this.left < right && left < this.right && this.top < bottom && top < this.bottom) {
            if (this.left < left) this.left = left;
            if (this.top < top) this.top = top;
            if (this.right > right) this.right = right;
            if (this.bottom > bottom) this.bottom = bottom;
            return true;
        }
        return false;
    }

    public boolean intersect(Rect r) {
        return r != null && intersect(r.left, r.top, r.right, r.bottom);
    }

    public boolean setIntersect(Rect a, Rect b) {
        if (a == null || b == null) return false;
        if (a.left >= b.right || a.right <= b.left || a.top >= b.bottom || a.bottom <= b.top) {
            return false;
        }
        this.left = Math.max(a.left, b.left);
        this.top = Math.max(a.top, b.top);
        this.right = Math.min(a.right, b.right);
        this.bottom = Math.min(a.bottom, b.bottom);
        return true;
    }

    public void union(int left, int top, int right, int bottom) {
        if (left < right && top < bottom) {
            if (this.left < this.right && this.top < this.bottom) {
                if (this.left > left) this.left = left;
                if (this.top > top) this.top = top;
                if (this.right < right) this.right = right;
                if (this.bottom < bottom) this.bottom = bottom;
            } else {
                this.left = left;
                this.top = top;
                this.right = right;
                this.bottom = bottom;
            }
        }
    }

    public void union(Rect r) {
        if (r != null) union(r.left, r.top, r.right, r.bottom);
    }

    public void union(int x, int y) {
        union(x, y, x + 1, y + 1);
    }

    public void offset(int dx, int dy) {
        this.left += dx;
        this.top += dy;
        this.right += dx;
        this.bottom += dy;
    }

    public void offsetTo(int newLeft, int newTop) {
        int dx = newLeft - this.left;
        int dy = newTop - this.top;
        offset(dx, dy);
    }

    public void sort() {
        if (this.left > this.right) {
            int t = this.left;
            this.left = this.right;
            this.right = t;
        }
        if (this.top > this.bottom) {
            int t = this.top;
            this.top = this.bottom;
            this.bottom = t;
        }
    }

    public void scale(float scale) {
        if (scale != 1.0f) {
            this.left = (int) (this.left * scale + 0.5f);
            this.top = (int) (this.top * scale + 0.5f);
            this.right = (int) (this.right * scale + 0.5f);
            this.bottom = (int) (this.bottom * scale + 0.5f);
        }
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Rect)) return false;
        Rect r = (Rect) o;
        return this.left == r.left && this.top == r.top
                && this.right == r.right && this.bottom == r.bottom;
    }

    @Override
    public int hashCode() {
        int h = this.left;
        h = 31 * h + this.top;
        h = 31 * h + this.right;
        h = 31 * h + this.bottom;
        return h;
    }

    @Override
    public String toString() {
        return "Rect(" + this.left + ", " + this.top + " - " + this.right + ", " + this.bottom + ")";
    }

    public String toShortString(StringBuilder sb) {
        sb.setLength(0);
        sb.append('[').append(this.left).append(',').append(this.top);
        sb.append("][").append(this.right).append(',').append(this.bottom).append(']');
        return sb.toString();
    }

    public String toShortString() {
        return toShortString(new StringBuilder(32));
    }

    public void readFromParcel(android.os.Parcel parcel) {
        if (parcel == null) throw new NullPointerException("parcel == null");
        this.left = parcel.readInt();
        this.top = parcel.readInt();
        this.right = parcel.readInt();
        this.bottom = parcel.readInt();
    }

    public void writeToParcel(android.os.Parcel parcel, int flags) {
        if (parcel == null) throw new NullPointerException("parcel == null");
        parcel.writeInt(this.left);
        parcel.writeInt(this.top);
        parcel.writeInt(this.right);
        parcel.writeInt(this.bottom);
    }

    public void flatten(StringBuilder sb) {
        sb.append(this.left).append(' ').append(this.top).append(' ');
        sb.append(this.right).append(' ').append(this.bottom);
    }

    public void unflatten(String str) {
        String[] parts = str.split(" ");
        this.left = Integer.parseInt(parts[0]);
        this.top = Integer.parseInt(parts[1]);
        this.right = Integer.parseInt(parts[2]);
        this.bottom = Integer.parseInt(parts[3]);
    }
}
