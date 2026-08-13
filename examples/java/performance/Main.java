import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/**
 * 性能优化：JVM 上的微基准为什么这么难写——JIT 预热、死代码消除、逃逸分析，三个陷阱全部实测。
 */
public class Main {

    static long compute(int n) {                     // 被测的「工作」
        long s = 0;
        for (int i = 0; i < n; i++) s += i % 7;
        return s;
    }

    /** 一个纯函数——极易被内联，因而也极易被死代码消除 */
    static int pure(int x) { return x * 31 + 7; }

    /** 把循环放进【独立方法】，才能被 C2 正常编译（写在 main 里只能走 OSR 编译） */
    static void discardLoop(int n) { for (int i = 0; i < n; i++) pure(i); }
    static long accumulateLoop(int n) {
        long s = 0;
        for (int i = 0; i < n; i++) s += pure(i);
        return s;
    }

    static double timeMs(Runnable r) {
        long t0 = System.nanoTime();
        r.run();
        return (System.nanoTime() - t0) / 1e6;
    }

    public static void main(String[] args) {
        final int N = 2_000_000;

        System.out.println("== ① JIT 预热：同一段代码，跑几轮之后才是真实速度（实测）==");
        double[] rounds = new double[8];
        for (int r = 0; r < rounds.length; r++) {
            long t0 = System.nanoTime();
            long sink = compute(N);
            rounds[r] = (System.nanoTime() - t0) / 1e6;
            if (sink == Long.MIN_VALUE) System.out.print("");   // 防止被消除
        }
        System.out.print("  同一个 compute(2000000) 连跑 8 次: ");
        for (double d : rounds) System.out.printf("%.1f ", d);
        System.out.println("ms");
        System.out.printf("  第 1 次 %.1f ms → 第 8 次 %.1f ms（快 %.1fx）%n",
                rounds[0], rounds[rounds.length - 1], rounds[0] / rounds[rounds.length - 1]);
        System.out.println("  → 第 1 次跑的是【解释执行】的字节码（第 5 章的 JVM）");
        System.out.println("  → 跑够次数后 C2 编译器把它编译成机器码（第 3 章的 JIT）");
        System.out.println("  → 教训: 在 JVM 上不预热就计时，测到的是【解释器的速度】，与生产环境无关");
        System.out.println("  → 第 52 章实测过它的后果: 不预热让并行加速比从 5.78x 掉到 1.92x");

        System.out.println("\n== ② 死代码消除：JIT 会把你的基准整个删掉（实测）==");
        final int M2 = 20_000_000;
        long warmSink = 0;
        for (int w = 0; w < 30; w++) {                            // 充分预热两条路径，让 C2 接管
            discardLoop(M2 / 10);
            warmSink += accumulateLoop(M2 / 10);
        }
        if (warmSink == Long.MIN_VALUE) System.out.print("");

        long t1 = System.nanoTime();
        discardLoop(M2);                                          // ⚠️ 返回值被丢弃
        double msDead = (System.nanoTime() - t1) / 1e6;

        t1 = System.nanoTime();
        long alive = accumulateLoop(M2);                          // 返回值被消费
        double msAlive = (System.nanoTime() - t1) / 1e6;

        System.out.printf("  调用 pure() %d 次，【丢弃】返回值: %7.1f ms%n", M2, msDead);
        System.out.printf("  调用 pure() %d 次，【累加】返回值: %7.1f ms%n", M2, msAlive);
        System.out.printf("  差距 %.0fx（累加结果 %d，证明它真的算了）%n",
                msAlive / Math.max(msDead, 0.001), alive);
        System.out.println("  → 丢弃版耗时归零: C2 判定「结果没人用」，把【整个两千万次循环删掉了】");
        System.out.println("  → 这是微基准最常见的假象: 你以为测出了「优化后快 100 倍」，");
        System.out.println("     实际上是【被优化的那份代码根本没运行】");
        System.out.println("  → 注意触发条件: 函数要【纯】且能被内联，还要预热到 C2 接管——");
        System.out.println("     所以这个陷阱时有时无，更难防（这正是它危险的地方）");
        System.out.println("  → 还有个细节: 循环必须放进【独立方法】才会被 C2 正常编译——");
        System.out.println("     写在 main 里只能走 OSR 编译，优化程度不同（本例踩过这个坑）");
        System.out.println("  → 防御: 把结果累加到逃逸的变量，或用 JMH 的 Blackhole");

        System.out.println("\n== ③ 逃逸分析：对象可能根本没被分配（实测）==");
        final int M = 20_000_000;
        // 版本 A: 对象不逃逸 → JIT 可以标量替换，根本不在堆上分配（第 33 章）
        long t0 = System.nanoTime();
        long sumA = 0;
        for (int i = 0; i < M; i++) {
            int[] pair = {i, i + 1};                              // 局部数组，不逃逸
            sumA += pair[0] + pair[1];
        }
        double msNoEscape = (System.nanoTime() - t0) / 1e6;

        // 版本 B: 对象逃逸到 list → 必须真的分配
        List<int[]> escaped = new ArrayList<>(1000);
        t0 = System.nanoTime();
        long sumB = 0;
        for (int i = 0; i < M / 20; i++) {
            int[] pair = {i, i + 1};
            escaped.add(pair);                                    // 逃逸了
            sumB += pair[0] + pair[1];
            if (escaped.size() > 900) escaped.clear();
        }
        double msEscape = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  不逃逸的对象 %d 次: %7.1f ms（%.2f ns/次）%n",
                M, msNoEscape, msNoEscape * 1e6 / M);
        System.out.printf("  逃逸的对象   %d 次: %7.1f ms（%.2f ns/次）%n",
                M / 20, msEscape, msEscape * 1e6 / (M / 20));
        System.out.printf("  单次成本差 %.1fx（结果 %d / %d）%n",
                (msEscape / (M / 20.0)) / (msNoEscape / (double) M), sumA, sumB);
        System.out.println("  → 不逃逸的对象被【标量替换】: 字段直接放寄存器，根本没有堆分配");
        System.out.println("  → 这是「Java 对象分配很贵」这个说法过时的原因之一（第 33 章 TLAB + 逃逸分析）");
        System.out.println("  → 但也意味着: 你的微基准里如果对象不逃逸，测的就不是真实场景的分配成本");

