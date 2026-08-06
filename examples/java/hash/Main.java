// 第 20 章 · 哈希 — Java 示例
// 运行：javac *.java && java Main
import java.util.*;

public class Main {
    // ❌ 只重写 equals，忘了 hashCode —— Java 最经典的坑
    static class BadKey {
        final String name;
        BadKey(String n) { name = n; }
        @Override public boolean equals(Object o) {
            return o instanceof BadKey && ((BadKey) o).name.equals(name);
        }
        // 忘了 hashCode！
    }

    // ✅ 两个都重写
    static class GoodKey {
        final String name;
        GoodKey(String n) { name = n; }
        @Override public boolean equals(Object o) {
            return o instanceof GoodKey && ((GoodKey) o).name.equals(name);
        }
        @Override public int hashCode() { return Objects.hash(name); }
    }

    public static void main(String[] args) {
        // 1. HashMap 基本操作
        Map<String, Integer> scores = new HashMap<>();
        scores.put("Alice", 92);
        System.out.println("get: " + scores.get("Alice")
            + " | getOrDefault: " + scores.getOrDefault("Carol", 0) + " ← 默认值");

        // 2. ⚠️ equals/hashCode 契约
        Map<BadKey, String> bad = new HashMap<>();
        bad.put(new BadKey("Alice"), "92分");
        System.out.println("\n只重写 equals:");
        System.out.println("  equals 说相等吗: " + new BadKey("Alice").equals(new BadKey("Alice")));
        System.out.println("  但 get() 返回: " + bad.get(new BadKey("Alice")) + "  ← 存进去了却查不到！");

        Map<GoodKey, String> good = new HashMap<>();
        good.put(new GoodKey("Alice"), "92分");
        System.out.println("两个都重写: get() = " + good.get(new GoodKey("Alice")) + " ✓");
        System.out.println("→ hashCode 决定去哪个桶找，equals 决定桶内哪个才是");

        // 3. Map 家族：有序性的三种选择
        Map<String, Integer> hash = new HashMap<>();
        Map<String, Integer> linked = new LinkedHashMap<>();
        Map<String, Integer> tree = new TreeMap<>();
        for (String k : List.of("zebra", "apple", "mango")) {
            hash.put(k, 1); linked.put(k, 1); tree.put(k, 1);
        }
        System.out.println("\nHashMap(无序):       " + hash.keySet());
        System.out.println("LinkedHashMap(插入序): " + linked.keySet());
        System.out.println("TreeMap(键排序):      " + tree.keySet());

        // 4. 哈希 vs 线性查找
        int N = 200000;
        List<String> list = new ArrayList<>();
        Set<String> set = new HashSet<>();
        for (int i = 0; i < N; i++) { list.add("student" + i); set.add("student" + i); }
        List<String> targets = new ArrayList<>();
        Random r = new Random(42);
        for (int i = 0; i < 200; i++) targets.add("student" + r.nextInt(N));

        long t0 = System.nanoTime();
        for (String x : targets) list.contains(x);      // O(n)
        long t1 = System.nanoTime();
        for (String x : targets) set.contains(x);       // O(1)
        long t2 = System.nanoTime();
        System.out.printf("%n在 %d 个元素中查找 200 次: List %.1fms vs HashSet %.3fms → 快约 %.0f 倍%n",
            N, (t1-t0)/1e6, (t2-t1)/1e6, (double)(t1-t0)/(t2-t1));

        // 5. 词频统计（merge 写法）
        Map<String, Integer> counts = new HashMap<>();
        for (String w : "the quick brown fox the lazy dog the fox".split(" "))
            counts.merge(w, 1, Integer::sum);
        System.out.println("\n词频: the=" + counts.get("the") + " fox=" + counts.get("fox"));
    }
}
