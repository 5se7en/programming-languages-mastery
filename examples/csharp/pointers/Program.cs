using System;

class Program
{
    static unsafe void Main()
    {
        Console.WriteLine("== ① C#：托管语言里唯一保留完整指针语法的（unsafe） ==");
        int x = 42;
        int* p = &x;                       // 与 C++ 一模一样的取地址
        *p = 99;                           // 解引用改写
        Console.WriteLine($"int x = 42; *(&x) = 99 之后，x = {x}");
        Console.WriteLine($"指针本身的值（栈上地址）: 0x{(ulong)p:x}");

        Console.WriteLine("\n== ② 指针算术：步长同样由类型决定 ==");
        int[] arr = { 10, 20, 30, 40 };
        fixed (int* pa = arr)              // fixed：钉住数组，GC 暂时不许搬它
        {
            Console.WriteLine($"pa[0]={pa[0]}, *(pa+2)={*(pa + 2)}   <- 和 C++ 同一套算术");
            Console.WriteLine($"int* +1 步进 {(byte*)(pa + 1) - (byte*)pa} 字节");
        }
        Console.WriteLine("（fixed 的存在泄露了天机：平时 GC 会搬对象——第 33 章的压缩整理）");

        Console.WriteLine("\n== ③ 中间态：Span<T>——有指针的效率，没指针的事故 ==");
        Span<int> span = stackalloc int[4] { 1, 2, 3, 4 };
        Span<int> tail = span.Slice(2);    // 切片 = 指针 + 长度，越界会被检查
        tail[0] = 99;
        Console.WriteLine($"span 经切片改写: [{span[0]}, {span[1]}, {span[2]}, {span[3]}]");
        Console.WriteLine("（Span 带边界检查——tail[5] 会抛异常而不是踩到别人的内存）");

        Console.WriteLine("\n== ④ 平时的 C#：引用 + 空安全语法 ==");
        string? maybe = null;
        Console.WriteLine($"maybe?.Length ?? -1 = {maybe?.Length ?? -1}   <- ?. 是「防空指针」进语法");
        Console.WriteLine("（NullReferenceException 就是被驯化的空指针事故——可捕获、带栈回溯）");
    }
}
