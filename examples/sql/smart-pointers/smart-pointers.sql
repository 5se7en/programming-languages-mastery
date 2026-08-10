-- 智能指针：外键的三种 ON DELETE 策略，正好对应三种所有权语义。

PRAGMA foreign_keys = ON;

CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT);

-- ① CASCADE ≈ unique_ptr：拥有者死，被拥有者跟着死
CREATE TABLE homework (
    id INTEGER PRIMARY KEY,
    student_id INTEGER REFERENCES student(id) ON DELETE CASCADE,
    title TEXT
);

-- ② RESTRICT（默认）≈ shared_ptr：还有人引用着，就不许死
CREATE TABLE enrollment (
    id INTEGER PRIMARY KEY,
    student_id INTEGER REFERENCES student(id) ON DELETE RESTRICT,
    course TEXT
);

-- ③ SET NULL ≈ weak_ptr：拥有者死了，引用自动置空（不阻止死亡）
CREATE TABLE locker (
    id INTEGER PRIMARY KEY,
    owner_id INTEGER REFERENCES student(id) ON DELETE SET NULL,
    location TEXT
);

INSERT INTO student VALUES (1, '小明'), (2, '小红');
INSERT INTO homework VALUES (1, 1, '数学作业'), (2, 1, '英语作业');
INSERT INTO locker VALUES (1, 1, 'A-101');
INSERT INTO enrollment VALUES (1, 2, '物理');

-- 删除小明（有 CASCADE 作业 + SET NULL 储物柜，无 RESTRICT 引用）
DELETE FROM student WHERE id = 1;
SELECT '① CASCADE:  作业剩 ' || COUNT(*) || ' 条（随拥有者一起删除 = unique_ptr）' AS r FROM homework;
SELECT '③ SET NULL: 储物柜 owner_id = ' || COALESCE(CAST((SELECT owner_id FROM locker WHERE id=1) AS TEXT), 'NULL')
       || '（引用置空，储物柜还在 = weak_ptr）' AS r;

-- ② RESTRICT：小红仍被 enrollment 引用，删除会被拒绝
--    （报错会中断脚本，故此处只做说明；实际报错见章节 shell 实测：
--     Runtime error: FOREIGN KEY constraint failed）
SELECT '② RESTRICT: 小红仍被 ' || COUNT(*) || ' 条选课引用 -> 删除会被拒绝'
       || '（= shared_ptr 计数非零，不许析构）' AS r FROM enrollment WHERE student_id = 2;
