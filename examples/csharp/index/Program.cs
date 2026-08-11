// 索引：为什么「回表」这么贵——把随机 I/O 与顺序 I/O 的差距量出来。
using System.Diagnostics;

class Program
{
    static void Main()
    {
        // 一张「表」: 每行 64 字节（= 一条缓存行），模拟真实的宽行
        const int ROWS = 1_000_000, W = 8;                 // 8 个 long = 64 字节
        var table = new long[(long)ROWS * W];
        for (int i = 0; i < ROWS; i++) table[(long)i * W] = i;

        Console.WriteLine("== \u2460 顺序访问 vs 随机访问：索引回表贵在哪里 ==");
        Console.WriteLine($"  一张 {ROWS} 行 \u00d7 {W * 8} 字节 = {(double)ROWS * W * 8 / 1048576:F0} MB 的表:");
        Console.WriteLine("  「走索引」= 按索引给出的行号【随机】跳着读；「全表扫」= 从头【顺序】读一遍");
        var rnd = new Random(42);
        foreach (int hits in new[] { 1_000, 10_000, 100_000, 300_000, 500_000, 1_000_000 })
        {
            var rowIds = new int[hits];
            for (int i = 0; i < hits; i++) rowIds[i] = rnd.Next(ROWS);

            var sw = Stopwatch.StartNew();
            long sum1 = 0;
            for (int i = 0; i < hits; i++) sum1 += table[(long)rowIds[i] * W];     // 随机
            double msRandom = sw.Elapsed.TotalMilliseconds;

            sw = Stopwatch.StartNew();
            long sum2 = 0;
            for (long i = 0; i < (long)ROWS * W; i += W) sum2 += table[i];         // 顺序
            double msSeq = sw.Elapsed.TotalMilliseconds;

            string winner = msRandom < msSeq ? "索引回表更快" : "【全表扫描更快】";
            Console.WriteLine($"  命中 {hits,7} 行（{100.0 * hits / ROWS,5:F1}%）: "
                              + $"回表 {msRandom,6:F1} ms   全表扫 {msSeq,6:F1} ms → {winner}");
        }
        Console.WriteLine("  \u2192 命中比例越高，随机访问次数越多，最终【反超】全表扫描的顺序访问");
        Console.WriteLine("  \u2192 这就是优化器的核心权衡: 估算命中行数，再决定用不用索引");
        Console.WriteLine("  \u26a0\ufe0f 但注意本例的交叉点很高（约 30~50%）——因为内存里随机只比顺序慢约 3x");
        Console.WriteLine("     【磁盘】上随机 I/O 比顺序慢一到两个数量级，交叉点因此低得多:");
        Console.WriteLine("     真实数据库的经验值是命中超过 5%~20% 就该放弃索引走全表扫");

        Console.WriteLine("\n== \u2461 缓存局部性：顺序为什么快（第 16 章的账，索引版）==");
        var perm = new int[ROWS];
        for (int i = 0; i < ROWS; i++) perm[i] = i;
        var rnd2 = new Random(7);
        for (int i = ROWS - 1; i > 0; i--) { int j = rnd2.Next(i + 1); (perm[i], perm[j]) = (perm[j], perm[i]); }

        var sw2 = Stopwatch.StartNew();
        long s1 = 0;
        for (long i = 0; i < (long)ROWS * W; i += W) s1 += table[i];               // 顺序
        double msLinear = sw2.Elapsed.TotalMilliseconds;

        sw2 = Stopwatch.StartNew();
        long s2 = 0;
        for (int i = 0; i < ROWS; i++) s2 += table[(long)perm[i] * W];             // 打乱顺序
        double msShuffled = sw2.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  读【同样的 {ROWS} 行】: 顺序 {msLinear:F1} ms，打乱顺序 {msShuffled:F1} ms"
                          + $"（慢 {msShuffled / msLinear:F1}x）");
        Console.WriteLine($"  结果一致: {s1 == s2}");
        Console.WriteLine("  \u2192 行数完全一样，只是【访问顺序】不同——差距全部来自 CPU 缓存行与硬件预取");
        Console.WriteLine("  \u2192 磁盘上同一个道理，只是量级从「缓存行」变成「4KB 页 + 寻道」，差距大得多");

        Console.WriteLine("\n== ③ 覆盖索引：把随机 I/O 变回顺序 I/O ==");
        Console.WriteLine("  普通索引查询的两步:");
        Console.WriteLine("    ① 在索引 B+ 树里找到匹配的 (列值 → 主键)  —— 顺序，便宜");
        Console.WriteLine("    ② 拿主键回主表逐行取完整数据            —— 【随机，昂贵】（① 实测）");
        Console.WriteLine("  覆盖索引省掉了第 ② 步: 要的列全在索引里，读完索引直接返回");
        Console.WriteLine("  → Python 版实测 1.9x，那还是在内存里；磁盘上差距会大得多");
        Console.WriteLine("  → 代价: 索引变宽 → 扇出变小 → 树变高 + 占更多空间（C++ 版的扇出实测）");

        Console.WriteLine("\n== ④ 聚簇索引 vs 非聚簇索引 ==");
        Console.WriteLine("  聚簇(clustered):   表数据【本身】就按索引顺序物理排列");
        Console.WriteLine("    → InnoDB 的主键、SQL Server 的聚簇索引；每张表只能有一个");
        Console.WriteLine("    → 主键范围查询是顺序 I/O（极快）；但主键值别用随机 UUID:");
        Console.WriteLine("      随机主键让每次插入都落在随机页上 → 页分裂 + 随机写（① 的代价）");
        Console.WriteLine("  非聚簇(secondary): 索引里只存「列值 → 主键」，取数据要回表");
        Console.WriteLine("    → 所以 InnoDB 的二级索引查询是【两次 B+ 树下钻】（先索引后主键）");
        Console.WriteLine("  → 「自增主键比 UUID 快」的根因就在这里，不是玄学");

        Console.WriteLine("\n== ⑤ .NET 侧的实用提示 ==");
        Console.WriteLine("  EF Core: [Index(nameof(Prop))] 特性 / modelBuilder.HasIndex(...)");
        Console.WriteLine("           .HasIndex(x => new { x.A, x.B }) —— 复合索引，注意列顺序");
        Console.WriteLine("           .IncludeProperties(...) —— SQL Server 的 INCLUDE，做覆盖索引");
        Console.WriteLine("  ⚠️ EF Core 的迁移不会替你判断索引是否值得建——那是你的工作");
        Console.WriteLine("  → 排查手段: 打开 EF 的查询日志，把慢 SQL 拿去 EXPLAIN");
    }
}
