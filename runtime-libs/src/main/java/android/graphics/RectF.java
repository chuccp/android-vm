package android.graphics;

/**
 * 浮点矩形（AOSP android.graphics.RectF 公开面；§31.7 C 档 lib POJO）。
 */
public class RectF {
    public float left;
    public float top;
    public float right;
    public float bottom;

    public RectF() {
    }

    public RectF(float left, float top, float right, float bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public RectF(RectF r) {
        if (r != null) {
            this.left = r.left;
            this.top = r.top;
            this.right = r.right;
            this.bottom = r.bottom;
        }
    }

    public RectF(Rect r) {
        if (r != null) {
            this.left = r.left;
            this.top = r.top;
            this.right = r.right;
            this.bottom = r.bottom;
        }
    }

    public void set(float left, float top, float right, float bottom) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
    }

    public void set(RectF src) {
        if (src != null) set(src.left, src.top, src.right, src.bottom);
    }

    public void set(Rect src) {
        if (src != null) set(src.left, src.top, src.right, src.bottom);
    }

    public void setEmpty() {
        this.left = 0;
        this.top = 0;
        this.right = 0;
        this.bottom = 0;
    }

    public float width() {
        return this.right - this.left;
    }

    public float height() {
        return this.bottom - this.top;
    }

    public float centerX() {
        return (this.left + this.right) * 0.5f;
    }

    public float centerY() {
        return (this.top + this.bottom) * 0.5f;
    }

    public boolean isEmpty() {
        return this.left >= this.right || this.top >= this.bottom;
    }

    public boolean contains(float x, float y) {
        return this.left < this.right && this.top < this.bottom
                && x >= this.left && x < this.right && y >= this.top && y < this.bottom;
    }

    public boolean contains(float left, float top, float right, float bottom) {
        return this.left < this.right && this.top < this.bottom
                && this.left <= left && this.top <= top
                && this.right >= right && this.bottom >= bottom;
    }

    public boolean contains(RectF r) {
        return r != null && contains(r.left, r.top, r.right, r.bottom);
    }

    public void inset(float dx, float dy) {
        this.left += dx;
        this.top += dy;
        this.right -= dx;
        this.bottom -= dy;
    }

    public boolean intersect(float left, float top, float right, float bottom) {
        if (this.left < right && left < this.right && this.top < bottom && top < this.bottom) {
            if (this.left < left) this.left = left;
            if (this.top < top) this.top = top;
            if (this.right > right) this.right = right;
            if (this.bottom > bottom) this.bottom = bottom;
            return true;
        }
        return false;
    }

    public boolean intersect(RectF r) {
        return r != null && intersect(r.left, r.top, r.right, r.bottom);
    }

    public boolean setIntersect(RectF a, RectF b) {
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

    public void union(float left, float top, float right, float bottom) {
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

    public void union(RectF r) {
        if (r != null) union(r.left, r.top, r.right, r.bottom);
    }

    public void union(float x, float y) {
        union(x, y, x + 1, y + 1);
    }

    public void offset(float dx, float dy) {
        this.left += dx;
        this.top += dy;
        this.right += dx;
        this.bottom += dy;
    }

    public void offsetTo(float newLeft, float newTop) {
        offset(newLeft - this.left, newTop - this.top);
    }

    public void round(Rect dst) {
        if (dst != null) {
            dst.set((int) (this.left + 0.5f), (int) (this.top + 0.5f),
                    (int) (this.right + 0.5f), (int) (this.bottom + 0.5f));
        }
    }

    public void roundOut(Rect dst) {
        if (dst != null) {
            dst.set((int) Math.floor(this.left), (int) Math.floor(this.top),
                    (int) Math.ceil(this.right), (int) Math.ceil(this.bottom));
        }
    }

    public void sort() {
        if (this.left > this.right) {
            float t = this.left;
            this.left = this.right;
            this.right = t;
        }
        if (this.top > this.bottom) {
            float t = this.top;
            this.top = this.bottom;
            this.bottom = t;
        }
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof RectF)) return false;
        RectF r = (RectF) o;
        return this.left == r.left && this.top == r.top
                && this.right == r.right && this.bottom == r.bottom;
    }

    @Override
    public int hashCode() {
        int h = Float.floatToIntBits(this.left);
        h = 31 * h + Float.floatToIntBits(this.top);
        h = 31 * h + Float.floatToIntBits(this.right);
        h = 31 * h + Float.floatToIntBits(this.bottom);
        return h;
    }

    @Override
    public String toString() {
        return "RectF(" + this.left + ", " + this.top + " - " + this.right + ", " + this.bottom + ")";
    }

    public void readFromParcel(android.os.Parcel parcel) {
        if (parcel == null) throw new NullPointerException("parcel == null");
        this.left = parcel.readFloat();
        this.top = parcel.readFloat();
        this.right = parcel.readFloat();
        this.bottom = parcel.readFloat();
    }

    public void writeToParcel(android.os.Parcel parcel, int flags) {
        if (parcel == null) throw new NullPointerException("parcel == null");
        parcel.writeFloat(this.left);
        parcel.writeFloat(this.top);
        parcel.writeFloat(this.right);
        parcel.writeFloat(this.bottom);
    }
}
