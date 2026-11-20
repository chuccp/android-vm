package android.app;

import android.content.Context;
import android.content.DialogInterface;
import android.graphics.drawable.Drawable;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

/**
 * 对话框（AOSP android.app.AlertDialog 公开面；§31.7 C 档 lib）。
 *
 * <p>在 lib {@link Dialog}/Window 的单窗口模型上构建内容树：标题 / 正文 /
 * 按钮行作为窗口根经 ViewSnapshot 叠加根导出，宿主 UI 桥物化成真弹窗（含
 * dim 遮罩）；按钮点击走 setOnClickListener → performClick 既有回灌路径。
 *
 * <p>Builder 覆盖应用常用面（标题/正文/三键/自定义 View/取消监听）。图标、
 * 主题等装饰面在 VM 单窗口模型下忽略。
 */
public class AlertDialog extends Dialog {

    private final Button mPositiveButton;
    private final Button mNegativeButton;
    private final Button mNeutralButton;

    protected AlertDialog(Context context, int themeResId, Builder builder) {
        super(context, themeResId);
        LinearLayout root = new LinearLayout(context);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(48, 48, 48, 48);
        root.setBackgroundColor(0xFFFFFFFF);

        if (builder.mTitle != null) {
            TextView title = new TextView(context);
            title.setText(builder.mTitle);
            title.setTextSize(20f);
            title.setPadding(0, 0, 0, 16);
            root.addView(title);
        }
        if (builder.mMessage != null) {
            TextView message = new TextView(context);
            message.setText(builder.mMessage);
            message.setTextSize(15f);
            message.setPadding(0, 0, 0, 24);
            root.addView(message);
        }
        if (builder.mView != null) {
            root.addView(builder.mView);
        }

        LinearLayout buttons = new LinearLayout(context);
        buttons.setOrientation(LinearLayout.HORIZONTAL);
        mNeutralButton = addButton(buttons, context, builder.mNeutralText,
                DialogInterface.BUTTON_NEUTRAL, builder.mNeutralListener);
        mNegativeButton = addButton(buttons, context, builder.mNegativeText,
                DialogInterface.BUTTON_NEGATIVE, builder.mNegativeListener);
        mPositiveButton = addButton(buttons, context, builder.mPositiveText,
                DialogInterface.BUTTON_POSITIVE, builder.mPositiveListener);
        if (mNeutralButton != null || mNegativeButton != null || mPositiveButton != null) {
            root.addView(buttons);
        }

        setContentView(root);
        setCancelable(builder.mCancelable);
        setCanceledOnTouchOutside(builder.mCanceledOnTouchOutside);
        if (builder.mOnCancelListener != null) {
            setOnCancelListener(builder.mOnCancelListener);
        }
        if (builder.mOnDismissListener != null) {
            setOnDismissListener(builder.mOnDismissListener);
        }
    }

