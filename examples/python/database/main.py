"""数据库：用「文件 + 手写代码」和 sqlite 各实现一遍同样的需求，把差距量出来。"""
import fcntl
import multiprocessing as mp
import os
import sqlite3
import tempfile
import time

WORK = os.path.join(tempfile.gettempdir(), f"pl-mastery-db-{os.getpid()}")


def timed(fn):
    t0 = time.perf_counter()
    r = fn()
    return (time.perf_counter() - t0) * 1000, r


# ---- ③ 丢失更新实验的两个 worker（spawn 模式必须放模块顶层，第 39 章的教训）----
def file_incr(path, n):
    done = 0
    while done < n:
        try:
            with open(path) as f:                   # 读-改-写，三步之间毫无保护
                v = int(f.read())
        except ValueError:                          # 撞见另一进程截断后还没写入的【空文件】
            continue                                # 这次读作废，重来（现象本身就是实验结果）
        with open(path, "w") as f:
            f.write(str(v + 1))
        done += 1


def db_incr(path, n):
    con = sqlite3.connect(path, timeout=10)
    for _ in range(n):
        with con:                                   # 一个事务 = 一次原子的读-改-写
            con.execute("UPDATE counter SET n = n + 1")
    con.close()


if __name__ == "__main__":
    os.makedirs(WORK, exist_ok=True)
    rec = b"id=00042,name=zhang,balance=100\n"

    print("== ① 持久化的三档价格：写文件 ≠ 数据落盘 ==")
    p = os.path.join(WORK, "t1.log")
    fd = os.open(p, os.O_WRONLY | os.O_CREAT | os.O_TRUNC)
    N1 = 2000
    ms, _ = timed(lambda: [os.write(fd, rec) for _ in range(N1)])
    print(f"  只 write() {N1} 次:          {ms:8.1f} ms（{ms*1000/N1:7.1f} μs/条）← 数据还在【页缓存】")
    N2 = 200
    def wf():
        for _ in range(N2):
            os.write(fd, rec); os.fsync(fd)
    ms, _ = timed(wf)
    print(f"  write+fsync {N2} 次:        {ms:8.1f} ms（{ms*1000/N2:7.1f} μs/条）← 到了【磁盘缓存】")
    N3 = 50
    def wff():
        for _ in range(N3):
            os.write(fd, rec); fcntl.fcntl(fd, fcntl.F_FULLFSYNC)
    ms, _ = timed(wff)
    print(f"  write+F_FULLFSYNC {N3} 次:  {ms:8.1f} ms（{ms*1000/N3:7.1f} μs/条）← 真正落到【存储介质】")
    os.close(fd)
    print("  → macOS 的 fsync 只刷到磁盘缓存，断电仍会丢！SQLite 用 F_FULLFSYNC 才敢承诺 D")
    print("  → 这就是第 43 章 synchronous=FULL 只慢 1.5x 的谜底：它走的是 F_BARRIERFSYNC 档")
    print("  → 注意: Python 的 os.fsync 是裸 fsync（21μs 档）；Node/.NET 的同名 API")
    print("    在 macOS 上却被运行时升级成 F_FULLFSYNC（3.5ms 档）——见 JS/C# 版实测")

    print("\n== ② 原子性：进程崩在写一半，文件就毁了 ==")
    p2 = os.path.join(WORK, "balance.txt")
    with open(p2, "w") as f:
        f.write("id=1,balance=100\nid=2,balance=100\n")
    with open(p2, "w") as f:                        # 模拟：改写到一半进程被杀
        f.write("id=1,balance=40\nid=2,bal")        # ← 「崩溃」发生在这里
    print(f"  崩溃后文件内容: {open(p2).read()!r}")
    print("  → 甲扣了 60，乙的记录只剩半行——钱凭空消失，文件格式也毁了")
    con = sqlite3.connect(os.path.join(WORK, "bank.db"))
    con.execute("CREATE TABLE account(id INTEGER PRIMARY KEY, balance INTEGER)")
    con.executemany("INSERT INTO account VALUES(?,?)", [(1, 100), (2, 100)])
    con.commit()
    try:
        with con:
            con.execute("UPDATE account SET balance = balance - 60 WHERE id = 1")
            raise RuntimeError("模拟崩溃")          # 事务中途「崩溃」
    except RuntimeError:
        pass
    print(f"  数据库同样中途崩溃后: {con.execute('SELECT id,balance FROM account').fetchall()}")
    print("  → 事务自动回滚，两人余额都是 100 —— 这就是 A（原子性），靠回滚日志/WAL 实现")

    print("\n== ③ 两个进程同时改一份数据 ==")
    p3 = os.path.join(WORK, "counter.txt")
    with open(p3, "w") as f:
        f.write("0")
    EACH = 200
    ps = [mp.Process(target=file_incr, args=(p3, EACH)) for _ in range(2)]
    [x.start() for x in ps]; [x.join() for x in ps]
    got = int(open(p3).read())
    print(f"  文件版: 2 进程 × {EACH} 次自增，期望 {2*EACH}，实际 {got}（丢了 {2*EACH-got} 次更新）")
    dbp = os.path.join(WORK, "counter.db")
    c2 = sqlite3.connect(dbp)
    c2.execute("CREATE TABLE counter(n INTEGER)"); c2.execute("INSERT INTO counter VALUES(0)")
    c2.commit(); c2.close()
    ps = [mp.Process(target=db_incr, args=(dbp, EACH)) for _ in range(2)]
    [x.start() for x in ps]; [x.join() for x in ps]
    got = sqlite3.connect(dbp).execute("SELECT n FROM counter").fetchone()[0]
    print(f"  sqlite: 2 进程 × {EACH} 次自增，期望 {2*EACH}，实际 {got} ✓")
    print("  → 数据库替你做了第 41 章的全部功课：跨进程的锁 + 排队 + 重试")

    print("\n== ④ 查询：十万条里找一条 ==")
    ROWS = 100_000
    p4 = os.path.join(WORK, "users.txt")
    with open(p4, "w") as f:
        for i in range(ROWS):
            f.write(f"id={i},name=user-{i},score={i % 100}\n")
    LOOKUPS = 20
    def scan_file():
        hits = 0
        for k in range(LOOKUPS):
            target = f"id={ROWS - 1 - k},"
            for line in open(p4):                   # 每次查找都从头扫
                if line.startswith(target):
                    hits += 1
                    break
        return hits
    ms_scan, hits = timed(scan_file)
    dbp4 = os.path.join(WORK, "users.db")
    c4 = sqlite3.connect(dbp4)
    c4.execute("CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, score INTEGER)")
    ms_load, _ = timed(lambda: (c4.executemany(
        "INSERT INTO users VALUES(?,?,?)",
        ((i, f"user-{i}", i % 100) for i in range(ROWS))), c4.commit()))
    def q_db():
        return sum(1 for k in range(LOOKUPS)
                   if c4.execute("SELECT name FROM users WHERE id=?", (ROWS - 1 - k,)).fetchone())
    ms_db, _ = timed(q_db)
    print(f"  文件顺序扫描 {LOOKUPS} 次查找: {ms_scan:8.1f} ms（每次 O(n) 读 {ROWS} 行）")
    print(f"  sqlite 主键查 {LOOKUPS} 次:    {ms_db:8.3f} ms（B 树索引 O(log n)，第 49 章）")
    print(f"  → 快 {ms_scan/ms_db:.0f}x；一次性建库耗时 {ms_load:.0f} ms，之后每次查询都受益")

    print("\n== ⑤ 聚合：换个问题，文件代码全部重写 ==")
    def agg_file():
        best = {}
        for line in open(p4):
            parts = dict(kv.split("=") for kv in line.strip().split(","))
            s = int(parts["score"])
            best[s] = best.get(s, 0) + 1
        return max(best.items())
    ms_f, r1 = timed(agg_file)
    ms_s, r2 = timed(lambda: c4.execute(
        "SELECT score, COUNT(*) FROM users GROUP BY score ORDER BY score DESC LIMIT 1").fetchone())
    print(f"  文件版手写聚合: {ms_f:7.1f} ms（15 行解析+分组代码，换个需求再写 15 行）")
    print(f"  SQL 一句 GROUP BY: {ms_s:7.1f} ms（声明「要什么」，怎么算是数据库的事——第 47 章）")
    print(f"  结果一致: {tuple(r1) == tuple(r2)}")

    print("\n== ⑥ 你刚才差点手写出一个数据库 ==")
    print("  持久化(①) + 原子性(②) + 并发控制(③) + 索引(④) + 查询语言(⑤)")
    print("  = 把这五件事的手写代码合起来，就是一个数据库的雏形")
    print("  → SQLite 约 15 万行 C 把它们做到工业级——这就是「为什么需要数据库」的全部答案")

    import shutil
    shutil.rmtree(WORK, ignore_errors=True)
