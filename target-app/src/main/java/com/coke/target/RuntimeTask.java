package com.coke.target;

import android.widget.TextView;

/**
 * Runnable executed by the guest thread scheduler.
 */
public final class RuntimeTask implements Runnable {
    private final TextView target;

    public RuntimeTask(TextView target) {
        this.target = target;
    }

    @Override
    public void run() {
        Thread.yield();
        pauseInNestedGuestFrame();
        Thread.yield();
        target.setText(R.string.thread_complete);
    }

    private void pauseInNestedGuestFrame() {
        try {
            Thread.sleep(5L);
        } catch (InterruptedException ignored) {
            // The fixture only exercises cooperative suspension.
        }
    }
}
