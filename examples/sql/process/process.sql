-- 进程：数据库如何对待"多个连接"——每连接一进程 vs 每连接一线程。

CREATE TABLE account (id INTEGER PRIMARY KEY, balance INTEGER);
INSERT INTO account VALUES (1, 100);

-- ① 连接即隔离单元：每个连接有自己的事务上下文
--    （SQLite 是嵌入式的——"连接"就在你的进程里；
--      PostgreSQL 每连接一个 OS 进程；MySQL 每连接一个线程）
SELECT '① 当前连接看到的余额: ' || balance AS r FROM account WHERE id = 1;

-- ② 事务隔离 = 进程隔离在数据层的映射
BEGIN;
UPDATE account SET balance = balance - 30 WHERE id = 1;
SELECT '② 事务内（未提交）看到: ' || balance AS r FROM account WHERE id = 1;
-- 此刻另一个连接看到的仍是 100 —— 见章节 shell 实测（两个 sqlite3 进程对照）
COMMIT;
SELECT '   提交之后所有连接看到: ' || balance AS r FROM account WHERE id = 1;

-- ③ 多进程写同一个库：文件锁串行化
--    两个进程同时写 -> 后到者拿不到写锁 -> SQLITE_BUSY（章节 shell 实测）
PRAGMA busy_timeout = 3000;              -- 拿不到锁时最多等 3 秒
SELECT '③ busy_timeout 已设为 ' || (SELECT * FROM pragma_busy_timeout()) || ' ms'
       || '（进程竞争写锁时的等待上限）' AS r;

-- ④ WAL 模式：读写不互斥，多进程并发能力的关键
--    （默认 DELETE 模式下写会阻塞读；WAL 下读者不被写者阻塞）
PRAGMA journal_mode = WAL;
SELECT '④ journal_mode = ' || (SELECT * FROM pragma_journal_mode())
       || '（WAL：多进程读写并发的标准配置）' AS r;

-- ⑤ 三种服务端模型的对照见章节正文：
--    PostgreSQL 每连接一进程（隔离强、创建贵、靠连接池摊薄）
--    MySQL      每连接一线程（轻量、共享内存、需要小心线程安全）
--    SQLite     无服务端进程（连接就在调用方进程里，靠文件锁协调）
SELECT '⑤ 隔离与共享的取舍，数据库和操作系统面对的是同一道题' AS r;
