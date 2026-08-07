// 第 25 章 · 封装 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o enc && ./enc
// C++ 独有 friend：与其放宽访问级别，不如精确列出例外

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// ---------- 封装良好的类 ----------
class Account {
private:
    int balance = 100;               // 只有本类和 friend 能访问

protected:
    int forSubclass = 0;             // 本类 + 派生类

public:
    int getBalance() const { return balance; }

    // ✅ 有业务含义的操作，而不是裸 setter
    void deposit(int n) {
        if (n <= 0) throw std::invalid_argument("金额必须为正");
        balance += n;
    }

    void withdraw(int n) {
        if (n > balance) throw std::runtime_error("余额不足");
        balance -= n;                 // 唯一的修改入口，不变式得到保证
    }

    // ⚠️ C++ 独有：开一个合法后门
    friend class Auditor;
    friend void debugPrint(const Account&);
};

// friend 类可以合法访问私有成员
class Auditor {
public:
    static int peek(const Account& a) { return a.balance; }
};

void debugPrint(const Account& a) {
    std::cout << "    debugPrint 直接读私有成员 balance = " << a.balance << "\n";
}

// ---------- 封装泄漏的演示 ----------
class BadRoster {
    std::vector<std::string> items{"Alice", "Bob"};
public:
    std::vector<std::string>& getItems() { return items; }   // ✗ 返回可变引用
};

class GoodRoster {
    std::vector<std::string> items{"Alice", "Bob"};
public:
    const std::vector<std::string>& getItems() const { return items; }  // ✓ const 引用
    std::size_t size() const { return items.size(); }
};

// ---------- Pimpl：连「有哪些字段」都藏起来 ----------
class Wallet {
public:
    Wallet();
    ~Wallet();
    Wallet(Wallet&&) noexcept;                 // 有 unique_ptr 成员，需要移动语义
    Wallet& operator=(Wallet&&) noexcept;
    int total() const;
    void add(int n);
private:
    class Impl;                                 // 只声明，不定义
    std::unique_ptr<Impl> pImpl;                // 指向实现
};

// 实现细节完全放在 .cpp 里（这里为单文件演示放在下方）
class Wallet::Impl {
public:
    int cash = 0;
    int coins = 0;                              // 使用者的头文件里看不到这些字段
};

Wallet::Wallet() : pImpl(std::make_unique<Impl>()) {}
Wallet::~Wallet() = default;
Wallet::Wallet(Wallet&&) noexcept = default;
Wallet& Wallet::operator=(Wallet&&) noexcept = default;
int Wallet::total() const { return pImpl->cash + pImpl->coins; }
void Wallet::add(int n) { pImpl->cash += n; }

int main() {
    std::cout << "=== 1. 封装后：编译器挡住直接访问 ===\n";
    Account acc;
    std::cout << "  acc.getBalance() = " << acc.getBalance() << "\n";
    try {
        acc.withdraw(1000);
    } catch (const std::exception& e) {
        std::cout << "  acc.withdraw(1000) → " << e.what()
                  << "  ← 不变式 balance >= 0 得到保证\n";
    }
    // acc.balance = -999;   // 编译错误：'balance' is a private member of 'Account'
    std::cout << "  写 acc.balance = -999 → 编译错误，编译器直接挡住\n";

    std::cout << "\n=== 2. ⚠️ C++ 独有：friend 开合法后门 ===\n";
    std::cout << "    Auditor::peek(acc) = " << Auditor::peek(acc)
              << "  ← 合法访问私有成员\n";
    debugPrint(acc);
    std::cout << "  → friend 的设计意图：与其放宽访问级别，不如精确列出谁是自己人\n";
    std::cout << "  → 好处是你明确知道有哪些代码依赖了内部实现\n";

    std::cout << "\n=== 3. ⚠️ private 只是编译期检查，内存层面拦不住 ===\n";
    {
        Account a2;
        std::cout << "    改之前 getBalance() = " << a2.getBalance() << "\n";
        // 未定义行为！仅作演示，实际代码绝不要这样写
        int* hack = reinterpret_cast<int*>(&a2);
        *hack = -999;
        std::cout << "    reinterpret_cast 强改后 = " << a2.getBalance() << "\n";
        std::cout << "  → 这是未定义行为，但确实「跑通了」\n";
        std::cout << "  → C++ 的哲学：编译期检查意图，但不在运行时付出代价去阻止你\n";
        std::cout << "  → 没有运行时访问检查 = 零开销（private 完全不影响内存布局，第 24 章）\n";
    }

    std::cout << "\n=== 4. struct 与 class 的唯一区别 ===\n";
    std::cout << "  struct A { int x; };   // 默认 public\n";
    std::cout << "  class  B { int x; };   // 默认 private\n";
    std::cout << "  → 约定：纯数据聚合用 struct，有不变式要维护的用 class\n";

    std::cout << "\n=== 5. ⚠️ 封装泄漏：返回可变引用 ===\n";
    {
        BadRoster bad;
        bad.getItems().push_back("入侵者");        // 外部直接改了内部 vector
        std::cout << "  BadRoster:  外部 push_back 后内部有 "
                  << bad.getItems().size() << " 项 → ";
        for (const auto& s : bad.getItems()) std::cout << s << " ";
        std::cout << "\n";

        GoodRoster good;
        // good.getItems().push_back("入侵者");    // 编译错误：const 引用不能修改
        std::cout << "  GoodRoster: 返回 const&，外部 push_back 直接编译错误\n";
        std::cout << "              内部仍是 " << good.size() << " 项\n";
        std::cout << "  → C++ 的 const 引用在这里比 Java 的 unmodifiableList 更好：\n";
        std::cout << "     Java 是运行时抛异常，C++ 是编译期就拦住\n";
    }

    std::cout << "\n=== 6. Pimpl：连「有哪些字段」都藏起来 ===\n";
    {
        Wallet w;
        w.add(100);
        std::cout << "  w.total() = " << w.total() << "\n";
        std::cout << "  头文件里只能看到 class Impl 的声明和一个 unique_ptr\n";
        std::cout << "  → 改动私有成员不需要重新编译使用者的代码（ABI 稳定）\n";
        std::cout << "  → 代价：多一次指针跳转和一次堆分配\n";
    }

    std::cout << "\n=== 7. 暴露操作，而不是暴露状态 ===\n";
    std::cout << "  ❌ setBalance(int)  —— 只是把字段赋值包了一层\n";
    std::cout << "  ✅ deposit(int) / withdraw(int) —— 表达业务意图，保护不变式\n";

    std::cout << "\n=== 8. 小结 ===\n";
    std::cout << "  · C++ 有三级访问控制 + 独有的 friend\n";
    std::cout << "  · private 是纯编译期概念，运行时零开销、也零保护\n";
    std::cout << "  · 返回内部容器要用 const&，编译期就能拦住修改\n";
    std::cout << "  · 需要 ABI 稳定时用 Pimpl 把实现彻底藏进 .cpp\n";
    return 0;
}
