-- 第 27 章 · 多态 — SQL 示例
-- 运行：sqlite3 :memory: < polymorphism.sql
-- 数据库没有对象和虚函数，但「同一查询、按类型给出不同结果」的需求同样存在

-- ============================================================
-- 一、CASE：最直接的类型分派（相当于代码里的 if-else）
-- ============================================================

CREATE TABLE shape (
    id   INTEGER PRIMARY KEY,
    type TEXT NOT NULL CHECK (type IN ('circle','rect','triangle')),
    a    REAL NOT NULL,     -- 圆：半径；矩形：宽；三角形：底
    b    REAL               -- 圆：不用；矩形：高；三角形：高
);

INSERT INTO shape VALUES
 (1,'circle',   2, NULL),
 (2,'rect',     3, 4),
 (3,'triangle', 6, 5),
 (4,'circle',   1, NULL);

SELECT '① CASE 分派：相当于代码里的 if-else' AS 说明;
SELECT id, type,
       CASE type
           WHEN 'circle'   THEN 3.14159 * a * a
           WHEN 'rect'     THEN a * b
           WHEN 'triangle' THEN a * b / 2
       END AS area
FROM shape ORDER BY id;

SELECT '  ⚠️ 问题：新增一种图形就要改这个查询' AS 缺点,
       '与本章开头的反面教材完全对应' AS 说明;

-- ============================================================
-- 二、视图：把「多态」封装起来（视图扮演接口的角色）
-- ============================================================

CREATE VIEW shape_with_area AS
SELECT id, type,
       CASE type
           WHEN 'circle'   THEN 3.14159 * a * a
           WHEN 'rect'     THEN a * b
           WHEN 'triangle' THEN a * b / 2
       END AS area
FROM shape;

SELECT '② 视图 = 数据库版的「接口」' AS 说明;
SELECT ROUND(SUM(area), 2) AS 总面积, COUNT(*) AS 图形数 FROM shape_with_area;
SELECT type AS 类型, COUNT(*) AS 数量, ROUND(SUM(area),2) AS 面积合计
FROM shape_with_area GROUP BY type ORDER BY type;

SELECT '  ✓ 使用方完全不关心面积是怎么算出来的' AS 优点
UNION ALL SELECT '  ✓ 新增图形类型时只改视图定义，所有使用方的查询一行不动'
UNION ALL SELECT '  → 这正是多态带来的「对修改关闭」（第 25 章的封装同理）';

-- 演示：新增一种图形，只改视图
INSERT INTO shape VALUES (5,'circle', 3, NULL);   -- 先加一条数据

DROP VIEW shape_with_area;
CREATE VIEW shape_with_area AS
SELECT id, type,
       CASE type
           WHEN 'circle'   THEN 3.14159 * a * a
           WHEN 'rect'     THEN a * b
           WHEN 'triangle' THEN a * b / 2
           ELSE 0                                  -- 只改了这里
       END AS area
FROM shape;

SELECT '③ 改了视图定义之后，同样的查询语句：' AS 说明;
SELECT ROUND(SUM(area), 2) AS 总面积 FROM shape_with_area;
-- → 查询语句一个字没改

-- ============================================================
-- 三、类表继承下的多态查询（承接第 26 章）
-- ============================================================

CREATE TABLE employee (id INTEGER PRIMARY KEY, name TEXT NOT NULL, salary INTEGER);
CREATE TABLE manager  (id INTEGER PRIMARY KEY REFERENCES employee(id), team_size INTEGER);
CREATE TABLE engineer (id INTEGER PRIMARY KEY REFERENCES employee(id), language TEXT);

INSERT INTO employee VALUES (1,'Alice',50000),(2,'Bob',40000),(3,'Carol',45000);
INSERT INTO manager  VALUES (1,8);
INSERT INTO engineer VALUES (2,'Java'),(3,'C++');

