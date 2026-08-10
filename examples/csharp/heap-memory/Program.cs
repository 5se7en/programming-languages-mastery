using System;
using System.Diagnostics;

class Student
{
    public string Name = "s";
    public int Score;
    public Student(int score) { Score = score; }
}

class Program
{
    static void Main()
    {
        Console.WriteLine("== ① 大对象堆（LOH）：85 KB 是一条国界线 ==");
        var small = new byte[84_000];
        var large = new byte[86_000];
        Console.WriteLine($"new byte[84000] 出生在第 {GC.GetGeneration(small)} 代（普通小对象堆）");
        Console.WriteLine($"new byte[86000] 出生在第 {GC.GetGeneration(large)} 代（直接进 LOH，按第 2 代对待）");
        Console.WriteLine("（LOH 默认不压缩——大对象搬家太贵，代价是它会碎片化）");

        Console.WriteLine("\n== ② 托管堆的分配有多快（一千万个对象） ==");
        for (int r = 0; r < 3; r++) Allocate(10_000_000);          // 预热
        var sw = Stopwatch.StartNew();
        long sum = Allocate(10_000_000);
        sw.Stop();
        Console.WriteLine($"总耗时 {sw.Elapsed.TotalMilliseconds:F1} ms，"
                + $"平均每个对象约 {sw.Elapsed.TotalMilliseconds * 1e6 / 10_000_000:F1} ns");
        if (sum == 42) Console.WriteLine();

        Console.WriteLine("\n== ③ 分配在悄悄触发回收：GC 计数器 ==");
        int gen0Before = GC.CollectionCount(0);
        Allocate(10_000_000);
        int gen0After = GC.CollectionCount(0);
        Console.WriteLine($"再分配一千万个临时对象，第 0 代 GC 发生了 {gen0After - gen0Before} 次");
        Console.WriteLine("（分配的账单没有消失——它变成了后台的回收工作，第 36 章详述）");
    }

    static long Allocate(int n)
    {
        long sum = 0;
        for (int i = 0; i < n; i++)
        {
            var s = new Student(i);
            sum += s.Score;
        }
        return sum;
    }
}
