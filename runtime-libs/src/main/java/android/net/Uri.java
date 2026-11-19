package android.net;

import java.io.File;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;

/**
 * URI（AOSP android.net.Uri 公开面；§31.7 C 档 lib——纯字符串解析/构造逻辑，
 * 无 VM 布局/native 需求）。字段 mUriString 保存原始串（与 VM 侧历史字段名兼容）。
 */
public abstract class Uri implements Comparable<Uri> {

    public static final Uri EMPTY = new StringUri("");

    final String mUriString;

    Uri(String uriString) {
        this.mUriString = uriString == null ? "" : uriString;
    }

    public static Uri parse(String uriString) {
        if (uriString == null || uriString.isEmpty()) return EMPTY;
        return new StringUri(uriString);
    }

    public static Uri fromParts(String scheme, String ssp, String fragment) {
        StringBuilder sb = new StringBuilder();
        if (scheme != null) sb.append(scheme).append(':');
        if (ssp != null) sb.append(ssp);
        if (fragment != null) sb.append('#').append(fragment);
        return parse(sb.toString());
    }

    public static Uri withAppendedPath(Uri baseUri, String pathSegment) {
        if (baseUri == null) return null;
        return parse(baseUri.toString() + "/" + pathSegment);
    }

    public String getScheme() {
        int colon = mUriString.indexOf(':');
        return colon < 0 ? null : mUriString.substring(0, colon);
    }

    private String getSsp() {
        int colon = mUriString.indexOf(':');
        String rest = colon < 0 ? mUriString : mUriString.substring(colon + 1);
        int hash = rest.indexOf('#');
        return hash < 0 ? rest : rest.substring(0, hash);
    }

    public String getSchemeSpecificPart() {
        return decode(getEncodedSchemeSpecificPart());
    }

    public String getEncodedSchemeSpecificPart() {
        return getSsp();
    }

    private String sspAfterDoubleSlash() {
        String ssp = getSsp();
        return ssp.startsWith("//") ? ssp.substring(2) : null;
    }

    public String getAuthority() {
        return decode(getEncodedAuthority());
    }

    public String getEncodedAuthority() {
        String s = sspAfterDoubleSlash();
        if (s == null) return null;
        int slash = s.indexOf('/');
        int query = s.indexOf('?');
        int end = slash < 0 ? s.length() : slash;
        if (query >= 0 && query < end) end = query;
        return s.substring(0, end);
    }

    public String getPath() {
        return decode(getEncodedPath());
    }

    public String getEncodedPath() {
        String ssp = getSsp();
        int start = ssp.startsWith("//") ? ssp.indexOf('/', 2) : (ssp.startsWith("/") ? 0 : -1);
        if (start < 0) return null;
        int query = ssp.indexOf('?', start);
        return query < 0 ? ssp.substring(start) : ssp.substring(start, query);
    }

    public String getQuery() {
        return decode(getEncodedQuery());
    }

    public String getEncodedQuery() {
        String ssp = getSsp();
        int q = ssp.indexOf('?');
        return q < 0 ? null : ssp.substring(q + 1);
    }

    public String getFragment() {
        return decode(getEncodedFragment());
    }

    public String getEncodedFragment() {
        int hash = mUriString.indexOf('#');
        return hash < 0 ? null : mUriString.substring(hash + 1);
    }

    public String getHost() {
        String auth = getEncodedAuthority();
        if (auth == null) return null;
        int at = auth.lastIndexOf('@');
        if (at >= 0) auth = auth.substring(at + 1);
        int colon = auth.lastIndexOf(':');
        return colon < 0 ? auth : auth.substring(0, colon);
    }

    public int getPort() {
        String auth = getEncodedAuthority();
        if (auth == null) return -1;
        int at = auth.lastIndexOf('@');
        if (at >= 0) auth = auth.substring(at + 1);
        int colon = auth.lastIndexOf(':');
        if (colon < 0) return -1;
        try {
            return Integer.parseInt(auth.substring(colon + 1));
        } catch (NumberFormatException e) {
            return -1;
        }
    }

    public String getUserInfo() {
        String auth = getEncodedAuthority();
        if (auth == null) return null;
        int at = auth.lastIndexOf('@');
        return at < 0 ? null : auth.substring(0, at);
    }

    public List<String> getPathSegments() {
        String path = getEncodedPath();
        if (path == null) return Collections.emptyList();
        ArrayList<String> out = new ArrayList<>();
        for (String seg : path.split("/")) {
            if (!seg.isEmpty()) out.add(decode(seg));
        }
        return out;
    }

    public String getLastPathSegment() {
        List<String> segs = getPathSegments();
        return segs.isEmpty() ? null : segs.get(segs.size() - 1);
    }

    public String getQueryParameter(String key) {
        List<String> all = getQueryParameters(key);
        return all.isEmpty() ? null : all.get(0);
    }

    public List<String> getQueryParameters(String key) {
        ArrayList<String> out = new ArrayList<>();
        String query = getEncodedQuery();
        if (query == null || key == null) return out;
        for (String pair : query.split("&")) {
            int eq = pair.indexOf('=');
            String k = eq < 0 ? pair : pair.substring(0, eq);
            if (!decode(k).equals(key)) continue;
            out.add(eq < 0 ? "" : decode(pair.substring(eq + 1)));
        }
        return out;
    }

    public Set<String> getQueryParameterNames() {
        LinkedHashSet<String> out = new LinkedHashSet<>();
        String query = getEncodedQuery();
        if (query == null) return out;
        for (String pair : query.split("&")) {
            int eq = pair.indexOf('=');
            out.add(decode(eq < 0 ? pair : pair.substring(0, eq)));
        }
        return out;
    }