SELECT '④ 类表继承的多态查询：LEFT JOIN 所有子类型 + CASE 判断实际类型' AS 说明;
SELECT e.id, e.name, e.salary,
       CASE WHEN m.id IS NOT NULL THEN 'manager'
            WHEN g.id IS NOT NULL THEN 'engineer'
            ELSE 'unknown' END                AS type,
       COALESCE(m.team_size, 0)               AS team_size,
       COALESCE(g.language, '-')              AS language
FROM employee e
LEFT JOIN manager  m ON e.id = m.id
LEFT JOIN engineer g ON e.id = g.id
ORDER BY e.id;

SELECT '  ⚠️ 代价：每增加一个子类型，就要多一个 JOIN' AS 缺点;

-- ============================================================
-- 四、数据库自身的多态：函数重载（特设多态，第 12 章）
-- ============================================================

SELECT '⑤ 函数重载：同一个函数名，按参数类型分派' AS 说明;
SELECT 'ABS(-5)'      AS 调用, ABS(-5)      AS 结果, '整数'   AS 参数类型
UNION ALL SELECT 'ABS(-5.5)',   ABS(-5.5),   '浮点数'
UNION ALL SELECT 'LENGTH(''hello'')', LENGTH('hello'), '字符串'
UNION ALL SELECT 'LENGTH(12345)',     LENGTH(12345),   '数字（隐式转字符串）';
-- → 这是「特设多态」：同名函数按参数类型分派

SELECT '⑥ 同一个 || 运算符对不同类型的行为' AS 说明;
SELECT '''a'' || ''b''' AS 表达式, 'a' || 'b' AS 结果
UNION ALL SELECT '1 || 2',       1 || 2
UNION ALL SELECT '''x'' || 1',   'x' || 1;
-- → 与 Python 里 + 对 int/str/list 的不同含义是同一回事

-- ============================================================
-- 五、⚠️ 什么时候该把逻辑搬回应用层
-- ============================================================

SELECT '⑦ ⚠️ 工程提醒' AS 说明;
SELECT '症状' AS 类别, '查询里出现很长的 CASE type WHEN ...' AS 内容
UNION ALL SELECT '含义',   '你在数据库里手工模拟多态，代价远高于代码里'
UNION ALL SELECT '选择一', '重新审视建模（是不是不该用继承？见第 26 章）'
UNION ALL SELECT '选择二', '把这部分逻辑放回应用层，让真正的多态机制来处理'
UNION ALL SELECT '选择三', '用视图封装，至少让「新增类型」只改一处';

-- ============================================================
-- 六、各语言 vs SQL 的多态实现对比
-- ============================================================

SELECT '⑧ 多态实现方式对比' AS 说明;
SELECT 'C++/Java/C#' AS 语言, 'vtable'          AS 机制, '编译期建表，一次间接跳转' AS 说明
UNION ALL SELECT 'JavaScript', '原型链+内联缓存', '运行时学出来的表'
UNION ALL SELECT 'Python',     'MRO 查找+缓存',   '比 vtable 慢一个量级'
UNION ALL SELECT 'SQL',        'CASE / 视图',     '手工分派，新增类型要改定义';

SELECT '  共同模式：所有实现都在「查表」和「缓存」之间做文章' AS 洞察
UNION ALL SELECT '  vtable 是编译期建好的表，内联缓存是运行时学出来的表'
UNION ALL SELECT '  而 SQL 的 CASE 是最原始的形式：每次都从头判断';

-- ============================================================
-- 七、小结
-- ============================================================
SELECT '⑨ 小结' AS 说明;
SELECT 'CASE 分派'   AS 主题, '最直接，但新增类型要改查询' AS 要点
UNION ALL SELECT '视图封装',   '扮演接口角色，新增类型只改视图定义'
UNION ALL SELECT '类表继承',   'LEFT JOIN 所有子类型 + CASE 判断，每加一个子类多一个 JOIN'
UNION ALL SELECT '函数重载',   'ABS/LENGTH 按参数类型分派 —— 特设多态'
UNION ALL SELECT '⚠️ 提醒',    '长串 CASE type WHEN 通常说明该重新审视建模'
UNION ALL SELECT '共同结论',   '数据库里模拟多态的代价远高于代码里';
