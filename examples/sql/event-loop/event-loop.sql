-- 事件循环：数据库的「事件驱动」——触发器的阶段与递归深度限制。

CREATE TABLE account (id INTEGER PRIMARY KEY, balance INTEGER);
CREATE TABLE audit (id INTEGER PRIMARY KEY AUTOINCREMENT, phase TEXT, detail TEXT);

-- ① 触发器的执行阶段：BEFORE → 实际操作 → AFTER
--    与事件循环的「阶段」是同一种设计：把回调挂到确定的时间点上
CREATE TRIGGER before_update BEFORE UPDATE ON account
BEGIN
    INSERT INTO audit(phase, detail)
      VALUES ('BEFORE', '旧值=' || OLD.balance || '，新值=' || NEW.balance);
END;

CREATE TRIGGER after_update AFTER UPDATE ON account
BEGIN
    INSERT INTO audit(phase, detail)
      VALUES ('AFTER', '已生效=' || NEW.balance);
END;

INSERT INTO account VALUES (1, 100);
UPDATE account SET balance = 200 WHERE id = 1;

SELECT '① 触发器阶段顺序:' AS r;
SELECT '   ' || id || '. ' || phase || ' — ' || detail AS r FROM audit ORDER BY id;

-- ② 递归触发器 = 微任务链
--    触发器里再改表 → 又触发新的触发器 → 与「微任务里再排微任务」完全同构
SELECT '② 递归触发器开关: recursive_triggers = '
       || (SELECT * FROM pragma_recursive_triggers())
       || '（0=关闭：触发器内的修改不再触发触发器）' AS r;

-- ③ 深度限制：数据库的「防饿死」机制
--    SQLite 用 SQLITE_MAX_TRIGGER_DEPTH（默认 1000）防止无限递归
--    → 这正是 JS 缺少的：微任务链无深度上限，所以能饿死宏任务（见章节实测）
SELECT '③ SQLite 有 SQLITE_MAX_TRIGGER_DEPTH（默认 1000）兜底' AS r;
SELECT '   而 JS 的微任务队列没有深度限制——递归 Promise 能让宏任务永远等待' AS r;

-- ④ 真正的「数据库事件循环」：服务端的主循环
--    PostgreSQL 的每个 backend 进程都有一个主循环：
--      读一条命令 → 解析 → 规划 → 执行 → 返回结果 → 回到读命令
--    与事件循环的「取一个任务 → 执行到底 → 取下一个」完全同构
SELECT '④ PostgreSQL backend 主循环: 读命令 → 执行到底 → 回到读命令' AS r;
SELECT '   同样的铁律：一条慢查询会占住这个 backend，其他命令只能排队' AS r;

-- ⑤ LISTEN/NOTIFY：数据库的事件驱动接口（PostgreSQL）
--    LISTEN channel;  → 订阅
--    NOTIFY channel;  → 发布，订阅方的连接会收到异步通知
--    → 这是把「事件循环」的模型延伸到了应用与数据库之间
SELECT '⑤ PostgreSQL 的 LISTEN/NOTIFY 把事件驱动延伸到了数据库外' AS r;
