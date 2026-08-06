// 第 10 章 · 运算符 — Java 示例
// 运行：javac Operators.java && java Operators
import java.util.Arrays;
import java.util.Objects;

public class Operators {
    static boolean boom() { System.out.println("   ← 这行不该出现！"); return true; }

    public static void main(String[] args) {
        // 1. 字符串：== 比较引用，equals 比较内容
        String s1 = "hi", s2 = "hi", s3 = new String("hi");
        System.out.println("字面量 s1 == s2   → " + (s1 == s2) + "  (常量池)");
        System.out.println("new    s1 == s3   → " + (s1 == s3) + " ← 内容相同但不是同一对象");
        System.out.println("s1.equals(s3)     → " + s1.equals(s3) + "  ✓");

        // 2. 包装类缓存陷阱：同样的写法，结果相反
        Integer x = 127, y = 127, m = 128, n = 128;
        System.out.println("Integer 127 == 127 → " + (x == y) + "  (缓存 -128~127)");
        System.out.println("Integer 128 == 128 → " + (m == n) + " ← 超出缓存！");
        System.out.println("128.equals(128)    → " + m.equals(n) + "  ✓");

        // 3. 数组比较要用 Arrays.equals
        int[] a = {1, 2}, b = {1, 2};
        System.out.println("数组 a == b        → " + (a == b)
            + " | Arrays.equals → " + Arrays.equals(a, b));

        // 4. 短路求值
        System.out.println("false && boom()   → " + (false && boom()));

        // 5. 无符号右移是 Java 独有
        System.out.println("-8 >> 1 = " + (-8 >> 1) + " | -8 >>> 1 = " + (-8 >>> 1));

        // 6. 空安全比较
        System.out.println("Objects.equals(null, null) → " + Objects.equals(null, null));
    }
}
