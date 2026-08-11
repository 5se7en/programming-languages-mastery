# 第 47 章 · SQL

**简体中文** ｜ [English](./47-sql.en-US.md)

---

> 本书的六门语言里，SQL 是唯一的**声明式**语言。前五门都要你写「怎么做」——循环、条件、赋值；SQL 只要你写「要什么」，**怎么找由优化器决定**。这一字之差，是本章的全部内容。
>
> 声明式的红利有多大？本章的**钥匙实验**亲手实现了 JOIN 的三种物理算法，用同一份数据（5 万订单 × 5 千用户）跑出：**嵌套循环 414 ms、哈希连接 2.6 ms（快 159x）、归并连接 4.1 ms（快 102x）**——三种算法结果完全一致。而你写的 SQL 只有一种：`JOIN ... ON`。**优化器按统计信息在这三者间替你选**，这就是声明式的全部含义。C# 和 Java 的实测从反面印证了它：LINQ 的 `Join` 快 342x、Java 手工建 `Set` 快 146x——但在这两门语言里，**选算法的人是你**。
>
> 不过本章也**推翻了一个流传甚广的说法**。「`IN`、`EXISTS`、`JOIN` 三种写法优化器都会归一成同一个计划」——实测证伪：同一个问题、同样返回 1000 行，`JOIN` 0.7 ms、`IN` 1.2 ms，而 `EXISTS` **31.9 ms，慢 46 倍**。`EXPLAIN` 给出了原因：`IN`/`JOIN` 走 `LIST SUBQUERY`（子查询物化一次），**相关子查询** `EXISTS` 走 `CORRELATED SCALAR SUBQUERY`——外层每一行都要重跑一次。优化器能归一很多写法，但归一不掉「相关性」。
>
> 声明式也有它的边界，而且比想象中窄。实测四种「让优化器缴械」的写法：列上套一个 `+ 0` 让点查从 1.39 ms 变成 **1779 ms（慢 1283 倍）**；`LIKE '%-4200'` 比 `LIKE 'user-4200%'` 慢 **420 倍**；更隐蔽的是——**光有索引还不够**：`name` 列上建了普通索引，前缀 `LIKE` 的计划**依然是 SCAN**，因为 LIKE 默认大小写不敏感而索引是 BINARY 排序规则，必须建成 `COLLATE NOCASE` 才会变 SEARCH。
>
> 最后是两个每天都在发生的代价：`SELECT *` 让**覆盖索引**永久失效（实测慢 4.4 倍，`COVERING INDEX` 从计划里消失）；而 **N+1 查询**——「for 每个用户 { 查它的订单 }」这个读起来天经地义的循环，实测发出 **1001 条 SQL 耗时 391.7 ms**，改成一条 `JOIN + GROUP BY` 只要 **7.6 ms，快 51 倍**；而这还是进程内的 sqlite，换成网络数据库每次再加 0.5 ms 往返，N+1 就是致命的。

## 1. 学习目标

学完本章，你将能够：

- 说清**声明式与命令式的分界**，并用 JOIN 三算法的实测（159x）解释优化器的价值；
- 复述 SQL 的**逻辑执行顺序**（`FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT`）并解释它导致的别名规则；
- 判断一个查询能否被索引服务——识别**四类让优化器缴械的写法**（列上表达式、前导通配、排序规则不匹配、`SELECT *`）；
- 识别 **N+1 查询**并用「N+1 → IN → JOIN」三级台阶改写它（实测 51x）；
- 解释参数化查询同时买到的三样东西（**防注入 + 语句复用 + 类型正确**）。

---

## 2. 为什么会出现这个概念

### 第 46 章留下的问题

```text
第 46 章证明了「必须用数据库」，但没回答: 怎么跟它说话？
两种可能的接口设计:
  命令式: openTable("users"); while (row = next()) { if (row.score > 90) ... }
  声明式: SELECT * FROM users WHERE score > 90
```

**1970 年 Codd 的关系模型选了后者，理由是一个至今成立的观察**：

```text
数据的【物理组织】会变（加索引、换存储引擎、数据量涨了一万倍）
但你要问的【问题】不变（「分数大于 90 的用户」）
→ 把「问什么」和「怎么找」分开，前者写进应用，后者留给数据库
→ 加个索引就能提速一万倍，而应用代码【一个字都不用改】
```

### 实测这个承诺的兑现

**同一句 SQL，加索引前后**（第 46 章实测）：

```text
无索引: SCAN users                                  ← 全表扫
建索引: SEARCH users USING INDEX idx_score (score=?)  ← 索引查
→ 你的 SQL 一个字都没改，优化器换了算法
```

**而同一个 JOIN，优化器手里有三张牌**（本章 C++ 实测）：

| 算法 | 实测 | 复杂度 | 前提 |
|------|------|--------|------|
| 嵌套循环 | 414 ms | O(N×M) | 无 |
| **哈希连接** | **2.6 ms（159x）** | O(N+M) | 等值连接 |
| 归并连接 | 4.1 ms（102x） | O(N log N) | 可排序 |

**命令式语言里这三种算法是三段完全不同的代码**；SQL 里它们是同一句话。

> **一句话总结**：SQL 的声明式不是「语法更简洁」，而是**把算法选择权从程序员手里转移给了能看见数据统计信息的优化器**——数据分布变了、索引加了、数据量涨了，优化器自动换策略，而你的代码是一个常量。

---

## 3. 底层原理

### 钥匙实验一：JOIN 的三种物理实现

SQL 写法只有一种，数据库执行它有三种算法。本章用 C++ 亲手实现了全部三种（5 万订单 JOIN 5 千用户）：

**① 嵌套循环连接（Nested Loop Join）**

```cpp
for (const auto& o : orders)
    for (const auto& u : users)          // 每行 orders 扫一遍 users
        if (o.user_id == u.id) { ... break; }
```

```text
414 ms（最坏 5 万 × 5 千 = 2.5 亿次比较）
→ 无索引、无排序、【任何】JOIN 条件都能用——数据库的保底算法
```

**② 哈希连接（Hash Join）**

