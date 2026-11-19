package com.coke.target;

public final class TargetEntry {
    private static int nativeCounter;

    static {
        nativeCounter = 4;
    }

    private TargetEntry() {
    }

    public static int run() {
        int value = 40;
        return value + 2;
    }

    public static String identity() {
        return "simple-target-apk";
    }

    public static String screenTitle() {
        return "Minimal Android";
    }

    public static String screenMessage() {
        return "Running in a self-hosted DEX VM";
    }

    public static int nativeCallFlow(int value) {
        return nativeAdd(value, 3);
    }

    private static int nativeAdd(int left, int right) {
        return left + right;
    }

    public static int guestUid() {
        return android.os.Process.myUid();
    }

    public static int guestPid() {
        return android.os.Process.myPid();
    }

    public static int nativeObjectFlow() {
        NativeCounter counter = new NativeCounter(10);
        return counter.add(5);
    }

    public static int nativeClassInitFlow() {
        nativeCounter += 3;
        return nativeCounter;
    }

    public static int serviceResult(int starts) {
        return starts > 0 ? 1 : 0;
    }

    private static final class NativeCounter {
        private final int value;

        private NativeCounter(int value) {
            this.value = value;
        }

        private int add(int delta) {
            return value + delta;
        }
    }
}
