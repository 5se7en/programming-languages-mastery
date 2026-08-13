import java.io.File;
import java.lang.reflect.Method;
import java.net.URL;
import java.net.URLClassLoader;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Deque;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import javax.tools.JavaCompiler;
import javax.tools.ToolProvider;

/**
 * 包管理：Maven 的答案——「最近者胜」，全局只留一个版本。
 * 用 JDK 自带的编译器动态编译两个版本的库，再用类加载器复现 NoSuchMethodError 运行时炸弹。
 */
public class Main {

    // ---------- ① 手写 Maven 的依赖调解（dependency mediation）----------
    record Dep(String name, String version, List<Dep> deps) {
        static Dep of(String name, String version, Dep... deps) {
            return new Dep(name, version, List.of(deps));
        }
    }

    /** Maven 规则: BFS 依赖树，同名包【深度最浅】的版本胜；同深度则先声明的胜。 */
    static Map<String, String> mediate(List<Dep> roots) {
        Map<String, String> winner = new LinkedHashMap<>();
        Deque<Dep> queue = new ArrayDeque<>(roots);
        while (!queue.isEmpty()) {
            Dep d = queue.poll();
            winner.putIfAbsent(d.name(), d.version());   // 先到先得 = 浅者/先声明者胜
            queue.addAll(d.deps());
        }
        return winner;
    }

    public static void main(String[] args) throws Exception {
        System.out.println("== ① Maven 的依赖调解：最近者胜（手写实测）==");
        Dep httpLib14 = Dep.of("http-lib", "1.4");
        Dep httpLib21 = Dep.of("http-lib", "2.1");
        Dep authKit = Dep.of("auth-kit", "1.0", httpLib14);         // auth-kit → http-lib 1.4
        Dep webFramework = Dep.of("web-framework", "2.0", httpLib21); // web-framework → http-lib 2.1

        Map<String, String> r1 = mediate(List.of(webFramework, authKit));
        Map<String, String> r2 = mediate(List.of(authKit, webFramework));
        System.out.println("  依赖树: web-framework→http-lib 2.1；auth-kit→http-lib 1.4（深度相同）");
        System.out.println("  声明顺序 [web-framework, auth-kit] → http-lib 胜出版本: "
                + r1.get("http-lib"));
        System.out.println("  声明顺序 [auth-kit, web-framework] → http-lib 胜出版本: "
                + r2.get("http-lib"));
        System.out.println("  → 【调换 pom.xml 里两行的顺序，依赖版本就变了】——且没有任何警告");
        System.out.println("  → Maven 不求解约束（对比 Python 版的回溯），也不共存（对比 JS 版）:");
        System.out.println("     它用一条简单规则【裁决】出唯一版本——输家的约束被静默丢弃");

        System.out.println("\n== ② 被裁决掉的版本去哪了：运行时炸弹（动态编译 + 类加载实测）==");
        Path work = Files.createTempDirectory("pl-mastery-mvn");
        Path v1Dir = work.resolve("http-lib-1.4"), v2Dir = work.resolve("http-lib-2.1");
        Files.createDirectories(v1Dir);
        Files.createDirectories(v2Dir);

        // http-lib 1.4: 只有 get()
        compile(v1Dir, "HttpLib", """
                public class HttpLib {
                    public static String get(String url) { return "GET " + url; }
                    public static String version() { return "1.4"; }
                }""");
        // http-lib 2.1: 多了 postJson() —— web-framework 编译时用的就是它
        compile(v2Dir, "HttpLib", """
                public class HttpLib {
                    public static String get(String url) { return "GET " + url; }
                    public static String postJson(String url) { return "POST " + url; }
                    public static String version() { return "2.1"; }
                }""");
        System.out.println("  动态编译了两个版本: http-lib 1.4（只有 get）、2.1（多了 postJson）");

        // 模拟 web-framework 的代码: 编译时基于 2.1，调用 postJson
        Path wfDir = work.resolve("web-framework");
        Files.createDirectories(wfDir);
        compile(wfDir, "WebFramework", """
                public class WebFramework {
                    public static String handle() { return HttpLib.postJson("/api"); }
                }""", v2Dir);
        System.out.println("  web-framework 用 2.1 的头【编译通过】（它调用了 postJson）");

        // 运行时: classpath 上是被 Maven 裁决出的 1.4（auth-kit 先声明的场景）
        System.out.println("\n  场景 A —— classpath 上是裁决胜出的 1.4:");
        runHandle(new Path[]{wfDir, v1Dir});
        System.out.println("  场景 B —— classpath 上是 2.1:");
        runHandle(new Path[]{wfDir, v2Dir});
        System.out.println("  → 编译期用 2.1、运行期是 1.4 → NoSuchMethodError【运行时】才炸");
        System.out.println("  → 这就是 Maven 版依赖地狱的形态: 装包不报错、编译不报错、上线才炸");
        System.out.println("  → 对比: Python 装包时冲突可见（求解失败），npm 运行时 TypeError（JS 版实测）");

        System.out.println("\n== ③ classpath 顺序决定谁被加载（实测）==");
        try (URLClassLoader cl = loader(v1Dir, v2Dir)) {
            Class<?> c = cl.loadClass("HttpLib");
            System.out.println("  classpath [1.4, 2.1] → 加载到的版本: "
                    + c.getMethod("version").invoke(null));
        }
        try (URLClassLoader cl = loader(v2Dir, v1Dir)) {
            Class<?> c = cl.loadClass("HttpLib");
            System.out.println("  classpath [2.1, 1.4] → 加载到的版本: "
                    + c.getMethod("version").invoke(null));
        }
        System.out.println("  → 同名类【先出现在 classpath 上的赢】，后面的永远不会被看一眼");
        System.out.println("  → 两个 jar 各带一份同名类（shade 没做好）时，谁赢取决于文件系统枚举顺序——");
        System.out.println("     这就是「本地好好的，CI 上炸了」的经典成因之一");

        System.out.println("\n== ④ Java 生态的三道防线 ==");
        System.out.println("  ① mvn dependency:tree + enforcer 插件: 把「同包多版本」变成【构建失败】");
        System.out.println("  ② BOM（Bill of Materials）: 框架发布一组【互相兼容】的版本清单，一次导入");
        System.out.println("     （Spring Boot 的 parent pom 管着几百个依赖的版本——你一个都不用写）");
        System.out.println("  ③ shade/relocation: 把依赖【改名】打进自己的 jar（org.foo → shaded.org.foo）");
        System.out.println("     —— 用重命名实现 npm 式的多版本共存，代价是包体积与调试栈");
        System.out.println("  → Gradle 默认规则不同: 冲突时【最高版本】胜（仍是单版本，但比「最近」可预测）");

        System.out.println("\n== ⑤ 三种生态的根本分野（本章总纲）==");
        System.out.println("  npm  : 冲突 → 各装各的（共存）  → 代价在运行时（instanceof/双实例）");
        System.out.println("  pip  : 冲突 → 回溯求解（单版本）→ 代价在安装时（NP 完全的求解）");
        System.out.println("  Maven: 冲突 → 规则裁决（单版本）→ 代价在【没人发现的运行时】(② 实测)");
        System.out.println("  → 决定因素是【模块系统】: JS 的 require 按路径解析（可共存），");
        System.out.println("     Python 的 sys.modules 和 JVM 的类加载按【名字】全局唯一（必须单版本）");

        deleteRecursively(work);
    }

