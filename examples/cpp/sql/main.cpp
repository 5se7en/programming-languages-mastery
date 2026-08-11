// SQL：JOIN 的三种物理实现——数据库优化器每天都在做的选择题，亲手实现并实测。
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms_since(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

struct User  { int id; int city; };          // 5 千用户
struct Order { int id; int user_id; int amount; };   // 5 万订单

int main() {
    const int USERS = 5000, ORDERS = 50000;
    std::vector<User> users;
    users.reserve(USERS);
    for (int i = 0; i < USERS; ++i) users.push_back({i, i % 100});
    std::vector<Order> orders;
    orders.reserve(ORDERS);
    // 乱序的 user_id，模拟真实到达顺序
    for (int i = 0; i < ORDERS; ++i) orders.push_back({i, (i * 7919) % USERS, i % 500});

    printf("== 任务: orders(%d 行) JOIN users(%d 行) ON user_id，求匹配行数与金额和 ==\n",
           ORDERS, USERS);
    printf("   SQL 写法只有一种: SELECT ... FROM orders JOIN users ON o.user_id = u.id\n");
    printf("   但数据库【执行】它有三种算法——优化器替你选，今天我们亲手写\n");

    // ---------- ① 嵌套循环连接 Nested Loop Join ----------
    long long cnt1 = 0, sum1 = 0;
    auto t0 = Clock::now();
    for (const auto& o : orders)
        for (const auto& u : users)                  // 每行 orders 扫一遍 users
            if (o.user_id == u.id) { cnt1++; sum1 += o.amount; break; }
    double msNested = ms_since(t0);
    printf("\n== ① 嵌套循环 Nested Loop: O(N×M) ==\n");
    printf("  %.0f ms（最坏 %d × %d = %.1f 亿次比较）\n",
           msNested, ORDERS, USERS, 1.0 * ORDERS * USERS / 1e8);
    printf("  → 无索引、无排序、任何 JOIN 条件都能用——数据库的保底算法\n");

    // ---------- ② 哈希连接 Hash Join ----------
    t0 = Clock::now();
    std::unordered_map<int, const User*> ht;         // 建表阶段: 小表进哈希
    ht.reserve(USERS * 2);
    for (const auto& u : users) ht.emplace(u.id, &u);
    long long cnt2 = 0, sum2 = 0;
    for (const auto& o : orders) {                   // 探测阶段: 大表逐行查
        auto it = ht.find(o.user_id);
        if (it != ht.end()) { cnt2++; sum2 += o.amount; }
    }
    double msHash = ms_since(t0);
    printf("\n== ② 哈希连接 Hash Join: O(N+M) ==\n");
    printf("  %.1f ms（建表 %d 行 + 探测 %d 行，第 20 章的哈希表）\n", msHash, USERS, ORDERS);
    printf("  → 只适用【等值】连接（=）；小表建哈希、大表探测——「小表驱动大表」的出处\n");

    // ---------- ③ 归并连接 Merge Join ----------
    t0 = Clock::now();
    auto so = orders;                                // 排序阶段（若本来有序可省）
    std::sort(so.begin(), so.end(), [](auto& a, auto& b) { return a.user_id < b.user_id; });
    auto su = users;                                 // users 本就按 id 有序，此排序实为免费
    std::sort(su.begin(), su.end(), [](auto& a, auto& b) { return a.id < b.id; });
    long long cnt3 = 0, sum3 = 0;
    size_t i = 0, j = 0;
    while (i < so.size() && j < su.size()) {         // 双指针归并（第 19 章队列般推进）
        if (so[i].user_id < su[j].id) ++i;
        else if (so[i].user_id > su[j].id) ++j;
        else { cnt3++; sum3 += so[i].amount; ++i; }  // user.id 唯一，orders 侧前进
    }
    double msMerge = ms_since(t0);
    printf("\n== ③ 归并连接 Merge Join: O(N log N + M log M)，已排序则 O(N+M) ==\n");
    printf("  %.1f ms（含排序；两边都有序时数据库直接归并，是三者最快）\n", msMerge);
    printf("  → 适合等值/范围连接；输出天然有序——ORDER BY 同键时白赚一次排序\n");

    printf("\n== ④ 三种算法结果一致性与实测对比 ==\n");
    printf("  结果一致: %s（匹配 %lld 行，金额和 %lld）\n",
           (cnt1 == cnt2 && cnt2 == cnt3 && sum1 == sum2 && sum2 == sum3) ? "true" : "false",
           cnt1, sum1);
    printf("  嵌套循环: %8.0f ms\n", msNested);
    printf("  哈希连接: %8.1f ms（快 %.0fx）\n", msHash, msNested / msHash);
    printf("  归并连接: %8.1f ms（快 %.0fx，含排序成本）\n", msMerge, msNested / msMerge);

    printf("\n== ⑤ 优化器怎么选（三种算法的适用面）==\n");
    printf("  ┌────────────┬──────────────┬────────────┬──────────────────┐\n");
    printf("  │ 算法        │ 复杂度        │ 前提       │ 数据库何时选它    │\n");
    printf("  ├────────────┼──────────────┼────────────┼──────────────────┤\n");
    printf("  │ 嵌套循环    │ O(N×M)       │ 无         │ 小表、或内表有索引 │\n");
    printf("  │ 哈希连接    │ O(N+M)       │ 等值连接   │ 大表等值、内存够  │\n");
    printf("  │ 归并连接    │ O(NlogN+..)  │ 可排序     │ 两边已有序/有索引 │\n");
    printf("  └────────────┴──────────────┴────────────┴──────────────────┘\n");
    printf("  内表有索引时嵌套循环变身 Index Nested Loop: 每行 O(log M)——小结果集之王\n");
    printf("  → 你写的 SQL 不变，优化器按【统计信息】换算法——声明式的全部含义\n");

    printf("\n== ⑥ 与第 20/21 章的呼应 ==\n");
    printf("  哈希连接 = 哈希表（第 20 章 O(1) 查找）用在两表之间\n");
    printf("  归并连接 = 归并排序的合并步（第 19 章双指针）\n");
    printf("  Index NLJ = B 树（第 21 章 O(log n)）逐行下钻\n");
    printf("  → 数据库没有新数据结构，只有把老数据结构用到极致的优化器\n");
    return 0;
}
