// 事务：乐观锁 vs 悲观锁——两种并发控制策略的实测对撞。
using System.Diagnostics;

class Program
{
    // 共享的「一行数据」：值 + 版本号（乐观锁的核心就是这个 version 列）
    class Row { public int Value; public int Version; }

    static void Main()
    {
        const int WORKERS = 8, PER_WORKER = 2000;
        const int TOTAL = WORKERS * PER_WORKER;

        Console.WriteLine($"== 任务: {WORKERS} 个线程共做 {TOTAL} 次「读-改-写」计数器 +1 ==");

        // ---------- ① 无并发控制：丢失更新 ----------
        var one = new[] { new Row() };
        var sw = Stopwatch.StartNew();
        RunAll(WORKERS, w =>
        {
            for (int i = 0; i < PER_WORKER; i++)
            {
                int read = one[0].Value;           // 读
                Thread.SpinWait(20);               // 「想一想」——放大竞争窗口
                one[0].Value = read + 1;           // 改-写
            }
        });
        double msNone = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine("\n== ① 无并发控制（对照组）==");
        Console.WriteLine($"  期望 {TOTAL}，实际 {one[0].Value}（丢了 {TOTAL - one[0].Value} 次），耗时 {msNone:F0} ms");
        Console.WriteLine("  → 读-改-写不原子 —— 第 46 章在文件上实测过，换成内存/数据库行一样成立");

        // ---------- ②③ 两种策略 × 四种冲突强度 ----------
        Console.WriteLine("\n== ② 悲观锁 vs ③ 乐观锁：在四种冲突强度下对撞 ==");
        Console.WriteLine("  悲观 = 先加锁再读（SELECT ... FOR UPDATE），整段读-改-写串行");
        Console.WriteLine("  乐观 = 不加锁地读，写回时检查版本号（UPDATE ... WHERE version = ?），冲突就重试");
        Console.WriteLine();
        Console.WriteLine("  行数  冲突强度        悲观锁      乐观锁    乐观重试次数   差距");

        foreach (var (rows, label) in new[]
                 { (1, "极高(热点行)"), (8, "高          "), (64, "中          "), (512, "低(分散)    ") })
        {
            var (pMs, pOk) = RunPessimistic(rows, WORKERS, PER_WORKER);
            var (oMs, oOk, retries) = RunOptimistic(rows, WORKERS, PER_WORKER);
            string gap = oMs < pMs ? $"乐观快 {pMs / oMs:F2}x" : $"悲观快 {oMs / pMs:F2}x";
            Console.WriteLine($"  {rows,4}  {label}  {pMs,7:F1} ms  {oMs,7:F1} ms  {retries,10}   {gap}"
                              + (pOk && oOk ? "" : "  ⚠️结果不正确"));
        }

        Console.WriteLine("\n== ④ 实测给那条教科书规律【打了个折扣】 ==");
        Console.WriteLine("  流行说法: 「冲突低时乐观锁赢，冲突高时悲观锁赢」");
        Console.WriteLine("  实测中【唯一稳定可复现】的只有第一行: 热点行上悲观锁快 1.5~1.9x");
        Console.WriteLine("  （乐观锁在那里做了五万次无用功——读完发现版本变了，全部重来）");
        Console.WriteLine("  其余三档反复跑会在 1.0~1.6x 之间【方向都会变】—— 那就是噪声，不是规律");
        Console.WriteLine("  → 原因: 内存里加一次锁只要几十纳秒，乐观锁根本没有多少东西可省");
        Console.WriteLine("  → 那条规律的前提是【持锁的代价很大】——而在数据库里代价大是因为");
        Console.WriteLine("     锁被持有【跨越了网络往返】甚至【跨越了用户思考时间】，不是因为锁指令本身");
        Console.WriteLine("  → 教训: 「乐观/悲观哪个快」是个被问错的问题（见 ⑤ 才是真正的分界）");

        Console.WriteLine("\n== ⑤ 乐观锁真正的价值：能跨越「无法持锁」的边界（实测）==");
        Console.WriteLine("  场景: 用户打开编辑表单 → 思考 5 分钟 → 点保存。你不可能持有数据库锁 5 分钟。");
        var doc = new Row { Value = 100, Version = 1 };
        // 两个用户在【同一时刻】各自读到了同一版本
        int u1Val = doc.Value, u1Ver = doc.Version;
        int u2Val = doc.Value, u2Ver = doc.Version;
        Console.WriteLine($"  用户甲、乙同时打开编辑页，都读到 值={u1Val} 版本={u1Ver}");
        // 甲先保存
        bool okA = doc.Version == u1Ver;
        if (okA) { doc.Value = u1Val + 10; doc.Version++; }
        Console.WriteLine($"  甲改成 {u1Val + 10} 并保存: {(okA ? "成功" : "失败")}"
                          + $"（现在 值={doc.Value} 版本={doc.Version}）");
        // 乙后保存——版本号已变
        bool okB = doc.Version == u2Ver;
        if (okB) { doc.Value = u2Val + 20; doc.Version++; }
        Console.WriteLine($"  乙改成 {u2Val + 20} 并保存: {(okB ? "成功" : "【被拒绝】")}"
                          + $"（他手里的版本 {u2Ver} ≠ 当前版本 {doc.Version}）");
        Console.WriteLine($"  最终 值={doc.Value} —— 乙的修改没有【悄悄覆盖】甲的");
        Console.WriteLine("  → 若无版本号: 乙的保存会直接覆盖甲的改动，甲永远不知道（丢失更新）");
        Console.WriteLine("  → 这才是乐观锁的分界线: 它不是「更快的锁」，而是");
        Console.WriteLine("     【唯一能跨越无状态请求边界的并发控制】——HTTP 请求之间没法持锁");

        Console.WriteLine("\n== ⑥ 乐观锁在 ORM 里的样子 ==");
        Console.WriteLine("  EF Core:    [Timestamp] byte[] RowVersion;  → 自动加 WHERE RowVersion = ?");
        Console.WriteLine("              提交时行数为 0 就抛 DbUpdateConcurrencyException");
        Console.WriteLine("  Hibernate:  @Version int version;           → 同样的机制（第 51 章 ORM）");
        Console.WriteLine("  → 「更新了 0 行」这个信号就是冲突检测: 版本号对不上 → WHERE 匹配不到");

        Console.WriteLine("\n== ⑦ .NET 的事务 API ==");
        Console.WriteLine("  using var tx = conn.BeginTransaction(IsolationLevel.ReadCommitted);");
        Console.WriteLine("  cmd.Transaction = tx;   ← ⚠️ 忘了这行，命令【不在事务里】（最常见的坑）");
        Console.WriteLine("  tx.Commit();  /  tx.Rollback();   ← using 块结束未提交则自动回滚");
        Console.WriteLine("  TransactionScope: 环境事务，能跨多个连接——但会升级成分布式事务(MSDTC)，慎用");
        Console.WriteLine("  → IsolationLevel.Snapshot 需要数据库先开 ALLOW_SNAPSHOT_ISOLATION");

        Console.WriteLine("\n== ⑧ 重试逻辑必须知道的两件事 ==");
        Console.WriteLine("  ① 只重试【可重试的错误】: 序列化冲突/死锁可以重试，约束违规重试一万次也没用");
        Console.WriteLine("  ② 必须【指数退避 + 上限】: 无退避的重试会在冲突时把系统推向雪崩（第 45 章）");
        Console.WriteLine("  → 且重试的是【整个事务】——包括第一次读，因为快照要重新取");
    }

