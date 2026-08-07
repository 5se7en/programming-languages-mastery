-- 泛型：SQL 没有类型参数——但"类型丢失"的代价在数据库里同样真实。

-- ① SQLite 动态类型：同一列可以放不同类型的值（类似动态语言）
CREATE TABLE flexible (val);
INSERT INTO flexible VALUES (90), ('小明'), (3.14), (NULL);
SELECT val, typeof(val) FROM flexible;

-- ② STRICT 表：找回静态类型（SQLite 3.37+）
CREATE TABLE student (id INTEGER, name TEXT, score INTEGER) STRICT;
INSERT INTO student VALUES (1, '小明', 90);
-- 下面这行在 STRICT 表上会报错（实测）：
--   INSERT INTO student VALUES (2, '小红', '优秀');
--   Runtime error: cannot store TEXT value in INTEGER column student.score
SELECT * FROM student;

-- ③ EAV 反模式："数据库里的 Object 容器"，类型全丢
CREATE TABLE eav (entity_id INTEGER, attr TEXT, value TEXT);
INSERT INTO eav VALUES
    (1, 'name', '小明'), (1, 'score', '100'),
    (2, 'name', '小红'), (2, 'score', '59'),
    (3, 'name', '小刚'), (3, 'score', '65');

-- 想找 60 分以上的学生？value 是 TEXT，走的是字符串比较：'100' < '60'！
SELECT entity_id, value AS score FROM eav WHERE attr = 'score' AND value > '60';

-- 必须显式 CAST 才能找回数值语义：
SELECT entity_id, value AS score FROM eav WHERE attr = 'score' AND CAST(value AS INTEGER) > 60;
