// 第 18 章 · 栈 — Java 示例
// 运行：javac *.java && java Main
import java.util.*;

public class Main {
    static boolean isBalanced(String s) {
        Map<Character, Character> pairs = Map.of(')', '(', ']', '[', '}', '{');
        Deque<Character> st = new ArrayDeque<>();
        for (char ch : s.toCharArray()) {
            if ("([{".indexOf(ch) >= 0) st.push(ch);
            else if (pairs.containsKey(ch)) {
                if (st.isEmpty() || st.pop() != pairs.get(ch)) return false;
            }
        }
        return st.isEmpty();
    }

    public static void main(String[] args) {
        // 1. ✅ 官方推荐：ArrayDeque
        Deque<Integer> stack = new ArrayDeque<>();
        stack.push(1); stack.push(2); stack.push(3);
        System.out.println("ArrayDeque 栈顶: " + stack.peek() + " | pop(): " + stack.pop());

        // 2. ⚠️ java.util.Stack 的设计缺陷：继承 Vector，竟能按下标访问
        Stack<String> bad = new Stack<>();
        bad.push("底"); bad.push("中"); bad.push("顶");
        System.out.println("\n⚠️ Stack 继承 Vector → bad.get(0) = " + bad.get(0)
            + "  ← 拿到了栈底，破坏了「只能一端进出」的语义");

        // 3. 性能对比（Stack 的方法都是 synchronized）
        int N = 5000000;
        for (int w = 0; w < 2; w++) {            // 预热
            Stack<Integer> s = new Stack<>(); Deque<Integer> d = new ArrayDeque<>();
            for (int i = 0; i < 500000; i++) { s.push(i); d.push(i); }
        }
        Stack<Integer> st = new Stack<>();
        long t0 = System.nanoTime();
        for (int i = 0; i < N; i++) st.push(i);
        for (int i = 0; i < N; i++) st.pop();
        long t1 = System.nanoTime();
        Deque<Integer> dq = new ArrayDeque<>();
        for (int i = 0; i < N; i++) dq.push(i);
        for (int i = 0; i < N; i++) dq.pop();
        long t2 = System.nanoTime();
        System.out.printf("%npush+pop 各 %d 次: Stack %.0fms vs ArrayDeque %.0fms → 快 %.1f 倍%n",
            N, (t1-t0)/1e6, (t2-t1)/1e6, (double)(t1-t0)/(t2-t1));

        // 4. 括号匹配
        System.out.println();
        for (String s : List.of("(a[b]{c})", "(a[b)]", "((("))
            System.out.printf("括号匹配 %-10s → %b%n", s, isBalanced(s));
    }
}
