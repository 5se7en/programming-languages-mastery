// 索引：手写 B+ 树——回答「为什么磁盘索引用 B+ 树而不用二叉树」。
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// ---------- B+ 树（批量构建版——真实数据库 CREATE INDEX 也是这么建的）----------
struct Node {
    bool leaf = false;
    std::vector<int> keys;        // 内部节点: 分隔键；叶子节点: 实际键
    std::vector<int> children;    // 内部节点: 子节点下标
    int next = -1;                // 叶子节点: 指向右邻叶子（范围扫描靠它）
    int min_key = 0;              // 本子树里的最小键（用作父节点的分隔键）
};

class BPlusTree {
public:
    BPlusTree(const std::vector<int>& sorted, int fanout) : fanout_(fanout) {
        // ① 先把有序键切成叶子
        std::vector<int> level;
        for (size_t i = 0; i < sorted.size(); i += fanout) {
            Node n;
            n.leaf = true;
            for (size_t j = i; j < std::min(i + fanout, sorted.size()); ++j)
                n.keys.push_back(sorted[j]);
            n.min_key = n.keys.front();
            nodes_.push_back(n);
            level.push_back((int)nodes_.size() - 1);
        }
        for (size_t i = 0; i + 1 < level.size(); ++i)      // 串起叶子链表
            nodes_[level[i]].next = level[i + 1];
        leaf_count_ = level.size();
        height_ = 1;

        // ② 自底向上建内部层，直到只剩一个根
        while (level.size() > 1) {
            std::vector<int> parent;
            for (size_t i = 0; i < level.size(); i += fanout) {
                Node n;
                n.leaf = false;
                for (size_t j = i; j < std::min(i + fanout, level.size()); ++j) {
                    n.children.push_back(level[j]);
                    // 分隔键 = 该子树的最小键（不能用 keys.front()：只有一个孩子的内部节点没有分隔键）
                    if (j > i) n.keys.push_back(nodes_[level[j]].min_key);
                }
                n.min_key = nodes_[n.children.front()].min_key;
                nodes_.push_back(n);
                parent.push_back((int)nodes_.size() - 1);
            }
            level = parent;
            height_++;
        }
        root_ = level[0];
    }

    /// 点查：从根下钻到叶。visits 记录访问了几个节点 = 真实数据库的【磁盘页读取次数】
    bool find(int key, int& visits) const {
        int cur = root_;
        visits = 0;
        while (true) {
            ++visits;
            const Node& n = nodes_[cur];
            if (n.leaf)
                return std::binary_search(n.keys.begin(), n.keys.end(), key);
            // 在分隔键里二分，决定走哪个孩子
            size_t idx = std::upper_bound(n.keys.begin(), n.keys.end(), key) - n.keys.begin();
            cur = n.children[idx];
        }
    }

    /// 范围扫描：定位到起点叶子后，沿叶子链表往右走——B+ 树相对哈希的核心优势
    int range_count(int lo, int hi, int& visits) const {
        int cur = root_;
        visits = 0;
        while (!nodes_[cur].leaf) {                       // 先下钻到 lo 所在的叶子
            ++visits;
            const Node& n = nodes_[cur];
            size_t idx = std::upper_bound(n.keys.begin(), n.keys.end(), lo) - n.keys.begin();
            cur = n.children[idx];
        }
        int cnt = 0;
        while (cur != -1) {                                // 然后顺着叶子链表扫
            ++visits;
            for (int k : nodes_[cur].keys) {
                if (k > hi) return cnt;
                if (k >= lo) ++cnt;
            }
            cur = nodes_[cur].next;
        }
        return cnt;
    }

    int height() const { return height_; }
    size_t node_count() const { return nodes_.size(); }
    size_t leaf_count() const { return leaf_count_; }

private:
    std::vector<Node> nodes_;
    int root_ = 0, fanout_, height_ = 0;
    size_t leaf_count_ = 0;
};

