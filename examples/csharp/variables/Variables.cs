// 第 08 章 · 变量 — C# 示例
// 运行：dotnet run（需 .NET 8+）
using System;

class Variables
{
    static void Main()
    {
        // 1. 声明
        string studentName = "Alice";
        const int MaxScore = 100;          // 编译期常量
        int age = 20;
        var score = 92;                    // 类型推导
        Console.WriteLine($"{studentName} {age} {score} {MaxScore}");

        // 2. 值类型：复制
        int a = 92;
        int b = a;
        b = 60;
        Console.WriteLine($"值类型 复制: {a} {b}");        // 92 60

        // 3. 引用类型：复制引用
        int[] s1 = { 92 };
        int[] s2 = s1;
        s2[0] = 60;
        Console.WriteLine($"引用类型 引用复制: {s1[0]}");   // 60
    }
}
