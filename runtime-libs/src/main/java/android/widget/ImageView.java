package android.widget;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Matrix;
import android.graphics.PorterDuff;
import android.graphics.drawable.Drawable;
import android.net.Uri;
import android.util.AttributeSet;
import android.view.View;

/**
 * 图片视图（AOSP android.widget.ImageView 公开面；§31.7 C 档 lib——图像态字段 +
 * scaleType 面；真光栅化归桥接渲染层）。
 */
public class ImageView extends View {

    /** 缩放类型（AOSP ImageView.ScaleType 枚举）。 */
    public enum ScaleType {
        MATRIX,
        FIT_XY,
        FIT_START,
        FIT_CENTER,
        FIT_END,
        CENTER,
        CENTER_CROP,
        CENTER_INSIDE
    }

    private Drawable mDrawable;
    private Matrix mMatrix;
    private ScaleType mScaleType = ScaleType.FIT_CENTER;
    // 字段名与桥接 props 契约对齐（ui_bridge_protocol 读 mImageResource）。
    private int mImageResource;
    private Uri mUri;

    public ImageView(Context context) {
        super(context);
    }

    public ImageView(Context context, AttributeSet attrs) {
        this(context);
    }

    public ImageView(Context context, AttributeSet attrs, int defStyleAttr) {
        this(context);
    }

    public void setImageResource(int resId) {
        this.mImageResource = resId;
    }

    public void setImageURI(Uri uri) {
        this.mUri = uri;
    }

    public void setImageDrawable(Drawable drawable) {
        this.mDrawable = drawable;
    }

    /**
     * AOSP ImageView.onMeasure 近似：内容尺寸 = drawable 固有尺寸 + padding，
     * 经 resolveSize 收敛。无 drawable 的 wrap_content 图片收为 0（若不覆写，
     * 会落到 View.onMeasure 的 getDefaultSize——AT_MOST 下直接吃满父约束，
     * 底部导航背景图把导航容器撑成全屏的根因）。
     */
    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int w = getPaddingLeft() + getPaddingRight();
        int h = getPaddingTop() + getPaddingBottom();
        Drawable d = this.mDrawable;
        if (d != null) {
            int dw = d.getIntrinsicWidth();
            int dh = d.getIntrinsicHeight();
            if (dw > 0) w += dw;
            if (dh > 0) h += dh;
        }
        setMeasuredDimension(resolveSize(w, widthMeasureSpec),
                resolveSize(h, heightMeasureSpec));
    }

    public void setImageBitmap(Bitmap bm) {
        this.mDrawable = null;
    }

    public void setImageState(int[] state, boolean merge) {
    }

    public void setImageLevel(int level) {
    }

    public void setBackgroundResource(int resId) {
    }

    public void setBackground(Drawable background) {
    }

    public void setColorFilter(int color) {
    }

    public void setColorFilter(int color, PorterDuff.Mode mode) {
    }

    public void clearColorFilter() {
    }

    public Drawable getDrawable() {
        return this.mDrawable;
    }

    public void setScaleType(ScaleType scaleType) {
        this.mScaleType = scaleType != null ? scaleType : ScaleType.FIT_CENTER;
    }

    public ScaleType getScaleType() {
        return this.mScaleType;
    }

    public void setAdjustViewBounds(boolean adjustViewBounds) {
    }

    public void setMaxWidth(int maxWidth) {
    }

    public void setMaxHeight(int maxHeight) {
    }

    public void setImageMatrix(Matrix matrix) {
        this.mMatrix = matrix;
    }

    public Matrix getImageMatrix() {
        return this.mMatrix;
    }

    public void setSelected(boolean selected) {
    }

    public void setAlpha(int alpha) {
    }

    private android.content.res.ColorStateList mImageTintList;
    private PorterDuff.Mode mImageTintMode;

    public void setImageTintList(android.content.res.ColorStateList tint) {
        this.mImageTintList = tint;
    }

    public android.content.res.ColorStateList getImageTintList() {
        return this.mImageTintList;
    }

    public void setImageTintMode(PorterDuff.Mode mode) {
        this.mImageTintMode = mode;
    }

    public PorterDuff.Mode getImageTintMode() {
        return this.mImageTintMode;
    }

    public void setBaseline(int baseline) {
    }

    public void setCropToPadding(boolean cropToPadding) {
    }
}
