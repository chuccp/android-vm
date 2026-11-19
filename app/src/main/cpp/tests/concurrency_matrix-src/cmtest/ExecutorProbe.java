package cmtest;

import java.util.concurrent.Callable;
import java.util.concurrent.CancellationException;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CompletionException;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * 执行器/Future/CompletableFuture 面（覆盖面参考启发自 v1
 * RuntimeModernConcurrency.java futureFlow/completableFutureFlow 与
 * NativeVmModernConcurrencyTest.kt 断言面）。判据（Android j.u.c 约定）：
 *   - submit/get 取值；get(timeout) 超时 TimeoutException；cancel(true)；
 *     失败任务 get -> ExecutionException(cause)；
 *   - shutdown + awaitTermination；
 *   - scheduleAtFixedRate 周期执行 + cancel；
 *   - CompletableFuture thenApply/thenCompose/thenCombine/exceptionally/handle/
 *     whenComplete/allOf/anyOf/cancel/completeOnTimeout/orTimeout。
 */
public class ExecutorProbe {

    private static String check(boolean cond, String name) {
        return name + (cond ? ": OK\n" : ": FAIL\n");
    }

    private static void sleep(long millis) {
        try {
            Thread.sleep(millis);
        } catch (InterruptedException ignored) {
            Thread.currentThread().interrupt();
        }
    }

