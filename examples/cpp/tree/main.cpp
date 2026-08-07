// 第 21 章 · 树 —— C++ 示例
// 运行：g++ -std=c++20 -O2 main.cpp -o tree && ./tree

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <map>
#include <numeric>
#include <queue>
#include <random>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

// ---------- 手写二叉搜索树 ----------
struct Node {
    int v;
    Node* left = nullptr;
    Node* right = nullptr;
    explicit Node(int value) : v(value) {}
};

// 迭代插入，避免深树时递归爆栈
Node* insert(Node* root, int v) {
    Node* node = new Node(v);
    if (!root) return node;
    Node* cur = root;
    while (true) {
        if (v < cur->v) {
            if (!cur->left) { cur->left = node; return root; }
            cur = cur->left;
        } else {
            if (!cur->right) { cur->right = node; return root; }
            cur = cur->right;
        }
    }
}

// 中序遍历：左 → 根 → 右，结果必然有序
void inorder(Node* node, std::vector<int>& out) {
    if (!node) return;
    inorder(node->left, out);
    out.push_back(node->v);
    inorder(node->right, out);
}

// 树高决定查找的最坏代价（迭代版）
int height(Node* root) {
    if (!root) return 0;
    int h = 0;
    std::stack<std::pair<Node*, int>> st;
    st.push({root, 1});
    while (!st.empty()) {
        auto [n, d] = st.top();
        st.pop();
        h = std::max(h, d);
        if (n->left) st.push({n->left, d + 1});
        if (n->right) st.push({n->right, d + 1});
    }
    return h;
}

// 释放整棵树（后序遍历：先删子节点再删自己）
void destroy(Node* node) {
    if (!node) return;
    destroy(node->left);
    destroy(node->right);
    delete node;
}

template <typename T>
void printVec(const std::vector<T>& v, size_t limit = 20) {
    std::cout << "[";
    for (size_t i = 0; i < v.size() && i < limit; ++i) {
        if (i) std::cout << ", ";
        std::cout << v[i];
    }
    if (v.size() > limit) std::cout << ", ...";
    std::cout << "]";
}

