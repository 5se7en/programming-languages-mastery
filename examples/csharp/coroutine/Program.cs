using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading.Tasks;

class Program
{
    // ① 迭代器方法：C# 的生成器（编译器改写成状态机——与 async 同源）
    static IEnumerable<string> Counter(string name, int n)
    {
        int total = 0;
        for (int i = 0; i < n; i++)
        {
            total += i;
            yield return $"{name}: 第 {i} 步，累计 {total}";
        }
    }

    // ③ 手工协程调度器
    static List<string> Scheduler(params IEnumerator<string>[] tasks)
    {
        var queue = new Queue<IEnumerator<string>>(tasks);
        var trace = new List<string>();
        while (queue.Count > 0)
        {
            var task = queue.Dequeue();
            if (task.MoveNext())               // 恢复它，跑到下一个 yield return
            {
                trace.Add(task.Current);
                queue.Enqueue(task);            // 排回队尾
            }
        }
        return trace;
    }

    static async Task Main()
    {
        Console.WriteLine("== ① yield return：C# 的生成器 ==");
        var gen = Counter("A", 3).GetEnumerator();
        Console.WriteLine($"  调用迭代器方法返回: {gen.GetType().Name}（函数体一行都没执行）");
        gen.MoveNext();
        Console.WriteLine($"  第一次 MoveNext(): {gen.Current}");
        gen.MoveNext();
        Console.WriteLine($"  第二次 MoveNext(): {gen.Current}   ← 从上次 yield 的下一行继续");
        Console.WriteLine($"  它的真身: {gen.GetType().FullName}");
        Console.WriteLine("  ↑ 编译器生成的状态机类——局部变量成了它的字段（住在堆上，第 32 章）");

        Console.WriteLine("\n== ② 与 async 状态机同源 ==");
        Console.WriteLine("  yield return  → 编译器生成 IEnumerator 状态机（MoveNext 里的 switch）");
        Console.WriteLine("  await         → 编译器生成 IAsyncStateMachine（同样是 MoveNext）");
        Console.WriteLine("  → 第 42 章实测过 <ShowStateMachine>d__4 —— 两者是同一套编译器机制");
        Console.WriteLine("  → C# 2.0（2005）的迭代器，为 C# 5.0（2012）的 async 铺好了路");

        Console.WriteLine("\n== ③ 钥匙实验：三十行搭一个协程调度器 ==");
        foreach (var line in Scheduler(Counter("协程甲", 3).GetEnumerator(),
                                       Counter("协程乙", 2).GetEnumerator()))
            Console.WriteLine($"    {line}");
        Console.WriteLine("  ↑ 两个协程交替推进——单线程上实现了「并发」，且完全没有锁");

        Console.WriteLine("\n== ④ 规模实验：协程的内存代价 ==");
        const int N = 100_000;
        long before = GC.GetTotalMemory(true);
        var gens = Enumerable.Range(0, N).Select(i => Counter($"g{i}", 1000).GetEnumerator()).ToArray();
        foreach (var g in gens) g.MoveNext();          // 启动每一个，让状态真的分配
        long after = GC.GetTotalMemory(true);
        double kb = (after - before) / 1024.0 / N;
        Console.WriteLine($"  {N} 个暂停中的迭代器: 占用 {(after - before) / 1024.0 / 1024:F1} MB");
        Console.WriteLine($"  平均每个约 {kb:F2} KB —— 而一条 OS 线程要 1024 KB（第 31/39 章）");
        Console.WriteLine($"  → 相差约 {Math.Round(1024 / kb)} 倍");
        GC.KeepAlive(gens);

        Console.WriteLine("\n== ⑤ 异步迭代器：yield + await 的合体（C# 8）==");
        var got = new List<int>();
        await foreach (var v in Ticker(3)) got.Add(v);
        Console.WriteLine($"  await foreach 收到: [{string.Join(", ", got)}]");
        Console.WriteLine("  → IAsyncEnumerable<T>：流式处理的标准写法（对应 JS 的 async function*）");

        Console.WriteLine("\n== ⑥ C# 的协程家族 ==");
        Console.WriteLine("  yield return      : 同步生成器（无栈协程，本节主角）");
        Console.WriteLine("  async/await       : 异步状态机（第 42 章）");
        Console.WriteLine("  IAsyncEnumerable  : 异步生成器（本节 ⑤）");
        Console.WriteLine("  ❌ 没有有栈协程：不能在任意深度让出（Unity 的 Coroutine 是用迭代器模拟的）");

        Console.WriteLine("\n== ⑦ Unity 的协程：迭代器的经典应用 ==");
        Console.WriteLine("  IEnumerator MyCoroutine() {");
        Console.WriteLine("      yield return new WaitForSeconds(1);   // 暂停 1 秒");
        Console.WriteLine("      yield return null;                    // 暂停到下一帧");
        Console.WriteLine("  }");
        Console.WriteLine("  → 游戏引擎每帧调一次 MoveNext()，就实现了「跨帧的执行流」");
        Console.WriteLine("  → 这正是本节 ③ 手工调度器的商业级版本");
    }

    static async IAsyncEnumerable<int> Ticker(int n)
    {
        for (int i = 1; i <= n; i++)
        {
            await Task.Delay(5);
            yield return i;
        }
    }
}
