-- 第 28 章 · 接口 — SQL 示例
-- 运行：sqlite3 :memory: < interface.sql
-- 数据库里「接口」的对应物是视图：把稳定的契约与易变的表结构分开

-- ============================================================
-- 一、视图即接口：契约稳定，实现可变
-- ============================================================

-- 底层表结构（实现细节，随时可能变）
CREATE TABLE student_v1 (
    id     INTEGER PRIMARY KEY,
    name   TEXT NOT NULL,
    score  INTEGER NOT NULL
);
INSERT INTO student_v1 VALUES (1,'Alice',92),(2,'Bob',45),(3,'Carol',88);

-- 视图 = 对外的稳定契约
CREATE VIEW student AS SELECT id, name, score FROM student_v1;

SELECT '① 应用代码只查视图（契约）' AS 说明;
SELECT name AS 姓名, score AS 分数 FROM student WHERE score >= 60 ORDER BY score DESC;

-- ============================================================
-- 二、演示「换实现不改调用方」
-- ============================================================

SELECT '② 现在表结构大改：改列名 + 加软删除 + 拆表' AS 说明;

CREATE TABLE student_v2 (
    id        INTEGER PRIMARY KEY,
    full_name TEXT NOT NULL,          -- 列名变了
    deleted   INTEGER DEFAULT 0       -- 加了软删除
);
CREATE TABLE score_v2 (
    student_id INTEGER PRIMARY KEY,
    score_raw  INTEGER NOT NULL       -- 分数拆到了另一张表
);
INSERT INTO student_v2 VALUES (1,'Alice',0),(2,'Bob',0),(3,'Carol',0),(4,'Dave',1);
INSERT INTO score_v2   VALUES (1,92),(2,45),(3,88),(4,70);

-- 只改视图定义，让它适配新的表结构
DROP VIEW student;
CREATE VIEW student AS
SELECT s.id, s.full_name AS name, c.score_raw AS score
FROM student_v2 s JOIN score_v2 c ON s.id = c.student_id
WHERE s.deleted = 0;

SELECT '  底层从「一张表」变成「两张表 JOIN + 软删除过滤」' AS 变化;
SELECT '  但完全相同的查询语句仍然可用：' AS 提示;
SELECT name AS 姓名, score AS 分数 FROM student WHERE score >= 60 ORDER BY score DESC;
-- → 查询语句一个字都没改！Dave 因为 deleted=1 被过滤掉了
-- → 这与代码里「依赖接口而非实现」是同一个道理

-- ============================================================
-- 三、多个视图 = 接口隔离原则
-- ============================================================

SELECT '③ 接口隔离：不同使用方需要不同的视图' AS 说明;

CREATE VIEW student_public AS          -- 给前端：只有基本信息
SELECT id, name FROM student;

CREATE VIEW student_grade AS           -- 给成绩系统：带及格判断
SELECT id, name, score,
       CASE WHEN score >= 60 THEN '及格' ELSE '不及格' END AS status
FROM student;

CREATE VIEW student_stats AS           -- 给管理层：只要聚合数据
SELECT COUNT(*) AS 学生数, ROUND(AVG(score),1) AS 平均分, MAX(score) AS 最高分
FROM student;

SELECT '  student_public（前端用）:' AS 视图;
SELECT id, name FROM student_public;
SELECT '  student_grade（成绩系统用）:' AS 视图;
SELECT name, score, status FROM student_grade;
SELECT '  student_stats（管理层用）:' AS 视图;
SELECT 学生数, 平均分, 最高分 FROM student_stats;

SELECT '  → 每个使用方只看到自己需要的「契约」' AS 要点
UNION ALL SELECT '  → 这就是接口隔离原则：与其一个大而全的接口，不如多个小接口'
UNION ALL SELECT '  → 前端不需要知道分数，管理层不需要知道单个学生';

-- ============================================================
-- 四、⚠️ 视图不是免费的
-- ============================================================

SELECT '④ ⚠️ 契约层要薄' AS 说明;

