// 第 19 章 · 队列 — Java 示例
// 运行：javac *.java && java Main
import java.util.*;

public class Main {
    public static void main(String[] args) {
        // 1. ✅ 推荐 ArrayDeque（底层环形数组）
        Queue<String> q = new ArrayDeque<>();
        q.offer("A"); q.offer("B"); q.offer("C");
        System.out.println("ArrayDeque 出队: " + q.poll() + " | 队首: " + q.peek() + " | 剩余: " + q.size());

        // 2. 两套 API：抛异常 vs 返回特殊值
        Queue<String> empty = new ArrayDeque<>();
        System.out.println("空队列 poll(): " + empty.poll() + " ← 返回 null，不抛异常");
        try { empty.remove(); }
        catch (NoSuchElementException e) {
            System.out.println("空队列 remove(): " + e.getClass().getSimpleName() + " ← 抛异常");
        }

        // 3. 栈 vs 队列 = DFS vs BFS
        Map<Integer, List<Integer>> tree = Map.of(
            1, List.of(2,3), 2, List.of(4,5), 3, List.of(6,7),
            4, List.of(), 5, List.of(), 6, List.of(), 7, List.of());
        System.out.println("\n用栈  (LIFO) → DFS: " + traverse(tree, 1, true));
        System.out.println("用队列(FIFO) → BFS: " + traverse(tree, 1, false) + " ← 逐层扫描");

        // 4. 优先队列：默认最小堆
        PriorityQueue<int[]> pq = new PriorityQueue<>(Comparator.comparingInt(a -> a[0]));
        pq.offer(new int[]{3, 300}); pq.offer(new int[]{1, 100}); pq.offer(new int[]{2, 200});
        StringBuilder sb = new StringBuilder();
        while (!pq.isEmpty()) sb.append(pq.poll()[1]).append(" ");
        System.out.println("\n优先队列出队(默认最小堆): " + sb.toString().trim());

        // ⚠️ 优先队列的迭代顺序不是优先级顺序
        PriorityQueue<Integer> p2 = new PriorityQueue<>(List.of(3, 1, 2));
        System.out.println("⚠️ 直接打印优先队列: " + p2 + " ← 这是堆的内部布局，不是有序序列");

        // 5. ArrayDeque 兼作栈与队列（双端队列是两者的超集）
        Deque<Integer> dq = new ArrayDeque<>();
        dq.addFirst(1); dq.addLast(2);
        System.out.println("\n双端队列: 首=" + dq.peekFirst() + " 尾=" + dq.peekLast()
            + " ← 既能当栈也能当队列");
    }

    static List<Integer> traverse(Map<Integer, List<Integer>> tree, int root, boolean useStack) {
        Deque<Integer> box = new ArrayDeque<>();
        box.add(root);
        List<Integer> order = new ArrayList<>();
        while (!box.isEmpty()) {
            int node = useStack ? box.pollLast() : box.pollFirst();   // ← 唯一的区别
            order.add(node);
            box.addAll(tree.get(node));
        }
        return order;
    }
}