```cpp
for (const auto& u : users) ht.emplace(u.id, &u);      // 建表阶段: 小表进哈希
for (const auto& o : orders) ht.find(o.user_id);       // 探测阶段: 大表逐行查
```

```text
2.6 ms —— 快 159x
→ 只适用【等值】连接（=）；小表建哈希、大表探测
→ 这就是「小表驱动大表」这条经验法则的真正出处
```

**③ 归并连接（Merge Join）**

```cpp
while (i < so.size() && j < su.size()) {               // 双指针归并
    if (so[i].user_id < su[j].id) ++i;
    else if (so[i].user_id > su[j].id) ++j;
    else { ...; ++i; }
}
```

```text
4.1 ms —— 快 102x（含排序成本；两边本就有序时是三者最快）
→ 适合等值/范围连接；输出天然有序——ORDER BY 同键时白赚一次排序
```

**优化器的选择表**：

| 算法 | 复杂度 | 前提 | 数据库何时选它 |
|------|--------|------|--------------|
| 嵌套循环 | O(N×M) | 无 | 小表，或内表有索引 |
| 哈希连接 | O(N+M) | 等值连接 | 大表等值、内存够 |
| 归并连接 | O(N log N + M log M) | 可排序 | 两边已有序/有索引 |

**第四张牌**：内表有索引时，嵌套循环变身 **Index Nested Loop**——每行 O(log M)，是小结果集之王。

**与前面章节的呼应**（数据库没有新数据结构）：

```text
哈希连接  = 第 20 章的哈希表（O(1) 查找）用在两表之间
归并连接  = 第 19 章的双指针 / 归并排序的合并步
Index NLJ = 第 21 章的 B 树（O(log n)）逐行下钻
→ 数据库没有发明新数据结构，只有把老数据结构用到极致的优化器
```

### 逻辑执行顺序：SELECT 写在最前，执行在倒数第二

```mermaid
flowchart LR
    F["FROM<br/>取表、做 JOIN"] --> W["WHERE<br/>过滤行"]
    W --> G["GROUP BY<br/>折叠成组"]
    G --> H["HAVING<br/>过滤组"]
    H --> S["SELECT<br/>算出列"]
    S --> O["ORDER BY<br/>排序"]
    O --> L["LIMIT<br/>截断"]
```

**这个顺序直接解释了两条常被死记硬背的规则**：

```text
① WHERE 里【用不了】SELECT 起的别名 —— WHERE 执行时 SELECT 还没跑
② ORDER BY 里【可以】用别名          —— ORDER BY 在 SELECT 之后
③ WHERE 过滤行、HAVING 过滤组       —— 它们分别在 GROUP BY 前后
→ 记住这张图，这些规则就不用背了
```

### 钥匙实验二：「殊途同归」是个误解

**流传甚广的说法**：「`IN`、`EXISTS`、`JOIN` 三种写法，优化器都会归一成同一个计划，写哪个都一样。」

**实测证伪**（Python，10 万行表，同一个问题，都返回 1000 行）：

```text
IN 子查询:     1.2 ms
EXISTS   :    31.9 ms      ← 慢 46 倍
JOIN     :     0.7 ms
```

**`EXPLAIN` 给出了原因**：

```text
IN 的计划:     |--SEARCH users USING INTEGER PRIMARY KEY
               `--LIST SUBQUERY 1        ← 子查询【物化一次】，之后重复使用
                  `--SCAN orders

EXISTS 的计划: |--SCAN u                 ← 外层全扫
               `--CORRELATED SCALAR SUBQUERY 1   ← 外层【每一行】都重跑一次
                  `--SCAN o
```

**关键区别是「相关性」（correlation）**：

```text
不相关子查询: 子查询不引用外层的列 → 可以【算一次，存起来，重复用】
相关子查询:   子查询引用了外层的列（如 s.id = u.id）→ 外层每行的子查询【都不一样】
             → 只能逐行求值，N 行就是 N 次
→ 优化器能归一很多写法，但归一不掉相关性——这是逻辑上的硬边界，不是实现缺陷
```

**注意这不是说 `EXISTS` 总是慢**：当外层很小、或子查询能用索引时，`EXISTS` 能提前短路（找到一行就返回），反而更快。**结论是「要看计划，别背口诀」**。

### 让优化器缴械的四种写法

**① 列上套表达式**（Python 实测）

```text
WHERE id = ?     200 次:     1.39 ms（主键 B 树 SEARCH）
WHERE id + 0 = ? 200 次:   1779.3 ms（表达式包裹 → 全表 SCAN）
→ 慢 1283x
```

```text
原因: 索引存的是【列的值】的有序结构；优化器无法透过 f(列) 反推出该查哪个区间
同类坑: WHERE DATE(created_at) = '2026-08-11'   WHERE UPPER(name) = 'ABC'
正确写法: WHERE created_at >= '2026-08-11' AND created_at < '2026-08-12'
```

**② 前导通配符**（Python 实测）

```text
LIKE 'user-4200%'（前缀）:  0.007 ms  → SEARCH ... (name>? AND name<?)
LIKE '%-4200'（前导通配）:  2.996 ms  → SCAN
→ 慢 420x
```

**B 树按前缀有序（第 21 章）**：知道前缀就能定位区间（`name >= 'user-4200' AND name < 'user-4201'`），前导 `%` 让有序性完全无从下手。

**③ 排序规则不匹配——最隐蔽的一个**（Python 实测）

```text
普通索引 + 前缀 LIKE: SCAN users USING COVERING INDEX idx_name     ← 竟然还是 SCAN！
NOCASE 索引 + 前缀 LIKE: SEARCH users USING COVERING INDEX (name>? AND name<?)
```

```text
原因: sqlite 的 LIKE 默认【大小写不敏感】，而 CREATE INDEX 默认是 BINARY 排序规则
     两者语义不一致 → 索引对这个 LIKE 无效
解法: CREATE INDEX idx ON users(name COLLATE NOCASE)  或  PRAGMA case_sensitive_like=ON
→ 「我明明建了索引为什么还是慢」的高频答案之一
```

**④ `SELECT *` 关闭覆盖索引**（Python 实测，同一个 `WHERE score = 42`）

