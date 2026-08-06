-- 第 14 章 · 模块 — SQL 示例
-- 运行：sqlite3 :memory: < modules.sql
-- 注意：SQLite 没有 CREATE SCHEMA，用 ATTACH DATABASE 挂载另一个库来模拟"模式"

CREATE TABLE student (name TEXT, score INTEGER);
INSERT INTO student VALUES ('Alice', 92), ('Bob', 55);

-- 1. ATTACH：把另一个数据库挂载进来，用「别名.表名」限定访问（类似命名空间）
ATTACH DATABASE ':memory:' AS sales;
CREATE TABLE sales.orders (id INTEGER, amount DECIMAL(10,2));
INSERT INTO sales.orders VALUES (1, 99.50), (2, 12.00);

SELECT '主库的表', name, score FROM main.student;
SELECT '挂载库的表', id, amount FROM sales.orders;

-- 2. 同名表靠限定名区分（这正是"命名空间"要解决的问题）
CREATE TABLE sales.student (name TEXT, region TEXT);
INSERT INTO sales.student VALUES ('Alice', '华东');
SELECT '同名表-主库', name FROM main.student WHERE name = 'Alice';
SELECT '同名表-挂载库', name, region FROM sales.student;

-- 3. 视图：SQL 的封装手段，隐藏底层结构（相当于对外公开的接口）
CREATE VIEW passed AS
SELECT name, score FROM student WHERE score >= 60;
SELECT '视图封装', name, score FROM passed;

-- 4. 视图使用者不必知道底层逻辑，底层可改而接口不变
SELECT '通过视图统计', COUNT(*) AS 及格人数 FROM passed;

DETACH DATABASE sales;
