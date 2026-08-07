// 第 28 章 · 接口 —— Java 示例
// 运行：javac Main.java && java Main
// Java 的接口经历三次演进，每次都在回答「接口能不能有实现」

import java.util.*;
import java.util.stream.Collectors;

public class Main {

    // ---------- ① 接口 = 只有契约，没有状态 ----------
    interface Flyable   { String fly(); }
    interface Swimmable { String swim(); }
    interface Walkable  { String walk(); }

    abstract static class Animal {
        final String name;                                // 抽象类可以有状态
        Animal(String name) { this.name = name; }
    }

    // 继承一个类（拿实现），实现多个接口（拿契约）
    static class Duck extends Animal implements Flyable, Swimmable, Walkable {
        Duck(String name) { super(name); }
        public String fly()  { return "飞"; }
        public String swim() { return "游"; }
        public String walk() { return "走"; }
    }

    // ---------- ② Java 8 默认方法：让接口能演进 ----------
    interface Storage {
        String save(String data);                          // 抽象方法（Java 1.0）

        default String saveAll(List<String> items) {       // 默认方法（Java 8）
            return items.stream().map(this::save).collect(Collectors.joining("; "));
        }

        static Storage inMemory() { return d -> "内存: " + d; }   // 静态方法（Java 8）
    }

    static class FileStorage implements Storage {
        public String save(String data) { return "文件: " + data; }
        // 不用实现 saveAll —— 用接口的默认实现
    }

    // ---------- ③ ⚠️ 默认方法的菱形冲突 ----------
    interface A { default String hello() { return "A"; } }
    interface B { default String hello() { return "B"; } }

    // class Conflict implements A, B { }
    // ↑ 编译错误：类 Conflict 从类型 A 和 B 中继承了 hello() 的不相关默认值
    static class Resolved implements A, B {
        public String hello() {
            return A.super.hello() + "+" + B.super.hello();   // 专为此而生的语法
        }
    }

    // 规则②：子接口的默认方法 > 父接口的
    interface Base    { default String who() { return "Base"; } }
    interface Derived extends Base { default String who() { return "Derived"; } }
    static class UseDerived implements Base, Derived { }      // 不冲突，Derived 更具体

    // ---------- ④ 接口没有实例状态 ----------
    interface Counter {
        int LIMIT = 100;          // ⚠️ 自动是 public static final（常量），不是实例字段
        default int limit() { return LIMIT; }
    }
    static class C1 implements Counter { }
    static class C2 implements Counter { }

    // ---------- ⑤ 依赖倒置 ----------
    static class ReportService {
        private final Storage storage;                     // 依赖契约，不依赖实现
        ReportService(Storage storage) { this.storage = storage; }
        String generate(String content) { return storage.save("报表[" + content + "]"); }
    }

    // ---------- ⑥ 函数式接口 ----------
    @FunctionalInterface
    interface Transformer { String apply(String s); }