```text
SELECT id（列全在索引里）:         0.13 ms  → SEARCH ... USING COVERING INDEX
SELECT id, name（回表取 name）:    0.41 ms（慢 3.1x）→ SEARCH ... USING INDEX
SELECT *（回表 + 搬 200B payload）: 0.57 ms（慢 4.4x）
```

```text
覆盖索引（covering index）: 查询要的列【全都在索引里】→ 读完索引就能返回，不用回表
计划里的 COVERING 一词消失，就意味着每命中一行都要回表读一次
→ SELECT * 把这个优化【永久】关闭了——它总会要到索引里没有的列
```

### N+1 查询：最贵的那个「看起来很自然」的循环

**JS 实测**（1000 用户，20000 订单）：

```javascript
const users = db.prepare('SELECT id, name FROM users').all();
for (const u of users) {
  perUserStmt.get(u.id);          // ← 每个用户一次查询
}
```

```text
① N+1: 发出 1001 条 SQL（1 + 1000），耗时 391.7 ms
② 一条 JOIN + GROUP BY: 7.6 ms   ← 快 51x
③ IN 批量（一次取回所有需要的）: 4.5 ms
```

**三级台阶**：

```text
N+1  → 循环里查询，1 + N 次往返（最差）
IN   → 收集所有 id，一次 IN 查询（ORM 的 DataLoader / eager loading 就是它）
JOIN → 让数据库在内部关联，一次往返（最好，且无参数个数限制）
```

**⚠️ 且这还是进程内的 sqlite**。换成网络数据库，每次往返再加 0.5 ms RTT，1000 次就是 500 ms 的纯网络等待——N+1 在生产里是致命的。

---

## 4. JavaScript

JS 版承包了 N+1 的实测，以及参数化的第三重红利。

### N+1 的三级台阶（实测）

```text
① N+1 查询: 1001 条 SQL，391.7 ms
② JOIN + GROUP BY: 1 条 SQL，7.6 ms（快 51x，结果一致）
③ IN 批量: 1 条带 1000 个占位符的 SQL，4.5 ms
```

**为什么 N+1 这么容易写出来**：

```javascript
for (const u of users) { u.orders = getOrders(u.id); }   // 读起来天经地义
```

这段代码在纯内存里是完全正常的写法——**问题在于每次 `getOrders` 都是一次数据库往返**。ORM 让这一点更隐蔽：`user.orders` 看起来只是个属性访问，背后却是一条 SQL（懒加载）。

### 占位符的边界（实测）

```text
sqlite 默认参数上限 999（SQLITE_MAX_VARIABLE_NUMBER）——IN 列表太长会撞墙
→ 真正海量关联应回到 JOIN（数据库内部做，无参数个数限制）
```

**这是「IN 批量」方案的天花板**：它把 N 次往返压成 1 次，但参数个数有上限（PostgreSQL 是 65535，Oracle 的 IN 列表上限 1000）。数据量大到一定程度，只有 JOIN 能走。

### 参数化顺带解决注入（实测）

```text
恶意输入 "'; DROP TABLE users; --" 走参数化: 匹配 0 行，users 表安然无恙
验证表还在: 1000 个用户
```

**注意「顺带」二字**：你用 `prepare` 的动机可能是复用语句，但它同时买到了防注入——因为占位符从机制上就**把值和结构分开**了。

> **注意**：`node:sqlite` 的 `prepare` 返回的语句对象可重复 `run`/`get`/`all`；服务器数据库驱动（pg/mysql2）的参数占位符语法不同（`$1` vs `?`），但语义一致；JS 生态里模板字符串拼 SQL 极其危险——用 `sql` 标签模板库（如 `postgres.js`）能让拼接自动参数化。

---

## 5. Python

Python 版是「让优化器缴械」的主战场——四类反模式全部实测。

### SQL 注入：拼接是把查询【结构】交给了用户（实测）

```python
evil = "' OR '1'='1"
sql = f"SELECT COUNT(*) FROM users WHERE name = '{evil}'"
```

```text
拼接版实际执行: SELECT COUNT(*) FROM users WHERE name = '' OR '1'='1'
拼接版返回 100000 行（全表泄露！）；参数化版返回 0 行 ✓
```

**注入的本质不是「特殊字符」而是「结构被改写」**：用户输入的 `' OR '1'='1` 从数据变成了**语法**——多了一个 `OR` 子句。参数化把输入钉死为「一个值」，无论内容是什么都不会成为语法。

### 参数化的第二重红利：解析一次，执行万次（实测）

```text
20000 次点查，每次拼新 SQL:   312.7 ms（每次都要重新解析 + 编译执行计划）
20000 次点查，参数化复用:     192.3 ms
→ 快 1.6x —— 服务器数据库差距更大（网络上还省了硬解析）
```

sqlite3 模块按 **SQL 文本**缓存语句（默认 128 条）；f-string 版本产生 2 万条不同文本，缓存全部落空。

### 四类缴械写法（实测汇总）

| 反模式 | 实测 | 计划变化 |
|--------|------|---------|
| `WHERE id + 0 = ?` | 1779 ms vs 1.39 ms（**1283x**） | SEARCH → SCAN |
| `LIKE '%-4200'` | 2.996 ms vs 0.007 ms（**420x**） | SEARCH → SCAN |
| 排序规则不匹配 | 前缀 LIKE 仍是 SCAN | 建 `COLLATE NOCASE` 才变 SEARCH |
| `SELECT *` | 0.57 ms vs 0.13 ms（**4.4x**） | COVERING INDEX → INDEX（回表） |

### 「殊途同归」的证伪（实测）

```text
IN 子查询:     1.2 ms → 1000 行
EXISTS   :    31.9 ms → 1000 行     ← 慢 46x
JOIN     :     0.7 ms → 1000 行
```

> **注意**：`sqlite3` 的参数占位符是 `?`（也支持 `:name` 具名参数）；`executemany` 比循环 `execute` 快得多（一次事务）；`cur.execute("... WHERE id IN (?)", (list,))` 是错的——IN 列表要自己生成对应个数的占位符；Python 3.12+ 的 `sqlite3` 支持 `autocommit` 属性，语义比旧的 `isolation_level` 清晰。

---

## 6. Java

