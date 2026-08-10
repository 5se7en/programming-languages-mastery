using System;

struct PointS { public int X, Y; }     // 值类型：8 字节数据，没有对象头
class PointC { public int X, Y; }      // 引用类型：数据 + 对象头，住在堆上

class Program
{
    static void Main()
    {
        Console.WriteLine("== ① struct[] vs class[]：同样一百万个点，两种住法 ==");
        long before = GC.GetTotalMemory(true);
        var structs = new PointS[1_000_000];             // 一整块连续内存
        structs[0] = new PointS { X = 1, Y = 2 };
        long afterStruct = GC.GetTotalMemory(true);
        var classes = new PointC[1_000_000];
        for (int i = 0; i < classes.Length; i++) classes[i] = new PointC { X = i, Y = -i };
        long afterClass = GC.GetTotalMemory(true);
        Console.WriteLine($"PointS[]（数据内联在数组里）:        {(afterStruct - before) / 1024.0 / 1024:F1} MB");
        Console.WriteLine($"PointC[]（引用数组 + 百万个堆对象）: {(afterClass - afterStruct) / 1024.0 / 1024:F1} MB");
        GC.KeepAlive(structs);
        GC.KeepAlive(classes);

        Console.WriteLine("\n== ② stackalloc：真正在栈上的数组 ==");
        Span<int> onStack = stackalloc int[128];         // 栈上分配，方法返回即消失
        onStack[0] = 42;
        Console.WriteLine($"stackalloc int[128] 可用，onStack[0] = {onStack[0]}（无 GC 参与）");

        Console.WriteLine("\n== ③ 装箱：值类型被搬进堆 ==");
        long b1 = GC.GetTotalMemory(true);
        object[] boxes = new object[100_000];
        for (int i = 0; i < boxes.Length; i++) boxes[i] = i;   // 每次赋值装箱一个 int
        long b2 = GC.GetTotalMemory(true);
        Console.WriteLine($"10 万次装箱新增堆内存: {(b2 - b1) / 1024.0 / 1024:F1} MB"
                + "（引用数组 0.8 MB + 每个箱子 24 字节）");
        GC.KeepAlive(boxes);

        Console.WriteLine("\n== ④ C# 的栈溢出没有第二次机会 ==");
        Console.WriteLine("StackOverflowException 无法被 catch——CLR 直接终止进程");
        Console.WriteLine("（Java 能 catch StackOverflowError，C# 从 .NET 2.0 起不给机会）");
    }
}
