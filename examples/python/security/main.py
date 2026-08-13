"""安全：SQL 注入——把「数据」当成「代码」执行，是绝大多数注入漏洞的同一个根因。"""
import sqlite3
import hashlib
import hmac
import secrets
import time


def make_db():
    con = sqlite3.connect(":memory:")
    con.executescript("""
        CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, role TEXT, secret TEXT);
        INSERT INTO users VALUES (1,'alice','user','alice-token');
        INSERT INTO users VALUES (2,'bob','user','bob-token');
        INSERT INTO users VALUES (3,'root','admin','ADMIN-MASTER-KEY');
    """)
    return con


def login_vulnerable(con, name):
    """⚠️ 拼接字符串——name 里的内容会成为 SQL 语法的一部分"""
    sql = f"SELECT id, name, role FROM users WHERE name = '{name}'"
    return sql, con.execute(sql).fetchall()


def login_safe(con, name):
    """✅ 参数化查询——name 永远只是一个【值】，不参与语法解析"""
    sql = "SELECT id, name, role FROM users WHERE name = ?"
    return sql, con.execute(sql, (name,)).fetchall()


if __name__ == "__main__":
    con = make_db()

    print("== ① SQL 注入：同一行代码，两种输入 ==")
    for label, name in [("正常输入", "alice"), ("恶意输入", "alice' OR '1'='1")]:
        sql, rows = login_vulnerable(con, name)
        print(f"  {label}: name = {name!r}")
        print(f"    实际执行的 SQL: {sql}")
        print(f"    返回 {len(rows)} 行: {[r[1] for r in rows]}")
    print("  → 第二次输入让 WHERE 条件恒真，【整张 users 表被拖走了】")
    print("  → 注意漏洞不在「没过滤引号」，而在于: 数据被拼进了【要被解析成语法的字符串】")

    print("\n== ② 参数化查询：同样的恶意输入，什么都没发生 ==")
    for name in ["alice", "alice' OR '1'='1"]:
        sql, rows = login_safe(con, name)
        print(f"  name = {name!r:22} → 返回 {len(rows)} 行: {[r[1] for r in rows]}")
    print(f"  两次执行的 SQL 完全相同: {sql}")
    print("  → 参数化不是「帮你转义引号」，而是【SQL 语句和数据走两条不同的通道】")
    print("     语句先被解析成执行计划，参数之后才填进去——它没有机会变成语法")
    print("  → 这也解释了为什么参数化【顺便还更快】: 执行计划可以复用（第 49 章）")

    print("\n== ③ 手工转义为什么总是输（实测一次真实绕过）==")
    def escape_quotes(s):
        """有人认为「把 ' 换成 '' 就安全了」"""
        return s.replace("'", "''")

    print("  防御方: 所有输入都过一遍 escape_quotes()")
    print("  但同一个项目里往往还有【另一处】查询，值没有被引号包住:")
    payload = "1 OR 1=1"                              # ⚠️ 全程没有一个引号
    sql = f"SELECT id, name FROM users WHERE id = {escape_quotes(payload)}"
    rows = con.execute(sql).fetchall()
    print(f"    输入: {payload!r}（转义后完全没变: {escape_quotes(payload)!r}）")
    print(f"    SQL:  {sql}")
    print(f"    → 返回 {len(rows)} 行: {[r[1] for r in rows]} —— 【转义被完整绕过】")
    print("  → 转义的前提是「值被引号包住」，而数值列往往不带引号——防御假设一旦不成立就全盘失效")
    print("  → 转义还依赖【数据库方言、字符集、版本】: 历史上 MySQL 的 GBK 编码")
    print("     曾让 addslashes() 整个失效（0x5c 被吃进多字节字符的第二字节）")
    print("  → 参数化没有这些前提: 它压根不产生「需要被正确转义的字符串」")
    sql_p = "SELECT id, name FROM users WHERE id = ?"
    try:
        rows = con.execute(sql_p, (payload,)).fetchall()
        print(f"    同样的输入走参数化 → 返回 {len(rows)} 行: {[r[1] for r in rows]}")
    except sqlite3.Error as e:
        print(f"    同样的输入走参数化 → 报错（类型不匹配，攻击不成立）: {e}")

    print("\n== ④ 参数化管不到的地方：标识符 ==")
    print("  ? 只能替换【值】，不能替换表名/列名/ORDER BY 方向:")
    try:
        con.execute("SELECT * FROM users ORDER BY ? ", ("name",)).fetchall()
        print("    ORDER BY ? → 语法上通过，但它按【常量 'name'】排序，等于没排序")
    except sqlite3.Error as e:
        print(f"    ORDER BY ? → 报错: {e}")
    allowed = {"name", "role"}                       # ✅ 白名单
    for want in ["name", "secret; DROP TABLE users"]:
        col = want if want in allowed else "id"
        print(f"    请求排序列 {want!r:28} → 实际使用 {col!r}")
    print("  → 动态标识符只有一种安全写法: 【白名单映射】，绝不允许用户输入直接拼进去")

    print("\n== ⑤ 密码存储：这里「越快越好」整个反过来 ==")
    pw = b"correct horse battery staple"
    t0 = time.perf_counter()
    for _ in range(200_000):
        hashlib.sha256(pw).hexdigest()
    fast_ms = (time.perf_counter() - t0) * 1000
    rate = 200_000 / (fast_ms / 1000)
    print(f"  SHA-256 直接哈希 200000 次: {fast_ms:.0f} ms → 约 {rate/1e6:.2f} M 次/秒（单核）")

    slowest = rate
    for iters in (10_000, 100_000, 600_000):
        t0 = time.perf_counter()
        hashlib.pbkdf2_hmac("sha256", pw, secrets.token_bytes(16), iters)
        ms = (time.perf_counter() - t0) * 1000
        slowest = 1000 / ms
        print(f"  PBKDF2-SHA256 {iters:>7} 轮: {ms:7.1f} ms → 约 {slowest:>10.1f} 次/秒")
    print(f"  → 猜测速率相差 {rate/slowest:,.0f} 倍（{rate/1e6:.2f} M/秒 vs {slowest:.1f}/秒）")
    print("  → 攻击者拿到库之后，唯一的成本就是【每次猜测的代价】——所以要把它拉高")
    print("  → 密码哈希【故意做慢】: 这是本书唯一一处「性能越差越好」的设计")
    print("     一本 1000 万条的常见密码字典: 用 SHA-256 几秒跑完，用 PBKDF2 要跑几周")
    print("  → 现代首选 Argon2id / scrypt: 它们还【故意吃内存】，让 GPU/ASIC 也占不到便宜")

    print("\n== ⑥ 加盐：为什么每个用户都要有自己的盐 ==")
    same = "hunter2"
    print("  三个用户用了同一个弱密码。不加盐:")
    for u in ["alice", "bob", "carol"]:
        print(f"    {u:6} → {hashlib.sha256(same.encode()).hexdigest()[:32]}...")
    print("  → 哈希值完全相同 → 一眼看出「这三个人密码一样」，且彩虹表一次命中三个")
    print("  加了各自的随机盐:")
    for u in ["alice", "bob", "carol"]:
        salt = secrets.token_bytes(16)
        h = hashlib.pbkdf2_hmac("sha256", same.encode(), salt, 10_000)
        print(f"    {u:6} → {h.hex()[:32]}...")
    print("  → 同一个密码，三个完全不同的结果——预计算的彩虹表【整类失效】")

    print("\n== ⑦ 比较也会泄密：时序攻击 ==")
    real = secrets.token_hex(16)
    print("  用 == 比较令牌时，Python 在【第一个不同的字节处就返回】——")
    print("  这意味着「前缀猜对得越多，函数返回得越晚」，逐字节就能爆破出整个令牌")
    naive_eq = real == ("0" * 32)
    safe_eq = hmac.compare_digest(real, "0" * 32)
    print(f"    ==                  → {naive_eq}（可能提前返回，耗时随前缀长度变化）")
    print(f"    hmac.compare_digest → {safe_eq}（恒定时间，与内容无关）")
    print("  → 凡是比较【密钥、令牌、签名、HMAC】，一律用恒定时间比较函数")
    print("  ⚠️ 注意本例【没有实测出时序差】: 单次差异在纳秒级，被 Python 解释器开销淹没了")
    print("     测不出来 ≠ 不存在 —— 攻击者可以发起百万次请求做统计，把噪声平均掉")
    print("     这是第 57 章「差距要大于波动」的反面: 攻击者有办法把波动降下去")

    print("\n== ⑧ 本节小结：注入类漏洞的统一形状 ==")
    print("  SQL 注入   : 数据被拼进【SQL 解析器】的输入")
    print("  XSS        : 数据被拼进【HTML 解析器】的输入（JS 版实测）")
    print("  命令注入   : 数据被拼进【shell 解析器】的输入")
    print("  路径穿越   : 数据被拼进【路径解析器】的输入")
    print("  → 同一个根因: 【把不可信数据交给了一个会解析它的东西】")
    print("  → 同一类解法: 别拼字符串，用【结构化接口】(参数化查询 / DOM API / execve 数组)")