Java 版换了个角度：**SQL 的声明式思想反哺了命令式语言**——Stream 就是「内存里的 SQL」。

### 五个动词一一对应

```text
WHERE → filter    SELECT → map    ORDER BY → sorted
LIMIT → limit     GROUP BY → Collectors.groupingBy
```

```java
users.stream()
     .filter(u -> u.score() >= 95)                             // WHERE
     .sorted(Comparator.comparingInt(User::score).reversed())  // ORDER BY DESC
     .limit(3)                                                 // LIMIT 3
     .collect(Collectors.toList());
```

```text
命令式 12 行: 2.62 ms；声明式 4 行: 2.96 ms（结果一致: true）
→ 「怎么做」写死在循环里 vs 「要什么」交给流水线
```

### 但 Stream 暴露了 SQL 替你藏起来的东西（实测）

```text
嵌套循环写法 anyMatch: 545 ms
先建 Set 再 contains:  3.7 ms（快 146x，结果一致）
```

**这就是本章的核心论点**：

```text
Stream 里【你】得决定用嵌套循环还是哈希——写错一个 anyMatch 就慢 146 倍
SQL 里【优化器】替你决定——你只写 JOIN，它看着统计信息选（C++ 版三种算法全实现）
→ 声明式的赢面不在语法，而在【谁来选算法】
```

### 惰性求值：与 SQL 共享的执行模型（实测）

```text
findFirst 只摸了 100 行就命中（共 10000 行）
→ 对应 SQL 的 LIMIT 1：优化器同样会「够了就停」，不会算完再截断
```

### 为什么 Stream 取代不了数据库

```text
Stream 处理的是【已在内存里】的集合——数据得先全捞进来（第 46 章实测: 109ms + 46MB）
SQL 处理的是【磁盘上】的表——只把结果捞回来，且有索引/事务/持久化
→ Stream 学到了 SQL 的【声明式表达】，学不到它的【存储与优化】
```

> **注意**：生产用 JDBC 的 `PreparedStatement`（占位符 `?`），绝不要用 `Statement` 拼字符串；`PreparedStatement` 的参数从 1 开始编号（不是 0）；批量插入用 `addBatch`/`executeBatch`；`ResultSet` 要在 try-with-resources 里关（第 37 章）；Java 无法把 lambda 转译成 SQL——这是它与 C# LINQ 的根本差距。

---

## 7. C++

C++ 版承包了本章的钥匙实验：**亲手实现优化器的三张牌**。

### 三种算法实测

```text
嵌套循环:      414 ms
哈希连接:      2.6 ms（快 159x）
归并连接:      4.1 ms（快 102x，含排序成本）
结果一致: true（匹配 50000 行，金额和 12475000）
```

**三段代码写下来，你会有一个直观感受**：这三种算法在命令式语言里是**三段完全不同的程序**——不同的数据结构、不同的循环结构、不同的适用前提。而 SQL 里它们是同一句 `JOIN ... ON`。

### 为什么这个实验重要

```text
写 SQL 时你以为自己在「描述数据」，其实你是在【放弃控制权】:
  放弃选择算法的权利 → 换来数据量/索引/分布变化时【自动】换算法
→ 这笔交易在数据规模会变的场景里永远划算（而数据规模总会变）
→ 但也意味着: 优化器选错时，你需要能读懂 EXPLAIN 才能干预
```

### C++ 与数据库的关系（承接第 46 章）

```text
SQLite/MySQL/PostgreSQL 的优化器都是 C/C++ 写的
→ 本章这三个函数，就是它们内部 Join 算子的教学版
→ 真实优化器还要做: 代价估算、统计信息采样、连接顺序枚举（N 表 JOIN 有 N! 种顺序）
```

**连接顺序是优化器最难的部分**：3 张表 JOIN 有 12 种顺序，10 张表有 3 亿种——真实优化器用动态规划 + 启发式剪枝在有限时间内找一个够好的。这也是「表太多时优化器可能选错」的原因。

> **注意**：C++ 侧连数据库用 sqlite3 C API（`sqlite3_prepare_v2` + `sqlite3_bind_*` 就是参数化）或 libpq；`sqlite3_bind_text` 的最后一个参数（析构器）用错会导致悬垂指针——第 34 章的坑在 C API 里重演。

---

## 8. C#

C# 是五门语言里对 SQL **致敬最彻底**的：它把 SQL 编进了语言本身。

### 两套写法，编译成同一个东西（实测）

```csharp
var querySyntax =                     // 查询语法：几乎就是 SQL
    from u in users
    where u.Score >= 95
    orderby u.Score descending
    select u.Name;

var methodSyntax = users              // 方法语法：链式调用
    .Where(u => u.Score >= 95)
    .OrderByDescending(u => u.Score)
    .Select(u => u.Name);
```

```text
两者结果一致: True
→ 查询语法是【语法糖】，编译器直接翻译成方法调用——C# 3.0(2007) 为 LINQ 而生
```

### 真正的杀手锏：表达式树（实测）

```csharp
Func<User, bool> compiled = u => u.Score > 90;          // 委托: 编译成 IL，只能【执行】
Expression<Func<User, bool>> tree = u => u.Score > 90;  // 表达式树: 保留【结构】
```

```text
委托只能调用: compiled(users[95]) = True
表达式树可以【读】: u => (u.Score > 90)
  根节点类型: GreaterThan
  左边: u.Score（成员访问）  右边: 90（常量）
→ EF Core 正是【读】这棵树，把它翻译成 SQL 的 WHERE score > 90
```

**这是 C# 相对 Java Stream 的根本差距**：Java 的 lambda 编译成字节码后**无法内省**，所以 Stream 的查询只能在内存里执行，永远转译不回 SQL；C# 的表达式树保留了语法结构，ORM 能读懂它并生成 SQL。

### `IEnumerable` vs `IQueryable`：在哪儿执行的分水岭

```text
IEnumerable<T>.Where(谓词是 Func)       → 数据先拉到内存，在【本地】过滤
IQueryable<T>.Where(谓词是 Expression)  → 翻译成 SQL，在【数据库】过滤
→ 一个 .AsEnumerable() 放错位置，就把「数据库过滤」变成「全表拉回内存过滤」
→ 第 46 章实测过这个代价: 拉全表 109ms + 46MB vs 让数据库查 9μs
```

