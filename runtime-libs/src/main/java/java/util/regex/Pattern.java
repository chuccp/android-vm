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

    private Pattern(String expression, int flags) {
        int translatedFlags = flags & (
            com.google.re2j.Pattern.CASE_INSENSITIVE |
                com.google.re2j.Pattern.MULTILINE |
                com.google.re2j.Pattern.DOTALL);
        String source = translateNamedGroups(expression);
        if ((flags & LITERAL) != 0) {
            source = quote(source);
        }
        try {
            implementation =
                com.google.re2j.Pattern.compile(source, translatedFlags);
        } catch (com.google.re2j.PatternSyntaxException failure) {
            throw new PatternSyntaxException(
                failure.getDescription(),
                failure.getPattern(),
                failure.getIndex());
        }
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
        return implementation.pattern();
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
