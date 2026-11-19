package android.media;

import android.os.Parcel;
import android.os.Parcelable;

/**
 * 音频属性（AOSP android.media.AudioAttributes 公开面；§31.7 C 档 lib POJO）。
 */
public final class AudioAttributes implements Parcelable {

    public static final int USAGE_UNKNOWN = 0;
    public static final int USAGE_MEDIA = 1;
    public static final int USAGE_VOICE_COMMUNICATION = 2;
    public static final int USAGE_VOICE_COMMUNICATION_SIGNALLING = 3;
    public static final int USAGE_ALARM = 4;
    public static final int USAGE_NOTIFICATION = 5;
    public static final int USAGE_NOTIFICATION_TELEPHONY_RINGTONE = 6;
    public static final int USAGE_NOTIFICATION_COMMUNICATION_REQUEST = 7;
    public static final int USAGE_NOTIFICATION_COMMUNICATION_INSTANT = 8;
    public static final int USAGE_NOTIFICATION_COMMUNICATION_DELAYED = 9;
    public static final int USAGE_NOTIFICATION_EVENT = 10;
    public static final int USAGE_ASSISTANCE_ACCESSIBILITY = 11;
    public static final int USAGE_ASSISTANCE_NAVIGATION_GUIDANCE = 12;
    public static final int USAGE_ASSISTANCE_SONIFICATION = 13;
    public static final int USAGE_GAME = 14;

    public static final int CONTENT_TYPE_UNKNOWN = 0;
    public static final int CONTENT_TYPE_SPEECH = 1;
    public static final int CONTENT_TYPE_MUSIC = 2;
    public static final int CONTENT_TYPE_MOVIE = 3;
    public static final int CONTENT_TYPE_SONIFICATION = 4;

    private final int mUsage;
    private final int mContentType;
    private final int mFlags;

    AudioAttributes(int usage, int contentType, int flags) {
        this.mUsage = usage;
        this.mContentType = contentType;
        this.mFlags = flags;
    }

    public int getUsage() {
        return this.mUsage;
    }

    public int getContentType() {
        return this.mContentType;
    }

    public int getFlags() {
        return this.mFlags;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof AudioAttributes)) return false;
        AudioAttributes a = (AudioAttributes) o;
        return this.mUsage == a.mUsage && this.mContentType == a.mContentType;
    }

    @Override
    public int hashCode() {
        return this.mUsage * 31 + this.mContentType;
    }

    @Override
    public String toString() {
        return "AudioAttributes[usage=" + this.mUsage + ", contentType=" + this.mContentType + "]";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel dest, int flags) {
    }

    /** 构建器（AOSP AudioAttributes.Builder）。 */
    public static final class Builder {
        private int mUsage = USAGE_UNKNOWN;
        private int mContentType = CONTENT_TYPE_UNKNOWN;
        private int mFlags;

        public Builder() {
        }

        public Builder(AudioAttributes aa) {
            if (aa != null) {
                this.mUsage = aa.mUsage;
                this.mContentType = aa.mContentType;
                this.mFlags = aa.mFlags;
            }
        }

        public Builder setUsage(int usage) {
            this.mUsage = usage;
            return this;
        }

        public Builder setContentType(int contentType) {
            this.mContentType = contentType;
            return this;
        }

        public Builder setFlags(int flags) {
            this.mFlags = flags;
            return this;
        }

        public AudioAttributes build() {
            return new AudioAttributes(this.mUsage, this.mContentType, this.mFlags);
        }
    }
}
