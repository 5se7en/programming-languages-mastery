-- 数据库：站在数据库这一侧，看它对文件多承诺了什么。

-- ① A（原子性）：转账要么全做，要么全不做
CREATE TABLE account (id INTEGER PRIMARY KEY, name TEXT, balance INTEGER);
INSERT INTO account VALUES (1, '甲', 100), (2, '乙', 100);

BEGIN;
UPDATE account SET balance = balance - 60 WHERE id = 1;
-- 假设此刻「崩溃」——ROLLBACK 模拟数据库重启后对未提交事务的处理
ROLLBACK;
SELECT '① 转账中途崩溃后: 甲=' ||
       (SELECT balance FROM account WHERE id=1) || ' 乙=' ||
       (SELECT balance FROM account WHERE id=2) || '（一分都没少）' AS r;

BEGIN;
UPDATE account SET balance = balance - 60 WHERE id = 1;
UPDATE account SET balance = balance + 60 WHERE id = 2;
COMMIT;
SELECT '   完整提交后:     甲=' ||
       (SELECT balance FROM account WHERE id=1) || ' 乙=' ||
       (SELECT balance FROM account WHERE id=2) AS r;

-- ② C（一致性）：约束是数据的守门员——坏数据根本进不来
CREATE TABLE account2 (
  id      INTEGER PRIMARY KEY,
  name    TEXT    NOT NULL,
  balance INTEGER NOT NULL CHECK (balance >= 0)   -- 余额不许为负
);
INSERT INTO account2 SELECT * FROM account;
UPDATE OR IGNORE account2 SET balance = -999 WHERE id = 1;   -- 违反 CHECK，被静默拒绝
SELECT '② 试图把余额改成 -999 后: 甲=' ||
       (SELECT balance FROM account2 WHERE id=1) || '（CHECK 约束拦下，changes=' || changes() || '）' AS r;
INSERT OR IGNORE INTO account2 VALUES (1, '假甲', 0);         -- 主键重复，被拒绝
SELECT '   试图插入重复 id=1: changes=' || changes() || '（主键约束拦下）' AS r;
SELECT '   → 文件版要在【每个写它的程序里】重复这些校验；数据库写一遍，所有人共享' AS r;

-- ③ D（持久性）的价目表：PRAGMA synchronous 就是三档开关
SELECT '③ PRAGMA synchronous = OFF    → 只 write()，进程/内核崩溃都可能丢' AS r;
SELECT '   PRAGMA synchronous = NORMAL → WAL 下常规选择，内核崩溃不丢，掉电可能丢最后事务' AS r;
SELECT '   PRAGMA synchronous = FULL   → 每次提交都 fsync（macOS 默认走 F_BARRIERFSYNC）' AS r;
SELECT '   PRAGMA fullfsync = ON       → 动真格的 F_FULLFSYNC，掉电也不丢（C++ 版实测它最贵）' AS r;
SELECT '   → 第 43 章 FULL 只慢 1.5x 的谜底: macOS 上 FULL ≠ F_FULLFSYNC，还差一档' AS r;

-- ④ 声明式查询：说「要什么」，不说「怎么找」
CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, score INTEGER);
WITH RECURSIVE seq(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM seq WHERE n < 99999)
INSERT INTO users SELECT n, 'user-' || n, n % 100 FROM seq;

SELECT '④ 十万行已就绪；同一份数据问三个问题——' AS r;
SELECT '   点查:   ' || (SELECT name  FROM users WHERE id = 99999) AS r;
SELECT '   聚合:   score=99 的有 ' || (SELECT COUNT(*) FROM users WHERE score = 99) || ' 人' AS r;
SELECT '   Top-1:  ' || (SELECT name FROM users ORDER BY score DESC, id DESC LIMIT 1) AS r;
SELECT '   → 文件版每个问题都要重写解析+循环；SQL 只改一句话（第 47 章展开）' AS r;

-- ⑤ 执行计划：数据库自己决定「怎么找」
SELECT '⑤ WHERE score=42 的执行计划（无索引）:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM users WHERE score = 42;
CREATE INDEX idx_score ON users(score);
SELECT '   建 idx_score 之后:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM users WHERE score = 42;
SELECT '   → SCAN（全表扫）变 SEARCH（索引查）——同一句 SQL，你一行代码没改' AS r;
SELECT '   → 这就是声明式的红利: 优化器替你选算法，加索引即换算法（第 49 章）' AS r;

-- ⑥ 数据库对文件的全部承诺（本章总纲）
SELECT '⑥ 文件给你: 一段能读写的字节。数据库在其上加了五层——' AS r;
SELECT '   D 持久化   : WAL + fsync 档位（③，C++ 版实测三档价格）' AS r;
SELECT '   A 原子性   : 事务 + 回滚（①，第 48 章）' AS r;
SELECT '   C 一致性   : 约束守门（②）' AS r;
SELECT '   I 隔离性   : 跨进程并发控制（Python 版实测丢更新 vs 不丢，第 50 章）' AS r;
SELECT '   查询引擎   : 声明式 SQL + 索引 + 优化器（④⑤，第 47/49 章）' AS r;
