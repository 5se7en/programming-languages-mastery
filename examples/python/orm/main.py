"""ORM：手写一个微型 ORM，把 N+1 从「一次属性访问」里抓出来——用真实 SQL 计数。"""
import os
import sqlite3
import tempfile
import time

DB = os.path.join(tempfile.gettempdir(), f"pl-mastery-orm-{os.getpid()}.db")
QUERY_LOG = []                      # 用 sqlite 的 trace 回调记录【每一条真实执行的 SQL】


def setup():
    if os.path.exists(DB):
        os.unlink(DB)
    con = sqlite3.connect(DB)
    con.executescript("""
        CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, city TEXT);
        CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER, amount INTEGER);
    """)
    con.executemany("INSERT INTO users VALUES(?,?,?)",
                    ((i, f"user-{i}", f"city-{i % 10}") for i in range(200)))
    con.executemany("INSERT INTO orders VALUES(?,?,?)",
                    ((i, i % 200, i % 500) for i in range(4000)))
    con.commit()
    con.set_trace_callback(lambda s: QUERY_LOG.append(s))   # ← 装上「SQL 计数器」
    return con


# ---------- 一个微型 ORM：40 行 ----------
class Model:
    """所有实体的基类：把「类 ↔ 表」「字段 ↔ 列」的映射做出来。"""
    __table__ = ""
    __fields__ = ()

    def __init__(self, **kw):
        for f in self.__fields__:
            setattr(self, f, kw.get(f))
        self._session = None

    @classmethod
    def _from_row(cls, row, session):
        obj = cls(**dict(zip(cls.__fields__, row)))
        obj._session = session
        return obj

    def __repr__(self):
        return f"{type(self).__name__}({', '.join(f'{f}={getattr(self, f)!r}' for f in self.__fields__)})"


class User(Model):
    __table__ = "users"
    __fields__ = ("id", "name", "city")

    @property
    def orders(self):
        """⚠️ 看起来只是一次属性访问，实际上是【一条 SQL】——N+1 的源头就在这里"""
        return self._session.query(Order, where=f"user_id = {self.id}")


class Order(Model):
    __table__ = "orders"
    __fields__ = ("id", "user_id", "amount")


class Session:
    """身份映射 + 脏检查 —— ORM 的另外两件核心工作。"""

    def __init__(self, con):
        self.con = con
        self.identity_map = {}          # (类, 主键) → 对象；保证「同一行只有一个对象」
        self.snapshots = {}             # 对象 id → 加载时的字段快照；用于脏检查

    def query(self, cls, where="1=1"):
        sql = f"SELECT {', '.join(cls.__fields__)} FROM {cls.__table__} WHERE {where}"
        out = []
        for row in self.con.execute(sql):
            key = (cls, row[0])
            if key in self.identity_map:            # 身份映射命中 → 复用同一个对象
                out.append(self.identity_map[key])
                continue
            obj = cls._from_row(row, self)
            self.identity_map[key] = obj
            self.snapshots[id(obj)] = {f: getattr(obj, f) for f in cls.__fields__}
            out.append(obj)
        return out

    def dirty(self):
        """脏检查：把当前字段值与加载时的快照逐一比对"""
        changed = []
        for (cls, pk), obj in self.identity_map.items():
            snap = self.snapshots[id(obj)]
            diff = {f: (snap[f], getattr(obj, f))
                    for f in cls.__fields__ if snap[f] != getattr(obj, f)}
            if diff:
                changed.append((obj, diff))
        return changed

    def flush(self):
        """把脏对象的改动生成 UPDATE —— 只更新【真正变了的列】"""
        stmts = []
        for obj, diff in self.dirty():
            sets = ", ".join(f"{f} = {v[1]!r}" for f, v in diff.items())
            stmts.append(f"UPDATE {type(obj).__table__} SET {sets} WHERE id = {obj.id}")
        return stmts


def count_queries(fn):
    QUERY_LOG.clear()
    t0 = time.perf_counter()
    r = fn()
    return len(QUERY_LOG), (time.perf_counter() - t0) * 1000, r


