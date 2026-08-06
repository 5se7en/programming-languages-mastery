-- 第 13 章 · 作用域 — SQL 示例
-- 运行：sqlite3 :memory: < scope.sql
-- SQL 的"作用域"由逻辑执行顺序决定：
--   FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY

CREATE TABLE student (name TEXT, class TEXT, score INTEGER);
INSERT INTO student VALUES ('Alice','一班',92), ('Bob','一班',55), ('Carol','二班',78), ('Dave','二班',60);

-- 1. 别名在 SELECT 阶段才产生，所以 ORDER BY 可以用它
SELECT '别名用于 ORDER BY', name, score * 1.1 AS adjusted
FROM student ORDER BY adjusted DESC LIMIT 2;

-- 2. 标准 SQL 中 WHERE 看不到别名（WHERE 先于 SELECT 执行）
--    可移植写法：重复表达式
SELECT 'WHERE 重复表达式', name, score * 1.1 AS adjusted
FROM student WHERE score * 1.1 > 80;
--    注意：SQLite / MySQL 作为扩展允许 WHERE 中用别名，但不可移植，不建议依赖

-- 3. CTE：给查询起名字，作用域限于本条语句
WITH passed AS (
    SELECT name, class, score FROM student WHERE score >= 60
)
SELECT 'CTE 作用域', name, score FROM passed ORDER BY score DESC;

-- 4. 相关子查询：内层可以引用外层的列 —— 相当于"作用域链向外查找"
SELECT '高于本班平均分', s.name, s.class, s.score
FROM student s
WHERE s.score > (SELECT AVG(score) FROM student WHERE class = s.class);

-- 5. 表别名的作用域覆盖整条语句
SELECT '表别名', s.name, s.class FROM student s WHERE s.score >= 90;
