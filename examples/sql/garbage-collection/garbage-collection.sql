-- 垃圾回收：数据库的"增量 GC"——incremental_vacuum 分期归还空间。

PRAGMA auto_vacuum = INCREMENTAL;       -- 必须在建表前设置

CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT, blob BLOB);
WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 5000
)
INSERT INTO student SELECT n, 'student-' || n, zeroblob(500) FROM seq;

SELECT '插入 5000 行后: ' || (SELECT * FROM pragma_page_count()) || ' 页, freelist='
       || (SELECT * FROM pragma_freelist_count()) AS step1;

-- 产生垃圾：删掉全部数据
DELETE FROM student;
SELECT '删光后:         ' || (SELECT * FROM pragma_page_count()) || ' 页, freelist='
       || (SELECT * FROM pragma_freelist_count()) AS step2;

-- 增量回收：一次只还 100 页——像增量 GC 一样摊薄停顿
PRAGMA incremental_vacuum(100);
SELECT '增量回收100页:  ' || (SELECT * FROM pragma_page_count()) || ' 页, freelist='
       || (SELECT * FROM pragma_freelist_count()) AS step3;

PRAGMA incremental_vacuum(100);
SELECT '再回收100页:    ' || (SELECT * FROM pragma_page_count()) || ' 页, freelist='
       || (SELECT * FROM pragma_freelist_count()) AS step4;

-- 全量回收剩余：参数为 0 或省略 = 清空 freelist（对应 Full GC / 第 33 章的 VACUUM）
PRAGMA incremental_vacuum;
SELECT '全部回收后:     ' || (SELECT * FROM pragma_page_count()) || ' 页, freelist='
       || (SELECT * FROM pragma_freelist_count()) AS step5;
