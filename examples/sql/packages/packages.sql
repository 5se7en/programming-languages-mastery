-- 第 15 章 · 包 — SQL 示例
-- 运行：sqlite3 :memory: < packages.sql
-- 数据库没有传统包管理器，但「迁移」承担了同样的版本管理职责

-- 1. 迁移工具维护的版本表：记录「已经执行到第几版」
CREATE TABLE schema_version (
    version     TEXT PRIMARY KEY,
    description TEXT,
    applied_at  TEXT
);

-- 2. V1：建表
CREATE TABLE student (name TEXT, score INTEGER);
INSERT INTO schema_version VALUES ('V1', 'create student table', '2026-01-01');

-- 3. V2：加字段（每个迁移只执行一次）
ALTER TABLE student ADD COLUMN email TEXT;
INSERT INTO schema_version VALUES ('V2', 'add email column', '2026-01-02');

-- 4. V3：建索引
CREATE INDEX idx_student_score ON student(score);
INSERT INTO schema_version VALUES ('V3', 'create index on score', '2026-01-03');

-- 5. 查看迁移历史 —— 这与代码世界的锁文件是同一种思想：
--    精确记录当前状态，让所有环境可重现
SELECT '迁移历史', version, description, applied_at FROM schema_version ORDER BY version;

-- 6. 验证 schema 已按迁移演进
INSERT INTO student VALUES ('Alice', 92, 'alice@example.com');
SELECT '当前结构', name, score, email FROM student;

-- 7. 幂等检查：迁移工具靠这张表避免重复执行
SELECT '当前版本', MAX(version) AS latest FROM schema_version;
SELECT '是否已应用 V2', COUNT(*) > 0 AS applied FROM schema_version WHERE version = 'V2';
