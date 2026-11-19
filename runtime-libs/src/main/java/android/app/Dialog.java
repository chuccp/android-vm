package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;

/**
 * 对话框（AOSP android.app.Dialog 公开面；§31.7 C 档 lib 骨架——窗口/显示态 +
 * show/dismiss 生命周期面；真窗口归桥接层）。
 */
public class Dialog {

    private final Context mContext;
    private Window mWindow;
    private boolean mShowing;
    private boolean mCancelable = true;
    private DialogInterface.OnCancelListener mCancelListener;
    private DialogInterface.OnDismissListener mDismissListener;
    private DialogInterface.OnKeyListener mKeyListener;

    public Dialog(Context context) {
        this.mContext = context;
    }

    public Dialog(Context context, int themeResId) {
        this.mContext = context;
    }

    protected Dialog(Context context, boolean cancelable,
                     DialogInterface.OnCancelListener cancelListener) {
        this.mContext = context;
        this.mCancelable = cancelable;
        this.mCancelListener = cancelListener;
    }

    public Context getContext() {
        return this.mContext;
    }

    public Window getWindow() {
        return this.mWindow;
    }

    public View getCurrentFocus() {
        return null;
    }

    public View findViewById(int id) {
        return this.mWindow != null ? this.mWindow.findViewById(id) : null;
    }

    public void setContentView(int layoutResID) {
    }

    public void setContentView(View view) {
    }

    public void setContentView(View view, ViewGroup.LayoutParams params) {
    }

    public void addContentView(View view, ViewGroup.LayoutParams params) {
    }

    public void setTitle(CharSequence title) {
    }

    public void setTitle(int titleId) {
    }

    public boolean onKeyDown(int keyCode, KeyEvent event) {
        return false;
    }

    public boolean onKeyUp(int keyCode, KeyEvent event) {
        return false;
    }

    public void onRestoreInstanceState(Bundle savedInstanceState) {
    }

    public Bundle onSaveInstanceState() {
        return new Bundle();
    }

    public void hide() {
    }

    public void show() {
        this.mShowing = true;
    }

    public void dismiss() {
        this.mShowing = false;
        if (this.mDismissListener != null) this.mDismissListener.onDismiss(null);
    }

    public void cancel() {
        if (this.mCancelListener != null) this.mCancelListener.onCancel(null);
        dismiss();
    }

    public void setCancelable(boolean flag) {
        this.mCancelable = flag;
    }

    public void setCanceledOnTouchOutside(boolean cancel) {
    }

    public void setOnCancelListener(DialogInterface.OnCancelListener listener) {
        this.mCancelListener = listener;
    }

    public void setOnDismissListener(DialogInterface.OnDismissListener listener) {
        this.mDismissListener = listener;
    }

    public void setOnShowListener(DialogInterface.OnShowListener listener) {
    }

    public void setOnKeyListener(DialogInterface.OnKeyListener listener) {
        this.mKeyListener = listener;
    }

    public boolean isShowing() {
        return this.mShowing;
    }

    public boolean isCancelable() {
        return this.mCancelable;
    }

    public void setOwnerActivity(Activity activity) {
    }

    public Activity getOwnerActivity() {
        return null;
    }

    public boolean takeKeyEvents(boolean get) {
        return false;
    }

    public void setVolumeControlStream(int streamType) {
    }

    public int getVolumeControlStream() {
        return 0;
    }

    public void setOnCreateContextMenuListener(android.view.View.OnCreateContextMenuListener l) {
    }

    public void requestWindowFeature(int featureId) {
    }

    public void setFeatureDrawable(int featureId, Drawable drawable) {
    }

    public LayoutInflater getLayoutInflater() {
        return LayoutInflater.from(this.mContext);
    }

    public interface OnCreateContextMenuListener extends
            android.view.View.OnCreateContextMenuListener {
    }
}