    static void runHandle(Path[] classpath) throws Exception {
        try (URLClassLoader cl = loader(classpath)) {
            Class<?> wf = cl.loadClass("WebFramework");
            try {
                Object result = wf.getMethod("handle").invoke(null);
                System.out.println("    WebFramework.handle() → " + result + " ✓");
            } catch (Exception e) {
                Throwable cause = e.getCause();
                System.out.println("    WebFramework.handle() → ✗ "
                        + cause.getClass().getSimpleName() + ": " + cause.getMessage());
            }
        }
    }

    static URLClassLoader loader(Path... dirs) throws Exception {
        URL[] urls = new URL[dirs.length];
        for (int i = 0; i < dirs.length; i++) urls[i] = dirs[i].toUri().toURL();
        return new URLClassLoader(urls, ClassLoader.getPlatformClassLoader());
    }

    static void compile(Path outDir, String className, String source, Path... classpath)
            throws Exception {
        Path src = outDir.resolve(className + ".java");
        Files.writeString(src, source);
        JavaCompiler javac = ToolProvider.getSystemJavaCompiler();
        List<String> opts = new ArrayList<>(List.of("-d", outDir.toString()));
        if (classpath.length > 0) {
            StringBuilder cp = new StringBuilder();
            for (Path p : classpath) cp.append(p).append(File.pathSeparator);
            opts.addAll(List.of("-cp", cp.toString()));
        }
        opts.add(src.toString());
        int rc = javac.run(null, null, null, opts.toArray(String[]::new));
        if (rc != 0) throw new IllegalStateException("编译失败: " + className);
    }

    static void deleteRecursively(Path dir) throws Exception {
        try (var walk = Files.walk(dir)) {
            walk.sorted((x, y) -> y.compareTo(x)).forEach(p -> p.toFile().delete());
        }
    }
}
