-- 异步：数据库版的「不等待」——synchronous 级别决定要不要等磁盘确认。

CREATE TABLE log (id INTEGER PRIMARY KEY, msg TEXT);

-- ① synchronous = FULL：每次提交都等 fsync 完成（最安全，最慢）
--    相当于同步 I/O：调用方一直等到数据真的落盘
PRAGMA synchronous = FULL;
SELECT '① synchronous = ' || (SELECT * FROM pragma_synchronous())
       || '（2=FULL：每次提交都等磁盘确认，掉电不丢数据）' AS r;

-- ② synchronous = NORMAL：WAL 模式下不是每次提交都 fsync
--    相当于「异步刷盘」：先返回，稍后再真正写入
PRAGMA synchronous = NORMAL;
SELECT '② synchronous = ' || (SELECT * FROM pragma_synchronous())
       || '（1=NORMAL：提交立刻返回，攒一批再落盘）' AS r;

-- ③ synchronous = OFF：完全不等（最快，掉电可能损坏）
PRAGMA synchronous = OFF;
SELECT '③ synchronous = ' || (SELECT * FROM pragma_synchronous())
       || '（0=OFF：交给操作系统，进程崩溃仍安全，掉电则不保证）' AS r;

-- ④ 这正是异步编程的同一道权衡：
--    等待完成 = 确定性 + 慢；不等待 = 快 + 需要额外机制保证最终一致
SELECT '④ 同步等待 = 安全但慢；异步不等 = 快但要额外保障——两个世界同一道题' AS r;
SELECT '   （本机实测数据见文件末尾注释）' AS r;

-- ⑤ 数据库的异步还有一面：连接层
--    传统驱动：一次查询占住一条线程直到结果返回（同步阻塞）
--    异步驱动：查询发出后线程立刻去干别的（asyncpg / node-postgres / R2DBC）
--    → 一条线程可以同时挂着上千个未完成的查询，与本章的异步 I/O 完全同构
PRAGMA synchronous = FULL;                 -- 恢复安全设置
INSERT INTO log VALUES (1, '演示完毕');
SELECT '⑤ 恢复 synchronous = ' || (SELECT * FROM pragma_synchronous())
       || '，已写入 ' || (SELECT COUNT(*) FROM log) || ' 行' AS r;
--    本机实测 300 次单独提交: FULL 55ms / NORMAL 50ms / OFF 37ms
--    （macOS 上 SQLite 默认用 F_BARRIERFSYNC 而非完整 fsync，所以差距只有约 1.5 倍；
--      Linux 上 FULL 与 OFF 常常相差一个数量级——见章节正文）