**这是 EF Core 最经典的性能事故**——两个类型看起来几乎一样，行为差了三个数量级。

### LINQ 的 JOIN 只有一张牌（实测）

```text
Any() 嵌套循环写法: 1686 ms
Join() 哈希连接:    4.9 ms（快 342x，结果一致）
→ LINQ 的 Join 内部建哈希表——与 C++ 版实测的 Hash Join 同一个算法
→ 但它【只会】哈希连接；SQL 优化器会在三种算法间按数据量选
```

### 延迟执行（实测）

```text
构造查询后还没执行: 已摸 0 行
调用 First() 才执行，且只摸了 100 行就命中 user-99
→ 延迟执行 + 短路，与 SQL 的 LIMIT 1 同一个思想
```

> **注意**：ADO.NET 的参数化用 `cmd.Parameters.AddWithValue`（但它会推断类型，大表上建议显式指定 `SqlDbType`）；EF Core 的 `FromSqlRaw` 支持插值字符串并**自动参数化**（`$"...{userInput}"` 是安全的，这是少数拼接安全的场景）；`ToList()` 触发执行，位置放错会导致后续 `Where` 变成内存过滤。

---

## 9. SQL

本节回到 SQL 自身，看它作为一门语言的几个关键设计。

### 逻辑执行顺序（实测印证）

```text
FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT
→ 所以 WHERE 里用不了 SELECT 起的别名（它还没执行到）
→ 而 ORDER BY 可以用（它在 SELECT 之后）
```

### JOIN 语义：INNER 丢孤儿，LEFT 留孤儿（实测）

```text
INNER JOIN 行数: 41663
LEFT  JOIN 行数: 50000
孤儿订单（user 已不存在）: 8337
```

```sql
SELECT COUNT(*) FROM orders o LEFT JOIN users u ON o.user_id = u.id WHERE u.id IS NULL;
```

**`LEFT JOIN + IS NULL` 是「找孤儿」的标准姿势**——差值 8337 正是 `INNER JOIN` 悄悄丢掉的行。这也是数据对账时最常用的一句 SQL：**统计口径不一致，往往就是有人用了 INNER 而另一个人用了 LEFT**。

### GROUP BY 折叠行，窗口函数保留行（实测）

```text
GROUP BY: 每城市一行（折叠）
  city-9 平均分 54.0   city-8 平均分 53.0   city-7 平均分 52.0

窗口函数: 每行保留，附上组内信息
  user-0 在 city-0 排名 901
```

```sql
RANK() OVER (PARTITION BY city ORDER BY score DESC)
```

**分界线很清晰**：

```text
要【汇总】               → GROUP BY（N 行折叠成 M 行）
要【明细 + 组内位置】     → 窗口函数（N 行还是 N 行，每行多几列）
→ 「每个部门薪资前三名」这类问题必须用窗口函数，GROUP BY 做不到
```

### NULL 的三值逻辑（实测）

```text
NULL = NULL 的结果: 不是真（是 NULL）
NULL 要用 IS NULL 判断
COUNT(*) = 3，COUNT(v) = 2，SUM(v) = 4    ← 表里有 1、NULL、3
```

**三值逻辑（TRUE / FALSE / UNKNOWN）是 SQL 最大的认知陷阱**：

```text
① NULL = NULL 是 UNKNOWN，不是 TRUE → 必须用 IS NULL
② COUNT(*) 数行，COUNT(列) 跳过 NULL → 两者结果不同
③ WHERE v != 5 【不会】返回 v 为 NULL 的行（UNKNOWN 不等于 TRUE）
   → 「为什么我的 != 查询少了几行」的标准答案
④ NULL 参与的算术全是 NULL（1 + NULL = NULL）；但 SUM 会跳过 NULL
```

### CTE：给查询分层

```sql
WITH spend AS (
  SELECT user_id, SUM(amount) AS total FROM orders GROUP BY user_id
), ranked AS (
  SELECT u.city, s.total FROM spend s JOIN users u ON u.id = s.user_id
)
SELECT city, SUM(total) FROM ranked GROUP BY city ORDER BY SUM(total) DESC LIMIT 1;
```

**`WITH` 子句 = 查询里的「局部变量」**——第 8 章「为什么需要变量而不是直接用地址」的动机，在 SQL 里原样重演：**给中间结果起个名字，让嵌套变成流水线**。

> **注意**：CTE 在部分数据库里是优化屏障（PostgreSQL 12 之前 `WITH` 总是物化，之后默认可内联，`MATERIALIZED` 关键字可强制）；递归 CTE（`WITH RECURSIVE`）能做图遍历，第 32 章实测过它百万层不爆栈；`EXPLAIN` 的语法各家不同（PostgreSQL 是 `EXPLAIN ANALYZE`，MySQL 是 `EXPLAIN FORMAT=JSON`）。

---

## 10. 五语言横向对比

### ① 声明式查询能力

| 能力 | SQL | C# (LINQ) | Java (Stream) | JavaScript | Python |
|------|-----|-----------|---------------|-----------|--------|
| 声明式语法 | ✅ 原生 | ✅ **查询语法 + 方法语法** | ✅ 方法链 | 数组方法链 | 列表推导 / pandas |
| `GROUP BY` | ✅ | ✅ `GroupBy` | ✅ `groupingBy` | ❌ 手写 reduce | `itertools.groupby` / pandas |
| `JOIN` | ✅ **优化器选三种算法** | ✅ `Join`（只有哈希） | ❌ 手写 | ❌ 手写 | pandas `merge` |
| 窗口函数 | ✅ | ❌ | ❌ | ❌ | pandas 有 |
| 延迟执行 | ✅ | ✅ | ✅ | ❌（数组方法立即） | 生成器 ✅ |
| **能否转译回 SQL** | — | ✅ **表达式树** | ❌ lambda 无法内省 | ❌ | ORM 靠解析 AST |
| 作用对象 | **磁盘上的表** | 内存或数据库 | 仅内存 | 仅内存 | 内存 / DataFrame |

