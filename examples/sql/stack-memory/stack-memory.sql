-- 栈内存：SQL 的"递归"CTE 其实是迭代——工作队列，不压调用栈。

-- ① 一百万层"递归"：在命令式语言里必爆栈，这里安然跑完
WITH RECURSIVE cnt(n) AS (
    SELECT 1
    UNION ALL
    SELECT n + 1 FROM cnt WHERE n < 1000000
)
SELECT MAX(n) AS depth FROM cnt;

-- ② 原因：递归 CTE 的求值是"取一行 → 算出新行 → 放回队列"的循环，
--    没有函数调用、没有栈帧——引擎把递归定义改写成了迭代执行。

-- ③ 真正会"压栈"的是表达式嵌套：解析器递归下降（第 3 章），
--    深度由 SQLITE_MAX_EXPR_DEPTH（默认 1000）限制——保护的正是 C 调用栈。
SELECT ((((((1))))));   -- 表达式嵌套没问题，但嵌套上千层会撞上限制
