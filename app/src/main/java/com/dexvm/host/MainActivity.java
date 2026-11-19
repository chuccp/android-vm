package com.dexvm.host;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.OpenableColumns;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowInsets;
import android.widget.BaseAdapter;
import android.widget.FrameLayout;
import android.widget.GridView;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * 宿主入口（Android 原生 View；v1 桌面信息架构）。
 *
 * 桌面：已导入应用（files/apps/*.apk）网格 + "+" 导入瓦片；无应用时只有 "+"。
 * 打开：guest 应用经自研 VM 启动——启动/考卷过程实时显示进度日志（轮询
 *       nativeGetRunProgress），有首帧时桥接物化为宿主真 View 树。
 * 导入：SAF ACTION_OPEN_DOCUMENT，APK 拷入 files/apps/ 持久化。
 *
 * 设备考卷契约（tools/target_exam_tests.sh）：
 * - run_target_exam=true / exam_round：跑 target-app 8 轮（tag DexVmTargetExam，
 *   "TARGET EXAM DONE" 结束标记；完成时首帧 guest 树已上屏供 uiautomator 断言）；
 * - run_exam=true：对导入的知乎 APK 跑 nativeRunExam（tag DexVmExam，"EXAM DONE"）。
 */
public class MainActivity extends Activity {
    private static final String TAG = "DexVmExam";
    private static final String TAG_TARGET = "DexVmTargetExam";
    private static final String TAG_OPEN = "DexVmOpen";
    private static final int REQ_IMPORT = 7001;

    // v1 桌面配色
    private static final int COLOR_BG_BOTTOM = 0xFFDDE7F4;
    private static final int COLOR_CARD = 0xFFFFFFFF;
    private static final int COLOR_CARD_SOFT = 0xF2FFFFFF;
    private static final int COLOR_TEXT = 0xFF161B26;
    private static final int COLOR_TEXT_SECONDARY = 0xFF697587;
    private static final int COLOR_ACCENT = 0xFF315CAB;
    private static final int COLOR_ACCENT_SOFT = 0xFFF3F6FC;
    private static final int COLOR_DIAG_BG = 0xFF071C24;
    private static final int COLOR_DIAG_TEXT = 0xFFB7D8D0;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final List<AppEntry> apps = new ArrayList<>();
    private boolean scanning = false;

    // 桌面
    private View desktopView;
    private GridView appGrid;
    private GridAdapter gridAdapter;
    private TextView appCountView;

    // 运行页
    private View runningView;
    private TextView runTitleView;
    private TextView runStatusView;
    private TextView runLogView;
    private ScrollView runLogScroll;
    private FrameLayout guestSlot;
    private TextView backView;
    private FrameLayout guestCardView;
    private FrameLayout logCardView;
    private TextView logToggleView;
    private boolean guestAvailable = false;
    private View bridgedView;
    private String activeApkPath;
    private volatile boolean running = false;
    private Runnable progressPoll;
    // 常驻会话面（打开 app 后保持 VM 存活：增量帧 + 点击回灌）
    private boolean sessionMode = false;
    private boolean sessionExitRequested = false;
    private UiBridgeMaterializer sessionMaterializer;
    private View sessionRootView;
    private int sessionFramesApplied = 0;
    private Runnable sessionPoll;
    // AOSP starting window（VM 解析，宿主显示；首帧/切换后不再应用）
    private boolean startingWindowApplied = false;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        // guest TLS 桥接线程（幂等）：宿主真 TLS 栈承接 guest javax.net.ssl。
        try {
            TlsBridge.start();
        } catch (Throwable t) {
            Log.e(TAG_OPEN, "TlsBridge start failed", t);
        }
        // 宿主字体 worker（guest 字体桥；fiber 不能直接调宿主 JNI）。
        try {
            FontBridge.start();
        } catch (Throwable t) {
            Log.e(TAG_OPEN, "FontBridge start failed", t);
        }
        buildUi();
        refreshApps();
        handleIntent(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        handleIntent(intent);
    }

    @Override
    public void onBackPressed() {
        if (running) {
            if (sessionMode) {
                requestSessionExit();
            } else {
                Toast.makeText(this, "虚拟机运行中，请稍候…", Toast.LENGTH_SHORT).show();
            }
            return;
        }
        if (runningView != null && runningView.getVisibility() == View.VISIBLE) {
            showDesktop();
            return;
        }
        super.onBackPressed();
    }

    /** 常驻会话退出：置停止标志，会话线程返回后回桌面。 */
    private void requestSessionExit() {
        sessionExitRequested = true;
        runStatusView.setText("正在退出…");
        Toast.makeText(this, "正在退出应用…", Toast.LENGTH_SHORT).show();
        try {
            NativeBridge.nativeSessionStop();
        } catch (Throwable ignored) {
        }
    }

    // =====================================================================
    // 界面骨架
    // =====================================================================

    private void buildUi() {
        FrameLayout root = new FrameLayout(this);
        GradientDrawable bg = new GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                new int[] {0xFFFBFCFE, 0xFFEDF2FA, COLOR_BG_BOTTOM});
        root.setBackground(bg);

        desktopView = buildDesktop();
        runningView = buildRunning();
        runningView.setVisibility(View.GONE);
        root.addView(desktopView);
        root.addView(runningView);

