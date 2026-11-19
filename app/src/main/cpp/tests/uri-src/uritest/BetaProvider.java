package uritest;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;

/**
 * 第二路由面 fixture（authorities 分号多值：com.dexvm.uri.beta;
 * com.dexvm.uri.gamma——注册表逐键分发断言）。CRUD 记录进静态 log。
 */
public class BetaProvider extends ContentProvider {
    public static String log = "";

    @Override
    public boolean onCreate() {
        log = log + "beta.onCreate\n";
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        log = log + "beta.query:" + uri + "\n";
        return new TestCursor(new String[] {"_id"}, new Object[][] {{7L}});
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        log = log + "beta.insert:" + uri + "\n";
        return uri;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        log = log + "beta.update:" + uri + "\n";
        return 3;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        log = log + "beta.delete:" + uri + "\n";
        return 4;
    }

    @Override
    public String getType(Uri uri) {
        log = log + "beta.getType:" + uri + "\n";
        return "vnd.dexvm/beta";
    }
}
