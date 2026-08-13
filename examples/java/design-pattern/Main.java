import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.Function;

/**
 * 设计模式：单例的四种写法与那个错了十年的双重检查锁定；以及 Java 8 之后模式的退化。
 */
public class Main {

    // ============ ① 单例的四种写法 ============

    /** 写法一: 饿汉——类加载时就创建（JVM 保证类初始化线程安全） */
    static class EagerSingleton {
        static final AtomicInteger created = new AtomicInteger();
        private static final EagerSingleton INSTANCE = new EagerSingleton();
        private EagerSingleton() { created.incrementAndGet(); }
        static EagerSingleton getInstance() { return INSTANCE; }
    }

    /** 写法二: 懒汉 + 无同步——【线程不安全】，本例会实测它造出多个实例 */
    static class UnsafeLazySingleton {
        static final AtomicInteger created = new AtomicInteger();
        private static UnsafeLazySingleton instance;
        private UnsafeLazySingleton() {
            created.incrementAndGet();
            try { Thread.sleep(1); } catch (InterruptedException ignored) { }  // 放大竞争窗口
        }
        static UnsafeLazySingleton getInstance() {
            if (instance == null) instance = new UnsafeLazySingleton();        // ⚠️ 检查-创建不原子
            return instance;
        }
    }

    /** 写法三: 双重检查锁定 + volatile——正确版（少了 volatile 就是错的，见 ②） */
    static class DclSingleton {
        static final AtomicInteger created = new AtomicInteger();
        private static volatile DclSingleton instance;      // ← volatile 是必须的
        private DclSingleton() {
            created.incrementAndGet();
            try { Thread.sleep(1); } catch (InterruptedException ignored) { }
        }
        static DclSingleton getInstance() {
            if (instance == null) {                          // 第一次检查: 不加锁，快路径
                synchronized (DclSingleton.class) {
                    if (instance == null)                    // 第二次检查: 加锁后再确认
                        instance = new DclSingleton();
                }
            }
            return instance;
        }
    }

    /** 写法四: 静态内部类（懒加载 + 线程安全 + 无锁开销）—— Java 的最佳实践 */
    static class HolderSingleton {
        static final AtomicInteger created = new AtomicInteger();
        private HolderSingleton() { created.incrementAndGet(); }
        private static class Holder {                        // 内部类首次被访问时才加载
            static final HolderSingleton INSTANCE = new HolderSingleton();
        }
        static HolderSingleton getInstance() { return Holder.INSTANCE; }
    }

    // ============ ③ 策略模式：GoF vs Java 8 ============
    interface SortStrategy { int compare(String a, String b); }     // GoF: 需要接口
    static class ByLength implements SortStrategy {
        public int compare(String a, String b) { return a.length() - b.length(); }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① 懒汉单例的线程不安全（实测造出多个实例）==");
        UnsafeLazySingleton.created.set(0);
        List<UnsafeLazySingleton> got = runConcurrently(16, UnsafeLazySingleton::getInstance);
        long distinct = got.stream().distinct().count();
        System.out.printf("  16 个线程同时调 getInstance(): 构造器执行了 %d 次，拿到 %d 个不同实例%n",
                UnsafeLazySingleton.created.get(), distinct);
        System.out.println("  → 「if (instance == null) instance = new ...」是【检查-创建】两步，不原子");
        System.out.println("  → 第 46/48 章的读-改-写在这里换了个形状: 竞态条件依然是同一个问题");

        System.out.println("\n== ② 双重检查锁定：正确版（volatile）实测 ==");
        DclSingleton.created.set(0);
        List<DclSingleton> dcl = runConcurrently(16, DclSingleton::getInstance);
        System.out.printf("  16 个线程同时调: 构造器执行了 %d 次，拿到 %d 个不同实例 ✓%n",
                DclSingleton.created.get(), dcl.stream().distinct().count());
        System.out.println("  → 但这个写法【在 Java 5 之前是错的】，而且错得极其隐蔽:");
        System.out.println("     instance = new DclSingleton() 不是一个原子操作，它是三步:");
        System.out.println("       ① 分配内存  ② 调用构造器  ③ 把引用赋给 instance");
        System.out.println("     JVM/CPU 允许把 ②③ 【重排序】（第 41 章的内存模型）→ 于是可能:");
        System.out.println("       线程 A 执行到 ③ 但还没执行 ②（instance 非空，但对象没初始化完）");
        System.out.println("       线程 B 的第一次检查看到 instance != null → 直接返回【半成品对象】");
        System.out.println("  → volatile 的作用不是「保证可见性」这么简单，它还【禁止这个重排序】");
        System.out.println("  → Java 5 (JSR-133) 修订内存模型后，volatile 才真正让 DCL 正确");
        System.out.println("  → 这是「模式本身是对的，但语言的内存模型让它错了十年」的经典案例");

