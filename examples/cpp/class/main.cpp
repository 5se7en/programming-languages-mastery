// 第 23 章 · 类 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o cls && ./cls
// C++ 与其他语言最大的差异：默认「值语义」，且对象可以在栈上

#include <iostream>
#include <string>
#include <utility>
#include <vector>

// ---------- 一个完整的类 ----------
class Student {
private:
    std::string name;
    int score;
    int id;

    static int count;                    // 静态成员：类内声明

public:
    static int copyCount;                // 统计发生过多少次拷贝构造
    static constexpr int PASS_LINE = 60; // 静态常量
    static std::string school;

    // 构造函数：优先用初始化列表，比在函数体里赋值更高效
    Student(std::string n, int s)
        : name(std::move(n)), score(s), id(++count) {
        if (s < 0 || s > 100)            // 构造函数保证对象从诞生起就合法
            throw std::invalid_argument("分数必须在 0..100 之间");
    }

    // ⚠️ 析构函数：其他语言没有这个东西
    ~Student() { --count; }

    // 拷贝构造函数：C++ 值语义的关键
    Student(const Student& other)
        : name(other.name), score(other.score), id(++count) { ++copyCount; }

    bool isPassing() const { return score >= PASS_LINE; }  // const 表示不修改对象

    const std::string& getName() const { return name; }
    void setName(std::string n) { name = std::move(n); }
    int getScore() const { return score; }
    int getId() const { return id; }

    static int getCount() { return count; }
};

int Student::count = 0;                          // 静态成员必须在类外定义
int Student::copyCount = 0;
std::string Student::school = "第一中学";

// RAII 演示：离开作用域自动释放资源
class ScopedResource {
    std::string tag;
public:
    explicit ScopedResource(std::string t) : tag(std::move(t)) {
        std::cout << "    [" << tag << "] 获取资源（构造函数）\n";
    }
    ~ScopedResource() {
        std::cout << "    [" << tag << "] 释放资源（析构函数，自动调用）\n";
    }
};

int main() {
    std::cout << "=== 1. 用类打包：数据和行为待在一起 ===\n";
    Student alice("Alice", 92);
    Student bob("Bob", 45);
    std::cout << "  " << alice.getName() << ": 分数 " << alice.getScore()
              << ", 及格? " << std::boolalpha << alice.isPassing() << "\n";
    std::cout << "  " << bob.getName() << ": 分数 " << bob.getScore()
              << ", 及格? " << bob.isPassing() << "\n";
    std::cout << "  静态成员 Student::school = " << Student::school << "  ← 所有实例共享\n";
    std::cout << "  当前存活实例数 = " << Student::getCount() << "\n";

    std::cout << "\n=== 2. ⚠️ C++ 默认是值语义：b = a 是拷贝，不是别名 ===\n";
    {
        Student a("Alice", 90);
        Student b = a;                   // 拷贝出一个全新的对象！
        b.setName("Bob");
        std::cout << "  赋值后: a.name=" << a.getName()
                  << "  b.name=" << b.getName() << "  ← a 完全没受影响\n";
        std::cout << "  a 的地址 " << &a << "\n";
        std::cout << "  b 的地址 " << &b << "  ← 两个不同的对象\n";

        std::cout << "\n  想要引用语义，必须显式用引用：\n";
        Student& r = a;                  // 引用才是别名
        r.setName("Changed");
        std::cout << "    用引用改后: a.name=" << a.getName() << "  ← 这才影响原对象\n";
    }
    std::cout << "  → 对比 Java/Python/C#/JS：它们的 b = a 都是起别名\n";
    std::cout << "  → 这是 C++ 与其余语言最根本的差异之一\n";

    std::cout << "\n=== 3. 对象可以在栈上，也可以在堆上（你决定）===\n";
    {
        Student onStack("Stack", 80);                     // 栈上对象
        Student* onHeap = new Student("Heap", 85);        // 堆上对象
        std::cout << "  栈上对象 " << onStack.getName() << " 的地址 " << &onStack << "\n";
        std::cout << "  堆上对象 " << onHeap->getName() << " 的地址 " << onHeap << "\n";
        delete onHeap;                                     // 堆对象必须手动释放
        std::cout << "  → 栈对象离开作用域自动销毁；堆对象必须 delete（或用智能指针）\n";
    }

    std::cout << "\n=== 4. 析构函数与 RAII：C++ 独有的资源管理方式 ===\n";
    std::cout << "  进入作用域前\n";
    {
        ScopedResource r1("文件句柄");
        ScopedResource r2("互斥锁");
        std::cout << "    ...在作用域内工作...\n";
    }   // 离开作用域，两个析构函数自动逆序调用
    std::cout << "  离开作用域后 —— 资源已全部自动释放\n";
    std::cout << "  → 不需要 GC，也不会忘记释放。这是 C++ 用值语义换来的最大好处\n";

    std::cout << "\n=== 5. 构造函数保证对象合法 ===\n";
    try {
        Student invalid("Invalid", 150);
    } catch (const std::invalid_argument& e) {
        std::cout << "  Student(\"Invalid\", 150) → " << e.what() << "\n";
        std::cout << "  → 非法对象根本无法被创建出来\n";
    }

    std::cout << "\n=== 6. struct 与 class 只差默认可见性 ===\n";
    std::cout << "  struct A { int x; };   // 默认 public\n";
    std::cout << "  class  B { int x; };   // 默认 private\n";
    std::cout << "  → 除此之外完全一样，这与 C# 的 struct(值类型) 是两回事\n";

    std::cout << "\n=== 7. 值语义的代价：传大对象要用 const& ===\n";
    {
        std::vector<Student> roster;
        roster.reserve(3);
        roster.emplace_back("R1", 70);
        roster.emplace_back("R2", 80);
        roster.emplace_back("R3", 90);

        // ✗ 按值传会拷贝整个对象
        auto byValue = [](Student s) { return s.getScore(); };
        // ✓ 用 const& 避免拷贝
        auto byRef = [](const Student& s) { return s.getScore(); };

        int before = Student::copyCount;
        byValue(roster[0]);                      // 产生一次拷贝构造
        int afterValue = Student::copyCount;
        byRef(roster[0]);                        // 不产生拷贝
        int afterRef = Student::copyCount;

        std::cout << "  按值传参  Student s : 拷贝次数 " << before
                  << " → " << afterValue << "   增加了 " << (afterValue - before) << " 次\n";
        std::cout << "  const& 传参        : 拷贝次数 " << afterValue
                  << " → " << afterRef << "   增加了 " << (afterRef - afterValue) << " 次\n";
        std::cout << "  → 这不是优化技巧，而是理解值语义后的自然结论\n";
    }

    return 0;
}
