-- 线程：数据竞争在数据库里的同款形态——丢失更新（lost update）。

CREATE TABLE account (id INTEGER PRIMARY KEY, balance INTEGER);
INSERT INTO account VALUES (1, 100);

-- ① 危险写法：读出来、算好、再写回（读-改-写三步，与 counter++ 同构）
--    两个会话同时这么干 -> 后写的覆盖先写的 -> 一次更新凭空消失
--    （完整的两进程实测见章节 shell 实测：期望 120，实际 110）
SELECT '① 危险写法: SELECT balance -> 应用层 +10 -> UPDATE SET balance=110' AS r;
SELECT '   两个会话并发执行 -> 都读到 100 -> 都写回 110 -> 丢了一次加钱' AS r;

-- ② 安全写法一：把读-改-写压成一条原子语句（数据库内部加行锁）
UPDATE account SET balance = balance + 10 WHERE id = 1;   -- 原子：与 atomic++ 同义
SELECT '② 原子写法 balance = balance + 10 之后: ' || balance AS r FROM account WHERE id = 1;

-- ③ 安全写法二：显式加锁（BEGIN IMMEDIATE 抢写锁，第 50 章展开）
BEGIN IMMEDIATE;
UPDATE account SET balance = balance + 10 WHERE id = 1;
COMMIT;
SELECT '③ 事务内加锁更新之后: ' || balance AS r FROM account WHERE id = 1;

-- ④ 安全写法三：乐观锁（版本号/CAS，与 Atomics.compareExchange 同构）
CREATE TABLE doc (id INTEGER PRIMARY KEY, content TEXT, version INTEGER);
INSERT INTO doc VALUES (1, '初稿', 1);
UPDATE doc SET content = '二稿', version = version + 1
  WHERE id = 1 AND version = 1;                            -- 版本对不上就不更新
SELECT '④ 乐观锁（CAS）更新: 影响行数 = ' || changes()
       || '，当前版本 = ' || (SELECT version FROM doc WHERE id = 1) AS r;
UPDATE doc SET content = '三稿', version = version + 1
  WHERE id = 1 AND version = 1;                            -- 版本已变 -> 更新失败
SELECT '   用旧版本号再更新: 影响行数 = ' || changes() || '（0 = 被别人改过，需重试）' AS r;

-- ⑤ 三种解法与并发编程一一对应：
--    原子语句 = atomic++ / Interlocked ；行锁 = mutex（第 41 章）；版本号 = CAS 自旋
SELECT '⑤ 数据库解决竞争的三招，与线程编程的三招完全同构' AS r;
