package java.util.concurrent;

public enum TimeUnit {
    NANOSECONDS(1L),
    MICROSECONDS(1_000L),
    MILLISECONDS(1_000_000L),
    SECONDS(1_000_000_000L),
    MINUTES(60_000_000_000L),
    HOURS(3_600_000_000_000L),
    DAYS(86_400_000_000_000L);

    private final long nanos;

    TimeUnit(long nanos) {
        this.nanos = nanos;
    }

    public long toNanos(long duration) {
        return duration * nanos;
    }

    public long toMicros(long duration) {
        return duration * nanos / MICROSECONDS.nanos;
    }

    public long toMillis(long duration) {
        return duration * nanos / MILLISECONDS.nanos;
    }

    public long toSeconds(long duration) {
        return duration * nanos / SECONDS.nanos;
    }

    public long convert(long sourceDuration, TimeUnit sourceUnit) {
        if (this == sourceUnit) {
            return sourceDuration;
        }
        return sourceUnit.toNanos(sourceDuration) / nanos;
    }

    public void sleep(long timeout) throws InterruptedException {
        if (timeout <= 0) {
            return;
        }
        Thread.sleep(toMillis(timeout));
    }

    public void timedWait(Object target, long timeout)
            throws InterruptedException {
        if (timeout <= 0) {
            return;
        }
        synchronized (target) {
            long millis = toMillis(timeout);
            int nanos = (int) (toNanos(timeout) - millis * MILLISECONDS.nanos);
            target.wait(millis, nanos);
        }
    }

    public void timedJoin(Thread thread, long timeout)
            throws InterruptedException {
        if (timeout <= 0) {
            return;
        }
        thread.join(toMillis(timeout));
    }
}