### ② 钥匙实验一：JOIN 三算法（C++ 实测）

```text
嵌套循环 414 ms → 哈希连接 2.6 ms（159x）→ 归并连接 4.1 ms（102x）
结果完全一致；而 SQL 写法只有一种
→ 对照 Java 实测 146x（anyMatch vs Set）、C# 实测 342x（Any vs Join）
→ 在命令式语言里，选错算法的代价由【你】承担
```

### ③ 钥匙实验二：「殊途同归」证伪（Python + SQL 实测）

```text
IN 1.2 ms / JOIN 0.7 ms / EXISTS 31.9 ms（慢 46x）
EXPLAIN 揭示: LIST SUBQUERY（物化一次） vs CORRELATED SCALAR SUBQUERY（每行重跑）
→ 优化器能归一很多写法，归一不掉【相关性】
```

### ④ 让优化器缴械的四种写法（Python 实测）

```text
列上套表达式  WHERE id + 0 = ?     慢 1283x   SEARCH → SCAN
前导通配符    LIKE '%-4200'        慢  420x   SEARCH → SCAN
排序规则不匹配 普通索引 + LIKE 前缀  仍是 SCAN  需 COLLATE NOCASE
SELECT *      回表 + 搬 payload     慢  4.4x   COVERING INDEX → INDEX
```

### ⑤ 共性与根因

**共性**：所有语言都借走了 SQL 的动词（filter/map/sorted/group）；但**只有 SQL 把算法选择权交给了优化器**——其余语言里选算法的都是程序员（Java 实测 146x、C# 实测 342x 的代价由你承担）。

**根因**：

- **SQL 是唯一为「数据在磁盘上、规模会变」设计的语言**——所以必须把物理执行留给能看见统计信息的优化器；
- **C# 用表达式树打通了两端**：查询既能在内存执行，又能转译成 SQL 下推给数据库——代价是引入了 `IEnumerable`/`IQueryable` 这个著名陷阱；
- **Java 的 lambda 编译后无法内省**，所以 Stream 永远只能在内存里跑——这不是设计疏忽，而是 Java 8 lambda 实现方式（invokedynamic + 方法句柄）的必然结果；
- **JS/Python 没有内建 JOIN/GROUP BY**——它们的集合 API 早于 LINQ，是函数式传统（map/filter/reduce）而非关系代数传统；
- **SQL 是唯一「反向输出」范式的领域语言**：1974 年的 SEQUEL 影响了此后所有语言的集合 API。

---

## 11. 底层实现对比

| 阶段 | 做什么 | 关键细节 |
|------|--------|---------|
| **解析（Parse）** | SQL 文本 → 抽象语法树 | 参数化查询在这一步就能命中缓存（Python 实测 1.6x） |
| **重写（Rewrite）** | 展开视图、常量折叠、子查询提升 | `IN` 转半连接、不相关子查询提升——「殊途同归」发生在这里 |
| **优化（Optimize）** | 枚举计划、代价估算、选最优 | JOIN 算法选择 + 连接顺序枚举（N 表有 N! 种） |
| **执行（Execute）** | 火山模型/向量化执行算子树 | 每个算子 `next()` 拉一行（第 44 章协程的 `yield` 同构） |

**代价估算靠统计信息**：

```text
数据库定期采样表的行数、列的基数（distinct 值个数）、值分布直方图
→ 「score = 42 大约命中 1000 行」这类估算就来自它
→ 统计信息过期 = 优化器判断失误（PostgreSQL 的 ANALYZE、MySQL 的 ANALYZE TABLE 用来刷新）
→ 「昨天还好好的查询今天突然变慢」的常见原因之一
```

**火山模型与协程的同构**（呼应第 44 章）：

```text
执行计划是一棵算子树，每个算子实现 next(): 产出一行然后【暂停】，被拉时再【恢复】
→ 这正是第 44 章的生成器/协程模型
→ 所以数据库能做到「LIMIT 1 就停」——上层不拉了，下层自然不算（Java/C# 实测的短路同源）
```

---

## 12. 性能分析

### 本章实测数字速查

```text
JOIN 算法:      嵌套循环 414ms → 哈希 2.6ms（159x）→ 归并 4.1ms（102x）
写法差异:       EXISTS 31.9ms vs JOIN 0.7ms（46x）—— 相关子查询归一不掉
列上表达式:     1779ms vs 1.39ms（1283x）
前导通配:       2.996ms vs 0.007ms（420x）
SELECT *:      0.57ms vs 0.13ms（4.4x，覆盖索引失效）
N+1:           391.7ms（1001 条）vs JOIN 7.6ms（1 条）→ 51x
参数化复用:     312.7ms vs 192.3ms（1.6x，服务器数据库差距更大）
```

### 优化的优先级

```text
① 消灭 N+1（51x）              —— 改一次代码，收益最大
② 让条件能用索引（420~1283x）   —— 别在列上套函数、别用前导 %
③ 检查排序规则匹配             —— 「建了索引还是慢」的隐蔽原因
④ 只取需要的列（4.4x）         —— 顺带打开覆盖索引的可能
⑤ 参数化（1.6x + 安全）        —— 收益不只是性能
```

> ⚠️ **所有优化都应以 `EXPLAIN` 为准**。本章的每个结论都来自实测计划，而不是「据说」。不同数据库、不同版本、不同数据分布下优化器的选择会变——**读计划的能力比记结论更重要**。

---

## 13. 工程实践

