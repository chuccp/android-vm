package android.content;

import android.net.Uri;
import java.util.ArrayList;
import java.util.List;

/**
 * URI 模式匹配树（Android 公开面；§31.7 C 档纯逻辑→lib 形态）。
 *
 * <p>AOSP android.content.UriMatcher 公开语义：
 * <ul>
 *   <li>{@link #addURI(String, String, int)}：按 authority → 路径段建树登记；
 *       路径首尾 '/' 容错（首 '/' 忽略、尾 '/' 剥除）；段 "*" 匹配任意单段，
 *       段 "#" 匹配纯数字段（{@code [0-9]+}）；code &lt; 0 抛
 *       IllegalArgumentException（AOSP 文案）。</li>
 *   <li>{@link #match(Uri)}：authority + 全部路径段逐段消费（{@link Uri#getPathSegments()}
 *       口径——解码段、空段省略）；须整段耗尽才算命中（规则长于 URI 不命中）。
 *       同层特异度优先级 = 字面 &gt; "#" &gt; "*"（DFS 回溯，登记序无关），
 *       即「最长路径段匹配 + 最特异通配优先」；未命中返回 {@link #NO_MATCH}。</li>
 * </ul>
 *
 * <p>实现为纯 lib Java（树节点自引用，无 VM 缝、无特判）；数据面取
 * {@code uri.getAuthority()}/{@code uri.getPathSegments()}（builtin Uri 查询面）。
 */
public class UriMatcher {

    /** 未命中返回值（AOSP 常量）。 */
    public static final int NO_MATCH = -1;

    private static final int EXACT = 0;
    private static final int NUMBER = 1;
    private static final int TEXT = 2;

    private final String mText;
    private final int mWhich;
    private int mCode;
    private ArrayList<UriMatcher> mChildren;

    /**
     * 建匹配树根（AOSP {@code UriMatcher(int code)}：根 code = NO_MATCH 为常规用法）。
     */
    public UriMatcher(int code) {
        mText = null;
        mWhich = EXACT;
        mCode = code;
    }

    private UriMatcher(String text, int which) {
        mText = text;
        mWhich = which;
        mCode = NO_MATCH;
    }

    /**
     * 登记一条规则（authority + path → code）。path 可带首 '/'（忽略）与尾
     * '/'（len&gt;1 时剥除）；null/空路径 = 该 authority 节点自身命中。
     */
    public void addURI(String authority, String path, int code) {
        if (code < 0) {
            throw new IllegalArgumentException("code " + code + " is negative: " + code);
        }
        ArrayList<String> segments = new ArrayList<String>();
        segments.add(authority == null ? "" : authority);
        if (path != null && path.length() > 0) {
            String p = path;
            if (p.charAt(0) == '/') {
                p = p.substring(1);
            }
            if (p.length() > 1 && p.charAt(p.length() - 1) == '/') {
                p = p.substring(0, p.length() - 1);
            }
            int start = 0;
            int len = p.length();
            while (start <= len) {
                int slash = p.indexOf('/', start);
                int end = slash < 0 ? len : slash;
                if (start < end) {  // 空段省略（与 getPathSegments 同口径）
                    segments.add(p.substring(start, end));
                }
                if (slash < 0) {
                    break;
                }
                start = slash + 1;
            }
        }

        UriMatcher node = this;
        for (int i = 0; i < segments.size(); i++) {
            node = node.childFor(segments.get(i));
        }
        node.mCode = code;
    }

    private UriMatcher childFor(String segment) {
        if (mChildren == null) {
            mChildren = new ArrayList<UriMatcher>();
        }
        for (int i = 0; i < mChildren.size(); i++) {
            UriMatcher child = mChildren.get(i);
            if (segment.equals(child.mText)) {
                return child;
            }
        }
        int which = EXACT;
        if ("*".equals(segment)) {
            which = TEXT;
        } else if ("#".equals(segment)) {
            which = NUMBER;
        }
        UriMatcher child = new UriMatcher(segment, which);
        mChildren.add(child);
        return child;
    }

    /**
     * 以 URI 的 authority + 路径段匹配本树：全段耗尽且节点带 code 才命中。
     * 同层按 字面 &gt; "#" &gt; "*" 优先尝试（子树不完整则回溯）。
     */
    public int match(Uri uri) {
        if (uri == null) {
            throw new NullPointerException("uri");
        }
        String auth = uri.getAuthority();
        List<String> pathSegments = uri.getPathSegments();
        int size = pathSegments.size();
        String[] items = new String[size + 1];
        items[0] = (auth == null) ? "" : auth;
        for (int i = 0; i < size; i++) {
            items[i + 1] = pathSegments.get(i);
        }
        return matchItems(items, 0);
    }

    private int matchItems(String[] items, int index) {
        if (index == items.length) {
            return mCode;  // 无规则节点 = NO_MATCH
        }
        if (mChildren == null) {
            return NO_MATCH;
        }
        String item = items[index];
        // 特异度序：字面 → "#"（纯数字）→ "*"；逐类尝试，子树未命中回溯。
        UriMatcher exact = findChild(item, EXACT);
        if (exact != null) {
            int r = exact.matchItems(items, index + 1);
            if (r != NO_MATCH) {
                return r;
            }
        }
        if (isDigits(item)) {
            UriMatcher number = findChildByWhich(NUMBER);
            if (number != null) {
                int r = number.matchItems(items, index + 1);
                if (r != NO_MATCH) {
                    return r;
                }
            }
        }
        UriMatcher text = findChildByWhich(TEXT);
        if (text != null) {
            return text.matchItems(items, index + 1);
        }
        return NO_MATCH;
    }

    private UriMatcher findChild(String item, int which) {
        for (int i = 0; i < mChildren.size(); i++) {
            UriMatcher child = mChildren.get(i);
            if (child.mWhich == which && item.equals(child.mText)) {
                return child;
            }
        }
        return null;
    }

    private UriMatcher findChildByWhich(int which) {
        for (int i = 0; i < mChildren.size(); i++) {
            UriMatcher child = mChildren.get(i);
            if (child.mWhich == which) {
                return child;
            }
        }
        return null;
    }

    private static boolean isDigits(String s) {
        int len = s.length();
        if (len == 0) {
            return false;
        }
        for (int i = 0; i < len; i++) {
            char c = s.charAt(i);
            if (c < '0' || c > '9') {
                return false;
            }
        }
        return true;
    }
}