    public static void main(String[] args) {
        System.out.println("=== 1. 接口 = 只有契约，所以能实现任意多个 ===");
        Duck d = new Duck("唐老鸭");
        System.out.println("  class Duck extends Animal implements Flyable, Swimmable, Walkable");
        System.out.printf("    %s 能: %s %s %s%n", d.name, d.fly(), d.swim(), d.walk());
        System.out.println("  → 继承一个类（拿实现），实现多个接口（拿契约）");
        System.out.println("  → 这就是 Java 的取舍：单继承避免菱形，多接口保留表达力");

        System.out.println("\n=== 2. ⚠️ 能多实现的根本原因：接口没有实例状态 ===");
        C1 c1 = new C1();
        C2 c2 = new C2();
        System.out.println("  interface Counter { int LIMIT = 100; }");
        System.out.println("    c1.limit() = " + c1.limit() + "   c2.limit() = " + c2.limit());
        System.out.println("    Counter.LIMIT = " + Counter.LIMIT);
        System.out.println("  → 接口里的字段自动是 public static final（常量），不是实例状态");
        System.out.println("  → 菱形问题的根源是「状态被继承多份」，接口没有状态所以安全");

        System.out.println("\n=== 3. Java 8 默认方法：让接口能演进 ===");
        Storage fs = new FileStorage();
        System.out.println("  FileStorage 只实现了 save()：");
        System.out.println("    save(\"a\")            = " + fs.save("a"));
        System.out.println("    saveAll([a, b, c])   = " + fs.saveAll(List.of("a", "b", "c")));
        System.out.println("  → saveAll 用的是接口的默认实现，FileStorage 一行都没写");
        System.out.println();
        System.out.println("  动机很实际：Java 8 想给所有 Collection 加 stream()");
        System.out.println("  若无默认方法，全世界的实现类都会编译失败");
        System.out.println("  → 这是「向后兼容」逼出来的设计");

        System.out.println("\n=== 4. ⚠️ 默认方法带回了菱形冲突 ===");
        System.out.println("  interface A { default String hello() { return \"A\"; } }");
        System.out.println("  interface B { default String hello() { return \"B\"; } }");
        System.out.println();
        System.out.println("  class Conflict implements A, B { }");
        System.out.println("    → 编译错误：从类型 A 和 B 中继承了 hello() 的不相关默认值");
        System.out.println();
        System.out.println("  必须显式指定用哪个：");
        System.out.println("    A.super.hello() + \"+\" + B.super.hello() = " + new Resolved().hello());
        System.out.println();
        System.out.println("  冲突解决的三条规则：");
        System.out.println("    ① 类的实现 > 接口的默认方法        （具体类总是赢）");
        System.out.println("    ② 子接口的默认方法 > 父接口的      （更具体的赢）");
        System.out.println("       UseDerived implements Base, Derived → " + new UseDerived().who());
        System.out.println("    ③ 平级冲突 → 编译错误，必须显式指定");

        System.out.println("\n=== 5. 为什么这不算走了回头路 ===");
        System.out.println("                  行为冲突（默认方法）      状态冲突（多继承字段）");
        System.out.println("  表现            两个同名方法              两份同名字段");
        System.out.println("  能否解决        ✅ 编译器强制你选一个      ❌ 无解，改哪份都不对");
        System.out.println("  语法支持        A.super.hello()          C++ 只能虚继承，代价高");
        System.out.println("  → 接口的底线：行为可以有默认值，状态绝对不行");

        System.out.println("\n=== 6. 依赖倒置：接口最重要的应用 ===");
        List<Storage> impls = List.of(
                new FileStorage(),
                d2 -> "S3: " + d2,                          // lambda 直接当实现
                Storage.inMemory()                           // 静态工厂方法
        );
        String[] names = {"FileStorage", "S3(lambda)", "inMemory"};
        for (int i = 0; i < impls.size(); i++) {
            System.out.printf("    %-14s → %s%n", names[i],
                    new ReportService(impls.get(i)).generate("月度"));
        }
        System.out.println("  → ReportService 的代码一个字都不用改");
        System.out.println("  → 生产注入真实存储，测试注入内存实现 —— 这是单元测试的基础");

        System.out.println("\n=== 7. 函数式接口：lambda 就是接口的实现 ===");
        Transformer upper = s -> s.toUpperCase();
        Transformer repeat = s -> s + s;
        System.out.println("    upper.apply(\"abc\")  = " + upper.apply("abc"));
        System.out.println("    repeat.apply(\"ab\")  = " + repeat.apply("ab"));
        System.out.println("  → @FunctionalInterface：只有一个抽象方法的接口");
        System.out.println("  → Runnable/Comparator/Function 都是，让 Java 有了轻量的「传递行为」能力");

        System.out.println("\n=== 8. 接口 vs 抽象类怎么选 ===");
        System.out.println("  「Dog 是一种 Animal」      → 抽象类（is-a，需要共享状态和实现）");
        System.out.println("  「Dog 能被序列化」          → 接口（can-do，只是一种能力）");
        System.out.println();
        System.out.println("  ⚠️ 不要因为「接口现在能有实现了」就把它当抽象类用");
        System.out.println("     默认方法是为「接口演进」准备的，不是提供实现基类");

        System.out.println("\n=== 9. 小结 ===");
        System.out.println("  · 接口 = 只有契约；能多实现的根本原因是没有实例状态");
        System.out.println("  · 默认方法解决了「接口无法演进」，代价是带回了行为冲突");
        System.out.println("  · 但行为冲突编译器能强制解决，状态冲突无解 —— 这是接口的底线");
        System.out.println("  · 依赖倒置让高层不依赖低层，是可测试性的基础");
    }
}
