package com.dexvm.runtime;

/**
 * 宿主桥接值面（VM 内建类 Lcom/dexvm/runtime/Host; 的 Java 可见入口）：宿主启动
 * 时把系统服务真值（telephony.* 等）快照推入 VM，guest lib 类经本类读取。
 * 实现体在 libdexvm.so（intrinsic）；未桥接键返回 null。
 */
public final class Host {
    private Host() {
    }

    public static native String get(String key);
}
