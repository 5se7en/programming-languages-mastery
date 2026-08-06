// 第 17 章 · 列表 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Collections.Generic;
using System.Diagnostics;

class Lists
{
    static void Main()
    {
        // 1. C# 暴露 Capacity（与 Java 不同）—— 直接观察扩容
        var list = new List<int>();
        int last = 0;
        Console.WriteLine("List<T> 追加时的 Capacity 变化:");
        for (int i = 1; i <= 70; i++)
        {
            list.Add(i);
            if (list.Capacity != last)
            {
                Console.Write($"  Count={list.Count} → Capacity={list.Capacity}");
                if (last > 0) Console.Write($"   增长倍数 {(double)list.Capacity / last}");
                Console.WriteLine();
                last = list.Capacity;
            }
        }

        int N = 2000000;
        // 预热
        for (int w = 0; w < 2; w++) { var t = new List<int>(); for (int i = 0; i < N; i++) t.Add(i); }

        // 2. 预分配的收益
        var sw = Stopwatch.StartNew();
        var noPre = new List<int>();
        for (int i = 0; i < N; i++) noPre.Add(i);
        double no = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        var pre = new List<int>(N);                 // 预分配容量
        for (int i = 0; i < N; i++) pre.Add(i);
        double yes = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"\n追加 {N} 个元素: 不预分配 {no:F1}ms vs 预分配 {yes:F1}ms → 快 {no / yes:F1} 倍");

        // 3. 头部插入 O(n)
        int M = 20000;
        sw.Restart();
        var head = new List<int>();
        for (int i = 0; i < M; i++) head.Insert(0, i);
        double insMs = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        var tail = new List<int>();
        for (int i = 0; i < M; i++) tail.Add(i);
        double addMs = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"{M} 次插入: 头部 Insert(0,x) {insMs:F1}ms vs 末尾 Add {addMs:F1}ms"
            + $" → 头部慢 {insMs / addMs:F0} 倍");

        // 4. TrimExcess 释放多余容量
        var big = new List<int>(1000000);
        for (int i = 0; i < 10; i++) big.Add(i);
        Console.WriteLine($"\n只放 10 个元素但容量是 {big.Capacity}");
        big.TrimExcess();
        Console.WriteLine($"TrimExcess() 后容量降为 {big.Capacity} ← 内存敏感时很有用");
    }
}
