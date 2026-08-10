-- 内存：数据库以"页"为单位管理内存与磁盘——页是它的分区制度。

-- ① 页：内存与磁盘之间交换的最小单位
PRAGMA page_size;          -- 每页字节数（默认 4096）
PRAGMA cache_size;         -- 页缓存大小（负数表示 KB）

-- ② 数据住在页里：插入一万行，看库长了几页
CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT, score INTEGER);
PRAGMA page_count;         -- 建表后的页数

WITH RECURSIVE seq(n) AS (
    SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < 10000
)
INSERT INTO student SELECT n, 'student-' || n, n % 100 FROM seq;

PRAGMA page_count;         -- 一万行之后的页数
SELECT COUNT(*) AS rows FROM student;

-- ③ freelist：被删除的页不还给 OS，留着复用（数据库自己的"空闲链表"）
DELETE FROM student WHERE id <= 5000;
PRAGMA freelist_count;     -- 空闲页数——空间没有归还，等下次 INSERT 复用
