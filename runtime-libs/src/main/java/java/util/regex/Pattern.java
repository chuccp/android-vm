package java.util.regex;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;

public final class Pattern {
    public static final int UNIX_LINES = 0x01;
    public static final int CASE_INSENSITIVE = 0x02;
    public static final int COMMENTS = 0x04;
    public static final int MULTILINE = 0x08;
    public static final int LITERAL = 0x10;
    public static final int DOTALL = 0x20;
    public static final int UNICODE_CASE = 0x40;
    public static final int CANON_EQ = 0x80;
    public static final int UNICODE_CHARACTER_CLASS = 0x100;

    private final com.google.re2j.Pattern implementation;
    private final String original;

    private Pattern(String expression, int flags) {
        this.original = expression;
        int translatedFlags = flags & (
            com.google.re2j.Pattern.CASE_INSENSITIVE |
                com.google.re2j.Pattern.MULTILINE |
                com.google.re2j.Pattern.DOTALL);
        String source = translateNamedGroups(expression);
        if ((flags & LITERAL) != 0) {
            source = quote(source);
        }
        com.google.re2j.Pattern impl;
        try {
            impl = com.google.re2j.Pattern.compile(source, translatedFlags);
        } catch (com.google.re2j.PatternSyntaxException first) {
            // re2j 不认 Java 的 POSIX 字符类（\p{Digit}/\p{XDigit} 等，
            // Kotlin toFloatOrNull 的 float regex 即此类）。按语义翻译后重试；
            // 翻译不了仍按原异常抛出。
            String translated = translateJavaCharacterClasses(source);
            if (translated.equals(source)) {
                throw new PatternSyntaxException(
                    first.getDescription(), first.getPattern(), first.getIndex());
            }
            try {
                impl = com.google.re2j.Pattern.compile(translated, translatedFlags);
            } catch (com.google.re2j.PatternSyntaxException second) {
                throw new PatternSyntaxException(
                    second.getDescription(), second.getPattern(), second.getIndex());
            }
        }
        this.implementation = impl;
    }

    public static Pattern compile(String expression) {
        return compile(expression, 0);
    }

    public static Pattern compile(String expression, int flags) {
        if (expression == null) {
            throw new NullPointerException("regex");
        }
        return new Pattern(expression, flags);
    }

    public static boolean matches(String expression, CharSequence input) {
        return compile(expression).matcher(input).matches();
    }

    public static String quote(String value) {
        return com.google.re2j.Pattern.quote(value);
    }

    public String pattern() {
        return original;
    }

    public int flags() {
        return implementation.flags();
    }

    public Matcher matcher(CharSequence input) {
        return new Matcher(this, implementation.matcher(input), input);
    }

    public String[] split(CharSequence input) {
        return split(input, 0);
    }

    public String[] split(CharSequence input, int limit) {
        String[] values = implementation.split(
            input.toString(),
            limit
        );
        if (limit == 0) return values;
        List<String> result = new ArrayList<>(values.length);
        Collections.addAll(result, values);
        while (result.size() < limit) {
            result.add("");
        }
        return result.toArray(new String[0]);
    }

    @Override
    public String toString() {
        return implementation.toString();
    }

    com.google.re2j.Pattern implementation() {
        return implementation;
    }

    Map<String, Integer> namedGroups() {
        return implementation.namedGroups();
    }

