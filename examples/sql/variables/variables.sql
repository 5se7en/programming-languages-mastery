-- 第 08 章 · 变量 — SQL 示例
-- 运行：sqlite3 :memory: < variables.sql

-- 1. 列：SQL 中真正"有类型的存储"
CREATE TABLE student (
    name  TEXT,
    age   INTEGER,
    score INTEGER
);
INSERT INTO student VALUES ('Alice', 20, 92);

-- 2. 用 CTE 给值命名（可移植写法）
WITH params(max_score) AS (VALUES (100))
SELECT name, score,
       ROUND(score * 100.0 / (SELECT max_score FROM params), 1) AS pct
FROM student;

-- 3. 各方言的局部/会话变量（语法不同，此处仅作说明）
--   SQL Server : DECLARE @max_score INT = 100;
--   MySQL      : SET @max_score = 100;
--   PostgreSQL : DECLARE max_score int := 100;  (PL/pgSQL 块内)
