import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ForkJoinPool;
import java.util.concurrent.Future;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.RecursiveTask;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ThreadFactory;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicInteger;

public class Main {

    static void sleepMs(long ms) {
        try { Thread.sleep(ms); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
    }

    /** 守护线程工厂：实验结束后绝不阻碍 JVM 退出 */
    static final ThreadFactory DAEMON = r -> { Thread t = new Thread(r); t.setDaemon(true); return t; };

    /** ⑥ 分治任务：ForkJoinPool 的主场 */
    static class SumTask extends RecursiveTask<Long> {
        private final long lo, hi;
        SumTask(long lo, long hi) { this.lo = lo; this.hi = hi; }
        @Override protected Long compute() {
            if (hi - lo <= 50_000) {
                long s = 0;
                for (long i = lo; i < hi; i++) s += i;
                return s;
            }
            long mid = (lo + hi) >>> 1;
            SumTask left = new SumTask(lo, mid);
            left.fork();                       // 交给池，可能被别的线程「窃取」
            long right = new SumTask(mid, hi).compute();
            return right + left.join();
        }
    }

    public static void main(String[] args) throws Exception {
        int cores = Runtime.getRuntime().availableProcessors();

        System.out.println("== ① 每任务一线程 vs 复用线程池 ==");
        final int N = 5_000;
        long t0 = System.nanoTime();
        List<Thread> raw = new ArrayList<>(N);
        for (int i = 0; i < N; i++) { Thread t = new Thread(() -> {}); t.start(); raw.add(t); }
        for (Thread t : raw) t.join();
        double msRaw = (System.nanoTime() - t0) / 1e6;

        ExecutorService pool = Executors.newFixedThreadPool(8);
        t0 = System.nanoTime();
        List<Future<?>> fs = new ArrayList<>(N);
        for (int i = 0; i < N; i++) fs.add(pool.submit(() -> {}));
        for (Future<?> f : fs) f.get();
        double msPool = (System.nanoTime() - t0) / 1e6;
        pool.shutdown();
        System.out.printf("  %d 个空任务，每任务新建线程: %.0f ms（%.1f μs/个）%n", N, msRaw, msRaw * 1000 / N);
        System.out.printf("  %d 个空任务，8 线程池复用    : %.0f ms（%.2f μs/个）%n", N, msPool, msPool * 1000 / N);
        System.out.printf("  → 池化快 %.1fx —— 省下的是创建 + 1MB 栈分配 + 销毁（第 40 章实测）%n", msRaw / msPool);

        System.out.println("\n== ② ThreadPoolExecutor 的扩容规则（最反直觉的一段）==");
        AtomicInteger ran = new AtomicInteger(), rejected = new AtomicInteger();
        ThreadPoolExecutor tpe = new ThreadPoolExecutor(
                2, 4, 1, TimeUnit.SECONDS,          // core=2, max=4
                new ArrayBlockingQueue<>(2),        // 有界队列，容量 2
                DAEMON, new ThreadPoolExecutor.AbortPolicy());
        for (int i = 0; i < 10; i++) {
            try {
                tpe.execute(() -> { ran.incrementAndGet(); sleepMs(120); });
            } catch (RejectedExecutionException e) {
                rejected.incrementAndGet();
            }
        }
        System.out.printf("  配置: core=2, 队列容量=2, max=4，提交 10 个任务%n");
        System.out.printf("  被接受 %d 个，被拒绝 %d 个（池的容量 = max + 队列 = 4 + 2 = 6）%n",
                10 - rejected.get(), rejected.get());
        System.out.println("  扩容顺序: ① 先开到 core 条 → ② 再【填队列】→ ③ 队列满了才扩到 max → ④ 还满就拒绝");
        System.out.println("  ↑ 注意第 ② 步：队列没满【绝不】开新线程——所以「无界队列 + 大 max」时 max 永远用不上");
        tpe.shutdown();
        tpe.awaitTermination(2, TimeUnit.SECONDS);
        System.out.printf("  实际执行了 %d 个%n", ran.get());

        System.out.println("\n== ③ 四种拒绝策略的真实后果 ==");
        String[] names = {"AbortPolicy（默认）", "CallerRunsPolicy", "DiscardPolicy", "DiscardOldestPolicy"};
        ThreadPoolExecutor.AbortPolicy p0 = new ThreadPoolExecutor.AbortPolicy();
        ThreadPoolExecutor.CallerRunsPolicy p1 = new ThreadPoolExecutor.CallerRunsPolicy();
        ThreadPoolExecutor.DiscardPolicy p2 = new ThreadPoolExecutor.DiscardPolicy();
        ThreadPoolExecutor.DiscardOldestPolicy p3 = new ThreadPoolExecutor.DiscardOldestPolicy();
        java.util.concurrent.RejectedExecutionHandler[] policies = {p0, p1, p2, p3};

        for (int k = 0; k < 4; k++) {
            AtomicInteger thrown = new AtomicInteger();
            StringBuilder callerRan = new StringBuilder();
            List<Integer> survived = java.util.Collections.synchronizedList(new ArrayList<>());
            ThreadPoolExecutor ex = new ThreadPoolExecutor(
                    1, 1, 1, TimeUnit.SECONDS, new ArrayBlockingQueue<>(1), DAEMON, policies[k]);
            for (int i = 0; i < 6; i++) {
                final int id = i;
                try {
                    ex.execute(() -> {
                        // CallerRunsPolicy 会让【提交者线程】亲自执行——这里就能抓到 main
                        if ("main".equals(Thread.currentThread().getName())) callerRan.append(id).append(' ');
                        sleepMs(60);
                        survived.add(id);
                    });
                } catch (RejectedExecutionException e) {
                    thrown.incrementAndGet();
                }
            }
            ex.shutdown();
            ex.awaitTermination(3, TimeUnit.SECONDS);
            List<Integer> executed = new ArrayList<>(survived);
            java.util.Collections.sort(executed);
            System.out.printf("  %-22s 执行 %d 个（任务号 %s），抛异常 %d 次%s%n",
                    names[k], executed.size(), executed.toString(), thrown.get(),
                    callerRan.length() > 0 ? "，主线程亲自跑了 " + callerRan.toString().trim() : "");
        }
        System.out.println("  Abort       : 抛异常，调用方立刻知道（唯一不丢数据的默认选择）");
        System.out.println("  CallerRuns  : 提交者自己执行 → 天然反压（生产者被拖慢，队列不再增长）");
        System.out.println("  Discard     : 静默丢弃 ← 最危险，任务凭空消失且无任何日志");
        System.out.println("  DiscardOldest: 丢队列里最老的 → 破坏 FIFO，老任务饿死");

        System.out.println("\n== ④ 无界队列 + 固定线程池 = OOM 的经典配方 ==");
        ThreadPoolExecutor fixed = (ThreadPoolExecutor) Executors.newFixedThreadPool(2, DAEMON);
        for (int i = 0; i < 20_000; i++) fixed.execute(() -> sleepMs(50));
        System.out.printf("  Executors.newFixedThreadPool(2) 的队列类型: %s%n",
                fixed.getQueue().getClass().getSimpleName());
        System.out.printf("  提交 20000 个任务后，队列里积压: %d 个（队列剩余容量 %d = Integer.MAX_VALUE 级）%n",
                fixed.getQueue().size(), fixed.getQueue().remainingCapacity());
        System.out.println("  → LinkedBlockingQueue 默认无界，任务对象一直堆在堆上直到 OOM（第 36 章：可达即不回收）");
        System.out.println("  → 阿里巴巴 Java 规范禁用 Executors 工厂方法，正是因为这一条");
        fixed.shutdownNow();

        System.out.println("\n== ⑤ 用固定线程池跑分治任务会【死锁】（实测，非推测）==");
        ExecutorService small = Executors.newFixedThreadPool(2, DAEMON);
        Callable<Long> recursive = new Callable<Long>() {
            @Override public Long call() throws Exception {
                Future<Long> child = small.submit(() -> 1L);   // 父任务占着线程，等子任务
                return 1L + child.get();                       // ← 子任务排在队列里，永远等不到线程
            }
        };
        List<Future<Long>> parents = new ArrayList<>();
        for (int i = 0; i < 2; i++) parents.add(small.submit(recursive));
        boolean deadlocked = false;
        try {
            for (Future<Long> f : parents) f.get(600, TimeUnit.MILLISECONDS);
        } catch (TimeoutException e) {
            deadlocked = true;
        }
        System.out.printf("  2 条线程都被「父任务」占据，子任务排在队列里 → 600ms 后仍未完成: %s%n", deadlocked);
        System.out.println("  ↑ 这就是「线程饥饿死锁」：不是锁的问题，是【线程这个资源】被自己耗尽了");
        System.out.println("  → 解法 A: 分治任务用 ForkJoinPool（join 时会去帮忙执行别的任务）");
        System.out.println("  → 解法 B: 父任务与子任务用不同的池（舱壁隔离）");
        small.shutdownNow();

        System.out.println("\n== ⑥ ForkJoinPool 的工作窃取 ==");
        final long RANGE = 400_000_000L;
        ForkJoinPool fjp = new ForkJoinPool();
        // 预热：让 JIT 先把两条路径都编译好，否则先跑的那个会白白背上编译成本
        fjp.invoke(new SumTask(0, RANGE / 10));
        long warm = 0;
        for (long i = 0; i < RANGE / 10; i++) warm += i;

        t0 = System.nanoTime();
        long sum = fjp.invoke(new SumTask(0, RANGE));
        double msFjp = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        long serial = 0;
        for (long i = 0; i < RANGE; i++) serial += i;
        double msSerial = (System.nanoTime() - t0) / 1e6;
        if (warm < 0) System.out.print("");   // 防止 JIT 把预热循环整个消掉

        System.out.printf("  并行度（默认 = 核心数-1）: %d%n", fjp.getParallelism());
        System.out.printf("  分治求和 0..%d: ForkJoinPool %.0f ms，单线程 %.0f ms（加速 %.2fx）%n",
                RANGE, msFjp, msSerial, msSerial / msFjp);
        System.out.printf("  结果一致: %s，窃取次数: %d%n", sum == serial, fjp.getStealCount());
        System.out.println("  工作窃取: 每条线程有自己的双端队列，自己从头取，【空闲时从别人队尾偷】");
        System.out.println("  → 队尾偷的是最大的任务（分治里越早 fork 的越大），一次偷够本，减少竞争（第 41 章）");
        fjp.shutdown();

        System.out.println("\n== ⑦ CPU 密集任务的池大小曲线 ==");
        final long WORK = 400_000_000L;
        for (int size : new int[]{1, 2, 4, 8, cores, cores * 2, cores * 4}) {
            ExecutorService p = Executors.newFixedThreadPool(size, DAEMON);
            t0 = System.nanoTime();
            List<Future<Long>> res = new ArrayList<>();
            for (int i = 0; i < 40; i++) {
                res.add(p.submit(() -> { long s = 0; for (long j = 0; j < WORK / 40; j++) s += j; return s; }));
            }
            for (Future<Long> f : res) f.get();
            double ms = (System.nanoTime() - t0) / 1e6;
            p.shutdown();
            System.out.printf("  池大小 %2d: %6.1f ms%s%n", size, ms,
                    size == cores ? "   ← 核心数" : (size > cores ? "   （超出核心数，收益归零）" : ""));
        }
        System.out.printf("  → CPU 密集公式: 线程数 ≈ 核心数（本机 %d）；再多只增加上下文切换%n", cores);
        System.out.printf("  → I/O 密集公式: 线程数 ≈ 核心数 × (1 + 等待/计算)，例 %d × 10 = %d 条%n",
                cores, cores * 10);
    }
}