if __name__ == "__main__":
    con = setup()
    s = Session(con)

    print("== ① 一次属性访问 = 一条 SQL：N+1 的诞生现场 ==")
    def lazy():
        total = 0
        for u in s.query(User):                 # 1 条 SQL 查所有用户
            total += sum(o.amount for o in u.orders)   # ← 每个用户【又一条】SQL
        return total
    n_lazy, ms_lazy, total_lazy = count_queries(lazy)
    print(f"  代码写法: for u in users: sum(o.amount for o in u.orders)")
    print(f"  → 实际执行了 {n_lazy} 条 SQL，耗时 {ms_lazy:.1f} ms（合计 {total_lazy}）")
    print(f"  前 3 条 SQL:")
    for q in QUERY_LOG[:3]:
        print(f"    {q}")
    print("  ⚠️ 代码里【一个 for 循环 + 一次属性访问】，生成了 1 + N 条查询")
    print("  → 这就是 N+1 最阴险的地方: 它在源码里【完全看不出来】")

    print("\n== ② 改成一条 JOIN（预加载）==")
    def eager():
        sql = ("SELECT u.id, u.name, u.city, o.amount FROM users u "
               "LEFT JOIN orders o ON o.user_id = u.id")
        return sum(r[3] or 0 for r in con.execute(sql))
    n_eager, ms_eager, total_eager = count_queries(eager)
    print(f"  → 实际执行了 {n_eager} 条 SQL，耗时 {ms_eager:.1f} ms（合计 {total_eager}）")
    print(f"  → SQL 条数少 {n_lazy // n_eager} 倍，耗时快 {ms_lazy / ms_eager:.1f}x，结果一致: "
          f"{total_lazy == total_eager}")
    print("  → 这就是 ORM 的 eager loading / DataLoader 在做的事（第 47 章实测过 51x）")

    print("\n== ③ 身份映射：同一行永远只有一个对象 ==")
    s2 = Session(con)
    a = s2.query(User, "id = 1")[0]
    b = s2.query(User, "id = 1")[0]
    print(f"  两次查询同一行: a is b = {a is b}（身份映射命中，第二次没有新建对象）")
    a.name = "改过的名字"
    print(f"  改 a 之后 b.name = {b.name!r}   ← 因为它们【就是同一个对象】")
    print("  → 没有身份映射会怎样: 同一行加载出两个对象，改了一个另一个还是旧值")
    print("  → 这是 ORM 替你解决的第二个「阻抗失配」: 数据库有【主键】，对象有【引用相等】")

    print("\n== ④ 脏检查：ORM 怎么知道该更新哪些列 ==")
    dirty = s2.dirty()
    print(f"  当前脏对象数: {len(dirty)}")
    for obj, diff in dirty:
        for f, (old, new) in diff.items():
            print(f"    {type(obj).__name__}(id={obj.id}).{f}: {old!r} → {new!r}")
    print(f"  生成的 SQL: {s2.flush()}")
    print("  → 只更新【真正变了的列】，而不是把整行 UPDATE 一遍")
    print("  → 实现方式: 加载时存一份快照，提交时逐字段比对（本例 5 行代码）")

    print("\n== ⑤ ORM 到底替你省了什么（把手写代码摆出来）==")
    print("  没有 ORM，把一行变成对象要写:")
    print("    row = cur.execute('SELECT id, name, city FROM users WHERE id=?', (i,)).fetchone()")
    print("    u = User(); u.id = row[0]; u.name = row[1]; u.city = row[2]")
    print("  有 ORM: session.query(User, 'id = 1')")
    print(f"  → 本例的 Model 基类靠 __fields__ 自动完成映射，一共 {40} 行左右")
    print("  → 真实 ORM 还要处理: 类型转换、关联、继承、迁移、方言差异……所以它们都很大")

    print("\n== ⑥ 阻抗失配：两套世界观对不上的五个地方 ==")
    print("  ┌────────────┬──────────────────┬──────────────────────┐")
    print("  │ 概念        │ 对象世界          │ 关系世界              │")
    print("  ├────────────┼──────────────────┼──────────────────────┤")
    print("  │ 身份        │ 引用相等(is)      │ 主键                  │")
    print("  │ 关联        │ 直接持有引用      │ 外键 + JOIN           │")
    print("  │ 集合        │ List<Order>      │ 另一张表的多行         │")
    print("  │ 继承        │ 天然支持          │ 【没有这个概念】       │")
    print("  │ 加载时机    │ 全部在内存        │ 按需查询（延迟 or 预加载）│")
    print("  └────────────┴──────────────────┴──────────────────────┘")
    print("  → ORM 就是这张表的翻译器；它省下的每一行代码，都对应上面某一行的手工处理")
    print("  → 而它的每个坑（N+1、延迟加载失效、继承映射），也都来自同一张表")

    con.close()
    os.unlink(DB)
