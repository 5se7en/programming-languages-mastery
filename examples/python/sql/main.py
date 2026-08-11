"""SQL：参数化不是「防注入的好习惯」，而是三件事——安全、性能、正确性的同一个答案。"""
import os
import sqlite3
import tempfile
import time


def timed(fn):
    t0 = time.perf_counter()
    r = fn()
    return (time.perf_counter() - t0) * 1000, r


if __name__ == "__main__":
    dbp = os.path.join(tempfile.gettempdir(), f"pl-mastery-sql-{os.getpid()}.db")
    con = sqlite3.connect(dbp)
    cur = con.cursor()
    cur.execute("CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, secret TEXT, "
                "payload TEXT, score INTEGER)")
    ROWS = 100_000
    cur.executemany(
        "INSERT INTO users VALUES(?,?,?,?,?)",
        ((i, f"user-{i}", f"secret-{i}", "x" * 200, i % 100) for i in range(ROWS)))
    con.commit()

    print("== ① SQL 注入：字符串拼接是把查询的【结构】交给了用户 ==")
    evil = "' OR '1'='1"
    sql = f"SELECT COUNT(*) FROM users WHERE name = '{evil}'"
    n_concat = cur.execute(sql).fetchone()[0]
    n_param = cur.execute("SELECT COUNT(*) FROM users WHERE name = ?", (evil,)).fetchone()[0]
    print(f"  用户输入: {evil!r}")
    print(f"  拼接版实际执行: {sql}")
    print(f"  拼接版返回 {n_concat} 行（全表泄露！）；参数化版返回 {n_param} 行 ✓")
    print("  → 拼接让输入【改写了 SQL 结构】；参数化把输入钉死为「一个值」——结构与数据分离")

    print("\n== ② 参数化的第二重红利：解析一次，执行万次 ==")
    N = 20_000
    def rebuild_each():
        for i in range(N):
            cur.execute(f"SELECT score FROM users WHERE id = {i}")   # 每次都是新 SQL 文本
            cur.fetchone()
    ms_text, _ = timed(rebuild_each)
    def param_reuse():
        for i in range(N):
            cur.execute("SELECT score FROM users WHERE id = ?", (i,))  # 同一文本，语句缓存命中
            cur.fetchone()
    ms_param, _ = timed(param_reuse)
    print(f"  {N} 次点查，每次拼新 SQL: {ms_text:7.1f} ms（每次都要重新解析+编译执行计划）")
    print(f"  {N} 次点查，参数化复用:   {ms_param:7.1f} ms（sqlite3 的语句缓存按 SQL 文本命中）")
    print(f"  → 快 {ms_text/ms_param:.1f}x —— 服务器数据库差距更大（网络上还省了硬解析）")

    print("\n== ③ 让索引失效只需一个表达式 ==")
    ms_idx, _ = timed(lambda: [cur.execute(
        "SELECT COUNT(*) FROM users WHERE id = ?", (k,)).fetchone() for k in range(200)])
    ms_expr, _ = timed(lambda: [cur.execute(
        "SELECT COUNT(*) FROM users WHERE id + 0 = ?", (k,)).fetchone() for k in range(200)])
    print(f"  WHERE id = ?     200 次: {ms_idx:8.2f} ms（主键 B 树 SEARCH）")
    print(f"  WHERE id + 0 = ? 200 次: {ms_expr:8.1f} ms（表达式包裹 → 全表 SCAN）")
    print(f"  → 慢 {ms_expr/ms_idx:.0f}x —— 优化器无法透过表达式看到列，索引即缴械")
    print("  → 同理: WHERE DATE(created_at) = ... / WHERE UPPER(name) = ... 全是此坑")

    print("\n== ④ 覆盖索引：为什么 SELECT * 永远拿不到最优计划 ==")
    cur.execute("CREATE INDEX idx_cover ON users(score, id)")   # 先建索引，三次查询同等条件
    REP = 30                                                     # 重复采样，消除单次抖动
    def rep(q):
        ms, _ = timed(lambda: [cur.execute(q).fetchall() for _ in range(REP)])
        return ms / REP
    ms_cover = rep("SELECT id FROM users WHERE score = 42")
    ms_cols = rep("SELECT id, name FROM users WHERE score = 42")
    ms_star = rep("SELECT * FROM users WHERE score = 42")
    print(f"  SELECT id（列全在索引里）:        {ms_cover:6.2f} ms ← 覆盖索引，【不回表】")
    print(f"  SELECT id, name（回表取 name）:   {ms_cols:6.2f} ms（慢 {ms_cols/ms_cover:.1f}x）")
    print(f"  SELECT *（回表 + 搬 200B payload）:{ms_star:6.2f} ms（慢 {ms_star/ms_cover:.1f}x）")
    print(f"  计划对比: {cur.execute('EXPLAIN QUERY PLAN SELECT id FROM users WHERE score=42').fetchone()[3]}")
    print(f"            {cur.execute('EXPLAIN QUERY PLAN SELECT * FROM users WHERE score=42').fetchone()[3]}")
    print("  → COVERING INDEX 消失就意味着「回表」——SELECT * 把这个优化永久关闭了")

    print("\n== ⑤ 「三种写法殊途同归」是个流传甚广的【误解】 ==")
    q_in = "SELECT COUNT(*) FROM users WHERE id IN (SELECT id FROM users WHERE score = 7)"
    q_exists = ("SELECT COUNT(*) FROM users u WHERE EXISTS "
                "(SELECT 1 FROM users s WHERE s.id = u.id AND s.score = 7)")
    q_join = ("SELECT COUNT(*) FROM users u JOIN (SELECT id FROM users WHERE score = 7) s "
              "ON s.id = u.id")
    results = {}
    for name, q in [("IN 子查询", q_in), ("EXISTS   ", q_exists), ("JOIN     ", q_join)]:
        ms, r = timed(lambda q=q: cur.execute(q).fetchone()[0])
        results[name] = ms
        print(f"  {name}: {ms:7.1f} ms → {r} 行")
    slow, fast = max(results.values()), min(results.values())
    print(f"  → 答案全部一致，耗时却差 {slow/fast:.0f}x！")
    print("  → 原因（EXPLAIN 已证）: IN/JOIN 走 LIST SUBQUERY（子查询物化一次）")
    print("     而【相关】子查询 EXISTS 走 CORRELATED SCALAR SUBQUERY —— 外层每行重跑一次")
    print("  → 声明式的边界: 优化器能归一很多写法，但归一不掉「相关性」——它得逐行求值")

    print("\n== ⑥ 索引能服务什么条件：LIKE 的两道门槛 ==")
    def plan(q):
        return cur.execute("EXPLAIN QUERY PLAN " + q).fetchone()[3]
    q_pre = "SELECT COUNT(*) FROM users WHERE name LIKE 'user-4200%'"
    q_suf = "SELECT COUNT(*) FROM users WHERE name LIKE '%-4200'"

    cur.execute("CREATE INDEX idx_name ON users(name)")          # 门槛一: 普通索引【不够】
    print("  门槛一 · 排序规则必须匹配:")
    print(f"    普通索引 + 前缀 LIKE: {plan(q_pre)}")
    print("    ↑ 竟然还是 SCAN！因为 LIKE 默认【大小写不敏感】，而索引是 BINARY 排序规则")
    cur.execute("DROP INDEX idx_name")
    cur.execute("CREATE INDEX idx_name ON users(name COLLATE NOCASE)")   # 排序规则对齐
    print(f"    NOCASE 索引 + 前缀 LIKE: {plan(q_pre)}")
    print("    ↑ 现在才是 SEARCH——索引的【排序规则】必须与查询语义一致，否则形同虚设")

    print("  门槛二 · 通配符必须在后面:")
    ms_prefix = rep(q_pre)
    ms_suffix = rep(q_suf)
    print(f"    LIKE 'user-4200%'（前缀）: {ms_prefix:6.3f} ms  计划: {plan(q_pre)}")
    print(f"    LIKE '%-4200'（前导通配）: {ms_suffix:6.3f} ms  计划: {plan(q_suf)}")
    print(f"    → 慢 {ms_suffix/ms_prefix:.0f}x —— B 树按前缀有序（第 21 章），前导 % 让有序性无从下手")
    print("  → 声明式不是魔法: 条件必须【可被索引服务】，优化器才有牌可打")

    con.close()
    os.unlink(dbp)
