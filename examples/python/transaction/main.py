"""事务：用双连接真实复现三种读异常，并量出隔离级别挡住了什么。"""
import os
import sqlite3
import tempfile
import time

DB = os.path.join(tempfile.gettempdir(), f"pl-mastery-tx-{os.getpid()}.db")


def fresh():
    """建库并开 WAL —— WAL 才有真正的快照隔离（读者不被写者阻塞）。"""
    if os.path.exists(DB):
        os.unlink(DB)
    con = sqlite3.connect(DB, isolation_level=None)   # None = 我们自己发 BEGIN/COMMIT
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("CREATE TABLE account(id INTEGER PRIMARY KEY, name TEXT, balance INTEGER)")
    con.executemany("INSERT INTO account VALUES(?,?,?)",
                    [(1, "甲", 100), (2, "乙", 100), (3, "丙", 100)])
    return con


if __name__ == "__main__":
    A = fresh()                                        # 连接 A：读者
    # timeout=0.2 让「拿不到写锁」快速失败——默认要死等 5 秒
    B = sqlite3.connect(DB, isolation_level=None, timeout=0.2)
    read_a = lambda: A.execute("SELECT balance FROM account WHERE id=1").fetchone()[0]

    print("== ① 不可重复读：没有事务包裹时，同一个查询两次结果不同 ==")
    v1 = read_a()
    B.execute("UPDATE account SET balance = 777 WHERE id = 1")     # 另一个连接改了并提交
    v2 = read_a()
    print(f"  A 第一次读: {v1}   B 提交修改后 A 第二次读: {v2}")
    print(f"  → 同一个事务里两次读到不同的值 = 【不可重复读】(non-repeatable read): {v1 != v2}")
    print("  → 后果: 「先查余额够不够，再扣款」中间余额被人改了，你的判断已经过期")

    print("\n== ② 用事务包裹：快照隔离让两次读必然一致（实测）==")
    B.execute("UPDATE account SET balance = 100 WHERE id = 1")     # 复位
    A.execute("BEGIN")                                             # ← A 开启事务
    s1 = read_a()
    B.execute("BEGIN IMMEDIATE")
    B.execute("UPDATE account SET balance = 999 WHERE id = 1")
    B.execute("COMMIT")                                            # B 已经提交了！
    s2 = read_a()
    A.execute("COMMIT")
    s3 = read_a()
    print(f"  事务内第一次读: {s1}")
    print(f"  B 提交 999 后，事务内第二次读: {s2}   ← 仍是旧值！")
    print(f"  A 提交后再读: {s3}")
    print(f"  → 事务里两次读一致: {s1 == s2} —— 这就是【快照隔离】(snapshot isolation)")
    print("  → A 看到的是「事务开始那一刻」的整个数据库快照，B 的提交对它不可见")

    print("\n== ③ 幻读：范围查询在事务内也被快照挡住 ==")
    A.execute("BEGIN")
    n1 = A.execute("SELECT COUNT(*) FROM account WHERE balance >= 100").fetchone()[0]
    B.execute("INSERT INTO account VALUES(4, '丁', 500)")          # B 插入【新行】
    n2 = A.execute("SELECT COUNT(*) FROM account WHERE balance >= 100").fetchone()[0]
    A.execute("COMMIT")
    n3 = A.execute("SELECT COUNT(*) FROM account WHERE balance >= 100").fetchone()[0]
    print(f"  事务内两次 COUNT: {n1} → {n2}（一致: {n1 == n2}）；事务外再查: {n3}")
    print("  → 【幻读】(phantom read) 是「新行凭空出现」，与不可重复读（既有行被改）是两种异常")
    print("  → 快照隔离把两者一起挡住了；而 SQL 标准的 REPEATABLE READ 只挡后者")

    print("\n== ④ 写-写冲突：数据库如何阻止丢失更新 ==")
    A.execute("BEGIN IMMEDIATE")                                   # A 先拿到写锁
    A.execute("UPDATE account SET balance = balance + 1 WHERE id = 2")
    t0 = time.perf_counter()
    try:
        B.execute("BEGIN IMMEDIATE")                               # B 也想写
        B.execute("UPDATE account SET balance = balance + 1 WHERE id = 2")
        conflict = "没冲突（不应该）"
    except sqlite3.OperationalError as e:
        conflict = f"{type(e).__name__}: {e}"
    ms = (time.perf_counter() - t0) * 1000
    A.execute("COMMIT")
    print(f"  A 持写锁期间 B 尝试写: {conflict}")
    print(f"  （B 等了 {ms:.0f} ms 才放弃——本例把 timeout 设成 0.2 秒，默认是 5 秒）")
    print("  → sqlite 用【库级写锁】串行化写者: 同一时刻只有一个写事务")
    print("  → PostgreSQL/MySQL 用【行级锁】: 只有改同一行才互斥，粒度细得多（第 50 章）")

    print("\n== ⑤ 读不被写阻塞：WAL 的核心红利（实测）==")
    B.execute("BEGIN IMMEDIATE")
    B.execute("UPDATE account SET balance = 12345 WHERE id = 3")   # B 持有写锁，未提交
    t0 = time.perf_counter()
    r = A.execute("SELECT balance FROM account WHERE id = 3").fetchone()[0]
    ms_read = (time.perf_counter() - t0) * 1000
    B.execute("ROLLBACK")
    print(f"  B 持有写锁未提交时，A 读同一行: {r}（耗时 {ms_read:.3f} ms，没有等待）")
    print("  → 读到的是【旧版本】——多版本并发控制（MVCC）的直接效果")
    print("  → 传统两阶段锁会让这个读【阻塞到 B 提交】；MVCC 让读写完全不互相阻塞")

    print("\n== ⑥ 原子性 + 保存点：部分回滚 ==")
    A.execute("UPDATE account SET balance = 100 WHERE id IN (1,2)")   # 复位，排除前面实验的影响
    A.execute("BEGIN")
    A.execute("UPDATE account SET balance = 1 WHERE id = 1")
    A.execute("SAVEPOINT sp1")                                     # 打一个存档点
    A.execute("UPDATE account SET balance = 2 WHERE id = 2")
    A.execute("ROLLBACK TO sp1")                                   # 只回滚到存档点
    A.execute("COMMIT")
    rows = A.execute("SELECT id, balance FROM account WHERE id IN (1,2) ORDER BY id").fetchall()
    print(f"  两者原本都是 100；提交后: {rows}")
    print("  → id=1 变成 1（改动在 sp1 之前，保留），id=2 还是 100（改动在 sp1 之后，被撤销）")
    print("  → SAVEPOINT = 事务内的部分回滚，ORM 的「嵌套事务」多半就是它（第 51 章）")

    print("\n== ⑦ 一个事务的完整生命周期 ==")
    print("  BEGIN            → 拿到一个【快照】（读什么版本从此确定）")
    print("  执行读写          → 写操作先进 WAL，读操作按快照过滤版本")
    print("  COMMIT           → 日志 fsync（第 46 章实测三档价格）→ 此刻才对别人可见")
    print("  ROLLBACK/崩溃     → 丢弃未提交的日志记录，别人从头到尾什么都没看见")
    print("  → 「原子」不是「不会失败」，而是【失败也不留痕迹】")

    A.close()
    B.close()
    for suffix in ("", "-wal", "-shm"):
        if os.path.exists(DB + suffix):
            os.unlink(DB + suffix)
