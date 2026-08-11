// 事务：手写一个 MVCC 引擎——搞懂「读不阻塞写」到底是怎么做到的。
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

// ---------- 一行数据的一个【版本】 ----------
struct Version {
    int value;
    uint64_t created_by;    // 哪个事务创建了它
    uint64_t deleted_by;    // 哪个事务删除了它（0 = 还活着）
};

// ---------- 最小 MVCC 存储引擎 ----------
class MvccStore {
public:
    uint64_t begin() { return ++txn_counter_; }          // 开事务 = 拿一个递增的事务号（快照）

    /** 可见性规则：MVCC 的全部秘密就这三行 */
    bool visible(const Version& v, uint64_t snapshot) const {
        if (v.created_by > snapshot) return false;                       // 我开始之后才创建的，看不见
        if (v.deleted_by != 0 && v.deleted_by <= snapshot) return false; // 我开始之前就删了的，看不见
        return true;
    }

    int read(const std::string& key, uint64_t snapshot) const {
        auto it = data_.find(key);
        if (it == data_.end()) return -1;
        // 从新到旧找第一个对我可见的版本
        for (auto rit = it->second.rbegin(); rit != it->second.rend(); ++rit)
            if (visible(*rit, snapshot)) return rit->value;
        return -1;
    }

    /** 写 = 把旧版本标记删除 + 追加新版本（【从不原地修改】——与第 46 章的追加日志同源） */
    void write(const std::string& key, int value, uint64_t txn) {
        auto& versions = data_[key];
        for (auto& v : versions)
            if (v.deleted_by == 0) v.deleted_by = txn;
        versions.push_back({value, txn, 0});
    }

    size_t version_count(const std::string& key) const {
        auto it = data_.find(key);
        return it == data_.end() ? 0 : it->second.size();
    }

    void dump(const std::string& key) const {
        auto it = data_.find(key);
        if (it == data_.end()) return;
        printf("    %s 的版本链: ", key.c_str());
        for (const auto& v : it->second)
            printf("[值=%d 生于T%llu 死于T%s] ", v.value,
                   (unsigned long long)v.created_by,
                   v.deleted_by ? std::to_string(v.deleted_by).c_str() : "-");
        printf("\n");
    }

private:
    std::map<std::string, std::vector<Version>> data_;
    uint64_t txn_counter_ = 0;
};

int main() {
    printf("== ① MVCC 的核心：写不覆盖旧值，而是【追加新版本】==\n");
    MvccStore db;
    uint64_t t0 = db.begin();
    db.write("balance", 100, t0);                 // 初始值由 T1 写入
    printf("  T%llu 写入 balance=100\n", (unsigned long long)t0);
    db.dump("balance");

    printf("\n== ② 快照隔离：读者拿到的是「事务开始那一刻」的世界 ==\n");
    uint64_t reader = db.begin();                 // 读者 T2：快照 = 2
    printf("  T%llu（读者）开始，第一次读 balance = %d\n",
           (unsigned long long)reader, db.read("balance", reader));

    uint64_t writer = db.begin();                 // 写者 T3
    db.write("balance", 999, writer);
    printf("  T%llu（写者）把 balance 改成 999 并提交\n", (unsigned long long)writer);
    db.dump("balance");

    int again = db.read("balance", reader);
    printf("  T%llu（读者）第二次读 balance = %d   ← 仍是旧值！\n",
           (unsigned long long)reader, again);
    printf("  → 可见性规则挡住了它: 新版本 created_by=T%llu > 快照 T%llu\n",
           (unsigned long long)writer, (unsigned long long)reader);

    uint64_t fresh = db.begin();
    printf("  T%llu（新事务）读 balance = %d   ← 新事务才看得见\n",
           (unsigned long long)fresh, db.read("balance", fresh));

    printf("\n== ③ 为什么读【永远不会】被写阻塞 ==\n");
    printf("  读者要的旧版本【还在版本链里】，写者追加的新版本不碰它\n");
    printf("  → 读操作全程【不需要任何锁】：只做版本链遍历 + 可见性判断\n");
    printf("  → 对照两阶段锁(2PL): 读者要拿共享锁、写者要拿排他锁，二者互斥 → 读写互相阻塞\n");
    printf("  → 这就是 Python 版实测「B 持写锁未提交时 A 照样瞬间读到旧值」的底层原因\n");

    printf("\n== ④ 可见性规则的三行代码（MVCC 的全部秘密）==\n");
    printf("  if (v.created_by > snapshot)                    return false;  // 未来的版本\n");
    printf("  if (v.deleted_by != 0 && v.deleted_by <= snap)  return false;  // 已成过去的版本\n");
    printf("  return true;                                                   // 恰好属于我这个快照\n");
    printf("  → 三行判断替代了整套读锁——这是数据库并发控制近三十年最重要的一次简化\n");

    printf("\n== ⑤ MVCC 的代价：版本会堆积 ==\n");
    for (int i = 0; i < 100; ++i) {
        uint64_t t = db.begin();
        db.write("balance", 1000 + i, t);
    }
    printf("  再写 100 次之后，balance 的版本链长度: %zu\n", db.version_count("balance"));
    printf("  → 旧版本不能立刻删: 只要还有事务的快照可能看见它，就得留着\n");
    printf("  → PostgreSQL 靠 VACUUM 清理死版本；不清理会「表膨胀」(table bloat)\n");
    printf("  → MySQL/InnoDB 把旧版本放在 undo log 里，由 purge 线程回收\n");
    printf("  → 长事务是 MVCC 的天敌: 一个开了几小时的事务，会卡住这几小时里所有的版本回收\n");

    printf("\n== ⑥ 写-写冲突 MVCC 挡不住，还得靠锁 ==\n");
    uint64_t x = db.begin(), y = db.begin();
    db.write("seat", 1, x);                       // 两个事务都想订同一个座位
    db.write("seat", 2, y);
    printf("  T%llu 和 T%llu 都写了 seat，版本链长度 %zu —— 【后写的赢，前一个悄悄没了】\n",
           (unsigned long long)x, (unsigned long long)y, db.version_count("seat"));
    printf("  → 本例的玩具引擎没有冲突检测，所以丢失更新真的发生了\n");
    printf("  → 真实数据库两种解法:\n");
    printf("     悲观: 写之前加行锁，第二个写者【等待】（MySQL 默认，第 50 章）\n");
    printf("     乐观: 提交时检查「我读到的版本还是最新的吗」，不是就【报错回滚】（PostgreSQL SI）\n");
    printf("  → 所以 MVCC 只解决了【读写并发】，写写并发仍然需要锁或冲突检测\n");

    printf("\n== ⑦ 与前面章节的呼应 ==\n");
    printf("  第 36 章 GC: 「还有人引用就不能回收」——MVCC 的旧版本回收是同一个判定问题\n");
    printf("  第 46 章 WAL: 「从不原地修改，只追加」——MVCC 的版本链是同一个思想\n");
    printf("  第 41 章 无锁: 「读路径不加锁」——MVCC 是数据库版的读-拷贝-更新(RCU)\n");
    return 0;
}
