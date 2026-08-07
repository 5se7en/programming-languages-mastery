// 第 09 章 · 数据类型 — C++ 示例
// 运行：g++ -std=c++20 -o data-types data-types.cpp && ./data-types
#include <iostream>
#include <iomanip>
#include <string>
#include <cstdint>
#include <climits>

int main() {
    // 1. 基本类型宽度由实现决定
    std::cout << "本机宽度(字节): int=" << sizeof(int) << " long=" << sizeof(long)
              << " double=" << sizeof(double) << "\n";
    std::cout << "int 范围: " << INT_MIN << " ~ " << INT_MAX << "\n";

    // 2. 跨平台要用固定宽度类型
    int32_t score = 92;
    int64_t bigId = 9007199254740993LL;
    std::cout << "int32_t score=" << score << " int64_t bigId=" << bigId << "\n";

    // 3. 浮点误差
    std::cout << std::setprecision(17) << "0.1 + 0.2 = " << (0.1 + 0.2)
              << " | 等于 0.3 吗: " << (0.1 + 0.2 == 0.3) << "\n";

    // 4. 整数除法会截断
    std::cout << "92 / 100 = " << (92 / 100) << "  ← 截断成 0\n";
    std::cout << "92.0 / 100 = " << std::setprecision(4) << (92.0 / 100) << "  ← 正确\n";

    // 5. std::string 数的是字节
    std::string wave = "👋";
    std::cout << "'👋'.size() = " << wave.size() << " (UTF-8 字节)\n";
    return 0;
}
