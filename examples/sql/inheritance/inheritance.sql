-- 第 26 章 · 继承 — SQL 示例
-- 运行：sqlite3 :memory: < inheritance.sql
-- 关系模型没有继承（第 23 章阻抗失配里最难处理的一项）
-- 实践中有三种建模策略，各有明确的取舍

-- 场景：Employee 父类，Manager（多 team_size）和 Engineer（多 language）两个子类

-- ============================================================
-- 策略①：单表继承 —— 所有子类挤一张表，用 type 列区分
-- ============================================================

CREATE TABLE sti_employee (
    id        INTEGER PRIMARY KEY,
    type      TEXT NOT NULL CHECK (type IN ('manager','engineer')),   -- 区分子类
    name      TEXT NOT NULL,
    salary    INTEGER NOT NULL,
    team_size INTEGER,             -- 只有 manager 用，engineer 这里是 NULL
    language  TEXT                 -- 只有 engineer 用，manager 这里是 NULL
);

INSERT INTO sti_employee VALUES
 (1,'manager', 'Alice', 50000, 8,    NULL),
 (2,'engineer','Bob',   40000, NULL, 'Java'),
 (3,'engineer','Carol', 45000, NULL, 'C++'),
 (4,'manager', 'Dave',  55000, 12,   NULL);

SELECT '① 单表继承：一张表装下所有子类' AS 说明;
SELECT id, type, name, salary, team_size, language FROM sti_employee;

SELECT '  多态查询（查所有员工）—— 最简单，无需 JOIN：' AS 提示;
SELECT name, type, salary FROM sti_employee ORDER BY salary DESC;

SELECT '  查单个子类 —— 加一个 WHERE：' AS 提示;
SELECT name, team_size FROM sti_employee WHERE type = 'manager';

SELECT '  ⚠️ 代价：大量 NULL 列，且无法给子类字段加 NOT NULL 约束' AS 缺点;
SELECT COUNT(*) AS 总行数,
       SUM(CASE WHEN team_size IS NULL THEN 1 ELSE 0 END) AS team_size为NULL,
       SUM(CASE WHEN language  IS NULL THEN 1 ELSE 0 END) AS language为NULL
FROM sti_employee;

-- ============================================================
-- 策略②：类表继承 —— 父表 + 子表，用外键关联
-- ============================================================

CREATE TABLE cti_employee (
    id     INTEGER PRIMARY KEY,
    name   TEXT NOT NULL,
    salary INTEGER NOT NULL
);
CREATE TABLE cti_manager (
    id        INTEGER PRIMARY KEY REFERENCES cti_employee(id),
    team_size INTEGER NOT NULL          -- ✓ 可以正常加 NOT NULL 约束
);
CREATE TABLE cti_engineer (
    id       INTEGER PRIMARY KEY REFERENCES cti_employee(id),
    language TEXT NOT NULL              -- ✓ 约束完整
);

INSERT INTO cti_employee VALUES (1,'Alice',50000),(2,'Bob',40000),(3,'Carol',45000),(4,'Dave',55000);
INSERT INTO cti_manager  VALUES (1,8),(4,12);
INSERT INTO cti_engineer VALUES (2,'Java'),(3,'C++');

SELECT '② 类表继承：父表存公共字段，子表存特有字段' AS 说明;

SELECT '  查单个子类 —— 需要 JOIN：' AS 提示;
SELECT e.name, e.salary, m.team_size
FROM cti_employee e JOIN cti_manager m ON e.id = m.id;

SELECT '  ⚠️ 多态查询 —— 要 JOIN 所有子表：' AS 提示;
SELECT e.name, e.salary,
       CASE WHEN m.id IS NOT NULL THEN 'manager'
            WHEN g.id IS NOT NULL THEN 'engineer' END AS type,
       m.team_size, g.language
FROM cti_employee e
LEFT JOIN cti_manager  m ON e.id = m.id
LEFT JOIN cti_engineer g ON e.id = g.id
ORDER BY e.id;

SELECT '  ✓ 优点：结构最规范，没有冗余 NULL，约束完整' AS 优点;

-- ============================================================
-- 策略③：具体表继承 —— 每个子类一张完整的表
-- ============================================================

CREATE TABLE tpc_manager (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL,
    salary INTEGER NOT NULL, team_size INTEGER NOT NULL
);
CREATE TABLE tpc_engineer (
    id INTEGER PRIMARY KEY, name TEXT NOT NULL,
    salary INTEGER NOT NULL, language TEXT NOT NULL
);

INSERT INTO tpc_manager  VALUES (1,'Alice',50000,8),(4,'Dave',55000,12);
INSERT INTO tpc_engineer VALUES (2,'Bob',40000,'Java'),(3,'Carol',45000,'C++');

SELECT '③ 具体表继承：每个子类一张独立的完整表' AS 说明;

