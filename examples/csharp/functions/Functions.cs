// 第 12 章 · 函数 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Linq;

class Functions
{
    // 1. 默认参数 + 命名参数
    static string CreateUser(string name, int age = 18, bool active = true)
        => $"{name}/{age}/{active}";

    // 2. 默认按值传递；ref / out 才是引用传递
    static void ByValue(int x)     { x = 100; }
    static void ByRef(ref int x)   { x = 100; }
    static bool TryHalf(int x, out int result) { result = x / 2; return x % 2 == 0; }

    // 3. 传对象：改内容外部可见，重新赋值外部不变
    class Score { public int V; }
    static void Modify(Score s)   { s.V = 60; }
    static void Reassign(Score s) { s = new Score { V = 0 }; }

    static void Main()
    {
        Console.WriteLine("平均分: " + new[] { 92, 75, 50 }.Average());
        Console.WriteLine("命名参数: " + CreateUser("Alice", active: false));

        int n = 5;
        ByValue(n);     Console.WriteLine($"值传递后:   {n}   ← 没变");
        ByRef(ref n);   Console.WriteLine($"ref 传递后: {n} ← 变了！（调用处也要写 ref）");

        bool even = TryHalf(10, out int half);
        Console.WriteLine($"out 参数: half={half} even={even}");

        var s = new Score { V = 92 };
        Modify(s);   Console.WriteLine($"改内容后:   {s.V} ← 外部可见");
        Reassign(s); Console.WriteLine($"重新赋值后: {s.V} ← 外部没变");

        // 4. 闭包
        Func<int> MakeCounter() { int count = 0; return () => ++count; }
        var c = MakeCounter();
        c(); c();
        Console.WriteLine($"闭包计数器: {c()}");
    }
}
