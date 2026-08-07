// 第 24 章 · 对象 —— Java 示例
// 运行：javac Main.java && java Main
// Java 对象布局最「确定」：12 字节对象头 + 字段（JVM 自动重排）+ 对齐填充

public class Main {

    static class Empty { }
    static class OneInt { int x; }
    static class TwoInts { int x, y; }

    // JVM 会自动重排字段，声明顺序不影响最终布局
    static class Mixed {
        byte a;
        long b;
        byte c;
        int d;
    }

    /** 估算每个对象的大小：创建 N 个后看内存增量 */
    static double measurePerObject(int n, java.util.function.Supplier<Object> factory)
            throws InterruptedException {
        Runtime rt = Runtime.getRuntime();
        // 预热，让 JIT 和内存分配器先稳定下来
        for (int w = 0; w < 3; w++) {
            Object[] warm = new Object[1000];
            for (int i = 0; i < 1000; i++) warm[i] = factory.get();
        }
        System.gc();
        Thread.sleep(120);
        long before = rt.totalMemory() - rt.freeMemory();

        Object[] arr = new Object[n];
        for (int i = 0; i < n; i++) arr[i] = factory.get();

        System.gc();
        Thread.sleep(120);
        long after = rt.totalMemory() - rt.freeMemory();

        if (arr[0] == null) System.out.println("防优化");   // 确保数组不被优化掉
        // 减去数组本身存引用的开销（压缩指针下每个引用 4 字节）
        return (after - before - 4.0 * n) / n;
    }

    public static void main(String[] args) throws InterruptedException {
        final int N = 1_000_000;

        System.out.println("=== 1. ⚠️ 对象头：你付不掉的固定开销 ===");
        double oneInt = measurePerObject(N, OneInt::new);
        System.out.printf("  %,d 个「只有一个 int」的对象%n", N);
        System.out.printf("  实测每个对象约 %.0f 字节%n", oneInt);
        System.out.println("  而 int 本身只需要 4 字节");
        System.out.printf("  → 对象头开销占比约 %.0f%%%n", 100 * (oneInt - 4) / oneInt);

        System.out.println("\n  对象头的组成（64 位 HotSpot，开启压缩指针）:");
        System.out.println("    mark word    8 字节   哈希码 / GC 分代年龄 / 锁状态");
        System.out.println("    类型指针      4 字节   指向 Class 元数据（我是谁）");
        System.out.println("    ────────────────────");
        System.out.println("    对象头合计    12 字节");
        System.out.println("    + 数据 4 字节 = 16 字节（对齐到 8 的倍数）");

        System.out.println("\n=== 2. 加字段后对象怎么变大的 ===");
        double twoInts = measurePerObject(N, TwoInts::new);
        System.out.printf("  只有一个 int  → 约 %.0f 字节%n", oneInt);
        System.out.printf("  有两个 int    → 约 %.0f 字节%n", twoInts);
        System.out.println();
        System.out.println("  算一下就明白了（对象整体要对齐到 8 的倍数）:");
        System.out.println("    一个 int : 12 字节头 + 4 = 16   ← 正好是 8 的倍数，零填充");
        System.out.println("    两个 int : 12 字节头 + 8 = 20   → 补到 24，浪费 4 字节");
        System.out.println("  → 注意「一个 int」那个对象是最理想的情况：16 字节里没有一点填充浪费，");
        System.out.println("     但其中 12 字节全是对象头，真正的数据只占 4 字节");

        System.out.println("\n=== 3. ⚠️ 包装类型的代价 ===");
        System.out.println("  int[] 10,000,000 个        ≈ 40 MB（一块连续内存）");
        System.out.println("  Integer[] 10,000,000 个    ≈ 40 MB 引用 + 160 MB 对象 = 200 MB");
        System.out.println("  → 差 5 倍！这是 Java 性能问题的经典来源");

        // 实测一个小规模的对比
        int m = 500_000;
        Runtime rt = Runtime.getRuntime();
        System.gc(); Thread.sleep(120);
        long b1 = rt.totalMemory() - rt.freeMemory();
        int[] prim = new int[m];
        for (int i = 0; i < m; i++) prim[i] = i;
        System.gc(); Thread.sleep(120);
        long a1 = rt.totalMemory() - rt.freeMemory();

        System.gc(); Thread.sleep(120);
        long b2 = rt.totalMemory() - rt.freeMemory();
        Integer[] boxed = new Integer[m];
        for (int i = 0; i < m; i++) boxed[i] = i;      // 自动装箱
        System.gc(); Thread.sleep(120);
        long a2 = rt.totalMemory() - rt.freeMemory();

        double primMB = (a1 - b1) / 1024.0 / 1024;
        double boxedMB = (a2 - b2) / 1024.0 / 1024;
        System.out.printf("%n  实测 %,d 个元素:%n", m);
        System.out.printf("    int[]     约 %.1f MB%n", primMB);
        System.out.printf("    Integer[] 约 %.1f MB   → 是前者的 %.1f 倍%n",
                boxedMB, boxedMB / primMB);
        if (prim[0] != 0 || boxed[1] != 1) System.out.println("防优化");

        System.out.println("\n  ⚠️ 注意 Integer 缓存：-128..127 是共享的同一批对象");
        Integer x = 100, y = 100;
        Integer p = 1000, q = 1000;
        System.out.println("    Integer x=100,  y=100  → x == y ? " + (x == y) + "  ← 命中缓存");
        System.out.println("    Integer p=1000, q=1000 → p == q ? " + (p == q) + "  ← 超出缓存范围");
        System.out.println("    → 所以比较包装类型永远要用 equals，不能用 ==");

        System.out.println("\n=== 4. JVM 会自动重排字段 ===");
        System.out.println("  class Mixed { byte a; long b; byte c; int d; }");
        System.out.println("  声明顺序: byte, long, byte, int");
        System.out.println("  JVM 实际可能排成: long, int, byte, byte");
        System.out.println("  → JVM 主动帮你做了 C++ 里需要手工做的字段重排");
        System.out.println("  → 所以「按大小降序声明」这条建议对 Java 不适用");

        System.out.println("\n=== 5. 精确测量应该用 JOL ===");
        System.out.println("  本示例用 Runtime 内存差值估算，做了预热和 GC，");
        System.out.println("  结果与 HotSpot 已知布局规则吻合，但仍是估算。");
        System.out.println("  精确查看请用 OpenJDK 的 JOL 工具：");
        System.out.println("    ClassLayout.parseClass(Student.class).toPrintable()");
        System.out.println("  → 它能打印每个字段的确切偏移和填充");
        System.out.println("  ⚠️ 换 JVM、关闭压缩指针（堆 > 32 GB）结果都会变");

        System.out.println("\n=== 6. 小结 ===");
        System.out.printf("  · Java 对象 = 12 字节头 + 字段 + 填充，实测最小实例 %.0f 字节%n", oneInt);
        System.out.println("  · 这 12 字节买到了 GC、锁、hashCode 和运行时类型信息");
        System.out.println("  · 热点路径处理大量数值时用原始类型数组，别用包装类型");
        System.out.println("  · 字段重排由 JVM 代劳，不需要你操心");
    }
}
