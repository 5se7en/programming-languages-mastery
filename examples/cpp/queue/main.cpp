// 第 19 章 · 队列 — C++ 示例
// 运行：g++ -std=c++20 -O2 -o main *.cpp && ./main
#include <iostream>
#include <queue>
#include <deque>
#include <vector>
#include <map>
#include <chrono>

int main() {
#ifdef __OPTIMIZE__
    std::cout << "[已开启 -O2]\n";
#else
    std::cout << "[未开启优化，性能数字仅供参考，建议 -O2]\n";
#endif

    // 1. std::queue 是容器适配器（底层默认 deque）
    std::queue<std::string> q;
    q.push("A"); q.push("B"); q.push("C");
    std::cout << "\nqueue 队首: " << q.front() << " | 队尾: " << q.back() << "\n";
    q.pop();                                    // ⚠️ pop() 不返回值
    std::cout << "pop() 后队首: " << q.front() << " | size: " << q.size() << "\n";

    // 2. 环形数组 vs 朴素数组（本章技术核心）
    const int N = 50000;
    using clk = std::chrono::high_resolution_clock;
    auto t0 = clk::now();
    std::vector<int> naive;
    for (int i = 0; i < N; i++) naive.push_back(i);
    long long s1 = 0;
    while (!naive.empty()) { s1 += naive.front(); naive.erase(naive.begin()); }   // O(n) 每次
    auto t1 = clk::now();
    std::vector<int> buf(N + 1);
    int head = 0, tail = 0;
    for (int i = 0; i < N; i++) { buf[tail] = i; tail = (tail + 1) % buf.size(); }
    long long s2 = 0;
    while (head != tail) { s2 += buf[head]; head = (head + 1) % buf.size(); }     // O(1) 每次
    auto t2 = clk::now();
    double nv = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double rg = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "\n" << N << " 个元素进出:\n";
    std::cout << "  朴素数组(出队O(n)): " << nv << " ms\n";
    std::cout << "  环形数组(出队O(1)): " << rg << " ms\n";
    std::cout << "  → 慢约 " << (int)(nv / rg) << " 倍（校验和一致: " << (s1 == s2 ? "是" : "否") << "）\n";

    // 3. ⚠️ priority_queue 默认是最大堆（与 Java/Python/C# 相反！）
    std::priority_queue<int> maxHeap;
    maxHeap.push(3); maxHeap.push(1); maxHeap.push(2);
    std::cout << "\n⚠️ std::priority_queue 默认 top() = " << maxHeap.top() << " ← 最大堆！\n";
    std::priority_queue<int, std::vector<int>, std::greater<int>> minHeap;
    minHeap.push(3); minHeap.push(1); minHeap.push(2);
    std::cout << "   用 greater<int> 才是最小堆, top() = " << minHeap.top() << "\n";

    // 4. std::deque：两端 O(1) 且保留随机访问
    std::deque<int> d;
    d.push_back(2); d.push_front(1); d.push_back(3);
    std::cout << "\ndeque 两端插入后: ";
    for (int x : d) std::cout << x << " ";
    std::cout << "| 随机访问 d[1] = " << d[1] << " ← vector 做不到高效头插\n";
    return 0;
}
