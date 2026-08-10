public class Main {

    static class FileHandle implements AutoCloseable {
        private final String name;
        private final boolean failOnClose;

        FileHandle(String name) { this(name, false); }

        FileHandle(String name, boolean failOnClose) {
            this.name = name;
            this.failOnClose = failOnClose;
            System.out.println("    [获取] 打开 " + name);
        }

        @Override
        public void close() {
            System.out.println("    [释放] 关闭 " + name);
            if (failOnClose) throw new IllegalStateException("关闭 " + name + " 时也出错了");
        }
    }

    static void manualStyle() {
        FileHandle f = new FileHandle("manual.txt");
        throw new RuntimeException("中途出错");
        // f.close() 永远执行不到
    }

    static void twrStyle() {
        try (FileHandle f = new FileHandle("twr.txt")) {
            throw new RuntimeException("中途出错");
        }
    }

    public static void main(String[] args) {
        System.out.println("== ① try-with-resources：作用域即资源生命周期 ==");
        try (FileHandle f = new FileHandle("data.txt")) {
            System.out.println("    使用中……");
        }
        System.out.println("    块结束——无需手写 close");

        System.out.println("\n== ② 钥匙实验：异常安全 ==");
        System.out.println("  手动风格:");
        try { manualStyle(); }
        catch (RuntimeException e) {
            System.out.println("    捕获: " + e.getMessage() + "   <- 没有 [释放] 打印！句柄泄漏");
        }
        System.out.println("  try-with-resources 风格:");
        try { twrStyle(); }
        catch (RuntimeException e) {
            System.out.println("    捕获: " + e.getMessage() + "   <- [释放] 已在上一行打印");
        }

        System.out.println("\n== ③ 多个资源：逆序关闭 ==");
        try (FileHandle a = new FileHandle("第一个");
             FileHandle b = new FileHandle("第二个");
             FileHandle c = new FileHandle("第三个")) {
            throw new RuntimeException("三个都开着的时候出错了");
        } catch (RuntimeException e) {
            System.out.println("    三个全部关闭，顺序是 3-2-1（声明的逆序）");
        }

        System.out.println("\n== ④ Java 独有：抑制异常（Suppressed）==");
        System.out.println("  业务出错 + 关闭也出错，谁赢？");
        try (FileHandle f = new FileHandle("双重故障.txt", true)) {
            throw new RuntimeException("业务逻辑出错（主异常）");
        } catch (RuntimeException e) {
            System.out.println("    主异常: " + e.getMessage());
            for (Throwable s : e.getSuppressed()) {
                System.out.println("    被抑制: " + s.getMessage() + "   <- 没有丢失！");
            }
            System.out.println("    （手写 finally 里 close 抛异常会\"顶掉\"主异常——真凶被掩盖）");
        }

        System.out.println("\n== ⑤ 对比：手写 finally 的异常吞噬 ==");
        try {
            FileHandle f = new FileHandle("finally.txt", true);
            try {
                throw new RuntimeException("业务逻辑出错（主异常）");
            } finally {
                f.close();                 // 这里抛异常 -> 主异常被丢弃
            }
        } catch (RuntimeException e) {
            System.out.println("    最终看到的异常: " + e.getMessage());
            System.out.println("    被抑制列表长度: " + e.getSuppressed().length
                    + "   <- 主异常彻底消失了，只剩 close 的异常");
        }
    }
}
