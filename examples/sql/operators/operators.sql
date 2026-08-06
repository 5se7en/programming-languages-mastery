-- 第 10 章 · 运算符 — SQL 示例
-- 运行：sqlite3 :memory: < operators.sql

CREATE TABLE student (name TEXT, score INTEGER, grade TEXT);
INSERT INTO student VALUES ('Alice', 92, 'A'), ('Bob', 75, 'B'), ('Carol', NULL, NULL);

-- 1. SQL 只有值相等
SELECT '值比较', name FROM student WHERE name = 'Alice';

-- 2. 三值逻辑真值表（空白 = NULL/未知）
SELECT (NULL AND 0) AS "NULL AND 假",
       (NULL AND 1) AS "NULL AND 真",
       (NULL OR  1) AS "NULL OR 真",
       (NULL OR  0) AS "NULL OR 假",
       (NOT NULL)   AS "NOT NULL";

-- 3. = NULL 查不到，IS NULL 才行
SELECT '用 = NULL', COUNT(*) FROM student WHERE score = NULL;
SELECT '用 IS NULL', COUNT(*) FROM student WHERE score IS NULL;

-- 4. NOT IN 遇到 NULL 永远不为真（经典陷阱）
SELECT 'NOT IN 含 NULL', COUNT(*) FROM student WHERE grade NOT IN ('A', NULL);
SELECT 'NOT IN 不含 NULL', COUNT(*) FROM student WHERE grade NOT IN ('A');

-- 5. SQL 专有运算符
SELECT 'BETWEEN', name FROM student WHERE score BETWEEN 60 AND 90;
SELECT 'LIKE',    name FROM student WHERE name LIKE 'A%';
SELECT 'IN',      name FROM student WHERE grade IN ('A', 'B');
SELECT '拼接 ||', name || ' 同学' FROM student WHERE name = 'Alice';
