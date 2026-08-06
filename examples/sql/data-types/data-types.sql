-- 第 09 章 · 数据类型 — SQL 示例
-- 运行：sqlite3 :memory: < data-types.sql

-- 1. 列类型：金额应用 DECIMAL（注意 SQLite 无真正 DECIMAL，仅类型亲和性）
CREATE TABLE product (
    name   TEXT,
    price  DECIMAL(10, 2),
    weight REAL
);
INSERT INTO product VALUES ('笔记本', 5999.00, 1.35);
SELECT name, price, weight FROM product;

-- 2. 浮点误差在 SQL 中同样存在
SELECT 0.1 + 0.2 AS 计算结果, (0.1 + 0.2) = 0.3 AS 是否等于零点三;

-- 3. NULL 是"未知"，不是值 —— 三值逻辑
SELECT NULL = NULL AS "NULL=NULL 的结果", NULL IS NULL AS "NULL IS NULL 的结果";

-- 4. 查空值必须用 IS NULL
CREATE TABLE student (name TEXT, score INTEGER);
INSERT INTO student VALUES ('Alice', 92), ('Bob', NULL);
SELECT '用 = NULL 查到的行数', COUNT(*) FROM student WHERE score = NULL;
SELECT '用 IS NULL 查到的行数', COUNT(*) FROM student WHERE score IS NULL;

-- 5. SQLite 默认类型亲和性（弱），STRICT 才严格
CREATE TABLE t (n INTEGER);
INSERT INTO t VALUES ('123');
SELECT n, typeof(n) AS 存进去后的实际类型 FROM t;
