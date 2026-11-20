package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.view.VmLayoutHost;
import android.view.Window;

/**
 * 对话框（AOSP android.app.Dialog 公开面；§31.7 C 档 lib）。
 *
 * VM 单窗口模型：Dialog 持有一个 lib Window（内容树 + 布局意图），show() 时经
 * VmLayoutHost 把内容树登记为 VM 的窗口根；会话快照把它作为叠加根导出，宿主
 * UI 桥物化成真 View 叠加层（含 dim 遮罩），点击经既有 objectId 回灌。
 * dismiss()/hide() 摘除窗口根。
 */
public class Dialog implements DialogInterface {

    private final Context mContext;
    private Window mWindow;
    private boolean mCreated;
    private boolean mShowing;
    private boolean mCancelable = true;
    private boolean mCanceledOnTouchOutside = true;
    private DialogInterface.OnCancelListener mCancelListener;
    private DialogInterface.OnDismissListener mDismissListener;
    private DialogInterface.OnShowListener mOnShowListener;
    private DialogInterface.OnKeyListener mKeyListener;
    private Activity mOwnerActivity;

    public Dialog(Context context) {
        this.mContext = context;
        this.mWindow = new Window(context);
    }

    public Dialog(Context context, int themeResId) {
        this.mContext = context;
        this.mWindow = new Window(context);
    }

    protected Dialog(Context context, boolean cancelable,
                     DialogInterface.OnCancelListener cancelListener) {
        this.mContext = context;
        this.mWindow = new Window(context);
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
        return this.mWindow != null ? this.mWindow.getDecorView() : null;
    }

    public View findViewById(int id) {
        return this.mWindow != null ? this.mWindow.findViewById(id) : null;
    }

    public void setContentView(int layoutResID) {
        if (this.mWindow != null) this.mWindow.setContentView(layoutResID);
    }

    public void setContentView(View view) {
        if (this.mWindow != null) this.mWindow.setContentView(view);
    }

    public void setContentView(View view, ViewGroup.LayoutParams params) {
        if (this.mWindow != null) this.mWindow.setContentView(view);
    }

    public void addContentView(View view, ViewGroup.LayoutParams params) {
        if (this.mWindow != null) this.mWindow.addContentView(view, params);
    }

    public void setTitle(CharSequence title) {
        if (this.mWindow != null) this.mWindow.setTitle(title);
    }

    public void setTitle(int titleId) {
        if (this.mWindow != null) this.mWindow.setTitle(titleId);
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

    /** AOSP 生命周期钩子：AppCompatDialog 在 onCreate 里 installContent。 */
    protected void onCreate(Bundle savedInstanceState) {
    }

    protected void onStart() {
    }

    protected void onStop() {
    }

    boolean dispatchOnCreate(Bundle savedInstanceState) {
        if (this.mCreated) return false;
        this.mCreated = true;
        onCreate(savedInstanceState);
        return true;
    }

    public void hide() {
        if (!this.mShowing) return;
        if (this.mWindow != null) {
            VmLayoutHost.dismissDialog(this);
        }
        this.mShowing = false;
        onStop();
    }

    public void show() {
        if (this.mShowing) return;
        dispatchOnCreate(null);
        onStart();
        if (this.mWindow != null) {
            View content = this.mWindow.getDecorView();
            if (content != null) {
                VmLayoutHost.showDialog(this, content,
                        this.mWindow.getLayoutWidthForVm(),
                        this.mWindow.getLayoutHeightForVm(),
                        this.mWindow.getLayoutGravityForVm(),
                        this.mWindow.getDimAmount(), this.mCancelable);
            }
        }
        this.mShowing = true;
        if (this.mOnShowListener != null) this.mOnShowListener.onShow(this);
    }

    public void dismiss() {
        if (!this.mShowing) return;
        if (this.mWindow != null) {
            VmLayoutHost.dismissDialog(this);
        }
        this.mShowing = false;
        onStop();
        if (this.mDismissListener != null) this.mDismissListener.onDismiss(this);
    }

    public void cancel() {
        if (!this.mCancelable) return;
        if (this.mCancelListener != null) this.mCancelListener.onCancel(this);
        dismiss();
    }

    public void setCancelable(boolean flag) {
        this.mCancelable = flag;
    }

    public void setCanceledOnTouchOutside(boolean cancel) {
        this.mCanceledOnTouchOutside = cancel;
    }

    public boolean isCanceledOnTouchOutside() {
        return this.mCanceledOnTouchOutside;
    }

    public void setOnCancelListener(DialogInterface.OnCancelListener listener) {
        this.mCancelListener = listener;
    }

    public void setOnDismissListener(DialogInterface.OnDismissListener listener) {
        this.mDismissListener = listener;
    }

    public void setOnShowListener(DialogInterface.OnShowListener listener) {
        this.mOnShowListener = listener;
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
        this.mOwnerActivity = activity;
    }

    public Activity getOwnerActivity() {
        return this.mOwnerActivity;
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

    /** AOSP：窗口特性请求（VM 无窗口 feature 集，恒接受）。 */
    public boolean requestWindowFeature(int featureId) {
        return true;
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
