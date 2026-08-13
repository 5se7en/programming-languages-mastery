// 依赖注入：C++ 的两条路——运行时多态（虚函数，有开销）与编译期注入（模板，零开销）。
#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ============ 路线一：运行时多态（Java/C# 风格）============
struct IClock {
    virtual ~IClock() = default;
    virtual long now() const = 0;              // 虚函数 → 每次调用一次间接跳转（第 27 章 vtable）
};
struct SystemClock : IClock {
    long now() const override { return 1'700'000'000; }
};
struct FrozenClock : IClock {
    long t;
    explicit FrozenClock(long t) : t(t) {}
    long now() const override { return t; }
};

class RuntimeService {
    std::shared_ptr<IClock> clock_;            // 依赖以【接口指针】持有
public:
    explicit RuntimeService(std::shared_ptr<IClock> c) : clock_(std::move(c)) {}
    long stamp() const { return clock_->now(); }
};

// ============ 路线二：编译期注入（模板参数）============
struct SystemClockCT {                         // 不继承任何接口，只要有 now() 就行
    long now() const { return 1'700'000'000; }
};
struct FrozenClockCT {
    long t;
    explicit FrozenClockCT(long t) : t(t) {}
    long now() const { return t; }
};

template <typename ClockT>                     // 依赖是【类型参数】
class CompileTimeService {
    ClockT clock_;                             // 【值】持有——无指针、无虚表
public:
    explicit CompileTimeService(ClockT c) : clock_(std::move(c)) {}
    long stamp() const { return clock_.now(); }   // 编译期就知道调哪个函数 → 可内联
};

int main() {
    printf("== ① C++ 的第一条路：运行时多态（与 Java/C# 同构）==\n");
    auto svc = RuntimeService(std::make_shared<FrozenClock>(1'000'000));
    printf("  RuntimeService(make_shared<FrozenClock>(1000000)).stamp() = %ld\n", svc.stamp());
    printf("  → 依赖以【接口指针】持有，运行时可换任意实现——测试里塞 FrozenClock 即可\n");
    printf("  → 代价: 虚函数调用无法内联（第 27 章的 vtable 间接跳转）+ 堆分配（第 33 章）\n");

    printf("\n== ② C++ 的第二条路：编译期注入（模板参数）==\n");
    auto ct = CompileTimeService<FrozenClockCT>(FrozenClockCT{2'000'000});
    printf("  CompileTimeService<FrozenClockCT>{...}.stamp() = %ld\n", ct.stamp());
    printf("  → 依赖是【类型参数】，编译期确定 → 调用可内联，对象可栈上（第 32 章）\n");
    printf("  → 「换实现」发生在编译期: 生产用 CompileTimeService<SystemClockCT>，\n");
    printf("     测试用 CompileTimeService<FrozenClockCT> —— 两个【不同的类型】\n");

    printf("\n== ③ 两条路的性能对比（实测）==\n");
    const int N = 20'000'000;
    RuntimeService rt(std::make_shared<SystemClock>());
    CompileTimeService<SystemClockCT> cte(SystemClockCT{});

    volatile long sink = 0;
    auto t0 = Clock::now();
    for (int i = 0; i < N; ++i) sink += rt.stamp();
    double msRt = ms_since(t0);

    t0 = Clock::now();
    for (int i = 0; i < N; ++i) sink += cte.stamp();
    double msCt = ms_since(t0);

    printf("  运行时多态 %d 次: %7.1f ms（%.2f ns/次）\n", N, msRt, msRt * 1e6 / N);
    printf("  编译期注入 %d 次: %7.1f ms（%.2f ns/次）\n", N, msCt, msCt * 1e6 / N);
    printf("  → 编译期版快 %.1fx —— 差距全部来自「能不能内联」\n", msRt / msCt);
    printf("  → 这就是零开销哲学（第 29/38 章）在 DI 上的又一次体现: \n");
    printf("     能在编译期决定的事，绝不留到运行时付代价\n");

    printf("\n== ④ 但编译期注入有三个代价 ==\n");
    printf("  ① 类型爆炸: 每个依赖组合都是一个【新类型】——Service<A,B> 与 Service<A,C> 无关\n");
    printf("  ② 无法运行时切换: 读配置文件决定用哪个实现？做不到——那是运行时的事\n");
    printf("  ③ 编译变慢 + 报错难读: 模板实例化的代价（第 54 章实测头文件展开 8286x）\n");
    printf("  → 所以真实项目常常【混用】: 稳定的高频依赖用模板，可配置的用虚函数\n");

    printf("\n== ⑤ C++ 为什么几乎没有 DI 容器 ==\n");
    printf("  与第 51 章 ORM 完全同一个原因: 【没有运行时反射】\n");
    printf("  → Java 容器能 getDeclaredConstructors() 问类要构造器（Java 版实测）\n");
    printf("  → C++ 的类不知道自己有哪些构造器、参数是什么类型 —— 容器无从下手\n");
    printf("  → 于是 C++ 的答案是【手工组装根】: 在 main() 里把对象图一层层 new 出来\n");
    printf("     这正是 Java 版 ③ 说的 Poor Man's DI —— 对 C++ 而言它不是「穷人版」，是唯一版\n");
    printf("  → 少数库（Boost.DI）用模板元编程模拟自动装配，代价是编译期魔法与报错地狱\n");

    printf("\n== ⑥ 手工组装根长什么样（C++ 的标准实践）==\n");
    printf("  int main() {\n");
    printf("      auto clock  = std::make_shared<SystemClock>();      // 组装根: 唯一 new 的地方\n");
    printf("      auto repo   = std::make_shared<SqlRepo>(dbConn);\n");
    printf("      auto service = UserService(clock, repo);            // 依赖层层注入\n");
    printf("      return App(service).run();\n");
    printf("  }\n");
    printf("  → 与 JS 版 ④ 的组装根、Java 版 ③ 的手工注入完全同构\n");
    printf("  → 判断标准也一样: 全项目搜 make_shared/new，应该只集中在 main 附近\n");
    (void)sink;
    return 0;
}
