// 第 23 章 · 类 —— Java 示例
// 运行：javac Main.java && java Main

import java.util.Arrays;

public class Main {

    // ---------- 一个完整的类 ----------
    static class Student {
        private String name;                        // 实例字段：每个对象一份
        private int score;
        private final int id;

        private static int count = 0;               // 静态字段：整个类共享一份
        public static final int PASS_LINE = 60;     // 静态常量
        public static String school = "第一中学";

        // 构造函数：与类同名，无返回类型
        public Student(String name, int score) {
            if (score < 0 || score > 100)           // 构造函数保证对象从诞生起就合法
                throw new IllegalArgumentException("分数必须在 0..100 之间");
            this.name = name;                        // this 区分参数与字段
            this.score = score;
            this.id = ++count;
        }

        public boolean isPassing() { return score >= PASS_LINE; }

        public String getName() { return name; }
        public int getScore() { return score; }
        public int getId() { return id; }

        public static int getCount() { return count; }   // 静态方法

        @Override
        public String toString() { return "Student(" + name + ", " + score + ")"; }
    }

    // ---------- Java 14+ 的 record：纯数据类 ----------
    record Point(int x, int y) { }

    public static void main(String[] args) {
        System.out.println("=== 1. 不用类的痛点：数据分散、关联脆弱 ===");
        String[] names = {"Alice", "Bob"};
        int[] scores = {92, 75};
        int[] ages = {16, 17};
        System.out.println("  平行数组: " + Arrays.toString(names) + " "
                + Arrays.toString(scores) + " " + Arrays.toString(ages));
        System.out.println("  ⚠️ 三个数组必须严格保持顺序一致，排序其中一个就全乱了");
        System.out.println("  isPassing(ages[0]) = " + (ages[0] >= 60)
                + "  ← 传错参数，语法却完全合法");

        System.out.println("\n=== 2. 用类打包：数据和行为待在一起 ===");
        Student alice = new Student("Alice", 92);
        Student bob = new Student("Bob", 45);
        System.out.printf("  %s: 分数 %d, 及格? %b%n",
                alice.getName(), alice.getScore(), alice.isPassing());
        System.out.printf("  %s: 分数 %d, 及格? %b%n",
                bob.getName(), bob.getScore(), bob.isPassing());
        System.out.println("  静态字段 Student.school = " + Student.school + "  ← 所有实例共享");
        System.out.println("  已创建实例数 = " + Student.getCount());

        System.out.println("\n=== 3. 构造函数保证对象合法 ===");
        try {
            new Student("Invalid", 150);
        } catch (IllegalArgumentException e) {
            System.out.println("  new Student(\"Invalid\", 150) → " + e.getMessage());
            System.out.println("  → 非法对象根本无法被创建出来");
        }

        System.out.println("\n=== 4. 引用语义：b = a 只是起了个别名 ===");
        Student a = new Student("Alice", 90);
        Student b = a;                      // 引用赋值，不是拷贝
        b.name = "Bob";
        System.out.println("  赋值后: a.name=" + a.name + "  b.name=" + b.name + "  ← a 也变了！");
        System.out.println("  a == b ? " + (a == b) + "  ← 根本就是同一个对象");
        System.out.println("  → Java 对象永远在堆上，变量持有的是引用");
        System.out.println("  → 与 C++ 的值语义形成鲜明对比（见 cpp/class 示例）");

        System.out.println("\n=== 5. 实例成员 vs 静态成员：存几份 ===");
        System.out.println("  alice.id = " + alice.getId() + "   bob.id = " + bob.getId()
                + "   ← 实例字段，每个对象一份");
        Student.school = "第二中学";
        System.out.println("  改静态字段后，所有实例看到的都是: " + Student.school);
        System.out.println("  → 静态成员整个类只有一份，存在类的存储区");

        System.out.println("\n=== 6. record：纯数据类（Java 14+）===");
        Point p1 = new Point(1, 2);
        Point p2 = new Point(1, 2);
        System.out.println("  new Point(1, 2)  → " + p1 + "  ← 自动生成的 toString");
        System.out.println("  p1.equals(p2)    → " + p1.equals(p2) + "  ← 自动实现基于值的比较");
        System.out.println("  p1.hashCode() == p2.hashCode() → "
                + (p1.hashCode() == p2.hashCode()));
        System.out.println("  → record 自动生成 构造函数/访问器/equals/hashCode/toString");
        System.out.println("  → 回顾第 20 章：手写 equals 忘了 hashCode 会导致 HashMap 存进去查不到");
        System.out.println("     record 把这对方法一起生成，从设计上避免了那个坑");

        System.out.println("\n=== 7. 方法只存一份，被所有实例共享 ===");
        System.out.println("  alice 和 bob 各有自己的 name/score（实例字段）");
        System.out.println("  但 isPassing() 的代码只有一份，存在方法区");
        System.out.println("  → 创建一百万个对象，不会产生一百万份方法代码");
    }
}
