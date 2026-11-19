package com.coke.target;

/**
 * Compatibility fixture for DEX shapes commonly emitted by Java and Kotlin:
 * switches, inheritance, interfaces, anonymous classes, lambdas, generics,
 * enums, exceptions, finally, arrays and instance checks.
 */
public final class RuntimeCapabilities {
    private RuntimeCapabilities() {
    }

    interface Unary {
        int apply(int value);
    }

    private static class Base {
        int value() {
            return 2;
        }
    }

    private static final class Doubled extends Base {
        @Override
        int value() {
            return super.value() * 2;
        }
    }

    private static final class AddSix implements Unary {
        @Override
        public int apply(int value) {
            return value + 6;
        }
    }

    private enum Mode {
        ALPHA,
        BETA,
        GAMMA
    }

    private static final class Box<T> {
        private final T value;

        Box(T value) {
            this.value = value;
        }

        T get() {
            return value;
        }
    }

    private static final class Pair {
        private final int left;
        private final int right;

        Pair(int left, int right) {
            this.left = left;
            this.right = right;
        }

        @Override
        public boolean equals(Object other) {
            if (this == other) {
                return true;
            }
            if (!(other instanceof Pair)) {
                return false;
            }
            Pair pair = (Pair) other;
            return left == pair.left && right == pair.right;
        }
    }

    private final int innerBase = 3;

    private final class Inner {
        int add(int value) {
            return innerBase + value;
        }
    }

    private static final class SpecialFailure extends Exception {
    }

    private static final class Resource implements AutoCloseable {
        int value() {
            return 9;
        }

        @Override
        public void close() {
        }
    }

    public static int run() {
        return integerSwitch(2)
                + stringSwitch("beta")
                + polymorphism()
                + interfacesAndAnonymous()
                + exceptionFinally()
                + instanceChecks()
                + arrayAndGenerics()
                + enumFlow()
                + objectEquality()
                + bitwiseFlow()
                + primitiveFlow()
                + arraysAndObjects()
                + innerClassFlow()
                + customExceptionFlow()
                + varargsFlow()
                + synchronizedFlow()
                + tryWithResourcesFlow()
                + enumSwitchFlow()
                + stringOperations()
                + floatingCompareFlow();
    }

    private static int integerSwitch(int value) {
        switch (value) {
            case 1:
                return 2;
            case 2:
                return 5;
            case 9:
                return 11;
            default:
                return 13;
        }
    }

    private static int stringSwitch(String value) {
        switch (value) {
            case "alpha":
                return 3;
            case "beta":
                return 7;
            default:
                return 17;
        }
    }

    private static int polymorphism() {
        Base value = new Doubled();
        return value.value();
    }

    private static int interfacesAndAnonymous() {
        Unary direct = new AddSix();
        Unary anonymous = new Unary() {
            @Override
            public int apply(int value) {
                return value * 3;
            }
        };
        Unary lambda = value -> value + 6;
        return direct.apply(1) + anonymous.apply(2) + lambda.apply(3);
    }

    private static int exceptionFinally() {
        int total = 0;
        try {
            total += 3;
            if (total == 3) {
                throw new IllegalStateException();
            }
        } catch (IllegalStateException failure) {
            total += 5;
        } finally {
            total += 7;
        }
        return total;
    }

    private static int instanceChecks() {
        return instanceScore("vm") * 10 + instanceScore(new Doubled());
    }

    private static int instanceScore(Object value) {
        int score = 0;
        if (value instanceof String) {
            score += 1;
        }
        if (value instanceof Base) {
            score += 2;
        }
        if (value instanceof Object) {
            score += 4;
        }
        return score;
    }

    private static int arrayAndGenerics() {
        int[] values = {1, 2, 3, 4};
        int total = 0;
        for (int value : values) {
            total += value;
        }
        Box<String> box = new Box<>("vm");
        return total + box.get().length();
    }

    private static int enumFlow() {
        return Mode.GAMMA.ordinal() + 21;
    }

    private static int objectEquality() {
        return new Pair(4, 9).equals(new Pair(4, 9)) ? 29 : 31;
    }

    public static int bitwiseFlow() {
        int value = ((5 << 2) | 3) ^ 1;
        return value;
    }

    public static int primitiveFlow() {
        long shifted = (1L << 34) + 7L;
        int high = (int) (shifted >>> 32);
        double ratio = high + 1.5;
        int cast = (int) ratio;
        float floatValue = (float) 2.25;
        int floatInt = (int) floatValue;
        byte truncated = (byte) 257;
        return high + cast + floatInt + (truncated & 0xff);
    }

    public static int arraysAndObjects() {
        int[][] grid = new int[2][3];
        grid[1][2] = 9;
        String[] labels = {"vm", "dex"};
        return grid[1][2] + labels.length;
    }

    public static int innerClassFlow() {
        RuntimeCapabilities outer = new RuntimeCapabilities();
        Inner inner = outer.new Inner();
        return inner.add(4);
    }

    public static int customExceptionFlow() {
        try {
            throw new SpecialFailure();
        } catch (SpecialFailure failure) {
            return 13;
        }
    }

    public static int varargsFlow() {
        return sum(1, 2, 3);
    }

    private static int sum(int... values) {
        int total = 0;
        for (int value : values) {
            total += value;
        }
        return total;
    }

    public static int synchronizedFlow() {
        Object lock = new Object();
        synchronized (lock) {
            return 11;
        }
    }

    public static int tryWithResourcesFlow() {
        try (Resource resource = new Resource()) {
            return resource.value();
        }
    }

    public static int enumSwitchFlow() {
        switch (Mode.GAMMA) {
            case ALPHA:
                return 3;
            case BETA:
                return 5;
            case GAMMA:
                return 8;
            default:
                return 10;
        }
    }

    public static int stringOperations() {
        return "  vm-compat  ".trim().toUpperCase().length();
    }

    public static int floatingCompareFlow() {
        int score = 0;
        float floatValue = Float.NaN;
        double doubleValue = Double.NaN;
        if (floatValue < 1.0f) {
            score += 2;
        }
        if (floatValue > 1.0f) {
            score += 4;
        }
        if (doubleValue <= 1.0) {
            score += 8;
        }
        if (doubleValue >= 1.0) {
            score += 16;
        }
        if (1.0f < 2.0f) {
            score += 1;
        }
        if (1.0 < 2.0) {
            score += 1;
        }
        return score;
    }

    public static int floatingPredicateFlow() {
        int score = 0;
        if (Float.isNaN(Float.NaN)) {
            score += 1;
        }
        if (!Float.isFinite(Float.NaN)) {
            score += 2;
        }
        if (Double.isInfinite(Double.POSITIVE_INFINITY)) {
            score += 4;
        }
        Float boxed = Float.valueOf(Float.NaN);
        if (boxed.isNaN()) {
            score += 8;
        }
        return score;
    }
}
