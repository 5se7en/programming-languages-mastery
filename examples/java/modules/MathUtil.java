// 同一个包内的另一个类
public class MathUtil {
    public static final int MAX_SCORE = 100;        // public：包外可见

    public static double average(int[] scores) {
        if (scores.length == 0) return 0;
        int sum = 0;
        for (int s : scores) sum += s;
        return (double) sum / scores.length;
    }

    static String packagePrivate() {                // 无修饰符：仅包内可见
        return "package-private：只有同包能调用";
    }

    private static String hidden() { return "private：只有本类能调用"; }
}
