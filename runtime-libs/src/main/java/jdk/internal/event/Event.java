package jdk.internal.event;

/**
 * 生成补源（sun-compat，随 runtime.dex 入包）：jdk.internal.event.Event 最小面
 * （JFR 事件基类；本 VM 不启用 JFR 事件流——isEnabled 恒 false，hook 面为
 * 空实现）。java.security.Provider 等 vendored 源的 jdk.internal.event 依赖
 * 在此收口（JBR 25 运行面无此类，缺则 NoClassDefFoundError）。
 */
public abstract class Event {

    public boolean isEnabled() {
        return false;
    }

    public void begin() {
    }

    public void end() {
    }

    public void commit() {
    }
}
