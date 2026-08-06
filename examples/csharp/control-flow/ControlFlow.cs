// 第 11 章 · 流程控制 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Collections.Generic;
using System.Linq;

class ControlFlow
{
    static string Grade(int score) =>
        score >= 90 ? "A" : score >= 60 ? "B" : "C";

    static void Main()
    {
        int[] scores = { 92, 75, 50 };

        // 1. 分支
        Console.WriteLine("分支: " + string.Join(" ", scores.Select(Grade)));

        // 2. switch 强制跳出：不写 break 直接编译报错
        string g = Grade(92);
        switch (g)
        {
            case "A": Console.WriteLine("switch: 优秀"); break;
            case "B": Console.WriteLine("switch: 及格"); break;
            default:  Console.WriteLine("switch: 不及格"); break;
        }

        // 3. switch 表达式 + 关系模式（C# 8+）
        string msg = scores[0] switch
        {
            >= 90 => "优秀",
            >= 60 => "及格",
            _     => "不及格"
        };
        Console.WriteLine($"switch 表达式（关系模式）: {msg}");

        // 4. foreach 遍历
        Console.Write("foreach: ");
        foreach (var s in scores) Console.Write(s + " ");
        Console.WriteLine();

        // 5. 卫语句
        Console.WriteLine("卫语句: " + Process(null) + " | " + Process("active"));
    }

    static string Process(string? user)
    {
        if (user is null) return "无用户";
        if (user != "active") return "未激活";
        return "处理完成";
    }
}
