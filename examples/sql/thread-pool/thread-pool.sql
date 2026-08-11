-- 线程池：数据库世界叫「连接池」——同一个思想，代价更高。

-- ① 为什么数据库必须池化：一条连接就是一条线程/进程
SELECT '① PostgreSQL: 每条连接 = 一个 OS 进程（第 39 章实测 fork 成本）' AS r;
SELECT '   MySQL:      每条连接 = 一条 OS 线程（第 40 章实测 1MB 栈）' AS r;
SELECT '   建立连接还要 TCP 握手 + 认证 + 会话初始化 —— 通常 10~100 ms' AS r;
SELECT '   → 每请求新建连接，等于每请求新建一条线程 + 一次网络往返（最贵的池化场景）' AS r;

-- ② 连接池大小的经典公式（HikariCP，来自 PostgreSQL 官方性能测试）
--    connections = ((core_count * 2) + effective_spindle_count)
--    注意它比「线程数 = 核心数 × (1+等待/计算)」小得多——因为数据库端才是瓶颈
WITH RECURSIVE cpu(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM cpu WHERE n < 5)
SELECT '② 核心数 ' || (n*2) || ' → 推荐连接池大小 ' || ((n*2)*2 + 1) ||
       '（公式: 核心数×2 + 磁盘数）' AS r
  FROM cpu;
SELECT '   ↑ 16 核的机器推荐 ~33 条连接，不是 200 条 —— 这是最反直觉的一条' AS r;

-- ③ 为什么池【太大】反而更慢：排队论
--    池大小 > 服务能力时，多出来的请求只是从「客户端排队」变成「数据库里排队」
--    而且每条都要参与锁竞争（第 41 章）与上下文切换（第 40 章）→ 净损失
WITH RECURSIVE pool(size) AS (SELECT 1 UNION ALL SELECT size*2 FROM pool WHERE size < 64)
SELECT '③ 池大小 ' || printf('%3d', size) ||
       ' → 100 个 10ms 任务理论耗时 ' || printf('%4d', ((100 + size - 1)/size) * 10) || ' ms' ||
       CASE WHEN size >= 16 THEN '   ← 收益已趋平，再大只增加竞争' ELSE '' END AS r
  FROM pool;
SELECT '   ↑ 这是排队论模型（非实测）：吞吐随池增大而饱和，延迟却随竞争线性上涨' AS r;

-- ④ 池耗尽 = 全站雪崩（连接池最常见的生产事故）
SELECT '④ 一个慢查询占住连接 10 秒 × 20 条连接被占满 → 第 21 个请求开始排队' AS r;
SELECT '   请求超时 → 客户端重试 → 队列更长 → 雪崩（与线程池的队头阻塞完全同构）' AS r;
SELECT '   → 三道防线: ① 连接超时 ② 语句超时（statement_timeout）③ 按用途拆池（舱壁）' AS r;

-- ⑤ 事务与连接的绑定：连接池最隐蔽的陷阱
CREATE TABLE account (id INTEGER PRIMARY KEY, balance INTEGER);
INSERT INTO account VALUES (1, 100), (2, 100);
BEGIN;
UPDATE account SET balance = balance - 30 WHERE id = 1;
UPDATE account SET balance = balance + 30 WHERE id = 2;
COMMIT;
SELECT '⑤ 事务期间连接【不能还给池】—— 事务状态就住在这条连接里' AS r;
SELECT '   余额校验: ' || (SELECT SUM(balance) FROM account) || '（事务内两次 UPDATE 必须同一条连接）' AS r;
SELECT '   → 长事务 = 长期霸占池里一个名额，比慢查询更隐蔽' AS r;

-- ⑥ 预编译语句：连接池之外的第二层复用
--    与线程池同构：昂贵的东西建一次、用很多次
SELECT '⑥ 预编译语句(prepared statement)也是池化: 解析+优化只做一次，之后只传参数' AS r;
SELECT '   注意: 预编译语句【绑定在连接上】——池里换了连接就得重新准备' AS r;
SELECT '   → 这也是 PgBouncer 的 transaction 模式不能用预编译语句的原因' AS r;

-- ⑦ 三层池化的对应关系
SELECT '⑦ 线程池 → 连接池 → 对象池: 都是「创建昂贵、生命周期短」的同一个答案' AS r;
SELECT '   线程池: 省下 pthread_create + 1MB 栈' AS r;
SELECT '   连接池: 省下 TCP 握手 + 认证 + 服务端进程/线程' AS r;
SELECT '   对象池: 省下 GC 压力（第 36 章）—— 但现代 GC 下常常得不偿失' AS r;
