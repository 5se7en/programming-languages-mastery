// 数据库：文件做不到的四件事——以及 .NET 里的对应 API。
using System.Diagnostics;
using System.Text;

class Program
{
    static void Main()
    {
        string work = Directory.CreateTempSubdirectory("pl-mastery-cs-db-").FullName;
        byte[] rec = Encoding.UTF8.GetBytes("id=00042,name=zhang,balance=100\n");

        Console.WriteLine("== ① 持久化两档：Flush(false) vs Flush(true) ==");
        string p1 = Path.Combine(work, "t.log");
        using (var fs = new FileStream(p1, FileMode.Create, FileAccess.Write))
        {
            const int N1 = 2000;
            var sw = Stopwatch.StartNew();
            for (int i = 0; i < N1; i++) { fs.Write(rec); fs.Flush(false); }
            double ms1 = sw.Elapsed.TotalMilliseconds;
            const int N2 = 200;
            sw = Stopwatch.StartNew();
            for (int i = 0; i < N2; i++) { fs.Write(rec); fs.Flush(true); }
            double ms2 = sw.Elapsed.TotalMilliseconds;
            Console.WriteLine($"  Flush(false) {N1} 次: {ms1:F1} ms   ← 只倒空用户态缓冲，数据在页缓存");
            Console.WriteLine($"  Flush(true)  {N2} 次: {ms2:F1} ms（{ms2 / N2:F1} ms/次）");
            Console.WriteLine("  → 裸 fsync 只要 ~21 μs（C++/Java 版实测）；.NET 的 PAL 在 macOS 上");
            Console.WriteLine("    把 Flush(true) 直接实现成【F_FULLFSYNC】——和 libuv 一样选择了掉电安全");
            Console.WriteLine("  → C/Python/Java 给你便宜但不防掉电的 fsync；Node/.NET 给你贵但真落盘的——");
            Console.WriteLine("    五个运行时、同一个「fsync」，两种不同的承诺");
        }

        Console.WriteLine("\n== ② 原子性的手工替代品：临时文件 + 原子换名 ==");
        string cfg = Path.Combine(work, "config.json");
        File.WriteAllText(cfg, "{\"version\": 1}");
        string tmp = cfg + ".tmp";
        File.WriteAllText(tmp, "{\"version\": 2}");               // 新内容先完整写进临时文件
        File.Replace(tmp, cfg, null);                              // rename 是原子的（POSIX 保证）
        Console.WriteLine($"  File.Replace 后内容: {File.ReadAllText(cfg)}");
        Console.WriteLine("  → 读者永远只会看到完整的版本 1 或版本 2，绝不会看到写了一半的");
        Console.WriteLine("  → 但它只能保护【单个文件的整体替换】——跨文件、跨行的原子性只有事务能给");

        Console.WriteLine("\n== ③ 并发读-改-写：丢更新实测 ==");
        string cnt = Path.Combine(work, "counter.txt");
        File.WriteAllText(cnt, "0");
        const int EACH = 150;
        int tornReads = 0;                   // WriteAllText = 先截断再写；读者可能撞见【空文件】
        void Incr()
        {
            for (int i = 0; i < EACH; i++)
            {
                if (!int.TryParse(File.ReadAllText(cnt), out int v))
                {
                    Interlocked.Increment(ref tornReads);
                    i--;                     // 这次读作废，重来
                    continue;
                }
                Thread.SpinWait(50);
                File.WriteAllText(cnt, (v + 1).ToString());
            }
        }
        var t1 = new Thread(Incr); var t2 = new Thread(Incr);
        t1.Start(); t2.Start(); t1.Join(); t2.Join();
        int got = int.Parse(File.ReadAllText(cnt));
        Console.WriteLine($"  2 线程 × {EACH} 次自增，期望 {2 * EACH}，实际 {got}（丢了 {2 * EACH - got} 次）");
        if (tornReads > 0)
            Console.WriteLine($"  更糟: 有 {tornReads} 次读到了【空文件】——WriteAllText 先截断后写，读者撞在中间");

        File.WriteAllText(cnt, "0");
        var gate = new object();
        void IncrLocked()
        {
            for (int i = 0; i < EACH; i++)
                lock (gate)
                {
                    int v = int.Parse(File.ReadAllText(cnt));
                    File.WriteAllText(cnt, (v + 1).ToString());
                }
        }
        t1 = new Thread(IncrLocked); t2 = new Thread(IncrLocked);
        t1.Start(); t2.Start(); t1.Join(); t2.Join();
        Console.WriteLine($"  加 lock 后: {File.ReadAllText(cnt)} ✓ —— 但 lock 只管【本进程】（第 41 章）");
        Console.WriteLine("  → 跨进程要 Mutex，跨机器就没有 API 了——数据库的锁天生跨进程、跨机器");

        Console.WriteLine("\n== ④ 查询：全表拉回 vs 让数据库找 ==");
        const int ROWS = 100_000;
        string users = Path.Combine(work, "users.csv");
        var sb = new StringBuilder();
        for (int i = 0; i < ROWS; i++) sb.Append(i).Append(",user-").Append(i).Append(',').Append(i % 100).Append('\n');
        File.WriteAllText(users, sb.ToString());
        var sw2 = Stopwatch.StartNew();
        int hits = 0;
        for (int k = 0; k < 20; k++)
        {
            string target = (ROWS - 1 - k) + ",";
            foreach (var line in File.ReadLines(users))          // 每次查找 O(n) 扫全文件
                if (line.StartsWith(target)) { hits++; break; }
        }
        double msScan = sw2.Elapsed.TotalMilliseconds;
        sw2 = Stopwatch.StartNew();
        var index = new Dictionary<int, string>(ROWS);
        foreach (var line in File.ReadLines(users))
        {
            int c = line.IndexOf(',');
            index[int.Parse(line.AsSpan(0, c))] = line;
        }
        double msBuild = sw2.Elapsed.TotalMilliseconds;
        sw2 = Stopwatch.StartNew();
        for (int k = 0; k < 20; k++) _ = index[ROWS - 1 - k];
        double msIdx = sw2.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  20 次查找逐次扫描: {msScan:F1} ms；先建 Dictionary（{msBuild:F0} ms）再查: {msIdx:F3} ms");
        Console.WriteLine("  → 索引就是「一次预处理换 N 次快查」；数据库把这个 Dictionary【持久化在磁盘上】");
        Console.WriteLine("    并在每次写入时自动维护它——这正是第 49 章 B 树要讲的事");

        Console.WriteLine("\n== ⑤ .NET 连数据库的正道 ==");
        Console.WriteLine("  ADO.NET: 一套接口（DbConnection/DbCommand），各家实现");
        Console.WriteLine("  Microsoft.Data.Sqlite / Npgsql / SqlClient——NuGet 引入（本例不联网故用文件演示）");
        Console.WriteLine("  EF Core 是 ORM（第 51 章）——对象和关系之间的翻译官");

        Directory.Delete(work, true);
    }
}
