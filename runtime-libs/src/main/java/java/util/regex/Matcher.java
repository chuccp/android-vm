package java.util.regex;

public final class Matcher {
    private Pattern pattern;
    private com.google.re2j.Matcher implementation;
    private CharSequence input;
    private int regionStart = 0;
    private int regionEnd = -1;  // <0 = input.length()

    Matcher(
            Pattern pattern,
            com.google.re2j.Matcher implementation,
            CharSequence input) {
        this.pattern = pattern;
        this.implementation = implementation;
        this.input = input;
    }

    public Pattern pattern() {
        return pattern;
    }

    public Matcher reset() {
        regionStart = 0;
        regionEnd = -1;
        implementation.reset(input == null ? "" : input);
        return this;
    }

    public Matcher reset(CharSequence input) {
        this.input = input;
        regionStart = 0;
        regionEnd = -1;
        implementation.reset(input == null ? "" : input);
        return this;
    }

    public boolean matches() {
        return implementation.matches();
    }

    public boolean find() {
        return implementation.find();
    }

    public boolean find(int start) {
        region(start, regionEnd());
        return implementation.find();
    }

    public boolean lookingAt() {
        return implementation.lookingAt();
    }

    public boolean hitEnd() {
        return false;
    }

    public int groupCount() {
        return implementation.groupCount();
    }

    public String group() {
        return implementation.group();
    }

    public String group(int group) {
        return implementation.group(group);
    }

    public String group(String name) {
        return implementation.group(name);
    }

    public int start() {
        return absolute(implementation.start());
    }

    public int start(int group) {
        return absolute(implementation.start(group));
    }

    public int start(String name) {
        return absolute(implementation.start(name));
    }

    public int end() {
        return absolute(implementation.end());
    }

    public int end(int group) {
        return absolute(implementation.end(group));
    }

    public int end(String name) {
        return absolute(implementation.end(name));
    }

    public String replaceAll(String replacement) {
        return implementation.replaceAll(replacement);
    }

    public String replaceFirst(String replacement) {
        return implementation.replaceFirst(replacement);
    }

    public static String quoteReplacement(String value) {
        return com.google.re2j.Matcher.quoteReplacement(value);
    }

    public Matcher appendReplacement(
            StringBuffer target,
            String replacement) {
        implementation.appendReplacement(target, replacement);
        return this;
    }

    public Matcher appendReplacement(
            StringBuilder target,
            String replacement) {
        implementation.appendReplacement(target, replacement);
        return this;
    }

    public StringBuffer appendTail(StringBuffer target) {
        return implementation.appendTail(target);
    }

    public StringBuilder appendTail(StringBuilder target) {
        return implementation.appendTail(target);
    }

    @Override
    public String toString() {
        return "java.util.regex.Matcher[pattern=" +
            pattern.pattern() + " region=0," + input.length() + "]";
    }

    // ---- overlay：region/usePattern/requireEnd/toMatchResult API 补全 ----
    private boolean requireEndFlag = false;
    private boolean anchoringBounds = true;
    private boolean transparentBounds = false;

    /**
     * region 语义（OpenJDK）：把匹配窗口限制在 [start, end)。旧实现直接
     * reset 整个输入，导致 OkHttp MediaType 的参数解析（region + lookingAt）
     * 恒失败——"Parameter is not formatted correctly: ; charset=UTF-8"。
     *
     * RE2/J 无 region API，这里用子序列实现并把 start/end 的返回值换算回
     * 绝对下标（group 文本不受影响）。
     */
    public Matcher region(int start, int end) {
        if (input == null) {
            regionStart = 0;
            regionEnd = 0;
            implementation.reset("");
            return this;
        }
        if (start < 0 || start > input.length()) {
            throw new IndexOutOfBoundsException("start=" + start + ", length=" + input.length());
        }
        if (end < start || end > input.length()) {
            throw new IndexOutOfBoundsException("end=" + end + ", length=" + input.length());
        }
        regionStart = start;
        regionEnd = end;
        implementation.reset(input.subSequence(start, end));
        return this;
    }

    public int regionStart() {
        return regionStart;
    }

    public int regionEnd() {
        return regionEnd < 0 ? (input == null ? 0 : input.length()) : regionEnd;
    }

    private int absolute(int relative) {
        return relative < 0 ? relative : relative + regionStart;
    }

    /**
     * usePattern 语义（OpenJDK）：只换模式，保留 input 与 region 窗口。
     * 旧实现重建 matcher，把 region 窗口丢回整个输入——OkHttp Cookie.parseExpires
     * 的 TIME_PATTERN/DAY_OF_MONTH_PATTERN/MONTH_PATTERN/YEAR_PATTERN 逐段
     * region + usePattern + matches 全部失配，日期字段恒 -1，最后抛出无消息
     * IllegalArgumentException（"OkHttp internal error" 的直接原因）。
     *
     * RE2/J 1.8 的 Matcher 没有 usePattern，这里按「当前 region 窗口的子序列」
     * 重建底层 matcher（窗口语义不变，start/end 仍经 absolute 换算）。
     */
    public Matcher usePattern(Pattern newPattern) {
        if (newPattern == null) {
            throw new IllegalArgumentException("newPattern is null");
        }
        this.pattern = newPattern;
        CharSequence window;
        if (input == null) {
            window = "";
        } else if (regionEnd >= 0) {
            window = input.subSequence(regionStart, regionEnd);
        } else {
            window = input;
        }
        this.implementation = newPattern.implementation().matcher(window);
        return this;
    }

    public Matcher useAnchoringBounds(boolean b) {
        anchoringBounds = b;
        return this;
    }

    public Matcher useTransparentBounds(boolean b) {
        transparentBounds = b;
        return this;
    }

    public boolean hasAnchoringBounds() {
        return anchoringBounds;
    }

    public boolean hasTransparentBounds() {
        return transparentBounds;
    }

    public boolean requireEnd() {
        return requireEndFlag;
    }

    public MatchResult toMatchResult() {
        return new MatchResult() {
            @Override public int start() { return Matcher.this.start(); }
            @Override public int start(int group) { return Matcher.this.start(group); }
            @Override public int end() { return Matcher.this.end(); }
            @Override public int end(int group) { return Matcher.this.end(group); }
            @Override public String group() { return Matcher.this.group(); }
            @Override public String group(int group) { return Matcher.this.group(group); }
            @Override public int groupCount() { return Matcher.this.groupCount(); }
        };
    }

}
