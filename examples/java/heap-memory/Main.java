import java.util.ArrayList;
import java.util.List;

public class Main {

    static class Student {
        String name;
        int score;
        Student(String name, int score) { this.name = name; this.score = score; }
    }

    static final List<byte[]> LEAK = new ArrayList<>();   // 静态集合：Java 泄漏的第一现场

    static long used(Runtime rt) { return rt.totalMemory() - rt.freeMemory(); }

    public static void main(String[] args) throws Exception {
        Runtime rt = Runtime.getRuntime();

        System.out.println("== ① 分配一千万个对象：JIT 先给你上一课 ==");
        for (int r = 0; r < 3; r++) { allocate(10_000_000); allocateEscaping(10_000_000); }  // 预热
        long t0 = System.nanoTime();
        long sum = allocate(10_000_000);                           // 对象不逃逸
        long t1 = System.nanoTime();
        long sum2 = allocateEscaping(10_000_000);                  // 对象存进数组：真实分配
        long t2 = System.nanoTime();
        System.out.printf("对象不逃逸:   %6.1f ms（%.2f ns/个） <- 逃逸分析把分配整个消除了！%n",
                (t1 - t0) / 1e6, (t1 - t0) / 1e7);
        System.out.printf("对象逃逸:     %6.1f ms（%.2f ns/个） <- 真实的 TLAB 指针碰撞分配%n",
                (t2 - t1) / 1e6, (t2 - t1) / 1e7);
        System.out.println("（即便真分配，也只是指针加一下——仍远快于 malloc 的空闲链表检索）");
        if (sum + sum2 == 42) System.out.println();

        System.out.println("\n== ② 代价在回收端：临时对象没让内存上涨 ==");
        System.gc(); Thread.sleep(200);
        long before = used(rt);
        for (int i = 0; i < 10; i++) allocate(1_000_000);          // 一千万个临时对象
        System.gc(); Thread.sleep(200);
        long after = used(rt);
        System.out.printf("一千万个临时对象前后，堆占用变化: %.1f MB —— GC 收走了它们%n",
                (after - before) / 1024.0 / 1024);

        System.out.println("\n== ③ 泄漏的形态：引用还在，GC 无能为力 ==");
        System.gc(); Thread.sleep(200);
        long l0 = used(rt);
        for (int i = 0; i < 50; i++) {
            LEAK.add(new byte[1024 * 1024]);                       // 每轮"缓存"1 MB，从不清理
        }
        System.gc(); Thread.sleep(200);
        long l1 = used(rt);
        System.out.printf("静态 List 积累 50 MB 后，System.gc() 也收不回: 堆增长 %.1f MB%n",
                (l1 - l0) / 1024.0 / 1024);
        System.out.println("（Java 没有悬垂指针，但\"忘了删的引用\"就是它的内存泄漏）");
        System.out.println("（彩蛋：增长是 100 MB 而非 50 MB——1 MB 数组在 G1 里是 humongous 对象，");
        System.out.println("  独占整个 2 MB region；实测加 -XX:G1HeapRegionSize=4m 后恰为 50 MB）");
    }

    static long allocate(int n) {
        long sum = 0;
        for (int i = 0; i < n; i++) {
            Student s = new Student("s", i);
            sum += s.score;
        }
        return sum;
    }

    static final Student[] HOLD = new Student[16];

    static long allocateEscaping(int n) {
        long sum = 0;
        for (int i = 0; i < n; i++) {
            Student s = new Student("s", i);
            HOLD[i & 15] = s;                 // 逃逸：对象必须真的建在堆上
            sum += s.score;
        }
        return sum;
    }
}
