// 第 11 章 · 流程控制 — C++ 示例
// 运行：g++ -std=c++17 -o control-flow control-flow.cpp && ./control-flow
#include <iostream>
#include <vector>
#include <string>
#include <map>

std::string grade(int score) {
    if (score >= 90) return "A";
    else if (score >= 60) return "B";
    else return "C";
}

int main() {
    std::vector<int> scores = {92, 75, 50};

    // 1. 分支
    std::cout << "分支: ";
    for (int s : scores) std::cout << grade(s) << " ";
    std::cout << "\n";

    // 2. 范围 for：用 const auto& 避免复制
    std::cout << "范围 for: ";
    for (const auto& s : scores) std::cout << s << " ";
    std::cout << "\n";

    // 3. C++17：if 语句内声明变量，限制作用域
    std::map<std::string, int> m{{"Alice", 92}};
    if (auto it = m.find("Alice"); it != m.end())
        std::cout << "if 内声明变量: 找到 " << it->first << " = " << it->second << "\n";

    // 4. switch 同样会穿透（这里正确地写了 break）
    int x = 2;
    switch (x) {
        case 1: std::cout << "一\n"; break;
        case 2: std::cout << "switch: 二（正确写了 break）\n"; break;
        default: std::cout << "其他\n";
    }

    // 5. do-while 至少执行一次
    int n = 0;
    do { n++; } while (n < 3);
    std::cout << "do-while 执行后 n = " << n << "\n";
    return 0;
}
