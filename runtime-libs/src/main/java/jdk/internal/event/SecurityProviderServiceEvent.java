package jdk.internal.event;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：
 * jdk.internal.event.SecurityProviderServiceEvent 最小面（JFR provider 服务
 * 事件；isTurnedOn 恒 false = 事件不启用语义，commit 空实现）。消费面：
 * java.security.Provider.getService 事件钩子（okio MessageDigest 查服务链，
 * 缺类即 NoClassDefFoundError）。字段面按消费方直写形态（provider/type/
 * algorithm）对照 JBR 类形状。
 */
public final class SecurityProviderServiceEvent extends Event {

    public String type;
    public String algorithm;
    public String provider;

    public SecurityProviderServiceEvent() {
    }

    public static boolean isTurnedOn() {
        return false;
    }
}
