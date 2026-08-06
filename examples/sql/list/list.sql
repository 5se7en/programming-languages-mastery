-- 第 17 章 · 列表 — SQL 示例
-- 运行：sqlite3 :memory: < list.sql
-- 对应关系：数据库的「批量插入」之于「逐条插入」，正如 reserve 之于反复扩容

-- 1. 用 position 列模拟「有序列表」（表本身是无序集合，见第 16 章）
CREATE TABLE playlist (position INTEGER, song TEXT);
INSERT INTO playlist VALUES (10, '第一首'), (20, '第二首'), (30, '第三首');
SELECT '有序列表', position, song FROM playlist ORDER BY position;

-- 2. 中间插入：稀疏编号留出空间，避免更新后续所有行（数组"中间插入O(n)"的数据库版本）
INSERT INTO playlist VALUES (15, '插队的歌');
SELECT '中间插入(用稀疏编号)', position, song FROM playlist ORDER BY position;

-- 3. 若编号连续，中间插入就得更新后续所有行 —— 这正是 O(n)
CREATE TABLE dense (position INTEGER, song TEXT);
INSERT INTO dense VALUES (1,'A'), (2,'B'), (3,'C');
UPDATE dense SET position = position + 1 WHERE position >= 2;   -- 搬移后续全部
INSERT INTO dense VALUES (2, '新插入');
SELECT '连续编号需搬移', position, song FROM dense ORDER BY position;

-- 4. 批量插入 vs 逐条插入（对应 reserve 的思想：摊薄固定开销）
CREATE TABLE bulk (n INTEGER);
INSERT INTO bulk VALUES (1),(2),(3),(4),(5),(6),(7),(8),(9),(10);   -- 一次搞定
SELECT '批量插入', COUNT(*) AS 行数 FROM bulk;

-- 5. 取"末尾追加"的语义：用自增主键
CREATE TABLE log (id INTEGER PRIMARY KEY AUTOINCREMENT, msg TEXT);
INSERT INTO log (msg) VALUES ('第一条'), ('第二条'), ('第三条');
SELECT '自增主键(末尾追加)', id, msg FROM log ORDER BY id;
