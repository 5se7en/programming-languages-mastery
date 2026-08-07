// 第 08 章 · 变量 — C++ 示例
// 运行：g++ -std=c++20 -o variables variables.cpp && ./variables
#include <iostream>
#include <string>

int main() {
    // 1. 声明
    std::string studentName = "Alice";
    const int MAX_SCORE = 100;
    int age = 20;
    auto score = 92;                       // 推导为 int
    std::cout << studentName << " " << age << " " << score << " " << MAX_SCORE << "\n";

    // 2. 值语义：赋值即复制
    int a = 92;
    int b = a;
    b = 60;
    std::cout << "值语义 复制: " << a << " " << b << "\n";        // 92 60

    // 3. 引用是别名，不是新变量
    int& ref = a;
    ref = 60;
    std::cout << "引用 别名: " << a << " " << ref << "\n";         // 60 60
    std::cout << "同一地址吗: " << (&a == &ref ? "是" : "否") << "\n";

    // 4. 变量就是存储：可以直接看见地址
    std::cout << "a 的地址: " << &a << "\n";
    return 0;
}
