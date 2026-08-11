import java.io.File;
import java.io.FileDescriptor;
import java.io.FileOutputStream;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashMap;
import java.util.Map;
import java.util.zip.CRC32;

/** 数据库：手写一个 60 行的 KV 存储——亲手走到「你正在重新发明数据库」的那一刻。 */
public class Main {

    /** 追加日志 + 内存索引 = Bitcask 模型（Riak 的存储引擎就是它） */
    static class TinyDB implements AutoCloseable {
        private final RandomAccessFile log;
        private final Map<String, Long> index = new HashMap<>();  // key → 日志偏移量

        TinyDB(File f) throws Exception {
            log = new RandomAccessFile(f, "rw");
            recover();                                            // 启动 = 重放日志重建索引
        }

        void put(String key, String value) throws Exception {
            byte[] payload = (key + "=" + value).getBytes(StandardCharsets.UTF_8);
            CRC32 crc = new CRC32();
            crc.update(payload);
            long pos = log.length();
            log.seek(pos);
            log.writeInt(payload.length);                         // [长度][数据][校验和]
            log.write(payload);
            log.writeLong(crc.getValue());
            index.put(key, pos);                                  // 旧值不删，只是索引不再指向它
        }

        String get(String key) throws Exception {
            Long pos = index.get(key);
            if (pos == null) return null;
            log.seek(pos);
            byte[] buf = new byte[log.readInt()];
            log.readFully(buf);
            String s = new String(buf, StandardCharsets.UTF_8);
            return s.substring(s.indexOf('=') + 1);
        }

        /** 崩溃恢复：逐条读，校验和不对的（写了一半的）直接截断丢弃 */
        int recover() throws Exception {
            index.clear();
            long pos = 0, end = log.length();
            int dropped = 0;
            while (pos + 4 <= end) {
                try {
                    log.seek(pos);
                    int n = log.readInt();
                    if (n <= 0 || pos + 4 + n + 8 > end) { dropped++; break; }
                    byte[] buf = new byte[n];
                    log.readFully(buf);
                    long stored = log.readLong();
                    CRC32 crc = new CRC32();
                    crc.update(buf);
                    if (crc.getValue() != stored) { dropped++; break; }   // 半条记录
                    String s = new String(buf, StandardCharsets.UTF_8);
                    index.put(s.substring(0, s.indexOf('=')), pos);
                    pos += 4 + n + 8;
                } catch (Exception e) { dropped++; break; }
            }
            log.setLength(pos);                                   // 截掉损坏的尾巴
            return dropped;
        }

        @Override public void close() throws Exception { log.close(); }
    }

    public static void main(String[] args) throws Exception {
        Path work = Files.createTempDirectory("pl-mastery-java-db");
        byte[] rec = "id=00042,name=zhang,balance=100\n".getBytes(StandardCharsets.UTF_8);

        System.out.println("== ① 持久化两档（Java 视角）==");
        File f1 = work.resolve("t.log").toFile();
        try (FileOutputStream out = new FileOutputStream(f1)) {
            FileDescriptor fdo = out.getFD();
            final int N1 = 2000;
            long t0 = System.nanoTime();
            for (int i = 0; i < N1; i++) out.write(rec);
            double ms1 = (System.nanoTime() - t0) / 1e6;
            final int N2 = 200;
            t0 = System.nanoTime();
            for (int i = 0; i < N2; i++) { out.write(rec); fdo.sync(); }
            double ms2 = (System.nanoTime() - t0) / 1e6;
            System.out.printf("  只 write %d 次: %.1f ms；write+sync %d 次: %.1f ms（每次落盘贵 %.0fx）%n",
                    N1, ms1, N2, ms2, (ms2 / N2) / (ms1 / N1));
        }
        System.out.println("  → FileDescriptor.sync()/FileChannel.force() 都是 fsync；");
        System.out.println("    F_FULLFSYNC 在 JVM 里【够不到】（要 JNI）——纯 Java 无法承诺掉电安全");

        System.out.println("\n== ② 手写 KV 数据库：60 行的 TinyDB ==");
        File dbf = work.resolve("tiny.db").toFile();
        try (TinyDB db = new TinyDB(dbf)) {
            db.put("42", "zhang,100");
            db.put("43", "li,250");
            db.put("42", "zhang,40");                             // 更新 = 追加新版本
            System.out.println("  put(42) 两次后 get(42) = " + db.get("42") + "   ← 读到的是新版本");
            System.out.println("  日志文件里旧版本还在（追加写从不回头改）——顺序 I/O，这正是 WAL 的写法");
        }

        System.out.println("\n== ③ 崩溃测试：写一半的记录能被识别并丢弃 ==");
        try (RandomAccessFile raw = new RandomAccessFile(dbf, "rw")) {
            raw.seek(raw.length());
            byte[] half = "44=wang,999".getBytes(StandardCharsets.UTF_8);
            raw.writeInt(half.length);
            raw.write(half, 0, 5);                                // ← 「崩溃」：只写了 5 字节就断了
        }
        long corrupt = dbf.length();
        try (TinyDB db = new TinyDB(dbf)) {
            System.out.printf("  注入半条记录后文件 %d 字节；重启恢复后截断到 %d 字节%n",
                    corrupt, dbf.length());
            System.out.println("  get(42) = " + db.get("42") + "，get(44) = " + db.get("44")
                    + "   ← 完整的都在，半条被丢弃");
            System.out.println("  → [长度][数据][校验和] + 重放截断 = 最小可用的崩溃恢复协议（WAL 同款）");
        }

        System.out.println("\n== ④ 两个线程读-改-写同一个文件：丢更新 ==");
        Path cnt = work.resolve("counter.txt");
        Files.writeString(cnt, "0");
        final int EACH = 150;
        Runnable incr = () -> {
            for (int i = 0; i < EACH; i++) {
                try {
                    int v = Integer.parseInt(Files.readString(cnt).trim());
                    Thread.onSpinWait();
                    Files.writeString(cnt, String.valueOf(v + 1));
                } catch (Exception ignored) { }
            }
        };
        Thread a = new Thread(incr), b = new Thread(incr);
        a.start(); b.start(); a.join(); b.join();
        int got = Integer.parseInt(Files.readString(cnt).trim());
        System.out.printf("  2 线程 × %d 次自增，期望 %d，实际 %d（丢了 %d 次）%n",
                EACH, 2 * EACH, got, 2 * EACH - got);
        System.out.println("  → 第 40 章的数据竞争换到文件上一样成立；数据库用事务 + 锁（第 48/50 章）根治");

        System.out.println("\n== ⑤ 从 TinyDB 到真数据库还差什么 ==");
        System.out.println("  已有: 持久化(追加日志) + 崩溃恢复(校验和) + 点查(内存索引)");
        System.out.println("  还缺: 范围查询(要 B 树，第 49 章)  事务(第 48 章)  并发控制(第 50 章)");
        System.out.println("        SQL(第 47 章)  日志压缩(不然文件无限涨)  网络协议  权限……");
        System.out.println("  → 每补一项都是几千行起步——「用数据库」就是把这些全部外包");

        System.out.println("\n== ⑥ Java 连数据库的正道 ==");
        System.out.println("  JDBC: 一套接口、各家驱动（java.sql.Connection/PreparedStatement）");
        System.out.println("  本例不引第三方 jar 故用文件演示原理；生产用 JDBC + HikariCP（第 45 章的连接池）");
        Files.walk(work).sorted((x, y) -> y.compareTo(x)).forEach(p -> p.toFile().delete());
    }
}
