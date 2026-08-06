-- 第 12 章 · 函数 — SQL 示例
-- 运行：sqlite3 :memory: < functions.sql
-- 注意：SQLite 不支持 CREATE FUNCTION（自定义函数须由宿主程序注册），
--       这里用 CASE 表达式 + 视图达到同样效果。

CREATE TABLE student (name TEXT, score INTEGER);
INSERT INTO student VALUES ('Alice', 92), ('Bob', 75), ('Carol', 50);

-- 1. 标量函数：作用于「每一行」
SELECT '标量函数', name, UPPER(name) AS 大写, LENGTH(name) AS 长度 FROM student;

-- 2. 聚合函数：把「多行」压缩成一个值 —— SQL 独有的概念
SELECT '聚合函数', COUNT(*) AS 人数, ROUND(AVG(score),1) AS 平均分,
       MAX(score) AS 最高, MIN(score) AS 最低 FROM student;

-- 3. 用视图封装可复用逻辑（相当于「命名的查询」）
CREATE VIEW graded AS
SELECT name, score,
       CASE WHEN score >= 90 THEN 'A' WHEN score >= 60 THEN 'B' ELSE 'C' END AS grade
FROM student;
SELECT '视图复用', name, grade FROM graded;

-- 4. 聚合 + 分组：按等级统计
SELECT '分组聚合', grade, COUNT(*) AS 人数 FROM graded GROUP BY grade ORDER BY grade;

-- 5. 窗口函数：既保留每一行，又能做聚合（对比普通聚合）
SELECT '窗口函数', name, score,
       ROUND(AVG(score) OVER (), 1) AS 全班平均,
       RANK() OVER (ORDER BY score DESC) AS 排名
FROM student;
