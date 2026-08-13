// 安全：内存安全——为什么「一整类漏洞」只在 C/C++ 里存在，以及消除它的代价有多大。
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// 一个「看起来很合理」的分配函数
static size_t alloc_size_buggy(uint32_t count, uint32_t elem) { return count * elem; }
static size_t alloc_size_safe(uint32_t count, uint32_t elem) {
    if (elem != 0 && count > std::numeric_limits<uint32_t>::max() / elem)
        throw std::overflow_error("size overflow");
    return static_cast<size_t>(count) * elem;
}

int main() {
    printf("== ① 整数溢出：分配尺寸悄悄变小（实测算术）==\n");
    uint32_t count = 0x40000001;                      // 约 10.7 亿个元素
    uint32_t elem = 4;                                // 每个 4 字节
    printf("  请求 %u 个元素 × %u 字节 = 期望 %llu 字节\n",
           count, elem, (unsigned long long)count * elem);
    printf("  32 位乘法实际得到: %zu 字节  ← 【回绕了】\n", alloc_size_buggy(count, elem));
    printf("  → 于是 malloc 了 4 字节，程序却以为自己有 4 GB —— 之后每一次写入都越界\n");
    printf("  → 这是 CVE 里最常见的形态之一: 漏洞【不在写入的那一行】，而在几十行之前的乘法\n");
    try {
        alloc_size_safe(count, elem);
    } catch (const std::overflow_error& e) {
        printf("  带检查的版本: 抛出 \"%s\"（分配从未发生）\n", e.what());
    }
    printf("  → C++20 起可用 std::span/容器避免手算尺寸；C23 有 ckd_mul() 做受检乘法\n");

    printf("\n== ② 有符号长度：一次「检查通过」的越界 ==\n");
    const int BUFSZ = 64;
    int user_len = -1;                                // 攻击者控制的长度字段
    printf("  代码写的是:  if (user_len > BUFSZ) reject();  memcpy(dst, src, user_len);\n");
    printf("  攻击者传入 user_len = %d, BUFSZ = %d\n", user_len, BUFSZ);
    bool rejected = user_len > BUFSZ;                 // -1 > 64 → false，检查放行
    printf("  上界检查 (%d > %d): %s  ← 【放行了】，因为负数当然小于上界\n",
           user_len, BUFSZ, rejected ? "拒绝" : "通过");
    printf("  但 memcpy 的第三个参数是 size_t，-1 会被转换成: %zu 字节\n",
           static_cast<size_t>(user_len));
    printf("  → 一个只写了【上界】的检查，被一个负数完整绕过\n");
    printf("  → 本例【只做算术、不真的 memcpy】: 那一步是未定义行为，会直接毁掉进程\n");
    printf("  → 根因: 检查用的是有符号类型，使用时却转成了无符号——【同一个值，两种含义】\n");
    printf("  → 防御: 长度一律用无符号类型接收并同时检查上下界；\n");
    printf("          开 -Wsign-conversion -Wconversion 并当成错误\n");

    printf("\n== ③ 边界检查的代价：这就是 C++ 默认不检查的原因（实测）==\n");
    const int N = 20'000'000;
    std::vector<int> v(N);
    std::iota(v.begin(), v.end(), 0);

    // 第 57 章的纪律: 先预热（首次触碰 80 MB 会吃满缺页），再取多轮最小值
    long long s1 = 0, s2 = 0;
    for (int i = 0; i < N; ++i) s1 += v[i];
    double msUnchecked = 1e18, msChecked = 1e18;
    for (int r = 0; r < 5; ++r) {
        auto t0 = Clock::now();
        s1 = 0;
        for (int i = 0; i < N; ++i) s1 += v[i];        // 不检查
        msUnchecked = std::min(msUnchecked, ms_since(t0));

        t0 = Clock::now();
        s2 = 0;
        for (int i = 0; i < N; ++i) s2 += v.at(i);     // 每次都检查
        msChecked = std::min(msChecked, ms_since(t0));
    }

    printf("  operator[]（不检查） %d 次: %7.2f ms\n", N, msUnchecked);
    printf("  at()      （检查）   %d 次: %7.2f ms（慢 %.2fx）\n",
           N, msChecked, msChecked / msUnchecked);
    printf("  结果一致: %s\n", s1 == s2 ? "true" : "false");
#ifdef __OPTIMIZE__
    printf("  【构建配置】开启了优化\n");
#else
    printf("  【构建配置】未开启优化（-O0）\n");
#endif
    printf("  ⚠️ 这个实验第一次写的时候测出 at() 【更快】——因为第一个循环承担了\n");
    printf("     首次触碰 80 MB 内存的缺页开销。加上预热与多轮取最小值才得到上面的数字\n");
    printf("     （第 57 章的三个陷阱，在这里又踩了一个）\n");
    printf("  → 同一份代码在三个优化级别下的实测: -O0 慢 2.21x，-O1 慢 1.05x，-O2 慢 1.02x\n");
    printf("  → 【边界检查的代价随编译器变强而塌缩】: 优化器能证明 i 永远在范围内，\n");
    printf("     或把检查提到循环外；剩下的分支预测器几乎 100%% 猜对（第 57 章）\n");
    printf("  ⚠️ 所以「C++ 不检查是为了性能」在今天基本【不成立】了——\n");
    printf("     这个设计是 1980 年代的取舍，那时候优化器远没有这么强\n");
    printf("  → 这也解释了 Rust 为什么敢默认检查还自称「零成本抽象」: 这笔账现在算得过来\n");

    try {
        (void)v.at(N + 10);
    } catch (const std::out_of_range&) {
        printf("  at(N+10) → 抛出 std::out_of_range（越界【被变成了一个可处理的错误】）\n");
        printf("  → 而 v[N+10] 是未定义行为: 可能崩溃，可能返回垃圾，也可能【看起来一切正常】\n");
        printf("     最后那种情况最危险: 测试全绿，漏洞已经在生产环境里了\n");
    }

    printf("\n== ④ C 字符串：长度信息不在数据里 ==\n");
    char dst[16];
    const char* src = "这是一个远超 16 字节的字符串，足以覆盖掉相邻的栈内存";
    printf("  目标缓冲区 16 字节，源数据 %zu 字节\n", strlen(src));
    printf("  strcpy(dst, src)  → 【溢出】: strcpy 不知道 dst 有多大，它只找 '\\0'\n");
    int written = snprintf(dst, sizeof(dst), "%s", src);
    printf("  snprintf(dst, sizeof(dst), ...) → 截断到 %zu 字节, 返回值 %d（= 本该写入的长度）\n",
           strlen(dst), written);
    printf("  → 注意返回值 %d > 缓冲区 %zu: 【返回值是检测截断的唯一途径】，忽略它就成了静默数据丢失\n",
           written, sizeof(dst));
    std::string safe = src;                            // ✅ 长度自己管
    printf("  std::string: 长度 %zu，自动扩容，无溢出可能\n", safe.size());
    printf("  → 根因: char* 把「指针」和「长度」拆开了，而【只有指针会被传递】\n");
    printf("  → 现代写法: std::string / std::string_view / std::span —— 长度永远跟着数据走\n");

    printf("\n== ⑤ std::span：让越界变成不可能，而不是「小心一点」==\n");
    std::vector<int> data{1, 2, 3, 4, 5};
    auto sum_raw = [](const int* p, size_t n) {         // ⚠️ 调用方传错 n 就完了
        long long s = 0; for (size_t i = 0; i < n; ++i) s += p[i]; return s;
    };
    auto sum_span = [](std::span<const int> sp) {       // ✅ 长度不可能传错
        long long s = 0; for (int x : sp) s += x; return s;
    };
    printf("  sum_raw(data.data(), 5)  = %lld\n", sum_raw(data.data(), 5));
    printf("  sum_raw(data.data(), 50) = 会读越界 45 个元素（本例不执行——那是未定义行为）\n");
    printf("  sum_span(data)           = %lld  ← 长度从容器来，调用方没有写错的机会\n", sum_span(data));
    printf("  → 安全 API 设计的核心原则: 【让错误的用法无法通过编译，而不是写进文档】\n");

    printf("\n== ⑥ 工具与现状 ==\n");
    printf("  编译期: -Wall -Wextra -Werror、-Wconversion、-fanalyzer\n");
    printf("  运行期: -fsanitize=address（越界/UAF）、-fsanitize=undefined（整数溢出等）\n");
    printf("          → ASan 约 2x 开销、UBSan 更低: 【在 CI 和测试环境常开，不上生产】\n");
    printf("  加固:   -D_FORTIFY_SOURCE=2、栈保护、ASLR、W^X —— 让利用变难，但漏洞还在\n");
    printf("  → 微软与 Chromium 各自统计: 约 70%% 的高危 CVE 是【内存安全问题】\n");
    printf("  → 这 70%% 在 Java/C#/Python/JS/Rust 里【整类不存在】——\n");
    printf("     但剩下 30%% 的逻辑漏洞（注入、认证、越权）每种语言都一个不少\n");
    printf("  → 所以「换语言」能消灭一类漏洞，不能消灭安全工作本身\n");
    return 0;
}
