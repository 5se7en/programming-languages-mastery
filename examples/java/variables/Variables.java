// 第 08 章 · 变量 — Java 示例
// 运行：javac Variables.java && java Variables
public class Variables {
    public static void main(String[] args) {
        // 1. 声明
        String studentName = "Alice";
        final int MAX_SCORE = 100;
        int age = 20;
        var score = 92;                 // 类型推导，仍是静态类型
        System.out.println(studentName + " " + age + " " + score + " " + MAX_SCORE);

        // 2. 基本类型：值复制
        int a = 92;
        int b = a;
        b = 60;
        System.out.println("基本类型 值复制: " + a + " " + b);      // 92 60

        // 3. 引用类型：引用复制
        int[] s1 = {92};
        int[] s2 = s1;
        s2[0] = 60;
        System.out.println("引用类型 引用复制: " + s1[0]);          // 60

        // 4. == 比较引用，equals 比较内容
        String x = new String("hi");
        String y = new String("hi");
        System.out.println("== : " + (x == y) + ", equals: " + x.equals(y));
    }
}
