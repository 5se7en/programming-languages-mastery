import java.lang.management.GarbageCollectorMXBean;
import java.lang.management.ManagementFactory;
import java.lang.ref.WeakReference;

public class Main {

    static class Student {
        String name;
        Student partner;
        byte[] payload = new byte[1024];      // 让对象有点分量
        Student(String name) { this.name = name; }
    }

    static void gcAndWait() throws InterruptedException {
        for (int i = 0; i < 3; i++) { System.gc(); Thread.sleep(100); }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① 可达性：强引用在，对象就在 ==");
        Student s = new Student("小明");
        WeakReference<Student> weak = new WeakReference<>(s);
        gcAndWait();
        System.out.println("强引用还在时 GC 后: weak.get() = "
                + (weak.get() != null ? weak.get().name : "null"));
        s = null;                              // 斩断最后一条从根出发的路
        gcAndWait();
        System.out.println("强引用断掉后 GC 后: weak.get() = " + weak.get()
                + "   <- 从根不可达，回收");

        System.out.println("\n== ② 钥匙实验：循环引用？可达性分析根本不在乎 ==");
        Student x = new Student("小红");
        Student y = new Student("小刚");
        x.partner = y;
        y.partner = x;                         // x ⇄ y 互指成环（Python 的死角）
        WeakReference<Student> wx = new WeakReference<>(x);
        WeakReference<Student> wy = new WeakReference<>(y);
        x = null;
        y = null;                              // 外部引用全断——环还在，但从根到不了
        gcAndWait();
        System.out.println("互指的两个对象 GC 后: wx = " + wx.get() + ", wy = " + wy.get());
        System.out.println("（从根出发标记可达——环内互指再紧，根到不了就是垃圾）");

        System.out.println("\n== ③ 分代的证据：两台收集器各司其职 ==");
        for (GarbageCollectorMXBean bean : ManagementFactory.getGarbageCollectorMXBeans()) {
            System.out.printf("  %-24s 回收 %3d 次，累计 %d ms%n",
                    bean.getName(), bean.getCollectionCount(), bean.getCollectionTime());
        }
        long t0 = System.nanoTime();
        for (int i = 0; i < 5_000_000; i++) {
            Student tmp = new Student("t");    // 朝生夕死的分配压力
            if (tmp.payload.length == 0) System.out.println();
        }
        long t1 = System.nanoTime();
        System.out.printf("分配五百万个临时对象（%.0f ms）之后:%n", (t1 - t0) / 1e6);
        for (GarbageCollectorMXBean bean : ManagementFactory.getGarbageCollectorMXBeans()) {
            System.out.printf("  %-24s 回收 %3d 次，累计 %d ms%n",
                    bean.getName(), bean.getCollectionCount(), bean.getCollectionTime());
        }
        System.out.println("（年轻代收集器忙碌、老年代几乎不动——分代假设在计数器上现形）");
    }
}