int main() {
    std::cout << "=== 1. 二叉搜索树：左小右大 ===\n";
    std::vector<int> values{50, 30, 70, 20, 40, 60, 80};
    Node* root = nullptr;
    for (int v : values) root = insert(root, v);

    std::vector<int> out;
    inorder(root, out);
    std::cout << "插入顺序: "; printVec(values); std::cout << "\n";
    std::cout << "中序遍历: "; printVec(out);
    std::cout << " ← 自动有序！这是 BST 的定义性质\n";
    std::cout << "树高: " << height(root) << "\n";

    std::cout << "\n=== 2. ⚠️ BST 的退化：有序插入会变成链表 ===\n";
    const int N = 2000;

    std::vector<int> shuffled(N);
    std::iota(shuffled.begin(), shuffled.end(), 0);
    std::shuffle(shuffled.begin(), shuffled.end(), std::mt19937(42));  // 固定种子

    Node* randomTree = nullptr;
    for (int v : shuffled) randomTree = insert(randomTree, v);

    Node* sortedTree = nullptr;
    for (int i = 0; i < N; ++i) sortedTree = insert(sortedTree, i);

    std::cout << "随机插入 " << N << " 个数 → 树高 " << height(randomTree) << "\n";
    std::cout << "有序插入 " << N << " 个数 → 树高 " << height(sortedTree)
              << "   ← 完全退化成链表！\n";
    std::cout << "理想树高 log2(" << N << ") ≈ "
              << static_cast<int>(std::round(std::log2(N))) << "\n";
    std::cout << "→ 这就是「平衡树」（AVL / 红黑树）存在的全部理由\n";

    std::cout << "\n=== 3. 容器名直接暴露底层结构 ===\n";
    std::map<std::string, int> treeMap{{"zebra", 1}, {"apple", 2}, {"mango", 3}};
    std::unordered_map<std::string, int> hashMap{{"zebra", 1}, {"apple", 2}, {"mango", 3}};

    std::cout << "std::map           (红黑树, 有序): ";
    for (const auto& [k, v] : treeMap) std::cout << k << " ";
    std::cout << "← 自动排序\n";

    std::cout << "std::unordered_map (哈希表, 无序): ";
    for (const auto& [k, v] : hashMap) std::cout << k << " ";
    std::cout << "← 顺序不确定\n";
    std::cout << "→ 换掉一个词就能切换实现，复杂度一目了然\n";

    std::cout << "\n=== 4. std::map 的有序能力 ===\n";
    std::map<int, std::string> m;
    for (int i = 0; i < 200000; ++i) m[i] = "v" + std::to_string(i);

    std::cout << "begin()->first   → " << m.begin()->first << "   最小键\n";
    std::cout << "rbegin()->first  → " << m.rbegin()->first << "   最大键\n";
    std::cout << "lower_bound(15)  → " << m.lower_bound(15)->first
              << "   第一个 >= 15 的\n";
    std::cout << "upper_bound(20)  → " << m.upper_bound(20)->first
              << "   第一个 > 20 的\n";

    std::cout << "范围查询 [100, 104]: ";
    for (auto it = m.lower_bound(100); it != m.upper_bound(104); ++it)
        std::cout << it->first << " ";
    std::cout << "\n→ 有序 = 能回答「大于/范围/最接近/排序」\n";

    std::cout << "\n=== 5. 有序的代价：std::map vs std::unordered_map ===\n";
    const int M = 200000;
    using Clock = std::chrono::steady_clock;

    // 预热，避免首次分配影响计时
    { std::unordered_map<int, int> warm; for (int i = 0; i < M; ++i) warm[i] = i; }

    volatile long long sink = 0;   // 防止 -O2 把查找循环整个优化掉

    std::unordered_map<int, int> hash;
    auto t0 = Clock::now();
    for (int i = 0; i < M; ++i) hash[i] = i;
    long long s1 = 0;
    for (int i = 0; i < M; ++i) s1 += hash.find(i)->second;
    auto t1 = Clock::now();

    std::map<int, int> tree;
    for (int i = 0; i < M; ++i) tree[i] = i;
    long long s2 = 0;
    for (int i = 0; i < M; ++i) s2 += tree.find(i)->second;
    auto t2 = Clock::now();

    sink = s1 + s2;   // 让结果被"使用"，循环才不会被优化掉

    double hashMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double treeMs = std::chrono::duration<double, std::milli>(t2 - t1).count();
    std::cout << M << " 次插入+查找:\n";
    std::cout << "  unordered_map: " << static_cast<int>(hashMs) << " ms\n";
    std::cout << "  map          : " << static_cast<int>(treeMs) << " ms\n";
    std::cout << "  → 哈希快约 " << std::fixed << std::setprecision(1)
              << (treeMs / hashMs) << " 倍，这就是「有序」的价格\n";
    std::cout << "  (校验和 " << sink << "，确保循环没被优化掉)\n";
    std::cout << "  ⚠️ 数字依赖环境，记住结论：哈希更快，树更全能\n";

    std::cout << "\n=== 6. priority_queue 是堆 ===\n";
    std::priority_queue<int, std::vector<int>, std::greater<int>> pq;
    for (int v : {5, 3, 8, 1, 9}) pq.push(v);
    std::cout << "逐个 pop: ";
    while (!pq.empty()) { std::cout << pq.top() << " "; pq.pop(); }
    std::cout << "← 小顶堆，每次取最小\n";

    std::cout << "\n=== 7. 堆算法作用于数组（不是容器）===\n";
    std::vector<int> h{3, 1, 4, 1, 5, 9, 2, 6};
    std::make_heap(h.begin(), h.end());
    std::cout << "make_heap 后: "; printVec(h);
    std::cout << " ← 不是有序数组！只保证堆顶最大\n";
    std::sort_heap(h.begin(), h.end());
    std::cout << "sort_heap 后: "; printVec(h); std::cout << " ← 这才有序\n";

    std::cout << "\n=== 8. 迭代器失效的差异 ===\n";
    std::cout << "std::map:           插入删除后迭代器仍有效（红黑树不搬移节点）\n";
    std::cout << "std::unordered_map: rehash 后迭代器失效（第 20 章）\n";

    destroy(root);
    destroy(randomTree);
    destroy(sortedTree);
    return 0;
}
