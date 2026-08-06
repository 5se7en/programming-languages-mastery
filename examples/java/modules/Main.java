// 第 14 章 · 模块 — Java 示例
// 运行：javac *.java && java Main
import java.util.Arrays;                       // 导入单个类
import static java.lang.Math.max;              // 静态导入：之后可直接写 max()

public class Main {
    public static void main(String[] args) {
        int[] scores = {92, 75, 50};

        // 1. 使用同包的另一个类（同包无需 import）
        System.out.printf("跨文件调用: %.2f | 常量: %d%n",
                MathUtil.average(scores), MathUtil.MAX_SCORE);

        // 2. package-private：同包可见
        System.out.println("同包可见: " + MathUtil.packagePrivate());
        // MathUtil.hidden();   // ✗ 编译错误：private 仅本类可见

        // 3. 静态导入后可直接使用方法名
        System.out.println("静态导入 max(3,7) = " + max(3, 7));

        // 4. 常规导入
        System.out.println("java.util.Arrays: " + Arrays.toString(scores));

        System.out.println("Java 的包名必须与目录结构严格对应");
    }
}
