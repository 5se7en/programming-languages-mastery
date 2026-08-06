// 第 22 章 · 图 —— C# 示例
// 运行：dotnet run

using System;
using System.Collections.Generic;
using System.Linq;

class Program
{
    // ---------- BFS：用队列，保证最短路径 ----------
    static List<string> BfsPath(Dictionary<string, List<string>> g, string start, string goal)
    {
        var queue = new Queue<List<string>>();
        queue.Enqueue(new List<string> { start });
        var seen = new HashSet<string> { start };
        while (queue.Count > 0)
        {
            var path = queue.Dequeue();
            var node = path[^1];                        // ^1 = 最后一个元素
            if (node == goal) return path;
            foreach (var next in g.GetValueOrDefault(node, new List<string>()))
                if (seen.Add(next))                      // Add 返回 bool，省一次查找
                    queue.Enqueue(new List<string>(path) { next });
        }
        return null;
    }

    // ---------- DFS：用递归，只保证「找得到」 ----------
    static List<string> DfsPath(Dictionary<string, List<string>> g, string cur, string goal,
                                List<string> path, HashSet<string> seen)
    {
        if (cur == goal) return path;
        foreach (var next in g.GetValueOrDefault(cur, new List<string>()))
            if (seen.Add(next))
            {
                var r = DfsPath(g, next, goal, new List<string>(path) { next }, seen);
                if (r != null) return r;
            }
        return null;
    }

    // ---------- Kahn 拓扑排序 ----------
    static (List<string> order, List<string> cycle) TopoSort(
        List<string> nodes, (string, string)[] edges)
    {
        var indeg = nodes.ToDictionary(n => n, _ => 0);
        var adj = nodes.ToDictionary(n => n, _ => new List<string>());
        foreach (var (a, b) in edges) { adj[a].Add(b); indeg[b]++; }

        var queue = new Queue<string>(nodes.Where(n => indeg[n] == 0));
        var order = new List<string>();
        while (queue.Count > 0)
        {
            var n = queue.Dequeue();
            order.Add(n);
            foreach (var m in adj[n])
                if (--indeg[m] == 0) queue.Enqueue(m);
        }
        // 没排完 → 剩下节点的入度降不到 0，它们在环里
        if (order.Count != nodes.Count)
            return (null, nodes.Where(n => indeg[n] > 0).ToList());
        return (order, null);
    }

    // ---------- 环检测：三色标记 ----------
    const int WHITE = 0, GRAY = 1, BLACK = 2;

    static bool HasCycle(Dictionary<string, List<string>> g)
    {
        var color = g.Keys.ToDictionary(n => n, _ => WHITE);
        bool Dfs(string n)
        {
            color[n] = GRAY;                             // 灰色 = 正在访问的路径上
            foreach (var m in g.GetValueOrDefault(n, new List<string>()))
            {
                if (color.GetValueOrDefault(m, WHITE) == GRAY) return true;   // 回边 → 有环
                if (color.GetValueOrDefault(m, WHITE) == WHITE && Dfs(m)) return true;
            }
            color[n] = BLACK;
            return false;
        }
        return g.Keys.Any(n => color[n] == WHITE && Dfs(n));
    }

