// 第 09 章 · 数据类型 — Java 示例
// 运行：javac DataTypes.java && java DataTypes
import java.math.BigDecimal;

public class DataTypes {
    public static void main(String[] args) {
        // 1. 基本类型宽度由规范固定
        System.out.println("int 范围: " + Integer.MIN_VALUE + " ~ " + Integer.MAX_VALUE);

        // 2. 溢出静默回绕
        int max = Integer.MAX_VALUE;
        System.out.println("最大值 + 1 = " + (max + 1) + "  ← 变负数了，且不报错");
        try {
            Math.addExact(max, 1);
        } catch (ArithmeticException e) {
            System.out.println("用 Math.addExact 才会抛异常: " + e.getMessage());
        }

        // 3. 浮点误差
        System.out.println("0.1 + 0.2 = " + (0.1 + 0.2) + " | 等于 0.3 吗: " + (0.1 + 0.2 == 0.3));

        // 4. 金额用 BigDecimal（从字符串构造）
        BigDecimal a = new BigDecimal("0.1");
        BigDecimal b = new BigDecimal("0.2");
        System.out.println("BigDecimal 精确: " + a.add(b)
            + " | 等于 0.3 吗: " + (a.add(b).compareTo(new BigDecimal("0.3")) == 0));

        // 5. 字符串长度数的是 UTF-16 码元
        String wave = "👋";
        System.out.println("length() = " + wave.length()
            + " | codePointCount = " + wave.codePointCount(0, wave.length()));
    }
}
