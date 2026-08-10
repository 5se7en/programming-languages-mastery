using System;

class StudentC { public string Name = ""; public int Score; }
struct StudentS { public int Score; }

class Program
{
    static void SwapPlain(int a, int b) { (a, b) = (b, a); }        // 按值：换不成
    static void SwapRef(ref int a, ref int b) { (a, b) = (b, a); }  // ref：真按引用传递

    static void Divide(int x, int y, out int quo, out int rem)      // out：多返回值
    {
        quo = x / y; rem = x % y;
    }

    static int SumIn(in StudentS s) => s.Score;                     // in：只读引用，大 struct 免拷贝

    static void MutateClass(StudentC s) => s.Score = 100;
    static void MutateStruct(StudentS s) => s.Score = 100;
    static void MutateStructRef(ref StudentS s) => s.Score = 100;

    static void Main()
    {
        Console.WriteLine("== ① swap 测试：C# 是唯一能选「真按引用传递」的托管语言 ==");
        int x = 1, y = 2;
        SwapPlain(x, y);
        Console.WriteLine($"SwapPlain(x, y):     x = {x}, y = {y}   <- 没换成（按值）");
        SwapRef(ref x, ref y);
        Console.WriteLine($"SwapRef(ref x, ref y): x = {x}, y = {y}   <- 成功！ref = C++ 的 &");

        Console.WriteLine("\n== ② out：把「多返回值」做成语法 ==");
        Divide(17, 5, out int quo, out int rem);
        Console.WriteLine($"Divide(17, 5): 商 = {quo}, 余 = {rem}");

        Console.WriteLine("\n== ③ struct/class × 传参方式 = 四种行为 ==");
        var sc = new StudentC { Score = 1 };
        var ss = new StudentS { Score = 1 };
        MutateClass(sc);
        Console.WriteLine($"class  按值传:      Score = {sc.Score}   <- 穿透（复制的是引用）");
        MutateStruct(ss);
        Console.WriteLine($"struct 按值传:      Score = {ss.Score}     <- 不穿透（复制的是整个值）");
        MutateStructRef(ref ss);
        Console.WriteLine($"struct 加 ref 传:   Score = {ss.Score}   <- 穿透（真引用）");
        Console.WriteLine($"in 只读引用求和:    {SumIn(in ss)}（不拷贝也不许改——大 struct 高效只读）");

        Console.WriteLine("\n== ④ 一张表说清 C# 的选择权 ==");
        Console.WriteLine("类型层: struct=值 / class=引用（声明时定，第 31 章）");
        Console.WriteLine("传参层: 默认按值 / ref 可读写 / out 必写 / in 只读（调用处也要写关键字——意图双向确认）");
    }
}
