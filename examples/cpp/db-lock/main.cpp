// 数据库锁：手写一个锁管理器——相容性判断 + 等待图找环 + 选受害者，MySQL 每天在做的事。
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

enum class Mode { S, X };                       // S = 共享锁（读），X = 排他锁（写）
static const char* name(Mode m) { return m == Mode::S ? "S" : "X"; }

/// 相容矩阵：只有「读读」相容，其余三种组合都互斥
static bool compatible(Mode held, Mode want) {
    return held == Mode::S && want == Mode::S;
}

struct Holder { int txn; Mode mode; };

class LockManager {
public:
    /// 尝试加锁。成功返回 true；被阻塞返回 false 并记入等待图
    bool acquire(int txn, const std::string& res, Mode want) {
        auto& hs = holders_[res];
        for (const auto& h : hs) {
            if (h.txn == txn) return true;                  // 已经持有（简化：不做锁升级）
            if (!compatible(h.mode, want)) {
                waits_for_res_[txn] = res;                  // 记录「我在等哪个资源」
                return false;
            }
        }
        hs.push_back({txn, want});
        waits_for_res_.erase(txn);
        return true;
    }

    void release_all(int txn) {
        for (auto& [res, hs] : holders_)
            hs.erase(std::remove_if(hs.begin(), hs.end(),
                                    [txn](const Holder& h) { return h.txn == txn; }),
                     hs.end());
        waits_for_res_.erase(txn);
    }

    /// 等待图：事务 A 等资源 R，而 R 被 B 持有 → 边 A→B
    std::map<int, std::set<int>> wait_for_graph() const {
        std::map<int, std::set<int>> g;
        for (const auto& [txn, res] : waits_for_res_) {
            auto it = holders_.find(res);
            if (it == holders_.end()) continue;
            for (const auto& h : it->second)
                if (h.txn != txn) g[txn].insert(h.txn);
        }
        return g;
    }

    /// 在等待图里找环 —— 找到就是死锁。返回环上的事务
    std::vector<int> detect_deadlock() const {
        auto g = wait_for_graph();
        std::set<int> visiting, done;
        std::vector<int> path, cycle;
        for (const auto& [start, _] : g) {
            if (done.count(start)) continue;
            if (dfs(g, start, visiting, done, path, cycle)) return cycle;
        }
        return {};
    }

    /// 选受害者：持锁最少的那个（回滚代价最小）—— InnoDB 的策略同源
    int pick_victim(const std::vector<int>& cycle) const {
        int best = cycle.front(), bestCount = 1 << 30;
        for (int t : cycle) {
            int c = 0;
            for (const auto& [res, hs] : holders_)
                for (const auto& h : hs) if (h.txn == t) ++c;
            if (c < bestCount) { bestCount = c; best = t; }
        }
        return best;
    }

    void dump() const {
        printf("    锁表: ");
        for (const auto& [res, hs] : holders_) {
            if (hs.empty()) continue;
            printf("%s{", res.c_str());
            for (size_t i = 0; i < hs.size(); ++i)
                printf("%sT%d:%s", i ? "," : "", hs[i].txn, name(hs[i].mode));
            printf("} ");
        }
        printf("\n");
    }

private:
    bool dfs(const std::map<int, std::set<int>>& g, int u, std::set<int>& visiting,
             std::set<int>& done, std::vector<int>& path, std::vector<int>& cycle) const {
        visiting.insert(u);
        path.push_back(u);
        auto it = g.find(u);
        if (it != g.end())
            for (int v : it->second) {
                if (visiting.count(v)) {                    // 回到了正在访问的节点 = 有环
                    auto pos = std::find(path.begin(), path.end(), v);
                    cycle.assign(pos, path.end());
                    return true;
                }
                if (!done.count(v) && dfs(g, v, visiting, done, path, cycle)) return true;
            }
        visiting.erase(u);
        done.insert(u);
        path.pop_back();
        return false;
    }

    std::map<std::string, std::vector<Holder>> holders_;
    std::map<int, std::string> waits_for_res_;
};

