// 线程池：.NET 的池是【全局单例 + 自动调参】——你几乎不该自己建池。
using System.Diagnostics;

class Program
{
    static double Ms(Stopwatch sw) => sw.Elapsed.TotalMilliseconds;

    static void Main()
    {
        int cores = Environment.ProcessorCount;

        Console.WriteLine("== ① .NET 的线程池是全局的，且会自动调参 ==");
        ThreadPool.GetMinThreads(out int minW, out int minIo);
        ThreadPool.GetMaxThreads(out int maxW, out int maxIo);
        Console.WriteLine($"  本机核心数: {cores}");
        Console.WriteLine($"  工作线程 min/max: {minW} / {maxW}   （min 默认 = 核心数）");
        Console.WriteLine($"  I/O 线程 min/max: {minIo} / {maxIo}");
        Console.WriteLine($"  当前可用工作线程: {AvailableWorkers()}");
        Console.WriteLine("  → 与 Java 要你填 7 个参数不同，.NET 只有一个池，靠爬山算法自己调");

        Console.WriteLine("\n== ② 每任务一线程 vs 复用线程池 ==");
        const int N = 5000;
        int counter = 0;
        var sw = Stopwatch.StartNew();
        var threads = new Thread[N];
        for (int i = 0; i < N; i++)
        {
            threads[i] = new Thread(() => Interlocked.Increment(ref counter));
            threads[i].Start();
        }
        foreach (var t in threads) t.Join();
        double msRaw = Ms(sw);

        counter = 0;
        sw = Stopwatch.StartNew();
        var tasks = new Task[N];
        for (int i = 0; i < N; i++) tasks[i] = Task.Run(() => Interlocked.Increment(ref counter));
        Task.WaitAll(tasks);
        double msPool = Ms(sw);

        Console.WriteLine($"  {N} 个空任务，每任务新建线程: {msRaw:F0} ms（{msRaw * 1000 / N:F1} μs/个）");
        Console.WriteLine($"  {N} 个空任务，线程池 Task.Run : {msPool:F0} ms（{msPool * 1000 / N:F2} μs/个）");
        Console.WriteLine($"  → 池化快 {msRaw / msPool:F1}x —— 省下的是创建 + 1MB 栈 + 销毁（第 40 章实测）");

        Console.WriteLine("\n== ③ 线程注入速率：池不会瞬间变大（实测）==");
        int before = AvailableWorkers();
        var gate = new ManualResetEventSlim(false);
        var startTimes = new List<double>();
        var lockObj = new object();
        int blockers = minW + 6;                       // 比 min 多 6 个，逼池「注入」新线程
        var allDone = new CountdownEvent(blockers);
        sw = Stopwatch.StartNew();
        for (int i = 0; i < blockers; i++)
        {
            ThreadPool.QueueUserWorkItem(_ =>
            {
                lock (lockObj) { startTimes.Add(Ms(sw)); }
                gate.Wait(3000);                       // 占住线程，最多 3 秒
                allDone.Signal();
            });
        }
        Thread.Sleep(1500);
        double[] snapshot;
        int busyNow = AvailableWorkers();
        lock (lockObj) { snapshot = startTimes.OrderBy(x => x).ToArray(); }
        gate.Set();
        allDone.Wait(4000);

        Console.WriteLine($"  一次性提交 {blockers} 个「会阻塞」的任务（min={minW}）");
        Console.WriteLine($"  1.5 秒内真正开始执行的: {snapshot.Length} / {blockers} 个");
        Console.WriteLine("  各任务开始时刻(ms): " +
                          string.Join(", ", snapshot.Select(x => x.ToString("F0"))));
        if (snapshot.Length > minW)
            Console.WriteLine($"  → 前 {minW} 个（= min）立刻开始；第 {minW + 1} 个等到 {snapshot[minW]:F0} ms 才拿到线程");
        Console.WriteLine("  → 超过 min 之后，池大约每 500ms 才【注入】一条新线程（爬山算法要先观察吞吐）");
        Console.WriteLine("  → 这正是第 42 章「.Result 死锁」的成因：占满线程 + 注入太慢 = 假死几分钟");
        Console.WriteLine($"  阻塞期间可用工作线程从 {before} 降到 {busyNow}（差值 = 被占住的线程数）");

        Console.WriteLine("\n== ④ Task 的工作窃取：每条线程有自己的本地队列 ==");
        long Crunch(int maxPar)          // 同一段代码，只改并行度——避免拿「裸循环」和「lambda」比
        {
            long acc = 0;
            Parallel.For(0, 40, new ParallelOptions { MaxDegreeOfParallelism = maxPar }, i =>
            {
                long local = 0;
                for (long j = 0; j < 10_000_000; j++) local += j;
                Interlocked.Add(ref acc, local);
            });
            return acc;
        }
        Crunch(1);                        // 预热，让 JIT 先把这段编译好
        sw = Stopwatch.StartNew();
        long serial = Crunch(1);
        double msSer = Ms(sw);
        sw = Stopwatch.StartNew();
        long total = Crunch(-1);          // -1 = 不限并行度，交给调度器
        double msPar = Ms(sw);
        Console.WriteLine($"  40 块计算，并行度 1: {msSer:F0} ms；不限并行度: {msPar:F0} ms（加速 {msSer / msPar:F2}x）");
        Console.WriteLine($"  结果一致: {total == serial}");
        Console.WriteLine("  Task.Run 的任务先进【本地队列】（LIFO，缓存友好），空闲线程从别人队列尾部窃取");
        Console.WriteLine("  → 与 Java 的 ForkJoinPool 同一个算法，.NET 把它做成了默认调度器");

        Console.WriteLine("\n== ⑤ 什么时候【不】该用线程池 ==");
        bool poolThread = false, longRunThread = true;
        Task.Run(() => { poolThread = Thread.CurrentThread.IsThreadPoolThread; }).Wait();
        Task.Factory.StartNew(() => { longRunThread = Thread.CurrentThread.IsThreadPoolThread; },
            TaskCreationOptions.LongRunning).Wait();
        Console.WriteLine($"  Task.Run 里 IsThreadPoolThread = {poolThread}");
        Console.WriteLine($"  LongRunning 里 IsThreadPoolThread = {longRunThread}   ← 专门开了一条独立线程");
        Console.WriteLine("  → 长期占用的任务（消费者循环、监听器）必须用 LongRunning，否则会饿死整个池");

        Console.WriteLine("\n== ⑥ 池大小曲线：CPU 密集任务 ==");
        foreach (int size in new[] { 1, 2, 4, 8, cores, cores * 2, cores * 4 })
        {
            sw = Stopwatch.StartNew();
            long acc = 0;
            Parallel.For(0, 40, new ParallelOptions { MaxDegreeOfParallelism = size }, i =>
            {
                long local = 0;
                for (long j = 0; j < 10_000_000; j++) local += j;
                Interlocked.Add(ref acc, local);
            });
            string tag = size == cores ? "   ← 核心数" : (size > cores ? "   （超出核心数，收益归零）" : "");
            Console.WriteLine($"  并行度 {size,2}: {Ms(sw),6:F1} ms{tag}");
        }
        Console.WriteLine($"  → CPU 密集: 线程数 ≈ 核心数（本机 {cores}）");
        Console.WriteLine($"  → I/O 密集: 线程数 ≈ 核心数 × (1 + 等待/计算)，例 {cores} × 10 = {cores * 10} 条");

        Console.WriteLine("\n== ⑦ .NET 与 Java 的池哲学对比 ==");
        Console.WriteLine("  Java : ThreadPoolExecutor 七个参数全给你 —— 灵活，但配错就是生产事故");
        Console.WriteLine("  .NET : 一个全局池 + 爬山算法自动调 —— 省心，但调优手段少（只能改 min/max）");
        Console.WriteLine("  共同点: 队列都无界（.NET 的全局队列也没有上限）→ 反压要靠你自己（Channel/Semaphore）");
    }

    static int AvailableWorkers()
    {
        ThreadPool.GetAvailableThreads(out int w, out _);
        return w;
    }
}
