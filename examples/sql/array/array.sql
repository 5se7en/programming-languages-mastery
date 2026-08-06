-- 第 16 章 · 数组 — SQL 示例
-- 运行：sqlite3 :memory: < array.sql
-- 核心区别：表是「无序集合」，数组是「有序序列」

CREATE TABLE student (name TEXT, score INTEGER);
INSERT INTO student VALUES ('Alice', 92), ('Bob', 75), ('Carol', 88), ('Dave', 60);

-- 1. 表没有「第 N 行」的概念，要顺序必须显式 ORDER BY
SELECT '必须显式排序', name, score FROM student ORDER BY score DESC;

-- 2. 用 LIMIT / OFFSET 取"第 2、3 名"（相当于数组切片）
SELECT '取第2-3名(切片)', name, score FROM student ORDER BY score DESC LIMIT 2 OFFSET 1;

-- 3. 用 ROW_NUMBER 模拟「下标」
SELECT '模拟下标', ROW_NUMBER() OVER (ORDER BY score DESC) AS idx, name, score FROM student;

-- 4. 关系模型的推荐做法：用关联表而非「一列里塞数组」
CREATE TABLE exam_score (student TEXT, subject TEXT, score INTEGER);
INSERT INTO exam_score VALUES
  ('Alice','语文',92), ('Alice','数学',95), ('Alice','英语',88),
  ('Bob','语文',75),   ('Bob','数学',70);
SELECT '关联表(推荐)', student, COUNT(*) AS 科目数, ROUND(AVG(score),1) AS 平均分
FROM exam_score GROUP BY student ORDER BY 平均分 DESC;

-- 5. 说明：PostgreSQL 支持数组列（且下标从 1 开始！），SQLite 不支持
--    CREATE TABLE t (name TEXT, scores INTEGER[]);
--    SELECT scores[1] FROM t;      -- PostgreSQL 数组下标从 1 开始
SELECT '注意' AS 提示, 'PostgreSQL 数组下标从 1 开始，与本章的 0 起点相反' AS 说明;
