// 第 12 章 · 函数 — C++ 示例
// 运行：g++ -std=c++20 -o functions functions.cpp && ./functions
#include <iostream>
#include <vector>
#include <numeric>
#include <functional>

// 1. 默认参数 + const& 传大对象（避免复制）
double average(const std::vector<int>& scores, bool skipZero = false) {
    if (scores.empty()) return 0;
    int sum = 0, count = 0;
    for (int s : scores) {
        if (skipZero && s == 0) continue;
        sum += s; count++;
    }
    return count ? static_cast<double>(sum) / count : 0;
}

// 2. C++ 是唯一支持真正引用传递的语言
void byValue(int x)      { x = 100; }     // 值传递：外部不变
void byReference(int& x) { x = 100; }     // 引用传递：外部会变
void byPointer(int* x)   { *x = 100; }    // 指针

int main() {
    std::vector<int> scores = {92, 75, 50};
    std::cout << "平均分: " << average(scores) << "\n";

    int n = 5;
    byValue(n);     std::cout << "值传递后:   " << n << "   ← 没变\n";
    byReference(n); std::cout << "引用传递后: " << n << " ← 变了！（真正的引用传递）\n";
    n = 5;
    byPointer(&n);  std::cout << "指针传递后: " << n << " ← 也变了\n";

    // 3. Lambda：必须显式声明捕获方式
    int base = 10;
    auto addByValue = [base](int x) { return x + base; };   // 按值捕获（复制）
    auto addByRef   = [&base](int x) { return x + base; };  // 按引用捕获
    base = 100;
    std::cout << "按值捕获(捕获时 base=10):   " << addByValue(1) << "\n";
    std::cout << "按引用捕获(现在 base=100): " << addByRef(1) << "\n";

    // 4. 闭包计数器
    auto makeCounter = []() {
        auto count = std::make_shared<int>(0);
        return [count]() { return ++(*count); };
    };
    auto c = makeCounter();
    c(); c();
    std::cout << "闭包计数器: " << c() << "\n";
    return 0;
}
