// 智能指针：把"谁负责 delete"写进类型系统——RAII 应用于内存的完整答案。
#include <iostream>
#include <memory>
#include <string>

struct Student {
    std::string name;
    std::shared_ptr<Student> partner;          // ⚠️ 强引用成员——成环就漏（实验 ④）
    std::weak_ptr<Student> weak_partner;       // ✅ 弱引用成员——不计数（实验 ⑤）
    explicit Student(std::string n) : name(std::move(n)) {
        std::cout << "    [构造] " << name << "\n";
    }
    ~Student() { std::cout << "    [析构] " << name << "\n"; }
};

int main() {
    std::cout << "== ① unique_ptr：唯一所有权，零开销 ==\n";
    {
        auto u = std::make_unique<Student>("小明");
        std::cout << "    sizeof(unique_ptr) = " << sizeof(u)
                  << " 字节，sizeof(裸指针) = " << sizeof(Student*)
                  << " 字节   <- 完全相同，零开销抽象\n";
    }                                          // 离开作用域自动 delete
    std::cout << "    （上面的 [析构] 就是 unique_ptr 干的——没有一行 delete）\n";

    std::cout << "\n== ② 所有权转移：不可拷贝，只可移动 ==\n";
    {
        auto a = std::make_unique<Student>("小红");
        // auto b = a;                          // ❌ 编译错误：拷贝构造被删除（见章节 shell 实测）
        auto b = std::move(a);                  // ✅ 移动：所有权转给 b
        std::cout << "    move 之后 a " << (a ? "还持有" : "已置空")
                  << "，b 持有 " << b->name << "\n";
        std::cout << "    （所有权唯一——编译器保证不会有两个 delete）\n";
    }

    std::cout << "\n== ③ shared_ptr：引用计数共享所有权 ==\n";
    {
        auto s1 = std::make_shared<Student>("小刚");
        std::cout << "    创建后 use_count = " << s1.use_count() << "\n";
        {
            auto s2 = s1;                       // 拷贝 = 计数 +1
            std::cout << "    拷贝一份 use_count = " << s1.use_count() << "\n";
        }                                       // s2 析构 = 计数 -1
        std::cout << "    内层结束 use_count = " << s1.use_count()
                  << "   <- 对象还活着\n";
        std::cout << "    sizeof(shared_ptr) = " << sizeof(s1)
                  << " 字节   <- 裸指针的两倍（多一个控制块指针）\n";
    }                                           // 计数归零 → 析构

    std::cout << "\n== ④ 钥匙实验：shared_ptr 成环 = 真实泄漏 ==\n";
    {
        auto x = std::make_shared<Student>("环-甲");
        auto y = std::make_shared<Student>("环-乙");
        x->partner = y;                         // 甲 →强→ 乙
        y->partner = x;                         // 乙 →强→ 甲（成环！）
        std::cout << "    成环后 use_count: 甲=" << x.use_count()
                  << ", 乙=" << y.use_count() << "\n";
        std::cout << "    （离开作用域——注意下面有没有 [析构] 打印）\n";
    }
    std::cout << "    ↑ 什么都没打印！两个对象永远不会被析构——泄漏\n";
    std::cout << "    （第 36 章 Python 引用计数的死角，在 C++ 里原样重现）\n";

    std::cout << "\n== ⑤ weak_ptr 拆环：同样的结构，正确的结局 ==\n";
    {
        auto x = std::make_shared<Student>("拆环-甲");
        auto y = std::make_shared<Student>("拆环-乙");
        x->partner = y;                         // 甲 →强→ 乙
        y->weak_partner = x;                    // 乙 ⇢弱⇢ 甲（不计数！）
        std::cout << "    拆环后 use_count: 甲=" << x.use_count()
                  << ", 乙=" << y.use_count() << "   <- 甲的计数没被加\n";
        std::cout << "    （离开作用域——）\n";
    }
    std::cout << "    ↑ 两个 [析构] 都打印了：环被拆开，正常回收\n";

    std::cout << "\n== ⑥ weak_ptr 的用法：lock() 提升 ==\n";
    std::weak_ptr<Student> observer;
    {
        auto owner = std::make_shared<Student>("被观察者");
        observer = owner;
        if (auto locked = observer.lock())      // 提升为 shared_ptr（临时续命）
            std::cout << "    对象活着: " << locked->name
                      << "，提升后 use_count = " << locked.use_count() << "\n";
    }
    std::cout << "    对象死后 expired() = " << std::boolalpha << observer.expired()
              << "，lock() = " << (observer.lock() ? "非空" : "空指针")
              << "   <- 弱引用安全地知道对象没了\n";
    return 0;
}
