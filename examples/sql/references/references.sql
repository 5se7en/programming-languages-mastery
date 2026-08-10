-- 引用：VIEW 是查询的别名（引用语义）；CREATE TABLE AS 是快照（值语义）。

CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT, score INTEGER);
INSERT INTO student VALUES (1, '小明', 90), (2, '小红', 85);

-- ① 两种"复制"：别名 vs 快照
CREATE VIEW top_students AS
    SELECT name, score FROM student WHERE score >= 80;     -- 不复制数据——只是存了查询

CREATE TABLE snapshot_students AS
    SELECT name, score FROM student WHERE score >= 80;     -- 真复制——此刻的数据凝固

SELECT 'VIEW 初始行数: ' || COUNT(*) FROM top_students;
SELECT '快照初始行数: ' || COUNT(*) FROM snapshot_students;

-- ② 修改原表，看谁跟着变
INSERT INTO student VALUES (3, '小刚', 95), (4, '小强', 88);
UPDATE student SET score = 70 WHERE id = 2;                -- 小红跌出 80 分线

SELECT 'VIEW 现在: ' || COUNT(*) || ' 行（小刚小强进来、小红出去——实时反映原表）' FROM top_students;
SELECT '快照现在: ' || COUNT(*) || ' 行（纹丝不动——那一刻的拷贝）' FROM snapshot_students;

-- ③ VIEW 没有自己的数据：EXPLAIN 显示它每次都去查原表
--    （视图 = 引用语义：零存储、永远新鲜、原表删了就悬垂——DROP TABLE 后查视图报错）
--    （快照 = 值语义：占存储、数据陈旧、与原表从此无关）
SELECT name FROM top_students ORDER BY score DESC;
