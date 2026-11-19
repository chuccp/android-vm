package android.location;

import android.os.Bundle;
import android.os.Parcel;
import android.os.Parcelable;

/**
 * 位置（AOSP android.location.Location 公开面；§31.7 C 档 lib POJO——经纬度/精度
 * 字段态）。
 */
public class Location implements Parcelable {

    public static final String GPS_PROVIDER = "gps";
    public static final String NETWORK_PROVIDER = "network";
    public static final String PASSIVE_PROVIDER = "passive";
    public static final String FUSED_PROVIDER = "fused";

    private String mProvider;
    private double mLatitude;
    private double mLongitude;
    private long mTime;
    private long mElapsedRealtimeNanos;
    private boolean mHasAccuracy;
    private float mAccuracy;
    private float mAltitude;
    private float mSpeed;
    private float mBearing;
    private Bundle mExtras;

    public Location(String provider) {
        this.mProvider = provider;
    }

    public Location(Location l) {
        if (l != null) set(l);
    }

    public void set(Location l) {
        if (l == null) return;
        this.mProvider = l.mProvider;
        this.mLatitude = l.mLatitude;
        this.mLongitude = l.mLongitude;
        this.mTime = l.mTime;
        this.mElapsedRealtimeNanos = l.mElapsedRealtimeNanos;
        this.mHasAccuracy = l.mHasAccuracy;
        this.mAccuracy = l.mAccuracy;
        this.mAltitude = l.mAltitude;
        this.mSpeed = l.mSpeed;
        this.mBearing = l.mBearing;
    }

    public String getProvider() {
        return this.mProvider;
    }

    public void setProvider(String provider) {
        this.mProvider = provider;
    }

    public long getTime() {
        return this.mTime;
    }

    public void setTime(long time) {
        this.mTime = time;
    }

    public long getElapsedRealtimeNanos() {
        return this.mElapsedRealtimeNanos;
    }

    public void setElapsedRealtimeNanos(long time) {
        this.mElapsedRealtimeNanos = time;
    }

    public double getLatitude() {
        return this.mLatitude;
    }

    public void setLatitude(double latitude) {
        this.mLatitude = latitude;
    }

    public double getLongitude() {
        return this.mLongitude;
    }

    public void setLongitude(double longitude) {
        this.mLongitude = longitude;
    }

    public boolean hasAltitude() {
        return false;
    }

    public double getAltitude() {
        return this.mAltitude;
    }

    public void setAltitude(double altitude) {
        this.mAltitude = (float) altitude;
    }

    public boolean hasSpeed() {
        return this.mSpeed > 0;
    }

    public float getSpeed() {
        return this.mSpeed;
    }

    public void setSpeed(float speed) {
        this.mSpeed = speed;
    }

    public boolean hasBearing() {
        return this.mBearing > 0;
    }

    public float getBearing() {
        return this.mBearing;
    }

    public void setBearing(float bearing) {
        this.mBearing = bearing;
    }

    public boolean hasAccuracy() {
        return this.mHasAccuracy;
    }

    public float getAccuracy() {
        return this.mAccuracy;
    }

    public void setAccuracy(float accuracy) {
        this.mHasAccuracy = true;
        this.mAccuracy = accuracy;
    }

    public Bundle getExtras() {
        return this.mExtras;
    }

    public void setExtras(Bundle extras) {
        this.mExtras = extras;
    }

    public void reset() {
    }

    public float distanceTo(Location dest) {
        return 0;
    }

    public static float distanceBetween(double startLatitude, double startLongitude,
                                        double endLatitude, double endLongitude,
                                        float[] results) {
        return 0;
    }

    @Override
    public String toString() {
        return "Location[" + this.mProvider + " " + this.mLatitude + "," + this.mLongitude + "]";
    }

    @Override
    public int describeContents() {
        return 0;
    }

    @Override
    public void writeToParcel(Parcel parcel, int flags) {
    }
}