| 场景 | ✅ 推荐 | ❌ 避免 | 原因 |
|------|--------|--------|------|
| 拼接用户输入 | 参数化占位符 | 字符串拼接/f-string | 实测拼接泄露 10 万行 |
| 循环里查询 | JOIN 或 IN 批量 | for 里 query | 实测 51x |
| 按日期查 | `WHERE t >= ? AND t < ?` | `WHERE DATE(t) = ?` | 实测列上表达式慢 1283x |
| 模糊搜索 | 前缀 `LIKE 'abc%'` | `LIKE '%abc%'` | 实测慢 420x；全文搜索用 FTS |
| 建索引给 LIKE 用 | `COLLATE NOCASE` 对齐 | 默认 BINARY 索引 | 实测计划仍是 SCAN |
| 取数据 | 列出需要的列 | `SELECT *` | 实测 4.4x + 关闭覆盖索引 |
| 找孤儿数据 | `LEFT JOIN ... IS NULL` | 两次查询在应用里比对 | 一次往返，数据库内部做 |
| 组内排名 | 窗口函数 | GROUP BY + 应用层排序 | GROUP BY 做不到「明细+排名」 |
| 判断空值 | `IS NULL` | `= NULL` | 三值逻辑：`= NULL` 永远是 UNKNOWN |
| 复杂查询 | CTE（`WITH`）分层 | 多层嵌套子查询 | 可读性；但注意物化行为 |
| C# 数据库查询 | 保持 `IQueryable` 到最后 | 过早 `ToList()`/`AsEnumerable()` | 变成全表拉回内存过滤 |
| 排查慢查询 | `EXPLAIN` 看计划 | 猜 / 背口诀 | 本章证伪了「殊途同归」口诀 |

### 一句话决策

```text
写完一个查询问自己三件事:
  ① 它在循环里吗？          → 是 → 改 JOIN/IN
  ② 条件能用上索引吗？      → EXPLAIN 看是 SEARCH 还是 SCAN
  ③ 我真的需要这些列吗？    → 只写需要的，可能白赚一个覆盖索引
```

---

## 14. 最佳实践

- **永远参数化**：它同时买到防注入（实测拼接泄露 10 万行）、语句复用（1.6x）和类型正确——没有任何理由拼字符串。
- **写完就 `EXPLAIN`**：本章证伪了「三种写法殊途同归」这条广为流传的口诀（实测 46x 差距）——**读计划，别背结论**。
- **警惕 for 循环里的查询**：N+1 是 ORM 时代最贵的反模式（实测 51x），且 ORM 的懒加载让它极其隐蔽。
- **不要在索引列上套任何东西**：函数、算术、类型转换都会让索引失效（实测 1283x）；把变换挪到常量一侧。
- **检查索引的排序规则**：`COLLATE` 不匹配时索引形同虚设（实测前缀 LIKE 仍走 SCAN）——「建了索引还是慢」的隐蔽答案。
- **只取需要的列**：`SELECT *` 不只是多搬数据，它**永久关闭了覆盖索引**这个优化（实测 4.4x）。
- **理解三值逻辑**：`= NULL` 永远不成立、`COUNT(列)` 跳过 NULL、`!=` 会漏掉 NULL 行——这三条能避掉大部分「数据对不上」。
- **用 CTE 给复杂查询分层**：`WITH` 是查询里的局部变量，把嵌套变流水线（但注意各数据库的物化行为差异）。

---

## 15. 常见坑

**坑 1 · 字符串拼接 SQL**

```python
cur.execute(f"SELECT * FROM users WHERE name = '{name}'")   # ⚠️ 实测泄露全表 10 万行
```

**避免**：`cur.execute("... WHERE name = ?", (name,))`。

**坑 2 · 在索引列上套函数**

```sql
WHERE DATE(created_at) = '2026-08-11'   -- ⚠️ 索引失效（实测同类写法慢 1283x）
WHERE created_at >= '2026-08-11' AND created_at < '2026-08-12'   -- ✅
```

**坑 3 · `= NULL`**

```sql
WHERE deleted_at = NULL    -- ⚠️ 永远返回 0 行（UNKNOWN 不是 TRUE）
WHERE deleted_at IS NULL   -- ✅
```

**坑 4 · `!=` 漏掉 NULL 行**

```sql
SELECT * FROM t WHERE status != 'done';   -- ⚠️ status 为 NULL 的行【不会】出现
SELECT * FROM t WHERE status IS NULL OR status != 'done';   -- ✅
```

**坑 5 · N+1 查询**

```javascript
for (const u of users) u.orders = await getOrders(u.id);   // ⚠️ 实测 51x（1001 条 SQL）
```

**避免**：一次 JOIN，或 IN 批量（ORM 里叫 eager loading / DataLoader）。

**坑 6 · C# 里过早 `ToList()`**

```csharp
db.Users.ToList().Where(u => u.Score > 90);   // ⚠️ 全表拉回内存再过滤
db.Users.Where(u => u.Score > 90).ToList();   // ✅ 翻译成 SQL 的 WHERE
```

**坑 7 · 以为 `LIMIT` 能让 `OFFSET` 变快**

```sql
SELECT * FROM users ORDER BY id LIMIT 10 OFFSET 1000000;   -- ⚠️ 仍要扫过前 100 万行
SELECT * FROM users WHERE id > ? ORDER BY id LIMIT 10;      -- ✅ 键集分页（第 44 章实测）
```

---

## 16. 面试题

**基础**

1. SQL 的逻辑执行顺序是什么？为什么 `WHERE` 里用不了 `SELECT` 的别名而 `ORDER BY` 可以？
2. `INNER JOIN` 和 `LEFT JOIN` 的区别是什么？怎么用一句 SQL 找出「孤儿」数据？
3. `WHERE` 和 `HAVING` 的区别是什么？（从执行顺序回答）

**中级**

4. **JOIN 有哪三种物理实现？各自的复杂度和适用前提是什么？（本章实测 159x 的来源）**
5. 哪些写法会让索引失效？至少说出四类，并解释原因。
6. **什么是 N+1 查询？「N+1 → IN → JOIN」三级台阶各自的适用场景和限制？**

**高级**

7. **「`IN`、`EXISTS`、`JOIN` 优化器都会归一」这个说法对吗？用相关子查询的概念解释实测的 46x 差距。**
8. 什么是覆盖索引？为什么 `SELECT *` 永远享受不到它？
9. C# 的表达式树为什么能转译成 SQL 而 Java 的 lambda 不能？这个差异带来了什么工程后果？

---

## 17. 练习

**基础**

1. 用 `EXPLAIN QUERY PLAN` 观察一个查询在建索引前后的计划变化，确认 `SCAN → SEARCH`。
2. 写出「每个城市分数最高的用户」的两种实现：GROUP BY 版和窗口函数版，比较它们能回答的问题范围。
3. 构造一个 `!=` 漏掉 NULL 行的例子，并修正它。