    static void Main()
    {
        Console.WriteLine("=== 1. 邻接表：Dictionary<string, List<string>> ===");
        var g = new Dictionary<string, List<string>>
        {
            ["A"] = new List<string> { "B", "D" },       // A 到 D 有两条路
            ["B"] = new List<string> { "C" },
            ["C"] = new List<string> { "D" },
            ["D"] = new List<string>(),
        };
        Console.WriteLine("图: A→B, A→D, B→C, C→D");
        foreach (var kv in g)
            Console.WriteLine($"  {kv.Key} → [{string.Join(", ", kv.Value)}]");

        Console.WriteLine("\n=== 2. ⚠️ DFS 找到的不是最短路径！===");
        var dfs = DfsPath(g, "A", "D", new List<string> { "A" }, new HashSet<string> { "A" });
        var bfs = BfsPath(g, "A", "D");
        Console.WriteLine($"DFS 找到: {string.Join(" → ", dfs)}  (先钻进了 B 这条深路)");
        Console.WriteLine($"BFS 找到: {string.Join(" → ", bfs)}          ← 才是最短路径");
        Console.WriteLine("→ 无权图求最短路径必须用 BFS，DFS 只保证「找得到」");

        Console.WriteLine("\n=== 3. 拓扑排序：构建顺序 + 循环依赖检测 ===");
        Console.WriteLine("场景 A：正常的模块依赖");
        var (orderA, _) = TopoSort(
            new List<string> { "utils", "config", "db", "api", "ui", "app" },
            new[] { ("utils","db"), ("config","db"), ("db","api"),
                    ("api","ui"), ("ui","app"), ("utils","api") });
        Console.WriteLine($"  构建顺序: {string.Join(" → ", orderA)}");
        Console.WriteLine("  ✓ 无环");

        Console.WriteLine("\n场景 B：⚠️ 循环依赖");
        var (orderB, cycleB) = TopoSort(
            new List<string> { "auth", "user", "order", "payment" },
            new[] { ("auth","user"), ("user","order"),
                    ("order","payment"), ("payment","user") });
        Console.WriteLine("  依赖: auth→user, user→order, order→payment, payment→user");
        Console.WriteLine($"  拓扑排序结果: {(orderB == null ? "null" : string.Join(" → ", orderB))}");
        Console.WriteLine($"  ⚠️ 检测到循环依赖，涉及模块: [{string.Join(", ", cycleB)}]");
        Console.WriteLine("  → 这就是 NuGet/npm 报 circular dependency 的原理");

        Console.WriteLine("\n=== 4. 环检测：三色标记法 ===");
        var cyclic = new Dictionary<string, List<string>>
        {
            ["A"] = new List<string> { "B" },
            ["B"] = new List<string> { "C" },
            ["C"] = new List<string> { "A" },
        };
        var diamond = new Dictionary<string, List<string>>
        {
            ["A"] = new List<string> { "B", "C" },
            ["B"] = new List<string> { "D" },
            ["C"] = new List<string> { "D" },
            ["D"] = new List<string>(),
        };
        Console.WriteLine($"A→B→C→A            有环? {HasCycle(cyclic)}");
        Console.WriteLine($"A→B, A→C, B→D, C→D 有环? {HasCycle(diamond)}  ← D 被访问两次，但这不是环（是 DAG）");
        Console.WriteLine("→ 关键：区分「重复访问」和「回到正在访问的路径上」");

        Console.WriteLine("\n=== 5. Dijkstra：用 PriorityQueue（.NET 6+，默认小顶堆）===");
        var roads = new Dictionary<string, List<(string to, int w)>>
        {
            ["北京"] = new() { ("天津", 120), ("济南", 400) },
            ["天津"] = new() { ("济南", 320), ("青岛", 550) },
            ["济南"] = new() { ("青岛", 360) },
            ["青岛"] = new(),
        };

        var dist = roads.Keys.ToDictionary(n => n, _ => int.MaxValue);
        dist["北京"] = 0;
        var prev = new Dictionary<string, string>();

        // C# 把元素和优先级分成两个泛型参数，比 Java 写 Comparator 更直观
        var pq = new PriorityQueue<string, int>();
        pq.Enqueue("北京", 0);
        while (pq.TryDequeue(out var n, out var d))
        {
            if (d > dist[n]) continue;                   // 过期条目
            foreach (var (to, w) in roads[n])
                if (d + w < dist[to])
                {
                    dist[to] = d + w;
                    prev[to] = n;
                    pq.Enqueue(to, d + w);
                }
        }

        Console.WriteLine("路网: 北京-天津 120, 北京-济南 400, 天津-济南 320,");
        Console.WriteLine("      天津-青岛 550, 济南-青岛 360");
        foreach (var city in new[] { "北京", "天津", "济南", "青岛" })
            Console.WriteLine($"  北京 → {city}: {dist[city]} km");

        var path = new List<string> { "青岛" };
        var cur = "青岛";
        while (prev.ContainsKey(cur)) { cur = prev[cur]; path.Add(cur); }
        path.Reverse();
        Console.WriteLine($"最短路径: {string.Join(" → ", path)} = {dist["青岛"]} km");
        Console.WriteLine($"穷举验证: 天津路线 {120 + 550} ✓   济南路线 {400 + 360}");

        Console.WriteLine("\n=== 6. 邻接矩阵 vs 邻接表：为什么默认用表 ===");
        foreach (var (v, deg) in new[] { (100, 4), (1000, 4), (10000, 4) })
        {
            long cells = (long)v * v, entries = (long)v * deg;
            Console.WriteLine($"  {v,5} 顶点(平均度{deg}): 矩阵 {cells,12:N0} 格   " +
                              $"表 {entries,8:N0} 项  → 矩阵是表的 {(double)cells / entries,6:N0} 倍");
        }
        Console.WriteLine("→ 真实的图几乎都是稀疏的，矩阵里 99% 以上存的都是「没有边」");
    }
}
