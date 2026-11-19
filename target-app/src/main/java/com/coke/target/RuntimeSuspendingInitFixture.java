package com.coke.target;

public final class RuntimeSuspendingInitFixture {
    private static int synchronousCalls;

    private static final class ValueHolder {
        private static final String VALUE;

        private static String createValue() {
            try {
                Thread.sleep(1);
            } catch (InterruptedException failure) {
                Thread.currentThread().interrupt();
            }
            return "ready";
        }

        static {
            VALUE = createValue();
        }
    }

    private static final class SynchronousHolder {
        private static final String VALUE = createSynchronousValue();

        private static String createSynchronousValue() {
            synchronousCalls++;
            return "sync";
        }

        static int touch() {
            return synchronousCalls;
        }
    }

    private static int longComputation() {
        int value = 0;
        for (int index = 0; index < 12_000; index++) {
            value = (value * 31 + index) & 0x7fffffff;
        }
        return value == 0 ? 39 : 41;
    }

    public int flow() {
        String value = ValueHolder.VALUE;
        return "ready".equals(value) ? 107 : -1;
    }

    public int synchronousInitializationIsNotReplayed() {
        return "sync".equals(SynchronousHolder.VALUE) ?
            SynchronousHolder.touch() : -1;
    }

    public int yieldPreservesCallResult() {
        return longComputation() + 1;
    }
}
