package uibridge;

import android.view.View;

/**
 * UI 桥接点击探针（guest 监听器）：viewId -> guest listener 注入派发的验收面。
 * onClick 落静态字段（tests/core/ui_bridge_smoke.cc 读取断言）；真实 guest
 * 字节码经解释器执行（非 intrinsic）。
 */
public final class UiBridgeClickProbe implements View.OnClickListener {
    public static int clicks = 0;
    public static int lastViewId = -999;
    public static View lastView = null;

    @Override
    public void onClick(View v) {
        clicks++;
        lastView = v;
        lastViewId = (v != null) ? v.getId() : -998;
    }

    public static void reset() {
        clicks = 0;
        lastView = null;
        lastViewId = -999;
    }
}
