package android.graphics;

/**
 * 3x2 变换矩阵（AOSP android.graphics.Matrix 公开面；§31.7 C 档 lib）。
 * 状态 = 9 元 float 数组（MSCALE/MSKEW/MTRANS 平面 2x3 用法为主）；
 * 变换语义按 AOSP 仿射口径实现（mapPoints/mapRect/pre/post）。
 */
public class Matrix {
    public static final int MSCALE_X = 0;
    public static final int MSKEW_X = 1;
    public static final int MTRANS_X = 2;
    public static final int MSKEW_Y = 3;
    public static final int MSCALE_Y = 4;
    public static final int MTRANS_Y = 5;
    public static final int MPERSP_0 = 6;
    public static final int MPERSP_1 = 7;
    public static final int MPERSP_2 = 8;

    private final float[] mValues = new float[9];

    public Matrix() {
        reset();
    }

    public Matrix(Matrix src) {
        if (src != null) {
            System.arraycopy(src.mValues, 0, this.mValues, 0, 9);
        } else {
            reset();
        }
    }

    public void reset() {
        float[] v = this.mValues;
        v[0] = 1;
        v[1] = 0;
        v[2] = 0;
        v[3] = 0;
        v[4] = 1;
        v[5] = 0;
        v[6] = 0;
        v[7] = 0;
        v[8] = 1;
    }

    public void set(Matrix src) {
        if (src != null) {
            System.arraycopy(src.mValues, 0, this.mValues, 0, 9);
        } else {
            reset();
        }
    }

    public boolean isIdentity() {
        float[] v = this.mValues;
        return v[0] == 1 && v[1] == 0 && v[2] == 0
                && v[3] == 0 && v[4] == 1 && v[5] == 0
                && v[6] == 0 && v[7] == 0 && v[8] == 1;
    }

    public void setTranslate(float dx, float dy) {
        reset();
        this.mValues[MTRANS_X] = dx;
        this.mValues[MTRANS_Y] = dy;
    }

    public void setScale(float sx, float sy) {
        reset();
        this.mValues[MSCALE_X] = sx;
        this.mValues[MSCALE_Y] = sy;
    }

    public void setScale(float sx, float sy, float px, float py) {
        reset();
        this.mValues[MSCALE_X] = sx;
        this.mValues[MSCALE_Y] = sy;
        this.mValues[MTRANS_X] = px * (1 - sx);
        this.mValues[MTRANS_Y] = py * (1 - sy);
    }

    public void setRotate(float degrees) {
        setSinCos((float) Math.sin(Math.toRadians(degrees)),
                (float) Math.cos(Math.toRadians(degrees)));
    }

    public void setRotate(float degrees, float px, float py) {
        setRotate(degrees);
        this.mValues[MTRANS_X] = px - (px * this.mValues[MSCALE_X] + py * this.mValues[MSKEW_X]);
        this.mValues[MTRANS_Y] = py - (px * this.mValues[MSKEW_Y] + py * this.mValues[MSCALE_Y]);
    }

    public void setSinCos(float sinValue, float cosValue) {
        reset();
        this.mValues[MSCALE_X] = cosValue;
        this.mValues[MSKEW_X] = -sinValue;
        this.mValues[MSKEW_Y] = sinValue;
        this.mValues[MSCALE_Y] = cosValue;
    }

    public void setSkew(float kx, float ky) {
        reset();
        this.mValues[MSKEW_X] = kx;
        this.mValues[MSKEW_Y] = ky;
    }

    public void setSkew(float kx, float ky, float px, float py) {
        reset();
        this.mValues[MSKEW_X] = kx;
        this.mValues[MSKEW_Y] = ky;
        this.mValues[MTRANS_X] = -ky * py;
        this.mValues[MTRANS_Y] = -kx * px;
    }

