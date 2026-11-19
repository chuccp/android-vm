package contexttest;

import android.content.Context;
import android.content.ContextWrapper;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.lang.reflect.Method;

/**
 * Context 文件/存储面语义用例（core/framework_builtins.cc B 档方法面）。
 * 口径（AOSP ContextImpl）：dataDir/filesDir/cacheDir 路径自洽、目录取用即建、
 * File 面可读写；getDatabasePath/getDatabaseNames/deleteDatabase/getDir/
 * getSharedPreferencesPath/move*From 同族语义。每行输出 "name: OK" 或
 * "name: FAIL ...";harness 按行判定。
 */
public class CtxFaceProbe {

    private static StringBuilder out = new StringBuilder();

    private static void check(String name, boolean ok, String detail) {
        out.append(name).append(": ").append(ok ? "OK" : "FAIL ").append(ok ? "" : detail).append('\n');
    }

    private static String p(File f) {
        return f == null ? "<null>" : f.getPath();
    }

    public static String run() {
        Context ctx = new ContextWrapper(null);

        File d = ctx.getDataDir();
        check("ctxDataDir", d != null && d.isDirectory(), p(d));

        File f = ctx.getFilesDir();
        check("ctxFilesDir",
                f != null && f.isDirectory()
                        && f.getPath().equals(d.getPath() + "/files")
                        && f.getParent().equals(d.getPath()),
                p(f) + " parent=" + (f == null ? "<null>" : f.getParent()));

        File c = ctx.getCacheDir();
        check("ctxCacheDir",
                c != null && c.isDirectory() && c.getPath().equals(d.getPath() + "/cache"), p(c));

        File cc = ctx.getCodeCacheDir();
        File nb = ctx.getNoBackupFilesDir();
        check("ctxCodeCacheNoBackup",
                cc != null && cc.isDirectory() && cc.getPath().equals(d.getPath() + "/code_cache")
                        && nb != null && nb.isDirectory()
                        && nb.getPath().equals(d.getPath() + "/no_backup"),
                p(cc) + " / " + p(nb));

        // File 面读写（filesDir 内写-读-长度自洽；逐步定位）
        File t = null;
        boolean mkOk = false;
        String mkDetail = "";
        try {
            t = new File(f, "probe.txt");
            mkOk = t != null && t.getPath().equals(f.getPath() + "/probe.txt");
            mkDetail = p(t);
        } catch (Throwable e) {
            mkDetail = e.toString();
        }
        check("ctxFileNew", mkOk, mkDetail);

        boolean wOk = false;
        String wDetail = "";
        try {
            FileOutputStream os = new FileOutputStream(t);
            os.write(new byte[] {1, 2, 3});
            os.close();
            wOk = t.length() == 3 && t.isFile();
            wDetail = "len=" + t.length();
        } catch (Throwable e) {
            wDetail = e.toString();
        }
        check("ctxFileWrite", wOk, wDetail);

        boolean rOk = false;
        String rDetail = "";
        try {
            FileInputStream is = new FileInputStream(t);
            int b0 = is.read();
            int b1 = is.read();
            is.close();
            rOk = b0 == 1 && b1 == 2;
            rDetail = "b0=" + b0 + " b1=" + b1;
        } catch (Throwable e) {
            rDetail = e.toString();
        }
        check("ctxFileRead", rOk, rDetail);

        // RandomAccessFile 面（sun-compat path 键 native：prepare/size/readAt/writeAt/setLength）
        boolean rafOk = false;
        String rafDetail = "";
        try {
            File rf = new File(f, "raf.bin");
            java.io.RandomAccessFile rw = new java.io.RandomAccessFile(rf, "rw");
            rw.write(new byte[] {9, 8, 7, 6});
            rw.setLength(2);
            long sz = rw.length();
            rw.close();
            java.io.RandomAccessFile rr = new java.io.RandomAccessFile(rf, "r");
            byte[] head = new byte[2];
            rr.readFully(head);
            int eof = rr.read();
            rr.close();
            rafOk = sz == 2 && head[0] == 9 && head[1] == 8 && eof == -1;
            rafDetail = "sz=" + sz + " eof=" + eof;
        } catch (Throwable e) {
            rafDetail = e.toString();
        }
        check("randomAccessFile", rafOk, rafDetail);

        // java.nio.ByteOrder <clinit>（jdk.internal.misc.Unsafe.isBigEndian 面）
        boolean boOk = false;
        String boDetail = "";
        try {
            java.nio.ByteOrder bo = java.nio.ByteOrder.nativeOrder();
            boOk = bo == java.nio.ByteOrder.BIG_ENDIAN || bo == java.nio.ByteOrder.LITTLE_ENDIAN;
            boDetail = String.valueOf(bo);
        } catch (Throwable e) {
            boDetail = e.toString();
        }
        check("byteOrder", boOk, boDetail);

        // getDir(name, mode)：<root>/<name> 取用即建
        File dir = ctx.getDir("mydir", 0);
        check("ctxGetDir",
                dir != null && dir.isDirectory() && dir.getPath().equals(d.getPath() + "/mydir"),
                p(dir));

        // getDatabasePath：databases 目录取用即建；路径自洽
        File db = ctx.getDatabasePath("t.db");
        check("ctxDatabasePath",
                db != null && db.getPath().equals(d.getPath() + "/databases/t.db")
                        && db.getParentFile().isDirectory(),
                p(db));

        // getDatabaseNames（@hide 族，反射取）：目录项含已建库；deleteDatabase 后消失
        boolean dbListOk = false;
        String dbListDetail = "";
        try {
            FileOutputStream os = new FileOutputStream(db);
            os.write(7);
            os.close();
            Method gn = ctx.getClass().getMethod("getDatabaseNames", new Class<?>[0]);
            String[] names = (String[]) gn.invoke(ctx);
            boolean has = false;
            if (names != null) {
                for (String s : names) {
                    if ("t.db".equals(s)) has = true;
                }
            }
            boolean gone = ctx.deleteDatabase("t.db") && !db.exists();
            dbListOk = has && gone;
            dbListDetail = "has=" + has + " gone=" + gone;
        } catch (Throwable e) {
            dbListDetail = e.toString();
        }
        check("ctxDatabaseNames", dbListOk, dbListDetail);

        // getSharedPreferencesPath（@hide 族，反射取）：<root>/shared_prefs/<name>.xml
        boolean spOk = false;
        String spDetail = "";
        try {
            Method m = ctx.getClass().getMethod("getSharedPreferencesPath", String.class);
            File sp = (File) m.invoke(ctx, "n");
            spOk = sp != null && sp.getPath().equals(d.getPath() + "/shared_prefs/n.xml")
                    && sp.getParentFile().isDirectory();
            spDetail = p(sp);
        } catch (Throwable e) {
            spDetail = e.toString();
        }
        check("ctxSharedPrefsPath", spOk, spDetail);

        // 服务桥：getPackageManager（AOSP 同 PM 全 Context 可得；单例语义）
        boolean pmOk = false;
        String pmDetail = "";
        try {
            Object pm1 = ctx.getPackageManager();
            Object pm2 = new ContextWrapper(null).getPackageManager();
            pmOk = pm1 != null && pm1 == pm2;
            pmDetail = String.valueOf(pm1);
        } catch (Throwable e) {
            pmDetail = e.toString();
        }
        check("ctxPackageManager", pmOk, pmDetail);

        // 服务桥：getApplicationInfo（AOSP 本 app 身份；单例语义）
        boolean aiOk = false;
        String aiDetail = "";
        try {
            Object ai = ctx.getApplicationInfo();
            aiOk = ai != null && ai == new ContextWrapper(null).getApplicationInfo();
            aiDetail = String.valueOf(ai);
        } catch (Throwable e) {
            aiDetail = e.toString();
        }
        check("ctxApplicationInfo", aiOk, aiDetail);

        // 迁移面（VM 单存储面：恒等完成 true）
        check("ctxMoveFrom",
                ctx.moveDatabaseFrom(ctx, "t.db") && ctx.moveSharedPreferencesFrom(ctx, "n"),
                "move not idempotent");

        // glue 面：Thread.getStackTrace（HMS log 链缺口；live 栈快照）
        boolean stOk = false;
        String stDetail = "";
        try {
            StackTraceElement[] st = Thread.currentThread().getStackTrace();
            stOk = st != null && st.length > 0;
            for (int i = 0; st != null && i < st.length; i++) {
                if (st[i] == null || st[i].getMethodName() == null) stOk = false;
            }
            stDetail = "len=" + (st == null ? -1 : st.length);
        } catch (Throwable e) {
            stDetail = e.toString();
        }
        check("threadStackTrace", stOk, stDetail);

        // glue 面：Log.getStackTraceString（HMS log 链；printStackTrace 收集语义）
        boolean gtsOk = false;
        String gtsDetail = "";
        try {
            String gts = android.util.Log.getStackTraceString(new RuntimeException("gts-probe"));
            gtsOk = gts != null && gts.indexOf("gts-probe") >= 0
                    && gts.indexOf("java.lang.RuntimeException") == 0
                    && "".equals(android.util.Log.getStackTraceString(null));
            gtsDetail = gts == null ? "<null>" : gts.substring(0, Math.min(60, gts.length()));
        } catch (Throwable e) {
            gtsDetail = e.toString();
        }
        check("logStackTraceString", gtsOk, gtsDetail);

        return out.toString();
    }
}
