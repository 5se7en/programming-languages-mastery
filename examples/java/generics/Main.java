import java.util.ArrayList;
import java.util.List;
import java.lang.reflect.Method;

public class Main {

    // 泛型类：类型参数 T 在使用时才确定
    static class Box<T> {
        static int created = 0;          // ⚠️ 静态字段：所有 Box 共享一份（擦除的后果）
        private final T value;
        Box(T value) { this.value = value; created++; }
        T get() { return value; }
    }

    // 泛型接口 + 具体实现：用于观察编译器生成的桥方法
    interface Container<T> { void set(T value); }
    static class StringContainer implements Container<String> {
        private String value;
        @Override public void set(String value) { this.value = value; }
    }

    // 泛型方法：有界类型参数（T 必须可比较）
    static <T extends Comparable<T>> T max(List<T> list) {
        T best = list.get(0);
        for (T x : list) if (x.compareTo(best) > 0) best = x;
        return best;
    }

    // PECS：只读取元素 → 生产者用 extends
    static double sum(List<? extends Number> nums) {
        double total = 0;
        for (Number n : nums) total += n.doubleValue();
        return total;
    }

    // PECS：只写入元素 → 消费者用 super
    static void fill(List<? super Integer> sink) {
        sink.add(90);
        sink.add(85);
    }

    @SuppressWarnings({"unchecked", "rawtypes"})
    public static void main(String[] args) {
        System.out.println("== ① 擦除：运行时只有一个 Class ==");
        List<String> names = new ArrayList<>();
        List<Integer> scores = new ArrayList<>();
        System.out.println("ArrayList<String> 与 ArrayList<Integer> 是同一个 Class: "
                + (names.getClass() == scores.getClass()));
        System.out.println("getClass() = " + names.getClass().getName());

        System.out.println("\n== ② 静态字段：所有参数化共享一份 ==");
        new Box<String>("小明");
        new Box<Integer>(90);
        System.out.println("Box.created = " + Box.created
                + "   <- 没有 Box<String>.created / Box<Integer>.created 之分");

        System.out.println("\n== ③ 原始类型绕过检查：堆污染 ==");
        List raw = names;                // 原始类型，编译只有 unchecked 警告
        raw.add(42);                     // 塞进去了！
        try {
            String s = names.get(0);     // 取出来这一刻才爆
            System.out.println(s);
        } catch (ClassCastException e) {
            System.out.println("ClassCastException: " + e.getMessage());
        }

        System.out.println("\n== ④ 桥方法：擦除的补丁 ==");
        for (Method m : StringContainer.class.getDeclaredMethods()) {
            System.out.println("  " + m.getName()
                    + "(" + m.getParameterTypes()[0].getSimpleName() + ")"
                    + (m.isBridge() ? "   <- 编译器生成的桥方法" : ""));
        }

        System.out.println("\n== ⑤ 通配符 PECS ==");
        List<Integer> intScores = List.of(90, 85, 98);
        List<Double> dblScores = List.of(88.5, 92.0);
        System.out.println("sum(List<Integer>) = " + sum(intScores));
        System.out.println("sum(List<Double>)  = " + sum(dblScores));
        List<Number> sink = new ArrayList<>();
        fill(sink);
        System.out.println("fill(List<? super Integer>) -> " + sink);

        System.out.println("\n== ⑥ 数组协变 vs 泛型不变 ==");
        Object[] arr = new String[1];    // 数组协变：编译通过
        try {
            arr[0] = 42;                 // 运行时才爆
        } catch (ArrayStoreException e) {
            System.out.println("ArrayStoreException: " + e.getMessage());
        }
        // List<Object> l = new ArrayList<String>();   // ✗ 编译错误：泛型不变，错误提前到编译期

        System.out.println("\n== ⑦ 泛型方法 ==");
        System.out.println("max([90, 85, 98]) = " + max(intScores));
        System.out.println("max([\"小明\", \"小红\"]) = " + max(List.of("小明", "小红")));

        System.out.println("\n== ⑧ 装箱的代价（1000 万元素求和） ==");
        int n = 10_000_000;
        int[] prim = new int[n];
        List<Integer> boxed = new ArrayList<>(n);
        for (int i = 0; i < n; i++) { prim[i] = i; boxed.add(i); }
        for (int r = 0; r < 3; r++) { sumPrim(prim); sumBoxed(boxed); }   // 预热
        long t1 = System.nanoTime();
        long s1 = sumPrim(prim);
        long t2 = System.nanoTime();
        long s2 = sumBoxed(boxed);
        long t3 = System.nanoTime();
        System.out.printf("int[]              求和: %6.1f ms（结果 %d）%n", (t2 - t1) / 1e6, s1);
        System.out.printf("ArrayList<Integer> 求和: %6.1f ms（结果 %d）%n", (t3 - t2) / 1e6, s2);
    }

    static long sumPrim(int[] a) {
        long s = 0;
        for (int x : a) s += x;
        return s;
    }

    static long sumBoxed(List<Integer> a) {
        long s = 0;
        for (int x : a) s += x;      // 每次循环都在拆箱
        return s;
    }
}
