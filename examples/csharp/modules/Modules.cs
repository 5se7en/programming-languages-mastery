// 第 14 章 · 模块 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Linq;
using Utils = Company.Project.Utils;      // 命名空间别名
using static System.Math;                 // 静态导入

namespace Company.Project.Utils
{
    public class MathUtil                  // public：所有程序集可见
    {
        public const int MaxScore = 100;
        public static double Average(int[] scores) => scores.Length == 0 ? 0 : scores.Average();
    }

    internal class Helper                  // internal：仅当前程序集可见
    {
        public static string Info() => "internal：只有本程序集能访问";
    }
}

class Program
{
    static void Main()
    {
        int[] scores = { 92, 75, 50 };

        // 1. 通过完整命名空间访问
        Console.WriteLine($"完整限定: {Company.Project.Utils.MathUtil.Average(scores):F2}");

        // 2. 通过别名访问
        Console.WriteLine($"命名空间别名: {Utils.MathUtil.MaxScore}");

        // 3. internal 在同一程序集内可见
        Console.WriteLine($"同程序集: {Utils.Helper.Info()}");

        // 4. 静态导入后可直接调用
        Console.WriteLine($"静态导入 Max(3,7) = {Max(3, 7)}");

        Console.WriteLine("C# 中命名空间管「命名」，程序集管「封装」——两者分离");
    }
}
