// 引用：变量的别名。不能为空、不能改指向——以及传参四件套的拷贝账单。
#include <iostream>
#include <string>
#include <utility>

struct Student {
    std::string name;
    int score;
    static inline int copies = 0;                      // 拷贝计数器
    Student(std::string n, int s) : name(std::move(n)), score(s) {}
    Student(const Student& other)                      // 拷贝构造函数：按值传参时被调用
        : name(other.name), score(other.score) { ++copies; }
};

void by_value(Student s)       { s.score = 0; }        // ① 按值：整个对象复制进来
void by_ref(Student& s)        { s.score = 100; }      // ② 引用：就是原对象本身
void by_const_ref(const Student& s) { (void)s; }       // ③ const 引用：只看不改、不复制
void by_pointer(Student* s)    { if (s) s->score = 60; } // ④ 指针：可空，要判

void swap_ref(int& a, int& b) { int t = a; a = b; b = t; }

int main() {
    std::cout << "== ① 引用即别名：连地址都是同一个 ==\n";
    int x = 42;
    int& r = x;                 // r 是 x 的别名——不是新变量
    r = 99;
    std::cout << "int& r = x; r = 99 之后 x = " << x << "\n";
    std::cout << "&x = " << &x << "\n&r = " << &r << "   <- 取 r 的地址得到的就是 x 的地址\n";

    std::cout << "\n== ② swap 测试：引用让交换成为可能 ==\n";
    int a = 1, b = 2;
    swap_ref(a, b);
    std::cout << "swap_ref(a, b) 之后: a = " << a << ", b = " << b << "   <- 成功！\n";

    std::cout << "\n== ③ 传参四件套的拷贝账单 ==\n";
    Student stu("小明", 90);
    Student::copies = 0;
    by_value(stu);
    std::cout << "按值 by_value(stu):        拷贝 " << Student::copies
              << " 次，score 仍 = " << stu.score << "（改的是副本）\n";
    Student::copies = 0;
    by_ref(stu);
    std::cout << "引用 by_ref(stu):          拷贝 " << Student::copies
              << " 次，score 变 = " << stu.score << "（改的就是本体）\n";
    Student::copies = 0;
    by_const_ref(stu);
    std::cout << "const& by_const_ref(stu):  拷贝 " << Student::copies
              << " 次（只读大对象的标准姿势）\n";
    Student::copies = 0;
    by_pointer(&stu);
    std::cout << "指针 by_pointer(&stu):     拷贝 " << Student::copies
              << " 次，score 变 = " << stu.score << "（可空版的引用）\n";

    std::cout << "\n== ④ 引用 vs 指针：四条纪律换来的安全 ==\n";
    std::cout << "必须初始化（没有野引用）| 不能重绑定 | 不能为空 | 没有算术\n";
    int y = 7;
    r = y;                      // ⚠️ 这不是"改指向"——是把 y 的值赋给 x！
    std::cout << "r = y 之后 x = " << x << "，r 仍是 x 的别名（&r==&x: "
              << std::boolalpha << (&r == &x) << "）\n";
    return 0;
}
