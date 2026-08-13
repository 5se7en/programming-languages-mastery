// 设计模式：C++ 的策略模式有三种写法，性能差几倍——模式的选择在这里是【性能决策】。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ============ 写法一：GoF 经典——虚函数（运行时多态）============
struct IScorer {
    virtual ~IScorer() = default;
    virtual int score(int x) const = 0;
};
struct DoubleScorer : IScorer {
    int score(int x) const override { return x * 2; }
};

// ============ 写法二：std::function（类型擦除，可运行时替换）============
using ScorerFn = std::function<int(int)>;

// ============ 写法三：模板（编译期多态，零开销）============
template <typename F>
struct TemplateContext {
    F f;
    explicit TemplateContext(F f) : f(std::move(f)) {}
    int run(int x) const { return f(x); }
};

// ============ ③ Meyers 单例（C++11 起线程安全）============
struct Config {
    static int constructed;
    int value = 42;
    Config() { ++constructed; }
    static Config& instance() {
        static Config c;            // C++11 保证【静态局部变量的初始化】是线程安全的
        return c;
    }
};
int Config::constructed = 0;

int main() {
    const int N = 50'000'000;
    volatile long sink = 0;

    printf("== ① 同一个策略，C++ 的三种写法 ==\n");
    printf("  写法一 虚函数:      IScorer* + override —— GoF 原版，运行时可换\n");
    printf("  写法二 std::function: 类型擦除 —— 能装 lambda/函数指针/仿函数，运行时可换\n");
    printf("  写法三 模板:        编译期确定 —— 不能运行时换，但可内联\n");

    printf("\n== ② 三种写法的性能实测（%d 次调用）==\n", N);
    auto scorer = std::make_unique<DoubleScorer>();
    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) sink += scorer->score(i);
    double msVirtual = ms_since(t0);

    ScorerFn fn = [](int x) { return x * 2; };
    t0 = Clock::now();
    for (int i = 0; i < N; ++i) sink += fn(i);
    double msFunction = ms_since(t0);

    auto tmpl = TemplateContext([](int x) { return x * 2; });
    t0 = Clock::now();
    for (int i = 0; i < N; ++i) sink += tmpl.run(i);
    double msTemplate = ms_since(t0);

    printf("  虚函数:        %7.1f ms（%.2f ns/次）\n", msVirtual, msVirtual * 1e6 / N);
    printf("  std::function: %7.1f ms（%.2f ns/次）\n", msFunction, msFunction * 1e6 / N);
    printf("  模板:          %7.1f ms（%.2f ns/次）\n", msTemplate, msTemplate * 1e6 / N);
    printf("  → 模板比虚函数快 %.1fx，比 std::function 快 %.1fx\n",
           msVirtual / std::max(msTemplate, 1e-9), msFunction / std::max(msTemplate, 1e-9));
    printf("  → 差距来自【能否内联】: 模板在编译期就知道调谁，虚函数和 std::function 都要间接跳转\n");
    printf("  → 在 Python/JS 里「策略模式 = 传函数」是零成本的选择；\n");
    printf("     在 C++ 里它是一个【性能决策】——这就是模式在不同语言里分量不同的原因\n");

    printf("\n== ③ 但也别过早优化：三者的适用场景 ==\n");
    printf("  虚函数:        策略在【运行时由配置/用户】决定，且调用不在热路径\n");
    printf("  std::function: 需要装各种可调用对象（lambda 捕获了状态时尤其方便）\n");
    printf("  模板:          策略在【编译期】就确定，且在热路径上（如排序比较器）\n");
    printf("  → std::sort 的比较器是模板参数，qsort 的是函数指针——这就是 std::sort 更快的原因之一\n");

    printf("\n== ④ 单例：Meyers 单例（C++11 起线程安全，实测）==\n");
    Config::constructed = 0;
    std::vector<std::thread> threads;
    std::vector<Config*> got(16);
    for (int i = 0; i < 16; ++i)
        threads.emplace_back([i, &got] { got[i] = &Config::instance(); });
    for (auto& t : threads) t.join();
    bool allSame = std::all_of(got.begin(), got.end(), [&](Config* p) { return p == got[0]; });
    printf("  16 个线程同时调 Config::instance(): 构造 %d 次，全部同一实例: %s\n",
           Config::constructed, allSame ? "true ✓" : "false ✗");
    printf("  → 关键是 `static Config c;` 这一行: C++11 起标准【要求】它的初始化线程安全\n");
    printf("     （编译器插入一次性的 guard 变量与同步——所谓「magic static」）\n");
    printf("  → 在 C++11 之前，这里也要写双重检查锁定，也踩过 Java 版 ② 同款的重排序坑\n");
    printf("  → 又一次印证: 【模式的复杂度会随语言进化被吸收进语言本身】\n");

    printf("\n== ⑤ C++ 里真正还需要的模式 ==\n");
    printf("  RAII（第 37 章）—— C++ 独有的、其他语言羡慕的「模式」，已内建为语言机制\n");
    printf("  Pimpl（第 53 章）—— 隔离 ABI 与编译依赖，是 C++ 特有问题的特有解法\n");
    printf("  CRTP —— 用模板实现静态多态（避免虚函数开销，② 的泛化）\n");
    printf("  类型擦除 —— std::function/std::any 的实现手法\n");
    printf("  → 注意这四个都【不在 GoF 书里】: 它们是 C++【自己的问题】催生的模式\n");
    printf("  → 而 GoF 里那些「模拟一等函数」的模式，在现代 C++ 里正在被 lambda 取代\n");

    printf("\n== ⑥ 模式的三种命运 ==\n");
    printf("  被语言吸收: 迭代器→范围 for、观察者→信号槽、策略→lambda、单例→magic static\n");
    printf("  仍然必要:   适配器、外观、仓储——它们解决的是【架构边界】而非语言缺陷\n");
    printf("  语言特有:   RAII、Pimpl、CRTP——由某个语言的独特约束催生\n");
    printf("  → 判断一个模式值不值得学: 它解决的是语言的问题，还是问题域的问题？\n");
    (void)sink;
    return 0;
}
