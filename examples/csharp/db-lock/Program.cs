// 数据库锁：锁粒度如何决定吞吐——「表锁 vs 行锁」这道题的量化答案。
using System.Diagnostics;

class Program
{
    const int ROWS = 4096;
    const int OPS_PER_THREAD = 20_000;

    static void Main()
    {
        Console.WriteLine("== ① 锁粒度对吞吐的影响（实测）==");
        Console.WriteLine($"  {ROWS} 行数据，每个线程做 {OPS_PER_THREAD} 次「读-改-写」，随机挑行");
        Console.WriteLine("  三种粒度: 表锁（1 把）、条带锁（16 把）、行锁（每行 1 把）");
        Console.WriteLine();
        Console.WriteLine("  线程数     表锁(1把)    条带锁(16把)     行锁(4096把)   行锁 vs 表锁");

        foreach (int threads in new[] { 1, 2, 4, 8 })
        {
            double table = Run(threads, 1);
            double striped = Run(threads, 16);
            double row = Run(threads, ROWS);
            Console.WriteLine($"  {threads,4}     {table,8:F0} ms    {striped,10:F0} ms    {row,10:F0} ms"
                              + $"     {table / row,6:F2}x");
        }
        Console.WriteLine("  → 单线程时三者几乎一样（没有竞争，加锁只是几十纳秒的开销）");
        Console.WriteLine("  → 线程越多，粗粒度锁的劣势越明显: 表锁把所有并发【强制串行化】");
        Console.WriteLine("  → 这就是数据库拼命做【行级锁】的原因: 粒度是并发度的上限");

        Console.WriteLine("\n== ② 但粒度细不是免费的 ==");
        long rowLockBytes = (long)ROWS * 8;          // 粗略：每把锁一个对象引用
        Console.WriteLine($"  {ROWS} 行 → {ROWS} 把锁，仅引用就要 {rowLockBytes / 1024.0:F1} KB");
        Console.WriteLine("  一张千万行的表若每行一把锁 → 锁对象本身就要几百 MB");
        Console.WriteLine("  → 所以数据库有【锁升级】(lock escalation): 一个事务锁的行太多时，");
        Console.WriteLine("     自动把成千上万把行锁替换成【一把表锁】——省内存，但并发瞬间归零");
        Console.WriteLine("  → SQL Server 的阈值约 5000 行；InnoDB 不做升级，改用位图压缩锁信息");
        Console.WriteLine("  → 「批量 UPDATE 突然把系统卡死」的常见原因就是锁升级");

        Console.WriteLine("\n== ③ 条带锁：工程上的折中（实测在 ① 里）==");
        Console.WriteLine("  16 把锁覆盖 4096 行，靠 hash(rowId) % 16 分配");
        Console.WriteLine("  → 内存只有行锁的 1/256，并发度却已接近行锁");
        Console.WriteLine("  → 代价: 【假冲突】——两行碰巧落在同一条带上就会互相等待");
        Console.WriteLine("  → Java 的 ConcurrentHashMap(JDK7)、各类分片计数器用的都是这个思路");

        Console.WriteLine("\n== ④ 读写锁：让「读读并发」真正生效 ==");
        var rw = new ReaderWriterLockSlim();
        int shared = 0;
        var sw = Stopwatch.StartNew();
        RunAll(8, _ =>
        {
            for (int i = 0; i < OPS_PER_THREAD; i++)
            {
                if (i % 10 == 0)                       // 10% 写
                {
                    rw.EnterWriteLock();
                    try { shared++; } finally { rw.ExitWriteLock(); }
                }
                else                                    // 90% 读
                {
                    rw.EnterReadLock();
                    try { _ = shared; } finally { rw.ExitReadLock(); }
                }
            }
        });
        double msRw = sw.Elapsed.TotalMilliseconds;

        var plain = new object();
        int shared2 = 0;
        sw = Stopwatch.StartNew();
        RunAll(8, _ =>
        {
            for (int i = 0; i < OPS_PER_THREAD; i++)
                lock (plain) { if (i % 10 == 0) shared2++; else _ = shared2; }
        });
        double msPlain = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  8 线程 × {OPS_PER_THREAD} 次操作（90% 读 / 10% 写）:");
        Console.WriteLine($"    普通互斥锁:   {msPlain,7:F0} ms");
        Console.WriteLine($"    读写锁:       {msRw,7:F0} ms"
                          + $"（{(msRw < msPlain ? $"快 {msPlain / msRw:F2}x" : $"慢 {msRw / msPlain:F2}x")}）");
        Console.WriteLine("  → 对应数据库的 S 锁 / X 锁: 读读相容，所以读多写少时读写锁应该赢");
        Console.WriteLine("  ⚠️ 但读写锁自身的记账开销不小，临界区极短时反而更慢——本次实测就能看出来");
        Console.WriteLine("  → 数据库绕开了这个问题: MVCC 让读【根本不加锁】（第 48 章），比读写锁更彻底");

        Console.WriteLine("\n== ⑤ 锁等待与死锁的处理策略对比 ==");
        Console.WriteLine("  ┌────────────┬──────────────────────┬────────────────────────┐");
        Console.WriteLine("  │ 系统        │ 死锁怎么办            │ 你需要做什么             │");
        Console.WriteLine("  ├────────────┼──────────────────────┼────────────────────────┤");
        Console.WriteLine("  │ 你的进程内  │ 永久挂起（第 41 章）  │ 自己保证加锁顺序         │");
        Console.WriteLine("  │ InnoDB     │ 自动检测 + 回滚一方   │ 捕获错误并【重试整个事务】│");
        Console.WriteLine("  │ PostgreSQL │ 自动检测 + 回滚一方   │ 同上（deadlock_timeout） │");
        Console.WriteLine("  │ SQLite     │ 不会死锁（单写者）    │ 处理 SQLITE_BUSY 并重试  │");
        Console.WriteLine("  └────────────┴──────────────────────┴────────────────────────┘");
        Console.WriteLine("  → 数据库比你的进程强的地方: 它【能回滚】，所以敢让死锁发生再解决");
        Console.WriteLine("  → 而进程内的锁没有回滚机制，死锁只能靠预防（第 41 章的结论）");

        Console.WriteLine("\n== ⑥ .NET 侧的实用提示 ==");
        Console.WriteLine("  EF Core 加锁: 只能走原生 SQL —— FromSqlRaw(\"SELECT ... FOR UPDATE\")");
        Console.WriteLine("  捕获死锁: SqlException.Number == 1205（SQL Server）/ PostgresException.SqlState == \"40P01\"");
        Console.WriteLine("  ⚠️ 重试必须重试【整个事务】，包括第一次读——因为快照要重新取（第 48 章）");
        Console.WriteLine("  → 首选方案仍是: 固定加锁顺序，把死锁概率降到接近零");
    }

    /// 用 lockCount 把锁覆盖 ROWS 行；lockCount=1 是表锁，=ROWS 是行锁
    static double Run(int threads, int lockCount)
    {
        var data = new int[ROWS];
        var locks = new object[lockCount];
        for (int i = 0; i < lockCount; i++) locks[i] = new object();
        var sw = Stopwatch.StartNew();
        RunAll(threads, w =>
        {
            for (int i = 0; i < OPS_PER_THREAD; i++)
            {
                int row = (int)((uint)(w * 2654435761u + (uint)i * 40503u) % ROWS);
                lock (locks[row % lockCount])
                {
                    int v = data[row];
                    Thread.SpinWait(10);            // 模拟「处理这一行」的耗时
                    data[row] = v + 1;
                }
            }
        });
        return sw.Elapsed.TotalMilliseconds;
    }

    static void RunAll(int n, Action<int> body)
    {
        var ts = new Thread[n];
        for (int i = 0; i < n; i++) { int id = i; ts[i] = new Thread(() => body(id)); ts[i].Start(); }
        foreach (var t in ts) t.Join();
    }
}
