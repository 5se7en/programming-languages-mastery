// 第 17 章 · 列表 — C++ 示例
// 运行：g++ -std=c++20 -O2 -o main *.cpp && ./main
#include <iostream>
#include <vector>
#include <deque>
#include <chrono>
#include <cstring>

int main() {
#ifdef __OPTIMIZE__
    std::cout << "[已开启 -O2，性能对比数据可靠]\n";
#else
    std::cout << "[未开启优化，性能数字仅供参考，建议用 -O2 重新编译]\n";
#endif

    // 1. C++ 唯一暴露 capacity 的语言 —— 直接观察扩容规律
    std::vector<int> v;
    size_t last = 0;
    std::cout << "\nvector 追加时的 capacity 变化:\n";
    for (int i = 1; i <= 70; i++) {
        v.push_back(i);
        if (v.capacity() != last) {
            std::cout << "  size=" << v.size() << " → capacity=" << v.capacity();
            if (last) std::cout << "   增长倍数 " << (double)v.capacity() / last;
            std::cout << "\n";
            last = v.capacity();
        }
    }

    using clk = std::chrono::high_resolution_clock;

    // 2. 为什么必须成倍增长：每次 +1 是 O(n²)
    const int N = 40000;
    auto t0 = clk::now();
    int* a = nullptr; int capA = 0;
    for (int i = 0; i < N; i++) {                       // 策略A：每次扩容 +1
        int* na = new int[capA + 1];
        if (a) { std::memcpy(na, a, capA * sizeof(int)); delete[] a; }
        a = na; a[capA] = i; capA++;
    }
    auto t1 = clk::now();
    delete[] a;
    int* b = nullptr; int capB = 0, sizeB = 0;
    for (int i = 0; i < N; i++) {                       // 策略B：满了翻倍
        if (sizeB == capB) {
            int nc = capB ? capB * 2 : 1;
            int* nb = new int[nc];
            if (b) { std::memcpy(nb, b, sizeB * sizeof(int)); delete[] b; }
            b = nb; capB = nc;
        }
        b[sizeB++] = i;
    }
    auto t2 = clk::now();
    delete[] b;
    double one = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double dbl = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "\n追加 " << N << " 个元素:\n";
    std::cout << "  每次容量+1 (O(n²))  : " << one << " ms\n";
    std::cout << "  满了就翻倍 (摊还O(1)): " << dbl << " ms\n";
    std::cout << "  → 朴素做法慢约 " << (int)(one / dbl) << " 倍\n";

    // 3. reserve 预分配的收益
    const int M = 2000000;
    t0 = clk::now();
    std::vector<int> x; for (int i = 0; i < M; i++) x.push_back(i);
    t1 = clk::now();
    std::vector<int> y; y.reserve(M);                   // 预分配
    for (int i = 0; i < M; i++) y.push_back(i);
    t2 = clk::now();
    double no = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double yes = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "\n追加 " << M << " 个元素: 不预分配 " << no << "ms vs reserve "
              << yes << "ms → 快 " << no / yes << " 倍\n";

    // 4. ⚠️ 扩容会使迭代器失效
    std::vector<int> w{1, 2, 3};
    w.reserve(10);                    // 先预留，避免下面 push_back 触发重新分配
    auto it = w.begin();
    w.push_back(4);                   // 有足够容量，不会重新分配
    std::cout << "\n预留容量后 push_back，迭代器仍有效: " << *it << "\n";
    std::cout << "（若未预留而触发扩容，迭代器会失效 → 未定义行为）\n";
    return 0;
}
