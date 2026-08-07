// 第 26 章 · 继承 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o inh && ./inh
// C++ 支持完整多继承，也因此必须直面菱形问题

#include <iostream>
#include <memory>
#include <string>
#include <vector>

// ---------- ① 菱形继承：不用虚继承 ----------
struct A     { int value = 42; };
struct B : A {};
struct C : A {};
struct D : B, C {};              // A 被继承两次

// ---------- ② 菱形继承：用虚继承 ----------
struct VA      { int value = 42; };
struct VB : virtual VA {};
struct VC : virtual VA {};
struct VD : VB, VC {};           // 虚继承让 VA 只保留一份

// ---------- ③ 虚析构函数：不加会内存泄漏 ----------
class BadBase {
public:
    ~BadBase() { std::cout << "    ~BadBase() 执行\n"; }   // ✗ 非虚析构
};
class BadDerived : public BadBase {
public:
    ~BadDerived() { std::cout << "    ~BadDerived() 执行\n"; }
};

class GoodBase {
public:
    virtual ~GoodBase() { std::cout << "    ~GoodBase() 执行\n"; }   // ✓ 虚析构
};
class GoodDerived : public GoodBase {
public:
    ~GoodDerived() override { std::cout << "    ~GoodDerived() 执行\n"; }
};

// ---------- ④ 正常的继承 ----------
class Animal {
protected:
    std::string name;
public:
    explicit Animal(std::string n) : name(std::move(n)) {}
    virtual ~Animal() = default;                  // 打算被继承 → 必须虚析构
    virtual std::string speak() const { return name + " 发出声音"; }
};

class Dog : public Animal {
public:
    explicit Dog(std::string n) : Animal(std::move(n)) {}
    std::string speak() const override {
        return Animal::speak() + "：汪！";          // C++ 用 Base::method() 调父类
    }
};

// ---------- ⑤ private 继承 ≈ 组合 ----------
class Engine {
public:
    void start() { std::cout << "    引擎启动\n"; }
};

class CarByPrivateInherit : private Engine {      // 「用 Engine 实现」而非「是一个 Engine」
public:
    void drive() { start(); }
};

class CarByComposition {                           // 现代 C++ 更推荐这样写，更清晰
    Engine engine;
public:
    void drive() { engine.start(); }
};

int main() {
    std::cout << "=== 1. ⚠️ 菱形继承：A 的字段真的存在两份 ===\n";
    {
        D d;
        // std::cout << d.value;    // ✗ 编译错误：ambiguous —— 是 B::value 还是 C::value？
        std::cout << "  d.value             → 编译错误 (ambiguous)，必须显式指定路径\n";
        std::cout << "  d.B::value = " << d.B::value << "   d.C::value = " << d.C::value << "\n";

        d.B::value = 1;
        d.C::value = 2;
        std::cout << "  分别赋值后: B::value=" << d.B::value
                  << "  C::value=" << d.C::value << "  ← 真的是两个独立的字段！\n";
        std::cout << "  sizeof(D) = " << sizeof(D) << " 字节（两个 int）\n";
    }

    std::cout << "\n=== 2. 虚继承让基类只保留一份 ===\n";
    {
        VD vd;
        vd.value = 99;                              // ✓ 没有歧义
        std::cout << "  struct VB : virtual VA {};  struct VC : virtual VA {};\n";
        std::cout << "  vd.value = " << vd.value << "  ← 无歧义，只有一份\n";
        std::cout << "  sizeof(VD) = " << sizeof(VD) << " 字节  ← 代价：多了虚基类指针\n";
        std::cout << "  → 从 " << sizeof(D) << " 涨到 " << sizeof(VD)
                  << " 字节，并且访问基类成员要多一次间接寻址\n";
    }

    std::cout << "\n=== 3. 各语言对菱形问题的三种答案 ===\n";
    std::cout << "  虚继承      C++          显式写 virtual，共同基类只保留一份\n";
    std::cout << "  MRO 线性化  Python       把继承图算成一条线性顺序，按序查找\n";
    std::cout << "  禁止多继承  Java/C#/JS   只能继承一个类，多实现用接口（第 28 章）\n";
    std::cout << "  → 菱形问题的根源是「状态（字段）被继承多份」\n";
    std::cout << "     接口不含状态，所以「多实现接口」是安全的\n";

    std::cout << "\n=== 4. ⚠️ 虚析构函数：不加会内存泄漏 ===\n";
    std::cout << "  非虚析构 —— delete 基类指针:\n";
    {
        BadBase* p = new BadDerived();
        delete p;                                   // ⚠️ 只调用 ~BadBase()
        std::cout << "    ⚠️ ~BadDerived() 根本没执行！子类持有的资源全部泄漏\n";
    }
    std::cout << "\n  虚析构 —— delete 基类指针:\n";
    {
        GoodBase* p = new GoodDerived();
        delete p;                                   // ✓ 先 ~GoodDerived() 再 ~GoodBase()
        std::cout << "    ✓ 两个析构函数都正确执行了\n";
    }
    std::cout << "  → 规则：任何打算被继承的类，析构函数必须是 virtual\n";
    std::cout << "  → 这是 C++ 继承里最容易造成实际损失的一个坑\n";

    std::cout << "\n=== 5. 正常的继承：is-a 成立 ===\n";
    {
        std::unique_ptr<Animal> a = std::make_unique<Dog>("旺财");
        std::cout << "  Animal* 指向 Dog 对象，调用 speak():\n";
        std::cout << "    " << a->speak() << "\n";
        std::cout << "  → C++ 用 Base::method() 调用父类实现（不是 super）\n";
    }

    std::cout << "\n=== 6. 三种继承方式 ===\n";
    std::cout << "  class D1 : public B    is-a：B 的 public 成员在 D1 中仍是 public\n";
    std::cout << "  class D2 : protected B 罕见\n";
    std::cout << "  class D3 : private B   「用 B 实现」而非「是一个 B」—— 本质是组合\n";
    {
        CarByPrivateInherit c1;
        CarByComposition c2;
        std::cout << "\n  private 继承版本 drive():\n";
        c1.drive();
        std::cout << "  组合版本 drive():\n";
        c2.drive();
        std::cout << "  → 两者语义等价，但组合更清晰，现代 C++ 推荐组合\n";
    }

    std::cout << "\n=== 7. C++ 继承的内存布局（呼应第 24 章）===\n";
    std::cout << "  普通继承 = 把父类对象「内嵌」进子类对象\n";
    std::cout << "    sizeof(A) = " << sizeof(A) << "  sizeof(B) = " << sizeof(B)
              << "  sizeof(D) = " << sizeof(D) << "\n";
    std::cout << "  → 所以菱形继承下 A 的字段真的有两份\n";
    std::cout << "  → 这不是缺陷，而是「零开销内嵌布局」的必然结果\n";
    std::cout << "  → 虚继承为了消除重复引入了间接层，也就付出了空间和速度的代价\n";

    std::cout << "\n=== 8. 小结 ===\n";
    std::cout << "  · C++ 支持完整多继承，代价是必须处理菱形问题\n";
    std::cout << "  · 实测：普通菱形 sizeof=" << sizeof(D) << "（两份），虚继承 sizeof="
              << sizeof(VD) << "（一份+指针）\n";
    std::cout << "  · 打算被继承的类，析构函数必须 virtual —— 否则子类析构被跳过\n";
    std::cout << "  · private 继承语义等价于组合，但组合写法更清晰\n";
    return 0;
}
