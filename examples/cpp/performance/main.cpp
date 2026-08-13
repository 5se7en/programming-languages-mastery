// 性能优化：同样的复杂度，性能差一个数量级——缓存局部性是「大 O 之外」最大的那个变量。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <numeric>
#include <random>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

int main() {
    const int N = 4'000'000;
    volatile long long sink = 0;

#ifdef __OPTIMIZE__
    printf("【构建配置】开启了优化（-O2 或更高）——这是性能测量应有的配置\n\n");
#else
    printf("⚠️【构建配置】未开启优化（-O0）——本章测得的所有数字都会因此偏离真实性能\n");
    printf("   本仓库的 run-all.sh 为了让所有章节可复现，统一用不带 -O 的默认编译；\n");
    printf("   要看真实数字请手动编译: g++ -std=c++20 -O2 main.cpp && ./a.out\n");
    printf("   → 这本身就是本章的第一课: 【任何性能数字都必须注明构建配置】\n");
    printf("   → 下面 ⑤ 的结论也会随优化级别而变，示例会如实说明\n\n");
#endif

    printf("== ① 同样是 O(n) 遍历：数组 vs 链表（实测）==\n");
    std::vector<int> vec(N);
    std::iota(vec.begin(), vec.end(), 0);
    std::list<int> lst(vec.begin(), vec.end());

    auto t0 = Clock::now();
    long long s1 = 0;
    for (int x : vec) s1 += x;
    double msVec = ms_since(t0);

    t0 = Clock::now();
    long long s2 = 0;
    for (int x : lst) s2 += x;
    double msList = ms_since(t0);

    printf("  vector 遍历 %d 个元素: %7.1f ms\n", N, msVec);
    printf("  list   遍历 %d 个元素: %7.1f ms（慢 %.1fx）\n", N, msList, msList / msVec);
    printf("  结果一致: %s\n", s1 == s2 ? "true" : "false");
    printf("  → 两者都是 O(n)，教科书上「复杂度相同」——实测差了一个数量级\n");
    printf("  → 原因: vector 元素连续（一次缓存行读回 16 个 int），list 每个节点在堆上随机位置\n");
    printf("  → 大 O 只数【操作次数】，不数【每次操作有多贵】——而缓存缺失比命中贵约 100 倍\n");

    printf("\n== ② 缓存局部性的纯净实验：同样的元素、只改访问顺序 ==\n");
    std::vector<int> idxSeq(N), idxRand(N);
    std::iota(idxSeq.begin(), idxSeq.end(), 0);
    idxRand = idxSeq;
    std::mt19937 rng(42);
    std::shuffle(idxRand.begin(), idxRand.end(), rng);

    t0 = Clock::now();
    long long a1 = 0;
    for (int i : idxSeq) a1 += vec[i];
    double msSeq = ms_since(t0);

    t0 = Clock::now();
    long long a2 = 0;
    for (int i : idxRand) a2 += vec[i];
    double msRand = ms_since(t0);

    printf("  顺序访问 %d 个元素:   %7.1f ms\n", N, msSeq);
    printf("  乱序访问【同样的】元素: %7.1f ms（慢 %.1fx）\n", msRand, msRand / msSeq);
    printf("  结果一致: %s（读的是同一批数字，只是顺序不同）\n", a1 == a2 ? "true" : "false");
    printf("  → 指令数完全相同、内存总量完全相同——差距 100%% 来自缓存与硬件预取\n");
    printf("  → 第 49 章实测过它在索引上的形态（回表 vs 全表扫），这里是它最纯粹的样子\n");

    printf("\n== ③ 二维数组：行优先 vs 列优先（同样的 n²）==\n");
    const int M = 2000;
    std::vector<int> mat(static_cast<size_t>(M) * M, 1);
    t0 = Clock::now();
    long long r1 = 0;
    for (int i = 0; i < M; ++i)
        for (int j = 0; j < M; ++j) r1 += mat[static_cast<size_t>(i) * M + j];   // 行优先
    double msRow = ms_since(t0);

    t0 = Clock::now();
    long long r2 = 0;
    for (int j = 0; j < M; ++j)
        for (int i = 0; i < M; ++i) r2 += mat[static_cast<size_t>(i) * M + j];   // 列优先
    double msCol = ms_since(t0);

    printf("  行优先遍历 %dx%d: %7.1f ms\n", M, M, msRow);
    printf("  列优先遍历 %dx%d: %7.1f ms（慢 %.1fx）\n", M, M, msCol, msCol / msRow);
    printf("  结果一致: %s\n", r1 == r2 ? "true" : "false");
    printf("  → 只是交换了两个 for 循环的顺序——代码几乎一模一样，性能天差地别\n");
    printf("  → 这是「性能优化最反直觉」的完美例子: 你改的不是算法，是【内存访问模式】\n");

    printf("\n== ④ 结构体数组 vs 数组结构体（AoS vs SoA）==\n");
    struct Particle { float x, y, z; float mass; int id; char pad[44]; };  // 64 字节
    const int P = 1'000'000;
    std::vector<Particle> aos(P);
    for (int i = 0; i < P; ++i) { aos[i].x = 1.0f; aos[i].mass = 2.0f; }
    std::vector<float> soaX(P, 1.0f), soaMass(P, 2.0f);

    t0 = Clock::now();
    float sumA = 0;
    for (const auto& p : aos) sumA += p.x;                        // 只用 x，却拉回整个 64 字节
    double msAoS = ms_since(t0);

    t0 = Clock::now();
    float sumB = 0;
    for (float x : soaX) sumB += x;                                // 只拉 x 的那个数组
    double msSoA = ms_since(t0);

    printf("  AoS（结构体数组，每个 %zu 字节）只求和 x: %6.1f ms\n", sizeof(Particle), msAoS);
    printf("  SoA（分离的 x 数组）             求和 x: %6.1f ms（快 %.1fx）\n",
           msSoA, msAoS / std::max(msSoA, 1e-9));
    printf("  结果一致: %s\n", sumA == sumB ? "true" : "false");
    printf("  → 每条缓存行 64 字节: AoS 拉回 1 个 float 就占满一行，SoA 能装 16 个\n");
    printf("  → 游戏引擎/数值计算大量用 SoA，正是这个原因（数据导向设计）\n");

    printf("\n== ⑤ 分支预测：一个被编译器改写了的经典实验（诚实版）==\n");
    std::vector<int> data(N);
    for (int i = 0; i < N; ++i) data[i] = rng() % 256;
    std::vector<int> sorted_ = data;
    std::sort(sorted_.begin(), sorted_.end());

    // 版本 A: 让编译器自由发挥
    t0 = Clock::now();
    long long c1 = 0;
    for (int x : sorted_) if (x >= 128) c1 += x;
    double msSortedFree = ms_since(t0);
    t0 = Clock::now();
    long long c2 = 0;
    for (int x : data) if (x >= 128) c2 += x;
    double msUnsortedFree = ms_since(t0);

    // 版本 B: 用内存屏障【强制保留真实分支】（屏障只能在分支成立时执行，编译器无法条件移动）
    t0 = Clock::now();
    long long c3 = 0;
    for (int x : sorted_) if (x >= 128) { __asm__ volatile("" ::: "memory"); c3 += x; }
    double msSortedBr = ms_since(t0);
    t0 = Clock::now();
    long long c4 = 0;
    for (int x : data) if (x >= 128) { __asm__ volatile("" ::: "memory"); c4 += x; }
    double msUnsortedBr = ms_since(t0);

    printf("  版本 A（编译器自由优化）:\n");
    printf("    已排序 %6.1f ms   未排序 %6.1f ms   → 差距仅 %.1fx\n",
           msSortedFree, msUnsortedFree, msUnsortedFree / std::max(msSortedFree, 1e-9));
    printf("  版本 B（屏障强制保留分支）:\n");
    printf("    已排序 %6.1f ms   未排序 %6.1f ms   → 差距 %.1fx\n",
           msSortedBr, msUnsortedBr, msUnsortedBr / std::max(msSortedBr, 1e-9));
    printf("  结果全部一致: %s\n", (c1 == c2 && c2 == c3 && c3 == c4) ? "true" : "false");
#ifdef __OPTIMIZE__
    printf("  → 版本 A 的差距消失了，因为编译器把 if 编译成了【条件移动指令】(csel/cmov):\n");
    printf("     「两条路都算，按条件选结果」—— 根本没有分支，自然无所谓预测\n");
    printf("  → 版本 B 强制保留分支后，分支预测的代价才显现出来\n");
    printf("  ⚠️ 教训: 「为什么处理有序数组更快」这个 StackOverflow 最高票问题，\n");
    printf("     在今天的编译器 + 这个简单循环下【已经不成立了】\n");
    printf("  → 这正是本章的主题: 关于性能的常识会过期，唯一可靠的是【当场测量】\n");
#else
    printf("  → 当前是【-O0】: 编译器没做条件移动优化，所以版本 A 也保留了真实分支，\n");
    printf("     两个版本的差距因而接近——这不是分支预测不存在，而是【实验没被改写】\n");
    printf("  → 用 -O2 重新编译，你会看到版本 A 的差距【塌缩到 1x 左右】，版本 B 依旧显著:\n");
    printf("     那才是本节真正想展示的东西——【编译器把这个经典实验优化没了】\n");
    printf("  ⚠️ 这也说明: 同一段基准代码，在两个优化级别下能得出【完全相反的结论】\n");
#endif

    printf("\n== ⑥ 本章的第一条纪律：这些差距，profiler 都能直接指出来 ==\n");
    printf("  ① 顺序 vs 乱序（②）、行优先 vs 列优先（③）: perf stat 看 cache-misses\n");
    printf("  ② 分支预测（⑤）:                             perf stat 看 branch-misses\n");
    printf("  ③ 热点函数:                                  perf record / Instruments / VTune\n");
    printf("  → 上面五个实验没有一个能靠【读代码】看出来——它们全都要测\n");
    printf("  → 而一旦测出来，修复往往只是【换个循环顺序】或【换个数据结构】\n");
    (void)sink;
    return 0;
}
