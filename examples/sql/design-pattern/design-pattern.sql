-- 设计模式：数据库世界也有自己的模式语言——它们同样是「反复出现的问题的通用解法」。

-- ① 软删除（Soft Delete）：不真删，只标记
CREATE TABLE users (
  id INTEGER PRIMARY KEY, name TEXT, deleted_at TEXT DEFAULT NULL
);
INSERT INTO users(id, name) VALUES (1,'张三'), (2,'李四'), (3,'王五');
UPDATE users SET deleted_at = '2026-08-01' WHERE id = 2;

CREATE VIEW active_users AS SELECT id, name FROM users WHERE deleted_at IS NULL;
SELECT '① 软删除: 物理行 ' || (SELECT COUNT(*) FROM users) ||
       ' 行，视图里可见 ' || (SELECT COUNT(*) FROM active_users) || ' 行' AS r;
SELECT '   → 好处: 可恢复、可审计、外键不会断' AS r;
SELECT '   → 代价: 每个查询都要记得加 deleted_at IS NULL（漏一次就是数据泄漏）' AS r;
SELECT '   → 所以配套做法是【视图即接口】(第 55 章): 应用只查 active_users，永不碰底表' AS r;
SELECT '   → 索引也要配合: 第 49 章的部分索引 WHERE deleted_at IS NULL 更小更快' AS r;

-- ② 乐观锁（Optimistic Locking）：第 48 章的模式化
CREATE TABLE docs (id INTEGER PRIMARY KEY, content TEXT, version INTEGER DEFAULT 1);
INSERT INTO docs VALUES (1, '初稿', 1);
-- 甲乙都读到 version=1，甲先写
UPDATE docs SET content='甲的修改', version=version+1 WHERE id=1 AND version=1;
SELECT '② 乐观锁: 甲提交（持 version=1）影响 ' || changes() || ' 行 ✓' AS r;
-- 乙拿着过期的 version=1 提交
UPDATE docs SET content='乙的修改', version=version+1 WHERE id=1 AND version=1;
SELECT '   乙提交（同样持 version=1）影响 ' || changes() || ' 行 → 【被拒绝】' AS r;
SELECT '   当前内容: ' || (SELECT content FROM docs WHERE id=1) ||
       '，version=' || (SELECT version FROM docs WHERE id=1) AS r;
SELECT '   → 「影响 0 行」就是冲突信号（第 48 章 C# 版实测过它的 ORM 形态）' AS r;
SELECT '   → 这是【模式】而非语言特性: 任何数据库、任何语言都是这一套' AS r;

-- ③ 事件溯源（Event Sourcing）：存变更而非状态
CREATE TABLE account_events (
  seq INTEGER PRIMARY KEY AUTOINCREMENT, account_id INTEGER, kind TEXT, amount INTEGER
);
INSERT INTO account_events(account_id, kind, amount) VALUES
  (1,'开户',0), (1,'存入',500), (1,'取出',200), (1,'存入',100);
SELECT '③ 事件溯源: 存下每一次【变更】，当前状态由重放得出' AS r;
SELECT '   事件流: ' || group_concat(kind || amount, ' → ') AS r FROM account_events WHERE account_id=1;
SELECT '   重放得到余额: ' ||
       SUM(CASE kind WHEN '存入' THEN amount WHEN '取出' THEN -amount ELSE 0 END) AS r
  FROM account_events WHERE account_id = 1;
SELECT '   → 与第 46 章的 WAL、第 54 章的迁移脚本是【同一个思想】: 状态 = 事件的折叠' AS r;
SELECT '   → 好处: 完整审计、可回到任意时刻、可重放出新的视图（CQRS）' AS r;
SELECT '   → 代价: 查当前状态要重放（所以要配快照）、事件结构改不了（它是历史）' AS r;

-- ④ 物化视图 = 缓存模式（第 54 章的增量刷新在这里是「模式」视角）
CREATE TABLE balance_snapshot (account_id INTEGER PRIMARY KEY, balance INTEGER, upto_seq INTEGER);
INSERT INTO balance_snapshot
  SELECT account_id,
         SUM(CASE kind WHEN '存入' THEN amount WHEN '取出' THEN -amount ELSE 0 END),
         MAX(seq)
  FROM account_events GROUP BY account_id;
SELECT '④ 快照模式: 把重放结果存下来（缓存），只重放【快照之后】的事件' AS r;
SELECT '   快照: 余额 ' || (SELECT balance FROM balance_snapshot WHERE account_id=1) ||
       '，已覆盖到 seq=' || (SELECT upto_seq FROM balance_snapshot WHERE account_id=1) AS r;
INSERT INTO account_events(account_id, kind, amount) VALUES (1,'存入',50);
SELECT '   新事件到达后，当前余额 = 快照 + 增量重放 = ' ||
       ((SELECT balance FROM balance_snapshot WHERE account_id=1) +
        (SELECT COALESCE(SUM(CASE kind WHEN '存入' THEN amount WHEN '取出' THEN -amount ELSE 0 END),0)
         FROM account_events
         WHERE account_id=1 AND seq > (SELECT upto_seq FROM balance_snapshot WHERE account_id=1))) AS r;
SELECT '   → 与第 54 章增量构建完全同构: 快照=构建产物，upto_seq=已应用到哪' AS r;

-- ⑤ 外键树 vs 闭包表：同一个问题的两种模式
CREATE TABLE category (id INTEGER PRIMARY KEY, name TEXT, parent_id INTEGER);
INSERT INTO category VALUES (1,'电子',NULL),(2,'手机',1),(3,'安卓机',2),(4,'图书',NULL);
SELECT '⑤ 树形结构的两种建模:' AS r;
SELECT '   邻接表(parent_id): 写简单，查子孙要递归 CTE' AS r;
WITH RECURSIVE tree(id, name, depth) AS (
  SELECT id, name, 0 FROM category WHERE id = 1
  UNION ALL SELECT c.id, c.name, t.depth+1 FROM category c JOIN tree t ON c.parent_id = t.id
)
SELECT '     递归查「电子」的所有子孙: ' || group_concat(name, ' → ') AS r FROM tree;
SELECT '   闭包表: 额外存下【所有祖先-后代对】——查询变成一次 JOIN，代价是写入要维护' AS r;
SELECT '   → 经典的读写权衡（第 49 章索引的同一笔账）: 为读优化就要在写时付出' AS r;

-- ⑥ 数据库模式与 GoF 的对照
SELECT '⑥ 数据库的模式语言与 GoF 的关系:' AS r;
SELECT '   软删除、乐观锁、事件溯源、闭包表 —— 【GoF 书里一个都没有】' AS r;
SELECT '   因为它们解决的是【数据与持久化】的问题，不是对象组织的问题' AS r;
SELECT '   → 每个领域都会长出自己的模式语言: GoF 是面向对象领域的，' AS r;
SELECT '     Fowler 的 PoEAA 是企业应用的，本节这些是数据领域的' AS r;
SELECT '   → 共同点: 都是【反复出现的问题】 + 【被验证过的解法】 + 【一个便于沟通的名字】' AS r;
SELECT '   → 模式最大的价值往往是最后一项: 让「你用闭包表吧」代替十分钟的白板讲解' AS r;
