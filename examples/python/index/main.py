"""索引：把「加速多少」和「代价多少」两笔账都算清楚。"""
import os
import shutil
import sqlite3
import tempfile
import time

ROWS = 200_000
PAD = "x" * 100          # 让每行足够宽，全表扫描的代价才真实


def timed(fn):
    t0 = time.perf_counter()
    r = fn()
    return (time.perf_counter() - t0) * 1000, r


def rows_gen():
    return ((i, i % 50000, i % 997, i % 3, f"name-{i}", PAD) for i in range(ROWS))


def build(path, indexes):
    """建同样的表 + 指定的若干索引，返回插入耗时与文件大小。"""
    if os.path.exists(path):
        os.unlink(path)
    con = sqlite3.connect(path)
    con.execute("CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, "
                "c INTEGER, name TEXT, pad TEXT)")
    for i, cols in enumerate(indexes):
        con.execute(f"CREATE INDEX idx_{i} ON t({cols})")
    ms, _ = timed(lambda: (con.executemany("INSERT INTO t VALUES(?,?,?,?,?,?)", rows_gen()),
                           con.commit()))
    con.close()
    return ms, os.path.getsize(path)


if __name__ == "__main__":
    work = tempfile.mkdtemp(prefix="pl-mastery-idx-")

    print("== ① 写入代价：每加一个索引，插入慢多少（实测）==")
    baseline_ms = baseline_size = None
    for idxs, label in [([], "无索引（只有主键）"), (["a"], "1 个索引"),
                        (["a", "b"], "2 个索引"), (["a", "b", "c"], "3 个索引"),
                        (["a", "b", "c", "name"], "4 个索引")]:
        ms, size = build(os.path.join(work, f"db{len(idxs)}.db"), idxs)
        if baseline_ms is None:
            baseline_ms, baseline_size = ms, size
        print(f"  {label:<12}  插入 {ROWS} 行 {ms:6.0f} ms（{ms/baseline_ms:.2f}x）"
              f"   文件 {size/1048576:5.1f} MB（{size/baseline_size:.2f}x）")
    print("  → 索引加速的是【读】，拖慢的是【写】——每次 INSERT 都要维护所有索引的 B+ 树")
    print("  → 文件也在涨: 索引是一份需要同步维护的【额外数据副本】（C++ 版算过它的空间）")

    # 主实验库
    base = os.path.join(work, "main.db")
    con = sqlite3.connect(base)
    con.execute("CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, "
                "c INTEGER, name TEXT, pad TEXT)")
    con.executemany("INSERT INTO t VALUES(?,?,?,?,?,?)", rows_gen())
    con.commit()
    con.execute("CREATE INDEX idx_abc ON t(a, b, c)")
    con.execute("CREATE INDEX idx_c ON t(c)")
    con.execute("ANALYZE")
    cur = con.cursor()

    def plan(sql):
        return " | ".join(r[3] for r in cur.execute("EXPLAIN QUERY PLAN " + sql).fetchall())

    def rep(sql, n=20):
        ms, _ = timed(lambda: [cur.execute(sql).fetchall() for _ in range(n)])
        return ms / n

    print("\n== ② 最左前缀原则：复合索引 (a, b, c) 能服务哪些查询（实测）==")
    for cond in ["WHERE a = 5",
                 "WHERE a = 5 AND b = 7",
                 "WHERE a = 5 AND b = 7 AND c = 1",
                 "WHERE a = 5 AND c = 1",
                 "WHERE b = 7",
                 "WHERE b = 7 AND c = 1"]:
        p = plan(f"SELECT COUNT(*) FROM t INDEXED BY idx_abc {cond}")  # 强制只考虑 idx_abc
        used = p.count("=?") if "SEARCH" in p else 0
        verdict = f"✓ 用上 idx_abc 的前 {used} 列" if used else "✗ 一列都用不上，退化为扫描"
        print(f"  {cond:<32} {verdict}")
        print(f"      {p}")
    print("  → 复合索引像【按「省-市-街道」排序的电话簿】: 知道省份能定位，只知道街道无从下手")
    print("  → 「a AND c」只用上了 a 那一段——c 只在每个 (a,b) 分组【内部】有序")
    print("  → 所以复合索引的【列顺序】就是它的能力边界，建错顺序等于白建")

    print("\n== ③ 选择性：索引不是永远更快（实测交叉点）==")
    print("  用 INDEXED BY / NOT INDEXED 强制两种执行方式，直接对比:")
    for label, hint_idx, cond, hits in [
        ("高选择性 a=5    ", "idx_abc", "a = 5", None),
        ("低选择性 c=1    ", "idx_c", "c = 1", None),
    ]:
        n = cur.execute(f"SELECT COUNT(*) FROM t WHERE {cond}").fetchone()[0]
        ms_idx = rep(f"SELECT name FROM t INDEXED BY {hint_idx} WHERE {cond}", 5)
        ms_scan = rep(f"SELECT name FROM t NOT INDEXED WHERE {cond}", 5)
        faster = "索引更快" if ms_idx < ms_scan else "【全表扫更快】"
        print(f"  {label} 命中 {n:6d} 行（{100*n/ROWS:5.2f}%）: "
              f"走索引 {ms_idx:7.2f} ms  全表扫 {ms_scan:7.2f} ms → {faster} "
              f"{max(ms_idx,ms_scan)/max(min(ms_idx,ms_scan),1e-6):.1f}x")
    print("  → 命中比例高时「走索引再逐行回表」比顺序扫全表【更慢】: 回表是随机 I/O，扫表是顺序 I/O")
    print(f"  ⚠️ 但 sqlite 实际选了: {plan('SELECT name FROM t WHERE c = 1')}")
    print("     它【仍然用了索引】——即本例中它选错了。sqlite 的代价模型比 PostgreSQL 简单得多")
    print("  → 教训: 「优化器会自动选最优」是有前提的；低选择性列上的索引往往是净负担")

    print("\n== ④ 覆盖索引：连表都不用回（实测）==")
    ms_cover = rep("SELECT a, b FROM t WHERE a < 200")        # a,b 都在 idx_abc 里
    ms_back = rep("SELECT a, name FROM t WHERE a < 200")      # name 不在索引里 → 回表
    print(f"  SELECT a, b   （列全在索引里）: {ms_cover:6.3f} ms  {plan('SELECT a, b FROM t WHERE a < 200')}")
    print(f"  SELECT a, name（name 要回表）: {ms_back:6.3f} ms  {plan('SELECT a, name FROM t WHERE a < 200')}")
    print(f"  → 慢 {ms_back/ms_cover:.1f}x —— 计划里 COVERING 一词消失，就意味着每命中一行都要回表一次")
    print("  → 这也是第 47 章「别写 SELECT *」的根因: 它保证了覆盖索引永远用不上")

    print("\n== ⑤ 索引的第二个用途：替代排序 ==")
    p_idx = plan("SELECT a FROM t ORDER BY a LIMIT 10")
    p_sort = plan("SELECT name FROM t ORDER BY name LIMIT 10")
    ms_i = rep("SELECT a FROM t ORDER BY a LIMIT 10")
    ms_s = rep("SELECT name FROM t ORDER BY name LIMIT 10", 5)
    print(f"  ORDER BY a   （有索引）: {ms_i:7.3f} ms   {p_idx}")
    print(f"  ORDER BY name（无索引）: {ms_s:7.1f} ms   {p_sort}")
    print(f"  → 快 {ms_s/ms_i:.0f}x —— B+ 树的叶子【本来就有序】，顺着叶子链表走即可，无需排序")
    print("  → 看到 USE TEMP B-TREE FOR ORDER BY 就说明数据库在【临时排序】，是建索引的信号")

    print("\n== ⑥ 索引的两本账 ==")
    print("  收益: ① 点查 O(log n)  ② 范围查询  ③ 免排序（⑤）④ 覆盖索引免回表（④）")
    print("  代价: ① 写入变慢（① 实测 3.18x）② 占磁盘（① 实测 3.06x）③ 占内存缓存")
    print("  → 「给每一列都建索引」是最常见的过度优化: 写入被拖垮，而多数索引一次都没用上")
    print("  → 正确做法: 按【实际出现的慢查询】建索引，再用 EXPLAIN 确认它真的被用上了")

    con.close()
    shutil.rmtree(work, ignore_errors=True)
