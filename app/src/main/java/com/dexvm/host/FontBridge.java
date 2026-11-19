package com.dexvm.host;

import android.graphics.Paint;
import android.graphics.Typeface;

import java.util.HashMap;
import java.util.Map;

/**
 * 字体桥（guest 字体面 -> 宿主真 Android 字体栈）。
 *
 * <p>guest lib 的 Typeface/FontFamily/Paint 只保留 API 形状；创建与文本度量
 * 经 VM intrinsic 请求到本桥。**不能在 guest fiber 栈上直接调宿主 JNI**：
 * ART 的 JNI 入口会用 Java 线程栈边界判定栈溢出（fiber 栈在另一映射上），
 * 实测直接调用会以 pending StackOverflowError abort。因此采用专用宿主线程：
 *
 * <pre>
 *   guest fiber --(native condvar 请求)--> dexvm-font 线程（本类 worker）
 *               &lt;--(nativeFontComplete)----  Typeface/Paint 真实现
 * </pre>
 *
 * worker 只做 Java→native 调用（ART 安全方向），guest 侧只碰 native 队列。
 */
public final class FontBridge {

    // 与 core/framework_font.cc 的 op 编号一致。
    static final int OP_CREATE_FAMILY = 1;
    static final int OP_CREATE_FILE = 2;
    static final int OP_RESTYLE = 3;
    static final int OP_MEASURE_TEXT = 4;
    static final int OP_FONT_METRICS = 5;

    private static final Map<Long, Typeface> TYPEFACES = new HashMap<>();
    private static final ThreadLocal<Paint> PAINTS = new ThreadLocal<Paint>() {
        @Override
        protected Paint initialValue() {
            Paint p = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.SUBPIXEL_TEXT_FLAG);
            p.setLinearText(false);
            return p;
        }
    };
    private static long sNextHandle = 1;
    private static boolean sStarted;

    private FontBridge() {
    }

    /** 启动宿主字体 worker（幂等；MainActivity.onCreate 调用）。 */
    public static void start() {
        synchronized (FontBridge.class) {
            if (sStarted) return;
            sStarted = true;
        }
        Thread t = new Thread(FontBridge::workerLoop, "dexvm-font");
        t.setDaemon(true);
        t.start();
    }

    private static void workerLoop() {
        android.util.Log.i("DexVmFont", "worker started");
        for (;;) {
            long id;
            try {
                id = NativeBridge.nativeFontNextRequest();
            } catch (Throwable t) {
                return;
            }
            if (id == 0) return;
            int op = NativeBridge.nativeFontOp(id);
            int style = NativeBridge.nativeFontStyle(id);
            float size = NativeBridge.nativeFontSize(id);
            long base = NativeBridge.nativeFontBase(id);
            long handle = 0L;
            float width = -1f;
            int[] metrics = null;
            boolean metricsOk = false;
            try {
                switch (op) {
                    case OP_CREATE_FAMILY:
                        handle = createFromFamily(NativeBridge.nativeFontString(id), style);
                        break;
                    case OP_CREATE_FILE:
                        handle = createFromFile(NativeBridge.nativeFontString(id), style);
                        break;
                    case OP_RESTYLE:
                        handle = restyle(base, style);
                        break;
                    case OP_MEASURE_TEXT:
                        width = measureText(base, NativeBridge.nativeFontText(id), size);
                        break;
                    case OP_FONT_METRICS:
                        metrics = new int[5];
                        metricsOk = fontMetrics(base, size, metrics);
                        break;
                    default:
                        break;
                }
            } catch (Throwable ignored) {
            }
            try {
                NativeBridge.nativeFontComplete(id, handle, width, metrics, metricsOk);
            } catch (Throwable t) {
                return;
            }
        }
    }

    private static synchronized long put(Typeface tf) {
        if (tf == null) return 0;
        long handle = sNextHandle++;
        TYPEFACES.put(handle, tf);
        return handle;
    }

    /** 系统字族（family 为空 = 平台默认）。 */
    public static long createFromFamily(String family, int style) {
        try {
            return put(Typeface.create(family, style));
        } catch (Throwable t) {
            return 0;
        }
    }

    /** 字体文件（VM 从 APK font 资源抽取落盘后的路径）。 */
    public static long createFromFile(String path, int style) {
        try {
            Typeface base = Typeface.createFromFile(path);
            return put(style == 0 ? base : Typeface.create(base, style));
        } catch (Throwable t) {
            return 0;
        }
    }

    /** 在已有宿主 Typeface 上做风格化（AOSP Typeface.create(family, style)）。 */
    public static long restyle(long baseHandle, int style) {
        try {
            Typeface base = TYPEFACES.get(baseHandle);
            if (base == null) return createFromFamily(null, style);
            return put(Typeface.create(base, style));
        } catch (Throwable t) {
            return 0;
        }
    }

    public static synchronized Typeface get(long handle) {
        return TYPEFACES.get(handle);
    }

    /** 文本宽度（宿主 Paint；handle=0 → 默认字体）。返回 -1 表示失败。 */
    public static float measureText(long handle, String text, float sizePx) {
        if (text == null) return 0f;
        try {
            Paint p = PAINTS.get();
            Typeface tf = handle == 0 ? null : TYPEFACES.get(handle);
            p.setTypeface(tf);
            p.setTextSize(sizePx > 0 ? sizePx : 15f);
            return p.measureText(text);
        } catch (Throwable t) {
            return -1f;
        }
    }

    /** FontMetricsInt（top/ascent/descent/bottom/leading 写入 out[0..4]）。 */
    public static boolean fontMetrics(long handle, float sizePx, int[] out) {
        if (out == null || out.length < 5) return false;
        try {
            Paint p = PAINTS.get();
            Typeface tf = handle == 0 ? null : TYPEFACES.get(handle);
            p.setTypeface(tf);
            p.setTextSize(sizePx > 0 ? sizePx : 15f);
            Paint.FontMetricsInt fm = p.getFontMetricsInt();
            out[0] = fm.top;
            out[1] = fm.ascent;
            out[2] = fm.descent;
            out[3] = fm.bottom;
            out[4] = fm.leading;
            return true;
        } catch (Throwable t) {
            return false;
        }
    }
}
