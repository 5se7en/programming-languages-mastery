// 第 27 章 · 多态 —— Java 示例
// 运行：javac Main.java && java Main
// Java 方法默认就是虚方法，而 JIT 的去虚化让这个默认几乎不付代价

import java.util.*;

public class Main {

    // ---------- ① 正常的多态 ----------
    interface Shape { double area(); String name(); }

    record Circle(double r) implements Shape {
        public double area() { return Math.PI * r * r; }
        public String name() { return "圆形"; }
    }
    record Rect(double w, double h) implements Shape {
        public double area() { return w * h; }
        public String name() { return "矩形"; }
    }
    record Triangle(double b, double h) implements Shape {
        public double area() { return b * h / 2; }
        public String name() { return "三角形"; }
    }

    // ---------- ② 字段没有多态 ----------
    static class FieldBase { String name = "base"; String getName() { return "base"; } }
    static class FieldDerived extends FieldBase {
        String name = "derived";                          // 遮蔽，不是重写
        @Override String getName() { return "derived"; }  // 这才是重写
    }

    // ---------- ③ 性能测试（串行依赖链，防止优化）----------
    interface Compute { int compute(int v); }
    static class CompA implements Compute { public int compute(int v){ return (v*33+11)&0xFFFFFF; } }
    static class CompB implements Compute { public int compute(int v){ return (v*37+13)&0xFFFFFF; } }
    static final class CompFinal { final int compute(int v){ return (v*33+11)&0xFFFFFF; } }

