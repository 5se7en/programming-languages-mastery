import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.NavigableMap;
import java.util.Random;
import java.util.TreeMap;

/**
 * 索引：内存里的「哈希 vs 有序树」，和数据库的「哈希索引 vs B+ 树索引」是同一道选择题。
 */
public class Main {

    public static void main(String[] args) {
        final int N = 1_000_000;
        Map<Integer, String> hash = new HashMap<>(N * 2);
        NavigableMap<Integer, String> tree = new TreeMap<>();
        for (int i = 0; i < N; i++) {
            hash.put(i * 2, "v" + i);
            tree.put(i * 2, "v" + i);
        }

        System.out.println("== ① 点查：哈希 vs 有序树 ==");
        Random rnd = new Random(42);
        int[] probes = new int[200_000];
        for (int i = 0; i < probes.length; i++) probes[i] = rnd.nextInt(N) * 2;

        long t0 = System.nanoTime();
        int hits = 0;
        for (int p : probes) if (hash.containsKey(p)) hits++;
        double msHash = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        int hits2 = 0;
        for (int p : probes) if (tree.containsKey(p)) hits2++;
        double msTree = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  %d 次点查（%d 条数据）:%n", probes.length, N);
        System.out.printf("    HashMap : %7.1f ms   O(1)%n", msHash);
        System.out.printf("    TreeMap : %7.1f ms   O(log n)，慢 %.1fx%n", msTree, msTree / msHash);
        System.out.printf("    结果一致: %s%n", hits == hits2);
        System.out.println("  → 只做等值查询时，哈希确实更快——数据库的哈希索引也是这个定位");

        System.out.println("\n== ② 范围查询：TreeMap 碾压 HashMap ==");
        final int LO = 1000, HI = 3000;
        t0 = System.nanoTime();
        int cnt1 = 0;
        for (int q = 0; q < 2000; q++) {
            cnt1 = 0;
            for (Map.Entry<Integer, String> e : hash.entrySet())       // 只能全部扫一遍
                if (e.getKey() >= LO && e.getKey() <= HI) cnt1++;
        }
        double msRangeHash = (System.nanoTime() - t0) / 1e6;

        t0 = System.nanoTime();
        int cnt2 = 0;
        for (int q = 0; q < 2000; q++)
            cnt2 = tree.subMap(LO, true, HI, true).size();             // 直接定位到区间
        double msRangeTree = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  查询 [%d, %d] 共 2000 次（命中 %d 条）:%n", LO, HI, cnt2);
        System.out.printf("    HashMap 全扫: %8.1f ms%n", msRangeHash);
        System.out.printf("    TreeMap 区间: %8.1f ms（快 %.0fx）%n",
                msRangeTree, msRangeHash / msRangeTree);
        System.out.printf("    结果一致: %s%n", cnt1 == cnt2);
        System.out.println("  → 哈希把顺序【彻底打乱】了，范围查询只能退化成全扫");
        System.out.println("  → 这就是数据库默认建 B+ 树而非哈希索引的原因（C++ 版实测同一结论）");

        System.out.println("\n== ③ 有序结构白送的三个能力 ==");
        System.out.println("  最小/最大:   firstKey=" + tree.firstKey() + "  lastKey=" + tree.lastKey()
                + "   → SQL 的 MIN()/MAX() 走索引是 O(log n) 而非 O(n)");
        System.out.println("  前驱/后继:   floorKey(1001)=" + tree.floorKey(1001)
                + "  ceilingKey(1001)=" + tree.ceilingKey(1001)
                + "   → SQL 的 >= / <= / BETWEEN");
        List<Integer> firstTen = new ArrayList<>(tree.headMap(20, true).keySet());
        System.out.println("  天然有序:    前几个键 " + firstTen
                + "   → SQL 的 ORDER BY 免排序（Python 版实测快 1652x）");

        System.out.println("\n== ④ 维护代价：插入时两者都要付钱 ==");
        final int M = 200_000;
        Map<Integer, String> h2 = new HashMap<>();
        t0 = System.nanoTime();
        for (int i = 0; i < M; i++) h2.put(rnd.nextInt(), "v");
        double msInsHash = (System.nanoTime() - t0) / 1e6;

        Map<Integer, String> t2 = new TreeMap<>();
        Random rnd2 = new Random(7);
        t0 = System.nanoTime();
        for (int i = 0; i < M; i++) t2.put(rnd2.nextInt(), "v");
        double msInsTree = (System.nanoTime() - t0) / 1e6;

        System.out.printf("  插入 %d 条: HashMap %.0f ms，TreeMap %.0f ms（慢 %.1fx）%n",
                M, msInsHash, msInsTree, msInsTree / msInsHash);
        System.out.println("  → 有序结构的插入要维护顺序（旋转/分裂），比哈希贵");
        System.out.println("  → 数据库里这笔账更贵: 还要写磁盘页 + 写 WAL（Python 版实测 4 个索引慢 3.18x）");

        System.out.println("\n== ⑤ 对应关系：内存数据结构 ←→ 数据库索引 ==");
        System.out.println("  HashMap        ←→ 哈希索引（MySQL MEMORY 引擎、PostgreSQL HASH 索引）");
        System.out.println("     只能等值，不能范围/排序；且哈希冲突下退化");
        System.out.println("  TreeMap（红黑树）←→ B+ 树索引（几乎所有数据库的默认）");
        System.out.println("     等值/范围/排序全支持；但红黑树为内存设计，B+ 树为【磁盘】设计");
        System.out.println("     区别在扇出: 红黑树每节点 1 键（树高 log2 n），B+ 树每节点上百键（C++ 版实测树高 3）");
        System.out.println("  → 同一道选择题，因为存储介质不同而有了不同的最优解");

        System.out.println("\n== ⑥ Java 侧的实用提示 ==");
        System.out.println("  想要「有序 + O(1) 点查」→ 两个结构一起维护（数据库也是这么干的:");
        System.out.println("    主键 B+ 树存数据，二级索引 B+ 树存「列值 → 主键」，回表再查一次）");
        System.out.println("  JDBC 侧: DatabaseMetaData.getIndexInfo() 可以列出表上的索引");
        System.out.println("  ⚠️ 别在 Java 里手工缓存整表来「代替索引」——第 46 章实测过它的内存代价");
    }
}
