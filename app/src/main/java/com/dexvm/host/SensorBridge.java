package com.dexvm.host;

import android.content.Context;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.util.Log;

import java.util.List;

/**
 * 宿主传感器桥：在宿主主线程注册真实 SensorManager 监听（全部传感器），
 * 把传感器描述表与实时事件推入 VM（core/sensor_bridge.cc 队列）。
 *
 * <p>guest 侧 {@code android.hardware.SensorManager}（lib Java）经
 * {@code android.hardware.VmSensors} 轮询事件并在 guest 主会话线程回调
 * {@link SensorEventListener}——真数据、真时间戳，不伪造传感器身份。
 *
 * <p>线程模型：register 必须发生在有 Looper 的线程（宿主主线程）；
 * nativeSensorPush 只入 VM 互斥队列，不触 guest 堆。
 */
public final class SensorBridge {
    private static final String TAG = "DexVmSensors";

    private static boolean sStarted;

    private SensorBridge() {
    }

    /** 幂等启动（宿主主线程调用；重复调用直接返回）。 */
    public static synchronized void start(Context context) {
        if (sStarted || context == null) return;
        sStarted = true;
        try {
            Context app = context.getApplicationContext();
            SensorManager sm = (SensorManager) (app != null ? app : context)
                    .getSystemService(Context.SENSOR_SERVICE);
            if (sm == null) {
                NativeBridge.nativeSensorSetList("");
                Log.i(TAG, "no sensor service");
                return;
            }
            List<Sensor> sensors = sm.getSensorList(Sensor.TYPE_ALL);
            StringBuilder sb = new StringBuilder();
            int registered = 0;
            if (sensors != null) {
                for (Sensor s : sensors) {
                    if (s == null) continue;
                    sb.append(s.getType()).append('|')
                            .append(clean(s.getName())).append('|')
                            .append(clean(s.getVendor())).append('|')
                            .append(s.getVersion()).append('|')
                            .append(s.getMaximumRange()).append('|')
                            .append(s.getResolution()).append('|')
                            .append(s.getPower()).append('|')
                            .append(s.getMinDelay()).append('\n');
                    final int type = s.getType();
                    SensorEventListener listener = new SensorEventListener() {
                        @Override
                        public void onSensorChanged(SensorEvent event) {
                            try {
                                NativeBridge.nativeSensorPush(type, event.values,
                                        event.timestamp);
                            } catch (Throwable ignored) {
                            }
                        }

                        @Override
                        public void onAccuracyChanged(Sensor sensor, int accuracy) {
                        }
                    };
                    try {
                        if (sm.registerListener(listener, s, SensorManager.SENSOR_DELAY_GAME)) {
                            registered++;
                        }
                    } catch (Throwable ignored) {
                    }
                }
            }
            NativeBridge.nativeSensorSetList(sb.toString());
            Log.i(TAG, "sensor bridge: " + (sensors == null ? 0 : sensors.size())
                    + " sensors, " + registered + " registered");
        } catch (Throwable t) {
            Log.w(TAG, "sensor bridge start failed", t);
            try {
                NativeBridge.nativeSensorSetList("");
            } catch (Throwable ignored) {
            }
        }
    }

    private static String clean(String s) {
        if (s == null) return "";
        return s.replace('|', ' ').replace('\n', ' ').replace('\r', ' ');
    }
}