    public void setConcat(Matrix a, Matrix b) {
        float[] va = a != null ? a.mValues : ident();
        float[] vb = b != null ? b.mValues : ident();
        float[] r = new float[9];
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                r[i * 3 + j] = va[i * 3] * vb[j]
                        + va[i * 3 + 1] * vb[3 + j]
                        + va[i * 3 + 2] * vb[6 + j];
            }
        }
        System.arraycopy(r, 0, this.mValues, 0, 9);
    }

    public boolean preTranslate(float dx, float dy) {
        Matrix m = new Matrix();
        m.setTranslate(dx, dy);
        return preConcat(m);
    }

    public boolean preScale(float sx, float sy) {
        Matrix m = new Matrix();
        m.setScale(sx, sy);
        return preConcat(m);
    }

    public boolean preRotate(float degrees) {
        Matrix m = new Matrix();
        m.setRotate(degrees);
        return preConcat(m);
    }

    public boolean postTranslate(float dx, float dy) {
        Matrix m = new Matrix();
        m.setTranslate(dx, dy);
        return postConcat(m);
    }

    public boolean postScale(float sx, float sy) {
        Matrix m = new Matrix();
        m.setScale(sx, sy);
        return postConcat(m);
    }

    public boolean postRotate(float degrees) {
        Matrix m = new Matrix();
        m.setRotate(degrees);
        return postConcat(m);
    }

    public boolean preConcat(Matrix other) {
        if (other == null) return true;
        setConcat(this, other);
        return true;
    }

    public boolean postConcat(Matrix other) {
        if (other == null) return true;
        setConcat(other, this);
        return true;
    }

    public boolean invert(Matrix inverse) {
        if (inverse == null) return false;
        float[] v = this.mValues;
        float det = v[0] * (v[4] * v[8] - v[5] * v[7])
                - v[1] * (v[3] * v[8] - v[5] * v[6])
                + v[2] * (v[3] * v[7] - v[4] * v[6]);
        if (det == 0) return false;
        float inv = 1.0f / det;
        float[] r = inverse.mValues;
        r[0] = (v[4] * v[8] - v[5] * v[7]) * inv;
        r[1] = (v[2] * v[7] - v[1] * v[8]) * inv;
        r[2] = (v[1] * v[5] - v[2] * v[4]) * inv;
        r[3] = (v[5] * v[6] - v[3] * v[8]) * inv;
        r[4] = (v[0] * v[8] - v[2] * v[6]) * inv;
        r[5] = (v[2] * v[3] - v[0] * v[5]) * inv;
        r[6] = (v[3] * v[7] - v[4] * v[6]) * inv;
        r[7] = (v[1] * v[6] - v[0] * v[7]) * inv;
        r[8] = (v[0] * v[4] - v[1] * v[3]) * inv;
        return true;
    }

    public void getValues(float[] values) {
        if (values == null || values.length < 9) {
            throw new IllegalArgumentException("needs 9 values");
        }
        System.arraycopy(this.mValues, 0, values, 0, 9);
    }

    public void setValues(float[] values) {
        if (values == null || values.length < 9) {
            throw new IllegalArgumentException("needs 9 values");
        }
        System.arraycopy(values, 0, this.mValues, 0, 9);
    }

    public void mapPoints(float[] pts) {
        mapPoints(pts, 0, pts, 0, pts == null ? 0 : pts.length / 2);
    }

    public void mapPoints(float[] dst, float[] src) {
        mapPoints(dst, 0, src, 0, src == null ? 0 : src.length / 2);
    }

    public void mapPoints(float[] dst, int dstIndex, float[] src, int srcIndex, int pointCount) {
        if (dst == null || src == null) {
            throw new NullPointerException("dst/src == null");
        }
        float[] v = this.mValues;
        for (int i = 0; i < pointCount; i++) {
            float x = src[srcIndex + i * 2];
            float y = src[srcIndex + i * 2 + 1];
            dst[dstIndex + i * 2] = v[0] * x + v[1] * y + v[2];
            dst[dstIndex + i * 2 + 1] = v[3] * x + v[4] * y + v[5];
        }
    }

    public void mapVectors(float[] vecs) {
        mapVectors(vecs, 0, vecs, 0, vecs == null ? 0 : vecs.length / 2);
    }

    public void mapVectors(float[] dst, float[] src) {
        mapVectors(dst, 0, src, 0, src == null ? 0 : src.length / 2);
    }

    public void mapVectors(float[] dst, int dstIndex, float[] src, int srcIndex, int vectorCount) {
        if (dst == null || src == null) {
            throw new NullPointerException("dst/src == null");
        }
        float[] v = this.mValues;
        for (int i = 0; i < vectorCount; i++) {
            float x = src[srcIndex + i * 2];
            float y = src[srcIndex + i * 2 + 1];
            dst[dstIndex + i * 2] = v[0] * x + v[1] * y;
            dst[dstIndex + i * 2 + 1] = v[3] * x + v[4] * y;
        }
    }

    public void mapRect(RectF dst, RectF src) {
        if (dst == null || src == null) {
            throw new NullPointerException("dst/src == null");
        }
        float[] pts = {src.left, src.top, src.right, src.top,
                src.right, src.bottom, src.left, src.bottom};
        mapPoints(pts);
        dst.set(Math.min(Math.min(pts[0], pts[2]), Math.min(pts[4], pts[6])),
                Math.min(Math.min(pts[1], pts[3]), Math.min(pts[5], pts[7])),
                Math.max(Math.max(pts[0], pts[2]), Math.max(pts[4], pts[6])),
                Math.max(Math.max(pts[1], pts[3]), Math.max(pts[5], pts[7])));
    }

    public void mapRect(RectF rect) {
        if (rect != null) mapRect(rect, rect);
    }

    public float mapRadius(float radius) {
        float[] v = this.mValues;
        float sx = (float) Math.hypot(v[0], v[3]);
        float sy = (float) Math.hypot(v[1], v[4]);
        return radius * (sx + sy) * 0.5f;
    }

    public boolean rectStaysRect() {
        float[] v = this.mValues;
        return v[1] == 0 && v[3] == 0 && v[6] == 0 && v[7] == 0 && v[8] == 1;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Matrix)) return false;
        float[] ov = ((Matrix) o).mValues;
        for (int i = 0; i < 9; i++) {
            if (this.mValues[i] != ov[i]) return false;
        }
        return true;
    }

    @Override
    public int hashCode() {
        int h = 1;
        for (int i = 0; i < 9; i++) {
            h = 31 * h + Float.floatToIntBits(this.mValues[i]);
        }
        return h;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder("Matrix{[");
        for (int i = 0; i < 9; i++) {
            if (i > 0) sb.append(", ");
            sb.append(this.mValues[i]);
        }
        sb.append("]}");
        return sb.toString();
    }

    private static float[] ident() {
        return new float[] {1, 0, 0, 0, 1, 0, 0, 0, 1};
    }
}
