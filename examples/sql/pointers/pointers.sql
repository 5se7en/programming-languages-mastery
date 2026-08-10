-- 指针：数据库的"指针"是 rowid 与外键——而且系统管理引用、禁止悬垂。

PRAGMA foreign_keys = ON;

-- ① rowid：每一行的"地址"
CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT);
INSERT INTO student VALUES (1, '小明'), (2, '小红'), (3, '小刚');
SELECT rowid, name FROM student WHERE rowid = 2;   -- 按"地址"直达一行

-- ② 外键：跨表的指针
CREATE TABLE enrollment (
    student_id INTEGER REFERENCES student(id) ON DELETE CASCADE,
    course TEXT
);
INSERT INTO enrollment VALUES (1, '数学'), (1, '英语'), (2, '物理');
SELECT s.name, e.course FROM enrollment e JOIN student s ON s.id = e.student_id;

-- ③ ON DELETE CASCADE：删除被指向的行，指向它的"指针"级联回收
DELETE FROM student WHERE id = 1;
SELECT '删除小明后，enrollment 剩 ' || COUNT(*) || ' 行（两条选课级联消失）' AS result
FROM enrollment;

-- ④ 若外键声明为默认（RESTRICT 语义），同样的 DELETE 会被直接拒绝：
--    Runtime error: FOREIGN KEY constraint failed —— 见章节 shell 实测。
--    数据库的立场：要么禁止悬垂（拒删），要么级联回收（CASCADE）——绝不留悬垂引用。
SELECT '仍有 ' || COUNT(*) || ' 行 student' AS remaining FROM student;
