package android.hardware;

/** AOSP android.hardware.SensorEventListener（震动/摇一摇等传感器消费面）。 */
public interface SensorEventListener {
    void onSensorChanged(SensorEvent event);

    void onAccuracyChanged(Sensor sensor, int accuracy);
}
