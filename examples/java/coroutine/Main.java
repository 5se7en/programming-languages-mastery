import java.lang.reflect.Method;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.Iterator;
import java.util.List;
import java.util.NoSuchElementException;

public class Main {

    /** ① Java 没有 yield —— 只能手工把「状态」写成对象字段（编译器不代劳）。 */
    static class Counter implements Iterator<String> {
        private final String name;
        private final int n;
        private int i = 0;          // ← 这就是「被迫手工维护的状态机」
        private int total = 0;

        Counter(String name, int n) { this.name = name; this.n = n; }

        @Override public boolean hasNext() { return i < n; }

        @Override public String next() {
            if (!hasNext()) throw new NoSuchElementException();
            total += i;
            return String.format("%s: 第 %d 步，累计 %d", name, i++, total);
        }
    }

    /** ③ 手工协程调度器：与其他语言完全同构 */
    @SafeVarargs
    static List<String> scheduler(Iterator<String>... tasks) {
        Deque<Iterator<String>> queue = new ArrayDeque<>(List.of(tasks));
        List<String> trace = new ArrayList<>();
        while (!queue.isEmpty()) {
            Iterator<String> task = queue.poll();
            if (task.hasNext()) {
                trace.add(task.next());     // 推进一步
                queue.offer(task);          // 排回队尾
            }
        }
        return trace;
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① Java 没有 yield 关键字 ==");
        Iterator<String> gen = new Counter("A", 3);
        System.out.println("  只能手工写 Iterator，把 i 和 total 提升为对象字段");
        System.out.println("  第一次 next(): " + gen.next());
        System.out.println("  第二次 next(): " + gen.next() + "   ← 状态存在对象里，不是栈上");
        System.out.println("  ↑ 别的语言由编译器生成这个状态机，Java 要你亲手写");

        System.out.println("\n== ② 为什么 Java 一直没加 yield ==");
        System.out.println("  因为它选了另一条路：与其让「函数」可暂停，不如让「线程」变廉价");
        System.out.println("  → 虚拟线程（Java 21+）：一条真实线程上跑成千上万个虚拟线程");
        System.out.println("  → 阻塞代码自动变让出，无需 async/yield 改写（第 42 章的传染性问题不存在）");

        System.out.println("\n== ③ 钥匙实验：三十行搭一个协程调度器 ==");
        for (String line : scheduler(new Counter("协程甲", 3), new Counter("协程乙", 2)))
            System.out.println("    " + line);
        System.out.println("  ↑ 两个「协程」交替推进——与其他四种语言输出完全一致");

        System.out.println("\n== ④ 虚拟线程可用性检测 ==");
        boolean hasVirtual = false;
        try {
            Method m = Thread.class.getMethod("ofVirtual");
            hasVirtual = true;
            System.out.println("  ✅ 本机支持虚拟线程: " + m);
        } catch (NoSuchMethodException e) {
            System.out.println("  ❌ 本机 Java " + System.getProperty("java.version")
                    + " 不支持虚拟线程（需要 21+）");
        }

        System.out.println("\n== ⑤ 平台线程的重量（对照组）==");
        final int N = 5_000;
        long t0 = System.nanoTime();
        Thread[] threads = new Thread[N];
        for (int i = 0; i < N; i++) {
            threads[i] = new Thread(() -> {
                try { Thread.sleep(10); } catch (InterruptedException ignored) {}
            });
            threads[i].start();
        }
        for (Thread t : threads) t.join();
        double ms = (System.nanoTime() - t0) / 1e6;
        System.out.printf("  %d 个平台线程: %.0f ms（每个约 %.1f μs）%n", N, ms, ms * 1000 / N);
        System.out.printf("  每个保留约 1 MB 栈 → %d 个就要 %d GB 虚拟地址（第 31 章实测）%n",
                1_000_000, 1_000);
        if (!hasVirtual) {
            System.out.println("  虚拟线程若可用: 同样 " + N + " 个只需几毫秒，每个约几百字节");
            System.out.println("  （它的栈住在 JVM 堆上，按需增长——第 32 章「帧可以住在堆上」的终极兑现）");
        }

        System.out.println("\n== ⑥ 虚拟线程的核心机制：挂载与卸载 ==");
        System.out.println("  虚拟线程运行时【挂载】到一条平台线程（carrier thread）上");
        System.out.println("  遇到阻塞（sleep/socket read）时：");
        System.out.println("    ① JVM 把它的【栈帧从平台线程栈复制到堆】← 关键的一步");
        System.out.println("    ② 平台线程被【卸载】，立刻去跑别的虚拟线程");
        System.out.println("    ③ I/O 就绪时，栈帧从堆复制回来，继续执行");
        System.out.println("  → 对你的代码来说，Thread.sleep() 看起来还是阻塞，实际上是让出");

        System.out.println("\n== ⑦ 两条路线的最终对照 ==");
        System.out.println("  async/await（JS/C#/Python）: 编译器改写函数 → 函数有颜色，生态分裂");
        System.out.println("  虚拟线程（Java 21+/Go）:     运行时搬运栈帧 → 代码不变，老库直接受益");
        System.out.println("  代价对比:");
        System.out.println("    async  : 内存最省（只存必要变量），但要重写所有 I/O 代码");
        System.out.println("    虚拟线程: 内存稍多（保存整个栈），但 synchronized 等场景仍会钉住线程");
    }
}
