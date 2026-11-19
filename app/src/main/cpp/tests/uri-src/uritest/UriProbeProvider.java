package uritest;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.UriMatcher;
import android.database.Cursor;
import android.net.Uri;

/**
 * Uri/UriMatcher/ContentResolver 路由 fixture provider（authority
 * com.dexvm.uri.alpha）。onCreate 内走 lib UriMatcher 登记（lib 消费链）；
 * CRUD 全形记录进静态 log（含 UriMatcher.match 结果与 ContentValues 读回），
 * 断言在 uri_resolver_smoke.cc 侧（单一事实源）。
 */
public class UriProbeProvider extends ContentProvider {
    public static String log = "";
    public static UriMatcher matcher;

    @Override
    public boolean onCreate() {
        matcher = new UriMatcher(UriMatcher.NO_MATCH);
        matcher.addURI("com.dexvm.uri.alpha", "items", 1);
        matcher.addURI("com.dexvm.uri.alpha", "items/#", 2);
        matcher.addURI("com.dexvm.uri.alpha", "special", 3);
        matcher.addURI("com.dexvm.uri.alpha", "*", 4);
        log = log + "alpha.onCreate\n";
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        int match = matcher.match(uri);
        log = log + "alpha.query:" + uri + ":m=" + match + "\n";
        return new TestCursor(new String[] {"_id", "name"},
                new Object[][] {{(long) match, "row-" + match}});
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        String name = values != null ? values.getAsString("name") : "null";
        log = log + "alpha.insert:" + uri + ":name=" + name + "\n";
        return Uri.withAppendedPath(uri, "42");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        String name = values != null ? values.getAsString("name") : "null";
        log = log + "alpha.update:" + uri + ":name=" + name + "\n";
        return 1;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        log = log + "alpha.delete:" + uri + "\n";
        return 2;
    }

    @Override
    public String getType(Uri uri) {
        log = log + "alpha.getType:" + uri + "\n";
        return "vnd.dexvm/alpha";
    }
}
