import java.util.Objects;

public class Main {

    static class Student {
        String name;
        Student(String name) { this.name = name; }
    }

    public static void main(String[] args) {
        System.out.println("== ① Java 的立场：引用是「不给看地址」的指针 ==");
        Student a = new Student("小明");
        Student b = a;                      // 复制的是引用，不是对象
        b.name = "小红";
        System.out.println("b = a 后改 b.name，a.name = " + a.name + "   <- 同一个对象");
        Student c = new Student("小红");
        System.out.println("a == b: " + (a == b) + "（引用相等）；a == c: " + (a == c)
                + "（内容相同也不等——== 比的是「地址」）");

        System.out.println("\n== ② identityHashCode：地址的\"影子\"，但不是地址 ==");
        System.out.println("System.identityHashCode(a) = 0x"
                + Integer.toHexString(System.identityHashCode(a)));
        System.out.println("（它由对象身份生成且终身不变——而对象本身可能已被 GC 搬过家：");
        System.out.println("  真地址若暴露，第 33 章的压缩整理就做不成了）");

        System.out.println("\n== ③ NullPointerException：被驯化的空指针 ==");
        Student nobody = null;
        try {
            nobody.name.length();
        } catch (NullPointerException e) {
            System.out.println("解引用 null -> 可捕获的 NPE: " + e.getMessage());
        }
        System.out.println("（C++ 的空指针是段错误；Java 把它驯化成异常——还附赠精确提示）");

        System.out.println("\n== ④ 防御工事：Objects 工具与 Optional ==");
        try {
            Objects.requireNonNull(nobody, "student 不能为空");
        } catch (NullPointerException e) {
            System.out.println("requireNonNull 前置拦截: " + e.getMessage());
        }
        String name = java.util.Optional.ofNullable(nobody)
                .map(s -> s.name).orElse("(无名氏)");
        System.out.println("Optional 链式取值: " + name + "   <- 把「可能为空」写进类型（第 40 章）");
    }
}
