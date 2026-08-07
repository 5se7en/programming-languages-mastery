-- 第 25 章 · 封装 — SQL 示例
-- 运行：sqlite3 :memory: < encapsulation.sql
-- 数据库的封装是唯一「运行时真正强制」的 —— 因为它有进程外的执行边界

-- ============================================================
-- 一、基表：包含敏感列
-- ============================================================

CREATE TABLE student (
    id            INTEGER PRIMARY KEY,
    name          TEXT NOT NULL,
    score         INTEGER NOT NULL CHECK (score BETWEEN 0 AND 100),
    id_number     TEXT,           -- 敏感：身份证号
    parent_income INTEGER         -- 敏感：家长收入
);

INSERT INTO student VALUES
 (1,'Alice',92,'110101200801011234', 250000),
 (2,'Bob',  45,'110101200802022345', 180000),
 (3,'Carol',88,'110101200803033456', 320000);

SELECT '① 基表包含敏感列（相当于类的私有字段）' AS 说明;
SELECT id, name, score, id_number, parent_income FROM student;

-- ============================================================
-- 二、视图 = 数据库版的「公开接口」
-- ============================================================

CREATE VIEW student_public AS
SELECT id, name, score,
       CASE WHEN score >= 60 THEN '及格' ELSE '不及格' END AS status
FROM student;

SELECT '② 视图只暴露该暴露的（敏感列根本不出现）' AS 说明;
SELECT id, name, score, status FROM student_public;

SELECT '③ 视图与编程语言封装的对应关系' AS 说明;
SELECT 'private 字段' AS 编程语言, '不出现在视图里的列' AS 数据库
UNION ALL SELECT 'public 方法',      '视图'
UNION ALL SELECT '计算属性',          '视图里的派生列（如上面的 status）'
UNION ALL SELECT '改实现不影响调用方', '改表结构，只要视图定义跟着调整，用户查询不用改';

-- ============================================================
-- 三、演示「改实现不影响调用方」
-- ============================================================

SELECT '④ 内部结构变了，视图接口不变' AS 说明;

-- 假设业务变化：分数改成百分制小数存储，加一张明细表
CREATE TABLE score_detail (student_id INTEGER, subject TEXT, points INTEGER);
INSERT INTO score_detail VALUES
 (1,'语文',95),(1,'数学',89),
 (2,'语文',40),(2,'数学',50),
 (3,'语文',90),(3,'数学',86);

-- 重建视图：底层改成从明细表算，但对外的列完全一样
DROP VIEW student_public;
CREATE VIEW student_public AS
SELECT s.id, s.name,
       CAST(AVG(d.points) AS INTEGER) AS score,
       CASE WHEN AVG(d.points) >= 60 THEN '及格' ELSE '不及格' END AS status
FROM student s JOIN score_detail d ON s.id = d.student_id
GROUP BY s.id, s.name;

SELECT '  同样的查询语句，结果结构完全不变：' AS 提示;
SELECT id, name, score, status FROM student_public ORDER BY id;
-- → 调用方一行都不用改，这正是封装的核心价值：保留改实现的自由

-- ============================================================
-- 四、权限：唯一「运行时强制」的封装
-- ============================================================

SELECT '⑤ 权限控制（语法示意，SQLite 不支持 GRANT）' AS 说明;
SELECT 'GRANT SELECT ON student_public TO reporting_user;' AS 授权视图
UNION ALL SELECT 'REVOKE ALL ON student FROM reporting_user;';

SELECT '⑥ ⚠️ 这是本章最有意思的对比' AS 说明;
SELECT 'Java private'   AS 机制, '编译期强制' AS 检查时机, '反射能绕过' AS 能否绕过
UNION ALL SELECT 'C++ private',   '编译期强制',   '内存操作能绕过'
UNION ALL SELECT 'Python __',     '不检查',       '改个名就能拿到'
UNION ALL SELECT 'JavaScript #',  '解析期',       '不能（语法禁止）'
UNION ALL SELECT '数据库 REVOKE', '每次查询运行时', '不能（服务端强制）';
-- → 数据库有一个绝对的执行边界（服务端进程），
--   而语言里的代码全都跑在同一个进程里，所以只能靠编译器和自觉

-- ============================================================
-- 五、约束 = 数据库层面的不变式（最强的一层保护）
-- ============================================================

