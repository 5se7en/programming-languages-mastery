// 第 13 章 · 作用域 — C++ 示例
// 运行：g++ -std=c++20 -o scope scope.cpp && ./scope
#include <iostream>
#include <string>
#include <map>

int value = 10;                       // 全局作用域
namespace app { int value = 20; }     // 命名空间作用域

int main() {
    int value = 30;                   // 局部遮蔽了全局
    std::cout << "局部 value      = " << value << "\n";
    std::cout << "全局 ::value    = " << ::value << "   ← :: 显式指定全局作用域\n";
    std::cout << "命名空间 app::  = " << app::value << "\n";

    // 1. 块作用域 + 允许遮蔽（Java/C# 不允许）
    {
        int value = 40;
        std::cout << "块内 value      = " << value << "   ← C++ 允许遮蔽\n";
    }
    std::cout << "块外 value      = " << value << "   ← 恢复外层\n";

    // 2. C++17：在 if 中声明变量，把作用域限制到最小
    std::map<std::string, int> m{{"Alice", 92}};
    if (auto it = m.find("Alice"); it != m.end())
        std::cout << "if 内声明: " << it->first << "=" << it->second << "（it 仅此可见）\n";

    // 3. Lambda 显式捕获：按值 vs 按引用
    int base = 1;
    auto byValue = [base]() { return base; };     // 捕获时复制
    auto byRef   = [&base]() { return base; };    // 引用外层变量
    base = 100;
    std::cout << "按值捕获(捕获时=1):   " << byValue() << "\n";
    std::cout << "按引用捕获(现在=100): " << byRef() << "\n";
    return 0;
}
