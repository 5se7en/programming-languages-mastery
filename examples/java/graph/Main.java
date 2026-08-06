// 第 22 章 · 图 —— Java 示例
// 运行：javac Main.java && java Main

import java.util.*;

public class Main {

    // ---------- BFS：用队列，保证最短路径 ----------
    static List<String> bfsPath(Map<String, List<String>> g, String start, String goal) {
        Deque<List<String>> queue = new ArrayDeque<>();
        queue.add(List.of(start));
        Set<String> seen = new HashSet<>(Set.of(start));
        while (!queue.isEmpty()) {
            List<String> path = queue.poll();
            String node = path.get(path.size() - 1);
            if (node.equals(goal)) return path;
            for (String next : g.getOrDefault(node, List.of())) {
                if (seen.add(next)) {          // add 返回 false 说明已存在，省一次哈希查找
                    List<String> np = new ArrayList<>(path);
                    np.add(next);
                    queue.add(np);
                }
            }
        }
        return null;
    }

    // ---------- DFS：用递归，只保证「找得到」 ----------
    static List<String> dfsPath(Map<String, List<String>> g, String cur, String goal,
                                List<String> path, Set<String> seen) {
        if (cur.equals(goal)) return path;
        for (String next : g.getOrDefault(cur, List.of())) {
            if (seen.add(next)) {
                List<String> np = new ArrayList<>(path);
                np.add(next);
                List<String> r = dfsPath(g, next, goal, np, seen);
                if (r != null) return r;
            }
        }
        return null;
    }

    // ---------- Kahn 拓扑排序：反复取入度为 0 的节点 ----------
    record TopoResult(List<String> order, List<String> cycle) {}

    /** 带权边：用 record 比 Object[] 清晰得多，也避免了泛型推断问题 */
    record Edge(String to, int weight) {}

    /** Dijkstra 队列里的条目 */
    record Entry(String node, int dist) {}

    static TopoResult topoSort(List<String> nodes, String[][] edges) {
        Map<String, Integer> indeg = new LinkedHashMap<>();
        Map<String, List<String>> adj = new HashMap<>();
        for (String n : nodes) { indeg.put(n, 0); adj.put(n, new ArrayList<>()); }
        for (String[] e : edges) {                    // e[0] 必须在 e[1] 之前
            adj.get(e[0]).add(e[1]);
            indeg.merge(e[1], 1, Integer::sum);
        }

        Deque<String> queue = new ArrayDeque<>();
        for (String n : nodes) if (indeg.get(n) == 0) queue.add(n);

        List<String> order = new ArrayList<>();
        while (!queue.isEmpty()) {
            String n = queue.poll();
            order.add(n);
            for (String m : adj.get(n))
                if (indeg.merge(m, -1, Integer::sum) == 0) queue.add(m);
        }
        if (order.size() != nodes.size()) {
            // 没排完 → 剩下节点的入度降不到 0，它们在环里
            List<String> cycle = new ArrayList<>();
            for (String n : nodes) if (indeg.get(n) > 0) cycle.add(n);
            return new TopoResult(null, cycle);
        }
        return new TopoResult(order, null);
    }

    // ---------- 环检测：三色标记 ----------
    static final int WHITE = 0, GRAY = 1, BLACK = 2;

    static boolean hasCycle(Map<String, List<String>> g) {
        Map<String, Integer> color = new HashMap<>();
        for (String n : g.keySet()) color.put(n, WHITE);
        for (String n : g.keySet())
            if (color.get(n) == WHITE && dfsCycle(g, n, color)) return true;
        return false;
    }

    static boolean dfsCycle(Map<String, List<String>> g, String n, Map<String, Integer> color) {
        color.put(n, GRAY);                        // 灰色 = 正在访问的路径上
        for (String m : g.getOrDefault(n, List.of())) {
            if (color.getOrDefault(m, WHITE) == GRAY) return true;   // 回边 → 有环
            if (color.getOrDefault(m, WHITE) == WHITE && dfsCycle(g, m, color)) return true;
        }
        color.put(n, BLACK);
        return false;
    }

