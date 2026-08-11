// 数据库：持久化的三档价格——write() / fsync / F_FULLFSYNC 逐档实测。
// 这一实验兑现第 43 章的悬案：为什么 macOS 上 synchronous=FULL 只慢了 1.5x。
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
    char path[] = "/tmp/pl-mastery-cpp-db-XXXXXX";
    int fd = mkstemp(path);
    if (fd < 0) { perror("mkstemp"); return 1; }
    const char rec[] = "id=00042,name=zhang,balance=100\n";
    const size_t len = strlen(rec);

    printf("== ① 第一档：只 write() —— 数据进的是【页缓存】==\n");
    const int N1 = 5000;
    auto t0 = Clock::now();
    for (int i = 0; i < N1; ++i) write(fd, rec, len);
    double ms1 = ms_since(t0);
    printf("  %d 次 write(): %.1f ms（%.2f μs/次）\n", N1, ms1, ms1 * 1000 / N1);
    printf("  → write() 返回成功 ≠ 数据在磁盘上；进程崩溃数据在（内核持有），【断电就没了】\n");

    printf("\n== ② 第二档：fsync() —— 数据到了【磁盘的写缓存】==\n");
    const int N2 = 300;
    t0 = Clock::now();
    for (int i = 0; i < N2; ++i) { write(fd, rec, len); fsync(fd); }
    double ms2 = ms_since(t0);
    printf("  %d 次 write+fsync: %.1f ms（%.1f μs/次）\n", N2, ms2, ms2 * 1000 / N2);
    printf("  ⚠️ macOS 的 fsync 只保证「交给磁盘」，不保证「磁盘写进介质」（POSIX 允许这样）\n");
    printf("  → OS 崩溃数据在，【掉电时磁盘缓存里的数据仍可能丢】\n");

    printf("\n== ③ 第三档：F_FULLFSYNC —— 数据真正落到【存储介质】==\n");
#ifdef F_FULLFSYNC
    const int N3 = 50;
    t0 = Clock::now();
    for (int i = 0; i < N3; ++i) { write(fd, rec, len); fcntl(fd, F_FULLFSYNC); }
    double ms3 = ms_since(t0);
    printf("  %d 次 write+F_FULLFSYNC: %.1f ms（%.1f μs/次）\n", N3, ms3, ms3 * 1000 / N3);
    printf("  → 三档对比: %.2f → %.0f → %.0f μs/次（每档一个数量级）\n",
           ms1 * 1000 / N1, ms2 * 1000 / N2, ms3 * 1000 / N3);
    printf("  → 谁在用它: SQLite 的 synchronous=FULL 在 macOS 上默认改走 F_BARRIERFSYNC（更快）\n");
    printf("    第 43 章实测 FULL 只慢 1.5x 的谜底就在这——真 F_FULLFSYNC 要慢得多（如上）\n");
#else
    printf("  （本平台无 F_FULLFSYNC，Linux 对应 fsync 已含介质落盘语义——取决于磁盘）\n");
    double ms3 = ms2;
#endif

    printf("\n== ④ 崩溃语义对照表 ==\n");
    printf("  ┌──────────────────┬──────────┬──────────┬──────────┐\n");
    printf("  │ 数据此刻在哪      │ 进程崩溃 │ 内核崩溃 │ 突然断电 │\n");
    printf("  ├──────────────────┼──────────┼──────────┼──────────┤\n");
    printf("  │ 用户态缓冲(FILE*) │ ✗ 丢     │ ✗ 丢     │ ✗ 丢     │\n");
    printf("  │ 页缓存(write)     │ ✓ 在     │ ✗ 丢     │ ✗ 丢     │\n");
    printf("  │ 磁盘缓存(fsync)   │ ✓ 在     │ ✓ 在     │ ⚠️ 可能丢 │\n");
    printf("  │ 介质(F_FULLFSYNC) │ ✓ 在     │ ✓ 在     │ ✓ 在     │\n");
    printf("  └──────────────────┴──────────┴──────────┴──────────┘\n");
    printf("  → 数据库的 D（持久性）承诺 = 永远为你选对档位，并把代价降到最低\n");

    printf("\n== ⑤ 数据库怎么降代价：组提交（group commit）==\n");
#ifdef F_FULLFSYNC
    const int BATCH = 100;
    t0 = Clock::now();
    for (int i = 0; i < BATCH; ++i) write(fd, rec, len);
    fcntl(fd, F_FULLFSYNC);                          // 一批只刷一次
    double msBatch = ms_since(t0);
    printf("  %d 条记录逐条 F_FULLFSYNC: 约 %.0f ms（外推自 ③）\n",
           BATCH, ms3 * 1000 / N3 * BATCH / 1000);
    printf("  %d 条记录攒一批刷一次:     %.1f ms\n", BATCH, msBatch);
    printf("  → 快 %.0fx —— WAL 的本质: 把 N 个事务的落盘合并成一次顺序写 + 一次 fsync\n",
           (ms3 / N3 * BATCH) / msBatch);
#endif
    printf("  → 追加写日志(WAL)是顺序 I/O，改 B 树是随机 I/O——先写日志再改树，两头占尽\n");

    printf("\n== ⑥ 崩溃恢复：WAL 的三步协议 ==\n");
    printf("  ① 把「我打算改什么」追加写进日志，fsync\n");
    printf("  ② 返回「提交成功」给用户 ← 此刻数据文件还没动！\n");
    printf("  ③ 之后慢慢把改动应用到数据文件（checkpoint）\n");
    printf("  崩溃后重启: 重放日志里已提交的、丢弃没写完的（靠每条记录末尾的校验和识别半条）\n");
    printf("  → 第 32 章栈帧、第 44 章协程帧之后，又一个「状态放哪」的问题——答案还是：先落日志\n");

    close(fd);
    unlink(path);
    return 0;
}
