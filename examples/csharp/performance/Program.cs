// 性能优化：热路径上的内存分配——GC 语言里最常见、也最容易消除的那笔开销。
using System.Diagnostics;
using System.Text;

class Program
{
    record Point(double X, double Y);                 // 引用类型: 每次 new 都在堆上
    readonly record struct PointS(double X, double Y); // 值类型: 栈上，无 GC

    static double Dist(Point a, Point b) =>
        Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));
    static double Dist(PointS a, PointS b) =>
        Math.Sqrt((a.X - b.X) * (a.X - b.X) + (a.Y - b.Y) * (a.Y - b.Y));

    /// 跑一次预热 + rounds 轮计时，返回【最快一轮的耗时】和【单轮的分配量】。
    /// 两个数字口径必须一致，否则「快 2x 但分配少 8x」这种对比就是假的。
    static (double ms, long bytes, int gen0) Bench(Action f, int rounds = 5)
    {
        f();                                          // 预热（第 52/57 章的 JIT 教训）
        var best = double.MaxValue;
        long bytes = 0;
        int gen0 = 0;
        for (int i = 0; i < rounds; i++)
        {
            long a0 = GC.GetTotalAllocatedBytes();
            int c0 = GC.CollectionCount(0);
            var sw = Stopwatch.StartNew();
            f();
            double ms = sw.Elapsed.TotalMilliseconds;
            if (ms < best) { best = ms; bytes = GC.GetTotalAllocatedBytes() - a0; gen0 = GC.CollectionCount(0) - c0; }
        }
        return (best, bytes, gen0);
    }

    static void Main()
    {
        const int N = 5_000_000;

        Console.WriteLine("== ① 热路径上的堆分配：贵的到底是什么（实测）==");
        var cls = Bench(() =>
        {
            double sum = 0;
            for (int i = 0; i < N; i++) sum += Dist(new Point(i, i), new Point(i + 1, i + 1));
            if (sum < 0) Console.Write("");
        }, 3);

        var str = Bench(() =>
        {
            double sum = 0;
            for (int i = 0; i < N; i++) sum += Dist(new PointS(i, i), new PointS(i + 1, i + 1));
            if (sum < 0) Console.Write("");
        }, 3);

        Console.WriteLine($"  record class（每次都在堆上 new）: {cls.ms,7:F1} ms，"
                          + $"分配 {cls.bytes / 1048576.0,6:F0} MB，Gen0 {cls.gen0} 次");
        Console.WriteLine($"  record struct（栈上，零分配）:    {str.ms,7:F1} ms，"
                          + $"分配 {str.bytes / 1048576.0,6:F0} MB，Gen0 {str.gen0} 次");
        Console.WriteLine($"  → 只快 {cls.ms / Math.Max(str.ms, 0.001):F1}x —— 【远小于直觉预期】");
        Console.WriteLine($"  → 期间分配了 {cls.bytes / 1048576.0:F0} MB、触发了 {cls.gen0} 次 Gen0 回收，"
                          + "代价却这么小");

        // 那 GC 的代价到底在哪？直接测「一次完整回收」与【存活对象数量】的关系。
        Console.WriteLine("\n  那 GC 的成本到底由什么决定？直接测一次完整回收的耗时:");
        var anchor = new List<Point[]>();
        foreach (int liveCount in new[] { 0, 1_000_000, 4_000_000 })
        {
            anchor.Clear();
            if (liveCount > 0)
            {
                var block = new Point[liveCount];
                for (int i = 0; i < liveCount; i++) block[i] = new Point(i, i);
                anchor.Add(block);                        // 这些对象【活着】
            }
            GC.Collect(); GC.WaitForPendingFinalizers();  // 先稳定下来
            for (int i = 0; i < N / 10; i++) { var g = new Point(i, i); if (g.X < 0) Console.Write(""); }

            var swGc = Stopwatch.StartNew();
            GC.Collect(2, GCCollectionMode.Forced, blocking: true);
            double gcMs = swGc.Elapsed.TotalMilliseconds;
            Console.WriteLine($"    存活 {liveCount,8} 个对象时，一次 Gen2 完整回收: {gcMs,6:F1} ms");
        }
        GC.KeepAlive(anchor);
        Console.WriteLine("  → 回收耗时随【存活对象数】增长，与【已经死掉的垃圾量】几乎无关");
        Console.WriteLine("  ⚠️ 这修正了「堆分配很贵」的粗糙说法: 分配几百 MB 纯垃圾几乎不要钱");
        Console.WriteLine("  → 第 36 章的【分代假说】: 回收器只遍历【存活】对象再把它们搬走，");
        Console.WriteLine("     死对象从头到尾没被访问过——所以「全是垃圾」是 GC 最擅长的情况");
        Console.WriteLine("  → 所以准确的原则不是「别分配」，而是【别让短命对象活过 Gen0】");
        Console.WriteLine("  → 真正该警惕的是【缓存】和【静态集合】: 它们把对象钉在老年代，");
        Console.WriteLine("     每次 Gen2 回收都要重新遍历一遍（上面第三行就是这笔账）");

        Console.WriteLine("\n== ② 字符串：不可变带来的隐形分配（实测）==");
        const int S = 30_000;
        var cat = Bench(() =>
        {
            string acc = "";
            for (int i = 0; i < S; i++) acc += "x";   // 每次都造新字符串
            if (acc.Length < 0) Console.Write("");
        }, 3);
        var bld = Bench(() =>
        {
            var sb = new StringBuilder();
            for (int i = 0; i < S; i++) sb.Append('x');
            if (sb.Length < 0) Console.Write("");
        }, 3);

        Console.WriteLine($"  循环里 s += \"x\" {S} 次:      {cat.ms,9:F3} ms，"
                          + $"分配 {cat.bytes / 1048576.0,7:F1} MB");
        Console.WriteLine($"  StringBuilder.Append {S} 次: {bld.ms,9:F3} ms，"
                          + $"分配 {bld.bytes / 1024.0,7:F1} KB");
        Console.WriteLine($"  → 快 {cat.ms / Math.Max(bld.ms, 0.0001):F0}x，"
                          + $"分配量差 {(double)cat.bytes / Math.Max(bld.bytes, 1):F0}x");
        Console.WriteLine("  → 字符串不可变（第 9 章）→ 每次 += 复制整串 → 分配量 O(n²)");
        Console.WriteLine("  → Java 版实测了同一件事——这是少数【跨语言通用】的性能规则");

        Console.WriteLine("\n== ③ Span<T>：切片而不复制（实测）==");
        const int SL = 2_000_000;
        var text = new string('a', 200);
        var sub = Bench(() =>
        {
            int n = 0;
            for (int i = 0; i < SL; i++) n += text.Substring(50, 100).Length;  // 每次复制 100 字符
            if (n < 0) Console.Write("");
        }, 3);
        var spn = Bench(() =>
        {
            int n = 0;
            for (int i = 0; i < SL; i++) n += text.AsSpan(50, 100).Length;     // 只是一个视图
            if (n < 0) Console.Write("");
        }, 3);

        Console.WriteLine($"  Substring(50,100) {SL} 次: {sub.ms,7:F1} ms，"
                          + $"分配 {sub.bytes / 1048576.0,6:F0} MB，Gen0 {sub.gen0} 次");
        Console.WriteLine($"  AsSpan(50,100)    {SL} 次: {spn.ms,7:F1} ms，"
                          + $"分配 {spn.bytes / 1024.0,6:F0} KB，Gen0 {spn.gen0} 次");
        Console.WriteLine($"  → 快 {sub.ms / Math.Max(spn.ms, 0.001):F0}x，几乎零分配");
        Console.WriteLine("  → Span<T> 是一个【指向已有内存的视图】（第 34 章的指针，被类型系统管住了）");
        Console.WriteLine("  → 解析器/序列化器的现代写法都建立在它之上——避免为「看一眼」而复制");

        Console.WriteLine("\n== ④ 装箱：值类型进了对象世界（实测）==");
        const int B = 5_000_000;
        var box = Bench(() =>
        {
            var list = new List<object>(1024);
            long sum = 0;
            for (int i = 0; i < B; i++)
            {
                object boxed = i;                     // ⚠️ int → object: 装箱，堆分配
                sum += (int)boxed;
                if (list.Count < 1000) list.Add(boxed);
            }
            if (sum < 0) Console.Write("");
        }, 3);

        var gen = Bench(() =>
        {
            var list = new List<int>(1024);
            long sum = 0;
            for (int i = 0; i < B; i++)
            {
                int v = i;                            // 泛型 List<int>: 无装箱
                sum += v;
                if (list.Count < 1000) list.Add(v);
            }
            if (sum < 0) Console.Write("");
        }, 3);

        Console.WriteLine($"  装箱成 object  {B} 次: {box.ms,7:F1} ms，"
                          + $"分配 {box.bytes / 1048576.0,6:F0} MB");
        Console.WriteLine($"  泛型 List<int> {B} 次: {gen.ms,7:F1} ms，"
                          + $"分配 {gen.bytes / 1024.0,6:F0} KB");
        Console.WriteLine($"  → 快 {box.ms / Math.Max(gen.ms, 0.001):F1}x");
        Console.WriteLine("  → 第 29 章讲过: C# 泛型是【真泛型】（运行时特化），Java 是类型擦除必须装箱");
        Console.WriteLine("  → 所以 Java 的 List<Integer> 无法避免装箱——这是两种泛型设计的性能后果");

        Console.WriteLine("\n== ⑤ .NET 的测量工具 ==");
#if DEBUG
        Console.WriteLine("  ⚠️ 本次运行在【Debug】配置下（dotnet run 的默认值）——");
        Console.WriteLine("     Debug 关闭了 JIT 的大部分优化，上面所有数字在 Release 下都会不同");
        Console.WriteLine("     → 又一条容易被忽略的纪律: 【性能数据必须注明构建配置】");
#else
        Console.WriteLine("  本次运行在 Release 配置下（性能测量应有的配置）");
#endif
        Console.WriteLine("  BenchmarkDotNet: 事实标准——自动预热、多次采样、报告分配量与 GC 次数");
        Console.WriteLine("     它还会【拒绝在 Debug 下运行】，就是为了防止上面那个错误");
        Console.WriteLine("     它会替你处理 Java 版 ② 那种死代码消除问题");
        Console.WriteLine("  dotnet-counters / dotnet-trace: 生产环境低开销采样");
        Console.WriteLine("  GC.GetTotalAllocatedBytes(): 本例用的——【分配量比耗时更稳定】，是更好的指标");
        Console.WriteLine("  → 关键洞察: 在 GC 语言里，先看【分配量】再看耗时——");
        Console.WriteLine("     分配量是确定性的，耗时受 GC 时机影响会抖动");

        Console.WriteLine("\n== ⑥ GC 语言的性能优化清单 ==");
        Console.WriteLine("  ① 热路径不分配: 值类型（①）、Span（③）、对象池、ArrayPool<T>");
        Console.WriteLine("  ② 避免装箱: 用泛型而非 object（④）");
        Console.WriteLine("  ③ 字符串: StringBuilder / string.Create / 插值优化（②）");
        Console.WriteLine("  ④ 大对象警惕 LOH: >85KB 的数组进大对象堆，不压缩（第 36 章）");
        Console.WriteLine("  → 但依然要先 profile: 上面每一条都可能在你的程序里【完全不重要】");
        Console.WriteLine("  → Python 版 ④ 的阿姆达尔定律永远是第一道闸门");
    }
}
