package com.coke.target;

public final class RuntimeSuspendingEntryFixture {
    private static final String VALUE;

    static {
        try {
            Thread.sleep(1);
        } catch (InterruptedException failure) {
            Thread.currentThread().interrupt();
        }
        VALUE = "entry-ready";
    }

    public int flow() {
        return "entry-ready".equals(VALUE) ? 108 : -1;
    }
}
