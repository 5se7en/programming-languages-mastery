-- 性能优化：数据库侧的诊断流程——先看执行计划，再动手。

CREATE TABLE orders (
  id INTEGER PRIMARY KEY, user_id INTEGER, status TEXT, amount INTEGER, created TEXT
);
WITH RECURSIVE seq(n) AS (SELECT 0 UNION ALL SELECT n+1 FROM seq WHERE n < 199999)
INSERT INTO orders SELECT n, n % 20000,
  CASE n % 4 WHEN 0 THEN 'paid' WHEN 1 THEN 'shipped' WHEN 2 THEN 'done' ELSE 'cancelled' END,
  n % 1000, '2026-' || printf('%02d', (n % 12) + 1) || '-01'
FROM seq;

-- ① 诊断第一步：EXPLAIN，而不是猜
SELECT '① 慢查询诊断的第一步永远是看【执行计划】，不是读代码猜' AS r;
SELECT '   查询: SELECT * FROM orders WHERE user_id = 42' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id = 42;
SELECT '   → SCAN = 全表扫描 20 万行。这一行就是诊断结论，不需要任何猜测' AS r;

CREATE INDEX idx_user ON orders(user_id);
SELECT '   建索引后:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id = 42;
SELECT '   → SEARCH = 走索引（第 49 章实测过它带来的 551x）' AS r;

-- ② 但索引不是万能药：先确认它真的被用上
SELECT '② 索引被写法「废掉」的三种常见形态（第 47/49 章的汇总）:' AS r;
SELECT '   ⓐ 列上套函数/表达式:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id + 0 = 42;
SELECT '   ⓑ 前导通配的 LIKE:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE status LIKE '%ped';
SELECT '   ⓒ 低选择性列（命中太多行，走索引反而慢）:' AS r;
SELECT '      status=''paid'' 命中 ' || (SELECT COUNT(*) FROM orders WHERE status='paid') ||
       ' / ' || (SELECT COUNT(*) FROM orders) || ' 行 —— 第 49 章实测过这时全表扫更快' AS r;
SELECT '   → 建完索引一定要【再 EXPLAIN 一次】确认它被用上了' AS r;

-- ③ 覆盖索引：把「回表」这个随机 I/O 去掉
CREATE INDEX idx_user_amount ON orders(user_id, amount);
SELECT '③ 覆盖索引（第 49 章实测 1.9x）:' AS r;
EXPLAIN QUERY PLAN SELECT user_id, amount FROM orders WHERE user_id = 42;
SELECT '   → 计划里出现 COVERING INDEX = 需要的列全在索引里，不用回表' AS r;
SELECT '   → 而 SELECT * 保证了这个优化【永远用不上】（第 47 章）' AS r;

-- ④ 聚合下推：让数据库算，而不是拉回来自己算
SELECT '④ 同一个需求的两种写法:' AS r;
SELECT '   ⓐ 拉回全部行，应用里循环求和 → 传输 ' || (SELECT COUNT(*) FROM orders) || ' 行' AS r;
SELECT '   ⓑ SUM(amount) 下推给数据库    → 传输 1 行，结果 ' ||
       (SELECT SUM(amount) FROM orders WHERE user_id = 42) AS r;
SELECT '   → 第 51 章实测过这笔账: 传输 5 行 vs 100000 行，慢 86x' AS r;
SELECT '   → 「把计算送到数据那边」是数据库性能优化的第一原则（第 46 章）' AS r;

-- ⑤ 分页：OFFSET 的陷阱
SELECT '⑤ 深分页的代价:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders ORDER BY id LIMIT 10 OFFSET 100000;
SELECT '   → OFFSET 100000 要先【扫过并丢弃】前十万行，越翻越慢（第 44 章）' AS r;
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE id > 100000 ORDER BY id LIMIT 10;
SELECT '   → 键集分页（WHERE id > 上次末尾）: 直接定位，与页码无关' AS r;

-- ⑥ 数据库性能诊断的完整顺序
SELECT '⑥ 诊断顺序（从最可能到最不可能）:' AS r;
SELECT '   ⓐ 查询条数: 是不是 N+1？（第 51 章实测 201 条 SQL vs 1 条）' AS r;
SELECT '   ⓑ 执行计划: SCAN 还是 SEARCH？（①）' AS r;
SELECT '   ⓒ 返回数据量: SELECT * 拉了多少用不上的列？（③④）' AS r;
SELECT '   ⓓ 锁与事务: 是不是在等锁？事务是不是太长？（第 48/50 章）' AS r;
SELECT '   ⓔ 最后才是: 加索引、改 schema、加缓存' AS r;
SELECT '   → 注意 ⓐ~ⓒ 都【不需要改数据库】——绝大多数「数据库慢」其实是查询写法问题' AS r;

-- ⑦ 慢查询日志：生产环境的 profiler
SELECT '⑦ 生产环境怎么找慢查询:' AS r;
SELECT '   PostgreSQL: log_min_duration_statement + pg_stat_statements（按总耗时排序）' AS r;
SELECT '   MySQL:      slow_query_log + performance_schema' AS r;
SELECT '   → pg_stat_statements 的关键: 它按【总耗时】排序，而不是单次耗时——' AS r;
SELECT '     一条 1ms 但每秒跑一万次的查询，比一条 3 秒但每天跑一次的更值得优化' AS r;
SELECT '   → 这正是阿姆达尔定律在数据库侧的形态（Python 版 ④ 实测）' AS r;