    public static String run() {
        StringBuilder sb = new StringBuilder();

        ExecutorService executor = Executors.newFixedThreadPool(2);
        try {
            // 1) submit/get 取值
            Future<Integer> future = executor.submit(new Callable<Integer>() {
                public Integer call() {
                    sleep(10L);
                    return 42;
                }
            });
            int got = -1;
            try {
                got = future.get().intValue();
            } catch (Exception ignored) {
            }
            sb.append(check(got == 42, "future-get-value"));

            // 2) get(timeout) 超时 + cancel(true)
            Future<Integer> timed = executor.submit(new Callable<Integer>() {
                public Integer call() {
                    sleep(200L);
                    return 7;
                }
            });
            boolean timedOut = true;
            try {
                timed.get(5L, TimeUnit.MILLISECONDS);
                timedOut = false;
            } catch (TimeoutException expected) {
            } catch (Exception ignored) {
            }
            sb.append(check(timedOut, "future-get-timeout"));
            sb.append(check(timed.cancel(true), "future-cancel"));
            boolean cancelledSeen = false;
            try {
                timed.get();
            } catch (CancellationException expected) {
                cancelledSeen = true;
            } catch (Exception ignored) {
            }
            sb.append(check(cancelledSeen, "future-cancelled-get"));

            // 3) 失败任务 -> ExecutionException(cause)
            Future<Integer> failing = executor.submit(new Callable<Integer>() {
                public Integer call() {
                    throw new IllegalArgumentException("expected");
                }
            });
            Throwable cause = null;
            try {
                failing.get();
            } catch (ExecutionException expected) {
                cause = expected.getCause();
            } catch (Exception ignored) {
            }
            sb.append(check(cause instanceof IllegalArgumentException,
                    "future-execution-exception"));

            // 4) shutdown + awaitTermination
            executor.shutdown();
            boolean terminated = false;
            try {
                terminated = executor.awaitTermination(200L, TimeUnit.MILLISECONDS);
            } catch (InterruptedException ignored) {
                Thread.currentThread().interrupt();
            }
            sb.append(check(terminated, "executor-shutdown-terminate"));
        } finally {
            executor.shutdownNow();
        }

        // 5) scheduleAtFixedRate 周期执行 + cancel
        ScheduledExecutorService scheduled = Executors.newScheduledThreadPool(1, new ThreadFactory() {
            public Thread newThread(Runnable task) {
                Thread thread = new Thread(task);
                thread.setName("cm-scheduled");
                return thread;
            }
        });
        AtomicInteger runs = new AtomicInteger();
        ScheduledFuture<?> periodic = scheduled.scheduleAtFixedRate(new Runnable() {
            public void run() {
                runs.incrementAndGet();
            }
        }, 0L, 5L, TimeUnit.MILLISECONDS);
        sleep(40L);
        periodic.cancel(false);
        int observed = runs.get();
        scheduled.shutdownNow();
        sb.append(check(observed >= 2, "scheduled-periodic-runs"));

        // 6) CompletableFuture 同步链
        int chained = CompletableFuture.completedFuture(20)
                .thenApply(new java.util.function.Function<Integer, Integer>() {
                    public Integer apply(Integer value) {
                        return value + 1;
                    }
                })
                .thenApply(new java.util.function.Function<Integer, Integer>() {
                    public Integer apply(Integer value) {
                        return value + 1;
                    }
                })
                .join().intValue();
        sb.append(check(chained == 22, "cf-thenApply-chain"));

        int composed = CompletableFuture.completedFuture(20)
                .thenCompose(new java.util.function.Function<Integer, CompletableFuture<Integer>>() {
                    public CompletableFuture<Integer> apply(Integer value) {
                        return CompletableFuture.completedFuture(value * 2);
                    }
                })
                .join().intValue();
        sb.append(check(composed == 40, "cf-thenCompose"));

        int combined = CompletableFuture.completedFuture(5)
                .thenCombine(CompletableFuture.completedFuture(7),
                        new java.util.function.BiFunction<Integer, Integer, Integer>() {
                            public Integer apply(Integer left, Integer right) {
                                return left + right;
                            }
                        })
                .join().intValue();
        sb.append(check(combined == 12, "cf-thenCombine"));

        // 7) 异常恢复面
        int recovered = 0;
        try {
            recovered = CompletableFuture
                    .<Integer>failedFuture(new IllegalArgumentException("expected"))
                    .exceptionally(new java.util.function.Function<Throwable, Integer>() {
                        public Integer apply(Throwable error) {
                            return 9;
                        }
                    })
                    .join().intValue();
        } catch (Throwable ignored) {
        }
        sb.append(check(recovered == 9, "cf-exceptionally"));

        int handled = CompletableFuture.completedFuture(4)
                .handle(new java.util.function.BiFunction<Integer, Throwable, Integer>() {
                    public Integer apply(Integer value, Throwable error) {
                        return error == null ? value + 1 : -1;
                    }
                })
                .join().intValue();
        sb.append(check(handled == 5, "cf-handle"));

        final AtomicInteger completedStage = new AtomicInteger();
        CompletableFuture.completedFuture(1)
                .whenComplete(new java.util.function.BiConsumer<Integer, Throwable>() {
                    public void accept(Integer value, Throwable error) {
                        completedStage.set(value.intValue());
                    }
                })
                .join();
        sb.append(check(completedStage.get() == 1, "cf-whenComplete"));

        // 8) supplyAsync 异步链 + allOf/anyOf
        int async = CompletableFuture.supplyAsync(new java.util.function.Supplier<Integer>() {
            public Integer get() {
                sleep(5L);
                return 30;
            }
        }).thenApply(new java.util.function.Function<Integer, Integer>() {
            public Integer apply(Integer value) {
                return value * 2;
            }
        }).join().intValue();
        sb.append(check(async == 60, "cf-supplyAsync"));

        CompletableFuture<Void> all = CompletableFuture.allOf(
                CompletableFuture.completedFuture(1),
                CompletableFuture.completedFuture(2),
                CompletableFuture.supplyAsync(new java.util.function.Supplier<Integer>() {
                    public Integer get() {
                        return 3;
                    }
                }));
        all.join();
        sb.append(check(all.isDone() && !all.isCompletedExceptionally(), "cf-allOf"));

        int first = CompletableFuture.anyOf(
                CompletableFuture.completedFuture(41),
                CompletableFuture.completedFuture(42))
                .thenApply(new java.util.function.Function<Object, Integer>() {
                    public Integer apply(Object value) {
                        return (Integer) value;
                    }
                })
                .join().intValue();
        sb.append(check(first == 41 || first == 42, "cf-anyOf"));

        // 9) 超时/兜底/取消面
        boolean cfTimedOut = true;
        try {
            new CompletableFuture<Integer>().orTimeout(5L, TimeUnit.MILLISECONDS).join();
        } catch (CompletionException expected) {
            cfTimedOut = expected.getCause() instanceof TimeoutException;
        } catch (Throwable ignored) {
        }
        sb.append(check(cfTimedOut, "cf-orTimeout"));

        int fallback = new CompletableFuture<Integer>()
                .completeOnTimeout(77, 5L, TimeUnit.MILLISECONDS)
                .join().intValue();
        sb.append(check(fallback == 77, "cf-completeOnTimeout"));

        CompletableFuture<Integer> cancelled = new CompletableFuture<Integer>();
        sb.append(check(cancelled.cancel(false) && cancelled.isCancelled(),
                "cf-cancel"));

        return sb.toString();
    }
}