        root.setOnApplyWindowInsetsListener(new View.OnApplyWindowInsetsListener() {
            @Override
            public WindowInsets onApplyWindowInsets(View v, WindowInsets insets) {
                v.setPadding(insets.getSystemWindowInsetLeft(), insets.getSystemWindowInsetTop(),
                        insets.getSystemWindowInsetRight(), insets.getSystemWindowInsetBottom());
                return insets;
            }
        });
        setContentView(root);
    }

    private View buildDesktop() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);

        // 顶栏：标题 + 日期/数量（v1 桌面同形）
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(22), dp(16), dp(22), dp(10));
        TextView title = new TextView(this);
        title.setText("DexVM");
        title.setTextColor(COLOR_TEXT);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        header.addView(title);
        TextView subtitle = new TextView(this);
        subtitle.setText("  自研 DEX 虚拟机");
        subtitle.setTextColor(COLOR_TEXT_SECONDARY);
        subtitle.setTextSize(TypedValue.COMPLEX_UNIT_SP, 12);
        subtitle.setPadding(0, dp(8), 0, 0);
        header.addView(subtitle);
        header.addView(new View(this), new LinearLayout.LayoutParams(0, 1, 1f));
        appCountView = new TextView(this);
        appCountView.setTextColor(COLOR_TEXT_SECONDARY);
        appCountView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        header.addView(appCountView);
        column.addView(header, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        // 应用网格（末位 "+" 导入瓦片；无应用时只有 +）
        appGrid = new GridView(this);
        appGrid.setNumColumns(4);
        appGrid.setHorizontalSpacing(dp(6));
        appGrid.setVerticalSpacing(dp(14));
        appGrid.setPadding(dp(16), dp(8), dp(16), dp(8));
        appGrid.setClipToPadding(false);
        appGrid.setSelector(new android.graphics.drawable.ColorDrawable(Color.TRANSPARENT));
        gridAdapter = new GridAdapter();
        appGrid.setAdapter(gridAdapter);
        appGrid.setOnItemClickListener((parent, view, position, id) -> {
            if (position < apps.size()) {
                openGuestApp(apps.get(position));
            } else {
                startImport();
            }
        });
        appGrid.setOnItemLongClickListener((parent, view, position, id) -> {
            if (position < apps.size()) {
                confirmRemove(apps.get(position));
                return true;
            }
            return false;
        });
        column.addView(appGrid, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        TextView hint = new TextView(this);
        hint.setText("+ 导入 APK   点击应用由自研虚拟机打开（启动日志与首帧实时呈现）");
        hint.setTextColor(COLOR_TEXT_SECONDARY);
        hint.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        hint.setGravity(Gravity.CENTER);
        hint.setPadding(dp(16), dp(4), dp(16), dp(18));
        column.addView(hint);
        return column;
    }

    private View buildRunning() {
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);

        // 顶栏：返回 + 应用名 + 状态
        LinearLayout header = new LinearLayout(this);
        header.setOrientation(LinearLayout.HORIZONTAL);
        header.setGravity(Gravity.CENTER_VERTICAL);
        header.setPadding(dp(12), dp(12), dp(16), dp(8));
        backView = new TextView(this);
        backView.setText("‹ 桌面");
        backView.setTextColor(COLOR_ACCENT);
        backView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 14);
        backView.setPadding(dp(8), dp(6), dp(8), dp(6));
        backView.setOnClickListener(v -> {
            if (running) {
                if (sessionMode) {
                    requestSessionExit();
                } else {
                    Toast.makeText(this, "虚拟机运行中，请稍候…", Toast.LENGTH_SHORT).show();
                }
                return;
            }
            showDesktop();
        });
        header.addView(backView);
        runTitleView = new TextView(this);
        runTitleView.setTextColor(COLOR_TEXT);
        runTitleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        runTitleView.setTypeface(Typeface.DEFAULT_BOLD);
        runTitleView.setPadding(dp(8), 0, dp(8), 0);
        header.addView(runTitleView, new LinearLayout.LayoutParams(0,
                LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        runStatusView = new TextView(this);
        runStatusView.setTextColor(COLOR_ACCENT);
        runStatusView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        header.addView(runStatusView);
        logToggleView = new TextView(this);
        logToggleView.setText("画面");
        logToggleView.setTextColor(COLOR_ACCENT);
        logToggleView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 11);
        logToggleView.setPadding(dp(10), dp(6), dp(4), dp(6));
        logToggleView.setVisibility(View.GONE);
        logToggleView.setOnClickListener(v -> toggleLogGuest());
        header.addView(logToggleView);
        column.addView(header, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        // 内容区：启动日志全屏（默认）；首帧就绪后切 guest 全屏，可切回日志
        FrameLayout content = new FrameLayout(this);
        column.addView(content, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        // guest 出画（桥接物化真 View 树；1:1 直出，视口不足横向滚动）
        guestCardView = new FrameLayout(this);
        guestCardView.setBackground(card(COLOR_CARD_SOFT, 20));
        guestCardView.setPadding(dp(6), dp(6), dp(6), dp(6));
        guestCardView.setVisibility(View.GONE);
        FrameLayout.LayoutParams guestLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT);
        guestLp.setMargins(dp(12), 0, dp(12), dp(10));
        content.addView(guestCardView, guestLp);
        // guest 画面铺满内容区（540x960 逻辑窗口由 attachGuestRootScaled
        // 缩放映射到宿主尺寸；不再用横竖滚动容器，避免只显示左上角）。
        guestSlot = new FrameLayout(this);
        guestCardView.addView(guestSlot, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));

        // 启动日志面板（全屏；实时轮询 nativeGetRunProgress）
        logCardView = new FrameLayout(this);
        logCardView.setBackground(card(COLOR_DIAG_BG, 18));
        FrameLayout.LayoutParams logLp = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT);
        logLp.setMargins(dp(12), 0, dp(12), dp(10));
        content.addView(logCardView, logLp);

        runLogScroll = new ScrollView(this);
        logCardView.addView(runLogScroll);
        runLogView = new TextView(this);
        runLogView.setTextColor(COLOR_DIAG_TEXT);
        runLogView.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
        runLogView.setTypeface(Typeface.MONOSPACE);
        runLogView.setLineSpacing(0f, 1.3f);
        runLogView.setPadding(dp(14), dp(12), dp(14), dp(12));
        runLogView.setTextIsSelectable(true);
        runLogScroll.addView(runLogView);
        return column;
    }

    private void showDesktop() {
        runningView.setVisibility(View.GONE);
        desktopView.setVisibility(View.VISIBLE);
        refreshApps();
    }

    private void showRunning(String title, String status) {
        desktopView.setVisibility(View.GONE);
        runningView.setVisibility(View.VISIBLE);
        runTitleView.setText(title);
        runStatusView.setText(status);
        guestAvailable = false;
        if (guestCardView != null) guestCardView.setVisibility(View.GONE);
        if (logCardView != null) logCardView.setVisibility(View.VISIBLE);
        if (logToggleView != null) logToggleView.setVisibility(View.GONE);
    }

    /**
     * 启动期默认开屏预览（Android 12 SplashScreen 形态）：白底 + 应用图标，
     * 图标来自已安装 APK（PackageManager 离线读取），不依赖网络与 guest 首帧。
     * 首帧到达后 applySessionEnvelope/replaceGuestRoot 会替换为真实画面。
     */
    private void showSplashPreview(AppEntry app) {
        if (guestSlot == null) return;
        guestSlot.removeAllViews();
        FrameLayout splash = new FrameLayout(this);
        // starting window 内容由 VM 按 activity 主题解析（AOSP 口径），宿主在
        // 轮询到 nativeGetStartingWindow 后就位；先显示纯背景占位。
        splash.setBackgroundColor(0xFFFFFFFF);
        addScaledSurface(splash);
        startingWindowApplied = false;
        if (guestCardView != null) guestCardView.setVisibility(View.VISIBLE);
        if (logCardView != null) logCardView.setVisibility(View.GONE);
        if (logToggleView != null) {
            logToggleView.setVisibility(View.VISIBLE);
            logToggleView.setText("日志");
        }
    }

    /**
     * VM 解析好的 starting window（u32 bgColor / u32 gravity / u32 len / bitmap
     * bytes，LE）。宿主只做显示：背景色 + 居中位图，等价系统启动预览层。
     */
    private void applyStartingWindowIfAvailable() {
        if (startingWindowApplied || guestSlot == null) return;
        byte[] sw;
        try {
            sw = NativeBridge.nativeGetStartingWindow();
        } catch (Throwable ignored) {
            return;
        }
        if (sw == null || sw.length < 12) return;
        java.nio.ByteBuffer buf = java.nio.ByteBuffer.wrap(sw)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int color = buf.getInt();
        int gravity = buf.getInt();
        int len = buf.getInt();
        if (len < 0 || len > buf.remaining()) return;
        byte[] bitmapBytes = new byte[len];
        buf.get(bitmapBytes);
        FrameLayout splash = new FrameLayout(this);
        splash.setBackgroundColor(color);
        if (len > 0) {
            android.graphics.Bitmap bitmap =
                    android.graphics.BitmapFactory.decodeByteArray(bitmapBytes, 0, len);
            if (bitmap != null) {
                ImageView image = new ImageView(this);
                image.setImageBitmap(bitmap);
                image.setScaleType(ImageView.ScaleType.FIT_CENTER);
                int g = gravity == 0 ? Gravity.CENTER : gravity;
                FrameLayout.LayoutParams lp = new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT, g);
                splash.addView(image, lp);
            }
        }
        addScaledSurface(splash);
        startingWindowApplied = true;
    }

    /** 启动日志全屏 / guest 全屏 切换（首帧就绪后可用）。 */
    private void showLogContent() {
        if (logCardView != null) logCardView.setVisibility(View.VISIBLE);
        if (guestCardView != null) guestCardView.setVisibility(View.GONE);
        if (logToggleView != null) {
            logToggleView.setVisibility(guestAvailable ? View.VISIBLE : View.GONE);
            logToggleView.setText("画面");
        }
    }

    private void showGuestContent() {
        if (!guestAvailable) {
            showLogContent();
            return;
        }
        if (logCardView != null) logCardView.setVisibility(View.GONE);
        if (guestCardView != null) guestCardView.setVisibility(View.VISIBLE);
        if (logToggleView != null) {
            logToggleView.setVisibility(View.VISIBLE);
            logToggleView.setText("日志");
        }
    }

    private void toggleLogGuest() {
        if (logCardView == null) return;
        if (logCardView.getVisibility() == View.VISIBLE) {
            showGuestContent();
        } else {
            showLogContent();
        }
    }

    // =====================================================================
    // 应用导入 / 桌面网格
    // =====================================================================

    private static final class AppEntry {
        File apk;
        String label;
        String packageName;
        Drawable icon;
    }

    private File appsDir() {
        File dir = new File(getFilesDir(), "apps");
        if (!dir.exists()) dir.mkdirs();
        return dir;
    }

    private void refreshApps() {
        if (scanning) return;
        scanning = true;
        new Thread(() -> {
            List<AppEntry> scanned = new ArrayList<>();
            File dir = appsDir();
            File[] files = dir.listFiles((d, name) -> name.endsWith(".apk"));
            if (files != null) {
                java.util.Arrays.sort(files);
                for (File apk : files) {
                    scanned.add(describeApk(apk));
                }
            }
            handler.post(() -> {
                apps.clear();
                apps.addAll(scanned);
                gridAdapter.notifyDataSetChanged();
                appCountView.setText(apps.size() + " 个应用");
                scanning = false;
            });
        }, "dexvm-app-scan").start();
    }

    private AppEntry describeApk(File apk) {
        AppEntry entry = new AppEntry();
        entry.apk = apk;
        entry.label = apk.getName().replaceFirst("\\.apk$", "");
        try {
            android.content.pm.PackageManager pm = getPackageManager();
            android.content.pm.PackageInfo pi = pm.getPackageArchiveInfo(apk.getAbsolutePath(), 0);
            if (pi != null && pi.applicationInfo != null) {
                pi.applicationInfo.sourceDir = apk.getAbsolutePath();
                pi.applicationInfo.publicSourceDir = apk.getAbsolutePath();
                CharSequence label = pm.getApplicationLabel(pi.applicationInfo);
                if (label != null && label.length() > 0) entry.label = label.toString();
                entry.packageName = pi.packageName;
                try {
                    entry.icon = pm.getApplicationIcon(pi.applicationInfo);
                } catch (Throwable ignored) {
                }
            }
        } catch (Throwable ignored) {
        }
        return entry;
    }

    private void startImport() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        try {
            startActivityForResult(intent, REQ_IMPORT);
        } catch (Throwable t) {
            Toast.makeText(this, "无法打开文件选择器: " + t, Toast.LENGTH_LONG).show();
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_IMPORT || resultCode != RESULT_OK || data == null) return;
        Uri uri = data.getData();
        if (uri == null) return;
        new Thread(() -> {
            String result = importFromUri(uri);
            handler.post(() -> {
                Toast.makeText(this, result, Toast.LENGTH_SHORT).show();
                refreshApps();
            });
        }, "dexvm-app-import").start();
    }

    private String importFromUri(Uri uri) {
        String display = "imported.apk";
        try (Cursor c = getContentResolver().query(uri, null, null, null, null)) {
            if (c != null && c.moveToFirst()) {
                int idx = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (idx >= 0) {
                    String name = c.getString(idx);
                    if (name != null && name.length() > 0) display = name;
                }
            }
        } catch (Throwable ignored) {
        }
        String base = display.replaceAll("[^A-Za-z0-9._-]", "_");
        if (!base.toLowerCase().endsWith(".apk")) base += ".apk";
        File out = new File(appsDir(), base);
        int suffix = 1;
        while (out.exists()) {
            out = new File(appsDir(), base.replaceFirst("\\.apk$", "") + "-" + suffix + ".apk");
            suffix++;
        }
        try (InputStream in = getContentResolver().openInputStream(uri);
             OutputStream os = new FileOutputStream(out)) {
            if (in == null) return "导入失败: 无法读取文件";
            byte[] buf = new byte[64 * 1024];
            int n;
            while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
            return "已导入 " + out.getName();
        } catch (Throwable t) {
            out.delete();
            return "导入失败: " + t;
        }
    }

    private void confirmRemove(AppEntry app) {
        new AlertDialog.Builder(this)
                .setTitle("移除应用")
                .setMessage(app.label + "\n从虚拟机中移除该 APK？")
                .setNegativeButton("取消", null)
                .setPositiveButton("移除", (d, w) -> {
                    app.apk.delete();
                    refreshApps();
                })
                .show();
    }

    // =====================================================================
    // 打开 guest 应用（自研 VM 启动；实时日志）
    // =====================================================================

    private void openGuestApp(final AppEntry app) {
        if (running) {
            Toast.makeText(this, "已有应用运行中", Toast.LENGTH_SHORT).show();
            return;
        }
        final String apkPath = app.apk.getAbsolutePath();
        activeApkPath = apkPath;
        final String label = app.label;
        final String pkg = app.packageName == null ? "" : app.packageName;
        bridgedView = null;
        guestSlot.removeAllViews();
        // 所有导入的 app 都走「生命周期 + 首帧 + 常驻会话」：知乎等真实 app 的
        // 启动链与测试 app 同一条语义链（Application→providers→Activity→首帧），
        // 首帧经 UI 桥接出画，会话内消息泵/点击回灌持续生效。
        // 调试：intent 传 trace_prefix 时开启 guest 方法入口 trace（默认关闭）。
        try {
            String trace = getIntent() != null
                    ? getIntent().getStringExtra("trace_prefix") : null;
            NativeBridge.nativeSetGuestTrace(trace == null ? "" : trace);
        } catch (Throwable ignored) {
        }
        sessionMode = true;
        sessionExitRequested = false;
        sessionMaterializer = null;
        sessionRootView = null;
        sessionFramesApplied = 0;
        running = true;
        showRunning(label, "启动中…");
        // 启动期先给默认开屏（本地图标，无网络依赖）；首帧到达后替换。
        showSplashPreview(app);
        NativeBridge.nativeClearRunProgress();
        runLogView.setText("");
        startProgressPoll();
        if (sessionMode) startSessionPoll();
        // 宿主传感器桥：SensorManager 注册要求 Looper 线程（宿主主线程，幂等）。
        // guest lib android.hardware.SensorManager 轮询消费真数据。
        try {
            SensorBridge.start(this);
        } catch (Throwable ignored) {
        }
        // 宿主通知桥：guest 通知请求 -> 宿主真 NotificationManager 下发。
        try {
            NotificationBridge.start(this);
        } catch (Throwable ignored) {
        }
        new Thread(() -> {
            String report;
            try {
                TlsBridge.start();  // 幂等（桥接线程异常退出时兜底重启）
                pushHostServiceSnapshot();
                String runtimeDex = copyAsset("runtime.dex");
                report = NativeBridge.nativeRunApkSession(runtimeDex, apkPath);
            } catch (Throwable t) {
                report = "error: " + t;
            }
            final String result = report;
            logToLogcat(TAG_OPEN, result, "RUN DONE");
            handler.post(() -> finishRun(label, result));
        }, "dexvm-open").start();
    }

    private void finishRun(String label, String result) {
        running = false;
        stopProgressPoll();
        stopSessionPoll();
        updateProgressLog();
        boolean wasSession = sessionMode;
        runStatusView.setText(result.startsWith("OK") ? (wasSession ? "已退出" : "已出画") : "已结束");
        if (!wasSession || !guestAvailable) {
            // 非会话，或会话未出帧（失败/未拉到）：一次性首帧快照回落
            attachBridgeIfAvailable();
        }
        // 报告追加到日志尾部（完整内容同时已写 logcat）
        runLogView.append("\n== 结果 ==\n" + result + "\n");
        if (wasSession && sessionExitRequested) {
            sessionMode = false;
            showDesktop();
            return;
        }
        if (guestAvailable) {
            showGuestContent();
        } else {
            showLogContent();
        }
        final Runnable scroll = () -> runLogScroll.fullScroll(View.FOCUS_DOWN);
        handler.post(scroll);
    }

    // ---- 常驻会话帧拉取（full 基线 + patch 增量；点击由 materializer 回灌）----

    private void startSessionPoll() {
        stopSessionPoll();
        sessionPoll = new Runnable() {
            @Override
            public void run() {
                if (!running) return;
                try {
                    byte[] frames = NativeBridge.nativeSessionPollFrames();
                    if (frames != null && frames.length >= 4) applySessionFrames(frames);
                } catch (Throwable ignored) {
                }
                if (running) handler.postDelayed(this, 200);
            }
        };
        handler.post(sessionPoll);
    }

    private void stopSessionPoll() {
        if (sessionPoll != null) {
            handler.removeCallbacks(sessionPoll);
            sessionPoll = null;
        }
    }

    private void applySessionFrames(byte[] blob) {
        java.nio.ByteBuffer buf = java.nio.ByteBuffer.wrap(blob)
                .order(java.nio.ByteOrder.LITTLE_ENDIAN);
        int count = buf.getInt();
        for (int i = 0; i < count; i++) {
            if (buf.remaining() < 4) break;
            int len = buf.getInt();
            if (len <= 0 || len > buf.remaining()) break;
            byte[] env = new byte[len];
            buf.get(env);
            try {
                applySessionEnvelope(env);
            } catch (Throwable ignored) {
            }
        }
    }

    private void applySessionEnvelope(byte[] env) {
        if (env.length < 20) return;
        int kind = env[6] & 0xFF;  // UIBR magic(4) + version(2) + kind(1)
        if (sessionMaterializer == null || kind == 1) {
            UiBridgeMaterializer mat = new UiBridgeMaterializer(this);
            mat.setApkPath(activeApkPath);
            UiBridgeMaterializer.Result r = mat.apply(env);
            if (r.root != null) {
                sessionMaterializer = mat;
                sessionRootView = r.root;
                replaceGuestRoot(r.root);
            }
        } else {
            UiBridgeMaterializer.Result r = sessionMaterializer.apply(env);
            if (r.root != null && r.root != sessionRootView) {
                sessionRootView = r.root;
                replaceGuestRoot(r.root);
            }
        }
        sessionFramesApplied++;
        if (!guestAvailable && sessionRootView != null) {
            guestAvailable = true;
            runStatusView.setText("运行中");
            showGuestContent();
        }
    }

    private void replaceGuestRoot(View root) {
        attachGuestRootScaled(root);
    }

    /**
     * guest 虚拟窗口 540x960 是逻辑坐标系；宿主内容区是真实像素。
     * 用一个铺满的容器托管 guest 根，并在每次尺寸变化时把 540x960
     * 的画面按 x/y 独立缩放铺满宿主（虚拟机显示面口径；命中测试由
     * ViewGroup 的 child matrix 自动换算）。
     */
    private void attachGuestRootScaled(View root) {
        addScaledSurface(root);
        bridgedView = root;
    }

    /** 把 540x960 逻辑画面（splash / starting window / guest 根）缩放铺满内容区。 */
    private void addScaledSurface(View content) {
        if (guestSlot == null) return;
        guestSlot.removeAllViews();
        FrameLayout box = new FrameLayout(this);
        guestSlot.addView(box, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT));
        box.addView(content, new FrameLayout.LayoutParams(540, 960));
        box.addOnLayoutChangeListener((v, l, t, r, b, ol, ot, or, ob) -> {
            int w = v.getWidth();
            int h = v.getHeight();
            if (w <= 0 || h <= 0) return;
            content.setPivotX(0f);
            content.setPivotY(0f);
            content.setScaleX(w / 540f);
            content.setScaleY(h / 960f);
        });
    }

    private void attachBridgeIfAvailable() {
        guestAvailable = false;
        try {
            byte[] envelope = NativeBridge.nativeGetViewSnapshot();
            if (envelope == null) return;
            UiBridgeMaterializer materializer = new UiBridgeMaterializer(this);
            materializer.setApkPath(activeApkPath);
            UiBridgeMaterializer.Result r = materializer.apply(envelope);
            bridgedView = r.root;
            if (bridgedView != null) {
                attachGuestRootScaled(bridgedView);
                guestAvailable = true;
            }
        } catch (Throwable ignored) {
        }
    }

    // ---- 进度轮询（VM/考卷线程写，互斥快照；UI 400ms 刷新）----

    private void startProgressPoll() {
        stopProgressPoll();
        progressPoll = new Runnable() {
            @Override
            public void run() {
                if (sessionMode && !guestAvailable && !startingWindowApplied) {
                    applyStartingWindowIfAvailable();
                }
                updateProgressLog();
                if (running) handler.postDelayed(this, 400);
            }
        };
        handler.post(progressPoll);
    }

    private void stopProgressPoll() {
        if (progressPoll != null) {
            handler.removeCallbacks(progressPoll);
            progressPoll = null;
        }
    }

    private void updateProgressLog() {
        try {
            String progress = NativeBridge.nativeGetRunProgress();
            if (progress == null) progress = "";
            if (!progress.equals(runLogView.getText().toString())) {
                boolean atBottom = runLogScroll.getChildCount() > 0
                        && runLogScroll.getScrollY() + runLogScroll.getHeight()
                        >= runLogView.getHeight() - 24;
                runLogView.setText(progress);
                if (atBottom) {
                    runLogScroll.post(() -> runLogScroll.fullScroll(View.FOCUS_DOWN));
                }
            }
        } catch (Throwable ignored) {
        }
    }

    // =====================================================================
    // 设备考卷入口（intent 契约保持）
    // =====================================================================

    private void handleIntent(Intent intent) {
        if (intent == null) return;
        if (intent.getBooleanExtra("run_tls_selftest", false)) {
            String host = intent.getStringExtra("tls_host");
            runTlsSelfTest(host == null || host.isEmpty() ? "example.com" : host);
            return;
        }
        boolean targetAuto = intent.getBooleanExtra("run_target_exam", false);
        if (targetAuto) {
            String sel = intent.getStringExtra("exam_round");
            runTargetExam(sel == null || sel.isEmpty() ? "all" : sel);
            return;
        }
        boolean zhihuAuto = intent.getBooleanExtra("run_exam", false);
        if (zhihuAuto) runExam();
    }

    /** 知乎 M3 考卷：优先已导入的 zhihu apk；兼容旧 files/zhihu.apk。 */
    private File findZhihuApk() {
        File dir = appsDir();
        File[] files = dir.listFiles((d, name) ->
                name.toLowerCase().contains("zhihu") && name.endsWith(".apk"));
        if (files != null && files.length > 0) return files[0];
        File legacy = new File(getFilesDir(), "zhihu.apk");
        return legacy.exists() ? legacy : null;
    }

    private void runExam() {
        File apk = findZhihuApk();
        if (apk == null) {
            Toast.makeText(this, "未导入知乎 APK（桌面 + 导入）", Toast.LENGTH_LONG).show();
            return;
        }
        if (running) return;
        bridgedView = null;
        guestSlot.removeAllViews();
        running = true;
        showRunning("知乎考试", "运行中…");
        NativeBridge.nativeClearRunProgress();
        runLogView.setText("");
        startProgressPoll();
        Log.i(TAG, "exam start");
        new Thread(() -> {
            String r;
            try {
                pushHostServiceSnapshot();
                String runtimeDex = copyAsset("runtime.dex");
                r = NativeBridge.nativeRunExam(runtimeDex, apk.getAbsolutePath());
            } catch (Throwable t) {
                r = "error: " + t;
            }
            final String result = r;
            handler.post(() -> {
                finishRun("知乎考试", result);
                logToLogcat(TAG, result, "EXAM DONE");
            });
        }, "dexvm-exam").start();
    }

    /** 设备 TLS 桥接自检（guest SSLContext/SSLSocket -> 宿主 TlsBridge）。 */
    private void runTlsSelfTest(final String host) {
        if (running) return;
        running = true;
        showRunning("TLS 自检", "连接 " + host + "…");
        NativeBridge.nativeClearRunProgress();
        runLogView.setText("");
        startProgressPoll();
        new Thread(() -> {
            String result;
            try {
                TlsBridge.start();
                String runtimeDex = copyAsset("runtime.dex");
                result = NativeBridge.nativeRunTlsSelfTest(runtimeDex, host);
            } catch (Throwable t) {
                result = "FAIL " + t;
            }
            final String report = result;
            handler.post(() -> finishRun("TLS 自检", report));
            logToLogcat("DexVmTlsSelfTest", report, "TLS SELFTEST DONE");
        }, "dexvm-tls-selftest").start();
    }

    /** target-app 8 轮考卷（设备脚本入口；完成时首帧 guest 树已上屏）。 */
    private void runTargetExam(final String roundSel) {
        if (running) return;
        bridgedView = null;
        guestSlot.removeAllViews();
        running = true;
        showRunning("target-app 考卷", "运行中 (" + roundSel + ")");
        NativeBridge.nativeClearRunProgress();
        runLogView.setText("");
        startProgressPoll();
        Log.i(TAG_TARGET, "exam start rounds=" + roundSel);
        new Thread(() -> {
            String r;
            try {
                pushHostServiceSnapshot();
                String runtimeDex = copyAsset("runtime.dex");
                String apk = copyAsset("target-app-debug.apk");
                r = NativeBridge.nativeRunTargetExam(runtimeDex, apk, roundSel);
            } catch (Throwable t) {
                r = "error: " + t;
            }
            final String result = r;
            handler.post(() -> {
                finishRun("target-app 考卷", result);
                // 完成标记在 guest 树/报告上屏之后写（脚本据此做 uiautomator 断言）
                logToLogcat(TAG_TARGET, result, "TARGET EXAM DONE");
            });
        }, "dexvm-target-exam").start();
    }

    /** logcat 单条 ~4KB 上限：分片写（doneMark 供 tools 判定跑卷结束）。 */
    private void logToLogcat(String tag, String text, String doneMark) {
        final int chunk = 3000;
        for (int i = 0; i < text.length(); i += chunk) {
            Log.i(tag, text.substring(i, Math.min(text.length(), i + chunk)));
        }
        Log.i(tag, doneMark);
    }

    /**
     * 宿主桥接值面：把真实系统服务快照推入 VM（telephony.* 等）。guest lib 类经
     * com.dexvm.runtime.Host.get(key) 读取——「framework 面桥接优先」口径。
     */
    private void pushHostServiceSnapshot() {
        StringBuilder sb = new StringBuilder();
        try {
            android.telephony.TelephonyManager tm = (android.telephony.TelephonyManager)
                    getSystemService(android.content.Context.TELEPHONY_SERVICE);
            if (tm != null) {
                String[][] strings = {
                        {"telephony.networkOperator", tm.getNetworkOperator()},
                        {"telephony.networkOperatorName", tm.getNetworkOperatorName()},
                        {"telephony.simOperator", tm.getSimOperator()},
                        {"telephony.simOperatorName", tm.getSimOperatorName()},
                        {"telephony.simCountryIso", tm.getSimCountryIso()},
                        {"telephony.networkCountryIso", tm.getNetworkCountryIso()},
                        {"telephony.deviceSoftwareVersion", tm.getDeviceSoftwareVersion()},
                };
                for (String[] kv : strings) {
                    if (kv[1] != null) sb.append(kv[0]).append('=').append(kv[1]).append('\n');
                }
                sb.append("telephony.phoneType=").append(tm.getPhoneType()).append('\n');
                sb.append("telephony.simState=").append(tm.getSimState()).append('\n');
                sb.append("telephony.networkType=").append(tm.getNetworkType()).append('\n');
                sb.append("telephony.dataState=").append(tm.getDataState()).append('\n');
                sb.append("telephony.networkRoaming=")
                        .append(tm.isNetworkRoaming() ? 1 : 0).append('\n');
                sb.append("telephony.phoneCount=").append(tm.getPhoneCount()).append('\n');
            }
        } catch (Throwable ignored) {
        }
        // 蓝牙适配器真值快照（guest lib BluetoothAdapter 读取 bluetooth.* 键）
        try {
            android.bluetooth.BluetoothManager bm = (android.bluetooth.BluetoothManager)
                    getSystemService(android.content.Context.BLUETOOTH_SERVICE);
            android.bluetooth.BluetoothAdapter bt = bm != null ? bm.getAdapter() : null;
            if (bt == null) {
                sb.append("bluetooth.available=0\n");
            } else {
                sb.append("bluetooth.available=1\n");
                try {
                    sb.append("bluetooth.enabled=").append(bt.isEnabled() ? 1 : 0).append('\n');
                } catch (Throwable t) {
                    sb.append("bluetooth.enabled=0\n");
                }
                try {
                    String name = bt.getName();
                    if (name != null) {
                        sb.append("bluetooth.name=").append(sanitizeValue(name)).append('\n');
                    }
                } catch (Throwable ignored) {
                }
                try {
                    String addr = bt.getAddress();
                    if (addr != null) {
                        sb.append("bluetooth.address=").append(sanitizeValue(addr)).append('\n');
                    }
                } catch (Throwable ignored) {
                }
                try {
                    java.util.Set<android.bluetooth.BluetoothDevice> bonded =
                            bt.getBondedDevices();
                    StringBuilder devs = new StringBuilder();
                    if (bonded != null) {
                        for (android.bluetooth.BluetoothDevice d : bonded) {
                            String name = d.getName() == null ? "" : sanitizeValue(d.getName());
                            String addr = d.getAddress() == null ? "" : d.getAddress();
                            int major = 0;
                            try {
                                android.bluetooth.BluetoothClass cls = d.getBluetoothClass();
                                if (cls != null) major = cls.getMajorDeviceClass();
                            } catch (Throwable ignored) {
                            }
                            if (devs.length() > 0) devs.append(';');
                            devs.append(name).append('|').append(addr).append('|')
                                    .append(major);
                        }
                    }
                    sb.append("bluetooth.devices=").append(devs).append('\n');
                } catch (Throwable ignored) {
                }
            }
        } catch (Throwable ignored) {
        }
        // UI 模式真值快照（guest lib UiModeManager 读取 uimode.* 键）
        try {
            android.app.UiModeManager um = (android.app.UiModeManager)
                    getSystemService(android.content.Context.UI_MODE_SERVICE);
            if (um != null) {
                sb.append("uimode.currentModeType=").append(um.getCurrentModeType()).append('\n');
                sb.append("uimode.nightMode=").append(um.getNightMode()).append('\n');
            }
        } catch (Throwable ignored) {
        }
        try {
            NativeBridge.nativeSetHostValues(sb.toString());
        } catch (Throwable ignored) {
        }
    }

    /** 宿主值快照的字段清洗："|"、";"、换行会破坏 k=v/设备编码。 */
    private static String sanitizeValue(String s) {
        if (s == null) return "";
        return s.replace('|', ' ').replace(';', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    private String copyAsset(String name) throws Exception {
        File out = new File(getFilesDir(), name);
        File stamp = new File(getFilesDir(), name + ".stamp");
        // 版本戳 = 宿主包 lastUpdateTime：adb install -r 保留 files/，但更新包会
        // 改变该值 -> 重新落位；未更新则直接复用，避免每次打开重拷 4.8MB。
        long pkgStamp = -1;
        try {
            pkgStamp = getPackageManager().getPackageInfo(getPackageName(), 0).lastUpdateTime;
        } catch (Throwable ignored) {
        }
        if (pkgStamp > 0 && out.isFile() && out.length() > 0 && stamp.isFile()) {
            try (InputStream in = new FileInputStream(stamp)) {
                byte[] raw = new byte[64];
                int n = in.read(raw);
                if (n > 0 && Long.parseLong(new String(raw, 0, n).trim()) == pkgStamp) {
                    return out.getAbsolutePath();
                }
            } catch (Throwable ignored) {
            }
        }
        try (InputStream in = getAssets().open(name);
             OutputStream os = new FileOutputStream(out)) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                os.write(buf, 0, n);
            }
        }
        if (pkgStamp > 0) {
            try (OutputStream so = new FileOutputStream(stamp)) {
                so.write(Long.toString(pkgStamp).getBytes("US-ASCII"));
            } catch (Throwable ignored) {
            }
        }
        return out.getAbsolutePath();
    }

    // =====================================================================
    // 网格适配器（应用瓦片 + "+"导入瓦片）
    // =====================================================================

    private final class GridAdapter extends BaseAdapter {
        @Override
        public int getCount() {
            return apps.size() + 1;
        }

        @Override
        public Object getItem(int position) {
            return position < apps.size() ? apps.get(position) : null;
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            LinearLayout tile = new LinearLayout(MainActivity.this);
            tile.setOrientation(LinearLayout.VERTICAL);
            tile.setGravity(Gravity.CENTER_HORIZONTAL);

            FrameLayout iconBox = new FrameLayout(MainActivity.this);
            iconBox.setBackground(card(COLOR_CARD_SOFT, 18));
            tile.addView(iconBox, new LinearLayout.LayoutParams(dp(64), dp(64)));

            if (position < apps.size()) {
                AppEntry app = apps.get(position);
                if (app.icon != null) {
                    ImageView icon = new ImageView(MainActivity.this);
                    icon.setImageDrawable(app.icon);
                    int pad = dp(12);
                    icon.setPadding(pad, pad, pad, pad);
                    iconBox.addView(icon, new FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.MATCH_PARENT));
                } else {
                    TextView glyph = new TextView(MainActivity.this);
                    String label = app.label == null || app.label.isEmpty() ? "?" : app.label;
                    glyph.setText(label.substring(0, 1).toUpperCase());
                    glyph.setTextColor(COLOR_ACCENT);
                    glyph.setTextSize(TypedValue.COMPLEX_UNIT_SP, 22);
                    glyph.setTypeface(Typeface.DEFAULT_BOLD);
                    glyph.setGravity(Gravity.CENTER);
                    iconBox.addView(glyph, new FrameLayout.LayoutParams(
                            FrameLayout.LayoutParams.MATCH_PARENT,
                            FrameLayout.LayoutParams.MATCH_PARENT));
                }
                TextView name = new TextView(MainActivity.this);
                name.setText(app.label);
                name.setTextColor(COLOR_TEXT);
                name.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
                name.setGravity(Gravity.CENTER);
                name.setMaxLines(2);
                name.setPadding(0, dp(5), 0, 0);
                tile.addView(name);
            } else {
                TextView plus = new TextView(MainActivity.this);
                plus.setText("+");
                plus.setTextColor(COLOR_TEXT_SECONDARY);
                plus.setTextSize(TypedValue.COMPLEX_UNIT_SP, 30);
                plus.setGravity(Gravity.CENTER);
                iconBox.addView(plus, new FrameLayout.LayoutParams(
                        FrameLayout.LayoutParams.MATCH_PARENT,
                        FrameLayout.LayoutParams.MATCH_PARENT));
                TextView label = new TextView(MainActivity.this);
                label.setText("导入 APK");
                label.setTextColor(COLOR_TEXT_SECONDARY);
                label.setTextSize(TypedValue.COMPLEX_UNIT_SP, 10);
                label.setGravity(Gravity.CENTER);
                label.setPadding(0, dp(5), 0, 0);
                tile.addView(label);
            }
            return tile;
        }
    }

    // ---- 小工具 ----

    private int dp(float value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private GradientDrawable card(int color, float radiusDp) {
        GradientDrawable d = new GradientDrawable();
        d.setColor(color);
        d.setCornerRadius(dp(radiusDp));
        return d;
    }
}