**中级**

4. **复现钥匙实验一**：用你熟悉的语言实现嵌套循环和哈希两种 JOIN，测出加速比。
5. 复现「殊途同归证伪」：写出同一问题的 `IN`/`EXISTS`/`JOIN` 三版，用 `EXPLAIN` 对比计划并计时。
6. 找出你项目里的一个 N+1 查询，改成 JOIN，测量前后差距。

**挑战**

7. **实现 Index Nested Loop Join**：给内表建一个 B 树（或用 `std::map`），测出它相对全表嵌套循环的加速比，并找出「结果集多大时哈希连接反超」的临界点。
8. 用 `COLLATE NOCASE` 与默认 BINARY 索引各建一次，用 `EXPLAIN` 验证前缀 `LIKE` 的计划差异（本章实测的隐蔽坑）。
9. 写一个窗口函数查询求「每个城市消费额前三的用户」，并用 CTE 分层让它可读。

---

## 18. 本章总结

**一句话**：SQL 是六门语言里唯一的声明式语言——你写「要什么」，优化器决定「怎么找」，而本章的钥匙实验量化了这个交易的价值：亲手实现的 JOIN 三种物理算法在同一份数据上跑出 **414 ms / 2.6 ms（159x）/ 4.1 ms（102x）**，结果完全一致而 SQL 写法只有一种（对照 Java 的 146x、C# 的 342x：命令式语言里选错算法的代价由你承担）；但声明式不是魔法，本章还**证伪了「`IN`/`EXISTS`/`JOIN` 殊途同归」这条流传甚广的口诀**——实测 `EXISTS` 比 `JOIN` 慢 **46 倍**，`EXPLAIN` 指出根因是**相关子查询**必须逐行求值（`CORRELATED SCALAR SUBQUERY`），这是逻辑上的硬边界；同时实测了四类让优化器缴械的写法（列上表达式 **1283x**、前导通配 **420x**、**排序规则不匹配导致索引形同虚设**、`SELECT *` 关闭覆盖索引 4.4x）与一个每天都在发生的反模式（**N+1 查询 51x**）——**结论不是背口诀，而是读 `EXPLAIN`**。

**关键要点**

- **声明式的本质**：把算法选择权从程序员转移给能看见统计信息的优化器（JOIN 三算法实测 159x）。
- **逻辑执行顺序**：`FROM → WHERE → GROUP BY → HAVING → SELECT → ORDER BY → LIMIT`——别名规则由此推出。
- **殊途同归是误解**（实测 46x）：优化器归一不掉「相关性」，相关子查询必须逐行求值。
- **四类缴械写法**：列上表达式 1283x、前导 `%` 420x、排序规则不匹配、`SELECT *` 关闭覆盖索引。
- **N+1 三级台阶**（实测 51x）：N+1 → IN 批量（有参数上限）→ JOIN（最优）。
- **参数化买三样**：防注入（实测拼接泄露 10 万行）+ 语句复用（1.6x）+ 类型正确。
- **三值逻辑**：`= NULL` 永假、`COUNT(列)` 跳过 NULL、`!=` 漏掉 NULL 行。
- **C# 表达式树**是唯一能把查询转译回 SQL 的机制；Java lambda 无法内省，Stream 只能在内存跑。

**自查清单**

- [ ] 我能说出 JOIN 的三种物理实现及各自前提。
- [ ] 我能背出逻辑执行顺序并用它解释别名规则。
- [ ] 我能说出至少四类让索引失效的写法。
- [ ] 我能识别 N+1 并用三级台阶改写它。
- [ ] 我会用 `EXPLAIN` 验证结论，而不是背口诀。

**下一章预告**：本章讲了怎么问，第 48 章讲**事务**——怎么保证「问和改」在并发下依然正确。第 46 章实测过一次转账崩在半路的后果，那只是 ACID 里的 A；第 48 章会把四个字母全部拆开：用实测复现**脏读、不可重复读、幻读**三种异常，量化四个隔离级别各自挡住了哪些、放过了哪些，并解释为什么 PostgreSQL 的「可重复读」和 MySQL 的「可重复读」是**两种不同的东西**——以及 MVCC 如何让读写互不阻塞。

---

## 19. 延伸阅读

- <a href="https://www.sqlite.org/optoverview.html" target="_blank" rel="noopener">SQLite · Query Optimizer Overview</a> —— 优化器做了哪些重写，本章实测计划的官方解释。
- <a href="https://www.sqlite.org/eqp.html" target="_blank" rel="noopener">SQLite · EXPLAIN QUERY PLAN</a> —— 怎么读 `SCAN`/`SEARCH`/`COVERING INDEX` 这些词。
- <a href="https://use-the-index-luke.com/" target="_blank" rel="noopener">Use The Index, Luke!</a> —— 索引与 SQL 性能最好的免费教程，本章「缴械写法」的系统版。
- <a href="https://www.postgresql.org/docs/current/using-explain.html" target="_blank" rel="noopener">PostgreSQL Docs · Using EXPLAIN</a> —— 服务器数据库的执行计划详解。
- <a href="https://en.wikipedia.org/wiki/Hash_join" target="_blank" rel="noopener">Wikipedia · Hash join</a> —— 本章钥匙实验中哈希连接的理论背景。
- <a href="https://en.wikipedia.org/wiki/Relational_algebra" target="_blank" rel="noopener">Wikipedia · Relational algebra</a> —— SQL 背后的数学，`JOIN`/`SELECT` 的形式定义。
- <a href="https://learn.microsoft.com/en-us/dotnet/csharp/linq/" target="_blank" rel="noopener">Microsoft Learn · LINQ</a> —— 查询语法、方法语法与表达式树的官方文档。
- <a href="https://docs.oracle.com/javase/8/docs/api/java/util/stream/package-summary.html" target="_blank" rel="noopener">Java Docs · java.util.stream</a> —— Stream 的官方说明，含惰性求值语义。
- <a href="https://owasp.org/www-community/attacks/SQL_Injection" target="_blank" rel="noopener">OWASP · SQL Injection</a> —— 注入攻击的完整分类与防御。
