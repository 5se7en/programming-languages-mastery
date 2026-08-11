-- 数据库锁：从 SQL 一侧看锁的种类、语法与死锁的规避。

CREATE TABLE account (id INTEGER PRIMARY KEY, owner TEXT, balance INTEGER);
INSERT INTO account VALUES (1, '甲', 100), (2, '乙', 100), (3, '丙', 100);
CREATE TABLE seat (id INTEGER PRIMARY KEY, row_no INTEGER, taken INTEGER);
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 100)
INSERT INTO seat SELECT n, (n-1)/10 + 1, 0 FROM seq;

-- ① 两种基本锁与它们的相容性
SELECT '① 两种基本锁:' AS r;
SELECT '   S 共享锁(shared)  : 读时持有，允许别人也读，不许写' AS r;
SELECT '   X 排他锁(exclusive): 写时持有，别人既不能读也不能写' AS r;
SELECT '   相容矩阵: S+S 相容；S+X、X+S、X+X 全部互斥（C++ 版实现了这张表）' AS r;
SELECT '   → 一条规则推出全部锁行为: 「读读不冲突，只要沾上写就冲突」' AS r;

-- ② 显式加锁的语法（跨数据库对照）
SELECT '② 显式加锁的写法:' AS r;
SELECT '   SELECT ... FOR UPDATE   → 加 X 锁（PostgreSQL / MySQL / Oracle）' AS r;
SELECT '   SELECT ... FOR SHARE    → 加 S 锁（MySQL 8 用 FOR SHARE，旧版 LOCK IN SHARE MODE）' AS r;
SELECT '   SELECT ... FOR UPDATE NOWAIT      → 拿不到就【立刻报错】，不等待' AS r;
SELECT '   SELECT ... FOR UPDATE SKIP LOCKED → 跳过被锁的行（任务队列的标准写法）' AS r;
SELECT '   sqlite: 没有这些语法，用 BEGIN IMMEDIATE 代替（Python 版实测）' AS r;

-- ③ 读后写为什么必须加锁
BEGIN;
SELECT '③ 危险写法: 先 SELECT 读余额，再按读到的值 UPDATE' AS r;
SELECT '   SELECT balance FROM account WHERE id=1;  → 读到 ' ||
       (SELECT balance FROM account WHERE id=1) AS r;
SELECT '   -- 此刻别的事务可能已经把它改了 --' AS r;
SELECT '   UPDATE account SET balance = 90 WHERE id=1;  ← 用的是【过期的值】' AS r;
UPDATE account SET balance = balance - 10 WHERE id = 1;   -- 正确写法: 让数据库自己算
COMMIT;
SELECT '   ✓ 正确写法一: UPDATE account SET balance = balance - 10（原子，无需读）' AS r;
SELECT '   ✓ 正确写法二: SELECT ... FOR UPDATE（读的时候就锁住）' AS r;
SELECT '   当前余额: ' || (SELECT balance FROM account WHERE id=1) AS r;

-- ④ SKIP LOCKED：任务队列的正确姿势
SELECT '④ 任务队列为什么需要 SKIP LOCKED:' AS r;
SELECT '   N 个 worker 同时抢任务，若用普通 FOR UPDATE:' AS r;
SELECT '     worker 2..N 全都【排队等 worker 1】→ 并发度退化成 1' AS r;
SELECT '   用 FOR UPDATE SKIP LOCKED:' AS r;
SELECT '     每个 worker 自动跳过别人锁住的行，各拿各的 → 真并发' AS r;
SELECT '   典型写法: SELECT id FROM job WHERE state=''pending''' AS r;
SELECT '             ORDER BY id LIMIT 10 FOR UPDATE SKIP LOCKED;' AS r;

-- ⑤ 间隙锁：挡住幻读的代价
SELECT '⑤ 间隙锁(gap lock) —— MySQL RR 挡住幻读的手段（第 48 章的伏笔）:' AS r;
SELECT '   普通行锁只能锁住【已存在的行】，挡不住别人【插入新行】（幻读）' AS r;
SELECT '   间隙锁锁住行【之间的空隙】: 锁了 (10, 20) 这个区间，就没人能插入 15' AS r;
SELECT '   代价一: 锁的范围远大于你实际读到的行' AS r;
SELECT '   代价二: 【极易制造意料之外的死锁】——两个事务锁了互相重叠的区间' AS r;
SELECT '   → PostgreSQL 不用间隙锁，靠 MVCC 快照挡幻读（第 48 章实测两家 RR 行为不同的根源）' AS r;

-- ⑥ 死锁的经典剧本与规避
SELECT '⑥ 死锁最常见的来源: 两个事务【以相反顺序】访问同一批行' AS r;
SELECT '   事务甲: UPDATE account WHERE id=1;  然后 UPDATE account WHERE id=2;' AS r;
SELECT '   事务乙: UPDATE account WHERE id=2;  然后 UPDATE account WHERE id=1;' AS r;
SELECT '   → 等待图成环，数据库检测到后回滚代价较小的一方（C++ 版实现了这个算法）' AS r;
SELECT '   ✓ 规避: 所有事务都【按 id 从小到大】访问' AS r;
SELECT '     转账时先锁 min(from,to) 再锁 max(from,to)，而不是「先扣款方后收款方」' AS r;
SELECT '   ✓ 兜底: 捕获死锁错误码并【重试整个事务】+ 指数退避' AS r;
SELECT '     MySQL 1213 / PostgreSQL 40P01 / SQLSTATE 40001' AS r;

-- ⑦ 锁与索引：第 49 章在这里收口
CREATE INDEX idx_row ON seat(row_no);
SELECT '⑦ 锁是加在【索引条目】上的:' AS r;
SELECT '   UPDATE seat SET taken=1 WHERE row_no = 3  （有 idx_row）:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM seat WHERE row_no = 3;
SELECT '     → 走索引 → 只锁命中的 ' || (SELECT COUNT(*) FROM seat WHERE row_no=3) || ' 行' AS r;
SELECT '   UPDATE seat SET taken=1 WHERE taken = 0  （taken 无索引）:' AS r;
EXPLAIN QUERY PLAN SELECT id FROM seat WHERE taken = 0;
SELECT '     → 全表扫 → 锁住【扫过的全部 ' || (SELECT COUNT(*) FROM seat) || ' 行】' AS r;
SELECT '   ⚠️ 这就是「没有索引的 UPDATE 把整张表锁住」的真正机制' AS r;
SELECT '   → 第 49 章的索引不只决定查询快慢，还直接决定【锁的范围】和并发上限' AS r;

-- ⑧ 三种锁策略的选择
SELECT '⑧ 三条路，按冲突概率选:' AS r;
SELECT '   悲观锁(FOR UPDATE): 冲突多、事务短、必须成功 → 库存扣减、座位预订' AS r;
SELECT '   乐观锁(version 列) : 冲突少、或【跨请求】编辑 → 表单保存（第 48 章实测的分界）' AS r;
SELECT '   无锁(原子语句)     : 能写成一条 UPDATE 就别读 → SET balance = balance - 10' AS r;
SELECT '   → 优先级: 无锁 > 乐观 > 悲观。能不加锁就不加，这是并发设计的第一原则' AS r;
