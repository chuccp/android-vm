package twtest;

import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;

/**
 * LinkedBlockingQueue.take/put 端到端探针（Object.wait/notifyAll 承接；
 * docs 04 章 14.8）。覆盖：take 阻塞到 put 唤醒、有界队列满 put 阻塞到 take
 * 放行（FIFO 顺序保持）、poll(timeout)/offer 超时与非阻塞语义。
 */
public class BlockingQueueProbe {

    static volatile String consumerGot;

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        // 1) take 阻塞 -> put 唤醒
        try {
            final LinkedBlockingQueue<String> q = new LinkedBlockingQueue<String>(2);
            consumerGot = null;
            Thread consumer = new Thread(new Runnable() {
                public void run() {
                    try {
                        consumerGot = q.take();
                    } catch (InterruptedException e) {
                    }
                }
            }, "lbq-consumer");
            consumer.setDaemon(true);
            consumer.start();
            Thread.sleep(120);  // consumer 已挂在 take()
            sb.append(check(consumerGot == null, "lbq-take-blocked"));
            q.put("hello");
            consumer.join(3000);
            sb.append(check("hello".equals(consumerGot), "lbq-put-wakes-take"));
        } catch (Throwable e) {
            sb.append("lbq-basic: FAIL ").append(e).append('\n');
        }

        // 2) 有界队列（容量 1）满 -> put 阻塞 -> take 放行；FIFO 顺序保持
        try {
            final LinkedBlockingQueue<Integer> q2 = new LinkedBlockingQueue<Integer>(1);
            final int[] got = new int[3];
            q2.put(Integer.valueOf(1));  // 满
            Thread consumer2 = new Thread(new Runnable() {
                public void run() {
                    try {
                        got[0] = q2.take().intValue();
                        got[1] = q2.take().intValue();
                        got[2] = q2.take().intValue();
                    } catch (InterruptedException e) {
                    }
                }
            }, "lbq-consumer2");
            consumer2.setDaemon(true);
            consumer2.start();
            Thread.sleep(80);
            q2.put(Integer.valueOf(2));  // 队满阻塞 -> consumer2 取走 1 后放行
            q2.put(Integer.valueOf(3));
            consumer2.join(5000);
            sb.append(check(got[0] == 1 && got[1] == 2 && got[2] == 3, "lbq-bounded-order"));
        } catch (Throwable e) {
            sb.append("lbq-bounded: FAIL ").append(e).append('\n');
        }

        // 3) offer/poll 超时与非阻塞语义
        try {
            LinkedBlockingQueue<String> q3 = new LinkedBlockingQueue<String>(1);
            long t0 = System.currentTimeMillis();
            String v = q3.poll(150, TimeUnit.MILLISECONDS);
            long dt = System.currentTimeMillis() - t0;
            sb.append(check(v == null && dt >= 100, "lbq-poll-timeout"));
            sb.append(check(q3.offer("x"), "lbq-offer"));
            sb.append(check("x".equals(q3.poll()), "lbq-poll"));
            sb.append(check(q3.poll() == null, "lbq-poll-empty"));
        } catch (Throwable e) {
            sb.append("lbq-offer-poll: FAIL ").append(e).append('\n');
        }
        return sb.toString();
    }
}
