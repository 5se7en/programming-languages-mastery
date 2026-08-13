import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import javax.tools.JavaCompiler;
import javax.tools.ToolProvider;

/**
 * 构建工具：增量编译的正确性陷阱——常量内联与过期类文件，动态编译真实复现。
 */
public class Main {

    static Path work;

    public static void main(String[] args) throws Exception {
        work = Files.createTempDirectory("pl-mastery-inc");
        Path out = work.resolve("classes");
        Files.createDirectories(out);

        System.out.println("== ① 增量编译的天真做法：只重编改过的文件 ==");
        System.out.println("  「哪个 .java 变了就重编哪个」——听起来无懈可击，本例连演两次它的翻车");

        // ---------- 实验一: 常量内联 ----------
        System.out.println("\n== ② 翻车一：常量内联（Java 增量编译最著名的坑，实测）==");
        compile(out, "Config", """
                public class Config {
                    public static final int MAX_RETRIES = 3;
                }""");
        compile(out, "App", """
                public class App {
                    public static String run() { return "重试上限 = " + Config.MAX_RETRIES; }
                }""", out);
        System.out.println("  初始编译两个文件: Config.MAX_RETRIES = 3");
        System.out.println("  运行 App.run() → " + invoke(out, "App", "run"));

        // 改常量，【只重编 Config】——模拟天真的增量策略
        compile(out, "Config", """
                public class Config {
                    public static final int MAX_RETRIES = 10;
                }""");
        System.out.println("\n  把 MAX_RETRIES 改成 10，按增量策略【只重编 Config.java】:");
        System.out.println("  运行 App.run() → " + invoke(out, "App", "run") + "   ← 还是 3！");
        System.out.println("  → 原因: static final 常量在编译期被【内联进 App.class 的字节码】");
        System.out.println("    App.class 里存的是字面量 3，根本没有对 Config 的运行时引用");
        System.out.println("  → 「哪个文件变了重编哪个」在这里【静默产出错误的程序】——比编译失败糟得多");

        compile(out, "App", """
                public class App {
                    public static String run() { return "重试上限 = " + Config.MAX_RETRIES; }
                }""", out);
        System.out.println("  重编 App.java 之后 → " + invoke(out, "App", "run") + " ✓");
        System.out.println("  → 正确的增量系统必须知道: App 依赖的不是 Config.class，而是【它的常量值】");

        // ---------- 实验二: 过期类文件 ----------
        System.out.println("\n== ③ 翻车二：方法签名变了，下游没重编（实测）==");
        compile(out, "Util", """
                public class Util {
                    public static String greet(String name) { return "你好, " + name; }
                }""");
        compile(out, "Caller", """
                public class Caller {
                    public static String run() { return Util.greet("世界"); }
                }""", out);
        System.out.println("  初始: Caller 调 Util.greet(String) → " + invoke(out, "Caller", "run"));

        // Util 的方法签名变了（加了参数），只重编 Util
        compile(out, "Util", """
                public class Util {
                    public static String greet(String name, boolean polite) {
                        return (polite ? "您好, " : "你好, ") + name;
                    }
                }""");
        System.out.println("  greet 加了个参数，只重编 Util.java:");
        System.out.println("  运行 Caller.run() → " + invoke(out, "Caller", "run"));
        System.out.println("  → NoSuchMethodError——第 53 章的运行时炸弹，这次的成因是【过期的 Caller.class】");
        System.out.println("  → 两次翻车的共同根因: 增量的正确性取决于【依赖图的完整性】——");
        System.out.println("    文件级 mtime 看不见「常量被内联」「签名被引用」这些字节码级依赖");

        System.out.println("\n== ④ 真实工具怎么解 ==");
        System.out.println("  javac 本身: 不做增量——每次给它的文件全都重编（把难题交给上层）");
        System.out.println("  Gradle 增量编译: 分析【类级依赖 + 常量依赖】，常量变了就重编所有引用者");
        System.out.println("  Bazel: 内容哈希 + 沙箱（Python 版实测哈希消灭 mtime 两种失效）");
        System.out.println("        + 编译隔离: Java 还能用 header jar（只含签名）做「接口没变就不传播」");
        System.out.println("  → 「接口 jar」的思想: 下游依赖的是你的【签名】，不是实现——");
        System.out.println("    实现变了不必重编下游，签名变了才必须（爆炸半径的精确化）");

        System.out.println("\n== ⑤ 增量正确性的判定标准 ==");
        System.out.println("  增量构建的黄金法则: 【增量的结果必须与全量重建逐字节一致】");
        System.out.println("  ② 和 ③ 都违反了它——增量出来的程序和全量的行为不同");
        System.out.println("  → 排查「诡异行为」的第一招永远是 clean build——");
        System.out.println("    如果 clean 后问题消失，说明你的增量系统在某处漏了一条依赖边");

        deleteRecursively(work);
    }

    static void compile(Path out, String className, String source, Path... cp) throws Exception {
        Path src = work.resolve(className + ".java");
        Files.writeString(src, source);
        JavaCompiler javac = ToolProvider.getSystemJavaCompiler();
        var opts = new java.util.ArrayList<>(List.of("-d", out.toString()));
        if (cp.length > 0) opts.addAll(List.of("-cp", cp[0].toString()));
        opts.add(src.toString());
        if (javac.run(null, null, null, opts.toArray(String[]::new)) != 0)
            throw new IllegalStateException("编译失败: " + className);
    }

    /** 每次用新的 ClassLoader 加载（避免 JVM 类缓存干扰实验） */
    static String invoke(Path classpath, String cls, String method) {
        try (URLClassLoader cl = new URLClassLoader(new URL[]{classpath.toUri().toURL()},
                ClassLoader.getPlatformClassLoader())) {
            Method m = cl.loadClass(cls).getMethod(method);
            return String.valueOf(m.invoke(null));
        } catch (Exception e) {
            Throwable cause = e.getCause() != null ? e.getCause() : e;
            return "✗ " + cause.getClass().getSimpleName() + ": " + cause.getMessage();
        }
    }

    static void deleteRecursively(Path dir) throws Exception {
        try (var walk = Files.walk(dir)) {
            walk.sorted((a, b) -> b.compareTo(a)).forEach(p -> p.toFile().delete());
        }
    }
}
