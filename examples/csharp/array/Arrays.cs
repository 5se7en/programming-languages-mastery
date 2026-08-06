// 第 16 章 · 数组 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Diagnostics;

class ArraysDemo
{
    static void Main()
    {
        int[] scores = { 92, 75, 88 };
        Console.WriteLine($"长度(属性): {scores.Length}");

        // 1. C# 8+ 的索引与范围语法
        Console.WriteLine($"倒数第一 a[^1]: {scores[^1]} ← 类似 Python 的 -1");
        Console.WriteLine($"切片 a[0..2]: [{string.Join(", ", scores[0..2])}]");

        // 2. 越界抛异常
        try { var v = scores[10]; }
        catch (IndexOutOfRangeException e)
        { Console.WriteLine($"scores[10] → {e.GetType().Name} ← 运行时检查"); }

        // 3. C# 有两种二维数组：矩形数组是真正连续的（Java 没有）
        int[,] rect = new int[3, 4];          // 矩形：一整块连续内存 ✓
        int[][] jagged = new int[3][];        // 交错：数组的数组（同 Java）
        jagged[0] = new int[2];
        Console.WriteLine($"矩形数组 int[3,4] 元素总数: {rect.Length} ← 一整块连续内存");
        Console.WriteLine($"交错数组 int[3][] 首行长度: {jagged[0].Length} ← 数组的数组");

        // 4. Span<T>：零拷贝引用数组的一段
        Span<int> span = scores.AsSpan(1, 2);
        Console.WriteLine($"Span 切片(零拷贝): [{string.Join(", ", span.ToArray())}]");

        // 5. 缓存局部性：用矩形数组测（真正连续）
        const int N = 2000;
        var m = new int[N, N];
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) m[i, j] = 1;

        var sw = Stopwatch.StartNew();
        long s1 = 0;
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) s1 += m[i, j];
        double row = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        long s2 = 0;
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) s2 += m[i, j];
        double col = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"\n缓存局部性: 行优先 {row:F1}ms vs 列优先 {col:F1}ms"
            + $" → 慢 {col / row:F1} 倍（校验和一致: {s1 == s2}）");
    }
}
