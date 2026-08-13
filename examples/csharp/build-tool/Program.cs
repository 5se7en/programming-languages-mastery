// 构建工具：任务图的并行调度——关键路径决定构建时间下限；以及确定性构建为什么难。
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;

class Program
{
    // 一个构建任务: 名字 + 依赖 + 耗时（模拟编译/打包的工作量）
    record Task(string Name, string[] Deps, int Ms);

    static readonly Task[] Graph =
    {
        new("core",     Array.Empty<string>(), 200),   // 底座，谁都要等它
        new("utils",    new[] { "core" },       80),
        new("data",     new[] { "core" },      120),
        new("api",      new[] { "utils", "data" }, 90),
        new("ui",       new[] { "utils" },      70),
        new("docs",     Array.Empty<string>(),  60),   // 与主链无关，可全程并行
        new("bundle",   new[] { "api", "ui" },  50),
    };

    static Dictionary<string, Task> ByName => Graph.ToDictionary(t => t.Name);

    static void Main()
    {
        Console.WriteLine("== ① 构建是一个任务图，不是一条流水线 ==");
        foreach (var t in Graph)
            Console.WriteLine($"  {t.Name,-8} ← [{string.Join(", ", t.Deps),-14}]  耗时 {t.Ms,3} ms");

        // ---------- 串行 ----------
        var sw = Stopwatch.StartNew();
        int serialTotal = RunSerial();
        Console.WriteLine($"\n== ② 串行执行: {serialTotal} ms（所有任务耗时之和）==");

        // ---------- 并行（按依赖就绪调度）----------
        var (parallelMs, waves) = RunParallel(workers: 8);
        Console.WriteLine($"\n== ③ 并行调度（8 个 worker）: {parallelMs} ms ==");
        for (int i = 0; i < waves.Count; i++)
            Console.WriteLine($"  第 {i + 1} 波（依赖已就绪，可同时跑）: {string.Join(", ", waves[i])}");
        Console.WriteLine($"  → 加速 {(double)serialTotal / parallelMs:F2}x");

        // ---------- 关键路径 ----------
        var (cp, cpLen) = CriticalPath();
        Console.WriteLine($"\n== ④ 关键路径：并行的下限（实测）==");
        Console.WriteLine($"  最长依赖链: {string.Join(" → ", cp)} = {cpLen} ms");
        Console.WriteLine($"  → 就算给你【无限个 CPU】，构建也不可能快过 {cpLen} ms");
        Console.WriteLine("  → 优化构建速度的第一步不是加机器，是【缩短关键路径】:");
        Console.WriteLine("     拆分 core 这种「谁都依赖」的巨型节点，比买 CPU 有效得多");
        var (inf, _) = RunParallel(workers: 999);
        Console.WriteLine($"  验证: 给 999 个 worker → {inf} ms（≈ 关键路径 {cpLen} ms，加不动了）");

        // ---------- 确定性构建 ----------
        Console.WriteLine("\n== ⑤ 确定性构建：同样的输入必须产出同样的字节（实测三个破坏者）==");
        string input = "source code v1";

        string BuildNondeterministic(string src) =>
            $"// built at {DateTime.Now:O}\n// by {Environment.MachineName}\n{src.ToUpper()}";
        string BuildDeterministic(string src) =>
            $"// built from source\n{src.ToUpper()}";

        string h1 = Sha(BuildNondeterministic(input));
        Thread.Sleep(15);
        string h2 = Sha(BuildNondeterministic(input));
        Console.WriteLine($"  含时间戳的构建，同样输入两次: {h1} vs {h2} → 一致: {h1 == h2}");
        string d1 = Sha(BuildDeterministic(input)), d2 = Sha(BuildDeterministic(input));
        Console.WriteLine($"  去掉时间戳后:              {d1} vs {d2} → 一致: {d1 == d2}");
        Console.WriteLine("  破坏确定性的三大常客:");
        Console.WriteLine("    ① 时间戳（编译时间写进产物）② 绝对路径（/Users/你的名字/...）");
        Console.WriteLine("    ③ 并行/哈希遍历顺序（同样的输入，产物里符号顺序不同）");
        Console.WriteLine("  → 没有确定性，缓存命中率暴跌（Python 版 ⑥ 的 action cache 全部失效）");
        Console.WriteLine("  → 也无法验证「这个二进制真的是这份源码编的吗」——第 53 章供应链的终局问题");
        Console.WriteLine("  → 工具支持: .NET 的 <Deterministic>true</Deterministic>（默认开）、");
        Console.WriteLine("     gcc 的 -ffile-prefix-map、SOURCE_DATE_EPOCH 环境变量约定");

        Console.WriteLine("\n== ⑥ 增量、并行、缓存：三者的关系 ==");
        Console.WriteLine("  增量: 少做事    —— 只重建受影响的（Python/Java 版实测它的正确性有多难）");
        Console.WriteLine("  并行: 同时做    —— 上限是关键路径（④ 实测）");
        Console.WriteLine("  缓存: 别人做过  —— 需要确定性（⑤）+ 内容哈希（Python 版 ⑤）");
        Console.WriteLine("  → 三者叠加才有现代构建体验；缺任何一个都会在某个规模上撞墙");
        Console.WriteLine("  → MSBuild 的 Inputs/Outputs、Gradle 的 UP-TO-DATE、Bazel 的三者全上");
    }

    static string Sha(string s) =>
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(s)))[..12];

    static int RunSerial() => Graph.Sum(t => t.Ms);

    /// 按依赖就绪逐波调度（真实构建工具的核心循环）
    static (int ms, List<List<string>> waves) RunParallel(int workers)
    {
        var done = new HashSet<string>();
        var waves = new List<List<string>>();
        int elapsed = 0;
        while (done.Count < Graph.Length)
        {
            // 找出所有「依赖都完成了」的任务
            var ready = Graph.Where(t => !done.Contains(t.Name) && t.Deps.All(done.Contains))
                             .Take(workers).ToList();
            if (ready.Count == 0) throw new InvalidOperationException("依赖成环");
            waves.Add(ready.Select(t => $"{t.Name}({t.Ms}ms)").ToList());
            elapsed += ready.Max(t => t.Ms);          // 一波的耗时 = 最慢的那个
            foreach (var t in ready) done.Add(t.Name);
        }
        return (elapsed, waves);
    }

    /// 关键路径 = 依赖图上最长的加权路径
    static (List<string> path, int length) CriticalPath()
    {
        var memo = new Dictionary<string, (int len, List<string> path)>();
        (int, List<string>) Longest(string name)
        {
            if (memo.TryGetValue(name, out var m)) return (m.len, m.path);
            var t = ByName[name];
            int best = 0;
            List<string> bestPath = new();
            foreach (var d in t.Deps)
            {
                var (len, p) = Longest(d);
                if (len > best) { best = len; bestPath = p; }
            }
            var path = new List<string>(bestPath) { name };
            memo[name] = (best + t.Ms, path);
            return (best + t.Ms, path);
        }
        var results = Graph.Select(t => Longest(t.Name)).ToList();
        var max = results.OrderByDescending(r => r.Item1).First();
        return (max.Item2, max.Item1);
    }
}
