package com.dexvm.rtest;

import android.content.ContentProvider;
import android.content.Context;
import android.content.pm.ProviderInfo;
import android.net.Uri;

/** grantUriPermissions 属性缺省投影探针（false 面；AOSP 缺省语义）。 */
public class PlainProbeProvider extends ContentProvider {
    public static String observations = "";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public void attachInfo(Context context, ProviderInfo info) {
        super.attachInfo(context, info);
        observations = "grant=" + info.grantUriPermissions
                + "|exported=" + info.exported
                + "|auth=" + info.authority + ";\n";
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, android.content.ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] args) {
        return 0;
    }

    @Override
    public int update(Uri uri, android.content.ContentValues values, String selection,
                      String[] args) {
        return 0;
    }

    @Override
    public android.database.Cursor query(Uri uri, String[] projection, String selection,
                                         String[] args, String sortOrder) {
        return null;
    }
}
