package com.dexvm.host;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.MenuItem;
import android.view.View;
import android.widget.PopupMenu;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;

/**
 * 宿主弹层桥（PopupMenu）：guest lib {@code android.widget.PopupMenu.show()} 经
 * VM 队列（core/popup_bridge.cc）→ {@link NativeBridge#nativePopupTake()} 取出，
 * 在宿主主线程用真 {@link PopupMenu} 出画（原生淡入/阴影/外点关闭）。
 *
 * <p>线格式（小端，见 core/popup_bridge.h）：
 * i32 kind（0 show / 1 dismiss）| i32 anchorViewId | u32 token | str titles
 * （str = i32 len + UTF-8；titles 以 '\n' 连接）。
 *
 * <p>菜单项点击 -> nativeDispatchPopupEvent(token, 0, index) 回灌 guest
 * OnMenuItemClickListener；菜单关闭（点击后自动关闭/外点）-> (token, 1, 0)
 * 回灌 guest 展示态与 OnDismissListener。锚点解析经 {@link Resolver}
 * （guest mId -> 宿主物化 View，主线程调用）。
 */
public final class PopupMenuBridge {
    private static final String TAG = "DexVmPopup";

    private static Thread sThread;

    private PopupMenuBridge() {
    }

    /** guest mId -> 宿主物化 View（UiBridgeMaterializer.findViewByGuestId）。 */
    public interface Resolver {
        View findViewByGuestId(int guestViewId);
    }

    /** 幂等启动（宿主主线程调用）。 */
    public static synchronized void start(Context context, Resolver resolver) {
        if (sThread != null || context == null || resolver == null) return;
        Context app = context.getApplicationContext();
        final Context ctx = app != null ? app : context;
        sThread = new Thread(() -> run(ctx, resolver), "dexvm-popup");
        sThread.setDaemon(true);
        sThread.start();
    }

    private static void run(Context ctx, Resolver resolver) {
        final Handler main = new Handler(Looper.getMainLooper());
        final Map<Integer, PopupMenu> menus = new HashMap<>();
        while (true) {
            byte[] req;
            try {
                req = NativeBridge.nativePopupTake();
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
                int kind = b.getInt();
                int anchorViewId = b.getInt();
                int token = b.getInt();
                String titles = readStr(b);
                if (kind == 1) {
                    main.post(() -> {
                        PopupMenu m = menus.remove(token);
                        if (m != null) m.dismiss();
                    });
                    continue;
                }
                final String[] items = titles.isEmpty() ? new String[0] : titles.split("\n", -1);
                main.post(() -> showMenu(ctx, resolver, menus, token, anchorViewId, items));
            } catch (Throwable t) {
                Log.w(TAG, "dispatch failed", t);
            }
        }
    }

    private static void showMenu(Context ctx, Resolver resolver, Map<Integer, PopupMenu> menus,
                                 int token, int anchorViewId, String[] items) {
        View anchor = resolver.findViewByGuestId(anchorViewId);
        if (anchor == null) {
            Log.w(TAG, "show token=" + token + " anchor guestId=" + anchorViewId + " not found");
            return;
        }
        PopupMenu previous = menus.remove(token);
        if (previous != null) previous.dismiss();
        final PopupMenu menu = new PopupMenu(ctx, anchor);
        for (int i = 0; i < items.length; i++) {
            menu.getMenu().add(0, i, i, items[i]);
        }
        final boolean[] clicked = new boolean[1];
        menu.setOnMenuItemClickListener((MenuItem item) -> {
            clicked[0] = true;
            int index = item.getItemId();
            String status;
            try {
                status = NativeBridge.nativeDispatchPopupEvent(token, 0, index);
            } catch (Throwable t) {
                status = "error: " + t;
            }
            Log.i(TAG, "menu click token=" + token + " index=" + index + " -> " + status);
            return true;
        });
        menu.setOnDismissListener((PopupMenu dismissed) -> {
            menus.remove(token);
            if (clicked[0]) {
                // 点击导致关闭：guest lib 在 onHostMenuItemClick 内部 dismiss 并
                // 派发 OnDismissListener，再回送 kind=1 只会覆盖单槽里的点击事件。
                return;
            }
            String status;
            try {
                status = NativeBridge.nativeDispatchPopupEvent(token, 1, 0);
            } catch (Throwable t) {
                status = "error: " + t;
            }
            Log.i(TAG, "menu dismissed token=" + token + " -> " + status);
        });
        menus.put(token, menu);
        try {
            menu.show();
        } catch (Throwable t) {
            Log.w(TAG, "menu.show failed token=" + token, t);
            menus.remove(token);
            return;
        }
        Log.i(TAG, "show menu token=" + token + " anchor=" + anchor.getClass().getSimpleName()
                + " items=" + items.length);
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
