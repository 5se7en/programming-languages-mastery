import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.time.Duration;
import java.util.List;

public class Main {

    static int counter = 100;              // JVM 内的静态变量——子进程完全看不到

    public static void main(String[] args) throws Exception {
        System.out.println("== ① 进程身份：ProcessHandle（Java 9+）==");
        ProcessHandle self = ProcessHandle.current();
        System.out.println("  我是进程 " + self.pid()
                + "，父进程 " + self.parent().map(ProcessHandle::pid).orElse(-1L));
        System.out.println("  可用处理器数 = " + Runtime.getRuntime().availableProcessors());

        System.out.println("\n== ② 钥匙实验：子进程是另一个世界 ==");
        System.out.println("  父进程 counter = " + counter);
        ProcessBuilder pb = new ProcessBuilder("sh", "-c",
                "echo \"  [子进程 $$] 我是独立进程，看不到 Java 的 counter 变量\"");
        pb.redirectErrorStream(true);
        Process child = pb.start();
        try (BufferedReader r = new BufferedReader(new InputStreamReader(child.getInputStream()))) {
            String line;
            while ((line = r.readLine()) != null) System.out.println(line);
        }
        int exit = child.waitFor();
        System.out.println("  子进程退出码 = " + exit + "，父进程 counter 仍是 " + counter);
        System.out.println("  （Java 无法 fork——只能启动全新进程，连内存快照都不共享）");

        System.out.println("\n== ③ 进程间通信：标准输入输出流 ==");
        Process grep = new ProcessBuilder("grep", "并发").start();
        grep.getOutputStream().write("第 39 章 进程\n第 40 章 线程\nPart 6 并发\n".getBytes("UTF-8"));
        grep.getOutputStream().close();
        try (BufferedReader r = new BufferedReader(
                new InputStreamReader(grep.getInputStream(), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null) System.out.println("  grep 返回: " + line);
        }
        grep.waitFor();
        System.out.println("  （管道 = 进程间通信的经典形态，与 C++ 的 pipe() 同源）");

        System.out.println("\n== ④ 进程树：看看系统上真实的进程 ==");
        List<ProcessHandle> children = self.children().toList();
        System.out.println("  我的子进程数 = " + children.size());
        long total = ProcessHandle.allProcesses().count();
        System.out.println("  系统上共有 " + total + " 个可见进程");

        System.out.println("\n== ⑤ Java 的立场：进程用得少，线程用得多 ==");
        System.out.println("  JVM 启动成本高（第 5 章），所以 Java 生态偏爱多线程");
        System.out.println("  ProcessBuilder 主要用于调用外部命令，而非并行计算");
        System.out.println("  onExit() 提供 CompletableFuture 异步等待（第 42 章）");
        Process quick = new ProcessBuilder("true").start();
        quick.onExit().get(Duration.ofSeconds(5).toMillis(), java.util.concurrent.TimeUnit.MILLISECONDS);
        System.out.println("  onExit() 异步等待完成，退出码 = " + quick.exitValue());
    }
}
