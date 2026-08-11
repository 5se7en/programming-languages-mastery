import java.util.concurrent.atomic.AtomicInteger;

public class Main {

    static int sharedCounter = 0;                          // ⚠️ 非同步的共享变量
    static final AtomicInteger atomicCounter = new AtomicInteger(0);
    static int globalMarker = 0;

    static void raceWorker(int times) {
        for (int i = 0; i < times; i++) sharedCounter++;    // 读-改-写三步
    }

    static void atomicWorker(int times) {
        for (int i = 0; i < times; i++) atomicCounter.incrementAndGet();
    }

    public static void main(String[] args) throws Exception {
        final int N = 1_000_000;

        System.out.println("== ① 线程的身份与共享 ==");
        System.out.println("  当前线程: " + Thread.currentThread().getName()
                + "，可用处理器 = " + Runtime.getRuntime().availableProcessors());
        Thread probe = new Thread(() -> {
            int localVar = 42;                              // 栈上，各线程独立
            System.out.println("  子线程看到的 globalMarker = " + globalMarker
                    + "（共享），自己的局部变量 = " + localVar + "（独立）");
        }, "probe");
        probe.start();
        probe.join();

        System.out.println("\n== ② 钥匙实验：数据竞争 ==");
        for (int run = 1; run <= 3; run++) {
            sharedCounter = 0;
            Thread a = new Thread(() -> raceWorker(N));
            Thread b = new Thread(() -> raceWorker(N));
            a.start(); b.start(); a.join(); b.join();
            System.out.printf("  第 %d 次运行: 期望 %d，实际 %d   （丢了 %d 次）%n",
                    run, 2 * N, sharedCounter, 2 * N - sharedCounter);
        }
        System.out.println("  ↑ 每次都不一样——Java 有真并行，竞争比 Python 更凶");

        System.out.println("\n== ③ AtomicInteger 修复 ==");
        for (int run = 1; run <= 3; run++) {
            atomicCounter.set(0);
            Thread a = new Thread(() -> atomicWorker(N));
            Thread b = new Thread(() -> atomicWorker(N));
            a.start(); b.start(); a.join(); b.join();
            System.out.printf("  第 %d 次运行: 期望 %d，实际 %d   ✅%n",
                    run, 2 * N, atomicCounter.get());
        }

        System.out.println("\n== ④ 真并行：CPU 密集任务的加速比 ==");
        final int M = 40_000_000;
        long t0 = System.nanoTime();
        for (int i = 0; i < 4; i++) cpuTask(M);
        double serial = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        Thread[] ts = new Thread[4];
        for (int i = 0; i < 4; i++) { ts[i] = new Thread(() -> cpuTask(M)); ts[i].start(); }
        for (Thread t : ts) t.join();
        double parallel = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  串行 4 个任务: %7.0f ms%n", serial);
        System.out.printf("  4 线程并行:    %7.0f ms%n", parallel);
        System.out.printf("  加速比 = %.2fx   <- 真并行（对比 Python 线程的 1x）%n", serial / parallel);

        System.out.println("\n== ⑤ 平台线程的重量：为什么会有虚拟线程 ==");
        int COUNT = 2_000;
        long vt0 = System.nanoTime();
        Thread[] many = new Thread[COUNT];
        for (int i = 0; i < COUNT; i++) {
            many[i] = new Thread(() -> { try { Thread.sleep(1); } catch (Exception ignored) {} });
            many[i].start();
        }
        for (Thread t : many) t.join();
        double ms = (System.nanoTime() - vt0) / 1e6;
        System.out.printf("  创建并运行 %d 个平台线程: %.0f ms（约 %.0f μs/个）%n",
                COUNT, ms, ms * 1000 / COUNT);
        System.out.println("  每个平台线程默认保留约 1 MB 栈空间（-Xss，第 31 章实测过）");
        System.out.printf("  → %d 个线程就要预留约 %d GB 虚拟地址空间%n", 1_000_000, 1_000);
        System.out.println("  这就是 Java 21 引入虚拟线程的理由：把栈搬到堆上（第 44 章）");
    }

    static long cpuTask(int n) {
        long total = 0;
        for (int i = 0; i < n; i++) total += (long) i * i;
        return total;
    }
}
