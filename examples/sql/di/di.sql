-- 依赖注入：数据库侧的「可替换实现」——视图作为接口、Repository 模式、以及测试替身。

-- ① 视图 = 数据库层的接口（调用方依赖它，不依赖底层表结构）
CREATE TABLE users_v1 (id INTEGER PRIMARY KEY, full_name TEXT, city TEXT);
INSERT INTO users_v1 VALUES (1, '张三', '北京'), (2, '李四', '上海');

CREATE VIEW users AS SELECT id, full_name AS name, city FROM users_v1;
SELECT '① 视图是数据库层的【接口】: 应用查 users，不直接碰 users_v1' AS r;
SELECT '   查询 users: ' || group_concat(name || '@' || city, ', ') AS r FROM users;

-- 底层表重构（改列名、拆表），只要视图签名不变，调用方一行都不用改
ALTER TABLE users_v1 RENAME COLUMN full_name TO name_text;
DROP VIEW users;
CREATE VIEW users AS SELECT id, name_text AS name, city FROM users_v1;
SELECT '   底层列名从 full_name 改成 name_text 后，同样的查询: ' ||
       group_concat(name || '@' || city, ', ') AS r FROM users;
SELECT '   → 与第 28 章「依赖接口而非实现」完全同构: 视图就是那个接口' AS r;
SELECT '   → 依赖倒置原则的数据库版: 应用不依赖表，表也不依赖应用，两者都依赖【视图契约】' AS r;

-- ② Repository 模式: 把「怎么存」藏在一层之后
SELECT '② Repository = 应用与存储之间的那层接口:' AS r;
SELECT '   应用调 userRepo.findById(1)，不关心背后是 SQL / 内存 / HTTP' AS r;
SELECT '   → 于是测试可以注入【内存版 Repository】——第 52 章 C# 版实测过的 FakeRepo' AS r;
SELECT '   → 也可以在不改业务代码的前提下把 sqlite 换成 PostgreSQL' AS r;

-- ③ 测试替身：用临时表/视图替换真实数据源
CREATE TABLE orders_real (id INTEGER PRIMARY KEY, user_id INTEGER, amount INTEGER);
INSERT INTO orders_real VALUES (1, 1, 5000), (2, 1, 3000), (3, 2, 100);

CREATE VIEW order_stats AS
  SELECT u.name, COUNT(o.id) AS cnt, COALESCE(SUM(o.amount), 0) AS total
  FROM users u LEFT JOIN orders_real o ON o.user_id = u.id GROUP BY u.id;
SELECT '③ 生产视图 order_stats（依赖真实的 orders_real 表）:' AS r;
SELECT '   ' || name || ': ' || cnt || ' 单, 共 ' || total AS r FROM order_stats ORDER BY name;

SELECT '   → 测试时的「注入」手法: 在测试库里建同名但数据可控的表' AS r;
SELECT '   → 或用 CTE 把数据源参数化（下方 ④）——SQL 版的「依赖注入」' AS r;

-- ④ CTE 参数化数据源：把「从哪取数」变成可替换的部分
WITH source_orders(id, user_id, amount) AS (
  VALUES (101, 1, 99), (102, 1, 1)          -- ← 这就是注入进来的「测试替身」
)
SELECT '④ 同一段统计逻辑，喂给它【替身数据】:' AS r
UNION ALL
SELECT '   ' || u.name || ': ' || COUNT(s.id) || ' 单, 共 ' || COALESCE(SUM(s.amount), 0)
FROM users u LEFT JOIN source_orders s ON s.user_id = u.id
GROUP BY u.id ORDER BY 1;
SELECT '   → CTE 让「数据从哪来」成为查询的参数——逻辑与数据源解耦' AS r;
SELECT '   → dbt 的 ref()/source() 宏把这件事做成了工程实践（第 54 章提过）' AS r;

-- ⑤ 存储过程与函数：依赖的另一种注入方式
SELECT '⑤ 更多可替换点:' AS r;
SELECT '   PRAGMA/SET 参数    → 运行时配置注入（第 46 章 synchronous、第 48 章隔离级别）' AS r;
SELECT '   自定义函数(UDF)     → 应用把函数【注册】进数据库，SQL 里调用它' AS r;
SELECT '      sqlite3 的 create_function() 就是把 Python 函数注入 SQL 执行引擎' AS r;
SELECT '   外部表/FDW         → 让「表」背后其实是另一个数据库或 API' AS r;
SELECT '   → 共同点: 让【契约】固定，让【实现】可换——与前五节的 DI 是同一句话' AS r;

-- ⑥ 但数据库层的「注入」有它的边界
SELECT '⑥ 边界与代价:' AS r;
SELECT '   视图不是免费的: 多层视图嵌套会让优化器难以下推谓词（第 47 章）' AS r;
SELECT '   Repository 抽象过度: 把 SQL 能力抽象成 CRUD 接口，会丢掉 JOIN/窗口函数（第 47 章）' AS r;
SELECT '   → 与应用层 DI 同样的判据: 【你真的需要换掉它吗】' AS r;
SELECT '   → 只有一个实现的接口，在数据库层和应用层都是同一种过度设计' AS r;