CREATE TABLE account (
    id      INTEGER PRIMARY KEY,
    owner   TEXT NOT NULL,
    balance INTEGER NOT NULL CHECK (balance >= 0)    -- 不变式写进表定义
);

INSERT INTO account VALUES (1,'Alice',100);

SELECT '⑦ CHECK 约束：无论谁、用什么方式写入都绕不过去' AS 说明;
-- 尝试写入违反不变式的数据（用 OR IGNORE 让脚本能跑完；
-- 不加它的话 SQLite 会报 CHECK constraint failed 并以非零状态退出，
-- 那才是生产中的真实行为）
INSERT OR IGNORE INTO account VALUES (2,'Bob',-500);
UPDATE OR IGNORE account SET balance = -999 WHERE id = 1;

SELECT '  尝试插入 balance=-500 和更新为 -999 之后：' AS 提示;
SELECT id, owner, balance FROM account;
SELECT '  余额为负的记录数 =' AS 结果, COUNT(*) AS 数量 FROM account WHERE balance < 0;
-- → 应用层的封装可以被反射绕过，数据库约束不能

SELECT '⑧ 为什么关键约束要在数据库也写一份' AS 说明;
SELECT '应用层校验' AS 层次, '会被绕过：换个客户端、写个脚本直接连库' AS 风险
UNION ALL SELECT '数据库约束', '不会被绕过：任何写入路径都要过这一关'
UNION ALL SELECT '结论',      '关键不变式（余额非负、库存非负）两层都要写';

-- ============================================================
-- 六、存储过程：暴露操作，而不是暴露表
-- ============================================================

SELECT '⑨ 存储过程 = 暴露操作而非状态（语法示意）' AS 说明;
SELECT 'CREATE PROCEDURE deposit(account_id INT, amount INT)' AS 语法
UNION ALL SELECT '  IF amount <= 0 THEN SIGNAL ... ''金额必须为正''; END IF;'
UNION ALL SELECT '  UPDATE account SET balance = balance + amount WHERE id = account_id;'
UNION ALL SELECT 'END;'
UNION ALL SELECT '-- 只给存储过程权限，不给表权限 → 校验永远绕不过去'
UNION ALL SELECT '-- 这与第 6 节的结论完全呼应：暴露 deposit()，而不是暴露 balance';
-- （SQLite 不支持存储过程，语法因数据库而异）

-- 用触发器演示同样的思路：把校验绑在数据变更上
-- 这里用 RAISE(IGNORE) 静默放弃该行操作，好让脚本能跑完；
-- 生产中应该用 RAISE(ABORT, '错误信息') —— 它会中止语句并返回错误，
-- 注意 ABORT 不受 "UPDATE OR IGNORE" 影响，一定会让客户端收到报错。
CREATE TRIGGER no_big_withdraw BEFORE UPDATE ON account
WHEN NEW.balance < OLD.balance - 10000
BEGIN
    SELECT RAISE(IGNORE);
END;

SELECT '⑩ 触发器：把业务规则钉在数据层' AS 说明;
UPDATE account SET balance = balance - 50000 WHERE id = 1;   -- 会被触发器拦下
SELECT '  尝试一次扣减 50000 后，余额仍是' AS 结果, balance FROM account WHERE id = 1;

UPDATE account SET balance = balance - 30 WHERE id = 1;      -- 正常额度，放行
SELECT '  正常扣减 30 之后，余额变成' AS 结果, balance FROM account WHERE id = 1;
-- → 规则钉在数据层，任何写入路径都要过这一关

-- ============================================================
-- 七、小结
-- ============================================================
SELECT '⑪ 小结' AS 说明;
SELECT '视图 ≈ 公开接口' AS 主题, '只暴露该暴露的列，隐藏敏感字段' AS 要点
UNION ALL SELECT '改实现的自由',   '底层表结构可以变，视图接口保持不变'
UNION ALL SELECT '⚠️ 权限是真强制', '不像 private 能被反射绕过，REVOKE 之后是真读不到'
UNION ALL SELECT '为什么',         '数据库有进程外的执行边界，语言里的代码同进程'
UNION ALL SELECT 'CHECK 约束',     '最强的一层：任何写入路径都绕不过去'
UNION ALL SELECT '存储过程/触发器', '暴露操作而非状态 —— 与各语言的结论完全一致'
UNION ALL SELECT '工程建议',       '关键不变式在应用层和数据库层各写一份';
