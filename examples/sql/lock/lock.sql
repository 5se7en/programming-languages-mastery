-- 锁：数据库的锁与死锁——和线程编程是同一套理论。

CREATE TABLE account (id INTEGER PRIMARY KEY, name TEXT, balance INTEGER);
INSERT INTO account VALUES (1, 'A', 1000), (2, 'B', 1000);

-- ① 事务 = 临界区：把跨多行的不变式保护起来
BEGIN IMMEDIATE;                       -- 立刻获取写锁（相当于 lock()）
UPDATE account SET balance = balance - 100 WHERE id = 1;
UPDATE account SET balance = balance + 100 WHERE id = 2;
COMMIT;                                -- 释放锁（相当于 unlock()）
SELECT '① 转账后 A=' || (SELECT balance FROM account WHERE id=1)
       || ', B=' || (SELECT balance FROM account WHERE id=2)
       || '，总额 = ' || (SELECT SUM(balance) FROM account) || '（守恒 ✅）' AS r;

-- ② 锁的粒度：SQLite 是「整库一把写锁」，其他数据库更细
SELECT '② SQLite 锁粒度 = 整个数据库文件（最粗）' AS r;
SELECT '   PostgreSQL/MySQL = 行级锁（最细，并发度最高）' AS r;
SELECT '   粒度越细并发越好，但锁管理开销越大——与分段锁同一权衡' AS r;

-- ③ 死锁：两个事务交叉更新两行，各持一半互等
--    （SQLite 因整库锁不易复现经典死锁；PostgreSQL/MySQL 会自动检测并回滚一个事务）
SELECT '③ 经典死锁: 事务1 锁住 A 等 B，事务2 锁住 B 等 A' AS r;
SELECT '   数据库比编程语言更进一步：自动检测死锁并回滚代价小的那个' AS r;
SELECT '   （PostgreSQL 报 deadlock detected；MySQL 报 Deadlock found）' AS r;

-- ④ 悲观锁 vs 乐观锁
--    悲观：先加锁再改（SELECT ... FOR UPDATE）—— 对应 mutex
--    乐观：改的时候校验版本 —— 对应 CAS（第 40 章实测过）
CREATE TABLE doc (id INTEGER PRIMARY KEY, content TEXT, version INTEGER);
INSERT INTO doc VALUES (1, '初稿', 1);
UPDATE doc SET content='二稿', version=version+1 WHERE id=1 AND version=1;
SELECT '④ 乐观锁更新: 影响行数 = ' || changes() || '（1 = 成功抢到）' AS r;

-- ⑤ 锁等待超时：拿不到锁时等多久（与 tryLock(timeout) 同义）
PRAGMA busy_timeout = 3000;
SELECT '⑤ busy_timeout = ' || (SELECT * FROM pragma_busy_timeout())
       || ' ms —— 相当于 Java 的 tryLock(3, SECONDS)' AS r;
SELECT '   拿不到锁就报 database is locked（第 39 章实测过）' AS r;
