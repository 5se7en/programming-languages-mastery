#include <array>
#include <chrono>
#include <concepts>
#include <iostream>
#include <numeric>
#include <string>
#include <typeinfo>
#include <vector>

// 类模板：编译期为每个 T 生成一份独立代码（单态化）
template <typename T>
class Box {
public:
    inline static int count = 0;     // 静态成员：每个实例化各一份
    explicit Box(T v) : value_(std::move(v)) { ++count; }
    const T& get() const { return value_; }

private:
    T value_;
};

// 函数模板
template <typename T>
T max_of(T a, T b) { return a > b ? a : b; }

// C++20 concept：给模板参数立契约
template <typename T>
concept Addable = requires(T a, T b) {
    { a + b } -> std::convertible_to<T>;
};

template <Addable T>
T sum(T a, T b) { return a + b; }

int main() {
    std::cout << std::boolalpha;

    std::cout << "== ① 单态化：每个实例化都是独立的类型 ==\n";
    std::cout << "typeid(Box<int>)         = " << typeid(Box<int>).name() << "\n";
    std::cout << "typeid(Box<std::string>) = " << typeid(Box<std::string>).name() << "\n";

    Box<int> b1(90);
    Box<int> b2(85);
    Box<std::string> b3("小明");
    std::cout << "Box<int>::count = " << Box<int>::count
              << ", Box<std::string>::count = " << Box<std::string>::count
              << "   <- 静态成员各一份\n";

    std::cout << "\n== ② 函数模板：真的生成了两个函数 ==\n";
    auto* p1 = reinterpret_cast<void*>(static_cast<int (*)(int, int)>(&max_of<int>));
    auto* p2 = reinterpret_cast<void*>(static_cast<double (*)(double, double)>(&max_of<double>));
    std::cout << "&max_of<int> == &max_of<double>: " << (p1 == p2) << "\n";

    std::cout << "\n== ③ concept：编译期契约 ==\n";
    std::cout << "sum(90, 8) = " << sum(90, 8) << "\n";
    std::cout << "sum(\"小\"s, \"明\"s) = " << sum(std::string("小"), std::string("明")) << "\n";
    std::cout << "Addable<int> = " << Addable<int>
              << ", Addable<std::vector<int>> = " << Addable<std::vector<int>> << "\n";
    // sum(std::vector<int>{}, std::vector<int>{});   // ✗ 编译错误：constraints not satisfied

    std::cout << "\n== ④ 非类型模板参数：值也能当参数 ==\n";
    std::array<int, 5> a5{};
    std::array<int, 8> a8{};
    std::cout << "std::array<int,5> 与 std::array<int,8> 是同一类型: "
              << (typeid(a5) == typeid(a8)) << "\n";

    std::cout << "\n== ⑤ vector<bool>：特化的经典陷阱 ==\n";
    std::vector<bool> vb(8, true);
    std::cout << "vb[0] 的类型是 bool&: " << std::is_same_v<decltype(vb[0]), bool&>
              << "   <- 拿到的是代理对象，不是引用\n";

    std::cout << "\n== ⑥ 零开销验证：vector<int> 求和（1000 万元素） ==\n";
    constexpr int n = 10'000'000;
    std::vector<int> v(n);
    std::iota(v.begin(), v.end(), 0);
    volatile long long keep = 0;
    for (int r = 0; r < 3; ++r) {                     // 预热
        long long warm = 0;
        for (int x : v) warm += x;
        keep = warm;
    }
    auto t0 = std::chrono::steady_clock::now();
    long long total = 0;
    for (int x : v) total += x;
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "vector<int> 求和: "
              << std::chrono::duration<double, std::milli>(t1 - t0).count()
              << " ms（结果 " << total << "）\n";
    (void)keep;
    return 0;
}
