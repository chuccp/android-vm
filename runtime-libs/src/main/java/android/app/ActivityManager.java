package android.app;

/**
 * AOSP android.app.ActivityManager 的最小 lib 面（类型解析 + 常用只读查询）。
 *
 * Room（androidx.room）、zlab AB 数据库、各家 SDK 的 <clinit> 会做
 * `context.getSystemService("activity")` 的 instance-of/check-cast 并读取
 * 内存类别；类本身缺失时 NoClassDefFoundError 会让整条 clinit 失败。
 * 这里只提供类型与保守值，不伪造进程/任务枚举（返回空）。
 */
public class ActivityManager {

    public static class MemoryInfo {
        public long availMem;
        public long totalMem;
        public long threshold;
        public boolean lowMemory;
        public int pid;
    }

    public static class RunningAppProcessInfo {
        public static final int IMPORTANCE_FOREGROUND = 100;
        public static final int IMPORTANCE_VISIBLE = 200;
        public static final int IMPORTANCE_SERVICE = 300;
        public static final int IMPORTANCE_BACKGROUND = 400;

        public String processName;
        public int pid;
        public int uid;
        public int importance;
        public String[] pkgList;
        public int[] pkgListUid;
    }

    public static class AppTask {
        public void finishAndRemoveTask() {
        }

        public int getTaskId() {
            return -1;
        }
    }

    public ActivityManager() {
    }

    /** API 19+ 的低内存设备判据（VM 环境恒 false，走常规 WAL 分支）。 */
    public boolean isLowRamDevice() {
        return false;
    }

    public int getMemoryClass() {
        return 256;
    }

    public int getLargeMemoryClass() {
        return 512;
    }

    public long getTotalMemory() {
        return -1L;
    }

    public void getMemoryInfo(MemoryInfo outInfo) {
        if (outInfo == null) return;
        outInfo.availMem = -1L;
        outInfo.totalMem = -1L;
        outInfo.threshold = -1L;
        outInfo.lowMemory = false;
    }

    public java.util.List<RunningAppProcessInfo> getRunningAppProcesses() {
        return new java.util.ArrayList<RunningAppProcessInfo>();
    }

    public java.util.List<AppTask> getAppTasks() {
        return new java.util.ArrayList<AppTask>();
    }

    public boolean isBackgroundRestricted() {
        return false;
    }
}
