-- 第 23 章 · 类 — SQL 示例
-- 运行：sqlite3 :memory: < class.sql
-- SQL 没有类（它是关系模型不是对象模型），但两者有清晰的对应关系

-- ============================================================
-- 一、表 = 类的「数据部分」
-- ============================================================

SELECT '① 对象模型 vs 关系模型的对应关系' AS 说明;
SELECT '类'        AS 面向对象, '表'          AS 关系数据库
UNION ALL SELECT '字段 / 属性', '列'
UNION ALL SELECT '对象 / 实例', '行'
UNION ALL SELECT '对象标识',   '主键'
UNION ALL SELECT '类型约束',   '列类型 + CHECK'
UNION ALL SELECT '构造函数校验', 'CHECK 约束 / NOT NULL';

-- 建表 = 定义"这类东西有哪些数据、要满足什么约束"
CREATE TABLE student (
    id     INTEGER PRIMARY KEY,                          -- 对象标识
    name   TEXT NOT NULL,                                -- 字段
    score  INTEGER NOT NULL CHECK (score BETWEEN 0 AND 100),  -- 对应构造函数里的校验
    school TEXT DEFAULT '第一中学'                        -- 对应有默认值的字段
);

INSERT INTO student (id, name, score) VALUES
 (1,'Alice',92), (2,'Bob',45), (3,'Carol',88);

SELECT '② 三行 = 三个「对象」' AS 说明;
SELECT id, name, score, school FROM student;

-- ============================================================
-- 二、CHECK 约束扮演的就是「构造函数校验」的角色
-- ============================================================

SELECT '③ CHECK 约束：保证数据从写入那一刻起就合法' AS 说明;
-- 尝试插入非法分数（150 超出 0..100）
-- 这里用 OR IGNORE 让脚本能继续跑完；不加它的话 SQLite 会直接报
-- "CHECK constraint failed" 并以非零状态退出 —— 那才是生产中的真实行为
INSERT OR IGNORE INTO student (id, name, score) VALUES (99,'Invalid',150);

SELECT '  尝试插入 score=150 后，表中行数仍是' AS 结果, COUNT(*) AS 行数 FROM student;
SELECT '  id=99 的记录存在吗？' AS 结果,
       CASE WHEN EXISTS(SELECT 1 FROM student WHERE id=99)
            THEN '存在（约束失效了！）' ELSE '不存在 ← 被约束挡住了' END AS 答案;
-- → 与构造函数里 throw 的效果一致：非法对象根本无法被创建出来

-- ============================================================
-- 三、行为放在哪里：视图 = 计算属性，触发器 = 自动行为
-- ============================================================

SELECT '④ 视图 = 只读的「计算属性」' AS 说明;
CREATE VIEW student_status AS
SELECT id, name, score,
       CASE WHEN score >= 60 THEN '及格' ELSE '不及格' END AS is_passing
FROM student;

SELECT name, score, is_passing FROM student_status;
-- → 相当于类里的 isPassing() 方法，只是它是"查出来的"而不是"算出来的"

SELECT '⑤ 触发器 = 数据变更时自动执行的行为' AS 说明;
CREATE TABLE audit_log (action TEXT, student_name TEXT);

CREATE TRIGGER log_new_student AFTER INSERT ON student
BEGIN
    INSERT INTO audit_log VALUES ('新增学生', NEW.name);
END;

INSERT INTO student (id, name, score) VALUES (4,'Dave',70);
SELECT action AS 动作, student_name AS 学生 FROM audit_log;
-- → 相当于构造函数里的副作用（比如实例计数）

-- ============================================================
-- 四、静态成员：所有"实例"共享的东西
-- ============================================================

SELECT '⑥ 静态成员的对应物' AS 说明;
-- 类的静态字段 → 单独一张配置表（或 DEFAULT 值）
CREATE TABLE school_config (key TEXT PRIMARY KEY, value TEXT);
INSERT INTO school_config VALUES ('school_name','第一中学'), ('pass_line','60');

SELECT key AS 配置项, value AS 值 FROM school_config;

-- 类的静态方法 count() → 聚合查询
SELECT '实例计数（相当于静态方法 count()）' AS 说明, COUNT(*) AS 学生总数 FROM student;

-- ============================================================
-- 五、⚠️ 阻抗失配：对象模型与关系模型对不上的地方
-- ============================================================

SELECT '⑦ 阻抗失配：ORM 要解决的问题' AS 说明;
SELECT '标识' AS 差异点, '内存地址 / 引用' AS 对象模型, '主键' AS 关系模型
UNION ALL SELECT '关联',   '直接持有对象引用', '外键 + JOIN'
UNION ALL SELECT '继承',   '天然支持',        '⚠️ 没有对应概念'
UNION ALL SELECT '集合',   '对象里放一个 List', '需要单独一张表'
UNION ALL SELECT '行为',   '方法',            '视图 / 触发器 / 存储过程';

-- 「对象里放一个 List」在关系模型里必须拆成单独的表
CREATE TABLE student_tag (student_id INTEGER, tag TEXT);
INSERT INTO student_tag VALUES (1,'优秀'), (1,'班长'), (3,'文艺委员');

SELECT '⑧ 对象的 List 字段 → 单独一张关联表' AS 说明;
SELECT s.name AS 学生, GROUP_CONCAT(t.tag, ', ') AS 标签
FROM student s LEFT JOIN student_tag t ON s.id = t.student_id
GROUP BY s.id, s.name ORDER BY s.id;
-- → 在对象里这只是 student.tags = ['优秀','班长']，在关系库里却要 JOIN

-- ============================================================
-- 六、小结
-- ============================================================
SELECT '⑨ 小结' AS 说明;
SELECT '表 ≈ 类的数据部分' AS 主题, '列=字段，行=实例，主键=对象标识' AS 要点
UNION ALL SELECT 'CHECK ≈ 构造函数校验', '保证数据从写入那一刻起就合法'
UNION ALL SELECT '视图 ≈ 计算属性',      '把派生数据固定下来'
UNION ALL SELECT '触发器 ≈ 自动行为',    '数据变更时执行'
UNION ALL SELECT '⚠️ 阻抗失配',          '继承和集合在关系模型里没有直接对应物'
UNION ALL SELECT 'ORM 的作用',           '弥合两个模型的差异，但会掩盖真实 SQL 开销'
UNION ALL SELECT '工程提醒',             '用 ORM 务必知道它生成了什么 SQL（避免 N+1，第 11 章）';
