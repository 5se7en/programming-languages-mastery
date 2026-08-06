// 第 19 章 · 队列 — C# 示例
// 运行：dotnet new console -o app，复制为 app/Program.cs，再 dotnet run --project app
using System;
using System.Collections.Generic;

class Queues
{
    static List<int> Traverse(Dictionary<int, int[]> tree, int root, bool useStack)
    {
        var box = new LinkedList<int>();
        box.AddLast(root);
        var order = new List<int>();
        while (box.Count > 0)
        {
            int node;
            if (useStack) { node = box.Last!.Value; box.RemoveLast(); }    // ← 唯一的区别
            else          { node = box.First!.Value; box.RemoveFirst(); }
            order.Add(node);
            foreach (var c in tree[node]) box.AddLast(c);
        }
        return order;
    }

    static void Main()
    {
        // 1. Queue<T>：底层环形数组，方法名最直白
        var q = new Queue<string>();
        q.Enqueue("A"); q.Enqueue("B"); q.Enqueue("C");
        Console.WriteLine($"Queue 出队: {q.Dequeue()} | 队首: {q.Peek()} | 剩余: {q.Count}");

        // 2. TryDequeue：空队列不抛异常
        var empty = new Queue<int>();
        Console.WriteLine($"空队列 TryDequeue 成功? {empty.TryDequeue(out _)} ← 安全版本");

        // 3. 栈 vs 队列 = DFS vs BFS
        var tree = new Dictionary<int, int[]> {
            [1] = new[]{2,3}, [2] = new[]{4,5}, [3] = new[]{6,7},
            [4] = Array.Empty<int>(), [5] = Array.Empty<int>(),
            [6] = Array.Empty<int>(), [7] = Array.Empty<int>() };
        Console.WriteLine($"\n用栈  (LIFO) → DFS: {string.Join(" ", Traverse(tree, 1, true))}");
        Console.WriteLine($"用队列(FIFO) → BFS: {string.Join(" ", Traverse(tree, 1, false))} ← 逐层扫描");

        // 4. PriorityQueue（.NET 6+）：元素与优先级分离，默认最小堆
        var pq = new PriorityQueue<string, int>();
        pq.Enqueue("低优先级", 3);
        pq.Enqueue("紧急", 1);
        pq.Enqueue("普通", 2);
        Console.Write("\n优先队列出队(默认最小堆): ");
        while (pq.Count > 0) Console.Write(pq.Dequeue() + " ");
        Console.WriteLine("← 按优先级，与入队顺序无关");

        // 5. 队列容量会自动增长（第 17 章的翻倍策略）
        var big = new Queue<int>();
        for (int i = 0; i < 5; i++) big.Enqueue(i);
        Console.WriteLine($"\n队列元素数: {big.Count}（底层环形数组，满了自动扩容）");
    }
}