    static void RunAll(int n, Action<int> body)
    {
        var threads = new Thread[n];
        for (int i = 0; i < n; i++) { int id = i; threads[i] = new Thread(() => body(id)); threads[i].Start(); }
        foreach (var t in threads) t.Join();
    }

    /// 把 (线程, 序号) 散列到行上——直接取模会让所有线程同步撞在同一行，"分散"就名不副实了
    static int Key(int w, int i, int rows) =>
        (int)((uint)(w * 2654435761u + (uint)i * 40503u) % rows);

    /// 悲观锁：每行一把锁，先锁再读（对应 SELECT ... FOR UPDATE）
    static (double ms, bool ok) RunPessimistic(int rowCount, int workers, int perWorker)
    {
        var rows = new Row[rowCount];
        var locks = new object[rowCount];
        for (int i = 0; i < rowCount; i++) { rows[i] = new Row(); locks[i] = new object(); }
        var sw = Stopwatch.StartNew();
        RunAll(workers, w =>
        {
            for (int i = 0; i < perWorker; i++)
            {
                int k = Key(w, i, rowCount);
                lock (locks[k])
                {
                    int read = rows[k].Value;
                    Thread.SpinWait(20);
                    rows[k].Value = read + 1;
                }
            }
        });
        double ms = sw.Elapsed.TotalMilliseconds;
        return (ms, rows.Sum(r => r.Value) == workers * perWorker);
    }

    /// 乐观锁：不持锁地读，写回时校验版本号，冲突则重试
    static (double ms, bool ok, int retries) RunOptimistic(int rowCount, int workers, int perWorker)
    {
        var rows = new Row[rowCount];
        var locks = new object[rowCount];
        for (int i = 0; i < rowCount; i++) { rows[i] = new Row(); locks[i] = new object(); }
        int retries = 0;
        var sw = Stopwatch.StartNew();
        RunAll(workers, w =>
        {
            for (int i = 0; i < perWorker; i++)
            {
                int k = Key(w, i, rowCount);
                while (true)
                {
                    int readVal, readVer;
                    lock (locks[k]) { readVal = rows[k].Value; readVer = rows[k].Version; }
                    Thread.SpinWait(20);                     // 「想一想」——【不持锁】
                    lock (locks[k])
                    {
                        // 对应 SQL: UPDATE t SET v=?, version=version+1 WHERE id=? AND version=?
                        if (rows[k].Version == readVer)      // 版本没变 = 期间没人改
                        {
                            rows[k].Value = readVal + 1;
                            rows[k].Version++;
                            break;
                        }
                    }
                    Interlocked.Increment(ref retries);       // 版本变了 → 整个操作重来
                }
            }
        });
        double ms = sw.Elapsed.TotalMilliseconds;
        return (ms, rows.Sum(r => r.Value) == workers * perWorker, retries);
    }
}
