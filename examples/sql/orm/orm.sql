-- ORM：从数据库一侧看「阻抗失配」——对象有的东西，关系模型没有。

-- ① 继承：对象世界天然支持，关系世界【根本没有这个概念】
-- 三种映射策略，各有取舍
SELECT '① 假设有继承: Payment ← CardPayment / WirePayment' AS r;

-- 策略 A：单表继承（Single Table）——所有子类挤一张表
CREATE TABLE payment_single (
  id      INTEGER PRIMARY KEY,
  kind    TEXT NOT NULL,          -- 鉴别列: 这一行到底是哪个子类
  amount  INTEGER NOT NULL,
  card_no TEXT,                   -- 只有 CardPayment 用
  bank    TEXT                    -- 只有 WirePayment 用
);
INSERT INTO payment_single VALUES
  (1, 'card', 100, '6222***1234', NULL),
  (2, 'wire', 200, NULL, '工商银行');
SELECT '   策略A 单表继承: 一张表 + 鉴别列 kind' AS r;
SELECT '     ' || kind || ' → ' || amount || '（card_no=' || COALESCE(card_no, 'NULL') ||
       ', bank=' || COALESCE(bank, 'NULL') || '）' AS r FROM payment_single;
SELECT '     ✓ 查询最快（无 JOIN）  ✗ 子类专有列必须可空 → 【约束失效】' AS r;

-- 策略 B：连接继承（Joined）——父子各一张表
CREATE TABLE payment_base (id INTEGER PRIMARY KEY, amount INTEGER NOT NULL);
CREATE TABLE payment_card (id INTEGER PRIMARY KEY REFERENCES payment_base(id),
                           card_no TEXT NOT NULL);
CREATE TABLE payment_wire (id INTEGER PRIMARY KEY REFERENCES payment_base(id),
                           bank TEXT NOT NULL);
INSERT INTO payment_base VALUES (1, 100), (2, 200);
INSERT INTO payment_card VALUES (1, '6222***1234');
INSERT INTO payment_wire VALUES (2, '工商银行');
SELECT '   策略B 连接继承: 父表 + 每个子类一张表' AS r;
SELECT '     card → ' || b.amount || ' / ' || c.card_no AS r
  FROM payment_base b JOIN payment_card c ON c.id = b.id;
SELECT '     ✓ 每列都能 NOT NULL（约束保住了）  ✗ 每次查询都要 JOIN' AS r;

-- 策略 C：每类一表（Table Per Class）——彻底分开
CREATE TABLE card_payment (id INTEGER PRIMARY KEY, amount INTEGER NOT NULL, card_no TEXT NOT NULL);
CREATE TABLE wire_payment (id INTEGER PRIMARY KEY, amount INTEGER NOT NULL, bank TEXT NOT NULL);
INSERT INTO card_payment VALUES (1, 100, '6222***1234');
INSERT INTO wire_payment VALUES (2, 200, '工商银行');
SELECT '   策略C 每类一表: 完全独立的表' AS r;
SELECT '     ✓ 最简单、约束最强  ✗ 查「所有支付」要 UNION ALL，且主键要跨表唯一' AS r;
SELECT '     所有支付合计: ' ||
       (SELECT SUM(amount) FROM (SELECT amount FROM card_payment
                                 UNION ALL SELECT amount FROM wire_payment)) AS r;
SELECT '   → 三种策略没有赢家: ORM 让你选，而【选错了很难改】（数据已经落在那个形状里）' AS r;

-- ② 关联：对象持有引用，关系模型用外键 + JOIN
CREATE TABLE author (id INTEGER PRIMARY KEY, name TEXT);
CREATE TABLE book (id INTEGER PRIMARY KEY, author_id INTEGER REFERENCES author(id), title TEXT);
INSERT INTO author VALUES (1, '张三'), (2, '李四');
INSERT INTO book VALUES (1, 1, '书甲'), (2, 1, '书乙'), (3, 2, '书丙');
SELECT '② 一对多: 对象里是 author.books（一个 List），数据库里是 book.author_id（一个外键）' AS r;
SELECT '   author.books 的真身: ' || group_concat(title, ', ') AS r
  FROM book WHERE author_id = 1;
SELECT '   → 对象里「读一个字段」，数据库里是「跑一次查询」——差六个数量级' AS r;
SELECT '   → N+1 就诞生在这个落差里（Python/JS 版各实测了一遍）' AS r;

-- ③ 多对多：对象里两个 List，数据库里必须多一张中间表
CREATE TABLE student (id INTEGER PRIMARY KEY, name TEXT);
CREATE TABLE course (id INTEGER PRIMARY KEY, title TEXT);
CREATE TABLE enrollment (student_id INTEGER, course_id INTEGER, PRIMARY KEY (student_id, course_id));
INSERT INTO student VALUES (1, '甲'), (2, '乙');
INSERT INTO course VALUES (1, '数学'), (2, '物理');
INSERT INTO enrollment VALUES (1,1), (1,2), (2,1);
SELECT '③ 多对多: 对象里 student.courses + course.students，数据库里【多一张表】' AS r;
SELECT '   甲 选了: ' || group_concat(c.title, ', ') AS r
  FROM enrollment e JOIN course c ON c.id = e.course_id WHERE e.student_id = 1;
SELECT '   → 中间表在对象模型里【不存在】——这是最典型的阻抗失配' AS r;
SELECT '   → 一旦中间表需要额外字段（如选课时间），就必须把它提升为一个实体' AS r;

-- ④ 集合语义：对象的 List 有顺序、可重复；表的行没有顺序
SELECT '④ 集合语义对不上:' AS r;
SELECT '   List<T> 有顺序、允许重复；Set<T> 无序、不重复' AS r;
SELECT '   表的行【本身没有顺序】(没有 ORDER BY 时顺序不保证)，也没有「重复」的概念' AS r;
SELECT '   → 想保住顺序就得加一列 position，ORM 再帮你维护它' AS r;
SELECT '   → 第 47 章说过: 不写 ORDER BY 时返回顺序【不是承诺】，只是当前实现的巧合' AS r;

-- ⑤ 类型：对象的类型系统比 SQL 丰富
SELECT '⑤ 类型对不上:' AS r;
SELECT '   枚举 → 存成 TEXT 还是 INTEGER？存文本可读、存数字省空间，改名时各有各的痛' AS r;
SELECT '   值对象(Money{amount, currency}) → 拆成两列，还是塞进一个 JSON 列？' AS r;
SELECT '   NULL → 对象世界有 Optional<T>，关系世界的 NULL 还是三值逻辑（第 47 章）' AS r;
SELECT '   → ORM 的「类型转换器」(AttributeConverter / ValueConverter) 就是为这些而生' AS r;

-- ⑥ ORM 生成的 SQL 值得每次都看一眼
CREATE INDEX idx_book_author ON book(author_id);
SELECT '⑥ 为什么要打开 SQL 日志:' AS r;
EXPLAIN QUERY PLAN SELECT * FROM book WHERE author_id = 1;
SELECT '   ↑ 这条是 ORM 帮你生成的；有没有走索引，只有看了才知道' AS r;
SELECT '   → 常见的坏 SQL: SELECT *（第 47 章: 关闭覆盖索引）、N+1、缺 WHERE 的 JOIN' AS r;
SELECT '   → 三条纪律: 打开 SQL 日志 / 只读加 no-tracking / 关联显式 include' AS r;
