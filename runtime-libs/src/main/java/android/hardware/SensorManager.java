package android.hardware;

import android.os.Handler;
import android.os.Looper;

import java.util.ArrayList;
import java.util.List;

/**
 * 传感器服务（AOSP android.hardware.SensorManager 公开面）。
 *
 * <p>宿主桥接：宿主 {@code SensorBridge} 注册真实 SensorManager 监听，把传感器
 * 描述表与实时事件推入 VM；本类经 {@link VmSensors} 轮询事件，在 guest 主
 * 会话线程回调 {@link SensorEventListener}（真数据/真时间戳，不伪造设备）。
 *
 * <p>线程语义：AOSP 在下发注册线程的 Looper 回调；本实现统一在 guest 主
 * Looper 派发（registerListener 的 Handler 参数保留签名，派发线程同主线程）。
 */
public class SensorManager {
    public static final String SENSOR_SERVICE = "sensor";
    public static final int SENSOR_DELAY_FASTEST = 0;
    public static final int SENSOR_DELAY_GAME = 1;
    public static final int SENSOR_DELAY_UI = 2;
    public static final int SENSOR_DELAY_NORMAL = 3;

    private static final Object sLock = new Object();
    private static final List<Record> sRecords = new ArrayList<>();
    private static final List<Sensor> sSensors = new ArrayList<>();
    private static final float[] sValues = new float[16];
    private static final long[] sStamp = new long[2];
    private static boolean sSensorsLoaded;
    private static boolean sPumping;
    private static final Handler sHandler = new Handler(Looper.getMainLooper());

    private static final class Record {
        final SensorEventListener listener;
        final int type;

        Record(SensorEventListener listener, int type) {
            this.listener = listener;
            this.type = type;
        }
    }

    private static final Runnable sPump = new Runnable() {
        @Override
        public void run() {
            dispatchPending();
            synchronized (sLock) {
                if (sRecords.isEmpty()) {
                    sPumping = false;
                    return;
                }
            }
            sHandler.postDelayed(this, 16);
        }
    };

    /** 宿主描述表 -> Sensor 实例表（一次装载；空表 = 宿主无传感器）。 */
    private static void ensureSensors() {
        synchronized (sLock) {
            if (sSensorsLoaded) return;
            sSensorsLoaded = true;
            String encoded = VmSensors.list();
            if (encoded == null || encoded.isEmpty()) return;
            for (String line : encoded.split("\n")) {
                if (line.isEmpty()) continue;
                String[] f = line.split("\\|", -1);
                if (f.length < 8) continue;
                Sensor s = new Sensor();
                try {
                    s.type = Integer.parseInt(f[0]);
                    s.name = f[1];
                    s.vendor = f[2];
                    s.version = Integer.parseInt(f[3]);
                    s.maxRange = Float.parseFloat(f[4]);
                    s.resolution = Float.parseFloat(f[5]);
                    s.power = Float.parseFloat(f[6]);
                    s.minDelay = Integer.parseInt(f[7]);
                } catch (Throwable ignored) {
                    continue;
                }
                sSensors.add(s);
            }
        }
    }

    private static Sensor findSensor(int type) {
        synchronized (sLock) {
            for (Sensor s : sSensors) {
                if (s.type == type) return s;
            }
        }
        return null;
    }

    /** 宿主事件泵：一次排空队列，按注册类型派发（主 Looper 线程）。 */
    private static void dispatchPending() {
        while (true) {
            int type = VmSensors.pollEvent(sValues, sStamp);
            if (type < 0) break;
            int count = (int) sStamp[1];
            if (count < 0) count = 0;
            if (count > sValues.length) count = sValues.length;
            float[] vals = new float[count > 0 ? count : 1];
            System.arraycopy(sValues, 0, vals, 0, count);
            Sensor sensor = findSensor(type);
            SensorEvent event = new SensorEvent(sensor, vals, 0, sStamp[0]);
            Record[] snapshot;
            synchronized (sLock) {
                snapshot = sRecords.toArray(new Record[0]);
            }
            if (sEventLogCount < 3) {
                sEventLogCount++;
                try {
                    android.util.Log.i("DexVmGuestSensor", "event type=" + type
                            + " count=" + count + " listeners=" + snapshot.length);
                } catch (Throwable ignored) {
                }
            }
            for (Record r : snapshot) {
                if (r.type != Sensor.TYPE_ALL && r.type != type) continue;
                try {
                    r.listener.onSensorChanged(event);
                } catch (Throwable ignored) {
                }
            }
        }
    }

    public List<Sensor> getSensorList(int type) {
        ensureSensors();
        List<Sensor> out = new ArrayList<>();
        synchronized (sLock) {
            for (Sensor s : sSensors) {
                if (type == Sensor.TYPE_ALL || s.type == type) out.add(s);
            }
        }
        return out;
    }

    public Sensor getDefaultSensor(int type) {
        ensureSensors();
        return findSensor(type);
    }

    public Sensor getDefaultSensor(int type, boolean wakeUp) {
        return getDefaultSensor(type);
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rateUs) {
        return register(listener, sensor);
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rateUs,
                                    int maxLatency) {
        return register(listener, sensor);
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rateUs,
                                    Handler handler) {
        return register(listener, sensor);
    }

    public boolean registerListener(SensorEventListener listener, Sensor sensor, int rateUs,
                                    int maxLatency, Handler handler) {
        return register(listener, sensor);
    }

    private static int sEventLogCount;

    private boolean register(SensorEventListener listener, Sensor sensor) {
        if (listener == null) return false;
        final int type = sensor == null ? Sensor.TYPE_ALL : sensor.type;
        synchronized (sLock) {
            sRecords.add(new Record(listener, type));
            if (!sPumping) {
                sPumping = true;
                sHandler.post(sPump);
            }
        }
        try {
            android.util.Log.i("DexVmGuestSensor", "register type=" + type
                    + " name=" + (sensor != null ? sensor.name : "all"));
        } catch (Throwable ignored) {
        }
        return true;
    }

    public void unregisterListener(SensorEventListener listener) {
        if (listener == null) return;
        synchronized (sLock) {
            for (int i = sRecords.size() - 1; i >= 0; i--) {
                if (sRecords.get(i).listener == listener) sRecords.remove(i);
            }
        }
    }

    public void unregisterListener(SensorEventListener listener, Sensor sensor) {
        if (listener == null) return;
        final int type = sensor == null ? Sensor.TYPE_ALL : sensor.type;
        synchronized (sLock) {
            for (int i = sRecords.size() - 1; i >= 0; i--) {
                Record r = sRecords.get(i);
                if (r.listener == listener && r.type == type) sRecords.remove(i);
            }
        }
    }

    public void unregisterListener(SensorEventListener listener, int sensorType) {
        if (listener == null) return;
        synchronized (sLock) {
            for (int i = sRecords.size() - 1; i >= 0; i--) {
                Record r = sRecords.get(i);
                if (r.listener == listener && r.type == sensorType) sRecords.remove(i);
            }
        }
    }
}
