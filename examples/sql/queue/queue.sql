-- 第 19 章 · 队列 — SQL 示例
-- 运行：sqlite3 :memory: < queue.sql
-- 用数据库表实现任务队列：中小系统里非常常见的做法

-- 1. 队列表：自增主键保证 FIFO 顺序
CREATE TABLE task_queue (
    id      INTEGER PRIMARY KEY AUTOINCREMENT,
    payload TEXT,
    status  TEXT DEFAULT 'pending'
);

-- 入队：插入一行
INSERT INTO task_queue (payload) VALUES ('发送邮件'), ('生成报表'), ('清理缓存');
SELECT '入队后', id, payload, status FROM task_queue ORDER BY id;

-- 2. ⚠️ 表是无序集合（第 16 章），FIFO 必须靠 ORDER BY 显式表达
SELECT '取最早任务(需 ORDER BY)', id, payload
FROM task_queue WHERE status = 'pending' ORDER BY id LIMIT 1;

-- 3. 出队：用原子 UPDATE 抢占，避免并发消费者取到同一条
--    （PostgreSQL/MySQL 8+ 可用 FOR UPDATE SKIP LOCKED；SQLite 不支持，故用此法）
UPDATE task_queue SET status = 'processing'
WHERE id = (SELECT id FROM task_queue WHERE status = 'pending' ORDER BY id LIMIT 1);
SELECT '抢占第1个任务后', id, payload, status FROM task_queue ORDER BY id;

-- 4. 完成任务
UPDATE task_queue SET status = 'done' WHERE status = 'processing';

-- 5. 继续消费下一个（验证 FIFO 顺序）
UPDATE task_queue SET status = 'processing'
WHERE id = (SELECT id FROM task_queue WHERE status = 'pending' ORDER BY id LIMIT 1);
SELECT '消费第2个后', id, payload, status FROM task_queue ORDER BY id;

-- 6. 队列积压监控（生产中的常见需求）
SELECT '队列状态统计', status, COUNT(*) AS 数量
FROM task_queue GROUP BY status ORDER BY status;

-- 7. 优先队列：加一个 priority 列，出队时按优先级而非 id 排序
CREATE TABLE priority_queue (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    payload  TEXT,
    priority INTEGER          -- 数字越小越紧急
);
INSERT INTO priority_queue (payload, priority) VALUES ('低优先级', 3), ('紧急', 1), ('普通', 2);
SELECT '优先队列出队顺序', payload, priority FROM priority_queue ORDER BY priority, id;
