using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;

class Program
{
    const int IoDelay = 50;
    const int Tasks = 20;

    static async Task<int> AsyncIo(int n)
    {
        await Task.Delay(IoDelay);              // 异步等待：不占线程
        return n;
    }

    static int BlockingIo(int n)
    {
        Thread.Sleep(IoDelay);                  // 阻塞等待：占住线程
        return n;
    }

    [MethodImpl(MethodImplOptions.NoInlining)]
    static async Task ShowStateMachine()
    {
        Console.WriteLine($"  await 之前，栈顶方法 = {new StackTrace().GetFrame(0)?.GetMethod()?.Name}");
        await Task.Delay(1);
        var top = new StackTrace().GetFrame(0)?.GetMethod();
        Console.WriteLine($"  await 之后，栈顶方法 = {top?.Name}（类型 {top?.DeclaringType?.Name}）");
    }

    static async Task Main()
    {
        Console.WriteLine("== ① 钥匙实验：三种做法 ==");
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < Tasks; i++) BlockingIo(i);
        double serialMs = sw.Elapsed.TotalMilliseconds;

        sw.Restart();
        var threads = Enumerable.Range(0, Tasks)
            .Select(i => { var t = new Thread(() => BlockingIo(i)); t.Start(); return t; }).ToArray();
        foreach (var t in threads) t.Join();
        double threadMs = sw.Elapsed.TotalMilliseconds;

        sw.Restart();
        var results = await Task.WhenAll(Enumerable.Range(0, Tasks).Select(AsyncIo));
        double asyncMs = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"  串行 {Tasks} 个 I/O:    {serialMs,7:F0} ms");
        Console.WriteLine($"  {Tasks} 个线程并发:     {threadMs,7:F0} ms（加速比 {serialMs / threadMs:F1}x）");
        Console.WriteLine($"  async 并发:          {asyncMs,7:F0} ms（加速比 {serialMs / asyncMs:F1}x）");
        Console.WriteLine($"  结果正确: {results.SequenceEqual(Enumerable.Range(0, Tasks))}");

        Console.WriteLine("\n== ② 规模：一万并发也不用一万个线程 ==");
        const int Big = 10_000;
        int before = Process.GetCurrentProcess().Threads.Count;
        sw.Restart();
        await Task.WhenAll(Enumerable.Range(0, Big).Select(_ => Task.Delay(10)));
        Console.WriteLine($"  {Big} 个并发任务: {sw.Elapsed.TotalMilliseconds:F0} ms");
        Console.WriteLine($"  线程数 {before} → {Process.GetCurrentProcess().Threads.Count}（远小于 {Big}）");

        Console.WriteLine("\n== ③ await 之后，栈帧去哪了（第 32 章的兑现）==");
        await ShowStateMachine();
        Console.WriteLine("  → 编译器把 async 方法改写成状态机对象，局部变量成了它的字段（住在堆上）");
        Console.WriteLine("  → 原来的栈帧在 await 时已正常弹出（第 32 章实测过 MoveNext）");

        Console.WriteLine("\n== ④ 阻塞异步 = 线程饥饿 ==");
        const int Jobs = 200;
        sw.Restart();
        await Task.WhenAll(Enumerable.Range(0, Jobs).Select(AsyncIo));
        double goodMs = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        await Task.WhenAll(Enumerable.Range(0, Jobs)
            .Select(i => Task.Run(() => AsyncIo(i).Result)));   // ⚠️ 每个任务占住一条池线程
        double badMs = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"  {Jobs} 个任务全程 await:  {goodMs,6:F0} ms ✅（等待完全不占线程）");
        Console.WriteLine($"  {Jobs} 个任务用 .Result:  {badMs,6:F0} ms ❌（每个都占住一条池线程）");
        Console.WriteLine($"  慢了 {badMs / goodMs:F1} 倍——线程池被迫扩容，而扩容是有节流的");
        Console.WriteLine("  ⚠️ 本章开发时实测：若把线程池上限掐到 4 条再跑 24 个 .Result 任务，");
        Console.WriteLine("     程序会直接死锁——4 条线程全在阻塞，await 的续体没有线程可恢复");
        Console.WriteLine("  → 铁律：async all the way，绝不 .Result / .Wait()");

        Console.WriteLine("\n== ⑤ Task 不等于线程 ==");
        Console.WriteLine($"  Task.Delay 期间不占任何线程——等待由操作系统的 I/O 完成端口驱动");
        Console.WriteLine($"  Task.Run 才会借用线程池的线程（第 45 章）");
        Console.WriteLine($"  ValueTask：同步完成时零分配（热路径优化）");

        Console.WriteLine("\n== ⑥ 取消与超时：CancellationToken ==");
        using var cts = new CancellationTokenSource(30);
        try
        {
            await Task.Delay(5000, cts.Token);
        }
        catch (OperationCanceledException)
        {
            Console.WriteLine("  30 ms 后取消了一个 5 秒的等待 ✅（协作式取消，不是强杀线程）");
        }
    }
}
