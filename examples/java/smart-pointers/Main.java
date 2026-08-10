import java.lang.ref.Cleaner;
import java.lang.ref.SoftReference;
import java.lang.ref.WeakReference;

public class Main {

    static class Student {
        final String name;
        Student partner;
        Student(String name) { this.name = name; }
    }

    static void gcAndWait() throws InterruptedException {
        for (int i = 0; i < 3; i++) { System.gc(); Thread.sleep(80); }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① Java 的引用 ≈ C++ 的 shared_ptr（但不数数）==");
        Student s = new Student("小明");
        WeakReference<Student> weak = new WeakReference<>(s);
        System.out.println("    强引用在: weak.get() = " + weak.get().name);
        s = null;
        gcAndWait();
        System.out.println("    强引用断: weak.get() = " + weak.get()
                + "   <- 无需 delete，也无需选指针类型");

        System.out.println("\n== ② 钥匙实验：同样的环，Java 毫无压力 ==");
        Student x = new Student("环-甲");
        Student y = new Student("环-乙");
        x.partner = y;
        y.partner = x;                         // 成环（C++ 在这里泄漏）
        WeakReference<Student> wx = new WeakReference<>(x);
        WeakReference<Student> wy = new WeakReference<>(y);
        x = null; y = null;
        gcAndWait();
        System.out.println("    成环对象 GC 后: wx=" + wx.get() + ", wy=" + wy.get());
        System.out.println("    （可达性分析不数引用——C++ 需要 weak_ptr，Java 什么都不用做）");

        System.out.println("\n== ③ 引用强度 ≈ 智能指针家族 ==");
        Student strong = new Student("强引用");
        SoftReference<Student> soft = new SoftReference<>(new Student("软引用"));
        WeakReference<Student> weak2 = new WeakReference<>(new Student("弱引用"));
        System.out.println("    强引用（默认）  ≈ shared_ptr：可达即活 -> " + strong.name);
        System.out.println("    SoftReference   ≈ 无对应物：内存紧张才回收 -> "
                + (soft.get() != null ? soft.get().name : "已回收"));
        System.out.println("    WeakReference   ≈ weak_ptr：下次 GC 即回收 -> "
                + (weak2.get() != null ? weak2.get().name : "已回收"));

        System.out.println("\n== ④ Cleaner ≈ 自定义删除器（deleter）==");
        Cleaner cleaner = Cleaner.create();
        Object resource = new Object();
        cleaner.register(resource, () -> System.out.println("    [清理] Cleaner 回调触发"));
        System.out.println("    注册了清理动作——但触发时机由 GC 决定（第 36/37 章：不可靠）");
        System.out.println("    （C++ 的 unique_ptr<T, Deleter> 是确定性的，Cleaner 不是）");

        System.out.println("\n== ⑤ Java 没有的：唯一所有权 ==");
        System.out.println("    任何引用赋值都是共享——无法在类型层面表达「只有我能删」");
        System.out.println("    好处：不用想所有权；代价：对象何时死不可知（第 36 章实测）");
    }
}
