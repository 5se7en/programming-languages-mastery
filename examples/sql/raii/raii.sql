-- RAII：数据库的对应物是事务——BEGIN 获取、COMMIT/ROLLBACK 释放，全有或全无。

CREATE TABLE account (id INTEGER PRIMARY KEY, name TEXT, balance INTEGER);
INSERT INTO account VALUES (1, '小明', 100), (2, '小红', 100);

-- ① 事务成功：COMMIT 提交全部改动
BEGIN;
UPDATE account SET balance = balance - 30 WHERE id = 1;
UPDATE account SET balance = balance + 30 WHERE id = 2;
COMMIT;
SELECT '① 转账成功后: 小明=' || (SELECT balance FROM account WHERE id = 1)
       || ', 小红=' || (SELECT balance FROM account WHERE id = 2) AS result;

-- ② 钥匙实验：中途出错 → ROLLBACK → 全部撤销（原子性 = 异常安全）
BEGIN;
UPDATE account SET balance = balance - 50 WHERE id = 1;
-- 假设这里业务校验失败（余额不足/风控拒绝）
ROLLBACK;
SELECT '② 回滚之后: 小明=' || (SELECT balance FROM account WHERE id = 1)
       || '（扣款被完整撤销——半途而废的状态不存在）' AS result;

-- ③ SAVEPOINT：嵌套作用域
BEGIN;
UPDATE account SET balance = balance + 1000 WHERE id = 1;     -- 外层改动
SAVEPOINT inner_scope;
UPDATE account SET balance = balance + 9999 WHERE id = 1;     -- 内层改动
ROLLBACK TO inner_scope;                                       -- 只撤内层
COMMIT;                                                        -- 外层照常提交
SELECT '③ 嵌套回滚后: 小明=' || (SELECT balance FROM account WHERE id = 1)
       || '（+1000 保留，+9999 撤销——内层作用域独立回滚）' AS result;

-- ④ 忘了结束事务的后果：连接持锁、其他会话阻塞、直到超时或断开
--    对应 C++ 忘了 delete、Java 忘了 close——而 RAII/with/using 的意义
--    就是让"忘记"在语法上不可能发生。
SELECT '④ 事务 = 作用域绑定的资源：BEGIN 获取，COMMIT/ROLLBACK 必须成对' AS lesson;
