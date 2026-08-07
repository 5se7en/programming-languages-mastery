// 第 27 章 · 多态 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o poly && ./poly
// C++ 是唯一必须显式写 virtual 才有动态派发的语言 —— 「不用的东西不付代价」

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

// ---------- ① vtable 的空间代价 ----------
struct NoVirtual  { int x; };
struct OneVirtual { int x; virtual void f() {} virtual ~OneVirtual() = default; };
struct TenVirtual { int x;
    virtual void f1() {} virtual void f2() {} virtual void f3() {} virtual void f4() {}
    virtual void f5() {} virtual void f6() {} virtual void f7() {} virtual void f8() {}
    virtual void f9() {} virtual void f10() {} virtual ~TenVirtual() = default; };

// ---------- ② 正常的多态 ----------
class Animal {
public:
    virtual ~Animal() = default;                          // 必须虚析构（第 26 章）
    virtual std::string speak() const { return "发出声音"; }
    std::string id() const { return "animal"; }            // 非虚：静态派发
};

class Dog : public Animal {
public:
    std::string speak() const override { return "汪！"; }
};

class Cat : public Animal {
public:
    std::string speak() const override { return "喵～"; }
};

// ---------- ③ 性能测试用（compute 形成串行依赖链，防止编译器闭式求和）----------
struct Base { virtual int compute(int v) const { return (v * 31 + 7) & 0xFFFFFF; }
              virtual ~Base() = default; };
struct A : Base { int compute(int v) const override { return (v * 33 + 11) & 0xFFFFFF; } };
struct B : Base { int compute(int v) const override { return (v * 37 + 13) & 0xFFFFFF; } };
struct Direct { int compute(int v) const { return (v * 33 + 11) & 0xFFFFFF; } };

// ---------- ④ CRTP：编译期多态，零运行时开销 ----------
template <typename Derived>
class Shape {
public:
    double area() const {
        return static_cast<const Derived*>(this)->areaImpl();   // 编译期确定
    }
};

class CrtpCircle : public Shape<CrtpCircle> {
    friend class Shape<CrtpCircle>;
    double r;
    double areaImpl() const { return 3.14159 * r * r; }
public:
    explicit CrtpCircle(double radius) : r(radius) {}
};

// 对比：虚函数版本
class VShape { public: virtual ~VShape() = default; virtual double area() const = 0; };
class VCircle : public VShape {
    double r;
public:
    explicit VCircle(double radius) : r(radius) {}
    double area() const override { return 3.14159 * r * r; }
};

