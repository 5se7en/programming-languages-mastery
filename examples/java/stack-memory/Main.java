import java.util.List;
import java.util.stream.Collectors;

public class Main {

    static int add(int a, int b) {         // javap -c 可看到它的操作数栈（见章节实测）
        int sum = a + b;
        return sum;
    }

    static void level3() {
        // StackWalker（Java 9+）：把调用栈变成可编程的数据
        List<String> frames = StackWalker.getInstance()
                .walk(s -> s.map(f -> f.getMethodName() + " (行 " + f.getLineNumber() + ")")
                            .collect(Collectors.toList()));
        System.out.println("StackWalker 看到的调用链（栈顶在前）:");
        frames.forEach(f -> System.out.println("  " + f));
    }

    static void level2() { level3(); }
    static void level1() { level2(); }

    public static void main(String[] args) {
        System.out.println("== ① JVM 的调用栈：StackWalker 实测 ==");
        level1();

        System.out.println("\n== ② 每个方法的栈帧 = 局部变量表 + 操作数栈 ==");
        System.out.println("add(1, 2) = " + add(1, 2));
        System.out.println("javap -c Main 可见 add 的字节码（章节实测）：");
        System.out.println("  stack=2, locals=3  <- 编译期就算好了帧的尺寸");
        System.out.println("  iload_0 / iload_1  <- 从局部变量表压进操作数栈");
        System.out.println("  iadd               <- 弹出两个，压回一个");

        System.out.println("\n== ③ 异常栈回溯：栈帧链的另一种呈现 ==");
        try {
            level1IntoTrouble();
        } catch (IllegalStateException e) {
            StackTraceElement[] st = e.getStackTrace();
            System.out.println("异常携带了 " + st.length + " 帧，最上面三帧:");
            for (int i = 0; i < 3 && i < st.length; i++) {
                System.out.println("  at " + st[i].getMethodName() + ":" + st[i].getLineNumber());
            }
        }
    }

    static void level1IntoTrouble() { level2IntoTrouble(); }
    static void level2IntoTrouble() { throw new IllegalStateException("演示"); }
}
