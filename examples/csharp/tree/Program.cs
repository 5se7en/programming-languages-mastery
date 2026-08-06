// 第 21 章 · 树 —— C# 示例
// 运行：dotnet run

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;

class Node
{
    public int V;
    public Node Left, Right;
    public Node(int v) { V = v; }
}

class Program
{
    // 迭代插入，避免深树时递归爆栈
    static Node Insert(Node root, int v)
    {
        var node = new Node(v);
        if (root == null) return node;
        var cur = root;
        while (true)
        {
            if (v < cur.V)
            {
                if (cur.Left == null) { cur.Left = node; return root; }
                cur = cur.Left;
            }
            else
            {
                if (cur.Right == null) { cur.Right = node; return root; }
                cur = cur.Right;
            }
        }
    }

    // 中序遍历：左 → 根 → 右，结果必然有序
    static void Inorder(Node node, List<int> outList)
    {
        if (node == null) return;
        Inorder(node.Left, outList);
        outList.Add(node.V);
        Inorder(node.Right, outList);
    }

    // 树高决定查找的最坏代价（迭代版）
    static int Height(Node root)
    {
        if (root == null) return 0;
        int h = 0;
        var stack = new Stack<(Node, int)>();
        stack.Push((root, 1));
        while (stack.Count > 0)
        {
            var (n, d) = stack.Pop();
            h = Math.Max(h, d);
            if (n.Left != null) stack.Push((n.Left, d + 1));
            if (n.Right != null) stack.Push((n.Right, d + 1));
        }
        return h;
    }

    static void Main()
    {
        Console.WriteLine("=== 1. 二叉搜索树：左小右大 ===");
        int[] values = { 50, 30, 70, 20, 40, 60, 80 };
        Node root = null;
        foreach (var v in values) root = Insert(root, v);

        var result = new List<int>();
        Inorder(root, result);
        Console.WriteLine($"插入顺序: [{string.Join(", ", values)}]");
        Console.WriteLine($"中序遍历: [{string.Join(", ", result)}] ← 自动有序！这是 BST 的定义性质");
        Console.WriteLine($"树高: {Height(root)}");

        Console.WriteLine("\n=== 2. ⚠️ BST 的退化：有序插入会变成链表 ===");
        const int N = 2000;
        var rng = new Random(42);   // 固定种子，结果可复现

        var shuffled = Enumerable.Range(0, N).OrderBy(_ => rng.Next()).ToArray();
        Node randomTree = null;
        foreach (var v in shuffled) randomTree = Insert(randomTree, v);

        Node sortedTree = null;
        for (int i = 0; i < N; i++) sortedTree = Insert(sortedTree, i);

        Console.WriteLine($"随机插入 {N} 个数 → 树高 {Height(randomTree)}");
        Console.WriteLine($"有序插入 {N} 个数 → 树高 {Height(sortedTree)}   ← 完全退化成链表！");
        Console.WriteLine($"理想树高 log2({N}) ≈ {Math.Log2(N):F0}");
        Console.WriteLine("→ 这就是「平衡树」（AVL / 红黑树）存在的全部理由");

        Console.WriteLine("\n=== 3. SortedDictionary：红黑树，自动按键排序 ===");
        var tree = new SortedDictionary<string, int>
        {
            ["zebra"] = 1, ["apple"] = 2, ["mango"] = 3
        };
        var hash = new Dictionary<string, int>
        {
            ["zebra"] = 1, ["apple"] = 2, ["mango"] = 3
        };
        Console.WriteLine("插入顺序: zebra, apple, mango");
        Console.WriteLine($"SortedDictionary: [{string.Join(", ", tree.Keys)}] ← 自动排序");
        Console.WriteLine($"Dictionary      : [{string.Join(", ", hash.Keys)}] ← 不保证顺序");

        Console.WriteLine("\n=== 4. C# 独有：SortedList vs SortedDictionary ===");
        var sortedList = new SortedList<string, int>
        {
            ["zebra"] = 1, ["apple"] = 2, ["mango"] = 3
        };
        Console.WriteLine($"SortedList: [{string.Join(", ", sortedList.Keys)}]");
        Console.WriteLine($"按索引访问 sortedList.Keys[0] → {sortedList.Keys[0]}   ← SortedDictionary 做不到！");
        Console.WriteLine();
        Console.WriteLine("                   SortedDictionary   SortedList");
        Console.WriteLine("  底层             红黑树             有序数组");
        Console.WriteLine("  插入/删除        O(log n)           O(n) 要搬移");
        Console.WriteLine("  按索引访问       ❌                 ✅ O(1)");
        Console.WriteLine("  内存             较多（节点指针）   较少（紧凑）");
        Console.WriteLine("  适合             频繁增删           一次构建、多次查询");

        Console.WriteLine("\n=== 5. 有序的代价：SortedDictionary vs Dictionary ===");
        const int M = 200000;

        // 预热，让 JIT 先编译好（第 05 章）
        for (int w = 0; w < 3; w++)
        {
            var warm = new Dictionary<int, int>();
            for (int i = 0; i < M; i++) warm[i] = i;
        }

        var d = new Dictionary<int, int>();
        var sw = Stopwatch.StartNew();
        for (int i = 0; i < M; i++) d[i] = i;
        long s1 = 0;
        for (int i = 0; i < M; i++) s1 += d[i];
        sw.Stop();
        double hashMs = sw.Elapsed.TotalMilliseconds;

        var sd = new SortedDictionary<int, int>();
        sw.Restart();
        for (int i = 0; i < M; i++) sd[i] = i;
        long s2 = 0;
        for (int i = 0; i < M; i++) s2 += sd[i];
        sw.Stop();
        double treeMs = sw.Elapsed.TotalMilliseconds;

        Console.WriteLine($"{M} 次插入+查找:");
        Console.WriteLine($"  Dictionary      : {hashMs:F0} ms");
        Console.WriteLine($"  SortedDictionary: {treeMs:F0} ms");
        Console.WriteLine($"  → 哈希快约 {treeMs / hashMs:F1} 倍，这就是「有序」的价格");
        Console.WriteLine($"  (校验和 {s1 + s2}，确保循环没被优化掉)");
        Console.WriteLine("  ⚠️ 数字依赖环境，记住结论：哈希更快，树更全能");

        Console.WriteLine("\n=== 6. PriorityQueue 是堆（.NET 6+）===");
        var pq = new PriorityQueue<string, int>();
        pq.Enqueue("普通任务", 3);
        pq.Enqueue("紧急任务", 1);
        pq.Enqueue("低优任务", 5);
        Console.Write("按优先级出队: ");
        while (pq.Count > 0) Console.Write(pq.Dequeue() + " ");
        Console.WriteLine("← 小顶堆，数字小的先出");

        Console.WriteLine("\n=== 7. LINQ 排序：方便但每次都是 O(n log n) ===");
        var scores = new Dictionary<string, int>
        {
            ["Carol"] = 92, ["Alice"] = 78, ["Bob"] = 85
        };
        Console.WriteLine("按分数降序:");
        foreach (var kv in scores.OrderByDescending(kv => kv.Value))
            Console.WriteLine($"  {kv.Key}: {kv.Value}");
        Console.WriteLine("→ 适合偶尔排序；频繁查询请用 SortedDictionary");
    }
}
