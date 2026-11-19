package com.dexvm.pitest;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.content.Context;
import android.content.pm.ApplicationInfo;
import android.content.pm.ProviderInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;

/**
 * attachInfo 链 fixture provider：attachInfo 覆写读 ProviderInfo 全字段面
 * （AOSP ProviderInfo/PackageItemInfo 公开面），逐字段记录进静态 observations；
 * 字段断言由 provider_info_smoke.cc 侧做（单一事实源）。读字段即断言装配非空
 * 指针链（provider[1..35] 共同墙回归锚点）。
 */
public class InfoProbeProvider extends ContentProvider {
    public static String observations = "";

    @Override
    public void attachInfo(Context context, ProviderInfo info) {
        StringBuilder sb = new StringBuilder();
        sb.append(info.name);
        sb.append('|').append(info.authority);
        sb.append('|').append(info.packageName);
        sb.append('|').append(info.processName);
        sb.append('|').append(info.exported);
        sb.append('|').append(info.enabled);
        sb.append('|').append(info.initOrder);
        sb.append('|').append(info.readPermission);
        sb.append('|').append(info.writePermission);
        sb.append('|').append(info.multiprocess);
        Bundle meta = info.metaData;
        if (meta == null) {
            sb.append("|null");
        } else {
            sb.append('|').append(meta.getString("p.key"));
            sb.append(',').append(meta.getInt("p.n"));
            sb.append(',').append(meta.getBoolean("p.b"));
        }
        ApplicationInfo ai = info.applicationInfo;
        sb.append('|').append(ai != null ? ai.packageName : "null");
        observations = observations + sb.toString() + "\n";
        super.attachInfo(context, info);
    }

    @Override
    public boolean onCreate() {
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
