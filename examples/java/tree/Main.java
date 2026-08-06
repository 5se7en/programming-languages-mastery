// 第 21 章 · 树 —— Java 示例
// 运行：javac Main.java && java Main

import java.util.*;

public class Main {

    // ---------- 手写二叉搜索树 ----------
    static class Node {
        int v;
        Node left, right;
        Node(int v) { this.v = v; }
    }

    /** 迭代插入，避免深树时递归爆栈 */
    static Node insert(Node root, int v) {
        Node node = new Node(v);
        if (root == null) return node;
        Node cur = root;
        while (true) {
            if (v < cur.v) {
                if (cur.left == null) { cur.left = node; return root; }
                cur = cur.left;
            } else {
                if (cur.right == null) { cur.right = node; return root; }
                cur = cur.right;
            }
        }
    }

    /** 中序遍历：左 → 根 → 右，结果必然有序 */
    static void inorder(Node node, List<Integer> out) {
        if (node == null) return;
        inorder(node.left, out);
        out.add(node.v);
        inorder(node.right, out);
    }

    /** 树高决定查找的最坏代价（迭代版） */
    static int height(Node root) {
        if (root == null) return 0;
        int h = 0;
        Deque<Object[]> stack = new ArrayDeque<>();
        stack.push(new Object[]{root, 1});
        while (!stack.isEmpty()) {
            Object[] top = stack.pop();
            Node n = (Node) top[0];
            int d = (Integer) top[1];
            h = Math.max(h, d);
            if (n.left != null) stack.push(new Object[]{n.left, d + 1});
            if (n.right != null) stack.push(new Object[]{n.right, d + 1});
        }
        return h;
    }

    public static void main(String[] args) {
        System.out.println("=== 1. 二叉搜索树：左小右大 ===");
        int[] values = {50, 30, 70, 20, 40, 60, 80};
        Node root = null;
        for (int v : values) root = insert(root, v);

        List<Integer> out = new ArrayList<>();
        inorder(root, out);
        System.out.println("插入顺序: " + Arrays.toString(values));
        System.out.println("中序遍历: " + out + " ← 自动有序！这是 BST 的定义性质");
        System.out.println("树高: " + height(root));

        System.out.println("\n=== 2. ⚠️ BST 的退化：有序插入会变成链表 ===");
        int N = 2000;

        List<Integer> shuffled = new ArrayList<>();
        for (int i = 0; i < N; i++) shuffled.add(i);
        Collections.shuffle(shuffled, new Random(42));   // 固定种子，结果可复现

        Node randomTree = null;
        for (int v : shuffled) randomTree = insert(randomTree, v);

        Node sortedTree = null;
        for (int i = 0; i < N; i++) sortedTree = insert(sortedTree, i);

        System.out.printf("随机插入 %d 个数 → 树高 %d%n", N, height(randomTree));
        System.out.printf("有序插入 %d 个数 → 树高 %d   ← 完全退化成链表！%n", N, height(sortedTree));
        System.out.printf("理想树高 log2(%d) ≈ %.0f%n", N, Math.log(N) / Math.log(2));
        System.out.println("→ 这就是「平衡树」（AVL / 红黑树）存在的全部理由");

        System.out.println("\n=== 3. TreeMap：红黑树，自动按键排序 ===");
        TreeMap<String, Integer> scores = new TreeMap<>();
        scores.put("zebra", 1);
        scores.put("apple", 2);
        scores.put("mango", 3);
        System.out.println("插入顺序: zebra, apple, mango");
        System.out.println("TreeMap : " + scores.keySet() + " ← 自动排序");

        HashMap<String, Integer> hashScores = new HashMap<>(scores);
        System.out.println("HashMap : " + hashScores.keySet() + " ← 无序");

        System.out.println("\n=== 4. TreeMap 能做哈希做不到的事 ===");
        TreeMap<Integer, String> tm = new TreeMap<>();
        for (int i = 0; i < 200000; i++) tm.put(i, "v" + i);

        System.out.println("firstKey()        → " + tm.firstKey() + "   最小键");
        System.out.println("lastKey()         → " + tm.lastKey() + "   最大键");
        System.out.println("subMap(100,105)   → " + tm.subMap(100, 105).keySet() + "   范围查询");
        System.out.println("floorKey(99999)   → " + tm.floorKey(99999) + "   小于等于它的最大键");
        System.out.println("ceilingKey(50)    → " + tm.ceilingKey(50) + "   大于等于它的最小键");
        System.out.println("→ 有序 = 能回答「大于/范围/最接近/排序」，这正是哈希丢掉的能力");

        System.out.println("\n=== 5. 有序的代价：TreeMap vs HashMap ===");
        int M = 200000;
        // 预热，让 JIT 先编译好（第 05 章）
        for (int w = 0; w < 3; w++) {
            Map<Integer, Integer> warm = new HashMap<>();
            for (int i = 0; i < M; i++) warm.put(i, i);
            Map<Integer, Integer> warmT = new TreeMap<>();
            for (int i = 0; i < M / 10; i++) warmT.put(i, i);
        }

        Map<Integer, Integer> hash = new HashMap<>();
        long t0 = System.nanoTime();
        for (int i = 0; i < M; i++) hash.put(i, i);
        for (int i = 0; i < M; i++) hash.get(i);
        long t1 = System.nanoTime();

        Map<Integer, Integer> tree = new TreeMap<>();
        for (int i = 0; i < M; i++) tree.put(i, i);
        for (int i = 0; i < M; i++) tree.get(i);
        long t2 = System.nanoTime();

        double hashMs = (t1 - t0) / 1e6, treeMs = (t2 - t1) / 1e6;
        System.out.printf("%d 次插入+查找:%n", M);
        System.out.printf("  HashMap: %.0f ms%n", hashMs);
        System.out.printf("  TreeMap: %.0f ms%n", treeMs);
        System.out.printf("  → 哈希快约 %.1f 倍，这就是「有序」的价格%n", treeMs / hashMs);
        System.out.println("  ⚠️ 数字依赖环境，记住结论：哈希更快，树更全能");

        System.out.println("\n=== 6. ⚠️ 坑：TreeMap 用 compareTo 判等，不是 equals ===");
        // Comparator 只比较年龄 → 年龄相同的会被当成同一个键
        TreeMap<String, String> byLength =
                new TreeMap<>(Comparator.comparingInt(String::length));
        byLength.put("abc", "第一个");
        byLength.put("xyz", "第二个");   // 长度也是 3 → 覆盖了上一个！
        System.out.println("放入 \"abc\" 和 \"xyz\"（Comparator 只比长度）");
        System.out.println("结果 size = " + byLength.size() + " ← 被当成同一个键！");
        System.out.println("内容: " + byLength);
        System.out.println("→ Comparator 必须覆盖所有区分性字段");

        System.out.println("\n=== 7. PriorityQueue 是堆（数组存的完全二叉树）===");
        PriorityQueue<Integer> pq = new PriorityQueue<>(Arrays.asList(5, 3, 8, 1, 9));
        System.out.println("堆的内部数组: " + pq + " ← 不是有序数组！只保证堆顶最小");
        System.out.print("逐个 poll: ");
        List<Integer> order = new ArrayList<>();
        while (!pq.isEmpty()) order.add(pq.poll());
        System.out.println(order + " ← 这样才有序");
    }
}
