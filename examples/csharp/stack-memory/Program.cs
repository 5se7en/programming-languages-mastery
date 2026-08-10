using System;
using System.Diagnostics;
using System.Threading.Tasks;

class Program
{
    static void Level3()
    {
        var trace = new StackTrace(fNeedFileInfo: false);
        Console.WriteLine("StackTrace 看到的调用链（栈顶在前）:");
        var frames = trace.GetFrames();
        for (int i = 0; i < Math.Min(4, frames.Length); i++)
        {
            Console.WriteLine($"  {frames[i].GetMethod()?.Name}");
        }
    }

    static void Level2() => Level3();
    static void Level1() => Level2();

    static async Task AsyncMethod()
    {
        await Task.Delay(1);
        // await 之后：这段代码运行在状态机的 MoveNext 里，原来的调用栈早已解开
        var top = new StackTrace().GetFrame(0)?.GetMethod();
        Console.WriteLine($"await 之后栈顶方法: {top?.Name}   <- 不是 AsyncMethod！");
        Console.WriteLine($"它属于类型: {top?.DeclaringType?.Name}");
    }

    static void Main()
    {
        Console.WriteLine("== ① CLR 的调用栈：StackTrace 实测 ==");
        Level1();

        Console.WriteLine("\n== ② async 方法的\"栈\"不是栈 ==");
        AsyncMethod().GetAwaiter().GetResult();
        Console.WriteLine("（局部变量被编译器搬进堆上的状态机对象——方法暂停时栈帧已还）");

        Console.WriteLine("\n== ③ struct 参数按值入栈：修改不影响调用方 ==");
        var p = new Point { X = 1 };
        Mutate(p);
        Console.WriteLine($"Mutate(p) 之后 p.X = {p.X}   <- 传的是栈上的副本");
    }

    struct Point { public int X; }
    static void Mutate(Point q) => q.X = 99;
}
