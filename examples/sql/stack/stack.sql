-- 第 18 章 · 栈 — SQL 示例
-- 运行：sqlite3 :memory: < stack.sql
-- SQL 没有栈结构，但 SAVEPOINT 的行为完全是栈式的（嵌套 ⇒ 栈）

CREATE TABLE student (name TEXT, score INTEGER);

-- 1. 保存点形成一个栈：每个 SAVEPOINT 相当于 push
BEGIN;
INSERT INTO student VALUES ('Alice', 92);
SAVEPOINT sp1;                       -- push sp1
INSERT INTO student VALUES ('Bob', 75);
SAVEPOINT sp2;                       -- push sp2
INSERT INTO student VALUES ('Carol', 50);
SELECT '回滚前', COUNT(*) AS 行数 FROM student;

-- 2. ROLLBACK TO sp2：弹出 sp2 之上的所有改动（撤销 Carol）
ROLLBACK TO sp2;
SELECT '回滚到 sp2 后', COUNT(*) AS 行数, group_concat(name) AS 剩余 FROM student;

-- 3. ROLLBACK TO sp1：继续弹出（撤销 Bob，且 sp2 一并作废）
ROLLBACK TO sp1;
SELECT '回滚到 sp1 后', COUNT(*) AS 行数, group_concat(name) AS 剩余 FROM student;

COMMIT;
SELECT '提交后最终', COUNT(*) AS 行数, group_concat(name) AS 剩余 FROM student;

-- 4. 验证栈语义：回滚到某层会作废其上所有保存点
BEGIN;
INSERT INTO student VALUES ('Dave', 80);
SAVEPOINT a;
INSERT INTO student VALUES ('Eve', 85);
SAVEPOINT b;
ROLLBACK TO a;                       -- 弹到 a，b 随之失效
INSERT INTO student VALUES ('Frank', 88);
COMMIT;
SELECT '栈语义验证', group_concat(name) AS 全部 FROM student;

-- 5. 递归 CTE 展开层级数据（遍历顺序天然是栈/队列式的，见第 11 章）
CREATE TABLE emp (id INTEGER, name TEXT, boss INTEGER);
INSERT INTO emp VALUES (1,'CEO',NULL), (2,'总监',1), (3,'组长',2);
WITH RECURSIVE chain(id, name, depth) AS (
    SELECT id, name, 0 FROM emp WHERE boss IS NULL
    UNION ALL
    SELECT e.id, e.name, c.depth+1 FROM emp e JOIN chain c ON e.boss = c.id
)
SELECT '层级展开', depth, name FROM chain ORDER BY depth;
