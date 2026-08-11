-- SQL：声明式的全部含义——说「要什么」，优化器决定「怎么找」。

CREATE TABLE users  (id INTEGER PRIMARY KEY, name TEXT, city TEXT, score INTEGER);
CREATE TABLE orders (id INTEGER PRIMARY KEY, user_id INTEGER, amount INTEGER);
WITH RECURSIVE seq(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM seq WHERE n < 9999)
INSERT INTO users SELECT n, 'user-' || n, 'city-' || (n % 10), n % 100 FROM seq;
WITH RECURSIVE seq(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM seq WHERE n < 49999)
INSERT INTO orders SELECT n, (n * 7919) % 12000, n % 500 FROM seq;
-- 注意: user_id 撒到 0..11999，而 users 只有 0..9999 —— 故意留下「孤儿订单」

-- ① 同一个问题的三种写法：答案必然相同，计划【未必】相同
SELECT '① 问题:「有订单的用户数」——三种写法，答案都是:' AS r;
SELECT '   IN 子查询:     ' || (SELECT COUNT(*) FROM users WHERE id IN (SELECT user_id FROM orders)) AS r;
SELECT '   EXISTS:        ' || (SELECT COUNT(*) FROM users u WHERE EXISTS
                             (SELECT 1 FROM orders o WHERE o.user_id = u.id)) AS r;
SELECT '   JOIN+DISTINCT: ' || (SELECT COUNT(DISTINCT u.id) FROM users u
                                JOIN orders o ON o.user_id = u.id) AS r;
SELECT '   ↓ 但 EXPLAIN 揭穿了「三种写法总会殊途同归」这个流传甚广的说法:' AS r;
SELECT '   IN 的计划 →' AS r;
EXPLAIN QUERY PLAN SELECT COUNT(*) FROM users WHERE id IN (SELECT user_id FROM orders);
SELECT '   EXISTS 的计划 →' AS r;
EXPLAIN QUERY PLAN SELECT COUNT(*) FROM users u WHERE EXISTS
  (SELECT 1 FROM orders o WHERE o.user_id = u.id);
SELECT '   → IN:     LIST SUBQUERY —— 子查询【物化一次】，再用主键 SEARCH 逐个查' AS r;
SELECT '   → EXISTS: CORRELATED SCALAR SUBQUERY —— 外层【每一行】都重跑一次子查询' AS r;
SELECT '   → 计划截然不同！Python 版实测了后果: 同题 EXISTS 比 IN 慢 34x' AS r;
SELECT '   → 结论: 优化器能归一【很多】写法，但相关子查询(correlated)常常归一不掉' AS r;

-- ② 书写顺序 ≠ 执行顺序
SELECT '② SELECT 写在最前，执行在倒数第二——逻辑执行顺序:' AS r;
SELECT '   FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT' AS r;
SELECT '   → 所以 WHERE 里用不了 SELECT 起的别名（它还没执行到）' AS r;
SELECT '   → 而 ORDER BY 可以用（它在 SELECT 之后）——sqlite 实测:' AS r;
SELECT '   score 前三名: ' || group_concat(s, ', ')
  FROM (SELECT score AS s FROM users ORDER BY s DESC LIMIT 3);

-- ③ 让优化器缴械：对索引列做手脚
CREATE INDEX idx_score ON users(score);
SELECT '③ 同一个条件的两种写法，计划天差地别:' AS r;
SELECT '   WHERE score = 42          → ' AS r;
EXPLAIN QUERY PLAN SELECT COUNT(*) FROM users WHERE score = 42;
SELECT '   WHERE score + 0 = 42      → ' AS r;
EXPLAIN QUERY PLAN SELECT COUNT(*) FROM users WHERE score + 0 = 42;
SELECT '   → 列上套任何表达式/函数，索引即失效（SEARCH 退化为 SCAN）' AS r;
SELECT '   → LIKE ''abc%'' 能用索引，LIKE ''%abc'' 不能——前缀才符合 B 树的有序性（第 21 章）' AS r;

-- ④ JOIN 语义：INNER 丢孤儿，LEFT 留孤儿
SELECT '④ INNER JOIN 行数: ' || (SELECT COUNT(*) FROM orders o JOIN users u ON o.user_id = u.id) AS r;
SELECT '   LEFT  JOIN 行数: ' || (SELECT COUNT(*) FROM orders o LEFT JOIN users u ON o.user_id = u.id) AS r;
SELECT '   孤儿订单（user 已不存在）: ' ||
       (SELECT COUNT(*) FROM orders o LEFT JOIN users u ON o.user_id = u.id WHERE u.id IS NULL) AS r;
SELECT '   → LEFT JOIN + IS NULL 是「找孤儿」的标准姿势；差值正是 INNER 悄悄丢掉的行' AS r;

-- ⑤ GROUP BY 折叠行，窗口函数保留行
SELECT '⑤ GROUP BY: 每城市一行（折叠）——' AS r;
SELECT '   ' || city || ' 平均分 ' || ROUND(AVG(score), 1)
  FROM users GROUP BY city ORDER BY AVG(score) DESC LIMIT 3;
SELECT '   窗口函数: 每行保留，附上组内信息——前 3 行:' AS r;
SELECT '   ' || name || ' 在 ' || city || ' 排名 ' ||
       RANK() OVER (PARTITION BY city ORDER BY score DESC)
  FROM users ORDER BY id LIMIT 3;
SELECT '   → 「要汇总还是要明细+汇总」——GROUP BY 与窗口函数的分界' AS r;

-- ⑥ NULL 的三值逻辑
SELECT '⑥ NULL = NULL 的结果: ' || COALESCE((SELECT 1 WHERE NULL = NULL), '不是真（是 NULL）') AS r;
SELECT '   NULL 要用 IS NULL 判断: ' || (SELECT COUNT(*) FROM (SELECT NULL AS x) WHERE x IS NULL) || ' 行' AS r;
CREATE TABLE t6 (v INTEGER);
INSERT INTO t6 VALUES (1), (NULL), (3);
SELECT '   COUNT(*) = ' || (SELECT COUNT(*) FROM t6) || '，COUNT(v) = ' || (SELECT COUNT(v) FROM t6)
       || '，SUM(v) = ' || (SELECT SUM(v) FROM t6) AS r;
SELECT '   → COUNT(列) 跳过 NULL；WHERE v != 5 也【不会】返回 NULL 行——三值逻辑的连环坑' AS r;

-- ⑦ CTE：给查询分层
SELECT '⑦ CTE 把嵌套子查询摊平成流水线:' AS r;
WITH spend AS (
  SELECT user_id, SUM(amount) AS total FROM orders GROUP BY user_id
), ranked AS (
  SELECT u.city, s.total FROM spend s JOIN users u ON u.id = s.user_id
)
SELECT '   消费额最高的城市: ' || city || '（合计 ' || SUM(total) || '）'
  FROM ranked GROUP BY city ORDER BY SUM(total) DESC LIMIT 1;
SELECT '   → WITH 子句 = 查询里的「局部变量」（第 8 章的动机在 SQL 里重演）' AS r;
