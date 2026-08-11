-- 索引：从 EXPLAIN 的视角看它什么时候被用上、什么时候被放弃。

CREATE TABLE orders (
  id       INTEGER PRIMARY KEY,
  user_id  INTEGER,
  status   TEXT,
  amount   INTEGER,
  created  TEXT
);
WITH RECURSIVE seq(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM seq WHERE n < 99999)
INSERT INTO orders
SELECT n, n % 5000,
       CASE n % 4 WHEN 0 THEN 'paid' WHEN 1 THEN 'shipped'
                  WHEN 2 THEN 'done' ELSE 'cancelled' END,
       n % 1000,
       '2026-' || printf('%02d', (n % 12) + 1) || '-' || printf('%02d', (n % 28) + 1)
FROM seq;

-- ① 主键索引是免费的，其他都要自己建
SELECT '① 主键（rowid）自带索引，其余列没有:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE id = 42;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id = 42;
SELECT '   → 主键 SEARCH，普通列 SCAN——「查得慢」的默认答案就是「这列没索引」' AS r;

-- ② 建索引后同一句 SQL 换了算法
CREATE INDEX idx_user ON orders(user_id);
SELECT '② CREATE INDEX 之后（SQL 一个字没改）:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id = 42;
SELECT '   → SCAN 变 SEARCH。第 47 章说的「声明式红利」在这里兑现' AS r;

-- ③ 索引能服务的四种条件形态
CREATE INDEX idx_amount ON orders(amount);
SELECT '③ 索引能服务哪些条件（B+ 树有序，所以这些都行）:' AS r;
SELECT '   等值 amount = 500:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE amount = 500;
SELECT '   范围 amount > 900:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE amount > 900;
SELECT '   区间 amount BETWEEN 100 AND 200:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE amount BETWEEN 100 AND 200;
SELECT '   排序 ORDER BY amount:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders ORDER BY amount LIMIT 10;
SELECT '   → 等值/范围/区间/排序全能服务——因为 B+ 树的叶子是【有序链表】（C++ 版实测）' AS r;

-- ④ 但这些条件用不上索引
SELECT '④ 用不上索引的四种写法:' AS r;
SELECT '   列上有表达式 amount + 0 = 500:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE amount + 0 = 500;
SELECT '   前导通配 status LIKE ''%ped'':' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE status LIKE '%ped';
SELECT '   否定条件 amount != 500:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE amount != 500;
SELECT '   → 前两种第 47 章实测过（慢 1283x / 420x）；第三种是新的:' AS r;
SELECT '     「不等于」意味着要取几乎所有行，走索引反而更慢——优化器主动放弃' AS r;

-- ⑤ 复合索引：列顺序就是能力边界
CREATE INDEX idx_us ON orders(user_id, status);
SELECT '⑤ 复合索引 (user_id, status) 的最左前缀:' AS r;
SELECT '   WHERE user_id = 1（第 1 列）:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders INDEXED BY idx_us WHERE user_id = 1;
SELECT '   WHERE user_id = 1 AND status = ''paid''（两列）:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders INDEXED BY idx_us WHERE user_id = 1 AND status = 'paid';
SELECT '   → 两种都能用；但只给 status（跳过第 1 列）就用不上（Python 版实测）' AS r;
SELECT '   → 建索引时把【选择性高的、等值查询的列】放前面' AS r;

-- ⑥ 部分索引：只索引你真正查的那部分行
CREATE INDEX idx_active ON orders(user_id) WHERE status != 'cancelled';
SELECT '⑥ 部分索引 WHERE status != ''cancelled'':' AS r;
EXPLAIN QUERY PLAN SELECT id FROM orders WHERE user_id = 7 AND status != 'cancelled';
SELECT '   索引只覆盖 ' || (SELECT COUNT(*) FROM orders WHERE status != 'cancelled') ||
       ' / ' || (SELECT COUNT(*) FROM orders) || ' 行' AS r;
SELECT '   → 体积更小、维护更便宜；「软删除」表的标配（大量 deleted=1 的行不必进索引）' AS r;

-- ⑦ 表达式索引：给「列上有函数」的查询一条活路
CREATE INDEX idx_month ON orders(substr(created, 6, 2));
SELECT '⑦ 表达式索引 —— ④ 里「列上有表达式」的解药:' AS r;
EXPLAIN QUERY PLAN SELECT COUNT(*) FROM orders WHERE substr(created, 6, 2) = '03';
SELECT '   → 把表达式本身建成索引，SCAN 又变回 SEARCH' AS r;
SELECT '   → PostgreSQL 同款: CREATE INDEX ON t (lower(email))' AS r;

-- ⑧ 索引的元信息
SELECT '⑧ 这张表上现在有 ' || COUNT(*) || ' 个索引:' AS r FROM sqlite_master
  WHERE type = 'index' AND tbl_name = 'orders' AND name NOT LIKE 'sqlite_%';
SELECT '   ' || name AS r FROM sqlite_master
  WHERE type = 'index' AND tbl_name = 'orders' AND name NOT LIKE 'sqlite_%' ORDER BY name;
SELECT '   → 每一个都在拖慢 INSERT（Python 版实测 4 个索引 → 插入慢 3.18x）' AS r;
SELECT '   → 定期检查「哪些索引从来没被用过」: PostgreSQL 查 pg_stat_user_indexes.idx_scan' AS r;