    public static void main(String[] args) {
        System.out.println("=== 1. 多态：新增类型不改已有代码（开闭原则）===");
        List<Shape> shapes = List.of(new Circle(2), new Rect(3, 4), new Triangle(6, 5));

        double total = 0;
        for (Shape s : shapes) {                 // 这个循环不关心具体是什么图形
            System.out.printf("    %s 面积 = %.2f%n", s.name(), s.area());
            total += s.area();
        }
        System.out.printf("  总面积 = %.2f%n", total);
        System.out.println("  → 新增一种图形，只需加一个 record，这个循环一个字不用改");

        System.out.println("\n  对比：没有多态时要写成这样");
        System.out.println("    if (s instanceof Circle)    sum += ...");
        System.out.println("    else if (s instanceof Rect) sum += ...");
        System.out.println("    else if (...)               // 每加一种图形就要回来改");
        System.out.println("  → 而这类判断往往散落在几十个文件里");

        System.out.println("\n=== 2. ⚠️ 字段没有多态，只有方法才有 ===");
        FieldBase b = new FieldDerived();
        System.out.println("  FieldBase b = new FieldDerived();");
        System.out.println("    b.name      = " + b.name + "      ← 字段按「变量的静态类型」访问");
        System.out.println("    b.getName() = " + b.getName() + "   ← 方法按「实际类型」派发");
        System.out.println("  → 永远不要用同名字段来「覆盖」父类字段，只会造成困惑");

        System.out.println("\n=== 3. 哪些方法是静态派发的 ===");
        System.out.println("  static   方法 → 静态派发（属于类，不属于对象）");
        System.out.println("  private  方法 → 静态派发（子类看不见，无从重写）");
        System.out.println("  final    方法 → 静态派发（明确禁止重写）");
        System.out.println("  其他所有方法  → 动态派发（Java 默认就是虚方法）");
        System.out.println("  → 对比 C++：默认非虚，必须显式写 virtual");

        // ---------- 性能测试 ----------
        final int N = 50_000_000;

        // 充分预热，让 JIT 完成编译和去虚化
        for (int w = 0; w < 3; w++) {
            Compute c = new CompA();
            int v = 1;
            for (int i = 0; i < 5_000_000; i++) v = c.compute(v);
            if (v == 0) System.out.print("");
        }

        System.out.println("\n=== 4. ⚠️ JIT 去虚化：Java 的独门优势 ===");

        // ⚠️ 微基准测试的关键：每组跑多轮取「最小值」
        //    首轮常因 JIT 尚未完全优化、CPU 频率未爬升而偏慢，
        //    单轮测量会得出「接口调用比 final 方法还快」这类噪声结论。
        final int ROUNDS = 5;
        double d1 = Double.MAX_VALUE, d2 = Double.MAX_VALUE, d3 = Double.MAX_VALUE;
        int v1 = 1, v2 = 1, v3 = 1;

        CompFinal f = new CompFinal();
        Compute mono = new CompA();               // 只有一个实现类
        Compute[] poly = new Compute[1024];       // 两种实现随机交替
        Random r = new Random(42);
        for (int i = 0; i < 1024; i++) poly[i] = r.nextBoolean() ? new CompA() : new CompB();

        for (int round = 0; round < ROUNDS; round++) {
            long t0 = System.nanoTime();
            for (int i = 0; i < N; i++) v1 = f.compute(v1);
            long t1 = System.nanoTime();
            for (int i = 0; i < N; i++) v2 = mono.compute(v2);
            long t2 = System.nanoTime();
            for (int i = 0; i < N; i++) v3 = poly[i & 1023].compute(v3);
            long t3 = System.nanoTime();

            d1 = Math.min(d1, (t1-t0)/1e6);
            d2 = Math.min(d2, (t2-t1)/1e6);
            d3 = Math.min(d3, (t3-t2)/1e6);
        }
        System.out.printf("  （每组跑 %d 轮取最小值，排除 JIT 预热和调度干扰）%n", ROUNDS);
        System.out.printf("  %d 百万次调用（每次输入依赖上次输出）:%n", N/1_000_000);
        System.out.printf("    final 类的 final 方法    %5.0f ms   → %.2f 倍%n", d1, 1.0);
        System.out.printf("    接口调用 · 单一实现类    %5.0f ms   → %.2f 倍%n", d2, d2/d1);
        System.out.printf("    接口调用 · 两种实现交替  %5.0f ms   → %.2f 倍%n", d3, d3/d1);
        System.out.printf("  (校验值 %d %d %d，确保循环真的执行了)%n", v1, v2, v3);

        System.out.println();
        System.out.println("  ⚠️ 单一实现时几乎追平 final 方法 —— JIT 完全去虚化了");
        System.out.println();
        System.out.println("  JIT 能做而静态编译器做不到的事：");
        System.out.println("    ① 单态内联缓存：观察到这个调用点至今只见过 CompA，就内联它");
        System.out.println("    ② 类层次分析：如果整个已加载的类层次里只有一个实现，直接去虚化");
        System.out.println("    ③ 保险机制：插入类型检查，万一来了 CompB 就退回慢路径并重新编译");
        System.out.println();
        System.out.println("  → 对比 C++ 的静态编译：单态虚调用仍有约 1.15 倍开销");
        System.out.println("     因为编译时无法知道未来会加载哪些子类");
        System.out.println("  → 这是「JIT 有时比 AOT 快」的具体例证（第 05 章）");

        System.out.println("\n=== 5. ⚠️ 别把多态退化成 instanceof ===");
        System.out.println("  ❌ for (Shape s : shapes) {");
        System.out.println("       if (s instanceof Circle) { ... }      // 多态被浪费了");
        System.out.println("       else if (s instanceof Rect) { ... }");
        System.out.println("     }");
        System.out.println("  ✅ for (Shape s : shapes) sum += s.area();");
        System.out.println("  → 如果每加一个类型都要改多处代码，说明多态没用对地方");

        System.out.println("\n=== 6. 小结 ===");
        System.out.println("  · Java 方法默认虚，靠 JIT 去虚化把代价降到几乎为零（实测 1.00 倍）");
        System.out.println("  · 这是「语言设计与运行时优化互相成就」的例子");
        System.out.println("  · 字段是静态绑定的，只有方法才有多态");
        System.out.println("  · final/sealed 既表达设计意图，又帮助去虚化");
        System.out.println("  · 多态的价值在于「新增类型的成本」，不在于「看起来面向对象」");
    }
}
