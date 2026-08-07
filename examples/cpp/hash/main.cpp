// 第 20 章 · 哈希 — C++ 示例
// 运行：g++ -std=c++20 -O2 -o main *.cpp && ./main
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <string>
#include <vector>
#include <chrono>

struct Student { std::string name; };
struct StudentHash {                                   // 自定义类型作键需提供哈希器
    size_t operator()(const Student& s) const { return std::hash<std::string>{}(s.name); }
};
struct StudentEq {
    bool operator()(const Student& a, const Student& b) const { return a.name == b.name; }
};

int main() {
    // 1. unordered_map（哈希，无序）vs map（红黑树，有序）
    std::unordered_map<std::string, int> hashMap{{"zebra",1},{"apple",2},{"mango",3}};
    std::map<std::string, int> treeMap{{"zebra",1},{"apple",2},{"mango",3}};
    std::cout << "unordered_map(哈希,无序): ";
    for (auto& [k, v] : hashMap) std::cout << k << " ";
    std::cout << "\nmap(红黑树,按键排序):     ";
    for (auto& [k, v] : treeMap) std::cout << k << " ";
    std::cout << "\n";

    // 2. ⚠️ operator[] 会静默插入默认值！
    std::unordered_map<std::string, int> m;
    std::cout << "\n插入前 size = " << m.size() << "\n";
    if (m["missing"] == 0) { }                          // ✗ 这一行就插入了一个元素
    std::cout << "只是「读」了一下 m[\"missing\"] 之后 size = " << m.size()
              << "  ← ⚠️ operator[] 静默插入了默认值！\n";
    std::unordered_map<std::string, int> m2;
    if (m2.count("missing")) { }                        // ✓ 只读
    std::cout << "改用 count() 检查后 size = " << m2.size() << "  ✓ 安全\n";

    // 3. at() 带边界检查
    try { m2.at("missing"); }
    catch (const std::out_of_range&) { std::cout << "at() 键不存在 → std::out_of_range ✓\n"; }

    // 4. 自定义类型作键
    std::unordered_map<Student, int, StudentHash, StudentEq> sm;
    sm[Student{"Alice"}] = 92;
    std::cout << "\n自定义键: Alice = " << sm[Student{"Alice"}] << " ← 需同时提供 Hash 与 Eq\n";

    // 5. C++ 暴露哈希表内部状态（透明哲学）
    std::unordered_map<int, int> big;
    for (int i = 0; i < 100; i++) big[i] = i;
    std::cout << "\n桶数量: " << big.bucket_count()
              << " | 负载因子: " << big.load_factor()
              << " | 最大负载因子: " << big.max_load_factor() << "\n";
    big.reserve(10000);                                  // 预分配，避免多次 rehash
    std::cout << "reserve(10000) 后桶数量: " << big.bucket_count() << " ← 避免反复 rehash\n";

    // 6. 哈希 vs 线性查找
    const int N = 200000;
    std::vector<std::string> vec;
    std::unordered_set<std::string> st;
    for (int i = 0; i < N; i++) { vec.push_back("student" + std::to_string(i)); st.insert(vec.back()); }
    // 目标值要均匀分布在整个数组中，否则线性查找会因为"总在前部命中"而失真
    std::vector<std::string> targets;
    for (int i = 0; i < 200; i++) targets.push_back("student" + std::to_string((long long)i * 7919 % N));

    using clk = std::chrono::high_resolution_clock;
    volatile int sink = 0;                    // volatile 防止 -O2 把整个循环优化掉
    auto t0 = clk::now();
    int c1 = 0;
    for (auto& x : targets) for (auto& v : vec) if (v == x) { c1++; break; }   // O(n)
    auto t1 = clk::now();
    int c2 = 0;
    for (auto& x : targets) if (st.count(x)) c2++;                              // O(1)
    auto t2 = clk::now();
    sink = c1 + c2;                            // 使用结果，确保循环不被消除
    double lin = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double hsh = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << "\n在 " << N << " 个元素中查找 200 次（各命中 " << c1 << "/" << c2 << " 个）:\n";
    std::cout << "  线性查找 O(n): " << lin << " ms\n";
    std::cout << "  哈希查找 O(1): " << hsh << " ms\n";
    std::cout << "  → 哈希快约 " << (int)(lin / hsh) << " 倍\n";
    return 0;
}
