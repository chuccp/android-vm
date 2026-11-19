package java.util.concurrent;

public class CountDownLatch {
    private int count;

    public CountDownLatch(int count) {
        if (count < 0) {
            throw new IllegalArgumentException("count < 0");
        }
        this.count = count;
    }

    public void await() throws InterruptedException {
        if (Thread.interrupted()) {
            throw new InterruptedException();
        }
        synchronized (this) {
            while (count > 0) {
                wait();
            }
        }
    }

    public boolean await(long timeout, TimeUnit unit)
            throws InterruptedException {
        if (Thread.interrupted()) {
            throw new InterruptedException();
        }
        long remaining = unit.toNanos(timeout);
        long t0 = System.nanoTime();
        if (remaining <= 0L) {
            synchronized (this) {
                return count == 0;
            }
        }
        long deadline = t0 + remaining;
        boolean result;
        synchronized (this) {
            while (count > 0) {
                remaining = deadline - System.nanoTime();
                if (remaining <= 0L) {
                    result = false;
                    diagAwait(timeout, unit, t0, result);
                    return result;
                }
                long millis = remaining / 1_000_000L;
                int nanos = (int) (remaining % 1_000_000L);
                wait(millis, nanos);
            }
            result = true;
        }
        diagAwait(timeout, unit, t0, result);
        return result;
    }

    /** TEMP DIAG: await 超时路径/耗时（cloudid 隐私等待卡死归因；事后移除）。 */
    private static void diagAwait(long timeout, TimeUnit unit, long t0, boolean result) {
        try {
            long ms = (System.nanoTime() - t0) / 1_000_000L;
            StringBuilder frames = new StringBuilder();
            StackTraceElement[] st = new Exception().getStackTrace();
            for (int i = 0; i < st.length && i < 10; i++) {
                frames.append("\n  at ").append(st[i]);
            }
            android.util.Log.i("DexVmCdl", "await " + timeout + " " + unit + " -> " + result
                    + " after " + ms + "ms thread=" + Thread.currentThread().getName()
                    + frames);
        } catch (Throwable ignored) {
        }
    }

    public void countDown() {
        synchronized (this) {
            if (count > 0 && --count == 0) {
                notifyAll();
            }
        }
    }

    public long getCount() {
        synchronized (this) {
            return count;
        }
    }

    @Override
    public String toString() {
        return super.toString() + "[Count = " + getCount() + "]";
    }
}
