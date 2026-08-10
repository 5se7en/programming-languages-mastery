// 垃圾回收：C++ 的答案是"不回收"——析构时机由作用域决定，确定到可以打印时间线。
#include <iostream>
#include <string>

struct Student {
    std::string name;
    Student(std::string n) : name(std::move(n)) {
        std::cout << "    构造: " << name << "\n";
    }
    ~Student() {
        std::cout << "    析构: " << name << "   <- 时机确定：作用域结束的这一行\n";
    }
};

int main() {
    std::cout << "== ① 确定性析构：作用域即生命 ==\n";
    {
        Student a("小明");
        Student b("小红");
        std::cout << "    （块即将结束）\n";
    }                                   // b 先析构、a 后析构——构造的逆序
    std::cout << "    （块已结束——两个析构都已发生，无需等待任何回收器）\n";

    std::cout << "\n== ② 对比托管语言 ==\n";
    std::cout << "Java/C#/JS: 对象死亡时机 = GC 心情（弱引用要等回收才变 null）\n";
    std::cout << "Python:     引用计数归零即回收（确定）——但循环引用要等 gc（不确定）\n";
    std::cout << "C++:        离开作用域即析构——永远确定，连顺序都确定（逆序）\n";

    std::cout << "\n== ③ 手动堆对象：确定性由你负责 ==\n";
    Student* p = new Student("小刚");
    std::cout << "    new 出来的不归作用域管——\n";
    delete p;                           // 你写 delete 的这一行就是它的死期
    std::cout << "    delete 的那一行就是死期（忘了写 = 第 33 章 leaks 实测的泄漏）\n";

    std::cout << "\n== ④ 没有 GC 的世界靠什么活 ==\n";
    std::cout << "确定性析构 + 作用域 = RAII（第 37 章）：资源跟着对象走\n";
    std::cout << "循环引用问题依然存在（shared_ptr 成环）——第 38 章 weak_ptr 拆环\n";
    return 0;
}
