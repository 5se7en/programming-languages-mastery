-- 协程：数据库的「可暂停查询」——游标就是查询的协程。

CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT, score INTEGER);
WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n+1 FROM seq WHERE n < 1000)
INSERT INTO student SELECT n, 'student-' || n, n % 100 FROM seq;

-- ① 游标 = 可暂停恢复的查询
--    普通查询: 一次执行完，全部结果送回来（相当于普通函数）
--    游标:     执行到「产出一行」就暂停，FETCH 时才恢复（相当于生成器）
SELECT '① 普通查询: 一次性返回 ' || COUNT(*) || ' 行（全部载入内存）' AS r FROM student;
SELECT '   游标: 每次 FETCH 恢复执行，产出一行就暂停 —— 与 yield 完全同构' AS r;

-- ② SQLite 的 LIMIT/OFFSET 分页 = 手工版的游标
--    每次只取一页，与「协程调度器轮流推进」是同一个模式
SELECT '② 第 1 页: ' || group_concat(name, ', ') AS r
  FROM (SELECT name FROM student ORDER BY id LIMIT 3 OFFSET 0);
SELECT '   第 2 页: ' || group_concat(name, ', ') AS r
  FROM (SELECT name FROM student ORDER BY id LIMIT 3 OFFSET 3);
SELECT '   ↑ 每次「恢复」都从上次的位置继续 —— 但 OFFSET 会重扫前面的行（低效）' AS r;

-- ③ 键集分页（keyset pagination）：真正高效的「游标」
--    记住上次的位置（而非偏移量），下次从那里继续 —— 与协程保存状态同理
SELECT '③ 键集分页第 1 页（id > 0）: ' || group_concat(name, ', ') AS r
  FROM (SELECT id, name FROM student WHERE id > 0 ORDER BY id LIMIT 3);
SELECT '   键集分页第 2 页（id > 3）: ' || group_concat(name, ', ') AS r
  FROM (SELECT id, name FROM student WHERE id > 3 ORDER BY id LIMIT 3);
SELECT '   ↑ 保存「上次到哪」而非「跳过多少」—— 这才是游标的正确姿势' AS r;

-- ④ 为什么游标很重要：内存
--    一亿行的查询若一次性返回，客户端会 OOM
--    游标让「产生」与「消费」交替进行 —— 与协程的流式处理完全一致
SELECT '④ 流式处理: 一亿行用游标只占一行的内存' AS r;
SELECT '   对应 JS 的 async function* / C# 的 IAsyncEnumerable / Python 的生成器' AS r;

-- ⑤ 服务端游标 vs 客户端游标
--    服务端游标（DECLARE CURSOR）: 状态保存在数据库进程里（第 39 章：每连接一进程）
--    客户端游标:                   驱动把全部结果拉到本地再逐行给你（假游标）
--    → 大结果集必须用服务端游标，否则「流式」名存实亡
SELECT '⑤ PostgreSQL: DECLARE mycur CURSOR FOR SELECT ...; FETCH 100 FROM mycur;' AS r;
SELECT '   状态存在数据库端 —— 与协程的 frame 存在堆上同理' AS r;

-- ⑥ 递归 CTE 也是一种协程：产出一批 → 用它算下一批 → 再产出
SELECT '⑥ 递归 CTE 的工作队列（第 32 章实测百万层不爆栈）也是「产出-恢复」模式' AS r;
