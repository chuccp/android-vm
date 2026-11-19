package android.app;

import android.content.Context;
import android.content.Intent;
import android.graphics.drawable.Icon;
import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;
import android.widget.RemoteViews;

/**
 * 通知（AOSP android.app.Notification 公开面；§31.7 C 档 lib——复杂类禁 native：
 * 字段态 + Builder 组装面；通知下发归桥接层）。
 */
public class Notification implements Parcelable {

    public String icon;
    public int iconLevel;
    public int number;
    public CharSequence tickerText;
    public CharSequence contentTitle;
    public CharSequence contentText;
    public CharSequence contentInfo;
    public CharSequence subText;
    public CharSequence[] remoteInputHistory;
    public PendingIntent contentIntent;
    public PendingIntent deleteIntent;
    public PendingIntent fullScreenIntent;
    public long when;
    public long[] whenItems;
    public boolean usesChronometer;
    public int priority;
    public boolean colorized;
    public int color;
    public int visibility;
    public int flags;
    public Bundle extras;
    public int ledARGB;
    public int ledOnMS;
    public int ledOffMS;
    public int sound = 0;
    public int defaults;
    public int audioStreamType = -1;
    public boolean vibrateSet;
    public long[] vibrate;
    public boolean silent;
    /** 渠道 id（AOSP Notification.mChannelId 公开投影；宿主通知桥消费）。 */
    public String channelId;

    public static final int FLAG_SHOW_LIGHTS = 0x00000001;
    public static final int FLAG_ONGOING_EVENT = 0x00000002;
    public static final int FLAG_INSISTENT = 0x00000004;
    public static final int FLAG_ONLY_ALERT_ONCE = 0x00000008;
    public static final int FLAG_AUTO_CANCEL = 0x00000010;
    public static final int FLAG_NO_CLEAR = 0x00000020;
    public static final int FLAG_FOREGROUND_SERVICE = 0x00000040;
    public static final int FLAG_HIGH_PRIORITY = 0x00000080;
    public static final int FLAG_LOCAL_ONLY = 0x00000100;
    public static final int FLAG_GROUP_SUMMARY = 0x00000200;

    public static final int VISIBILITY_PRIVATE = 0;
    public static final int VISIBILITY_PUBLIC = 1;
    public static final int VISIBILITY_SECRET = -1;

    public static final int PRIORITY_MIN = -2;
    public static final int PRIORITY_LOW = -1;
    public static final int PRIORITY_DEFAULT = 0;
    public static final int PRIORITY_HIGH = 1;
    public static final int PRIORITY_MAX = 2;

    public static final String EXTRA_TITLE = "android.title";
    public static final String EXTRA_TEXT = "android.text";
    public static final String EXTRA_SUB_TEXT = "android.subText";
    public static final String EXTRA_INFO_TEXT = "android.infoText";
    public static final String EXTRA_SUMMARY_TEXT = "android.summaryText";
    public static final String EXTRA_TITLE_BIG = "android.title.big";
    public static final String EXTRA_BIG_TEXT = "android.bigText";
    public static final String EXTRA_TEMPLATE = "android.template";

    public Notification() {
    }

    public Notification(Context context, int icon, CharSequence tickerText, long when) {
        this.icon = context != null ? context.getPackageName() : null;
        this.iconLevel = icon;
        this.tickerText = tickerText;
        this.when = when;
    }

    public void setLatestEventInfo(Context context, CharSequence contentTitle,
                                   CharSequence contentText, PendingIntent contentIntent) {
        this.contentTitle = contentTitle;
        this.contentText = contentText;
        this.contentIntent = contentIntent;
    }

    @Override
    public String toString() {
        return "Notification(" + this.contentTitle + "/" + this.contentText + ")";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel out, int flags) {
    }

    /** 动作按钮（AOSP Notification.Action）。 */
    public static class Action implements Parcelable {
        public int icon;
        public CharSequence title;
        public PendingIntent actionIntent;
        public Bundle extras;

        public Action() {
        }

        public Action(int icon, CharSequence title, PendingIntent intent) {
            this.icon = icon;
            this.title = title;
            this.actionIntent = intent;
        }

        @Override
        public int describeContents() {
            return 0;
        }

        @Override
        public void writeToParcel(Parcel out, int flags) {
        }
    }

    /** 气泡元数据（AOSP Notification.BubbleMetadata；骨架）。 */
    public static class BubbleMetadata implements Parcelable {

        public BubbleMetadata() {
        }

        @Override
        public int describeContents() {
            return 0;
        }

        @Override
        public void writeToParcel(Parcel out, int flags) {
        }

        /** 气泡构建器（AOSP BubbleMetadata.Builder）。 */
        public static class Builder {
            public Builder() {
            }

            public Builder setIntent(PendingIntent intent) {
                return this;
            }

            public Builder setAutoExpandBubble(boolean expand) {
                return this;
            }

            public Builder setSuppressNotification(boolean suppress) {
                return this;
            }

            public BubbleMetadata build() {
                return new BubbleMetadata();
            }
        }
    }

    /** 大文本样式（AOSP Notification.BigTextStyle）。 */
    public static class BigTextStyle extends Style {
        private CharSequence mBigText;

        public BigTextStyle() {
        }

        public BigTextStyle setBigContentTitle(CharSequence title) {
            this.mBigContentTitle = title;
            return this;
        }

        public BigTextStyle bigText(CharSequence cs) {
            this.mBigText = cs;
            return this;
        }

        public BigTextStyle setSummaryText(CharSequence cs) {
            this.mSummaryText = cs;
            return this;
        }
    }

