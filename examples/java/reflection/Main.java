import java.lang.reflect.Field;
import java.lang.reflect.InaccessibleObjectException;
import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.List;

public class Main {

    static class Student {
        private String name;
        private int score;

        public Student() { this("未命名", 0); }
        public Student(String name, int score) { this.name = name; this.score = score; }

        public String getName() { return name; }
        public int getScore() { return score; }

        private String secret() { return name + " 的真实分数是 " + score; }
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① Class 对象：类型信息的运行时入口 ==");
        Class<?> c1 = Student.class;                    // 编译期字面量
        Class<?> c2 = new Student().getClass();         // 从对象上问
        Class<?> c3 = Class.forName("Main$Student");    // 从字符串加载！
        System.out.println("三种方式拿到同一个 Class 对象: " + (c1 == c2 && c2 == c3));

        System.out.println("\n== ② 枚举成员：类的结构一览无余 ==");
        for (Field f : c1.getDeclaredFields()) {
            System.out.println("  字段: " + f.getType().getSimpleName() + " " + f.getName());
        }
        for (Method m : c1.getDeclaredMethods()) {
            System.out.println("  方法: " + m.getName());
        }

        System.out.println("\n== ③ 动态创建 + 动态调用：不写 new，不写方法名 ==");
        Object obj = c1.getDeclaredConstructor(String.class, int.class)
                       .newInstance("小明", 90);
        Method getName = c1.getMethod("getName");
        System.out.println("invoke(getName) = " + getName.invoke(obj));

        System.out.println("\n== ④ 击穿封装：private 形同虚设 ==");
        Field name = c1.getDeclaredField("name");
        name.setAccessible(true);                       // 一行破除第 25 章的保护
        name.set(obj, "被改名");
        Method secret = c1.getDeclaredMethod("secret");
        secret.setAccessible(true);
        System.out.println("私有字段已改，私有方法照调: " + secret.invoke(obj));

        System.out.println("\n== ⑤ 但 JDK 内部被模块系统保护（Java 9+） ==");
        try {
            Field value = String.class.getDeclaredField("value");
            value.setAccessible(true);
        } catch (InaccessibleObjectException e) {
            String msg = e.getMessage();
            System.out.println("InaccessibleObjectException: "
                    + msg.substring(0, Math.min(72, msg.length())) + "...");
        }

        System.out.println("\n== ⑥ 反射绕过泛型检查（呼应第 29 章的擦除） ==");
        List<String> names = new ArrayList<>();
        Method add = List.class.getMethod("add", Object.class);
        add.invoke(names, 42);                          // 编译器管不到反射
        System.out.println("List<String> 里被塞进了: " + names);

        System.out.println("\n== ⑦ 性能：直接调用 vs 反射调用（1000 万次） ==");
        Student s = new Student("小红", 85);
        Method m = c1.getMethod("getScore");
        long sink = 0;
        for (int i = 0; i < 3_000_000; i++) {           // 预热
            sink += s.getScore();
            sink += (int) m.invoke(s);
        }
        int n = 10_000_000;
        long t1 = System.nanoTime();
        for (int i = 0; i < n; i++) sink += s.getScore();
        long t2 = System.nanoTime();
        for (int i = 0; i < n; i++) sink += (int) m.invoke(s);
        long t3 = System.nanoTime();
        System.out.printf("直接调用:       %6.1f ms%n", (t2 - t1) / 1e6);
        System.out.printf("Method.invoke:  %6.1f ms%n", (t3 - t2) / 1e6);
        if (sink == 42) System.out.println();           // 防止死代码消除
    }
}
