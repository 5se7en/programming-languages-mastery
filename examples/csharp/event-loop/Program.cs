using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;

/// <summary>手工实现一个单线程事件循环——就是 JS/asyncio 那台引擎的骨架。</summary>
class SingleThreadLoop : SynchronizationContext
{
    private readonly BlockingCollection<(SendOrPostCallback cb, object? state)> _queue = new();

    public override void Post(SendOrPostCallback d, object? state) => _queue.Add((d, state));

    public void Run()
    {
        SetSynchronizationContext(this);
        foreach (var (cb, state) in _queue.GetConsumingEnumerable())
            cb(state);                                  // 取一个任务，执行到底
    }

    public void Stop() => _queue.CompleteAdding();
}

class Program
{
    static async Task Main()
    {
        Console.WriteLine("== ① .NET 没有内置事件循环——但有同步上下文 ==");
        Console.WriteLine($"  控制台程序的 SynchronizationContext = "
                + $"{SynchronizationContext.Current?.ToString() ?? "null（直接用线程池）"}");
        Console.WriteLine("  WinForms/WPF 有 UI 消息循环；ASP.NET Core 也没有（用线程池）");
        Console.WriteLine("  → 这就是为什么 C# 的 await 默认「回到原来的上下文」，而控制台里没有上下文");

        Console.WriteLine("\n== ② await 之后回到哪条线程 ==");
        int before = Environment.CurrentManagedThreadId;
        await Task.Delay(10);
        int after = Environment.CurrentManagedThreadId;
        Console.WriteLine($"  await 前线程 = {before}，await 后线程 = {after}"
                + $"（{(before == after ? "相同" : "不同")}）");
        Console.WriteLine("  控制台无同步上下文 → 续体在任意线程池线程上恢复");
        Console.WriteLine("  UI 程序有上下文 → 续体一定回到 UI 线程（这也是死锁的来源）");

        Console.WriteLine("\n== ③ 钥匙实验：手工搭一个事件循环 ==");
        var loop = new SingleThreadLoop();
        var loopThread = new Thread(loop.Run) { Name = "event-loop" };
        loopThread.Start();

        var order = new ConcurrentQueue<string>();
        var done = new TaskCompletionSource();

        loop.Post(_ =>
        {
            order.Enqueue($"1. 第一个任务（线程 {Environment.CurrentManagedThreadId}）");
            loop.Post(__ => order.Enqueue("3. 任务里排的新任务（排到队尾）"), null);
            order.Enqueue("2. 第一个任务的剩余部分（不可抢占，必须跑完）");
        }, null);
        loop.Post(_ => order.Enqueue("4. 第二个任务"), null);
        loop.Post(_ => { done.SetResult(); }, null);

        await done.Task;
        foreach (var line in order) Console.WriteLine($"    {line}");
        Console.WriteLine("  ↑ 「取一个任务 → 执行到底 → 取下一个」：与 JS/asyncio 同一台引擎");
        loop.Stop();
        loopThread.Join();

        Console.WriteLine("\n== ④ ConfigureAwait(false)：告诉运行时不必回原上下文 ==");
        Console.WriteLine("  库代码应该用 ConfigureAwait(false)：");
        Console.WriteLine("    ① 避免不必要的上下文切换（性能）");
        Console.WriteLine("    ② 避免 UI 线程死锁（调用方 .Result + 续体要回 UI 线程 → 互等）");
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < 1000; i++) await Task.Yield();
        Console.WriteLine($"  1000 次 Task.Yield（每次都重新入队）: {sw.Elapsed.TotalMilliseconds:F0} ms");

        Console.WriteLine("\n== ⑤ .NET 的选择：线程池而非单线程循环 ==");
        Console.WriteLine("  JS/asyncio: 一条线程 + 一个循环 → 天然无数据竞争，但吃不满多核");
        Console.WriteLine("  .NET:       线程池 + 工作窃取 → 能吃满多核，但要自己管同步（第 41 章）");
        Console.WriteLine($"  当前进程线程数 = {Process.GetCurrentProcess().Threads.Count}");
        Console.WriteLine("  → 两种模型各有代价：单线程省心但受限，多线程强大但危险");
    }
}
