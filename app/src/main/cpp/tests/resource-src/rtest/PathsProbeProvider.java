package com.dexvm.rtest;

import android.content.ContentProvider;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.content.res.Resources;
import android.content.res.XmlResourceParser;
import android.os.Bundle;
import android.os.Environment;

import java.io.File;

/**
 * FileProvider attachInfo 消费链同款探针（androidx FileProvider 字节码对齐）：
 *   1) grantUriPermissions/exported 字段断言（SecurityException 消费面同判）
 *   2) pm.resolveContentProvider -> info.loadXmlMetaData(pm, FILE_PROVIDER_PATHS)
 *      -> PathStrategy 语义解析（files/cache/external/root-path 的 name/path）
 *   3) Resources.getIdentifier + getXml 直面（meta-data 资源 id 对账）
 * 观测写静态 observations（宿主 harness 断言）。
 */
public class PathsProbeProvider extends ContentProvider {
    public static String observations = "";

    static final String META_KEY = "android.support.FILE_PROVIDER_PATHS";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public void attachInfo(Context context, ProviderInfo info) {
        super.attachInfo(context, info);
        StringBuilder sb = new StringBuilder();
        // FileProvider.attachInfo 同款 SecurityException 消费面（grantUri 投影断言）
        if (info.exported) {
            throw new SecurityException("Provider must not be exported");
        }
        if (!info.grantUriPermissions) {
            throw new SecurityException("Provider must grant uri permissions");
        }
        sb.append("grant=").append(info.grantUriPermissions)
          .append("|exported=").append(info.exported).append(";\n");

        // ---- FileProvider 同款 meta-data 消费链 ----
        PackageManager pm = context.getPackageManager();
        String authority = info.authority.split(";")[0];
        ProviderInfo resolved = pm.resolveContentProvider(authority, 128);
        sb.append("resolved=").append(resolved != null ? resolved.authority : "null").append(";\n");
        XmlResourceParser parser =
                resolved.loadXmlMetaData(pm, META_KEY);
        sb.append("metaParse:\n");
        appendPaths(sb, parser, context);

        // ---- getIdentifier + getXml 直面（meta-data 资源 id 对账）----
        Resources res = context.getResources();
        Bundle meta = info.metaData;
        int metaId = meta != null ? meta.getInt(META_KEY) : 0;
        int id = res.getIdentifier("file_paths", "xml", context.getPackageName());
        sb.append("idmatch=").append(id == metaId && id != 0)
          .append("|id=0x").append(Integer.toHexString(id)).append(";\n");
        XmlResourceParser p2 = res.getXml(id);
        sb.append("idParse:\n");
        appendPaths(sb, p2, context);
        observations = sb.toString();
    }

    static void appendPaths(StringBuilder sb, XmlResourceParser parser, Context context) {
        if (parser == null) {
            sb.append("nullParser;\n");
            return;
        }
        try {
            int type;
            while ((type = parser.next()) != XmlResourceParser.END_DOCUMENT) {
                if (type != XmlResourceParser.START_TAG) {
                    continue;
                }
                String tag = parser.getName();
                String name = parser.getAttributeValue(null, "name");
                String path = parser.getAttributeValue(null, "path");
                File base;
                if ("files-path".equals(tag)) {
                    base = context.getFilesDir();
                } else if ("cache-path".equals(tag)) {
                    base = context.getCacheDir();
                } else if ("external-path".equals(tag)) {
                    base = Environment.getExternalStorageDirectory();
                } else if ("root-path".equals(tag)) {
                    base = new File("/");
                } else if ("external-files-path".equals(tag)) {
                    // androidx FileProvider 同款：type=null（path 属性 = 基下子目录）
                    File[] dirs = context.getExternalFilesDirs(null);
                    base = dirs != null && dirs.length > 0 ? dirs[0] : null;
                } else if ("external-cache-path".equals(tag)) {
                    File[] dirs = context.getExternalCacheDirs();
                    base = dirs != null && dirs.length > 0 ? dirs[0] : null;
                } else if ("external-media-path".equals(tag)) {
                    File[] dirs = context.getExternalMediaDirs();
                    base = dirs != null && dirs.length > 0 ? dirs[0] : null;
                } else if ("paths".equals(tag)) {
                    continue;
                } else {
                    sb.append(tag).append("|").append(name).append("|UNKNOWN;\n");
                    continue;
                }
                if (base == null) {
                    sb.append(tag).append("|").append(name).append("|nullBase;\n");
                    continue;
                }
                File f = path != null ? new File(base, path) : base;
                sb.append(tag).append("|").append(name).append("|").append(f.getPath()).append(";\n");
            }
        } catch (Exception e) {
            sb.append("parseError:").append(e).append(";\n");
        }
    }

    @Override
    public String getType(android.net.Uri uri) {
        return null;
    }

    @Override
    public android.net.Uri insert(android.net.Uri uri, android.content.ContentValues values) {
        return null;
    }

    @Override
    public int delete(android.net.Uri uri, String selection, String[] args) {
        return 0;
    }

    @Override
    public int update(android.net.Uri uri, android.content.ContentValues values, String selection,
                      String[] args) {
        return 0;
    }

    @Override
    public android.database.Cursor query(android.net.Uri uri, String[] projection, String selection,
                                         String[] args, String sortOrder) {
        return null;
    }
}