    private Button addButton(LinearLayout row, Context context, CharSequence text,
                             final int which,
                             final DialogInterface.OnClickListener listener) {
        if (text == null) return null;
        Button button = new Button(context);
        button.setText(text);
        // 固定动作宽度：宿主 Button 自带背景内边距（guest 量到的宽度是
        // text+padding 的精确值，无余量），自然宽度会把 CANCEL 裁成 CANC。
        button.setPadding(24, 8, 24, 8);
        LinearLayout.LayoutParams buttonParams = new LinearLayout.LayoutParams(
                200, LinearLayout.LayoutParams.WRAP_CONTENT);
        button.setLayoutParams(buttonParams);
        button.setOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View view) {
                if (listener != null) {
                    listener.onClick(AlertDialog.this, which);
                }
                dismiss();
            }
        });
        row.addView(button);
        return button;
    }

    /** 返回 Builder 登记过的按钮（未登记返回 null；AOSP 同形）。 */
    public Button getButton(int which) {
        switch (which) {
            case DialogInterface.BUTTON_POSITIVE:
                return mPositiveButton;
            case DialogInterface.BUTTON_NEGATIVE:
                return mNegativeButton;
            case DialogInterface.BUTTON_NEUTRAL:
                return mNeutralButton;
            default:
                return null;
        }
    }

    /** AlertDialog 的构造器（AOSP android.app.AlertDialog.Builder 公开面）。 */
    public static class Builder {
        private final Context mContext;
        private final int mThemeResId;

        CharSequence mTitle;
        CharSequence mMessage;
        CharSequence mPositiveText;
        CharSequence mNegativeText;
        CharSequence mNeutralText;
        DialogInterface.OnClickListener mPositiveListener;
        DialogInterface.OnClickListener mNegativeListener;
        DialogInterface.OnClickListener mNeutralListener;
        boolean mCancelable = true;
        boolean mCanceledOnTouchOutside = true;
        DialogInterface.OnCancelListener mOnCancelListener;
        DialogInterface.OnDismissListener mOnDismissListener;
        View mView;

        public Builder(Context context) {
            this(context, 0);
        }

        public Builder(Context context, int themeResId) {
            this.mContext = context;
            this.mThemeResId = themeResId;
        }

        public Builder setTitle(CharSequence title) {
            this.mTitle = title;
            return this;
        }

        public Builder setTitle(int titleId) {
            this.mTitle = mContext != null ? mContext.getText(titleId) : null;
            return this;
        }

        public Builder setMessage(CharSequence message) {
            this.mMessage = message;
            return this;
        }

        public Builder setMessage(int messageId) {
            this.mMessage = mContext != null ? mContext.getText(messageId) : null;
            return this;
        }

        /** VM 单窗口模型无图标装饰面：接受并忽略。 */
        public Builder setIcon(int iconId) {
            return this;
        }

        /** VM 单窗口模型无图标装饰面：接受并忽略。 */
        public Builder setIcon(Drawable icon) {
            return this;
        }

        public Builder setView(View view) {
            this.mView = view;
            return this;
        }

        public Builder setPositiveButton(CharSequence text,
                                         DialogInterface.OnClickListener listener) {
            this.mPositiveText = text;
            this.mPositiveListener = listener;
            return this;
        }

        public Builder setPositiveButton(int textId,
                                         DialogInterface.OnClickListener listener) {
            return setPositiveButton(
                    mContext != null ? mContext.getText(textId) : null, listener);
        }

        public Builder setNegativeButton(CharSequence text,
                                         DialogInterface.OnClickListener listener) {
            this.mNegativeText = text;
            this.mNegativeListener = listener;
            return this;
        }

        public Builder setNegativeButton(int textId,
                                         DialogInterface.OnClickListener listener) {
            return setNegativeButton(
                    mContext != null ? mContext.getText(textId) : null, listener);
        }

        public Builder setNeutralButton(CharSequence text,
                                        DialogInterface.OnClickListener listener) {
            this.mNeutralText = text;
            this.mNeutralListener = listener;
            return this;
        }

        public Builder setNeutralButton(int textId,
                                        DialogInterface.OnClickListener listener) {
            return setNeutralButton(
                    mContext != null ? mContext.getText(textId) : null, listener);
        }

        public Builder setCancelable(boolean cancelable) {
            this.mCancelable = cancelable;
            return this;
        }

        public Builder setCanceledOnTouchOutside(boolean cancel) {
            this.mCanceledOnTouchOutside = cancel;
            return this;
        }

        public Builder setOnCancelListener(DialogInterface.OnCancelListener listener) {
            this.mOnCancelListener = listener;
            return this;
        }

        public Builder setOnDismissListener(DialogInterface.OnDismissListener listener) {
            this.mOnDismissListener = listener;
            return this;
        }

        public AlertDialog create() {
            return new AlertDialog(mContext, mThemeResId, this);
        }

        public AlertDialog show() {
            AlertDialog dialog = create();
            dialog.show();
            return dialog;
        }
    }
}
