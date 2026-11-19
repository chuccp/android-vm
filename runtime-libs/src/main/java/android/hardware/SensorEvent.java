package android.hardware;

/** AOSP android.hardware.SensorEvent（传感器事件载体；事件来自宿主桥）。 */
public final class SensorEvent {
    public final float[] values;
    public Sensor sensor;
    public int accuracy;
    public long timestamp;

    public SensorEvent(int valueSize) {
        this.values = new float[valueSize > 0 ? valueSize : 3];
    }

    public SensorEvent(Sensor sensor, float[] values, int accuracy, long timestamp) {
        this.sensor = sensor;
        this.values = values != null ? values : new float[0];
        this.accuracy = accuracy;
        this.timestamp = timestamp;
    }
}
