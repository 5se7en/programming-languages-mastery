-- 反射：数据库的对应物是"元数据查询"——schema 本身也是数据，可以被 SELECT。

CREATE TABLE student (
    id INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    score INTEGER DEFAULT 0
);
CREATE INDEX idx_student_score ON student (score);
CREATE VIEW top_student AS SELECT name, score FROM student WHERE score >= 90;

-- ① sqlite_master：整个库的"Class 对象"，每个表/索引/视图一行
SELECT type, name FROM sqlite_master ORDER BY type, name;

-- ② PRAGMA table_info：枚举一张表的"字段"
PRAGMA table_info(student);

-- ③ 连建表语句本身都存着——ORM 与迁移工具全靠它对比 schema
SELECT sql FROM sqlite_master WHERE name = 'student';
