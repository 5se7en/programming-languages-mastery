-- 事务：ACID 四个字母，逐个用 SQL 兑现。

CREATE TABLE account (
  id      INTEGER PRIMARY KEY,
  name    TEXT    NOT NULL,
  balance INTEGER NOT NULL CHECK (balance >= 0)     -- C：一致性的守门员
);
INSERT INTO account VALUES (1, '甲', 100), (2, '乙', 100);

-- ① A 原子性：转账的两步要么都做，要么都不做
BEGIN;
  UPDATE account SET balance = balance - 60 WHERE id = 1;
  UPDATE account SET balance = balance + 60 WHERE id = 2;
COMMIT;
SELECT '① 转账 60 成功后: 甲=' || (SELECT balance FROM account WHERE id=1) ||
       ' 乙=' || (SELECT balance FROM account WHERE id=2) ||
       '，总额=' || (SELECT SUM(balance) FROM account) AS r;

BEGIN;
  UPDATE account SET balance = balance - 30 WHERE id = 1;
ROLLBACK;                                            -- 模拟「转到一半崩溃」
SELECT '   崩溃回滚后: 甲=' || (SELECT balance FROM account WHERE id=1) ||
       '，总额仍是 ' || (SELECT SUM(balance) FROM account) || '（一分没少）' AS r;

-- ② C 一致性：约束让「不变量」无法被打破
BEGIN;
  UPDATE OR IGNORE account SET balance = balance - 999 WHERE id = 1;   -- 会让余额变负
SELECT '② 试图透支 999: 影响行数=' || changes() || '（CHECK 拦下）' AS r;
COMMIT;
SELECT '   甲的余额仍是 ' || (SELECT balance FROM account WHERE id=1) AS r;
SELECT '   → C 是唯一需要【你】参与的字母: 数据库只能执行你声明的规则' AS r;

-- ③ 保存点：事务内的部分回滚
BEGIN;
  UPDATE account SET balance = 1 WHERE id = 1;
  SAVEPOINT sp1;
  UPDATE account SET balance = 2 WHERE id = 2;
  ROLLBACK TO sp1;                                   -- 只撤销 sp1 之后的
COMMIT;
SELECT '③ SAVEPOINT 后部分回滚: 甲=' || (SELECT balance FROM account WHERE id=1) ||
       '（保留）乙=' || (SELECT balance FROM account WHERE id=2) || '（已撤销）' AS r;

-- ④ 隔离级别与它们放过的异常（SQL 标准）
SELECT '④ 四个隔离级别 —— 每一级挡住上一级放过的一种异常:' AS r;
SELECT '   READ UNCOMMITTED : 脏读✗ 不可重复读✗ 幻读✗ 写偏斜✗（几乎没人用）' AS r;
SELECT '   READ COMMITTED   : 脏读✓ 不可重复读✗ 幻读✗ 写偏斜✗（PostgreSQL/Oracle 默认）' AS r;
SELECT '   REPEATABLE READ  : 脏读✓ 不可重复读✓ 幻读~ 写偏斜✗（MySQL 默认）' AS r;
SELECT '   SERIALIZABLE     : 全部✓（sqlite 只有这一级）' AS r;
SELECT '   ✓=挡住 ✗=放过 ~=看实现' AS r;

-- ⑤ 同名不同义：三家的 REPEATABLE READ 是三种东西
SELECT '⑤ 「REPEATABLE READ」这个名字下藏着三种不同实现:' AS r;
SELECT '   SQL 标准    : 允许幻读（只保证既有行不变）' AS r;
SELECT '   MySQL/InnoDB: 用【间隙锁】额外挡住了幻读，但挡不住写偏斜' AS r;
SELECT '   PostgreSQL  : 实际是【快照隔离】，幻读也挡住了，仍挡不住写偏斜' AS r;
SELECT '   → 所以「我用了 RR 所以安全」是句危险的话——要问的是「哪家的 RR」' AS r;

-- ⑥ sqlite 的选择：只有一级，靠串行化写者取得正确性
SELECT '⑥ sqlite 的隔离级别: 永远是 SERIALIZABLE' AS r;
SELECT '   实现方式: 同一时刻只允许【一个写事务】（库级写锁）' AS r;
SELECT '   PRAGMA journal_mode=WAL 后读者不再被写者阻塞（Python 版实测 0.00x ms 无等待）' AS r;
SELECT '   → 正确性拉满，写并发度为 1 —— 嵌入式场景的合理取舍' AS r;

-- ⑦ 事务的三条实务纪律
SELECT '⑦ 三条纪律:' AS r;
SELECT '   ① 事务要短: 长事务卡住 MVCC 的版本回收（C++ 版实测版本链堆到 101 个）' AS r;
SELECT '   ② 事务里别做 I/O: 发邮件/调支付回滚不掉（JS 版演示）' AS r;
SELECT '   ③ 按固定顺序访问行: 顺序不一致 → 死锁（第 41 章四个必要条件，第 50 章展开）' AS r;

SELECT '   最终账面总额: ' || (SELECT SUM(balance) FROM account) AS r;
