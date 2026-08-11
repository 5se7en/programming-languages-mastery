import java.time.Duration;
import java.util.List;
import java.util.concurrent.*;
import java.util.stream.IntStream;

public class Main {

    static final int IO_DELAY = 50;
    static final int TASKS = 20;

    static int blockingIo(int n) {
        try { Thread.sleep(IO_DELAY); } catch (InterruptedException ignored) {}
        return n;
    }

    /** 用调度器模拟「异步 I/O」：不占用线程等待，到点了才提交结果。 */
    static CompletableFuture<Integer> asyncIo(int n, ScheduledExecutorService scheduler) {
        CompletableFuture<Integer> f = new CompletableFuture<>();
        scheduler.schedule(() -> f.complete(n), IO_DELAY, TimeUnit.MILLISECONDS);
        return f;
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① 钥匙实验：三种做法 ==");
        long t0 = System.nanoTime();
        for (int i = 0; i < TASKS; i++) blockingIo(i);
        double serialMs = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        Thread[] ts = new Thread[TASKS];
        for (int i = 0; i < TASKS; i++) { int k = i; ts[i] = new Thread(() -> blockingIo(k)); ts[i].start(); }
        for (Thread t : ts) t.join();
        double threadMs = (System.nanoTime() - t0) / 1e6;

        ScheduledExecutorService scheduler = Executors.newScheduledThreadPool(1);
        t0 = System.nanoTime();
        List<CompletableFuture<Integer>> futures =
                IntStream.range(0, TASKS).mapToObj(i -> asyncIo(i, scheduler)).toList();
        CompletableFuture.allOf(futures.toArray(new CompletableFuture[0])).join();
        double asyncMs = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  串行 %d 个 I/O:        %7.0f ms%n", TASKS, serialMs);
        System.out.printf("  %d 个线程并发:         %7.0f ms（加速比 %.1fx）%n", TASKS, threadMs, serialMs / threadMs);
        System.out.printf("  CompletableFuture:   %7.0f ms（加速比 %.1fx，仅 1 个调度线程）%n",
                asyncMs, serialMs / asyncMs);

        System.out.println("\n== ② 组合：Java 的异步是「拼装管道」而非 await ==");
        CompletableFuture<String> pipeline = CompletableFuture
                .supplyAsync(() -> { blockingIo(0); return "第一步"; })
                .thenApply(s -> s + " → 第二步")                    // 同步变换
                .thenCompose(s -> CompletableFuture.supplyAsync(() -> s + " → 第三步"))  // 串联异步
                .exceptionally(e -> "出错了: " + e.getMessage());   // 错误处理
        System.out.println("  " + pipeline.get());
        System.out.println("  （没有 async/await 关键字，全靠方法链——可读性是 Java 异步的痛点）");

        System.out.println("\n== ③ 组合多个异步结果 ==");
        CompletableFuture<Integer> a = asyncIo(10, scheduler);
        CompletableFuture<Integer> b = asyncIo(20, scheduler);
        System.out.println("  thenCombine 两个结果: " + a.thenCombine(b, Integer::sum).get());
        CompletableFuture<Object> fastest = CompletableFuture.anyOf(
                asyncIo(1, scheduler), CompletableFuture.completedFuture(99));
        System.out.println("  anyOf 取最快的: " + fastest.get() + "（相当于 Promise.race）");

        System.out.println("\n== ④ 超时与取消 ==");
        try {
            asyncIo(1, scheduler).orTimeout(10, TimeUnit.MILLISECONDS).get();
        } catch (ExecutionException e) {
            System.out.println("  orTimeout(10ms) 让一个 50ms 的任务超时: "
                    + e.getCause().getClass().getSimpleName() + " ✅");
        }

        System.out.println("\n== ⑤ Java 的路线选择 ==");
        System.out.println("  CompletableFuture: 回调式组合，无 await 语法（本节实测）");
        System.out.println("  虚拟线程（Java 21+）: 换一条路——让阻塞代码本身变廉价");
        System.out.println("    → 写同步风格的阻塞代码，运行时自动把「阻塞」变成「让出」");
        System.out.println("    → 没有 async 传染性问题（第 44 章展开）");
        System.out.printf("  本机 Java 版本 = %s%n", System.getProperty("java.version"));

        scheduler.shutdown();
        scheduler.awaitTermination(2, TimeUnit.SECONDS);
    }
}
