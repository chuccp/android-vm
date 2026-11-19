package android.graphics;

import java.util.ArrayList;

/**
 * 路径（AOSP android.graphics.Path 公开面；§31.7 C 档 lib——复杂类禁 native）。
 * 几何状态 = 命令序列（moveTo/lineTo/quadTo/cubicTo/close）；transform 作用于
 * 命令坐标；布尔运算 op 后置（骨架返回 false/占位）。光栅化语义经 Canvas
 * drawPath 缝（后置）。
 */
public class Path {
    public enum Direction {
        CW,
        CCW
    }

    public enum FillType {
        WINDING,
        EVEN_ODD,
        INVERSE_WINDING,
        INVERSE_EVEN_ODD
    }

    public enum Op {
        DIFFERENCE,
        INTERSECT,
        UNION,
        XOR,
        REVERSE_DIFFERENCE,
        REPLACE
    }

    // 命令编码：0=MOVE, 1=LINE, 2=QUAD, 3=CUBIC, 4=CLOSE（float 参数紧随）
    private final ArrayList<float[]> mOps = new ArrayList<float[]>();
    private FillType mFillType = FillType.WINDING;
    private float mLastX;
    private float mLastY;
    private float mStartX;
    private float mStartY;

    public Path() {
    }

    public Path(Path src) {
        set(src);
    }

    public void set(Path src) {
        this.mOps.clear();
        if (src != null) {
            for (int i = 0; i < src.mOps.size(); i++) {
                float[] op = src.mOps.get(i);
                this.mOps.add(op.clone());
            }
            this.mFillType = src.mFillType;
            this.mLastX = src.mLastX;
            this.mLastY = src.mLastY;
            this.mStartX = src.mStartX;
            this.mStartY = src.mStartY;
        }
    }

    public void reset() {
        this.mOps.clear();
        this.mFillType = FillType.WINDING;
        this.mLastX = 0;
        this.mLastY = 0;
        this.mStartX = 0;
        this.mStartY = 0;
    }

    public void rewind() {
        this.mOps.clear();
        this.mLastX = 0;
        this.mLastY = 0;
        this.mStartX = 0;
        this.mStartY = 0;
    }

    public boolean isEmpty() {
        return this.mOps.isEmpty();
    }

    public void setFillType(FillType ft) {
        this.mFillType = ft != null ? ft : FillType.WINDING;
    }

    public FillType getFillType() {
        return this.mFillType;
    }

    public void moveTo(float x, float y) {
        this.mOps.add(new float[] {0, x, y});
        this.mLastX = x;
        this.mLastY = y;
        this.mStartX = x;
        this.mStartY = y;
    }

    public void rMoveTo(float dx, float dy) {
        moveTo(this.mLastX + dx, this.mLastY + dy);
    }

    public void lineTo(float x, float y) {
        this.mOps.add(new float[] {1, x, y});
        this.mLastX = x;
        this.mLastY = y;
    }

    public void rLineTo(float dx, float dy) {
        lineTo(this.mLastX + dx, this.mLastY + dy);
    }

    public void quadTo(float x1, float y1, float x2, float y2) {
        this.mOps.add(new float[] {2, x1, y1, x2, y2});
        this.mLastX = x2;
        this.mLastY = y2;
    }

    public void rQuadTo(float dx1, float dy1, float dx2, float dy2) {
        quadTo(this.mLastX + dx1, this.mLastY + dy1, this.mLastX + dx2, this.mLastY + dy2);
    }