SELECT '  查单个子类 —— 最快，无 JOIN 无 NULL：' AS 提示;
SELECT name, salary, team_size FROM tpc_manager;

SELECT '  ⚠️ 多态查询 —— 要 UNION ALL：' AS 提示;
SELECT name, salary, 'manager' AS type FROM tpc_manager
UNION ALL
SELECT name, salary, 'engineer'        FROM tpc_engineer
ORDER BY salary DESC;

SELECT '  ⚠️ 代价：公共字段（name/salary）重复定义，加字段要改所有表' AS 缺点;

-- ============================================================
-- 三种策略对比
-- ============================================================

SELECT '④ 三种策略对比' AS 说明;
SELECT '单表继承'   AS 策略,
       '简单，无 JOIN，多态查询最快' AS 优点,
       '大量 NULL，子类字段无法加约束' AS 缺点,
       '子类差异小、需频繁多态查询'   AS 适合场景
UNION ALL SELECT '类表继承',
       '最规范，约束完整，无冗余',
       '每次查询都要 JOIN',
       '子类差异大、约束要求严格'
UNION ALL SELECT '具体表继承',
       '单子类查询最快，无 JOIN 无 NULL',
       '公共字段重复，加字段要改所有表',
       '子类几乎不一起查询';

SELECT '⑤ ORM 里的对应实现' AS 说明;
SELECT '单表继承'   AS 策略, 'Hibernate: SINGLE_TABLE'    AS Java, 'Django: 不直接支持' AS Python
UNION ALL SELECT '类表继承',  'Hibernate: JOINED',          'Django: 多表继承'
UNION ALL SELECT '具体表继承','Hibernate: TABLE_PER_CLASS', 'Django: 抽象基类';

-- ============================================================
-- ⚠️ 最重要的一条：先问「真的需要继承吗」
-- ============================================================

SELECT '⑥ ⚠️ 选型前先问一句：真的需要继承吗？' AS 说明;
SELECT '很多时候更好的方案' AS 思路, '说明' AS 详情
UNION ALL SELECT '一张表 + type 列',   '子类差异只有一两个字段时，硬套继承层次纯属自找麻烦'
UNION ALL SELECT '拆成两张无关的表',   '如果 Manager 和 Engineer 业务上几乎不一起处理'
UNION ALL SELECT '公共字段 + 扩展表',  '把可选字段放进 key-value 扩展表（EAV），灵活但查询复杂'
UNION ALL SELECT '组合而非继承',       '「员工 has-a 管理职责」比「管理者 is-a 员工」更贴近现实';

-- 演示「组合」思路：把管理职责建成一张关联表，而不是子类
CREATE TABLE emp (id INTEGER PRIMARY KEY, name TEXT, salary INTEGER);
CREATE TABLE management_role (emp_id INTEGER REFERENCES emp(id), team_size INTEGER);
CREATE TABLE engineering_role (emp_id INTEGER REFERENCES emp(id), language TEXT);

INSERT INTO emp VALUES (1,'Alice',50000),(2,'Bob',40000),(5,'Eve',60000);
INSERT INTO management_role  VALUES (1,8),(5,3);
INSERT INTO engineering_role VALUES (2,'Java'),(5,'Rust');   -- ⚠️ Eve 两个角色都有！

SELECT '⑦ 组合思路：一个人可以同时有多个角色（继承做不到）' AS 说明;
SELECT e.name,
       CASE WHEN m.emp_id IS NOT NULL THEN '管理(' || m.team_size || '人)' ELSE '' END AS 管理角色,
       CASE WHEN g.emp_id IS NOT NULL THEN '技术(' || g.language || ')'     ELSE '' END AS 技术角色
FROM emp e
LEFT JOIN management_role  m ON e.id = m.emp_id
LEFT JOIN engineering_role g ON e.id = g.emp_id
ORDER BY e.id;
-- → Eve 既是管理者又是工程师。用继承建模的话，她只能属于一个子类！
-- → 这与代码里「组合优于继承」是完全同一个判断

-- ============================================================
-- 小结
-- ============================================================
SELECT '⑧ 小结' AS 说明;
SELECT '关系模型没有继承' AS 主题, '这是第 23 章阻抗失配里最难处理的一项' AS 要点
UNION ALL SELECT '三种建模策略', '单表 / 类表 / 具体表，按「子类差异」和「查询模式」选'
UNION ALL SELECT '单表继承',    '简单但 NULL 多，约束弱'
UNION ALL SELECT '类表继承',    '规范但要 JOIN'
UNION ALL SELECT '具体表继承',  '快但字段重复'
UNION ALL SELECT '⚠️ 最重要的',  '选型前先问：真的需要继承吗？'
UNION ALL SELECT '组合的优势',  '一个实体可以同时拥有多个角色，继承做不到（见 ⑦）'
UNION ALL SELECT '共同结论',    '与代码里「组合优于继承」是同一个判断';