int main() {
    const int N = 1'000'000;
    std::vector<int> keys(N);
    for (int i = 0; i < N; ++i) keys[i] = i * 2;          // 偶数键，便于构造「查不到」的情况

    printf("== ① 扇出决定树高——这就是 B+ 树存在的全部理由 ==\n");
    printf("  %d 个键，不同扇出下的树形态:\n", N);
    printf("  ┌────────┬────────┬──────────┬────────────┬──────────────────────┐\n");
    printf("  │ 扇出    │ 树高    │ 节点总数  │ 每次点查     │ 相当于                │\n");
    printf("  ├────────┼────────┼──────────┼────────────┼──────────────────────┤\n");
    for (int fanout : {2, 8, 128, 512}) {
        BPlusTree t(keys, fanout);
        int visits = 0;
        t.find(N, visits);
        const char* note = fanout == 2 ? "二叉树（教科书里的树）"
                         : fanout == 128 ? "典型数据库页(4KB/32B)  "
                         : fanout == 512 ? "大页/窄键的索引        "
                                         : "                      ";
        printf("  │ %6d │ %6d │ %8zu │ 访问 %2d 节点 │ %s │\n",
               fanout, t.height(), t.node_count(), visits, note);
    }
    printf("  └────────┴────────┴──────────┴────────────┴──────────────────────┘\n");
    printf("  → 扇出 2（二叉树）要访问 20 个节点；扇出 128 只要 3 个\n");
    printf("  → 在内存里这没什么了不起，但每个节点 = 【一次磁盘页读取】时，20 次 vs 4 次是天壤之别\n");
    printf("  → 磁盘读一页 ~100μs（第 46 章的量级）→ 20 页 = 2ms，3 页 = 0.3ms（快 6.7x）\n");

    printf("\n== ② 树高的增长有多慢（B+ 树最反直觉的性质）==\n");
    for (int n : {1000, 100000, 10000000}) {
        std::vector<int> k(n);
        for (int i = 0; i < n; ++i) k[i] = i;
        BPlusTree t(k, 128);
        printf("  %10d 个键，扇出 128 → 树高 %d\n", n, t.height());
    }
    printf("  → 数据量涨 10000 倍，树高只从 2 涨到 4——查询成本几乎不变\n");
    printf("  → 这就是索引能「一次建好、一直有效」的数学基础\n");

    printf("\n== ③ 点查对决：全表扫描 vs 二分 vs B+ 树 vs 哈希 ==\n");
    BPlusTree tree(keys, 128);
    std::unordered_map<int, int> hash;
    hash.reserve(N * 2);
    for (int i = 0; i < N; ++i) hash[keys[i]] = i;

    const int LOOKUPS = 20000;
    volatile int sink = 0;

    auto t0 = Clock::now();
    for (int q = 0; q < 200; ++q) {                       // 全表扫只做 200 次，否则太慢
        int target = (q * 9973) % N * 2;
        for (int i = 0; i < N; ++i) if (keys[i] == target) { sink = i; break; }
    }
    double msScan = ms_since(t0) / 200 * LOOKUPS;         // 外推到同样的次数

    t0 = Clock::now();
    for (int q = 0; q < LOOKUPS; ++q)
        sink = std::binary_search(keys.begin(), keys.end(), (q * 9973) % N * 2);
    double msBin = ms_since(t0);

    t0 = Clock::now();
    int visits = 0;
    for (int q = 0; q < LOOKUPS; ++q) sink = tree.find((q * 9973) % N * 2, visits);
    double msTree = ms_since(t0);

    t0 = Clock::now();
    for (int q = 0; q < LOOKUPS; ++q) sink = hash.count((q * 9973) % N * 2);
    double msHash = ms_since(t0);

    printf("  %d 次点查（%d 行）:\n", LOOKUPS, N);
    printf("    全表扫描  : %9.1f ms（外推自 200 次）  O(n)\n", msScan);
    printf("    有序数组二分: %9.1f ms（快 %.0fx）        O(log n)\n", msBin, msScan / msBin);
    printf("    B+ 树     : %9.1f ms（快 %.0fx）        O(log_f n)，%d 次节点访问\n",
           msTree, msScan / msTree, visits);
    printf("    哈希表    : %9.1f ms（快 %.0fx）        O(1)\n", msHash, msScan / msHash);
    printf("  ⚠️ 注意 B+ 树【没有】比有序数组二分快——内存里它俩都是 O(log n) 且二分更省指针跳转\n");
  printf("  → B+ 树的优势【不在内存】: 有序数组无法高效【插入】，而 B+ 树可以（且为磁盘优化）\n");
  printf("  → 内存里哈希最快；但数据库索引【绝大多数是 B+ 树】，为什么？看 ④\n");

    printf("\n== ④ 哈希索引的死穴：范围查询 ==\n");
    int rvisits = 0;
    t0 = Clock::now();
    int cnt = 0;
    for (int q = 0; q < 1000; ++q) cnt = tree.range_count(1000, 3000, rvisits);
    double msRangeTree = ms_since(t0);

    t0 = Clock::now();
    int cnt2 = 0;
    for (int q = 0; q < 1000; ++q) {
        cnt2 = 0;
        for (const auto& kv : hash) if (kv.first >= 1000 && kv.first <= 3000) ++cnt2;
    }
    double msRangeHash = ms_since(t0);

    printf("  查询「1000 <= key <= 3000」1000 次（命中 %d 行）:\n", cnt);
    printf("    B+ 树: %8.2f ms（下钻 + 沿叶子链表走，共访问 %d 个节点）\n", msRangeTree, rvisits);
    printf("    哈希 : %8.1f ms（慢 %.0fx —— 哈希打乱了顺序，只能【全部扫一遍】）\n",
           msRangeHash, msRangeHash / msRangeTree);
    printf("  结果一致: %s\n", cnt == cnt2 ? "true" : "false");
    printf("  → 哈希索引只能回答「等于」；B+ 树能回答等于/大于/小于/BETWEEN/前缀/ORDER BY\n");
    printf("  → 这就是数据库默认建 B+ 树索引的原因: 一种结构覆盖绝大多数查询形态\n");

    printf("\n== ⑤ B+ 树的三个设计决策（每一个都为磁盘服务）==\n");
    printf("  ① 数据【只放叶子】，内部节点只放分隔键\n");
    printf("     → 内部节点能塞下更多键 → 扇出更大 → 树更矮（① 的实测）\n");
    printf("  ② 叶子之间【串成链表】\n");
    printf("     → 范围扫描不用回到根节点，顺序读磁盘（④ 的实测）\n");
    printf("  ③ 节点大小 = 【磁盘页大小】（通常 4KB/8KB/16KB）\n");
    printf("     → 一次 I/O 读一个完整节点，不浪费任何一次读\n");
    printf("  → 对比第 21 章的二叉搜索树: 它为【内存】设计，每节点 1 个键，树高 log2(n)\n");

    printf("\n== ⑥ 索引也是数据，也要占空间 ==\n");
    BPlusTree t128(keys, 128);
    size_t idx_entries = t128.node_count() * 128;
    printf("  %d 个键的 B+ 树: %zu 个节点（其中叶子 %zu 个）\n",
           N, t128.node_count(), t128.leaf_count());
    printf("  若每节点 = 一个 4KB 页 → 索引占 %.1f MB\n", t128.node_count() * 4096.0 / 1048576);
    printf("  → 索引不是免费的加速器，它是【一份额外的、需要同步维护的数据副本】\n");
    printf("  → 每次 INSERT/UPDATE/DELETE 都要把所有相关索引一起改（Python 版实测代价）\n");
    (void)idx_entries;
    (void)sink;
    return 0;
}