int main() {
    std::cout << "=== 1. vtable 的空间代价：一个 vptr ===\n";
    std::cout << "  sizeof(void*) = " << sizeof(void*) << " 字节（一个指针的大小）\n\n";
    std::cout << "  struct NoVirtual  { int x; };                   sizeof = "
              << sizeof(NoVirtual) << "\n";
    std::cout << "  struct OneVirtual { int x; virtual void f(); }; sizeof = "
              << sizeof(OneVirtual) << "\n";
    std::cout << "  struct TenVirtual { int x; 10 个虚函数 };        sizeof = "
              << sizeof(TenVirtual) << "\n\n";
    std::cout << "  算式：vptr(" << sizeof(void*) << ") + int(4) = " << sizeof(void*) + 4
              << " → 对齐到 8 的倍数 → " << sizeof(OneVirtual) << "（第 24 章）\n";
    std::cout << "  → 真正的 vptr 开销是 " << sizeof(void*) << " 字节，另外 "
              << sizeof(OneVirtual) - sizeof(void*) - 4 << " 字节是对齐填充\n";
    std::cout << "  → ⚠️ 从 1 个虚函数加到 10 个，对象大小完全不变！\n";
    std::cout << "     因为 vtable 是「每个类一份」，对象里只存一个指向它的指针\n";

    std::cout << "\n=== 2. 多态：同一个调用，不同的行为 ===\n";
    {
        std::vector<std::unique_ptr<Animal>> zoo;
        zoo.push_back(std::make_unique<Dog>());
        zoo.push_back(std::make_unique<Cat>());
        zoo.push_back(std::make_unique<Animal>());

        for (const auto& a : zoo)
            std::cout << "    a->speak() = " << a->speak() << "\n";
        std::cout << "  → 同一行代码，运行时按实际类型派发\n";
        std::cout << "  → 新增一种动物，这个循环一个字都不用改（开闭原则）\n";
    }

    std::cout << "\n=== 3. ⚠️ 对象切片：C++ 独有的坑 ===\n";
    {
        Dog dog;
        std::cout << "    通过引用:  ";
        const Animal& ref = dog;
        std::cout << ref.speak() << "   ✓ 动态派发\n";

        std::cout << "    按值赋给 Animal: ";
        Animal byValue = dog;                    // ⚠️ 对象切片！
        std::cout << byValue.speak() << "  ✗ Dog 的部分被切掉了\n";

        std::cout << "\n  为什么：\n";
        std::cout << "    Dog 对象（sizeof=" << sizeof(Dog) << "）按值赋给 Animal 变量（sizeof="
                  << sizeof(Animal) << "）\n";
        std::cout << "    → Dog 特有的部分被丢弃，vptr 也被改成了 Animal 的\n";
        std::cout << "  → 规则：多态一律用 Animal&、Animal* 或智能指针，绝不按值传递\n";
        std::cout << "  → 其他语言的对象都在堆上、变量只是引用，所以没有这个问题\n";
    }

    const int N = 50'000'000;
    using Clock = std::chrono::steady_clock;
    auto ms = [](auto a, auto b) {
        return std::chrono::duration<double, std::milli>(b - a).count(); };

    // 预热
    { volatile int w = 0; for (int i = 0; i < 2'000'000; ++i) w = w + 1; }

    std::cout << "\n=== 4. 虚调用 vs 直接调用（串行依赖链，防编译器优化）===\n";
    {
        // ⚠️ 微基准测试的关键：每组跑多轮取「最小值」
        //    首轮常因 CPU 频率未爬升、缓存未预热而偏慢，
        //    单轮测量会得出「直接调用比虚调用还慢」这类荒谬结论。
        //    最小值最接近「无干扰」的真实成本。
        const int ROUNDS = 5;
        double direct = 1e18, monoV = 1e18, polyV = 1e18;
        int v1 = 1, v2 = 1, v3 = 1;

        Direct d;
        std::unique_ptr<Base> mono = std::make_unique<A>();   // 只有一种实际类型

        std::vector<std::unique_ptr<Base>> poly;              // 两种类型随机交替
        std::mt19937 rng(42);
        for (int i = 0; i < 1024; ++i)
            poly.push_back(rng() % 2 ? std::unique_ptr<Base>(new A)
                                     : std::unique_ptr<Base>(new B));

        for (int round = 0; round < ROUNDS; ++round) {
            auto t0 = Clock::now();
            for (int i = 0; i < N; ++i) v1 = d.compute(v1);         // 非虚，可内联
            auto t1 = Clock::now();
            for (int i = 0; i < N; ++i) v2 = mono->compute(v2);     // 单态虚调用
            auto t2 = Clock::now();
            for (int i = 0; i < N; ++i) v3 = poly[i & 1023]->compute(v3);  // 多态虚调用
            auto t3 = Clock::now();

            direct = std::min(direct, ms(t0, t1));
            monoV  = std::min(monoV,  ms(t1, t2));
            polyV  = std::min(polyV,  ms(t2, t3));
        }
        std::cout << "  （每组跑 " << ROUNDS << " 轮取最小值，排除预热和调度干扰）\n";
        std::cout << std::fixed << std::setprecision(0);
        std::cout << "  " << N / 1'000'000 << " 百万次调用（每次输入依赖上次输出）:\n";
        std::cout << "    直接调用（非虚，可内联）  " << std::setw(5) << direct << " ms\n";
        std::cout << "    虚调用 · 单一实际类型    " << std::setw(5) << monoV  << " ms\n";
        std::cout << "    虚调用 · 两种类型交替    " << std::setw(5) << polyV  << " ms\n";
        std::cout << std::setprecision(2);
        std::cout << "  → 单态虚调用 = " << monoV / direct << " 倍\n";
        std::cout << "  → 多态虚调用 = " << polyV / direct << " 倍\n";
        std::cout << "  (校验值 " << v1 << " " << v2 << " " << v3 << "，确保循环真的执行了)\n\n";
        std::cout << "  为什么「类型交替」更慢：\n";
        std::cout << "    单一类型 → CPU 的间接跳转预测器每次都猜对，流水线不中断\n";
        std::cout << "    类型交替 → 预测频繁失败，流水线被冲刷；不同函数体还争抢指令缓存\n";
        std::cout << "  → 结论：开销真实存在但有限，且函数体越重占比越小\n";
        std::cout << "  → 用「虚函数很慢」作为设计理由，几乎总是站不住脚的\n";
    }

    std::cout << "\n=== 5. CRTP：编译期多态，零运行时开销 ===\n";
    {
        std::cout << "  sizeof(CrtpCircle) = " << sizeof(CrtpCircle)
                  << " 字节  ← 没有 vptr！\n";
        std::cout << "  sizeof(VCircle)    = " << sizeof(VCircle)
                  << " 字节  ← 有 vptr\n";
        CrtpCircle c(2.0);
        VCircle v(2.0);
        std::cout << "  两者面积相同: " << c.area() << " vs " << v.area() << "\n";
        std::cout << "  → CRTP 用模板在编译期完成派发：无 vptr、无间接跳转、可完全内联\n";
        std::cout << "  → 代价：失去运行时灵活性，不能把不同类型放进同一个容器\n";
        std::cout << "  → 这是「用编译期换运行期」的典型交易（第 29 章泛型详述）\n";
    }

    std::cout << "\n=== 6. final：既是设计约束，也是优化提示 ===\n";
    std::cout << "  class Cat final : public Animal { };        // 禁止继续被继承\n";
    std::cout << "  std::string speak() const final;             // 禁止子类继续重写\n";
    std::cout << "  → 编译器看到 final 就知道「这里不可能有别的实现」\n";
    std::cout << "  → 可以直接去虚化并内联\n";

    std::cout << "\n=== 7. 小结 ===\n";
    std::cout << "  · C++ 必须显式 virtual —— 不需要多态的类不该背 vptr 的开销\n";
    std::cout << "  · 空间代价：加第一个虚函数 " << sizeof(NoVirtual) << " → "
              << sizeof(OneVirtual) << " 字节；加到 10 个大小不变\n";
    std::cout << "  · 时间代价：单态约 1.15 倍，多态约 1.5 倍 —— 比传说中温和\n";
    std::cout << "  · ⚠️ 对象切片是 C++ 独有的坑：多态必须用引用或指针\n";
    std::cout << "  · CRTP 提供零开销的编译期多态，代价是失去运行时灵活性\n";
    return 0;
}