        System.out.println("\n== ③ 静态内部类：Java 单例的最佳写法（实测）==");
        HolderSingleton.created.set(0);
        List<HolderSingleton> holder = runConcurrently(16, HolderSingleton::getInstance);
        System.out.printf("  16 个线程同时调: 构造器执行了 %d 次，%d 个不同实例 ✓%n",
                HolderSingleton.created.get(), holder.stream().distinct().count());
        System.out.println("  → 原理: JVM 保证【类初始化】是线程安全的（JLS 12.4.2）");
        System.out.println("     内部类 Holder 只在首次被访问时加载 → 天然懒加载 + 天然线程安全 + 零锁");
        System.out.println("  → 比 DCL 简单、比饿汉懒——用【语言机制】代替【手写同步】");
        System.out.println("  → 更彻底的是枚举单例（Effective Java 推荐）: 还能防反射与序列化破坏");

        System.out.println("\n== ④ 但单例本身是个可疑的模式 ==");
        System.out.println("  它同时做了两件事: ① 保证只有一个实例  ② 提供【全局访问点】");
        System.out.println("  ② 才是问题所在: 全局访问 = 隐藏的依赖（第 55 章的服务定位器同款批评）");
        System.out.println("    · 测试时换不掉（第 52 章实测过依赖写死的后果）");
        System.out.println("    · 谁用了它，从签名上完全看不出来");
        System.out.println("  → 现代做法: 让容器管理【生命周期为单例】，通过构造器注入（第 55 章实测）");
        System.out.println("  → 「我需要唯一实例」是合理需求，「我要全局静态访问」不是");

        System.out.println("\n== ⑤ 策略模式：Java 8 之后也退化了（实测）==");
        List<String> words = new ArrayList<>(List.of("banana", "kiwi", "apple", "fig"));
        List<String> gof = new ArrayList<>(words);
        SortStrategy s = new ByLength();
        gof.sort(s::compare);
        List<String> lambda = new ArrayList<>(words);
        lambda.sort(Comparator.comparingInt(String::length));
        System.out.println("  GoF 版（接口 + 实现类）: " + gof);
        System.out.println("  Java 8 版 Comparator.comparingInt(String::length): " + lambda);
        System.out.println("  结果一致: " + gof.equals(lambda));
        System.out.println("  → 有了 lambda 与函数式接口，Java 的策略模式也变成了「传一个函数」");
        System.out.println("  → 但注意 Java 的函数【仍然是对象】: lambda 编译成一个函数式接口的实例");
        System.out.println("     所以 Java 是「用语法糖模拟一等函数」，与 Python 的原生一等函数仍有差别");

        System.out.println("\n== ⑥ 模式的必要性由语言决定（本章总纲）==");
        System.out.println("  同一个「把行为参数化」的需求:");
        System.out.println("    C++98/Java 7 : 接口 + 实现类 + 上下文（GoF 策略模式，二三十行）");
        System.out.println("    Java 8+/C#   : lambda + 函数式接口（一行）");
        System.out.println("    Python/JS    : 直接传函数（零额外结构）");
        System.out.println("  → 模式没有变，【语言把模式内建了】");
        System.out.println("  → 所以读 GoF 时要问: 这个模式在【我的语言里】还是问题吗？");
    }

    /** 让 N 个线程同时调用同一个工厂方法（用 CountDownLatch 对齐起跑线，最大化竞争） */
    static <T> List<T> runConcurrently(int n, java.util.function.Supplier<T> factory)
            throws Exception {
        CountDownLatch start = new CountDownLatch(1), done = new CountDownLatch(n);
        List<T> results = java.util.Collections.synchronizedList(new ArrayList<>());
        for (int i = 0; i < n; i++) {
            new Thread(() -> {
                try {
                    start.await();
                    results.add(factory.get());
                } catch (InterruptedException ignored) {
                } finally { done.countDown(); }
            }).start();
        }
        start.countDown();
        done.await();
        return results;
    }
}
