-- 堆内存：数据库的碎片与整理——页内碎片、整页回收与 VACUUM。

CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT, score INTEGER);
WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 10000
)
INSERT INTO student SELECT n, 'student-' || n, n % 100 FROM seq;

-- ① 基线：一万行占了多少页
SELECT 'page_count 基线' AS step, (SELECT * FROM pragma_page_count()) AS pages,
       (SELECT * FROM pragma_freelist_count()) AS free_pages;

-- ② 删除全部奇数行：每一页都还有活数据 → 整页无法回收（页内碎片）
DELETE FROM student WHERE id % 2 = 1;
SELECT '删一半(奇数行)后' AS step, (SELECT * FROM pragma_page_count()) AS pages,
       (SELECT * FROM pragma_freelist_count()) AS free_pages;

-- ③ 再删掉剩下的一半：整页清空 → 进 freelist（可复用但不还 OS）
DELETE FROM student;
SELECT '删光后' AS step, (SELECT * FROM pragma_page_count()) AS pages,
       (SELECT * FROM pragma_freelist_count()) AS free_pages;

-- ④ VACUUM：重建数据库，把洞挤掉——数据库版的"碎片整理"
VACUUM;
SELECT 'VACUUM 后' AS step, (SELECT * FROM pragma_page_count()) AS pages,
       (SELECT * FROM pragma_freelist_count()) AS free_pages;