    public boolean isHierarchical() {
        String ssp = getSsp();
        return ssp.startsWith("/") || ssp.startsWith("//");
    }

    public boolean isOpaque() {
        String ssp = getSsp();
        return !(ssp.startsWith("/") || ssp.startsWith("//"));
    }

    public boolean isAbsolute() {
        return getScheme() != null;
    }

    public boolean isRelative() {
        return getScheme() == null;
    }

    public Uri normalizeScheme() {
        String scheme = getScheme();
        if (scheme == null) return this;
        String lower = scheme.toLowerCase(java.util.Locale.ROOT);
        if (lower.equals(scheme)) return this;
        String rest = mUriString.substring(scheme.length());
        return parse(lower + rest);
    }

    public static String encode(String s) {
        return encode(s, null);
    }

    public static String encode(String s, String allow) {
        if (s == null) return null;
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            boolean ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                    || (c >= '0' && c <= '9') || "-_.!~*'()".indexOf(c) >= 0
                    || (allow != null && allow.indexOf(c) >= 0);
            if (ok) {
                sb.append(c);
            } else {
                byte[] bytes = String.valueOf(c).getBytes(java.nio.charset.StandardCharsets.UTF_8);
                for (byte b : bytes) {
                    sb.append('%');
                    String hex = Integer.toHexString(b & 0xFF).toUpperCase(java.util.Locale.ROOT);
                    if (hex.length() < 2) sb.append('0');
                    sb.append(hex);
                }
            }
        }
        return sb.toString();
    }

    public static String decode(String s) {
        if (s == null || s.indexOf('%') < 0) return s;
        java.io.ByteArrayOutputStream out = new java.io.ByteArrayOutputStream();
        for (int i = 0; i < s.length(); ) {
            char c = s.charAt(i);
            if (c == '%' && i + 2 < s.length()) {
                try {
                    out.write(Integer.parseInt(s.substring(i + 1, i + 3), 16));
                    i += 3;
                    continue;
                } catch (NumberFormatException ignored) {
                }
            }
            byte[] bytes = String.valueOf(c).getBytes(java.nio.charset.StandardCharsets.UTF_8);
            out.write(bytes, 0, bytes.length);
            i++;
        }
        return new String(out.toByteArray(), java.nio.charset.StandardCharsets.UTF_8);
    }

    public File getPathFile() {
        String path = getPath();
        return path == null ? null : new File(path);
    }

    public Builder buildUpon() {
        return new Builder(this);
    }

    public static final class Builder {
        private String mScheme;
        private String mAuthority;
        private String mPath;
        private String mQuery;
        private String mFragment;

        public Builder() {
        }

        Builder(Uri uri) {
            if (uri == null) return;
            mScheme = uri.getScheme();
            mAuthority = uri.getEncodedAuthority();
            mPath = uri.getEncodedPath();
            mQuery = uri.getEncodedQuery();
            mFragment = uri.getEncodedFragment();
        }

        public Builder scheme(String scheme) {
            mScheme = scheme;
            return this;
        }

        public Builder authority(String authority) {
            mAuthority = authority;
            return this;
        }

        public Builder encodedAuthority(String authority) {
            mAuthority = authority;
            return this;
        }

        public Builder path(String path) {
            mPath = path;
            return this;
        }

        public Builder encodedPath(String path) {
            mPath = path;
            return this;
        }

        public Builder appendPath(String newSegment) {
            if (mPath == null || mPath.isEmpty() || "/".equals(mPath)) {
                mPath = "/" + newSegment;
            } else {
                mPath = mPath + "/" + newSegment;
            }
            return this;
        }

        public Builder query(String query) {
            mQuery = query;
            return this;
        }

        public Builder encodedQuery(String query) {
            mQuery = query;
            return this;
        }

        public Builder appendQueryParameter(String key, String value) {
            String pair = encode(key) + "=" + (value == null ? "" : encode(value));
            mQuery = (mQuery == null || mQuery.isEmpty()) ? pair : mQuery + "&" + pair;
            return this;
        }

        public Builder clearQuery() {
            mQuery = null;
            return this;
        }

        public Builder fragment(String fragment) {
            mFragment = fragment;
            return this;
        }

        public Builder encodedFragment(String fragment) {
            mFragment = fragment;
            return this;
        }

        public Builder buildUpon() {
            Builder b = new Builder();
            b.mScheme = mScheme;
            b.mAuthority = mAuthority;
            b.mPath = mPath;
            b.mQuery = mQuery;
            b.mFragment = mFragment;
            return b;
        }

        public Uri build() {
            StringBuilder sb = new StringBuilder();
            if (mScheme != null) sb.append(mScheme).append(':');
            if (mAuthority != null) sb.append("//").append(mAuthority);
            if (mPath != null) sb.append(mPath);
            if (mQuery != null) sb.append('?').append(mQuery);
            if (mFragment != null) sb.append('#').append(mFragment);
            return parse(sb.toString());
        }

        @Override
        public String toString() {
            return build().toString();
        }
    }

    static final class StringUri extends Uri {
        StringUri(String uriString) {
            super(uriString);
        }
    }

    @Override
    public String toString() {
        return mUriString;
    }

    @Override
    public boolean equals(Object o) {
        if (this == o) return true;
        if (!(o instanceof Uri)) return false;
        return mUriString.equals(((Uri) o).mUriString);
    }

    @Override
    public int hashCode() {
        return mUriString.hashCode();
    }

    @Override
    public int compareTo(Uri other) {
        return mUriString.compareTo(other.mUriString);
    }
}
