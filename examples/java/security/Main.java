import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Arrays;
import java.util.Random;
import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;

/**
 * 安全：随机数、密码哈希与恒定时间比较——三个「看起来能用、实际上不能用」的 API。
 */
public class Main {

    static double ms(long t0) { return (System.nanoTime() - t0) / 1e6; }

    static byte[] pbkdf2(char[] pw, byte[] salt, int iters) throws Exception {
        var f = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256");
        return f.generateSecret(new PBEKeySpec(pw, salt, iters, 256)).getEncoded();
    }

    public static void main(String[] args) throws Exception {

        System.out.println("== ① java.util.Random 是可预测的（实测）==");
        // 攻击者场景: 系统用 Random 生成「密码重置令牌」，种子是当前毫秒时间戳
        long serverSeed = 1_700_000_000_123L;                 // 假设攻击者猜到了时间戳
        Random serverRng = new Random(serverSeed);
        int[] issued = new int[5];
        for (int i = 0; i < issued.length; i++) issued[i] = serverRng.nextInt(1_000_000);
        System.out.println("  服务端签发的 5 个重置令牌: " + Arrays.toString(issued));

        Random attackerRng = new Random(serverSeed);          // 攻击者用同一个种子重放
        int[] guessed = new int[5];
        for (int i = 0; i < guessed.length; i++) guessed[i] = attackerRng.nextInt(1_000_000);
        System.out.println("  攻击者用同一种子推算:      " + Arrays.toString(guessed));
        System.out.println("  完全一致: " + Arrays.equals(issued, guessed));
        System.out.println("  → java.util.Random 是【线性同余生成器】: 状态只有 48 位，");
        System.out.println("     观察到【两个连续输出】就能解出内部状态，然后预测出全部后续值");
        System.out.println("  → 它是为「模拟、洗牌、游戏」设计的，不是为安全设计的");

        SecureRandom sr = new SecureRandom();
        byte[] tok = new byte[16];
        sr.nextBytes(tok);
        System.out.printf("  SecureRandom 生成的令牌: %s（每次运行都不同，且不可推算）%n",
                java.util.HexFormat.of().formatHex(tok));
        System.out.println("  → 判定标准很简单: 【任何与安全相关的随机数，一律用 SecureRandom】");
        System.out.println("     令牌、盐、IV、会话 ID、密码重置码、CSRF token —— 无一例外");

        System.out.println("\n== ② 密码哈希：这里「越慢越好」（实测）==");
        char[] pw = "correct horse battery staple".toCharArray();
        byte[] salt = new byte[16];
        sr.nextBytes(salt);

        MessageDigest sha = MessageDigest.getInstance("SHA-256");
        byte[] pwBytes = new String(pw).getBytes("UTF-8");
        for (int w = 0; w < 200_000; w++) sha.digest(pwBytes);        // 预热（第 57 章）
        long t0 = System.nanoTime();
        final int FAST_N = 1_000_000;
        for (int i = 0; i < FAST_N; i++) sha.digest(pwBytes);
        double fastMs = ms(t0);
        double fastRate = FAST_N / (fastMs / 1000.0);
        System.out.printf("  SHA-256 直接哈希 %d 次: %.0f ms → 约 %.2f M 次/秒（单核）%n",
                FAST_N, fastMs, fastRate / 1e6);

        double slowRate = 0;
        for (int iters : new int[]{10_000, 100_000, 600_000}) {
            pbkdf2(pw, salt, iters);                                   // 预热
            t0 = System.nanoTime();
            pbkdf2(pw, salt, iters);
            double d = ms(t0);
            slowRate = 1000.0 / d;
            System.out.printf("  PBKDF2-HmacSHA256 %7d 轮: %7.1f ms → 约 %10.1f 次/秒%n",
                    iters, d, slowRate);
        }
        System.out.printf("  → 猜测速率相差 %,.0f 倍%n", fastRate / slowRate);
        System.out.println("  → 这是全书唯一一处【性能越差越好】的设计: 攻击者的成本 = 每次猜测的代价");
        System.out.println("  → 注意成本是【对称】的: 你的登录接口也要付这 150 ms，");
        System.out.println("     所以迭代次数是一个【安全 vs 可用性】的权衡，OWASP 会随硬件逐年上调建议值");
        System.out.println("  → 更好的选择是 Argon2id/scrypt/bcrypt: 它们还【故意吃内存】，");
        System.out.println("     让攻击者无法靠 GPU/ASIC 的并行度把成本摊平（PBKDF2 只吃 CPU，可被 GPU 加速）");

        System.out.println("\n== ③ 恒定时间比较（实测能测出差别）==");
        byte[] real = new byte[32];
        sr.nextBytes(real);
        byte[] wrongEarly = real.clone(); wrongEarly[0] ^= 1;          // 第 1 字节就不同
        byte[] wrongLate = real.clone(); wrongLate[31] ^= 1;           // 最后 1 字节才不同

        final int R = 3_000_000;
        for (int w = 0; w < 200_000; w++) { naiveEquals(real, wrongEarly); naiveEquals(real, wrongLate); }
        t0 = System.nanoTime();
        for (int i = 0; i < R; i++) naiveEquals(real, wrongEarly);
        double earlyMs = ms(t0);
        t0 = System.nanoTime();
        for (int i = 0; i < R; i++) naiveEquals(real, wrongLate);
        double lateMs = ms(t0);
        System.out.printf("  逐字节比较，第 1 字节就不同:  %6.1f ms%n", earlyMs);
        System.out.printf("  逐字节比较，第 32 字节才不同: %6.1f ms（慢 %.1fx）%n",
                lateMs, lateMs / earlyMs);
        System.out.println("  → 耗时【随猜对的前缀长度增长】——这个差距就是一条信息泄露通道");
        System.out.println("  → 攻击者逐字节爆破: 32 字节的令牌从 256^32 次降到 256×32 = 8192 次");

        for (int w = 0; w < 200_000; w++) { MessageDigest.isEqual(real, wrongEarly); }
        t0 = System.nanoTime();
        for (int i = 0; i < R; i++) MessageDigest.isEqual(real, wrongEarly);
        double ctEarly = ms(t0);
        t0 = System.nanoTime();
        for (int i = 0; i < R; i++) MessageDigest.isEqual(real, wrongLate);
        double ctLate = ms(t0);
        System.out.printf("  MessageDigest.isEqual 早不同: %6.1f ms%n", ctEarly);
        System.out.printf("  MessageDigest.isEqual 晚不同: %6.1f ms（差 %.2fx）%n",
                ctLate, ctLate / ctEarly);
        System.out.println("  → 恒定时间比较【始终扫完全部字节】，用位运算累积差异，不提前返回");
        System.out.println("  → Java 用 MessageDigest.isEqual；Python 用 hmac.compare_digest；");
        System.out.println("     C# 用 CryptographicOperations.FixedTimeEquals（C# 版实测）");

        System.out.println("\n== ④ 反序列化：把数据变成代码的另一条路 ==");
        System.out.println("  ObjectInputStream.readObject() 会【调用被反序列化类的方法】");
        System.out.println("  → 攻击者构造一条「gadget 链」，让 JVM 在还原对象的过程中执行任意命令");
        System.out.println("  → 这是 Java 生态最著名的一类漏洞（Apache Commons Collections、Log4Shell 同源思路）");
        System.out.println("  → 与 SQL 注入完全同构: 【不可信数据被交给了一个会「执行」它的东西】");
        System.out.println("  → 防御顺序: ① 别反序列化不可信数据 ② 用 JSON 等纯数据格式");
        System.out.println("     ③ 实在要用就上 ObjectInputFilter 白名单（Java 9+）");

        System.out.println("\n== ⑤ Java 的安全默认值 ==");
        System.out.println("  ✅ 内存安全: 数组越界抛 ArrayIndexOutOfBoundsException，不会踩到别人的内存（对比 C++ 版）");
        System.out.println("  ✅ 整数不会「悄悄」变成缓冲区大小: new int[n] 对负数直接抛异常");
        System.out.println("  ⚠️ 但整数【依然会溢出】: Integer.MAX_VALUE + 1 = " + (Integer.MAX_VALUE + 1));
        System.out.println("     经典漏洞形态: if (offset + len > size) 的左边先溢出成负数，检查被绕过");
        System.out.println("     → 用 Math.addExact() 让溢出抛异常，或改写成 offset > size - len");
        System.out.println("  ⚠️ 默认的 ObjectInputStream 不安全（④）、默认的 XML 解析器允许外部实体(XXE)");
        System.out.println("  → 「内存安全的语言」消灭了一整类漏洞，但【逻辑类漏洞一个都没少】");
    }

    /** ⚠️ 短路比较: 一发现不同就返回——耗时泄露了「猜对了多少」 */
    static boolean naiveEquals(byte[] a, byte[] b) {
        if (a.length != b.length) return false;
        for (int i = 0; i < a.length; i++) if (a[i] != b[i]) return false;
        return true;
    }
}
