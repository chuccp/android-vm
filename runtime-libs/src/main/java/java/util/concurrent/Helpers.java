package java.util.concurrent;

import java.util.Collection;

class Helpers {
    private Helpers() {}

    static String collectionToString(Collection<?> c) {
        final Object[] a = c.toArray();
        final int size = a.length;
        if (size == 0)
            return "[]";
        int charLength = 0;

        for (int i = 0; i < size; i++) {
            Object e = a[i];

            String s = (e == c) ? "(this Collection)" : objectToString(e);
            a[i] = s;
            charLength += s.length();
        }

        return toString(a, size, charLength);
    }

    static String toString(Object[] a, int size, int charLength) {

        final char[] chars = new char[charLength + 2 * size];
        chars[0] = '[';
        int j = 1;
        for (int i = 0; i < size; i++) {
            if (i > 0) {
                chars[j++] = ',';
                chars[j++] = ' ';
            }
            String s = (String) a[i];
            int len = s.length();
            s.getChars(0, len, chars, j);
            j += len;
        }
        chars[j] = ']';

        return new String(chars);
    }

    static String mapEntryToString(Object key, Object val) {
        final String k, v;
        final int klen, vlen;
        final char[] chars =
            new char[(klen = (k = objectToString(key)).length()) +
                     (vlen = (v = objectToString(val)).length()) + 1];
        k.getChars(0, klen, chars, 0);
        chars[klen] = '=';
        v.getChars(0, vlen, chars, klen + 1);
        return new String(chars);
    }

    private static String objectToString(Object x) {

        String s;
        return (x == null || (s = x.toString()) == null) ? "null" : s;
    }
}
