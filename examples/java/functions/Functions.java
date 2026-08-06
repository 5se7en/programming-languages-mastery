// 第 12 章 · 函数 — Java 示例
// 运行：javac Functions.java && java Functions
import java.util.List;
import java.util.function.Supplier;

public class Functions {
    // 1. 重载：Java 没有默认参数，用重载模拟
    static double average(int... scores) {
        if (scores.length == 0) return 0;
        int sum = 0;
        for (int s : scores) sum += s;
        return (double) sum / scores.length;
    }
    static int  max(int a, int b)       { return a > b ? a : b; }
    static double max(double a, double b) { return a > b ? a : b; }

    // 2. 证明 Java 永远是值传递
    static void modify(StringBuilder sb)   { sb.append(" 被改了"); }
    static void reassign(StringBuilder sb) { sb = new StringBuilder("新对象"); }
    static void addOne(int x)              { x = x + 1; }

    // 3. 闭包：捕获的变量必须 effectively final
    static Supplier<Integer> makeCounter() {
        int[] count = {0};                 // 用数组绕过 effectively final 限制
        return () -> ++count[0];
    }

    public static void main(String[] args) {
        System.out.println("平均分: " + average(92, 75, 50));
        System.out.println("重载: max(1,2)=" + max(1, 2) + " max(1.5,2.5)=" + max(1.5, 2.5));

        StringBuilder s = new StringBuilder("原始");
        modify(s);   System.out.println("改内容后:   " + s + "   ← 外部可见");
        reassign(s); System.out.println("重新赋值后: " + s + "   ← 外部没变！");
        int n = 5; addOne(n);
        System.out.println("基本类型:   " + n + "            ← 没变");
        System.out.println("结论：Java 永远是值传递，传的是引用的副本");

        Supplier<Integer> c = makeCounter();
        c.get(); c.get();
        System.out.println("闭包计数器: " + c.get());

        // Lambda 与方法引用
        List.of(92, 75, 50).stream().filter(x -> x >= 60).forEach(x -> System.out.print(x + " "));
        System.out.println("← Lambda 过滤及格分");
    }
}
