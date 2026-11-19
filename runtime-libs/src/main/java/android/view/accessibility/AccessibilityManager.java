package android.view.accessibility;

import android.content.Context;

import java.util.Collections;
import java.util.List;

/**
 * android.view.accessibility.AccessibilityManager（AOSP 公开面骨架）。
 *
 * <p>VM 无无障碍服务通道：单例面齐备，状态恒关闭、监听注册 no-op、
 * 服务列表为空（RecyclerView/ViewPager2 等构造期只做状态查询与监听注册）。
 */
public class AccessibilityManager {

    public interface AccessibilityStateChangeListener {
        void onAccessibilityStateChanged(boolean enabled);
    }

    public interface TouchExplorationStateChangeListener {
        void onTouchExplorationStateChanged(boolean enabled);
    }

    private static AccessibilityManager sInstance;

    public static AccessibilityManager getInstance(Context context) {
        if (sInstance == null) {
            sInstance = new AccessibilityManager();
        }
        return sInstance;
    }

    public boolean isEnabled() {
        return false;
    }

    public boolean isTouchExplorationEnabled() {
        return false;
    }

    public boolean isHighTextContrastEnabled() {
        return false;
    }

    public void addAccessibilityStateChangeListener(AccessibilityStateChangeListener listener) {
    }

    public void removeAccessibilityStateChangeListener(AccessibilityStateChangeListener listener) {
    }

    public void addTouchExplorationStateChangeListener(TouchExplorationStateChangeListener listener) {
    }

    public void removeTouchExplorationStateChangeListener(
            TouchExplorationStateChangeListener listener) {
    }

    public List<AccessibilityServiceInfo> getEnabledAccessibilityServiceList(
            int feedbackTypeFlags) {
        return Collections.emptyList();
    }

    public List<AccessibilityServiceInfo> getInstalledAccessibilityServiceList() {
        return Collections.emptyList();
    }

    public boolean isRequestFromAccessibilityTool() {
        return false;
    }

    public void interrupt() {
    }
}
