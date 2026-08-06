// 第 17 章 · 列表 — Java 示例
// 运行：javac *.java && java Main
import java.util.*;

public class Main {
    public static void main(String[] args) {
        // 1. ArrayList 是动态数组，增长因子 1.5（newCap = oldCap + (oldCap >> 1)）
        List<Integer> list = new ArrayList<>();
        list.add(92); list.add(75);
        System.out.println("ArrayList: " + list + " | size: " + list.size());
        System.out.println("Java 不暴露 capacity，但可通过构造参数预分配");

        int N = 200000;
        // 预热，排除 JIT 干扰
        for (int w = 0; w < 3; w++) { List<Integer> t = new ArrayList<>(); for (int i=0;i<N;i++) t.add(i); }

        // 2. 预分配 vs 不预分配
        long t0 = System.nanoTime();
        List<Integer> noPre = new ArrayList<>();
        for (int i = 0; i < N; i++) noPre.add(i);
        long t1 = System.nanoTime();
        List<Integer> pre = new ArrayList<>(N);        // 预分配容量
        for (int i = 0; i < N; i++) pre.add(i);
        long t2 = System.nanoTime();
        System.out.printf("%n追加 %d 个元素: 不预分配 %.1fms vs 预分配 %.1fms → 快 %.1f 倍%n",
            N, (t1-t0)/1e6, (t2-t1)/1e6, (double)(t1-t0)/(t2-t1));

        // 3. 头部插入：ArrayList O(n) vs ArrayDeque O(1)
        int M = 20000;
        t0 = System.nanoTime();
        List<Integer> al = new ArrayList<>();
        for (int i = 0; i < M; i++) al.add(0, i);       // O(n)
        t1 = System.nanoTime();
        Deque<Integer> dq = new ArrayDeque<>();
        for (int i = 0; i < M; i++) dq.addFirst(i);     // O(1)
        t2 = System.nanoTime();
        System.out.printf("%d 次头部插入: ArrayList %.1fms vs ArrayDeque %.1fms → 快 %.0f 倍%n",
            M, (t1-t0)/1e6, (t2-t1)/1e6, (double)(t1-t0)/(t2-t1));

        // 4. 不可变列表不能修改
        try {
            List.of(1, 2, 3).add(4);
        } catch (UnsupportedOperationException e) {
            System.out.println("\nList.of(...) 是不可变的: " + e.getClass().getSimpleName());
        }
        System.out.println("需要可变列表: new ArrayList<>(List.of(1,2,3))");
    }
}
