package android.hardware;

/**
 * 传感器描述（AOSP android.hardware.Sensor 公开字段面；VM 无传感器 HAL，
 * 仅作为 SensorManager 返回面的类型载体，实例不会产生）。
 */
public final class Sensor {
    public static final int TYPE_ALL = -1;
    public static final int TYPE_ACCELEROMETER = 1;
    public static final int TYPE_MAGNETIC_FIELD = 2;
    public static final int TYPE_ORIENTATION = 3;
    public static final int TYPE_GYROSCOPE = 4;
    public static final int TYPE_LIGHT = 5;
    public static final int TYPE_PRESSURE = 6;
    public static final int TYPE_PROXIMITY = 8;
    public static final int TYPE_GRAVITY = 9;
    public static final int TYPE_LINEAR_ACCELERATION = 10;
    public static final int TYPE_ROTATION_VECTOR = 11;
    public static final int TYPE_STEP_COUNTER = 19;
    public static final int TYPE_STEP_DETECTOR = 18;

    public String name = "";
    public String vendor = "";
    public int version = 1;
    public float maxRange = 0f;
    public float resolution = 0f;
    public float power = 0f;
    public int minDelay = 0;
    public int type = 0;
    public int accuracy = 0;
    public int reportingMode = 0;
    public boolean isWakeUpSensor = false;
    public int maxDelay = 0;
    public String stringType = "";

    public int getType() {
        return type;
    }

    public String getName() {
        return name;
    }

    public String getVendor() {
        return vendor;
    }

    public float getMaximumRange() {
        return maxRange;
    }

    public float getResolution() {
        return resolution;
    }
}
