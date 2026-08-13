-- 测试：数据库世界的三种「断言」——约束、断言查询、回滚隔离。

-- ① 约束就是「每次写入都自动运行的断言」
CREATE TABLE orders (
  id      INTEGER PRIMARY KEY,
  user_id INTEGER NOT NULL,                          -- 断言: 必须有用户
  cents   INTEGER NOT NULL CHECK (cents > 0),        -- 断言: 金额为正
  status  TEXT NOT NULL CHECK (status IN ('paid', 'shipped', 'done', 'cancelled'))
);
INSERT INTO orders VALUES (1, 100, 1250, 'paid'), (2, 101, 5000, 'shipped');

SELECT '① 约束 = 永远在线的断言（第 46 章的 C，从测试视角再看一遍）' AS r;
INSERT OR IGNORE INTO orders VALUES (3, 102, -50, 'paid');       -- 违反 CHECK
SELECT '   插入 -50 分的订单: changes=' || changes() || '（被 CHECK 拦下）' AS r;
INSERT OR IGNORE INTO orders VALUES (4, 103, 100, 'flying');     -- 非法状态
SELECT '   插入状态 flying: changes=' || changes() || '（枚举 CHECK 拦下）' AS r;
SELECT '   → 单元测试跑一次，约束【每次写入】都跑——它是数据层的最后防线' AS r;
SELECT '   → 应用层校验可以被绕过（新代码、手工 SQL、别的服务），约束不能' AS r;

-- ② 断言查询：SELECT 出「违反业务规则的行」，0 行 = 通过
SELECT '② 断言查询——数据质量测试（dbt tests 的原理）:' AS r;
SELECT '   规则 A「不允许孤儿状态」: 违规 ' ||
       (SELECT COUNT(*) FROM orders WHERE status NOT IN ('paid','shipped','done','cancelled'))
       || ' 行 → ' ||
       CASE WHEN (SELECT COUNT(*) FROM orders
                  WHERE status NOT IN ('paid','shipped','done','cancelled')) = 0
            THEN '通过 ✓' ELSE '失败 ✗' END AS r;
SELECT '   规则 B「已发货订单金额 ≥ 1 元」: 违规 ' ||
       (SELECT COUNT(*) FROM orders WHERE status = 'shipped' AND cents < 100)
       || ' 行 → 通过 ✓' AS r;
SELECT '   → 把业务规则写成「找违规行」的查询: 结果为空 = 断言成立' AS r;
SELECT '   → 这类测试跑在【生产数据】上——单元测试测代码，数据测试测数据' AS r;

-- ③ 回滚隔离：数据库测试的标准姿势
SELECT '③ 事务回滚 = 测试隔离（第 48 章的 A，从测试视角再看一遍）:' AS r;
SELECT '   测试前订单数: ' || (SELECT COUNT(*) FROM orders) AS r;
BEGIN;                                               -- 测试开始: 开事务
  INSERT INTO orders VALUES (10, 999, 777, 'paid');  -- 造测试数据
  UPDATE orders SET status = 'done' WHERE id = 1;    -- 执行被测操作
  SELECT '   事务内（测试中）订单数: ' || (SELECT COUNT(*) FROM orders) ||
         '，订单 1 状态: ' || (SELECT status FROM orders WHERE id = 1) AS r;
ROLLBACK;                                            -- 测试结束: 全部回滚
SELECT '   回滚后订单数: ' || (SELECT COUNT(*) FROM orders) ||
       '，订单 1 状态: ' || (SELECT status FROM orders WHERE id = 1) ||
       '（分毫未动）' AS r;
SELECT '   → 每个测试一个事务，结束就 ROLLBACK——不用手写清理代码，也不会互相污染' AS r;
SELECT '   → Django/Rails/Spring 的数据库测试默认就是这么做的' AS r;
SELECT '   → 边界: 被测代码自己 COMMIT 或开新连接时，这招失效（得换 truncate/重建）' AS r;

-- ④ 测试数据的两难：造的 vs 真的
SELECT '④ 测试数据从哪来:' AS r;
SELECT '   手工造（本例）  : 可控、稳定——但覆盖不了真实数据的怪状' AS r;
SELECT '   生产数据脱敏副本: 真实——但慢、有隐私风险、且【每次都不一样】(不可复现)' AS r;
SELECT '   → 常见分工: 单元/集成用手工数据，数据质量断言（②）跑生产' AS r;

-- ⑤ 幂等性：数据侧最值得测的性质（C++ 版 ⑤ 的不变量清单之一）
SELECT '⑤ 幂等性断言——「同一操作执行两次 = 执行一次」:' AS r;
CREATE TABLE inventory (sku TEXT PRIMARY KEY, qty INTEGER NOT NULL);
INSERT INTO inventory VALUES ('SKU-1', 10);
-- 幂等的写法: 用 UPSERT 按目标值设置（而非累加）
INSERT INTO inventory VALUES ('SKU-1', 8)
  ON CONFLICT(sku) DO UPDATE SET qty = excluded.qty;
INSERT INTO inventory VALUES ('SKU-1', 8)
  ON CONFLICT(sku) DO UPDATE SET qty = excluded.qty;  -- 故意重复执行
SELECT '   UPSERT 设值执行两次后 qty = ' || (SELECT qty FROM inventory WHERE sku='SKU-1') ||
       '（幂等 ✓——重试安全）' AS r;
SELECT '   对照: 若写成 qty = qty - 2 的累加式，重复执行就是 6 而不是 8（重试不安全）' AS r;
SELECT '   → 消息重投、接口重试无处不在（第 48 章的重试逻辑）——幂等是必测性质' AS r;

-- ⑥ 数据库侧测试清单
SELECT '⑥ 数据库测试的完整清单:' AS r;
SELECT '   约束（①）        —— 声明一次，每次写入都验证' AS r;
SELECT '   断言查询（②）    —— 业务规则写成「找违规行」，定时跑在真实数据上' AS r;
SELECT '   回滚隔离（③）    —— 测试互不污染的标准姿势' AS r;
SELECT '   迁移测试          —— 新 schema 在生产数据副本上演练（升级不能回滚才最疼）' AS r;
SELECT '   幂等断言（⑤）    —— 一切会被重试的写操作' AS r;
