// 测试：C++ 没有反射，测试怎么被「发现」？——静态注册器；以及属性测试如何抓住手挑用例漏掉的 bug。
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <numeric>
#include <string>
#include <vector>

// ---------- 30 行的测试框架（Catch2/GoogleTest 的骨架）----------
struct TestCase { const char* name; std::function<void()> fn; };

static std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;                  // 函数内静态，规避初始化顺序问题
    return tests;
}

struct Registrar {                                       // 全局对象的构造函数在 main 之前跑
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

struct AssertFail { std::string msg; };

#define TEST(name)                                                        \
    static void test_##name();                                            \
    static Registrar reg_##name(#name, test_##name);   /* ← 自动注册 */   \
    static void test_##name()

#define CHECK_EQ(expected, actual)                                        \
    do {                                                                  \
        auto e_ = (expected); auto a_ = (actual);                         \
        if (!(e_ == a_))                                                  \
            throw AssertFail{std::string(#actual) + ": 期望 " +           \
                             std::to_string(e_) + "，实际 " + std::to_string(a_)}; \
    } while (0)

static int run_all() {
    int passed = 0, failed = 0;
    for (const auto& t : registry()) {
        try {
            t.fn();
            printf("    ✓ %s\n", t.name);
            ++passed;
        } catch (const AssertFail& f) {
            printf("    ✗ %s —— %s\n", t.name, f.msg.c_str());
            ++failed;
        }
    }
    printf("    %d/%d 通过\n", passed, passed + failed);
    return failed;
}

// ============ 被测代码 ============

/// 二分查找——埋着那个在教科书里活了二十年的 bug: mid 用 (lo+hi)/2 会溢出
int buggy_binary_search(const std::vector<int>& v, int target) {
    int lo = 0, hi = (int)v.size() - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;                         // ⚠️ lo+hi 可能溢出 int
        if (v[(size_t)mid] == target) return mid;
        if (v[(size_t)mid] < target) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

/// 平均值——同一个溢出 bug 的直接形态
int buggy_average(int a, int b) { return (a + b) / 2; }  // ⚠️ a+b 溢出

// ============ 测试 ============

TEST(空数组找不到) { CHECK_EQ(-1, buggy_binary_search({}, 5)); }

TEST(能找到中间元素) {
    std::vector<int> v = {1, 3, 5, 7, 9};
    CHECK_EQ(2, buggy_binary_search(v, 5));
}

TEST(能找到两端) {
    std::vector<int> v = {1, 3, 5, 7, 9};
    CHECK_EQ(0, buggy_binary_search(v, 1));
    CHECK_EQ(4, buggy_binary_search(v, 9));
}

TEST(平均值_手挑的用例) {
    CHECK_EQ(5, buggy_average(4, 6));
    CHECK_EQ(0, buggy_average(-3, 3));
    CHECK_EQ(-5, buggy_average(-4, -6));
}

int main() {
    printf("== ① C++ 的测试发现：没有反射，靠【静态注册】==\n");
    printf("  Java 靠反射扫 @Test 注解（Java 版实测）；C++ 的类不知道自己有什么方法\n");
    printf("  → 解法: TEST 宏展开成一个【全局 Registrar 对象】，\n");
    printf("     它的构造函数在 main 之前运行，把测试函数塞进注册表\n");
    printf("  → Catch2 / GoogleTest 的 TEST_CASE / TEST 宏就是这么实现的\n");
    printf("  → 与第 51 章 ORM 同一个结论: 语言不给反射，就得靠宏/代码生成绕过去\n\n");
    printf("  跑注册表里的 %zu 个测试:\n", registry().size());
    run_all();
    printf("  → 手挑的用例【全部通过】——看起来代码没问题\n");

    printf("\n== ② 属性测试：不挑用例，验证【不变量】==\n");
    printf("  与其想「该测哪些输入」，不如声明「对任何输入都该成立的性质」:\n");
    printf("    性质: average(a, b) 必须落在 [min(a,b), max(a,b)] 区间内\n");
    printf("  随机生成 100000 组输入去轰炸:\n");
    srand(42);
    long tried = 0, caught = 0;
    int bad_a = 0, bad_b = 0, bad_result = 0;
    for (int i = 0; i < 100000; ++i) {
        int a = rand() - RAND_MAX / 2 + rand() % 65536;  // 铺满 int 范围的随机数
        int b = rand() - RAND_MAX / 2 + rand() % 65536;
        a = (rand() % 2) ? a : a * 2;                     // 有时放大到接近 INT_MAX/MIN
        b = (rand() % 2) ? b : b * 2;
        ++tried;
        int avg = buggy_average(a, b);
        if (avg < std::min(a, b) || avg > std::max(a, b)) {
            if (caught == 0) { bad_a = a; bad_b = b; bad_result = avg; }
            ++caught;
        }
    }
    printf("  %ld 组随机输入 → 违反不变量 %ld 次\n", tried, caught);
    printf("  第一个反例: average(%d, %d) = %d   ← 均值跑到了两数区间【之外】！\n",
           bad_a, bad_b, bad_result);
    printf("  → 根因: a+b 溢出 int（第 9 章的整数溢出，藏在最不起眼的地方）\n");
    printf("  → 手挑用例测的是【你想到的输入】，属性测试测的是【你没想到的】\n");
    printf("  → 同一个 bug 让二分查找在超大数组上出错——它在 JDK 里潜伏了九年（JDK-5045582）\n");

    printf("\n== ③ 「经典修法」也被属性测试抓住了 ==\n");
    auto classic_fix = [](int a, int b) { return a + (b - a) / 2; };   // 教科书上的修法
    srand(42);
    long ok1 = 0, bad1 = 0;
    int ca = 0, cb = 0, cr = 0;
    for (int i = 0; i < 100000; ++i) {
        int a = rand() - RAND_MAX / 2 + rand() % 65536;
        int b = rand() - RAND_MAX / 2 + rand() % 65536;
        a = (rand() % 2) ? a : a * 2;
        b = (rand() % 2) ? b : b * 2;
        int avg = classic_fix(a, b);
        if (avg >= std::min(a, b) && avg <= std::max(a, b)) ++ok1;
        else { if (bad1 == 0) { ca = a; cb = b; cr = avg; } ++bad1; }
    }
    printf("  经典修法 a + (b-a)/2: %ld 组通过，仍有 %ld 组违反不变量！\n", ok1, bad1);
    printf("  反例: average(%d, %d) = %d\n", ca, cb, cr);
    printf("  → 这个修法只对【同号】安全（二分查找里下标都非负，所以够用）\n");
    printf("     a 为大负数、b 为大正数时，b - a 照样溢出——修复自己也有 bug\n");

    printf("\n== ④ 真正的答案进了标准库: std::midpoint（C++20）==\n");
    srand(42);
    long ok2 = 0;
    for (int i = 0; i < 100000; ++i) {
        int a = rand() - RAND_MAX / 2 + rand() % 65536;
        int b = rand() - RAND_MAX / 2 + rand() % 65536;
        a = (rand() % 2) ? a : a * 2;
        b = (rand() % 2) ? b : b * 2;
        int avg = std::midpoint(a, b);
        if (avg >= std::min(a, b) && avg <= std::max(a, b)) ++ok2;
    }
    printf("  std::midpoint: 同样 100000 组输入，%ld 组全部满足不变量 ✓\n", ok2);
    printf("  → 「求两数中点」难到 2019 年才进标准库（P0811）——因为朴素写法和经典修法都错\n");
    printf("  → 属性测试的完整叙事: 抓出原始 bug → 抓出修复的 bug → 证明标准库版是对的\n");

    printf("\n== ⑤ 好用的不变量长什么样 ==\n");
    printf("  排序:   输出有序 + 长度不变 + 元素多重集相同（三条缺一不可）\n");
    printf("  编解码: decode(encode(x)) == x（往返性质——序列化/压缩/转义通用）\n");
    printf("  求逆:   apply(undo(op)) == 原状态（第 48 章 undo log 的正确性就是它）\n");
    printf("  幂等:   f(f(x)) == f(x)（去重、规范化、重试安全）\n");
    printf("  → 不变量比具体用例【便宜】: 一条性质顶几百个手写断言\n");

    printf("\n== ⑥ C++ 测试生态对号入座 ==\n");
    printf("  Catch2 / GoogleTest : TEST 宏 + 静态注册（本例手写的骨架）\n");
    printf("  RapidCheck          : 属性测试（本例 ② 的工业版，会自动【缩小】反例）\n");
    printf("  sanitizers          : -fsanitize=address,undefined —— 另一类「测试」:\n");
    printf("     UBSan 会直接在运行期抓到 ② 的有符号溢出（它是未定义行为！）\n");
    printf("  → C++ 的特殊之处: 有些 bug（UB）测试框架测不到，要交给编译器插桩\n");
    return 0;
}
