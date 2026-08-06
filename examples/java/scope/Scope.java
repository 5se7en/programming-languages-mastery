// 第 13 章 · 作用域 — Java 示例
// 运行：javac Scope.java && java Scope
public class Scope {
    static int classField = 1;                 // 类作用域

    static String shadowDemo(int score) {      // 参数遮蔽字段时必须用 this（此处为静态方法示意）
        return "参数 score=" + score + "，类字段 classField=" + classField;
    }

    public static void main(String[] args) {
        int local = 2;                          // 方法作用域
        if (true) {
            int inside = 3;                     // 块作用域
            System.out.println("块内可见: inside=" + inside + " local=" + local);
        }
        // System.out.println(inside);          // ✗ 编译错误：块外不可见
        System.out.println("块外不可见 inside（取消注释会编译失败）");

        for (int i = 0; i < 3; i++) { }
        // System.out.println(i);               // ✗ 编译错误：i 只在 for 内
        System.out.println("循环变量 i 不会泄漏（与 Python 相反）");

        System.out.println(shadowDemo(99));

        // 闭包：捕获的局部变量必须是 effectively final
        int captured = 10;
        Runnable r = () -> System.out.println("Lambda 捕获 effectively final 变量: " + captured);
        r.run();
        // captured++;   // ✗ 加上这行，上面的 lambda 会编译报错
    }
}
