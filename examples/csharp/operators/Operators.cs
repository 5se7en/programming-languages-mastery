// 第 10 章 · 运算符 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;

struct Score
{
    public int V;
    public static Score operator +(Score a, Score b) => new Score { V = a.V + b.V };
}

class Operators
{
    static bool Boom() { Console.WriteLine("   ← 这行不该出现！"); return true; }

    static void Main()
    {
        // 1. string 的 == 比较内容
        string s1 = "hi", s2 = "hi";
        Console.WriteLine($"string s1 == s2 → {s1 == s2} (C# 为 string 重载了 ==)");

        // 2. 作为 object 比较时按引用
        object o1 = new string("hi".ToCharArray());
        object o2 = new string("hi".ToCharArray());
        Console.WriteLine($"object o1 == o2 → {o1 == o2} ← 比较引用");
        Console.WriteLine($"ReferenceEquals → {ReferenceEquals(o1, o2)}");

        // 3. 运算符重载
        Score s = new Score { V = 90 } + new Score { V = 5 };
        Console.WriteLine($"重载 + → {s.V}");

        // 4. 短路求值
        Console.WriteLine($"false && Boom() → {false && Boom()}");

        // 5. 空合并与可选链
        int? maybe = null;
        Console.WriteLine($"maybe ?? 8080 → {maybe ?? 8080}");
        string? name = null;
        Console.WriteLine($"name?.Length → {(name?.Length.ToString() ?? "null")}");
    }
}
