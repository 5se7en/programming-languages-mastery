// 第 16 章 · 数组 — C++ 示例
// 运行：g++ -std=c++20 -O2 -o main *.cpp && ./main
#include <iostream>
#include <array>
#include <vector>
#include <chrono>
#include <stdexcept>

int main() {
    // 1. 地址连续：验证 地址 = 基址 + 索引 × 元素大小
    int a[5] = {92, 75, 88, 60, 100};
    std::cout << "sizeof(int) = " << sizeof(int) << " 字节\n";
    for (int i = 0; i < 3; i++)
        std::cout << "  a[" << i << "] 距首地址 "
                  << ((char*)&a[i] - (char*)&a[0]) << " 字节\n";
    std::cout << "→ 这就是 O(1) 随机访问的原理，也是下标从 0 开始的原因\n";

    // 2. sizeof 求长度（注意：传入函数后会退化为指针）
    std::cout << "\nsizeof(a)/sizeof(a[0]) = " << sizeof(a) / sizeof(a[0]) << "\n";

    // 3. ⚠️ 越界不检查（未定义行为）；.at() 才检查
    std::vector<int> v{10, 20, 30};
    std::cout << "v[10] → " << v[10] << "  ← 未定义行为，读到垃圾值，不报错！\n";
    try { v.at(10); }
    catch (const std::out_of_range&) { std::cout << "v.at(10) → std::out_of_range ← .at() 会检查\n"; }

    // 4. C++ 的二维数组是真正连续的（与 Java 不同）
    int m2[2][3] = {{1,2,3},{4,5,6}};
    std::cout << "\n二维数组按行主序连续存放: ";
    int* flat = &m2[0][0];
    for (int i = 0; i < 6; i++) std::cout << flat[i] << " ";
    std::cout << "← 可当一维数组线性访问\n";

    // 5. 缓存局部性：行优先 vs 列优先
    //    ⚠️ 这个实验的结果高度依赖编译优化：不开 -O2 时，循环开销会掩盖缓存差异。
#ifdef __OPTIMIZE__
    std::cout << "\n[已开启编译器优化 -O2，可观察到明显的缓存差异]\n";
#else
    std::cout << "\n[未开启优化！循环开销会掩盖缓存效应，请用 -O2 重新编译对比]\n";
#endif
    const int N = 2000;
    std::vector<std::vector<int>> m(N, std::vector<int>(N, 1));
    using clk = std::chrono::high_resolution_clock;

    volatile long long sink = 0;
    for (int w = 0; w < 2; w++) {            // 预热，排除首次访问的冷缓存/缺页影响
        long long x = 0;
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) x += m[i][j];
        sink = x;
    }

    auto t0 = clk::now();
    long long s1 = 0;
    for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) s1 += m[i][j];
    auto t1 = clk::now();
    long long s2 = 0;
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) s2 += m[i][j];
    auto t2 = clk::now();
    sink = s1 + s2;

    double row = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double col = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "缓存局部性: 行优先 " << row << "ms vs 列优先 " << col
              << "ms → 慢 " << col / row << " 倍（校验和一致: "
              << (s1 == s2 ? "是" : "否") << "）\n";
    return 0;
}
