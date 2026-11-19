package android.app;

import com.dexvm.runtime.Host;

/**
 * UI 模式服务（AOSP android.app.UiModeManager 公开面）。
 *
 * <p>宿主桥接：宿主启动时把真实 UiModeManager 状态快照推入 VM
 * （com.dexvm.runtime.Host 的 uimode.* 键）；本类读取宿主真值（车载/桌面/
 * 夜间判定进入真实分支）。夜间模式写面（setNightMode）对非系统应用本身
 * 无效，VM 侧保持 no-op。
 */
public class UiModeManager {
    public static final int MODE_NIGHT_AUTO = 0;
    public static final int MODE_NIGHT_NO = 1;
    public static final int MODE_NIGHT_YES = 2;
    public static final int MODE_NIGHT_CUSTOM = 3;

    public static final int UI_MODE_TYPE_NORMAL = 1;
    public static final int UI_MODE_TYPE_DESK = 2;
    public static final int UI_MODE_TYPE_CAR = 3;
    public static final int UI_MODE_TYPE_TELEVISION = 4;
    public static final int UI_MODE_TYPE_APPLIANCE = 5;
    public static final int UI_MODE_TYPE_WATCH = 6;
    public static final int UI_MODE_TYPE_VR_HEADSET = 7;
    public static final int UI_MODE_NIGHT_NO = 1;
    public static final int UI_MODE_NIGHT_YES = 2;

    public int getCurrentModeType() {
        return parseInt(Host.get("uimode.currentModeType"), UI_MODE_TYPE_NORMAL);
    }

    public int getNightMode() {
        return parseInt(Host.get("uimode.nightMode"), MODE_NIGHT_NO);
    }

    public boolean isNightMode() {
        return getNightMode() == MODE_NIGHT_YES;
    }

    public void setNightMode(int mode) {
        // AOSP：非系统应用无 MODIFY_DAY_NIGHT_MODE，调用无效；保持 no-op。
    }

    private static int parseInt(String value, int fallback) {
        if (value == null || value.isEmpty()) return fallback;
        try {
            return Integer.parseInt(value.trim());
        } catch (Throwable ignored) {
            return fallback;
        }
    }
}
