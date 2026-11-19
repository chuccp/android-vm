package android.view;

/**
 * 逐帧指标（AOSP android.view.FrameMetrics 公开面）；VM 无 HWUI 帧指标源，
 * 本类作为 Window.OnFrameMetricsAvailableListener 的参数类型链接面存在。
 */
public final class FrameMetrics {
    public static final int UNKNOWN_DELAY_DURATION = 0;
    public static final int INPUT_HANDLING_DURATION = 1;
    public static final int ANIMATION_DURATION = 2;
    public static final int LAYOUT_MEASURE_DURATION = 3;
    public static final int DRAW_DURATION = 4;
    public static final int SYNC_DURATION = 5;
    public static final int COMMAND_ISSUE_DURATION = 6;
    public static final int SWAP_BUFFERS_DURATION = 7;
    public static final int TOTAL_DURATION = 8;
    public static final int FIRST_DRAW_FRAME = 9;

    private final long[] mMetrics = new long[10];

    public FrameMetrics(FrameMetrics other) {
        if (other != null) {
            System.arraycopy(other.mMetrics, 0, mMetrics, 0, mMetrics.length);
        }
    }

    public long getMetric(int id) {
        if (id < 0 || id >= mMetrics.length) return 0;
        return mMetrics[id];
    }
}
