-- 第 21 章 · 树 — SQL 示例
-- 运行：sqlite3 :memory: < tree.sql
-- 树在数据库里有两个身份：① 索引结构（B+ 树）② 数据本身的形状（层级数据）

CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT, class TEXT, score INTEGER);
INSERT INTO student VALUES
 (1,'Alice','一班',92), (2,'Bob','一班',75), (3,'Carol','二班',88),
 (4,'Dave','二班',60),  (5,'Eve','三班',95),  (6,'Frank','三班',81);

-- ============================================================
-- 一、B+ 树索引：数据库为什么不用二叉树，也不用哈希
-- ============================================================

CREATE INDEX idx_score ON student(score);   -- 默认创建 B+ 树索引

-- 1. 为什么不用二叉树？因为磁盘 I/O 慢十万倍，而树的每一层≈一次 I/O
SELECT '① 树高对比（一亿条数据）' AS 说明;
SELECT '二叉树'   AS 结构, '≈ 27 层' AS 树高, '约 27 次 I/O' AS 代价
UNION ALL
SELECT 'B+树(阶500)', '≈ 3 层',  '约 3 次 I/O';
-- → B+ 树让一个节点存几百个键，把树压矮，从而大幅减少 I/O

-- 2. 为什么不用哈希？因为哈希丢掉了顺序（第 20 章），答不了范围查询

-- ============================================================
-- 二、B+ 树让哪些查询变快（索引有序 → 能回答"大于/范围/排序/最值"）
-- ============================================================

SELECT '② 能用上索引的查询' AS 说明;

-- 等值查询
SELECT '等值 score=92'    AS 查询, name FROM student WHERE score = 92;

-- 范围查询：定位起点后沿叶子链表扫，这是 B+ 树相对 B 树的关键改进
SELECT '范围 80..95'      AS 查询, name, score FROM student
WHERE score BETWEEN 80 AND 95 ORDER BY score;

-- 排序：索引本身就是有序的，不需要额外排序
SELECT '排序 ORDER BY'    AS 查询, name, score FROM student ORDER BY score DESC LIMIT 3;

-- 最值：走到最左/最右叶子即可
SELECT '最值 MIN/MAX'     AS 查询, MIN(score) AS 最低, MAX(score) AS 最高 FROM student;

-- 前缀匹配：字符串索引按字典序排列，前缀相同的必然挨在一起
SELECT '前缀 LIKE ''A%''' AS 查询, name FROM student WHERE name LIKE 'A%';

-- ============================================================
-- 三、⚠️ 索引失效：一旦破坏"按列原始值排序"的关系，索引就用不上了
-- ============================================================

SELECT '③ 索引失效对比' AS 说明;

-- ✗ 对列做运算 → 排序关系被破坏 → 全表扫描
EXPLAIN QUERY PLAN SELECT * FROM student WHERE score + 10 > 100;

-- ✓ 把运算移到常量侧 → 索引可用
EXPLAIN QUERY PLAN SELECT * FROM student WHERE score > 90;

-- ✗ 对列用函数 → 同样失效
EXPLAIN QUERY PLAN SELECT * FROM student WHERE ABS(score) = 92;

-- ✗ 前导通配符 → 无法用前缀定位
EXPLAIN QUERY PLAN SELECT * FROM student WHERE name LIKE '%ice';

-- 看执行计划：SEARCH ... USING INDEX 表示用上了索引；SCAN 表示全表扫描

-- ============================================================
-- 四、复合索引与最左前缀原则
-- ============================================================

CREATE INDEX idx_class_score ON student(class, score);
-- 复合索引像按"姓、名"排序的电话簿：先按 class 排，class 相同再按 score 排

SELECT '④ 最左前缀原则' AS 说明;

-- ✓ 用到第一列
EXPLAIN QUERY PLAN SELECT * FROM student WHERE class = '一班';

-- ✓ 两列都用上
EXPLAIN QUERY PLAN SELECT * FROM student WHERE class = '一班' AND score > 80;

-- ✗ 跳过第一列 → 用不上这个复合索引
--   （能找到"所有姓张的"，但没法快速找到"所有名叫伟的"）
EXPLAIN QUERY PLAN SELECT * FROM student WHERE class > '零';

-- ============================================================
-- 五、树形数据本身：邻接表 + 递归 CTE
-- ============================================================

CREATE TABLE emp (id INTEGER, name TEXT, boss INTEGER);
INSERT INTO emp VALUES
 (1,'CEO',NULL),
 (2,'技术总监',1), (3,'销售总监',1),
 (4,'后端组长',2), (5,'前端组长',2), (6,'华东经理',3),
 (7,'小王',4),     (8,'小李',4),     (9,'小张',5);

SELECT '⑤ 递归 CTE 遍历组织架构树' AS 说明;

WITH RECURSIVE tree(id, name, level, path) AS (
    -- 锚定成员：根节点（没有上级的人）
    SELECT id, name, 0, name FROM emp WHERE boss IS NULL
    UNION ALL
    -- 递归成员：逐层向下找下属
    SELECT e.id, e.name, t.level + 1, t.path || ' → ' || e.name
    FROM emp e JOIN tree t ON e.boss = t.id
)
SELECT level AS 层级, substr('          ', 1, level*2) || name AS 组织架构, path AS 路径
FROM tree ORDER BY path;

-- 查某个节点的所有下属（子树）
SELECT '⑥ 技术总监的所有下属' AS 说明;
WITH RECURSIVE sub(id, name) AS (
    SELECT id, name FROM emp WHERE name = '技术总监'
    UNION ALL
    SELECT e.id, e.name FROM emp e JOIN sub s ON e.boss = s.id
)
SELECT name FROM sub WHERE name != '技术总监';

-- 查某个节点到根的路径（向上追溯）
SELECT '⑦ 小王的汇报链' AS 说明;
WITH RECURSIVE up(id, name, boss) AS (
    SELECT id, name, boss FROM emp WHERE name = '小王'
    UNION ALL
    SELECT e.id, e.name, e.boss FROM emp e JOIN up u ON e.id = u.boss
)
SELECT name AS 汇报链 FROM up;

-- ============================================================
-- 六、小结
-- ============================================================
SELECT '⑧ 小结' AS 说明;
SELECT 'B+树索引' AS 主题, '数据只在叶子 + 叶子用链表相连 → 范围查询极快' AS 要点
UNION ALL SELECT '为什么不是二叉树', '磁盘 I/O 慢十万倍，多路让树变矮 → I/O 从 27 次降到 3 次'
UNION ALL SELECT '为什么不是哈希',   '哈希丢掉顺序，答不了 >、BETWEEN、ORDER BY、MIN/MAX'
UNION ALL SELECT '索引失效根因',     '索引按列的原始值排序；对列做运算就破坏了这个排序关系'
UNION ALL SELECT '最左前缀',         '复合索引像按"姓、名"排的电话簿，跳过第一列就用不上'
UNION ALL SELECT '树形数据',         '邻接表 + 递归 CTE，一条 SQL 遍历整棵树，避免 N+1';
