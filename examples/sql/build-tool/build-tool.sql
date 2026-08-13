-- 构建工具：数据库里的「增量构建」——物化视图与增量刷新，同一个问题的数据版。

CREATE TABLE orders (
  id INTEGER PRIMARY KEY, user_id INTEGER, amount INTEGER, day TEXT
);
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 20000)
INSERT INTO orders SELECT n, n % 500, n % 200, '2026-08-' || printf('%02d', (n % 28) + 1) FROM seq;

-- ① 「视图」= 每次查询都重算（相当于每次都全量构建）
CREATE VIEW daily_totals_view AS
  SELECT day, COUNT(*) AS cnt, SUM(amount) AS total FROM orders GROUP BY day;
SELECT '① 普通视图 = 每次查询都【重新计算】（全量构建）' AS r;
SELECT '   查询结果前两天: ' || group_concat(day || '=' || total, ', ') AS r
  FROM (SELECT day, total FROM daily_totals_view ORDER BY day LIMIT 2);
SELECT '   → 数据没变也要重算 —— 相当于每次 make 都 make clean' AS r;

-- ② 「物化视图」= 把结果存下来（相当于构建产物）
CREATE TABLE daily_totals (day TEXT PRIMARY KEY, cnt INTEGER, total INTEGER);
INSERT INTO daily_totals SELECT day, COUNT(*), SUM(amount) FROM orders GROUP BY day;
SELECT '② 物化视图 = 把计算结果【存成表】（构建产物）' AS r;
SELECT '   已物化 ' || (SELECT COUNT(*) FROM daily_totals) || ' 天的汇总' AS r;
SELECT '   查询变成直接读表——但产物会【过期】，这就引出了刷新策略' AS r;

-- ③ 全量刷新 vs 增量刷新（构建世界的同一道选择题）
INSERT INTO orders VALUES (99001, 7, 500, '2026-08-01'), (99002, 8, 300, '2026-08-01');
SELECT '③ 新到 2 笔订单（都在 2026-08-01）后，两种刷新策略:' AS r;
SELECT '   全量刷新: DELETE FROM daily_totals; INSERT ... GROUP BY day  → 重算全部 28 天' AS r;
SELECT '   增量刷新: 只更新【受影响的那一天】 → 重算 1 天' AS r;

-- 增量刷新: 只重算受影响的分区（= 只重编受影响的目标）
DELETE FROM daily_totals WHERE day IN (SELECT DISTINCT day FROM orders WHERE id >= 99001);
INSERT INTO daily_totals
  SELECT day, COUNT(*), SUM(amount) FROM orders
  WHERE day IN (SELECT DISTINCT day FROM orders WHERE id >= 99001)
  GROUP BY day;
SELECT '   增量刷新后 2026-08-01: cnt=' || (SELECT cnt FROM daily_totals WHERE day='2026-08-01')
       || ', total=' || (SELECT total FROM daily_totals WHERE day='2026-08-01') AS r;
SELECT '   校验（与实时计算比对）: ' ||
       CASE WHEN (SELECT total FROM daily_totals WHERE day='2026-08-01')
                 = (SELECT SUM(amount) FROM orders WHERE day='2026-08-01')
            THEN '✓ 一致' ELSE '✗ 不一致' END AS r;
SELECT '   → 「只重建受影响的部分」——与 Python 版 ② 的增量构建是同一个算法' AS r;
SELECT '   → 关键前提也一样: 你得【准确知道】哪些产物受影响（依赖图的完整性）' AS r;

-- ④ 增量的正确性陷阱：漏掉一条依赖边
UPDATE orders SET amount = 9999 WHERE id = 1;      -- 改了一条【老】数据
SELECT '④ 增量的翻车: 改了 id=1 的老订单（属于 2026-08-02）' AS r;
SELECT '   若增量刷新只看「新插入的行」，这次 UPDATE 就【看不见】:' AS r;
SELECT '   物化表里 2026-08-02 的 total = ' || (SELECT total FROM daily_totals WHERE day='2026-08-02') AS r;
SELECT '   实时计算的 total       = ' || (SELECT SUM(amount) FROM orders WHERE day='2026-08-02') AS r;
SELECT '   → 产物【悄悄过期】——与 Java 版的常量内联、Python 版的 mtime 假阴性完全同构' AS r;
SELECT '   → 三个例子，同一条教训: 增量的正确性 = 依赖追踪的完整性' AS r;

-- ⑤ 用触发器把依赖追踪做完整（等价于构建系统的自动依赖发现）
CREATE TRIGGER orders_ai AFTER INSERT ON orders BEGIN
  INSERT INTO daily_totals(day, cnt, total) VALUES (NEW.day, 1, NEW.amount)
    ON CONFLICT(day) DO UPDATE SET cnt = cnt + 1, total = total + NEW.amount;
END;
CREATE TRIGGER orders_au AFTER UPDATE OF amount ON orders BEGIN
  UPDATE daily_totals SET total = total - OLD.amount + NEW.amount WHERE day = NEW.day;
END;
-- 先修正 ④ 造成的偏差，再验证触发器
UPDATE daily_totals SET total = (SELECT SUM(amount) FROM orders WHERE day = daily_totals.day),
                        cnt   = (SELECT COUNT(*)    FROM orders WHERE day = daily_totals.day);
UPDATE orders SET amount = 7777 WHERE id = 1;
INSERT INTO orders VALUES (99003, 9, 111, '2026-08-03');
SELECT '⑤ 加了 INSERT/UPDATE 触发器后（自动增量维护）:' AS r;
SELECT '   2026-08-02 物化=' || (SELECT total FROM daily_totals WHERE day='2026-08-02') ||
       ' 实时=' || (SELECT SUM(amount) FROM orders WHERE day='2026-08-02') ||
       CASE WHEN (SELECT total FROM daily_totals WHERE day='2026-08-02')
                 = (SELECT SUM(amount) FROM orders WHERE day='2026-08-02') THEN ' ✓' ELSE ' ✗' END AS r;
SELECT '   2026-08-03 物化=' || (SELECT total FROM daily_totals WHERE day='2026-08-03') ||
       ' 实时=' || (SELECT SUM(amount) FROM orders WHERE day='2026-08-03') ||
       CASE WHEN (SELECT total FROM daily_totals WHERE day='2026-08-03')
                 = (SELECT SUM(amount) FROM orders WHERE day='2026-08-03') THEN ' ✓' ELSE ' ✗' END AS r;
SELECT '   → 触发器 = 构建系统的【自动依赖发现】: 不靠人记得声明，靠机制捕获每次变更' AS r;
SELECT '   → 代价也一样: 每次写入都变慢（第 49 章实测索引写放大，同一笔账）' AS r;

-- ⑥ 数据构建与代码构建的同构对照
SELECT '⑥ 同构对照表:' AS r;
SELECT '   源文件      ↔ 基础表(orders)' AS r;
SELECT '   构建产物    ↔ 物化视图(daily_totals)' AS r;
SELECT '   依赖图      ↔ 视图定义里的表引用' AS r;
SELECT '   增量构建    ↔ 增量刷新（只重算受影响分区，③）' AS r;
SELECT '   自动依赖发现 ↔ 触发器 / CDC 变更捕获（⑤）' AS r;
SELECT '   clean build ↔ 全量刷新（永远正确，永远慢）' AS r;
SELECT '   → dbt 把这套完整搬进了数据工程: 模型即目标、DAG 调度、增量物化策略' AS r;