        System.out.println("\n== ④ 字符串拼接：循环里的 + 是真的慢（实测）==");
        final int S = 20_000;
        t0 = System.nanoTime();
        String acc = "";
        for (int i = 0; i < S; i++) acc += "x";                    // 每次都造新 String
        double msConcat = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < S; i++) sb.append("x");
        String built = sb.toString();
        double msBuilder = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  循环里 s += \"x\" %d 次:      %7.1f ms%n", S, msConcat);
        System.out.printf("  StringBuilder.append %d 次: %7.1f ms（快 %.0fx）%n",
                S, msBuilder, msConcat / Math.max(msBuilder, 0.001));
        System.out.printf("  结果一致: %s（长度 %d）%n", acc.equals(built), built.length());
        System.out.println("  → String 不可变（第 9 章）→ 每次 += 都要【复制整个字符串】→ O(n²)");
        System.out.println("  → 注意: 单条语句里的 a + b + c 会被编译器优化成 StringBuilder，");
        System.out.println("     但【跨循环迭代】的累加优化不掉——这是少数「凭经验就能判断」的优化");

        System.out.println("\n== ⑤ JVM 上正确的性能测量 ==");
        System.out.println("  ① 用 JMH（Java Microbenchmark Harness）——它替你处理:");
        System.out.println("     预热轮次（①）、死代码消除（② 的 Blackhole）、分叉进程隔离 JIT 状态");
        System.out.println("  ② 生产环境用采样式 profiler: async-profiler / JFR");
        System.out.println("     —— 它们不需要修改代码，且开销低到可以【一直开着】");
        System.out.println("  ③ 看 GC 日志: -Xlog:gc —— 停顿时间往往是延迟毛刺的真凶（第 36 章）");
        System.out.println("  → 手写 System.nanoTime() 的微基准，在 JVM 上【错误率极高】——");
        System.out.println("     本节三个实验就是三种典型的错法");

        System.out.println("\n== ⑥ 与其他语言的对照 ==");
        System.out.println("  C++（无 JIT）: 编译期定死，测量相对直接——但要防编译器优化掉（C++ 版实测）");
        System.out.println("  JVM/CLR:      有 JIT → 必须预热；有逃逸分析 → 微基准容易失真");
        System.out.println("  Python:       无 JIT（CPython）→ 预热效应很小（Python 版实测仅 3%）");
        System.out.println("  → 「怎么测」这件事本身就是【语言相关】的——这是本章最容易被忽略的一课");
    }
}