-- 反面教材：多层嵌套视图
CREATE VIEW layer1 AS SELECT * FROM student;
CREATE VIEW layer2 AS SELECT * FROM layer1 WHERE score > 0;
CREATE VIEW layer3 AS SELECT * FROM layer2 WHERE name IS NOT NULL;

SELECT '  三层嵌套视图的执行计划：' AS 提示;
EXPLAIN QUERY PLAN SELECT * FROM layer3 WHERE score >= 60;

SELECT '  问题' AS 类别, '多层嵌套视图会让查询计划难以优化' AS 说明
UNION ALL SELECT '问题', '也会掩盖真实的表关联复杂度'
UNION ALL SELECT '建议', '契约层要薄 —— 与代码里「接口不要过度设计」是同一条经验'
UNION ALL SELECT '建议', '一层视图通常够用，超过两层就该反思';

-- ============================================================
-- 五、存储过程 = 更严格的接口
-- ============================================================

SELECT '⑤ 存储过程：只暴露操作，完全隐藏表结构（语法示意）' AS 说明;
SELECT 'CREATE PROCEDURE enroll_student(IN p_name TEXT, IN p_score INT)' AS 语法
UNION ALL SELECT 'BEGIN'
UNION ALL SELECT '  IF p_score < 0 OR p_score > 100 THEN'
UNION ALL SELECT '    SIGNAL SQLSTATE ''45000'' SET MESSAGE_TEXT = ''分数必须在 0..100'';'
UNION ALL SELECT '  END IF;'
UNION ALL SELECT '  INSERT INTO student_v2 ... ;'
UNION ALL SELECT 'END;'
UNION ALL SELECT ''
UNION ALL SELECT '-- 配合权限，就是真正强制的接口（第 25 章）'
UNION ALL SELECT 'GRANT EXECUTE ON PROCEDURE enroll_student TO app_user;'
UNION ALL SELECT 'REVOKE ALL ON student_v2 FROM app_user;   -- 表本身不给权限';
-- （SQLite 不支持存储过程，语法因数据库而异）

-- ============================================================
-- 六、依赖倒置在数据访问层
-- ============================================================

SELECT '⑥ 依赖倒置：两层契约' AS 说明;
SELECT '层次' AS 列1, '契约' AS 列2, '实现' AS 列3
UNION ALL SELECT '应用代码', 'Repository 接口', '具体的 SQL 实现'
UNION ALL SELECT '数据库',   '视图 / 存储过程', '实际的表结构';

SELECT '⑦ 对比' AS 说明;
SELECT '❌ 应用直接写 SQL 操作表' AS 做法, '表结构一变，应用到处改' AS 后果
UNION ALL SELECT '✅ 应用调用视图/存储过程', '数据库内部随便重构，应用不动';

-- ============================================================
-- 七、视图与编程语言接口的对应关系
-- ============================================================

SELECT '⑧ 概念对应关系' AS 说明;
SELECT '接口（编程语言）' AS 编程语言, '视图（数据库）' AS 数据库, '共同点' AS 本质
UNION ALL SELECT '方法签名',      '视图的列',       '对外承诺的契约'
UNION ALL SELECT '实现类',        '底层表结构',      '可以随时替换'
UNION ALL SELECT '换实现不改调用方', '改表不改查询',   '依赖倒置'
UNION ALL SELECT '接口隔离原则',   '多个专用视图',    '每个使用方只看需要的'
UNION ALL SELECT '默认方法',      '视图里的计算列',  '提供派生能力';

-- ============================================================
-- 八、小结
-- ============================================================
SELECT '⑨ 小结' AS 说明;
SELECT '视图即接口' AS 主题, '把稳定的契约与易变的表结构分开' AS 要点
UNION ALL SELECT '换实现不改调用方', '实测：表从 1 张变 2 张 + 加软删除，查询语句一字未改'
UNION ALL SELECT '接口隔离',       '前端/成绩系统/管理层各用各的视图'
UNION ALL SELECT '存储过程',       '更严格的接口：只暴露操作，配合权限成为强制契约'
UNION ALL SELECT '⚠️ 契约层要薄',   '多层嵌套视图会让查询计划难优化'
UNION ALL SELECT '共同结论',       '与代码里「面向接口编程」是完全同一个思路';
