// 安全：密码学的默认值——「能跑通」和「安全」之间隔着好几个不会报错的选择。
using System.Diagnostics;
using System.Security.Cryptography;
using System.Text;

class Program
{
    static string Hex(byte[] b, int max = 32) =>
        Convert.ToHexString(b)[..Math.Min(max, b.Length * 2)].ToLower();

    static void Main()
    {
        Console.WriteLine("== ① ECB 模式：加密了，但什么都没藏住（实测）==");
        // 构造一段「有结构」的明文: 三条记录，其中两条内容相同
        var records = new[] { "alice:role=user", "bob:role=user", "alice:role=user" }
            .Select(r => r.PadRight(16))                // 每条恰好一个 AES 块（16 字节）
            .ToArray();
        var plain = Encoding.ASCII.GetBytes(string.Concat(records));
        byte[] key = RandomNumberGenerator.GetBytes(32);

        byte[] EncryptEcb()
        {
            using var aes = Aes.Create();
            aes.Key = key; aes.Mode = CipherMode.ECB; aes.Padding = PaddingMode.None;
            return aes.EncryptEcb(plain, PaddingMode.None);
        }
        byte[] EncryptCbc()
        {
            using var aes = Aes.Create();
            aes.Key = key;
            return aes.EncryptCbc(plain, RandomNumberGenerator.GetBytes(16), PaddingMode.None);
        }

        var ecb = EncryptEcb();
        var cbc = EncryptCbc();
        Console.WriteLine("  明文的三个 16 字节块（第 1 块与第 3 块相同）:");
        for (int i = 0; i < 3; i++)
            Console.WriteLine($"    块{i + 1}: {Encoding.ASCII.GetString(plain, i * 16, 16)}");
        Console.WriteLine("  ECB 密文:");
        for (int i = 0; i < 3; i++)
            Console.WriteLine($"    块{i + 1}: {Hex(ecb[(i * 16)..((i + 1) * 16)])}");
        bool ecbLeaks = ecb[0..16].SequenceEqual(ecb[32..48]);
        Console.WriteLine($"  → 第 1 块与第 3 块的密文相同: {ecbLeaks}  ← 【明文结构完整泄露】");
        Console.WriteLine("  CBC 密文:");
        for (int i = 0; i < 3; i++)
            Console.WriteLine($"    块{i + 1}: {Hex(cbc[(i * 16)..((i + 1) * 16)])}");
        Console.WriteLine($"  → 第 1 块与第 3 块的密文相同: {cbc[0..16].SequenceEqual(cbc[32..48])}");
        Console.WriteLine("  → ECB 逐块独立加密 → 【相同明文块 = 相同密文块】");
        Console.WriteLine("     著名的「ECB 企鹅」图片就是这么来的: 加密后还能看出轮廓");
        Console.WriteLine("  → 两次加密都没报错、都能正确解密——【安全性不体现在功能测试里】");

        Console.WriteLine("\n== ② 同一个 IV 用两次，CBC 也一样泄露（实测）==");
        byte[] fixedIv = new byte[16];                 // ⚠️ 全零的固定 IV
        byte[] Enc(string s)
        {
            using var aes = Aes.Create();
            aes.Key = key;
            return aes.EncryptCbc(Encoding.ASCII.GetBytes(s.PadRight(32)), fixedIv, PaddingMode.None);
        }
        var m1 = Enc("transfer 100 to bob");
        var m2 = Enc("transfer 100 to bob");
        var m3 = Enc("transfer 100 to eve");           // 前 16 字节与 m1 相同，第 17 字节起不同
        Console.WriteLine($"  明文 A = \"transfer 100 to bob\"，明文 B = \"transfer 100 to eve\"");
        Console.WriteLine($"  两次加密【同一条】明文 A → 密文完全相同: {m1.SequenceEqual(m2)}");
        Console.WriteLine($"  加密 A 与 B → 第 1 块密文相同: {m1[0..16].SequenceEqual(m3[0..16])}"
                          + "  ← 泄露了「两条消息前 16 字节一样」");
        Console.WriteLine($"                第 2 块密文相同: {m1[16..32].SequenceEqual(m3[16..32])}"
                          + "  ← 差异从这里开始");
        Console.WriteLine("  → 攻击者由此能【二分定位差异位置】，还能识别重放的请求");
        Console.WriteLine("  → IV 的作用就是【让相同明文每次产生不同密文】，固定它等于取消这个作用");
        Console.WriteLine("  → IV 必须【每次随机生成】（可以公开传输，但不能重复）");
        Console.WriteLine("  → 更彻底的解法: 用 AES-GCM 这类 AEAD 模式，它同时提供机密性与【完整性】");

        Console.WriteLine("\n== ③ 只加密不认证：密文可以被「有意义地」篡改 ==");
        Console.WriteLine("  CBC 不保证完整性: 攻击者改一个密文字节，解密仍然「成功」，");
        Console.WriteLine("  只是对应的明文块变成乱码——而【前一块的对应位会被精确翻转】");
        byte[] nonce = RandomNumberGenerator.GetBytes(12);
        byte[] msg = Encoding.ASCII.GetBytes("transfer 100 to bob");
        byte[] ct = new byte[msg.Length], tag = new byte[16];
        using (var gcm = new AesGcm(key, 16)) gcm.Encrypt(nonce, msg, ct, tag);
        ct[0] ^= 1;                                    // 篡改一个 bit
        try
        {
            byte[] outp = new byte[ct.Length];
            using var gcm = new AesGcm(key, 16);
            gcm.Decrypt(nonce, ct, tag, outp);
            Console.WriteLine("    AES-GCM 解密被篡改的密文 → 竟然成功了（不应发生）");
        }
        catch (CryptographicException)
        {
            Console.WriteLine("    AES-GCM 解密被篡改的密文 → 抛出 CryptographicException ✅");
        }
        Console.WriteLine("  → AEAD 把「认证」内建进来: 密文动了一个 bit，解密就直接失败");
        Console.WriteLine("  → 规则: 【永远不要只加密不认证】。默认选 AES-GCM 或 ChaCha20-Poly1305");

        Console.WriteLine("\n== ④ 恒定时间比较（实测能测出差别）==");
        byte[] real = RandomNumberGenerator.GetBytes(32);
        byte[] early = (byte[])real.Clone(); early[0] ^= 1;
        byte[] late = (byte[])real.Clone(); late[31] ^= 1;

        static bool Naive(byte[] a, byte[] b)
        {
            if (a.Length != b.Length) return false;
            for (int i = 0; i < a.Length; i++) if (a[i] != b[i]) return false;   // ⚠️ 提前返回
            return true;
        }
        static double Bench(Func<bool> f, int rounds = 5)
        {
            for (int i = 0; i < 2_000_000; i++) f();                              // 预热
            double best = double.MaxValue;
            for (int r = 0; r < rounds; r++)
            {
                var sw = Stopwatch.StartNew();
                for (int i = 0; i < 5_000_000; i++) f();
                best = Math.Min(best, sw.Elapsed.TotalMilliseconds);
            }
            return best;
        }
        double nEarly = Bench(() => Naive(real, early));
        double nLate = Bench(() => Naive(real, late));
        Console.WriteLine($"  逐字节比较，第 1 字节就不同:  {nEarly,7:F1} ms");
        Console.WriteLine($"  逐字节比较，第 32 字节才不同: {nLate,7:F1} ms（慢 {nLate / nEarly:F1}x）");
        Console.WriteLine("  → 耗时随【猜对的前缀长度】增长，这就是一条信息泄露通道");

        double fEarly = Bench(() => CryptographicOperations.FixedTimeEquals(real, early));
        double fLate = Bench(() => CryptographicOperations.FixedTimeEquals(real, late));
        Console.WriteLine($"  FixedTimeEquals，第 1 字节不同:  {fEarly,7:F1} ms");
        Console.WriteLine($"  FixedTimeEquals，第 32 字节不同: {fLate,7:F1} ms（差 {fLate / fEarly:F2}x）");
        Console.WriteLine("  → 恒定时间比较不提前返回，用位运算累积差异后一次性判断");
        Console.WriteLine($"  ⚠️ 注意它的【绝对耗时更高】（{fEarly:F0} ms vs {nEarly:F0} ms）: 它永远做满全部工作");
        Console.WriteLine("     这正是本章与第 57 章的分歧点: 那里追求「尽早返回」，这里必须【拒绝提前返回】");
        Console.WriteLine("  → 凡是比较令牌/签名/HMAC/密码哈希，一律用它（Java 版实测了同一件事）");

        Console.WriteLine("\n== ⑤ 随机数：两个 API，只有一个能用 ==");
        var predictable = new Random(12345);
        var replay = new Random(12345);
        var a = Enumerable.Range(0, 4).Select(_ => predictable.Next(1_000_000)).ToArray();
        var b = Enumerable.Range(0, 4).Select(_ => replay.Next(1_000_000)).ToArray();
        Console.WriteLine($"  Random(12345) 生成: [{string.Join(", ", a)}]");
        Console.WriteLine($"  同种子重放:        [{string.Join(", ", b)}]  → 完全一致: {a.SequenceEqual(b)}");
        Console.WriteLine($"  RandomNumberGenerator: {Hex(RandomNumberGenerator.GetBytes(16))}（不可预测）");
        Console.WriteLine("  → System.Random 是伪随机: 种子确定则序列确定，且能从少量输出反推状态");
        Console.WriteLine("  → 令牌/盐/IV/会话 ID 一律用 RandomNumberGenerator（Java 版是 SecureRandom）");
        Console.WriteLine("  ⚠️ 命名是个陷阱: Random 听起来就够随机了，而它恰恰是不能用的那个");

        Console.WriteLine("\n== ⑥ 密码学的「不要自己造」清单 ==");
        Console.WriteLine("  ❌ 自己设计加密算法       → 用 AES-GCM / ChaCha20-Poly1305");
        Console.WriteLine("  ❌ 自己实现填充/模式拼装   → 用 AEAD，别手工组 CBC+HMAC");
        Console.WriteLine("  ❌ 用 SHA-256 存密码      → 用 Argon2id / bcrypt / PBKDF2（Python/Java 版实测差 6 个数量级）");
        Console.WriteLine("  ❌ 用 == 比较密钥         → 用 FixedTimeEquals（④ 实测）");
        Console.WriteLine("  ❌ 用 Random 生成令牌      → 用 RandomNumberGenerator（⑤ 实测）");
        Console.WriteLine("  ❌ 固定或复用 IV/nonce    → 每次随机生成（② 实测）");
        Console.WriteLine("  → 共同点: 上面每一条【功能测试都会通过】——");
        Console.WriteLine("     密码学的错误不会让程序崩溃，只会让它安静地不安全");
    }
}