    /** 大图样式（AOSP Notification.BigPictureStyle）。 */
    public static class BigPictureStyle extends Style {
        private Icon mPicture;

        public BigPictureStyle() {
        }

        public BigPictureStyle setBigContentTitle(CharSequence title) {
            this.mBigContentTitle = title;
            return this;
        }

        public BigPictureStyle bigPicture(Icon b) {
            this.mPicture = b;
            return this;
        }

        public BigPictureStyle setSummaryText(CharSequence cs) {
            this.mSummaryText = cs;
            return this;
        }
    }

    /** 收件箱样式（AOSP Notification.InboxStyle）。 */
    public static class InboxStyle extends Style {
        private final java.util.ArrayList<CharSequence> mLines = new java.util.ArrayList<CharSequence>();

        public InboxStyle() {
        }

        public InboxStyle setBigContentTitle(CharSequence title) {
            this.mBigContentTitle = title;
            return this;
        }

        public InboxStyle addLine(CharSequence cs) {
            this.mLines.add(cs);
            return this;
        }

        public InboxStyle setSummaryText(CharSequence cs) {
            this.mSummaryText = cs;
            return this;
        }
    }

    /** 样式基（AOSP Notification.Style）。 */
    public abstract static class Style {
        CharSequence mBigContentTitle;
        CharSequence mSummaryText;
        Builder mBuilder;

        public Style() {
        }

        public void setBuilder(Builder builder) {
            this.mBuilder = builder;
        }

        public Notification build() {
            return this.mBuilder != null ? this.mBuilder.build() : new Notification();
        }
    }

    /** 构建器（AOSP Notification.Builder）。 */
    public static class Builder {
        private final Context mContext;
        private final Notification mN = new Notification();
        private int mDefaults;

        public Builder(Context context) {
            this.mContext = context;
        }

        public Builder(Context context, String channelId) {
            this.mContext = context;
            this.mN.channelId = channelId;
        }

        public Builder setWhen(long when) {
            this.mN.when = when;
            return this;
        }

        public Builder setShowWhen(boolean show) {
            return this;
        }

        public Builder setSmallIcon(int icon) {
            this.mN.iconLevel = icon;
            return this;
        }

        public Builder setSmallIcon(Icon icon) {
            return this;
        }

        public Builder setSmallIcon(int icon, int level) {
            this.mN.iconLevel = icon;
            this.mN.iconLevel = level;
            return this;
        }

        public Builder setContentTitle(CharSequence title) {
            this.mN.contentTitle = title;
            return this;
        }

        public Builder setContentText(CharSequence text) {
            this.mN.contentText = text;
            return this;
        }

        public Builder setSubText(CharSequence text) {
            this.mN.subText = text;
            return this;
        }

        public Builder setContentInfo(CharSequence info) {
            this.mN.contentInfo = info;
            return this;
        }

        public Builder setTicker(CharSequence tickerText) {
            this.mN.tickerText = tickerText;
            return this;
        }

        public Builder setContentIntent(PendingIntent intent) {
            this.mN.contentIntent = intent;
            return this;
        }

        public Builder setDeleteIntent(PendingIntent intent) {
            this.mN.deleteIntent = intent;
            return this;
        }

        public Builder setFullScreenIntent(PendingIntent intent, boolean highPriority) {
            this.mN.fullScreenIntent = intent;
            return this;
        }

        public Builder setAutoCancel(boolean autoCancel) {
            if (autoCancel) this.mN.flags |= FLAG_AUTO_CANCEL;
            return this;
        }

        public Builder setOngoing(boolean ongoing) {
            if (ongoing) this.mN.flags |= FLAG_ONGOING_EVENT;
            return this;
        }

        public Builder setOnlyAlertOnce(boolean onlyAlertOnce) {
            return this;
        }

        public Builder setLights(int argb, int onMs, int offMs) {
            this.mN.ledARGB = argb;
            this.mN.ledOnMS = onMs;
            this.mN.ledOffMS = offMs;
            return this;
        }

        public Builder setVibrate(long[] pattern) {
            this.mN.vibrate = pattern;
            return this;
        }

        public Builder setDefaults(int defaults) {
            this.mDefaults = defaults;
            this.mN.defaults = defaults;
            return this;
        }

        public Builder setPriority(int pri) {
            this.mN.priority = pri;
            return this;
        }

        public Builder setColor(int argb) {
            this.mN.color = argb;
            return this;
        }

        public Builder setVisibility(int visibility) {
            this.mN.visibility = visibility;
            return this;
        }

        public Builder setNumber(int number) {
            this.mN.number = number;
            return this;
        }

        public Builder setExtras(Bundle extras) {
            this.mN.extras = extras;
            return this;
        }

        public Bundle getExtras() {
            return this.mN.extras;
        }

        public Builder addAction(int icon, CharSequence title, PendingIntent intent) {
            return this;
        }

        public Builder addAction(Notification.Action action) {
            return this;
        }

        public Builder setStyle(Style style) {
            if (style != null) style.setBuilder(this);
            return this;
        }

        public Builder setCustomContentView(RemoteViews contentView) {
            return this;
        }

        public Builder setCustomBigContentView(RemoteViews contentView) {
            return this;
        }

        public Builder setCustomHeadsUpContentView(RemoteViews contentView) {
            return this;
        }

        public Builder setChannelId(String channelId) {
            this.mN.channelId = channelId;
            return this;
        }

        public Notification build() {
            return this.mN;
        }

        public Notification getNotification() {
            return this.mN;
        }
    }
}