int main() {
    printf("== ① 相容矩阵：四种组合里只有一种能共存 ==\n");
    printf("  ┌──────────┬────────┬────────┐\n");
    printf("  │ 已持有 \\ 想要 │   S    │   X    │\n");
    printf("  ├──────────┼────────┼────────┤\n");
    for (Mode held : {Mode::S, Mode::X}) {
        printf("  │    %s      │", name(held));
        for (Mode want : {Mode::S, Mode::X})
            printf("  %s   │", compatible(held, want) ? "✓相容" : "✗互斥");
        printf("\n");
    }
    printf("  └──────────┴────────┴────────┘\n");
    printf("  → 「读读不冲突，读写/写读/写写都冲突」——一条规则推出全部锁行为\n");
    printf("  → 注意 MVCC（第 48 章）让【读】完全不加锁，所以真实数据库里 S 锁比想象中少见\n");
    printf("     S 锁主要出现在 SELECT ... FOR SHARE 和外键检查等显式场景\n");

    printf("\n== ② 正常情况：读读并发，写写排队 ==\n");
    {
        LockManager lm;
        printf("  T1 请求 行A 的 S 锁: %s\n", lm.acquire(1, "行A", Mode::S) ? "✓ 成功" : "✗ 阻塞");
        printf("  T2 请求 行A 的 S 锁: %s   ← 读读并发\n",
               lm.acquire(2, "行A", Mode::S) ? "✓ 成功" : "✗ 阻塞");
        printf("  T3 请求 行A 的 X 锁: %s   ← 写要等读完\n",
               lm.acquire(3, "行A", Mode::X) ? "✓ 成功" : "✗ 阻塞");
        lm.dump();
        lm.release_all(1); lm.release_all(2);
        printf("  T1、T2 提交释放锁后，T3 重试: %s\n",
               lm.acquire(3, "行A", Mode::X) ? "✓ 成功" : "✗ 阻塞");
    }

    printf("\n== ③ 钥匙实验：造一个死锁，并让程序自己发现它 ==\n");
    {
        LockManager lm;
        printf("  剧本（两个事务【以相反顺序】访问两行——死锁的经典配方）:\n");
        printf("    T1: 锁住 行A → 然后想要 行B\n");
        printf("    T2: 锁住 行B → 然后想要 行A\n\n");
        printf("  T1 acquire(行A, X): %s\n", lm.acquire(1, "行A", Mode::X) ? "✓" : "✗ 阻塞");
        printf("  T2 acquire(行B, X): %s\n", lm.acquire(2, "行B", Mode::X) ? "✓" : "✗ 阻塞");
        printf("  T1 acquire(行B, X): %s   ← 行B 被 T2 占着\n",
               lm.acquire(1, "行B", Mode::X) ? "✓" : "✗ 阻塞");
        printf("  T2 acquire(行A, X): %s   ← 行A 被 T1 占着\n",
               lm.acquire(2, "行A", Mode::X) ? "✓" : "✗ 阻塞");
        lm.dump();

        printf("\n  等待图:\n");
        for (const auto& [from, tos] : lm.wait_for_graph())
            for (int to : tos) printf("    T%d ──等待──> T%d\n", from, to);

        auto cycle = lm.detect_deadlock();
        if (!cycle.empty()) {
            printf("  ⚠️ 检测到【环】: ");
            for (size_t i = 0; i < cycle.size(); ++i) printf("T%d → ", cycle[i]);
            printf("T%d  = 死锁\n", cycle.front());
            int victim = lm.pick_victim(cycle);
            printf("  → 选受害者: T%d（持锁最少，回滚代价最小）\n", victim);
            lm.release_all(victim);
            printf("  → 回滚 T%d 释放它的锁后，另一个事务立刻能推进: %s\n", victim,
                   lm.acquire(victim == 1 ? 2 : 1, victim == 1 ? "行A" : "行B", Mode::X)
                       ? "✓ 成功" : "✗ 仍阻塞");
        } else {
            printf("  没有检测到死锁（不应该）\n");
        }
    }

    printf("\n== ④ 死锁的四个必要条件（第 41 章的四条，在数据库里原样成立）==\n");
    printf("  ① 互斥      —— X 锁天然互斥（① 的相容矩阵）\n");
    printf("  ② 持有并等待 —— 事务持有 行A 的同时去等 行B（③ 的剧本）\n");
    printf("  ③ 不可剥夺   —— 锁只能由持有者提交/回滚时释放\n");
    printf("  ④ 循环等待   —— T1→T2→T1（③ 的等待图找到的环）\n");
    printf("  → 数据库【放弃】破坏前三个条件（它们是正确性的基础），\n");
    printf("     转而【接受死锁发生，然后检测并回滚一方】——这是与第 41 章最大的不同\n");
    printf("  → 你能做的是破坏第 ④ 条: 让所有事务【按同一顺序】访问行\n");

    printf("\n== ⑤ 破坏循环等待：固定顺序（唯一实用的预防手段）==\n");
    {
        LockManager lm;
        printf("  同样两个事务，但都按【行A → 行B】的固定顺序访问:\n");
        printf("  T1 acquire(行A): %s\n", lm.acquire(1, "行A", Mode::X) ? "✓" : "✗ 阻塞");
        printf("  T2 acquire(行A): %s   ← T2 在第一步就等待，还没持有任何锁\n",
               lm.acquire(2, "行A", Mode::X) ? "✓" : "✗ 阻塞");
        printf("  T1 acquire(行B): %s\n", lm.acquire(1, "行B", Mode::X) ? "✓" : "✗ 阻塞");
        auto cycle = lm.detect_deadlock();
        printf("  检测死锁: %s\n", cycle.empty() ? "✓ 无环——不可能死锁" : "有环");
        printf("  → T2 在【还没持有任何锁】时就开始等 → 破坏了「持有并等待 + 循环」\n");
        printf("  → 实践: 转账时永远【按账号 id 从小到大】加锁，而不是「先扣款方后收款方」\n");
    }

    printf("\n== ⑥ 锁粒度：范围越大越安全，也越慢 ==\n");
    printf("  行锁   : 只锁命中的行     → 并发最好，锁对象最多（内存开销）\n");
    printf("  间隙锁 : 锁住行【之间的空隙】→ 挡住幻读（第 48 章），但极易制造意外死锁\n");
    printf("  表锁   : 锁住整张表        → 并发最差，但锁对象只有一个\n");
    printf("  → InnoDB 在【找不到合适索引】时会把行锁退化成【锁住扫描过的所有行】\n");
    printf("     这是「没有索引的 UPDATE 把整张表锁住」这个经典事故的真正原因\n");
    printf("     → 第 49 章的索引不只影响查询速度，还直接决定【锁的范围】\n");
    return 0;
}
