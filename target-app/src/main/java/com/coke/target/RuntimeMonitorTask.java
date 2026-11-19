package com.coke.target;


/**
 * Exercises cooperative monitor ownership and Object.wait/notifyAll.
 */
public final class RuntimeMonitorTask implements Runnable {
    private static final Object LOCK = new Object();
    private static boolean ready;
    private static int observed;

    private final RuntimePanel target;
    private final boolean notifier;

    public RuntimeMonitorTask(RuntimePanel target, boolean notifier) {
        this.target = target;
        this.notifier = notifier;
    }

    @Override
    public void run() {
        if (notifier) {
            notifyWaiter();
        } else {
            waitForSignal();
        }
    }

    private void waitForSignal() {
        synchronized (LOCK) {
            while (!ready) {
                try {
                    LOCK.wait(2_000L);
                } catch (InterruptedException ignored) {
                    // The fixture expects cooperative wake-up before timeout.
                }
            }
            observed = 73;
            target.updateMonitorStatus("Guest monitor: " + observed);
        }
    }

    private void notifyWaiter() {
        Thread.yield();
        try {
            Thread.sleep(10L);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
        synchronized (LOCK) {
            ready = true;
            LOCK.notifyAll();
        }
    }
}
