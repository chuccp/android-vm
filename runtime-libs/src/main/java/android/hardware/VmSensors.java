package android.hardware;

/**
 * VM 传感器钩子（编译期脚手架）：实现由 libdexvm.so 内建类提供
 * （core/framework_builtins.cc 注册 intrinsic；builtin_classes.def 排除本类
 * 的 Java 版本，双实现零容忍——本文件仅让 lib 源码可编译）。
 *
 * <p>实现口径（core/framework_sensor.cc）：宿主 SensorBridge.java 注册真
 * SensorManager 监听 -> nativeSensorPush 入 VM 队列；本钩子向 guest 暴露
 * 描述表与事件轮询。
 */
public final class VmSensors {
    private VmSensors() {
    }

    /**
     * 传感器描述表：每行 "type|name|vendor|version|maxRange|resolution|power|minDelay"，
     * "\n" 分隔；无宿主传感器时为空串。
     */
    public static String list() {
        throw new UnsupportedOperationException("VmSensors.list: VM intrinsic");
    }

    /**
     * 取一条事件：values 为 float[] 出参（按序填 c 个 float 位型），
     * timestamp[0]=事件时间戳（ns）、timestamp[1]=有效值个数；返回 sensor type，
     * 无事件返回 -1。
     */
    public static int pollEvent(float[] values, long[] timestamp) {
        throw new UnsupportedOperationException("VmSensors.pollEvent: VM intrinsic");
    }
}
