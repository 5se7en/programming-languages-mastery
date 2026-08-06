// 第 20 章 · 哈希 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

// ✅ record 自动生成正确的 Equals/GetHashCode —— 从语言层面消灭了 Java 的经典坑
record StudentKey(string Name);

// ❌ 只重写 Equals，忘了 GetHashCode（对照组）
class BadKey
{
    public string Name { get; }
    public BadKey(string n) => Name = n;
    public override bool Equals(object? o) => o is BadKey b && b.Name == Name;
    // 忘了 GetHashCode！（编译器会警告 CS0659）
    public override int GetHashCode() => base.GetHashCode();   // 用引用哈希 → 每个实例不同
}

class Hashes
{
    static void Main()
    {
        // 1. Dictionary 基本操作
        var scores = new Dictionary<string, int> { ["Alice"] = 92 };
        Console.WriteLine($"TryGetValue 存在: {scores.TryGetValue("Alice", out int a)} → {a}");
        Console.WriteLine($"TryGetValue 不存在: {scores.TryGetValue("Carol", out _)} ← 安全，不抛异常");

        // 2. record 作键：自动正确
        var good = new Dictionary<StudentKey, string>();
        good[new StudentKey("Alice")] = "92分";
        Console.WriteLine($"\nrecord 作键: {good[new StudentKey("Alice")]} ✓ 自动生成 Equals/GetHashCode");

        // 3. 对照：GetHashCode 不一致的后果
        var bad = new Dictionary<BadKey, string>();
        bad[new BadKey("Alice")] = "92分";
        bad.TryGetValue(new BadKey("Alice"), out var v);
        Console.WriteLine($"哈希不一致的键: {v ?? "null"}  ← 存进去了却查不到");
        Console.WriteLine($"  但 Equals 说相等: {new BadKey("Alice").Equals(new BadKey("Alice"))}");

        // 4. 有序性的三种选择
        var hash = new Dictionary<string, int>();
        var sorted = new SortedDictionary<string, int>();
        foreach (var k in new[] { "zebra", "apple", "mango" }) { hash[k] = 1; sorted[k] = 1; }
        Console.WriteLine($"\nDictionary(无序):       {string.Join(" ", hash.Keys)}");
        Console.WriteLine($"SortedDictionary(键序): {string.Join(" ", sorted.Keys)}");

        // 5. 哈希 vs 线性查找
        int N = 200000;
        var list = new List<string>();
        var set = new HashSet<string>();
        for (int i = 0; i < N; i++) { list.Add($"student{i}"); set.Add($"student{i}"); }
        var rnd = new Random(42);
        var targets = Enumerable.Range(0, 200).Select(_ => $"student{rnd.Next(N)}").ToList();

        var sw = Stopwatch.StartNew();
        foreach (var x in targets) list.Contains(x);      // O(n)
        double lin = sw.Elapsed.TotalMilliseconds;
        sw.Restart();
        foreach (var x in targets) set.Contains(x);       // O(1)
        double hsh = sw.Elapsed.TotalMilliseconds;
        Console.WriteLine($"\n在 {N} 个元素中查找 200 次: List {lin:F1}ms vs HashSet {hsh:F3}ms"
            + $" → 快约 {lin / hsh:F0} 倍");

        // 6. 词频统计
        var counts = new Dictionary<string, int>();
        foreach (var w in "the quick brown fox the lazy dog the fox".Split(' '))
            counts[w] = counts.GetValueOrDefault(w) + 1;
        Console.WriteLine($"\n词频: the={counts["the"]} fox={counts["fox"]}");
    }
}
