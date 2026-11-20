package com.dexvm.host;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.widget.Toast;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;

/**
 * 宿主 Toast 桥：guest builtin {@code android.widget.Toast.show()} 经 VM 队列
 * （core/toast_bridge.cc）→ {@link NativeBridge#nativeToastTake()} 取出，在宿主
 * 主线程用真 {@link Toast#makeText} 显示（真系统提示，不伪造）。
 *
 * <p>线格式（小端，见 core/toast_bridge.h）：i32 duration | str text
 * （str = i32 len + UTF-8）。
 */
public final class ToastBridge {
    private static final String TAG = "DexVmToast";

    private static Thread sThread;

    private ToastBridge() {
    }

    /** 幂等启动（宿主主线程调用）。 */
    public static synchronized void start(Context context) {
        if (sThread != null || context == null) return;
        Context app = context.getApplicationContext();
        final Context ctx = app != null ? app : context;
        sThread = new Thread(() -> run(ctx), "dexvm-toast");
        sThread.setDaemon(true);
        sThread.start();
    }

    private static void run(Context ctx) {
        final Handler main = new Handler(Looper.getMainLooper());
        while (true) {
            byte[] req;
            try {
                req = NativeBridge.nativeToastTake();
            } catch (Throwable t) {
                // VM 线程尚未就绪/已退出：退避后重试，桥线程常驻。
                try {
                    Thread.sleep(500);
                } catch (InterruptedException e) {
                    return;
                }
                continue;
            }
            if (req == null) continue;
            try {
                ByteBuffer b = ByteBuffer.wrap(req).order(ByteOrder.LITTLE_ENDIAN);
                int duration = b.getInt();
                String text = readStr(b);
                if (text == null || text.isEmpty()) text = "(guest toast)";
                final String shown = text;
                final int dur = duration == Toast.LENGTH_LONG
                        ? Toast.LENGTH_LONG : Toast.LENGTH_SHORT;
                main.post(() -> Toast.makeText(ctx, shown, dur).show());
                Log.i(TAG, "toast [" + shown + "] duration=" + dur);
            } catch (Throwable t) {
                Log.w(TAG, "dispatch failed", t);
            }
        }
    }

    private static String readStr(ByteBuffer b) {
        if (b.remaining() < 4) return "";
        int len = b.getInt();
        if (len < 0 || len > b.remaining()) return "";
        byte[] bytes = new byte[len];
        b.get(bytes);
        return new String(bytes, StandardCharsets.UTF_8);
    }
}
