"""数据库锁：sqlite 真实的锁行为——以及「延迟事务升级」这个最隐蔽的坑。"""
import os
import sqlite3
import tempfile
import time

DB = os.path.join(tempfile.gettempdir(), f"pl-mastery-lock-{os.getpid()}.db")


def fresh():
    for s in ("", "-wal", "-shm"):
        if os.path.exists(DB + s):
            os.unlink(DB + s)
    con = sqlite3.connect(DB, isolation_level=None)
    con.execute("PRAGMA journal_mode=WAL")
    con.execute("CREATE TABLE account(id INTEGER PRIMARY KEY, balance INTEGER)")
    con.executemany("INSERT INTO account VALUES(?,?)", [(1, 100), (2, 100)])
    return con


def conn(timeout=0.2):
    return sqlite3.connect(DB, isolation_level=None, timeout=timeout)


def try_do(c, sql, label):
    """执行一条语句，返回 (是否成功, 耗时ms, 错误信息)"""
    t0 = time.perf_counter()
    try:
        c.execute(sql)
        return True, (time.perf_counter() - t0) * 1000, ""
    except sqlite3.OperationalError as e:
        return False, (time.perf_counter() - t0) * 1000, str(e)


if __name__ == "__main__":
    A = fresh()
    B = conn()

    print("== ① sqlite 的锁模型：一个写者，多个读者 ==")
    A.execute("BEGIN IMMEDIATE")                      # A 立刻拿到写锁
    A.execute("UPDATE account SET balance = 50 WHERE id = 1")
    ok, ms, err = try_do(B, "BEGIN IMMEDIATE", "B 想写")
    print(f"  A 持有写锁期间，B 尝试开写事务: {'成功' if ok else '✗ 被拒绝'}"
          f"（等了 {ms:.0f} ms）")
    print(f"    错误: {err}")
    t0 = time.perf_counter()
    r = B.execute("SELECT balance FROM account WHERE id = 1").fetchone()[0]
    ms_read = (time.perf_counter() - t0) * 1000
    print(f"  但 B 读同一行: {r}（耗时 {ms_read:.3f} ms，零等待）← WAL 下读不被写阻塞")
    A.execute("ROLLBACK")
    print("  → sqlite 的粒度是【整个数据库】: 写锁只有一把，写者严格排队")
    print("  → 对照 PostgreSQL/MySQL 的【行级锁】: 只有改同一行才互斥（C++ 版实现的就是行锁）")

    print("\n== ② 最隐蔽的坑：BEGIN（延迟）事务的锁升级失败 ==")
    A.execute("BEGIN")                                # DEFERRED：此刻【不拿任何锁】
    v = A.execute("SELECT balance FROM account WHERE id = 1").fetchone()[0]
    print(f"  A: BEGIN（延迟）→ 读到 balance = {v}   ← 此刻 A 只有【读锁】")
    B.execute("BEGIN IMMEDIATE")
    B.execute("UPDATE account SET balance = 999 WHERE id = 1")
    B.execute("COMMIT")
    print("  B: 趁机改成 999 并提交")
    ok, ms, err = try_do(A, f"UPDATE account SET balance = {v - 10} WHERE id = 1", "A 想写")
    print(f"  A: 现在想把读到的值 -10 写回去: {'成功' if ok else '✗ 失败'}")
    print(f"    {err}")
    print("  → 底层原因: A 的快照已经过期——它看到的 100 早就不是最新值了")
    print("     （sqlite 的 C API 在这里返回扩展码 SQLITE_BUSY_SNAPSHOT，Python 只暴露为 database is locked）")
    print("  → 数据库拒绝这次升级，正是在阻止【丢失更新】（第 46 章实测过它的后果）")
    A.execute("ROLLBACK")
    print("  → 解法: 需要「读后写」的事务【一开始就用 BEGIN IMMEDIATE】，别用默认的延迟事务")

    print("\n== ③ 对比：BEGIN IMMEDIATE 从一开始就拿写锁 ==")
    A.execute("BEGIN IMMEDIATE")                      # 一上来就拿写锁
    v = A.execute("SELECT balance FROM account WHERE id = 1").fetchone()[0]
    ok, ms, err = try_do(B, "BEGIN IMMEDIATE", "")
    print(f"  A: BEGIN IMMEDIATE → 读到 {v}；此时 B 想开写事务: {'成功' if ok else '✗ 被挡在门外'}")
    ok2, _, _ = try_do(A, f"UPDATE account SET balance = {v - 10} WHERE id = 1", "")
    A.execute("COMMIT")
    final = A.execute("SELECT balance FROM account WHERE id = 1").fetchone()[0]
    print(f"  A 的写入: {'✓ 成功' if ok2 else '失败'}，最终 balance = {final}")
    print("  → 代价: B 从头到尾被挡住（并发更低）；收益: A 的「读-改-写」不会中途失效")
    print("  → 这就是【悲观锁】: 先占住位置再干活（第 48 章 C# 版量过它与乐观锁的对比）")

    print("\n== ④ busy_timeout：等多久才放弃（实测）==")
    A.execute("BEGIN IMMEDIATE")
    for t in (0.05, 0.2, 0.5):
        C = conn(timeout=t)
        t0 = time.perf_counter()
        try:
            C.execute("BEGIN IMMEDIATE")
            waited = "拿到了锁"
        except sqlite3.OperationalError:
            waited = f"等了 {(time.perf_counter()-t0)*1000:.0f} ms 后放弃"
        print(f"  timeout={t}s: {waited}")
        C.close()
    A.execute("ROLLBACK")
    print("  → timeout 设太小: 正常的锁等待也会失败；设太大: 请求线程被长时间占住（第 45 章池饥饿）")
    print("  → 生产经验: 设成「你能容忍的最长响应时间」，并配合重试")

    print("\n== ⑤ 重试 + 指数退避：拿到 BUSY 之后该怎么办（实测）==")
    A.execute("BEGIN IMMEDIATE")

    def with_retry(c, sql, max_tries=5):
        delay, tries = 0.01, 0
        for i in range(max_tries):
            tries += 1
            ok, _, err = try_do(c, sql, "")
            if ok:
                return True, tries
            if "locked" not in err and "busy" not in err.lower():
                return False, tries                    # 不可重试的错误，立刻放弃
            time.sleep(delay)
            delay *= 2                                 # 指数退避
            if i == 1:
                A.execute("ROLLBACK")                  # 第 2 次重试时 A 恰好提交了
        return False, tries

    D = conn(timeout=0.05)
    ok, tries = with_retry(D, "BEGIN IMMEDIATE")
    print(f"  带退避重试: {'✓ 第 ' + str(tries) + ' 次成功' if ok else '✗ 重试 ' + str(tries) + ' 次仍失败'}")
    if ok:
        D.execute("ROLLBACK")
    D.close()
    print("  → 两条纪律: ① 只重试【可重试】的错误 ② 必须指数退避（否则冲突时雪崩，第 45 章）")

    print("\n== ⑥ 锁与索引的关系（第 49 章在这里收口）==")
    print("  行锁是加在【索引条目】上的，不是加在「行」这个抽象概念上")
    print("  → 没有合适索引时，数据库只能【扫描并锁住扫过的每一行】")
    print("  → 这就是「没有索引的 UPDATE 把整张表锁住」的真正机制")
    print("  → 所以第 49 章的索引不只决定查询速度，还直接决定【锁的范围】和并发度")

    A.close()
    B.close()
    for s in ("", "-wal", "-shm"):
        if os.path.exists(DB + s):
            os.unlink(DB + s)
