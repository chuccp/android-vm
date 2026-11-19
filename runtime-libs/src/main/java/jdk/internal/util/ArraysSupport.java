package jdk.internal.util;

public final class ArraysSupport {
    private ArraysSupport() {
    }

    public static int mismatch(
            boolean[] a, boolean[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            boolean[] a, int aFrom, boolean[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (a[aFrom + index] != b[bFrom + index]) return index;
        }
        return ~length;
    }

    public static int mismatch(byte[] a, byte[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            byte[] a, int aFrom, byte[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (a[aFrom + index] != b[bFrom + index]) return index;
        }
        return ~length;
    }

    public static int mismatch(char[] a, char[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            char[] a, int aFrom, char[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (a[aFrom + index] != b[bFrom + index]) return index;
        }
        return ~length;
    }

    public static int mismatch(short[] a, short[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            short[] a, int aFrom, short[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (a[aFrom + index] != b[bFrom + index]) return index;
        }
        return ~length;
    }

    public static int mismatch(int[] a, int[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            int[] a, int aFrom, int[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (a[aFrom + index] != b[bFrom + index]) return index;
        }
        return ~length;
    }

    public static int mismatch(long[] a, long[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            long[] a, int aFrom, long[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (a[aFrom + index] != b[bFrom + index]) return index;
        }
        return ~length;
    }

    public static int mismatch(float[] a, float[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            float[] a, int aFrom, float[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (Float.floatToIntBits(a[aFrom + index]) !=
                    Float.floatToIntBits(b[bFrom + index])) {
                return index;
            }
        }
        return ~length;
    }

    public static int mismatch(double[] a, double[] b, int length) {
        return mismatch(a, 0, b, 0, length);
    }

    public static int mismatch(
            double[] a, int aFrom, double[] b, int bFrom, int length) {
        for (int index = 0; index < length; index++) {
            if (Double.doubleToLongBits(a[aFrom + index]) !=
                    Double.doubleToLongBits(b[bFrom + index])) {
                return index;
            }
        }
        return ~length;
    }

    // ---- overlay：LOG2_ARRAY_*_INDEX_SCALE + vectorizedMismatch API 补全
    //（供 java.nio.BufferMismatch/Bits 全量源编译）----
    public static final int LOG2_ARRAY_BOOLEAN_INDEX_SCALE = 0;
    public static final int LOG2_ARRAY_BYTE_INDEX_SCALE = 0;
    public static final int LOG2_ARRAY_CHAR_INDEX_SCALE = 1;
    public static final int LOG2_ARRAY_SHORT_INDEX_SCALE = 1;
    public static final int LOG2_ARRAY_INT_INDEX_SCALE = 2;
    public static final int LOG2_ARRAY_FLOAT_INDEX_SCALE = 2;
    public static final int LOG2_ARRAY_LONG_INDEX_SCALE = 3;
    public static final int LOG2_ARRAY_DOUBLE_INDEX_SCALE = 3;

    public static int vectorizedMismatch(Object a, long aOffset, Object b, long bOffset,
                                         int length, int log2ArrayIndexScale) {
        // 入参 offset 是【字节偏移】（Buffer.address = arrayOffset * indexScale）；
        // 按 log2 换算为元素下标（caller 仅 BufferMismatch）。
        int aIndex = (int) aOffset >> log2ArrayIndexScale;
        int bIndex = (int) bOffset >> log2ArrayIndexScale;
        int i = 0;
        switch (log2ArrayIndexScale) {
            case 0: {
                byte[] aa = (byte[]) a;
                byte[] bb = (byte[]) b;
                while (i < length && aa[aIndex + i] == bb[bIndex + i]) i++;
                break;
            }
            case 1: {
                char[] aa = (char[]) a;
                char[] bb = (char[]) b;
                while (i < length && aa[aIndex + i] == bb[bIndex + i]) i++;
                break;
            }
            case 2: {
                int[] aa = (int[]) a;
                int[] bb = (int[]) b;
                while (i < length && aa[aIndex + i] == bb[bIndex + i]) i++;
                break;
            }
            default: {
                long[] aa = (long[]) a;
                long[] bb = (long[]) b;
                while (i < length && aa[aIndex + i] == bb[bIndex + i]) i++;
                break;
            }
        }
        return i == length ? -1 : i;
    }
}
