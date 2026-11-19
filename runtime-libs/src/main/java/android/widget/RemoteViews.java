package android.widget;

import android.content.Context;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * 远程视图（AOSP android.widget.RemoteViews 公开面；§31.7 C 档 lib 骨架——
 * 通知/桌面部件视图面；跨进程视图归桥接层）。
 */
public class RemoteViews implements Parcelable {

    public RemoteViews(String packageName, int layoutId) {
    }

    public RemoteViews(RemoteViews landscape, RemoteViews portrait) {
    }

    public RemoteViews(RemoteViews src) {
    }

    public void setTextViewText(int viewId, CharSequence text) {
    }

    public void setTextViewTextSize(int viewId, int units, float size) {
    }

    public void setViewVisibility(int viewId, int visibility) {
    }

    public void setImageViewResource(int viewId, int srcId) {
    }

    public void setImageViewBitmap(int viewId, android.graphics.Bitmap bitmap) {
    }

    public void setImageViewIcon(int viewId, android.graphics.drawable.Icon icon) {
    }

    public void setInt(int viewId, String methodName, int value) {
    }

    public void setBoolean(int viewId, String methodName, boolean value) {
    }

    public void setLong(int viewId, String methodName, long value) {
    }

    public void setCharSequence(int viewId, String methodName, CharSequence value) {
    }

    public void setOnClickPendingIntent(int viewId, android.app.PendingIntent pendingIntent) {
    }

    public void setEmptyView(int viewId, int emptyViewId) {
    }

    public void addView(int viewId, RemoteViews nestedView) {
    }

    public void removeAllViews(int viewId) {
    }

    public void setViewLayoutMargin(int viewId, int type, float margin) {
    }

    public void setChronometer(int viewId, long base, String format, boolean started) {
    }

    public void setContentDescription(int viewId, CharSequence contentDescription) {
    }

    public void setProgressBar(int viewId, int max, int progress, boolean indeterminate) {
    }

    public void setViewPadding(int viewId, int left, int top, int right, int bottom) {
    }

    public void apply(Context context, android.view.ViewGroup parent) {
    }

    public android.view.View apply(Context context, android.view.ViewGroup parent,
                                   OnClickHandler handler) {
        return null;
    }

    public android.view.View inflate(Context context, android.view.ViewGroup parent) {
        return null;
    }

    public void reapply(Context context, android.view.View v) {
    }

    public int getLayoutId() {
        return 0;
    }

    public String getPackage() {
        return null;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }

    /** 点击处理器（AOSP RemoteViews.OnClickHandler）。 */
    public interface OnClickHandler {
        boolean onClickHandler(android.view.View view, android.app.PendingIntent pendingIntent,
                               android.content.Intent fillInIntent);
    }
}
