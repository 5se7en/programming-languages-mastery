// 第 16 章 · 数组 — Java 示例
// 运行：javac *.java && java Main
import java.util.Arrays;

public class Main {
    public static void main(String[] args) {
        // 1. Java 数组：连续内存、定长、类型统一
        int[] scores = {92, 75, 88};
        System.out.println("长度(字段): " + scores.length);
        System.out.println("打印数组必须用 Arrays.toString: " + Arrays.toString(scores));
        System.out.println("直接打印会得到哈希: " + scores);

        // 2. == 比较引用，Arrays.equals 比较内容
        int[] a = {1, 2}, b = {1, 2};
        System.out.println("a == b → " + (a == b) + " | Arrays.equals → " + Arrays.equals(a, b));

        // 3. 越界抛异常（JVM 每次访问都做边界检查）
        try {
            int v = scores[10];
        } catch (ArrayIndexOutOfBoundsException e) {
            System.out.println("scores[10] → " + e.getClass().getSimpleName() + " ← 运行时检查");
        }

        // 4. 二维数组是「数组的数组」，各行可以不等长（交错数组）
        int[][] jagged = new int[3][];
        jagged[0] = new int[2];
        jagged[1] = new int[5];
        jagged[2] = new int[1];
        System.out.println("交错数组各行长度: " + jagged[0].length + ", "
            + jagged[1].length + ", " + jagged[2].length + " ← Java 二维行不一定相邻");

        // 5. 缓存局部性：行优先 vs 列优先
        int N = 2000;
        int[][] m = new int[N][N];
        for (int[] row : m) Arrays.fill(row, 1);

        // 预热：排除 JIT 编译开销（否则第一个循环会背上编译成本，结果失真）
        for (int w = 0; w < 3; w++) {
            long x = 0;
            for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) x += m[i][j];
            for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) x += m[i][j];
            if (x == -1) System.out.print("");
        }

        long t0 = System.nanoTime();
        long s1 = 0;
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) s1 += m[i][j];
        long t1 = System.nanoTime();
        long s2 = 0;
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) s2 += m[i][j];
        long t2 = System.nanoTime();

        double row = (t1 - t0) / 1e6, col = (t2 - t1) / 1e6;
        System.out.printf("%n缓存局部性(已预热): 行优先 %.1fms vs 列优先 %.1fms → 慢 %.1f 倍（校验和一致: %b）%n",
            row, col, col / row, s1 == s2);
        System.out.println("注：Java 二维是「数组的数组」，行间本就不连续；JIT 也可能重排循环，"
            + "所以差异通常小于 C++。结论看原理，数字请以自己机器实测为准。");
    }
}
