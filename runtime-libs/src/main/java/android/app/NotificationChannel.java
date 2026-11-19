package android.app;

import android.net.Uri;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * 通知渠道（AOSP android.app.NotificationChannel；§31.7 C 档 lib POJO——渠道态）。
 */
public class NotificationChannel implements Parcelable {

    public static final String DEFAULT_CHANNEL_ID = "miscellaneous";

    public static final int IMPORTANCE_NONE = 0;
    public static final int IMPORTANCE_MIN = 1;
    public static final int IMPORTANCE_LOW = 2;
    public static final int IMPORTANCE_DEFAULT = 3;
    public static final int IMPORTANCE_HIGH = 4;
    public static final int IMPORTANCE_MAX = 5;

    private final String mId;
    private CharSequence mName;
    private String mDescription;
    private int mImportance = IMPORTANCE_DEFAULT;
    private boolean mVibrationEnabled;
    private long[] mVibration;
    private Uri mSound;
    private int mLightColor;
    private boolean mLights;
    private boolean mShowBadge = true;

    public NotificationChannel(String id, CharSequence name, int importance) {
        this.mId = id;
        this.mName = name;
        this.mImportance = importance;
    }

    public String getId() {
        return this.mId;
    }

    public CharSequence getName() {
        return this.mName;
    }

    public void setName(CharSequence name) {
        this.mName = name;
    }

    public String getDescription() {
        return this.mDescription;
    }

    public void setDescription(String description) {
        this.mDescription = description;
    }

    public int getImportance() {
        return this.mImportance;
    }

    public void setSound(Uri sound, android.media.AudioAttributes attributes) {
        this.mSound = sound;
    }

    public Uri getSound() {
        return this.mSound;
    }

    public void enableVibration(boolean vibration) {
        this.mVibrationEnabled = vibration;
    }

    public boolean shouldVibrate() {
        return this.mVibrationEnabled;
    }

    public void setVibrationPattern(long[] vibrationPattern) {
        this.mVibration = vibrationPattern;
    }

    public long[] getVibrationPattern() {
        return this.mVibration;
    }

    public void enableLights(boolean lights) {
        this.mLights = lights;
    }

    public boolean shouldShowLights() {
        return this.mLights;
    }

    public void setLightColor(int argb) {
        this.mLightColor = argb;
    }

    public int getLightColor() {
        return this.mLightColor;
    }

    public void setShowBadge(boolean showBadge) {
        this.mShowBadge = showBadge;
    }

    public boolean canShowBadge() {
        return this.mShowBadge;
    }

    public void setGroup(String groupId) {
    }

    public String getGroup() {
        return null;
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }

    @Override
    public String toString() {
        return "NotificationChannel{" + this.mId + "/" + this.mName + "}";
    }
}
