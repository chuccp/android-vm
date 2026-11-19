package android.view.accessibility;

/**
 * android.view.accessibility.AccessibilityServiceInfo（AOSP 公开数据面骨架）。
 *
 * <p>VM 无无障碍服务，字段面齐备供类型/取值使用；行为面后置。
 */
public class AccessibilityServiceInfo {
    public static final int FEEDBACK_ALL_MASK = -1;
    public static final int FEEDBACK_AUDIBLE = 4;
    public static final int FEEDBACK_HAPTIC = 2;
    public static final int FEEDBACK_SPOKEN = 1;
    public static final int FEEDBACK_VISUAL = 8;
    public static final int FEEDBACK_GENERIC = 16;
    public static final int FEEDBACK_BRAILLE = 32;
    public static final int FLAG_DEFAULT = 0;
    public static final int FLAG_INCLUDE_NOT_IMPORTANT_VIEWS = 2;
    public static final int FLAG_REPORT_VIEW_IDS = 16;
    public static final int FLAG_REQUEST_ENHANCED_WEB_ACCESSIBILITY = 8;
    public static final int FLAG_REQUEST_FILTER_KEY_EVENTS = 32;
    public static final int FLAG_REQUEST_TOUCH_EXPLORATION_MODE = 4;

    public int eventTypes;
    public int feedbackType;
    public int flags;

    public AccessibilityServiceInfo() {
    }

    public int getEventTypes() {
        return eventTypes;
    }

    public void setEventTypes(int eventTypes) {
        this.eventTypes = eventTypes;
    }

    public int getFeedbackType() {
        return feedbackType;
    }

    public void setFeedbackType(int feedbackType) {
        this.feedbackType = feedbackType;
    }

    public int getFlags() {
        return flags;
    }

    public void setFlags(int flags) {
        this.flags = flags;
    }
}
