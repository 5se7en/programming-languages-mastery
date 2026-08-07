-- 第 24 章 · 对象 — SQL 示例
-- 运行：sqlite3 :memory: < object.sql
-- 数据库里「对象」的对应物是「行」，行的存储布局同样有实实在在的性能后果

-- ============================================================
-- 一、行存在页里：一页能放多少行，决定要读多少次 I/O
-- ============================================================

SELECT '① 行、页与 I/O 的关系' AS 说明;
SELECT '一个数据页' AS 概念, '通常 4KB - 16KB' AS 说明
UNION ALL SELECT '行宽',      '一行占多少字节'
UNION ALL SELECT '每页行数',  '页大小 ÷ 行宽'
UNION ALL SELECT '结论',      '行越窄 → 每页装的行越多 → 读同样多的数据 I/O 越少';

-- 与第 21 章 B+ 树同样的道理：减少 I/O 次数比减少比较次数重要得多
SELECT '② 举例：8KB 的页' AS 说明;
SELECT 100 AS 行宽字节, 8192/100 AS 每页可放行数, '读 10 万行需要约 ' || (100000/(8192/100)) || ' 页' AS I_O估算
UNION ALL
SELECT 500,             8192/500,                 '读 10 万行需要约 ' || (100000/(8192/500)) || ' 页'
UNION ALL
SELECT 2000,            8192/2000,                '读 10 万行需要约 ' || (100000/(8192/2000)) || ' 页';
-- → 行宽从 100 涨到 2000 字节，I/O 次数涨了 20 倍

-- ============================================================
-- 二、选择合适的列类型能显著减小行宽
-- ============================================================

-- ❌ 浪费的设计
CREATE TABLE bloated (
    id     BIGINT,          -- 8 字节，但数据量根本用不到
    status VARCHAR(255),    -- 存的其实只是 'active' / 'inactive'
    flag   VARCHAR(10),     -- 存的其实是 'yes' / 'no'
    score  DOUBLE           -- 分数是整数，用不着浮点
);

-- ✅ 紧凑的设计
CREATE TABLE compact (
    id     INTEGER,         -- 4 字节足够
    status SMALLINT,        -- 用枚举值代替字符串
    flag   BOOLEAN,         -- 1 字节
    score  SMALLINT         -- 0-100 用 SMALLINT 就够
);

SELECT '③ 类型选择对行宽的影响（估算）' AS 说明;
SELECT 'bloated' AS 表, 'BIGINT(8) + VARCHAR(255) + VARCHAR(10) + DOUBLE(8)' AS 列类型,
       '约 100+ 字节/行' AS 估算行宽
UNION ALL
SELECT 'compact',       'INTEGER(4) + SMALLINT(2) + BOOLEAN(1) + SMALLINT(2)',
       '约 9-16 字节/行';
-- → 同样的信息量，行宽可能差一个数量级

-- ============================================================
-- 三、定长在前、变长在后
-- ============================================================

SELECT '④ 列顺序：定长在前、变长在后' AS 说明;
CREATE TABLE good_order (
    id      INTEGER,        -- 定长
    score   INTEGER,        -- 定长
    created DATE,           -- 定长
    name    TEXT,           -- 变长放后面
    bio     TEXT            -- 变长放后面
);
SELECT '定长列（INTEGER/DATE/BOOLEAN）' AS 类别, '放前面' AS 建议, '存储引擎可以直接算偏移' AS 原因
UNION ALL SELECT '变长列（TEXT/BLOB）', '放后面', '长度不定，只能顺序扫描定位';
-- ⚠️ 具体规则因数据库而异（PostgreSQL 有列对齐填充，MySQL InnoDB 有自己的行格式）
--    但共同原则一致：让行更窄

-- ============================================================
-- 四、⚠️ SELECT * 的真实代价
-- ============================================================

CREATE TABLE student (
    id    INTEGER PRIMARY KEY,
    name  TEXT,
    score INTEGER,
    bio   TEXT              -- 假设这是一大段文本
);
INSERT INTO student VALUES
 (1,'Alice',92, '这里假装是一段很长很长的个人简介....................'),
 (2,'Bob',  75, '这里假装是一段很长很长的个人简介....................'),
 (3,'Carol',88, '这里假装是一段很长很长的个人简介....................');

SELECT '⑤ 只查需要的列' AS 说明;
SELECT id, name FROM student;      -- ✅ 只读需要的
-- SELECT * FROM student;          -- ❌ 会把那个巨大的 bio 也读出来

SELECT '  行式存储：读一行就要把整行从磁盘 load 进来' AS 原理
UNION ALL SELECT '  所以 SELECT * 不只是「多传点数据」，而是真的多读了磁盘'
UNION ALL SELECT '  列式存储（ClickHouse/Parquet）把同一列放一起，才能真的只读两列'
UNION ALL SELECT '  → 这是分析型数据库快几十倍的根本原因之一';

-- ============================================================
-- 五、⚠️ ORM：每一行都会变成一个完整的语言对象
-- ============================================================

SELECT '⑥ ORM 的隐藏代价' AS 说明;
SELECT '查询返回 10 万行' AS 场景, '数据库侧可能很快' AS 数据库, '应用侧要创建 10 万个对象' AS 应用
UNION ALL SELECT '每个对象的开销',  '（第 24 章）',      'Java 12字节头 / Python 152 字节'
UNION ALL SELECT '结果',            'SQL 执行 50ms',     '对象创建 + GC 可能几百 ms'
UNION ALL SELECT '这就是',          '「查询很快但程序很慢」', '的常见原因';

SELECT '⑦ 应对办法' AS 说明;
SELECT '只取需要的列' AS 办法, 'SELECT id, name 而不是 SELECT *' AS 做法
UNION ALL SELECT '投影到轻量对象', '很多 ORM 提供 projection / DTO 选项'
UNION ALL SELECT '分页',          'LIMIT + OFFSET，别一次拉全表'
UNION ALL SELECT '流式处理',      '游标逐行处理，避免一次性建全部对象'
UNION ALL SELECT '聚合下推',      '能在数据库算的就别拉到应用里算';

-- 演示：聚合在数据库做，只返回一行
SELECT '⑧ 聚合下推：返回 1 行而不是 N 行' AS 说明;
SELECT COUNT(*) AS 学生数, ROUND(AVG(score),1) AS 平均分, MAX(score) AS 最高分 FROM student;
-- → 相比把 3 行（或 10 万行）全部取回应用层再算，这里只创建了 1 个对象

-- ============================================================
-- 六、小结
-- ============================================================
SELECT '⑨ 小结' AS 说明;
SELECT '行 ≈ 对象' AS 主题, '数据库里对象的对应物是行' AS 要点
UNION ALL SELECT '行存在页里',   '一页能放多少行，决定 I/O 次数（同第 21 章 B+ 树）'
UNION ALL SELECT '让行更窄',     '选合适的列类型，定长在前变长在后'
UNION ALL SELECT 'SELECT *',     '行式存储下会真的多读磁盘，不只是多传数据'
UNION ALL SELECT '列式存储',     '同列数据放一起，才能真正只读需要的列'
UNION ALL SELECT '⚠️ ORM',       '每行变成一个带完整开销的语言对象'
UNION ALL SELECT '共同主题',     '紧凑布局快而省 —— 与本章各语言的结论完全一致';