    public void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
        this.mOps.add(new float[] {3, x1, y1, x2, y2, x3, y3});
        this.mLastX = x3;
        this.mLastY = y3;
    }

    public void rCubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        cubicTo(this.mLastX + dx1, this.mLastY + dy1,
                this.mLastX + dx2, this.mLastY + dy2,
                this.mLastX + dx3, this.mLastY + dy3);
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle) {
        if (oval == null) return;
        float rad = (float) Math.toRadians(startAngle);
        float cx = oval.centerX();
        float cy = oval.centerY();
        float rx = oval.width() * 0.5f;
        float ry = oval.height() * 0.5f;
        moveTo(cx + rx * (float) Math.cos(rad), cy + ry * (float) Math.sin(rad));
    }

    public void arcTo(RectF oval, float startAngle, float sweepAngle, boolean forceMoveTo) {
        arcTo(oval, startAngle, sweepAngle);
    }

    public void close() {
        this.mOps.add(new float[] {4});
        this.mLastX = this.mStartX;
        this.mLastY = this.mStartY;
    }

    public void addRect(RectF rect, Direction dir) {
        if (rect == null) return;
        addRect(rect.left, rect.top, rect.right, rect.bottom, dir);
    }

    public void addRect(float left, float top, float right, float bottom, Direction dir) {
        moveTo(left, top);
        if (dir == Direction.CCW) {
            lineTo(left, bottom);
            lineTo(right, bottom);
            lineTo(right, top);
        } else {
            lineTo(right, top);
            lineTo(right, bottom);
            lineTo(left, bottom);
        }
        close();
    }

    public void addRect(Rect rect, Direction dir) {
        if (rect != null) addRect(rect.left, rect.top, rect.right, rect.bottom, dir);
    }

    public void addCircle(float x, float y, float radius, Direction dir) {
        RectF oval = new RectF(x - radius, y - radius, x + radius, y + radius);
        addOval(oval, dir);
    }

    public void addOval(RectF oval, Direction dir) {
        addOval(oval, 0, 360, dir);
    }

    public void addOval(RectF oval, float startAngle, float sweepAngle, Direction dir) {
        if (oval == null) return;
        arcTo(oval, startAngle, sweepAngle, true);
        close();
    }

    public void addRoundRect(RectF rect, float rx, float ry, Direction dir) {
        if (rect == null) return;
        addRect(rect, dir);
    }

    public void addRoundRect(RectF rect, float[] radii, Direction dir) {
        if (rect != null) addRect(rect, dir);
    }

    public void addRoundRect(float left, float top, float right, float bottom, float rx, float ry,
                             Direction dir) {
        addRoundRect(new RectF(left, top, right, bottom), rx, ry, dir);
    }

    public void addPath(Path src) {
        addPath(src, 0, 0);
    }

    public void addPath(Path src, float dx, float dy) {
        if (src == null) return;
        for (int i = 0; i < src.mOps.size(); i++) {
            float[] op = src.mOps.get(i).clone();
            for (int j = 1; j < op.length; j += 2) {
                op[j] += dx;
                op[j + 1] += dy;
            }
            this.mOps.add(op);
        }
    }

    public void addPath(Path src, Matrix matrix) {
        if (src == null) return;
        for (int i = 0; i < src.mOps.size(); i++) {
            float[] op = src.mOps.get(i).clone();
            if (matrix != null) {
                float[] pts = new float[op.length - 1];
                System.arraycopy(op, 1, pts, 0, pts.length);
                matrix.mapPoints(pts);
                System.arraycopy(pts, 0, op, 1, pts.length);
            }
            this.mOps.add(op);
        }
    }

    public void addPath(Path src, Matrix matrix, Path dst) {
        if (dst == null) return;
        dst.reset();
        dst.addPath(src, matrix);
    }

    public void offset(float dx, float dy) {
        offset(dx, dy, null);
    }

    public void offset(float dx, float dy, Path dst) {
        Path target = dst != null ? dst : this;
        if (dst != null) target.set(this);
        for (int i = 0; i < target.mOps.size(); i++) {
            float[] op = target.mOps.get(i);
            for (int j = 1; j < op.length; j += 2) {
                op[j] += dx;
                op[j + 1] += dy;
            }
        }
        target.mLastX += dx;
        target.mLastY += dy;
        target.mStartX += dx;
        target.mStartY += dy;
    }

    public void transform(Matrix matrix) {
        transform(matrix, null);
    }

    public void transform(Matrix matrix, Path dst) {
        if (matrix == null) return;
        Path target = dst != null ? dst : this;
        if (dst != null) target.set(this);
        for (int i = 0; i < target.mOps.size(); i++) {
            float[] op = target.mOps.get(i);
            if (op.length >= 3) {
                float[] pts = new float[op.length - 1];
                System.arraycopy(op, 1, pts, 0, pts.length);
                matrix.mapPoints(pts);
                System.arraycopy(pts, 0, op, 1, pts.length);
            }
        }
    }

    public boolean op(Path path1, Path path2, Op op) {
        // 布尔运算缝后置：占位 false（缺口显式）。
        return false;
    }

    public boolean op(Path path, Op op) {
        return op(this, path, op);
    }

    public void setLastPoint(float dx, float dy) {
        this.mLastX = dx;
        this.mLastY = dy;
    }

    public void incReserve(int extraPtCount) {
    }

    public void toggleInverseFillType() {
        switch (this.mFillType) {
            case WINDING:
                this.mFillType = FillType.INVERSE_WINDING;
                break;
            case EVEN_ODD:
                this.mFillType = FillType.INVERSE_EVEN_ODD;
                break;
            case INVERSE_WINDING:
                this.mFillType = FillType.WINDING;
                break;
            case INVERSE_EVEN_ODD:
                this.mFillType = FillType.EVEN_ODD;
                break;
        }
    }

    public void computeBounds(RectF bounds, boolean exact) {
        if (bounds == null) return;
        float minX = Float.MAX_VALUE;
        float minY = Float.MAX_VALUE;
        float maxX = -Float.MAX_VALUE;
        float maxY = -Float.MAX_VALUE;
        boolean any = false;
        for (int i = 0; i < this.mOps.size(); i++) {
            float[] op = this.mOps.get(i);
            for (int j = 1; j < op.length; j += 2) {
                any = true;
                if (op[j] < minX) minX = op[j];
                if (op[j] > maxX) maxX = op[j];
                if (op[j + 1] < minY) minY = op[j + 1];
                if (op[j + 1] > maxY) maxY = op[j + 1];
            }
        }
        if (any) {
            bounds.set(minX, minY, maxX, maxY);
        } else {
            bounds.setEmpty();
        }
    }

    /** 命令序列（包内渲染缝消费；勿外传）。 */
    ArrayList<float[]> ops() {
        return this.mOps;
    }
}
