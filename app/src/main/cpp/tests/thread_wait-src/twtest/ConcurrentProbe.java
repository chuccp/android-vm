package twtest;

/**
 * 多 GuestTask 并发推进探针（动态线程数：8 线程各跑循环；M4.4 协程调度）。
 * 判据：8 线程全部 join 完成 + 互相观察到彼此推进（并发推进而非串行执行）。
 */
public class ConcurrentProbe {

    static final int N = 8;
    static final int[] counters = new int[N];
    static final boolean[] sawOthers = new boolean[N];

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();
        Thread[] ts = new Thread[N];
        for (int i = 0; i < N; i++) {
            final int id = i;
            counters[i] = 0;
            sawOthers[i] = false;
            ts[i] = new Thread(new Runnable() {
                public void run() {
                    // 循环推进：推进自身计数并观察其他线程是否也在推进
                    //（若调度为串行，本线程永远看不到他人计数 -> 判据失败）
                    boolean seenAll = false;
                    int spins = 0;
                    while (spins < 300000) {
                        counters[id]++;
                        seenAll = true;
                        for (int j = 0; j < N; j++) {
                            if (j != id && counters[j] == 0) {
                                seenAll = false;
                                break;
                            }
                        }
                        if (seenAll) {
                            break;
                        }
                        Thread.yield();
                        spins++;
                    }
                    sawOthers[id] = seenAll;
                    for (int k = 0; k < 50; k++) {
                        counters[id]++;
                        Thread.yield();
                    }
                }
            }, "loop-" + i);
            ts[i].setDaemon(true);
        }
        try {
            long t0 = System.currentTimeMillis();
            for (int i = 0; i < N; i++) {
                ts[i].start();
            }
            for (int i = 0; i < N; i++) {
                ts[i].join(15000);
            }
            long dt = System.currentTimeMillis() - t0;
            sb.append("concurrent-elapse=").append(dt).append("ms\n");
            boolean allDone = true;
            boolean allSaw = true;
            boolean allCounted = true;
            for (int i = 0; i < N; i++) {
                if (ts[i].isAlive()) {
                    allDone = false;
                }
                if (!sawOthers[i]) {
                    allSaw = false;
                }
                if (counters[i] < 50) {
                    allCounted = false;
                }
            }
            sb.append(check(allDone, "concurrent-join"));
            sb.append(check(allSaw, "concurrent-mutual-progress"));
            sb.append(check(allCounted, "concurrent-loops"));
        } catch (Throwable e) {
            sb.append("concurrent: FAIL ").append(e).append('\n');
        }
        return sb.toString();
    }
}
