using System;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

class Program
{
    static int sharedCounter = 0;                   // ⚠️ 非同步的共享变量
    static int atomicCounter = 0;
    static int globalMarker = 0;

    static void RaceWorker(int times)
    {
        for (int i = 0; i < times; i++) sharedCounter++;              // 读-改-写三步
    }

    static void AtomicWorker(int times)
    {
        for (int i = 0; i < times; i++) Interlocked.Increment(ref atomicCounter);
    }

    static long CpuTask(int n)
    {
        long total = 0;
        for (int i = 0; i < n; i++) total += (long)i * i;
        return total;
    }

    static void Main()
    {
        const int N = 1_000_000;

        Console.WriteLine("== ① 线程的身份与共享 ==");
        Console.WriteLine($"  当前线程 Id = {Environment.CurrentManagedThreadId}"
                + $"，处理器数 = {Environment.ProcessorCount}");
        var probe = new Thread(() =>
        {
            int localVar = 42;
            Console.WriteLine($"  子线程 Id = {Environment.CurrentManagedThreadId}"
                    + $"，看到 globalMarker = {globalMarker}（共享），局部变量 = {localVar}（独立）");
        });
        probe.Start();
        probe.Join();

        Console.WriteLine("\n== ② 钥匙实验：数据竞争 ==");
        for (int run = 1; run <= 3; run++)
        {
            sharedCounter = 0;
            var a = new Thread(() => RaceWorker(N));
            var b = new Thread(() => RaceWorker(N));
            a.Start(); b.Start(); a.Join(); b.Join();
            Console.WriteLine($"  第 {run} 次运行: 期望 {2 * N}，实际 {sharedCounter}"
                    + $"   （丢了 {2 * N - sharedCounter} 次）");
        }
        Console.WriteLine("  ↑ 每次都不一样——与 Java/C++ 同款结论");

        Console.WriteLine("\n== ③ Interlocked 修复 ==");
        for (int run = 1; run <= 3; run++)
        {
            atomicCounter = 0;
            var a = new Thread(() => AtomicWorker(N));
            var b = new Thread(() => AtomicWorker(N));
            a.Start(); b.Start(); a.Join(); b.Join();
            Console.WriteLine($"  第 {run} 次运行: 期望 {2 * N}，实际 {atomicCounter}   ✅");
        }

        Console.WriteLine("\n== ④ 真并行：CPU 密集任务的加速比 ==");
        const int M = 40_000_000;
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < 4; i++) CpuTask(M);
        double serial = sw.Elapsed.TotalMilliseconds;

        sw.Restart();
        var threads = new Thread[4];
        for (int i = 0; i < 4; i++) { threads[i] = new Thread(() => CpuTask(M)); threads[i].Start(); }
        foreach (var t in threads) t.Join();
        double parallel = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  串行 4 个任务: {serial,7:F0} ms");
        Console.WriteLine($"  4 线程并行:    {parallel,7:F0} ms");
        Console.WriteLine($"  加速比 = {serial / parallel:F2}x   <- 真并行");

        Console.WriteLine("\n== ⑤ 现代 C#：很少直接 new Thread ==");
        Console.WriteLine("  Task / async-await（第 42 章）与线程池（第 45 章）才是日常写法");
        var results = Parallel.For(0, 4, _ => CpuTask(M / 4));
        Console.WriteLine($"  Parallel.For 完成: IsCompleted = {results.IsCompleted}");
        Console.WriteLine("  （框架帮你分配线程、负载均衡——手写 Thread 只在需要精细控制时）");
    }
}