    /**
     * Java/POSIX 预定义字符类 -> re2j 可编译形态。
     *
     * <p>Java 的 \p{Digit}/\p{XDigit}/\p{Alpha} 等是 java.util.regex 的
     * 命名类，re2j 只认 Unicode 类别（\p{Nd}/\p{L}…）。仅在被 re2j 拒绝
     * 后调用；字符类内的 XDigit/Blank/ASCII 需要以区间形式展开（避免嵌套
     * 方括号），类外则用 [...] 包装。
     */
    private static String translateJavaCharacterClasses(String expression) {
        StringBuilder out = new StringBuilder(expression.length() + 16);
        int classDepth = 0;
        for (int i = 0; i < expression.length();) {
            char c = expression.charAt(i);
            if (c == '\\' && i + 2 < expression.length()
                    && (expression.charAt(i + 1) == 'p' || expression.charAt(i + 1) == 'P')
                    && expression.charAt(i + 2) == '{') {
                int end = expression.indexOf('}', i + 3);
                if (end > 0) {
                    String name = expression.substring(i + 3, end);
                    String mapped = mapJavaCharacterClass(
                        name, expression.charAt(i + 1) == 'P', classDepth > 0);
                    if (mapped != null) {
                        out.append(mapped);
                        i = end + 1;
                        continue;
                    }
                }
            }
            if (c == '\\' && i + 1 < expression.length()) {
                out.append(c).append(expression.charAt(i + 1));
                i += 2;
                continue;
            }
            if (c == '[') {
                classDepth++;
            } else if (c == ']' && classDepth > 0) {
                classDepth--;
            }
            out.append(c);
            i++;
        }
        return out.toString();
    }

    private static String mapJavaCharacterClass(String name, boolean negated,
                                                boolean insideClass) {
        switch (name) {
            case "Digit":
            case "IsDigit":
            case "javaDigit":
                return negated ? "\\P{Nd}" : "\\p{Nd}";
            case "Alphabetic":
            case "IsAlphabetic":
            case "Alpha":
            case "IsAlpha":
            case "IsLetter":
            case "javaAlphabetic":
            case "javaLetter":
                return negated ? "\\P{L}" : "\\p{L}";
            case "Lower":
            case "IsLower":
            case "javaLowerCase":
                return negated ? "\\P{Ll}" : "\\p{Ll}";
            case "Upper":
            case "IsUpper":
            case "javaUpperCase":
                return negated ? "\\P{Lu}" : "\\p{Lu}";
            case "Space":
            case "IsSpace":
            case "javaWhitespace":
                return negated ? "\\S" : "\\s";
            case "Punct":
            case "IsPunct":
                return negated ? "\\P{P}" : "\\p{P}";
            case "Cntrl":
            case "IsCntrl":
                return negated ? "\\P{Cc}" : "\\p{Cc}";
            case "Alnum":
            case "IsAlnum":
            case "javaLetterOrDigit":
                if (negated) {
                    return insideClass ? null : "[^\\p{L}\\p{Nd}]";
                }
                return insideClass ? "\\p{L}\\p{Nd}" : "[\\p{L}\\p{Nd}]";
            case "XDigit":
            case "IsXDigit":
                if (negated) {
                    return insideClass ? null : "[^0-9A-Fa-f]";
                }
                return insideClass ? "0-9A-Fa-f" : "[0-9A-Fa-f]";
            case "Blank":
            case "IsBlank":
                if (negated) {
                    return insideClass ? null : "[^ \\t]";
                }
                return insideClass ? " \\t" : "[ \\t]";
            case "ASCII":
            case "IsASCII":
                if (negated) {
                    return insideClass ? null : "[^\\x00-\\x7F]";
                }
                return insideClass ? "\\x00-\\x7F" : "[\\x00-\\x7F]";
            default:
                return null;
        }
    }

    private static String translateNamedGroups(String expression) {
        StringBuilder output = new StringBuilder(expression.length() + 8);
        int index = 0;
        while (index < expression.length()) {
            if (index + 3 < expression.length() &&
                    expression.charAt(index) == '(' &&
                    expression.charAt(index + 1) == '?' &&
                    expression.charAt(index + 2) == '<') {
                char next = expression.charAt(index + 3);
                if (next != '=' && next != '!') {
                    output.append("(?P<");
                    index += 3;
                    continue;
                }
            }
            output.append(expression.charAt(index++));
        }
        return output.toString();
    }
}
