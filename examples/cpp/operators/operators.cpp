// 第 10 章 · 运算符 — C++ 示例
// 运行：g++ -std=c++20 -o operators operators.cpp && ./operators
#include <iostream>
#include <string>

// 运算符重载：让自定义类型支持 + 和 ==
struct Score {
    int v;
};
Score operator+(const Score& a, const Score& b) { return Score{a.v + b.v}; }
bool operator==(const Score& a, const Score& b) { return a.v == b.v; }

bool boom() { std::cout << "   ← 这行不该出现！\n"; return true; }

int main() {
    // 1. 运算符重载生效
    Score s = Score{90} + Score{5};
    std::cout << "重载 + → " << s.v << " | 重载 == → " << (Score{95} == s) << "\n";

    // 2. string 重载了 ==（比较内容），裸指针比较的是地址
    std::string a = "hi", b = "hi";
    std::cout << "std::string a == b → " << (a == b) << " (比较内容)\n";

    // 3. 短路求值
    std::cout << "false && boom() → " << (false && boom()) << "\n";

    // 4. 位运算优先级低于比较 —— 必须加括号
    int flags = 0b1010, mask = 0b0010;
    std::cout << "(flags & mask) == 2 → " << ((flags & mask) == 2) << "  ✓ 加了括号\n";

    // 5. 整数除法截断
    std::cout << "92 / 100 = " << (92 / 100) << " | 92.0 / 100 = " << (92.0 / 100) << "\n";
    return 0;
}
