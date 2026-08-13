import java.io.File;
import java.lang.management.ManagementFactory;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

/**
 * 部署：「运行它需要带走什么」——JVM 语言把一整个运行时也算进了交付物。
 */
public class Main {

    /** 递归统计一个目录的总大小与文件数 */
    static long[] measure(Path root) {
        long bytes = 0, files = 0;
        List<Path> stack = new ArrayList<>();
        stack.add(root);
        while (!stack.isEmpty()) {
            Path p = stack.remove(stack.size() - 1);
            File f = p.toFile();
            File[] kids = f.listFiles();
            if (kids == null) continue;
            for (File k : kids) {
                if (k.isDirectory()) stack.add(k.toPath());
                else if (k.isFile()) { bytes += k.length(); files++; }
            }
        }
        return new long[]{bytes, files};
    }

    public static void main(String[] args) throws Exception {
        var rt = ManagementFactory.getRuntimeMXBean();

        System.out.println("== ① 启动时间：main() 之前已经花掉了多少（实测）==");
        long beforeMain = rt.getUptime();                 // JVM 启动 → 走到这里
        long t0 = System.nanoTime();
        // 做一点真实工作，作为「业务代码」的参照
        long sum = 0;
        for (int i = 0; i < 5_000_000; i++) sum += i % 7;
        double workMs = (System.nanoTime() - t0) / 1e6;
        System.out.printf("  JVM 启动 → 进入 main(): %6.1f ms%n", (double) beforeMain);
        System.out.printf("  一段 500 万次循环的业务代码: %6.1f ms（结果 %d）%n", workMs, sum);
        System.out.printf("  → 【还没执行任何业务逻辑，就已经花了 %.0f ms】%n", (double) beforeMain);
        System.out.println("  → 这笔开销在长驻服务里可以忽略（分摊到几个月的运行时间上），");
        System.out.println("     但在 Serverless / CLI / 扩容时它就是【用户直接感受到的延迟】");
        System.out.println("  → 这是选择部署形态时最容易被忽略的一个变量");

        System.out.println("\n== ② 交付物：要带走的运行时有多大（实测）==");
        Path javaHome = Path.of(System.getProperty("java.home"));
        long[] jre = measure(javaHome);
        System.out.printf("  java.home = %s%n", javaHome);
        System.out.printf("  运行时总大小: %,d 字节（%.0f MB，%,d 个文件）%n",
                jre[0], jre[0] / 1048576.0, jre[1]);

        // 量一下「你自己写的那部分」到底多大
        long mine = 0;
        try {
            Path cs = Path.of(Main.class.getProtectionDomain()
                    .getCodeSource().getLocation().toURI());
            mine = Files.isDirectory(cs) ? measure(cs)[0] : Files.size(cs);
        } catch (Exception ignored) { }
        System.out.printf("  你的业务代码（本例编译产物）: %,d 字节%n", mine);
        System.out.printf("  → 比例 1 : %,.0f —— 交付物里【%.4f%% 才是你写的代码】%n",
                (double) jre[0] / Math.max(mine, 1), 100.0 * mine / (jre[0] + mine));
        System.out.println("  → 这就是「基础镜像选择比业务代码优化更影响镜像大小」的原因");
        System.out.println("  → jlink 可以按实际用到的模块裁剪运行时（下面 ③ 算一下能裁多少）");

        System.out.println("\n== ③ 模块化：实际用到的远少于打包的（实测）==");
        var modules = ModuleLayer.boot().modules();
        var loaded = new TreeMap<String, Integer>();
        for (var m : modules) loaded.put(m.getName(), 1);
        System.out.printf("  JDK 提供的模块总数: %d%n", ModuleFinder.count());
        System.out.printf("  本进程实际解析到的模块: %d%n", loaded.size());
        System.out.print("  本例真正用到的: ");
        System.out.println("java.base, java.management（另加启动时默认解析的若干个）");
        System.out.println("  → jlink --add-modules java.base 可以把运行时压到几十 MB");
        System.out.println("  → 但注意: 裁剪是【构建期决定】的，运行时才发现少了模块就来不及了");
        System.out.println("     反射加载（第 30 章）会让「用到了哪些模块」无法被静态分析出来——");
        System.out.println("     这是 AOT / 裁剪 / 原生镜像共同的难点");

        System.out.println("\n== ④ 环境依赖：「在我机器上是好的」的三种成因 ==");
        Map<String, String> env = new TreeMap<>();
        for (String k : new String[]{"java.version", "java.vendor", "os.name", "os.arch",
                "file.encoding", "user.timezone", "user.language"}) {
            env.put(k, String.valueOf(System.getProperty(k)));
        }
        env.forEach((k, v) -> System.out.printf("    %-16s = %s%n", k, v));
        System.out.println("  成因一【运行时版本】: 用 17 编译、拿 11 运行 → UnsupportedClassVersionError");
        System.out.println("     好消息是它【启动就失败】，属于最容易发现的一种");
        System.out.println("  成因二【隐式的环境依赖】: file.encoding / user.timezone / locale");
        System.out.println("     它们【不会报错】，只会让日期、排序、大小写转换的结果悄悄不同");
        System.out.printf("     例: \"I\".toLowerCase() 在土耳其 locale 下得到的不是 \"i\"%n");
        System.out.println("  成因三【外部状态】: 数据库 schema、配置、文件路径、时钟、DNS");
        System.out.println("     这一类最难，因为它【不在你的交付物里】（SQL 版专门讲这个）");
        System.out.println("  → 防御: 显式声明一切（JDK 版本、编码、时区），别依赖默认值");

        System.out.println("\n== ⑤ 三种交付形态的取舍 ==");
        System.out.println("  ⓐ jar + 系统 JRE:   产物最小（几 MB），但【依赖目标机器装了对的 JRE】");
        System.out.println("  ⓑ fat jar / 容器:    自带全部依赖，可复现；镜像几百 MB");
        System.out.println("  ⓒ jlink 定制运行时:  裁剪后几十 MB，启动略快");
        System.out.println("  ⓓ GraalVM 原生镜像:  启动降到毫秒级、内存占用大降，");
        System.out.println("     代价是构建慢、反射需要显式配置、失去 JIT 的峰值性能（第 57 章）");
        System.out.println("  → 没有最优解: 长驻服务选 ⓑ/ⓒ，Serverless/CLI 选 ⓓ");
        System.out.println("  → 判据是【启动次数】: 启动一次跑三个月，和每秒启动一百次，答案完全不同");

        System.out.println("\n== ⑥ 分层：为什么容器镜像要分层 ==");
        System.out.println("  一个典型 Java 镜像的层次（从下到上，变化频率递增）:");
        System.out.println("    ① 基础 OS         ~50 MB   几个月变一次");
        System.out.println("    ② JRE             ~200 MB  几个月变一次");
        System.out.println("    ③ 第三方依赖 jar   ~50 MB   几周变一次");
        System.out.println("    ④ 你的业务代码     ~1 MB    【每天变几十次】");
        System.out.println("  → 如果打成一个 fat jar，改一行代码就要重传【全部 300 MB】");
        System.out.println("  → 分层之后只重传第 ④ 层的 1 MB —— 这就是 jib / Spring Boot 分层的意义");
        System.out.println("  → 通用原则: 【按变化频率分层】，稳定的在下，易变的在上");
        System.out.println("     这和第 54 章增量构建是同一个思路: 让不变的部分不必重做");
    }

    /** 数一下当前 JDK 里有多少个可用模块 */
    static class ModuleFinder {
        static int count() {
            return java.lang.module.ModuleFinder.ofSystem().findAll().size();
        }
    }
}