    public static void main(String[] args) {
        System.out.println("=== 1. 邻接表：Map<String, List<String>> ===");
        Map<String, List<String>> g = new LinkedHashMap<>();
        // A 到 D 有两条路：直达，或绕经 B、C
        g.computeIfAbsent("A", k -> new ArrayList<>()).addAll(List.of("B", "D"));
        g.computeIfAbsent("B", k -> new ArrayList<>()).add("C");
        g.computeIfAbsent("C", k -> new ArrayList<>()).add("D");
        g.computeIfAbsent("D", k -> new ArrayList<>());
        System.out.println("图: A→B, A→D, B→C, C→D");
        g.forEach((k, v) -> System.out.println("  " + k + " → " + v));

        System.out.println("\n=== 2. ⚠️ DFS 找到的不是最短路径！===");
        List<String> dfs = dfsPath(g, "A", "D", new ArrayList<>(List.of("A")),
                                   new HashSet<>(Set.of("A")));
        List<String> bfs = bfsPath(g, "A", "D");
        System.out.println("DFS 找到: " + String.join(" → ", dfs) + "  (先钻进了 B 这条深路)");
        System.out.println("BFS 找到: " + String.join(" → ", bfs) + "          ← 才是最短路径");
        System.out.println("→ 无权图求最短路径必须用 BFS，DFS 只保证「找得到」");

        System.out.println("\n=== 3. 拓扑排序：构建顺序 + 循环依赖检测 ===");
        System.out.println("场景 A：正常的模块依赖");
        List<String> nodesA = List.of("utils", "config", "db", "api", "ui", "app");
        String[][] edgesA = {{"utils","db"},{"config","db"},{"db","api"},
                             {"api","ui"},{"ui","app"},{"utils","api"}};
        TopoResult a = topoSort(nodesA, edgesA);
        System.out.println("  构建顺序: " + String.join(" → ", a.order()));
        System.out.println("  ✓ 无环");

        System.out.println("\n场景 B：⚠️ 循环依赖");
        List<String> nodesB = List.of("auth", "user", "order", "payment");
        String[][] edgesB = {{"auth","user"},{"user","order"},
                             {"order","payment"},{"payment","user"}};
        TopoResult b = topoSort(nodesB, edgesB);
        System.out.println("  依赖: auth→user, user→order, order→payment, payment→user");
        System.out.println("  拓扑排序结果: " + b.order());
        System.out.println("  ⚠️ 检测到循环依赖，涉及模块: " + b.cycle());
        System.out.println("  → 这就是 Maven/Gradle 报 circular dependency 的原理");

        System.out.println("\n=== 4. 环检测：三色标记法 ===");
        Map<String, List<String>> cyclic = Map.of("A", List.of("B"), "B", List.of("C"),
                                                   "C", List.of("A"));
        Map<String, List<String>> diamond = Map.of("A", List.of("B", "C"), "B", List.of("D"),
                                                    "C", List.of("D"), "D", List.of());
        System.out.println("A→B→C→A            有环? " + hasCycle(cyclic));
        System.out.println("A→B, A→C, B→D, C→D 有环? " + hasCycle(diamond)
                + "  ← D 被访问两次，但这不是环（是 DAG）");
        System.out.println("→ 关键：区分「重复访问」和「回到正在访问的路径上」");

        System.out.println("\n=== 5. Dijkstra：用 PriorityQueue（堆）求带权最短路径 ===");
        Map<String, List<Edge>> roads = new LinkedHashMap<>();
        roads.put("北京", List.of(new Edge("天津", 120), new Edge("济南", 400)));
        roads.put("天津", List.of(new Edge("济南", 320), new Edge("青岛", 550)));
        roads.put("济南", List.of(new Edge("青岛", 360)));
        roads.put("青岛", List.of());

        Map<String,Integer> dist = new LinkedHashMap<>();
        for (String n : roads.keySet()) dist.put(n, Integer.MAX_VALUE);
        dist.put("北京", 0);
        Map<String,String> prev = new HashMap<>();

        // Java 的 PriorityQueue 默认是小顶堆（与 C++ 相反）
        PriorityQueue<Entry> pq = new PriorityQueue<>(Comparator.comparingInt(Entry::dist));
        pq.add(new Entry("北京", 0));
        while (!pq.isEmpty()) {
            Entry top = pq.poll();
            if (top.dist() > dist.get(top.node())) continue;         // 过期条目
            for (Edge e : roads.getOrDefault(top.node(), List.of())) {
                int nd = top.dist() + e.weight();
                if (nd < dist.get(e.to())) {
                    dist.put(e.to(), nd);
                    prev.put(e.to(), top.node());
                    pq.add(new Entry(e.to(), nd));
                }
            }
        }

        System.out.println("路网: 北京-天津 120, 北京-济南 400, 天津-济南 320,");
        System.out.println("      天津-青岛 550, 济南-青岛 360");
        dist.forEach((c, d) -> System.out.println("  北京 → " + c + ": " + d + " km"));

        List<String> path = new ArrayList<>(List.of("青岛"));
        String cur = "青岛";
        while (prev.containsKey(cur)) { cur = prev.get(cur); path.add(cur); }
        Collections.reverse(path);
        System.out.println("最短路径: " + String.join(" → ", path) + " = " + dist.get("青岛") + " km");
        System.out.println("穷举验证: 天津路线 " + (120+550) + " ✓   济南路线 " + (400+360));

        System.out.println("\n=== 6. 邻接矩阵 vs 邻接表：为什么默认用表 ===");
        int[][] cases = {{100,4},{1000,4},{10000,4}};
        for (int[] c : cases) {
            long cells = (long) c[0] * c[0], entries = (long) c[0] * c[1];
            System.out.printf("  %5d 顶点(平均度%d): 矩阵 %,12d 格   表 %,8d 项  → 矩阵是表的 %,.0f 倍%n",
                    c[0], c[1], cells, entries, (double) cells / entries);
        }
        System.out.println("→ 真实的图几乎都是稀疏的，矩阵里 99% 以上存的都是「没有边」");
    }
}
