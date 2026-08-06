-- 第 20 章 · 哈希 — SQL 示例
-- 运行：sqlite3 :memory: < hash.sql
-- 数据库里哈希的角色：哈希连接、哈希聚合；而索引默认用 B 树（因为哈希丢失了顺序）

CREATE TABLE student (id INTEGER, name TEXT, class TEXT, score INTEGER);
INSERT INTO student VALUES
 (1,'Alice','一班',92), (2,'Bob','一班',75), (3,'Carol','二班',88),
 (4,'Dave','二班',60),  (5,'Eve','三班',95);

CREATE TABLE course (student_id INTEGER, title TEXT);
INSERT INTO course VALUES (1,'数学'), (1,'英语'), (2,'数学'), (3,'物理'), (5,'化学');

-- 1. 哈希聚合：GROUP BY 的典型实现就是"用哈希表按分组键累积"
--    （与你在代码里用 dict 统计词频是同一件事）
SELECT '哈希聚合 GROUP BY', class, COUNT(*) AS 人数, ROUND(AVG(score),1) AS 平均分
FROM student GROUP BY class ORDER BY class;

-- 2. 哈希去重：DISTINCT 同样可用哈希实现
SELECT '哈希去重 DISTINCT', COUNT(DISTINCT class) AS 班级数 FROM student;

-- 3. 哈希连接：等值连接时，把小表建成哈希表再探测大表
--    把 O(n×m) 的嵌套循环降到约 O(n+m)
SELECT '哈希连接 JOIN', s.name, c.title
FROM student s JOIN course c ON s.id = c.student_id
ORDER BY s.id, c.title;

-- 4. 为什么索引默认用 B 树而非哈希：哈希不支持范围查询与排序
CREATE INDEX idx_score ON student(score);       -- B 树索引（SQLite 默认且唯一）
SELECT '范围查询(B树支持,哈希不支持)', name, score
FROM student WHERE score BETWEEN 80 AND 95 ORDER BY score DESC;

-- 5. 等值查询：哈希索引本可更快，但代价是丢掉上面那些能力
SELECT '等值查询(哈希索引擅长)', name FROM student WHERE score = 92;

-- 6. 用 EXPLAIN 观察执行计划（生产中判断是否用了哈希策略的方法）
EXPLAIN QUERY PLAN
SELECT s.name, c.title FROM student s JOIN course c ON s.id = c.student_id;
