using System;
using System.Diagnostics;
using System.Threading;

class Program
{
    static int counter = 0;
    static readonly object bigLock = new object();
    static readonly object lockA = new object();
    static readonly object lockB = new object();

    class Account
    {
        public string Name = "";
        public int Balance;
        public readonly object Lock = new object();
    }

    static void Main()
    {
        const int N = 200_000;

        Console.WriteLine("== ① lock 语句：C# 最常用的同步手段 ==");
        counter = 0;
        var sw = Stopwatch.StartNew();
        var t1 = new Thread(() => { for (int i = 0; i < N; i++) lock (bigLock) counter++; });
        var t2 = new Thread(() => { for (int i = 0; i < N; i++) lock (bigLock) counter++; });
        t1.Start(); t2.Start(); t1.Join(); t2.Join();
        double lockMs = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  加锁结果 = {counter}（期望 {2 * N}）✅，耗时 {lockMs:F1} ms");

        int atomic = 0;
        sw.Restart();
        var t3 = new Thread(() => { for (int i = 0; i < N; i++) Interlocked.Increment(ref atomic); });
        var t4 = new Thread(() => { for (int i = 0; i < N; i++) Interlocked.Increment(ref atomic); });
        t3.Start(); t4.Start(); t3.Join(); t4.Join();
        double atomMs = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  原子结果 = {atomic}，耗时 {atomMs:F1} ms");
        Console.WriteLine($"  锁比原子慢 {lockMs / atomMs:F1} 倍");
        Console.WriteLine("  （lock 语句实际是 Monitor.Enter/Exit + try-finally 的语法糖）");

        Console.WriteLine("\n== ② 钥匙实验：死锁（用 TryEnter 安全演示）==");
        var w1 = new Thread(() =>
        {
            lock (lockA)
            {
                Thread.Sleep(100);
                if (Monitor.TryEnter(lockB, TimeSpan.FromMilliseconds(500)))
                {
                    Console.WriteLine("  w1 拿到了两把锁"); Monitor.Exit(lockB);
                }
                else Console.WriteLine("  w1: 持有 A，等 B 超时——对方正持有 B");
            }
        });
        var w2 = new Thread(() =>
        {
            lock (lockB)                                  // ⚠️ 顺序相反
            {
                Thread.Sleep(100);
                if (Monitor.TryEnter(lockA, TimeSpan.FromMilliseconds(500)))
                {
                    Console.WriteLine("  w2 拿到了两把锁"); Monitor.Exit(lockA);
                }
                else Console.WriteLine("  w2: 持有 B，等 A 超时——对方正持有 A");
            }
        });
        w1.Start(); w2.Start(); w1.Join(); w2.Join();
        Console.WriteLine("  （把 TryEnter 换成 lock 就是真死锁——.NET 无内置死锁检测）");

        Console.WriteLine("\n== ③ 破解：全局锁顺序 ==");
        var a = new Account { Name = "A", Balance = 1000 };
        var b = new Account { Name = "B", Balance = 1000 };
        var x = new Thread(() => { for (int i = 0; i < 1000; i++) Transfer(a, b, 1); });
        var y = new Thread(() => { for (int i = 0; i < 1000; i++) Transfer(b, a, 1); });
        x.Start(); y.Start(); x.Join(); y.Join();
        Console.WriteLine($"  双向转账后 A={a.Balance}, B={b.Balance}，总额 = {a.Balance + b.Balance}（守恒 ✅）");

        Console.WriteLine("\n== ④ C# 的锁家族 ==");
        Console.WriteLine("  lock/Monitor      : 最常用，可重入");
        Console.WriteLine("  SemaphoreSlim     : 限流 + 支持 async（await 时不占线程）");
        Console.WriteLine("  ReaderWriterLockSlim: 读多写少场景（多读并行，写独占）");
        Console.WriteLine("  Interlocked       : 单变量原子操作（最轻）");
        Console.WriteLine("  ⚠️ lock 不能跨 await —— 异步场景要用 SemaphoreSlim（第 42 章）");

        Console.WriteLine("\n== ⑤ 读写锁演示 ==");
        var rw = new ReaderWriterLockSlim();
        int shared = 0;
        var readers = new Thread[4];
        for (int i = 0; i < 4; i++)
        {
            readers[i] = new Thread(() =>
            {
                rw.EnterReadLock();
                try { _ = shared; } finally { rw.ExitReadLock(); }
            });
            readers[i].Start();
        }
        foreach (var r in readers) r.Join();
        rw.EnterWriteLock();
        try { shared = 42; } finally { rw.ExitWriteLock(); }
        Console.WriteLine($"  4 个读者并行读取、1 个写者独占写入，最终值 = {shared}");
    }

    static void Transfer(Account from, Account to, int amount)
    {
        // 按名字排序取锁 —— 破坏「循环等待」
        Account first = string.CompareOrdinal(from.Name, to.Name) < 0 ? from : to;
        Account second = ReferenceEquals(first, from) ? to : from;
        lock (first.Lock)
            lock (second.Lock)
            {
                from.Balance -= amount;
                to.Balance += amount;
            }
    }
}
