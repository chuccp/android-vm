package com.coke.target;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.util.Log;

/**
 * 生命周期 fixture Provider（旧 guest 考卷 Provider 继任）：install 顺序在
 * Application.onCreate 之前（docs 19 章），onCreate 追加 "provider.onCreate;"。
 */
public class RuntimeLifecycleProvider extends ContentProvider {
    @Override
    public boolean onCreate() {
        RuntimeLifecycleApp.lifecycleLog += "provider.onCreate;";
        Log.i("DexVmTargetLifecycle", "Provider.onCreate");
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        return 0;
    }
}
