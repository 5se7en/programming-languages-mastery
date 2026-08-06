-- 第 11 章 · 流程控制 — SQL 示例
-- 运行：sqlite3 :memory: < control-flow.sql

CREATE TABLE student (name TEXT, score INTEGER);
INSERT INTO student VALUES ('Alice', 92), ('Bob', 75), ('Carol', 50);

-- 1. 分支用 CASE 表达式（不是语句，可出现在 SELECT/WHERE/ORDER BY 中）
SELECT 'CASE 分支', name,
       CASE WHEN score >= 90 THEN 'A'
            WHEN score >= 60 THEN 'B'
            ELSE 'C'
       END AS grade
FROM student;

-- 2. 不需要循环：一条语句作用于整个集合
UPDATE student SET score = score + 5 WHERE score < 60;
SELECT '集合更新后', name, score FROM student WHERE name = 'Carol';

-- 3. 真正需要重复时用递归 CTE
WITH RECURSIVE cnt(x) AS (
    SELECT 1
    UNION ALL
    SELECT x + 1 FROM cnt WHERE x < 5
)
SELECT '递归 CTE', group_concat(x) FROM cnt;

-- 4. 递归 CTE 的真实用途：查询层级数据
CREATE TABLE emp (id INTEGER, name TEXT, boss INTEGER);
INSERT INTO emp VALUES (1,'CEO',NULL), (2,'总监',1), (3,'组长',2), (4,'员工',3);
WITH RECURSIVE chain(id, name, level) AS (
    SELECT id, name, 0 FROM emp WHERE boss IS NULL
    UNION ALL
    SELECT e.id, e.name, c.level + 1 FROM emp e JOIN chain c ON e.boss = c.id
)
SELECT '层级查询', level, name FROM chain ORDER BY level;
