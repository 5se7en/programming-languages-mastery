-- 第 22 章 · 图 — SQL 示例
-- 运行：sqlite3 :memory: < graph.sql
-- 数据库里的图：邻接表存储（一张边表）+ 递归 CTE 查询

-- ============================================================
-- 一、存储：图就是一张边表
-- ============================================================

CREATE TABLE follows (follower TEXT, followee TEXT);   -- 有向图：谁关注了谁
INSERT INTO follows VALUES
 ('Alice','Bob'), ('Alice','Dave'),
 ('Bob','Carol'), ('Carol','Dave'), ('Dave','Eve');

SELECT '① 图的边表存储' AS 说明;
SELECT follower AS 起点, followee AS 终点 FROM follows;

-- 出度 / 入度（第 22 章的基本概念）
SELECT '② 每个人的出度与入度' AS 说明;
WITH people AS (
    SELECT follower AS p FROM follows UNION SELECT followee FROM follows
)
SELECT p AS 人,
       (SELECT COUNT(*) FROM follows WHERE follower = p) AS 出度_关注了几人,
       (SELECT COUNT(*) FROM follows WHERE followee = p) AS 入度_被几人关注
FROM people ORDER BY p;

-- ============================================================
-- 二、BFS：查「N 度人脉」——递归 CTE 就是 SQL 版的图遍历
-- ============================================================

SELECT '③ Alice 的 N 度人脉（BFS）' AS 说明;
WITH RECURSIVE reach(person, depth) AS (
    SELECT 'Alice', 0                                    -- 起点
    UNION ALL
    SELECT f.followee, r.depth + 1
    FROM follows f JOIN reach r ON f.follower = r.person
    WHERE r.depth < 3                                     -- ⚠️ 必须限制深度
)
-- 同一个人可能通过多条路径到达，取 MIN 才是最短距离（这正是 BFS 的思想）
SELECT person AS 人, MIN(depth) AS 最短距离
FROM reach WHERE person != 'Alice'
GROUP BY person ORDER BY 最短距离, person;

-- 注意 Dave：Alice→Dave 是 1 度，Alice→Bob→Carol→Dave 是 3 度
-- MIN(depth) 取到 1，这就是"最短路径"

-- ============================================================
-- 三、⚠️ 环：递归查询的头号杀手
-- ============================================================

CREATE TABLE dep (a TEXT, b TEXT);        -- a 依赖 b
INSERT INTO dep VALUES
 ('auth','user'), ('user','order'), ('order','payment'), ('payment','user');
--                                    ↑ user→order→payment→user 形成环！

SELECT '④ 有环的依赖数据' AS 说明;
SELECT a AS 模块, b AS 依赖 FROM dep;

-- ✗ 没有防护的递归查询在这份数据上会无限循环（此处不演示，会挂住数据库）
-- ✓ 防护手段 1：限制深度
SELECT '⑤ 防护手段1：限制递归深度' AS 说明;
WITH RECURSIVE walk(node, depth) AS (
    SELECT 'auth', 0
    UNION ALL
    SELECT d.b, w.depth + 1 FROM dep d JOIN walk w ON d.a = w.node
    WHERE w.depth < 6                                     -- 硬性上限
)
SELECT depth AS 深度, node AS 节点 FROM walk ORDER BY depth LIMIT 8;
-- 可以看到 user/order/payment 不断重复出现 —— 这就是在绕圈

-- ✓ 防护手段 2：记录路径，走过的不再走（真正的环检测）
SELECT '⑥ 防护手段2：路径记录 + 环检测' AS 说明;
WITH RECURSIVE walk(node, path, is_cycle) AS (
    SELECT 'auth', ',auth,', 0
    UNION ALL
    SELECT d.b,
           w.path || d.b || ',',
           CASE WHEN w.path LIKE '%,' || d.b || ',%' THEN 1 ELSE 0 END
    FROM dep d JOIN walk w ON d.a = w.node
    WHERE w.is_cycle = 0                                  -- 一旦发现环就停止扩展
)
SELECT node AS 节点, path AS 路径,
       CASE is_cycle WHEN 1 THEN '⚠️ 环在这里闭合' ELSE '' END AS 状态
FROM walk;

-- ============================================================
-- 四、拓扑排序思想：找入度为 0 的节点（构建顺序的第一步）
-- ============================================================

CREATE TABLE build (a TEXT, b TEXT);      -- a 必须在 b 之前构建
INSERT INTO build VALUES
 ('utils','db'), ('config','db'), ('db','api'),
 ('api','ui'), ('ui','app'), ('utils','api');

SELECT '⑦ 无环依赖：谁可以最先构建（入度为 0）' AS 说明;
WITH nodes AS (
    SELECT a AS n FROM build UNION SELECT b FROM build
)
SELECT n AS 模块,
       (SELECT COUNT(*) FROM build WHERE b = n) AS 入度,
       CASE WHEN (SELECT COUNT(*) FROM build WHERE b = n) = 0
            THEN '✓ 可以最先构建' ELSE '需等待依赖' END AS 状态
FROM nodes ORDER BY 入度, n;

-- 完整的拓扑排序需要"取出节点→更新入度→重复"的循环，
-- SQL 不擅长这类迭代过程；生产中通常在应用层做（见本章各语言示例）

SELECT '⑧ 用递归 CTE 求每个模块的构建层级' AS 说明;
WITH RECURSIVE layer(node, lvl) AS (
    -- 入度为 0 的是第 0 层
    SELECT n, 0 FROM (SELECT a AS n FROM build UNION SELECT b FROM build)
    WHERE (SELECT COUNT(*) FROM build WHERE b = n) = 0
    UNION ALL
    SELECT bd.b, l.lvl + 1 FROM build bd JOIN layer l ON bd.a = l.node
    WHERE l.lvl < 10
)
-- 取 MAX：一个模块必须等到它所有依赖都完成，所以取最深的那条路径
SELECT MAX(lvl) AS 构建层级, node AS 模块
FROM layer GROUP BY node ORDER BY 构建层级, node;

-- ============================================================
-- 五、小结
-- ============================================================
SELECT '⑨ 小结' AS 说明;
SELECT '图的存储' AS 主题, '邻接表 = 一张边表(起点, 终点[, 权重])' AS 要点
UNION ALL SELECT 'BFS',        '递归 CTE + MIN(depth) 求最短距离'
UNION ALL SELECT '⚠️ 环',      '无防护的递归查询遇到环会无限循环，能把 CPU 打满'
UNION ALL SELECT '防护1',      'WHERE depth < N —— 简单有效，生产必加'
UNION ALL SELECT '防护2',      '路径字段 + NOT LIKE 检测 —— 真正的环检测'
UNION ALL SELECT '拓扑排序',   'SQL 不擅长迭代过程，建议在应用层做'
UNION ALL SELECT '选型建议',   '图查询是业务核心时，考虑专门的图数据库';
