# 第 51 章 · ORM

**简体中文** ｜ [English](./51-orm.en-US.md)

---

> 前五章讲的全是数据库怎么想问题——表、行、SQL、事务、锁。但你的代码想的是**对象**——类、字段、集合、继承。这两套世界观对不上的地方有个专门的名字：**阻抗失配**（impedance mismatch）。ORM 就是这道鸿沟上的翻译器。
>
> 本章的**钥匙实验**分三处落地：Java **用反射手写了一个 ORM**（`@Table`/`@Column` 注解 + `Field.set()`，实测生成出 `INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')`，而实体类上**没有一行映射代码**）；C# **手写了表达式树 → SQL 的翻译器**（实测把 `u => u.Score > 90 && u.City == "city-3"` 翻译成 `WHERE ((Score > 90) AND (City = 'city-3'))`，一百行就是 EF Core 的核心）；Python **用真实 SQL 计数抓住了 N+1 的诞生现场**。
>
> 那个诞生现场值得单独说：源码里只是 `for u in users: sum(o.amount for o in u.orders)`——**一个 for 循环加一次属性访问**，实测却发出了 **201 条 SQL**；改成一条 JOIN 是 1 条，**SQL 条数少 201 倍，耗时快 8.9 倍**。JS 版复现了同一个现象（301 条 vs 1 条，快 8.0 倍）。**N+1 最阴险的地方在于它在源码里完全看不出来**——因为 ORM 把「数据库访问」伪装成了「内存访问」，而这两者差六个数量级。
>
> 抽象漏出来的另一刻是**延迟加载**：JS 实测会话内访问 `u.orders` 拿到 20 条订单，会话关闭后再访问就抛异常——这就是 Hibernate 的 `LazyInitializationException`。根因是**对象看起来是个普通对象，实际上它还连着数据库**，一旦离开事务边界（返回视图层、放进缓存、序列化成 JSON），关联就取不到了。
>
> C# 版还量化了那个著名的 `IQueryable`/`IEnumerable` 陷阱。用内存集合根本测不出来（那里没有「传输」这一步），所以实验模拟了一个真实数据源：**过滤在服务端只传输 5 行、3.1 ms；先 `.ToList()` 再过滤传输了 100000 行、266.3 ms——多传输两万倍，慢 86 倍**。一个方法调用的位置，差了两个数量级。
>
> 最后是一个语言层面的发现：**C++ 没有主流 ORM，因为它没有运行时反射**（第 30 章的那个缺口）。Java 能 `for (Field f : User.class.getFields())` 直接问类要字段，C++ 的类**不知道自己有哪些字段**——所以 C++ ORM 只能靠宏、模板或外部代码生成器。一条规律浮现出来：**运行时越能自省的语言，ORM 越强大也越流行**。

## 1. 学习目标

学完本章，你将能够：

- 列出**阻抗失配**的五个具体表现（身份、关联、集合、继承、加载时机），并说清 ORM 为每一项做了什么；
- 解释 **N+1 查询如何从一次属性访问里冒出来**（实测 201 条 SQL），并用三种手段消除它；
- 说清**延迟加载**为什么会在事务外失效，以及现代 ORM 用什么方案取代它；
- 判断一个 ORM 的查询是在**数据库端**还是**内存端**执行（实测差 86 倍）；
- 解释**为什么不同语言的 ORM 走了不同技术路线**，以及 C++ 为何几乎没有 ORM。

---

## 2. 为什么会出现这个概念

### 两套世界观

```text
你的代码想的是: 类、对象、字段、List<T>、继承、引用
数据库想的是:   表、行、列、外键、JOIN、主键
→ 这两套模型【不是同一件事的两种说法】，而是真的对不上
```

**对不上的五个具体地方**（本章 Python 版把它做成了一张表）：

| 概念 | 对象世界 | 关系世界 |
|------|---------|---------|
| 身份 | 引用相等（`is`） | 主键 |
| 关联 | 直接持有引用 | 外键 + JOIN |
| 集合 | `List<Order>` | 另一张表的多行 |
| 继承 | 天然支持 | **没有这个概念** |
| 加载时机 | 全部在内存 | 按需查询（延迟 or 预加载） |

### 没有 ORM 时你要写什么

**Java 版把手写代码摆了出来**：

```java
PreparedStatement ps = conn.prepareStatement(
    "INSERT INTO users (id, name, city_name) VALUES (?, ?, ?)");
ps.setInt(1, user.id);
ps.setString(2, user.name);
ps.setString(3, user.city);
ps.executeUpdate();
// 查询侧还要再写一遍【反向】映射:
User u = new User();
u.id = rs.getInt("id");
u.name = rs.getString("name");
u.city = rs.getString("city_name");
```

```text
手写: 11 行，且【每个实体、每个方向都要重写一遍】
ORM : orm.save(user) / orm.find(User.class, 1) —— 2 行，对所有实体通用
→ 10 个实体 × 增删改查 4 个方向 ≈ 手写 220 行样板 vs ORM 0 行
```

> **一句话总结**：ORM 消灭的是**与业务无关的重复代码**——但它是通过**把数据库访问伪装成内存访问**做到的，而这个伪装恰恰是它所有坑的来源（本章会逐个量出来）。

---

## 3. 底层原理

### 钥匙实验一：反射如何生成 SQL（Java）

**实体类上只有注解，没有一行映射代码**：

```java
@Table("users")
public static class User {
    @Id @Column public Integer id;
    @Column public String name;
    @Column("city_name") public String city;      // 字段名与列名不同
    public String notPersisted = "不带 @Column 的字段不入库";
}
```

**ORM 用反射读出映射**（实测输出）：

```text
表名: users
主键字段: id
列 id         ← 字段 id     (Integer)
列 name       ← 字段 name   (String)
列 city_name  ← 字段 city   (String)
⚠️ notPersisted 字段没有 @Column，所以【不在映射里】
```

**据此生成 SQL**：

```java
for (Field f : c.getFields()) {
    Column col = f.getAnnotation(Column.class);
    if (col == null) continue;                       // 没标注的字段不入库
    cols.put(col.value().isEmpty() ? f.getName() : col.value(), f);
}
```

```text
你写的代码: orm.save(user)
ORM 生成的: INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')

你写的代码: orm.find(User.class, 1)
ORM 生成的: SELECT id, name, city_name FROM users WHERE id = 1
还原出的对象: User{id=1, name='张三', city='北京'}
```

**这就是第 30 章反射最实际的用途**：运行时读出类型结构，据此生成 SQL 并把行还原成对象。

### 钥匙实验二：表达式树如何变成 SQL（C#）

**一百行的翻译器**（实测输出）：

```text
你写的 LINQ                                  →  ORM 生成的 SQL
u => u.Score > 90                            → WHERE (Score > 90)
u => u.City == "city-3"                      → WHERE (City = 'city-3')
u => u.Score > 90 && u.City == "city-3"      → WHERE ((Score > 90) AND (City = 'city-3'))
u => u.Name.StartsWith("user-1")             → WHERE Name LIKE 'user-1%'
```

**核心就是一个模式匹配的递归**：

```csharp
public static string Translate(Expression expr) => expr switch
{
    BinaryExpression b => $"({Translate(b.Left)} {Op(b.NodeType)} {Translate(b.Right)})",
    MemberExpression m when m.Expression is ParameterExpression => m.Member.Name,   // u.Score → Score
    ConstantExpression c => c.Value is string s ? $"'{s}'" : c.Value?.ToString(),
    ...
};
```

**关键在于「读」而不是「执行」**——第 47 章讲过它的前提：只有 `Expression<Func<>>` 保留了语法结构，`Func<>` 编译成 IL 后就无法内省了。

**翻译不了时会发生什么**（实测）：

```text
尝试翻译 u => MyCustomCheck(u.Name) ... → ✗ 抛出异常: 翻译不了: Call

真实 ORM 有两种反应:
  EF Core 3.0+ : 直接【报错】——逼你自己决定怎么办（好设计）
  EF Core 2.x  : 【静默降级】为客户端求值 = 悄悄把全表拉回内存
  → 后者制造过无数生产事故，所以 3.0 把它改成了默认报错
```

### 钥匙实验三：N+1 的诞生现场（Python）

**源码看起来完全正常**：

```python
for u in s.query(User):                        # 1 条 SQL 查所有用户
    total += sum(o.amount for o in u.orders)   # ← 每个用户【又一条】SQL
```

**而 `u.orders` 只是一个属性**：

```python
@property
def orders(self):
    """⚠️ 看起来只是一次属性访问，实际上是【一条 SQL】"""
    return self._session.query(Order, where=f"user_id = {self.id}")
```

**用 sqlite 的 trace 回调数出真实执行的 SQL**（实测）：

```text
→ 实际执行了 201 条 SQL，耗时 17.6 ms
前 3 条 SQL:
  SELECT id, name, city FROM users WHERE 1=1
  SELECT id, user_id, amount FROM orders WHERE user_id = 0
  SELECT id, user_id, amount FROM orders WHERE user_id = 1
```

**改成一条 JOIN**：

```text
→ 实际执行了 1 条 SQL，耗时 2.0 ms
→ SQL 条数少 201 倍，耗时快 8.9x，结果一致: True
```

**JS 版复现了同一现象**：301 条 vs 1 条，快 8.0 倍。

```text
⚠️ N+1 最阴险的地方: 它在源码里【完全看不出来】
根因: ORM 把「数据库访问」伪装成了「内存访问」
     而内存访问是纳秒级的，数据库访问是毫秒级的 —— 伪装掉的正是这六个数量级
```

### 身份映射与脏检查：ORM 的另外两件核心工作

**身份映射**（实测）：

```text
两次查询同一行: a is b = True（第二次没有新建对象）
改 a 之后 b.name = '改过的名字'   ← 因为它们【就是同一个对象】
→ 没有身份映射会怎样: 同一行加载出两个对象，改了一个另一个还是旧值
→ 这解决的是阻抗失配的第一项: 数据库有【主键】，对象有【引用相等】
```

**脏检查**（实测，快照法）：

```text
当前脏对象数: 1
  User(id=1).name: 'user-1' → '改过的名字'
生成的 SQL: ["UPDATE users SET name = '改过的名字' WHERE id = 1"]
→ 只更新【真正变了的列】，而不是把整行 UPDATE 一遍
→ 实现方式: 加载时存一份快照，提交时逐字段比对（本例 5 行代码）
```

**两种实现路线**：

```text
快照法（EF Core 默认 / Hibernate）: 加载时存副本，提交时比对
  ✓ 简单可靠   ✗ 每个实体一份内存副本（C# 实测 5 万个实体的快照成本）
代理法（Change Proxies）: 生成子类拦截 setter，改了就标记
  ✓ 省内存     ✗ 属性必须是 virtual，且对象不再是你的类型
→ 大批量只读查询要关掉跟踪: EF 的 .AsNoTracking()
```

---

## 4. JavaScript

JS 版抓住了 ORM 抽象**漏出来的那一刻**。

### 延迟加载：属性访问背后藏着一条 SQL（实测）

```javascript
return {
  ...row,
  // ⚠️ 看起来是个普通属性，实际每次访问都发一条 SQL
  get orders() { return session.run('SELECT amount FROM orders WHERE user_id = ?', row.id); },
};
```

```text
for (const u of users) u.orders...  → 发出 301 条 SQL，耗时 29.2 ms
一条 JOIN → 1 条 SQL，耗时 3.7 ms
→ SQL 条数少 301 倍，耗时快 8.0x
```

### 抽象漏出来的那一刻（实测）

```text
在会话内访问 u.orders: ✓ 拿到 20 条订单
会话关闭后再访问: ✗ 会话已关闭
→ 这就是 Hibernate 的 LazyInitializationException / Django 的相关异常
```

```text
根因: 对象【看起来】是个普通对象，实际上它还【连着数据库】
     一旦离开事务边界（返回给视图层、放进缓存、序列化成 JSON），关联就取不到了
```

**这是 ORM 最典型的「抽象泄漏」**：它努力让你相信这只是个内存对象，直到某一刻它突然不是了。

### 于是产生了两难

```text
用延迟加载: 省内存，但离开事务就炸，且极易 N+1
用预加载  : 安全，但可能把用不到的数据也捞回来（over-fetching）
→ 现代方案是【显式声明】: Prisma 的 include、EF 的 .Include()、JPA 的 fetch join
   把「要不要加载关联」变成代码里【看得见的一行】，而不是隐式的属性访问
```

### over-fetching 的代价（实测）

```text
取 5 列: 3.0 ms（6000 行）
取 2 列: 1.1 ms（6000 行，快 2.7x）
→ ORM 默认「取整个实体」= 永远是 SELECT *（第 47 章实测它关闭了覆盖索引）
→ 列表页只需两列时，用投影查询（Prisma 的 select、EF 的 .Select()）而不是取实体
```

> **注意**：JS 既没有运行时反射也没有标准注解，所以主流方案都靠**代码生成**（Prisma 的 schema 文件、TypeORM 的实验性装饰器）；这与 C++ 的 ODB 是同一个思路——**语言不给的，就用外部工具生成**；Knex 是纯查询构建器，不做对象映射，是「不想要 ORM 但想要类型安全」时的选择。

---

## 5. Python

Python 版是 N+1 实测的主战场，也把 ORM 的三件核心工作（映射、身份映射、脏检查）各实现了一遍。

### 用真实 SQL 计数（实验设计的关键）

```python
con.set_trace_callback(lambda s: QUERY_LOG.append(s))   # ← 装上「SQL 计数器」
```

**没有这个回调，N+1 就只能靠推理**；有了它，每一条真实执行的 SQL 都被记下来，**201 这个数字是数出来的而不是算出来的**。

### 微型 ORM 的三件事

```python
class Session:
    def query(self, cls, where="1=1"):
        sql = f"SELECT {', '.join(cls.__fields__)} FROM {cls.__table__} WHERE {where}"
        for row in self.con.execute(sql):
            key = (cls, row[0])
            if key in self.identity_map:            # ① 身份映射: 复用同一个对象
                out.append(self.identity_map[key]); continue
            obj = cls._from_row(row, self)          # ② 映射: 行 → 对象
            self.snapshots[id(obj)] = {...}         # ③ 脏检查: 存一份快照
```

**40 行左右就把 ORM 的骨架搭出来了**——真实 ORM 大得多，是因为还要处理类型转换、关联、继承、迁移、方言差异。

### 阻抗失配的五个表现（实测输出的总表）

```text
┌────────────┬──────────────────┬──────────────────────┐
│ 概念        │ 对象世界          │ 关系世界              │
├────────────┼──────────────────┼──────────────────────┤
│ 身份        │ 引用相等(is)      │ 主键                  │
│ 关联        │ 直接持有引用      │ 外键 + JOIN           │
│ 集合        │ List<Order>      │ 另一张表的多行         │
│ 继承        │ 天然支持          │ 【没有这个概念】       │
│ 加载时机    │ 全部在内存        │ 按需查询（延迟 or 预加载）│
└────────────┴──────────────────┴──────────────────────┘
→ ORM 就是这张表的翻译器
→ 它省下的每一行代码，都对应上面某一行的手工处理
→ 而它的每个坑（N+1、延迟加载失效、继承映射），也都来自同一张表
```

> **注意**：SQLAlchemy 靠**描述符 + 元类**拦截属性访问（本例的 `@property` 是它的简化版）；Django ORM 的 `select_related`（JOIN）和 `prefetch_related`（IN 批量）分别对应第 47 章「N+1 → IN → JOIN」三级台阶的后两级；`session.query()` 在 SQLAlchemy 2.0 已改为 `session.execute(select(...))`。

---

## 6. Java

Java 版手写了一个反射 ORM——**JPA/Hibernate 的骨架**。

### 反射：ORM 最核心的依赖

```java
static Map<String, Field> columnsOf(Class<?> c) {
    Map<String, Field> cols = new LinkedHashMap<>();
    for (Field f : c.getFields()) {                      // ← 运行时【问类要字段】
        Column col = f.getAnnotation(Column.class);       // ← 运行时读注解
        if (col == null) continue;
        cols.put(col.value().isEmpty() ? f.getName() : col.value(), f);
    }
    return cols;
}
```

**这几行是 Java ORM 与 C++ 的分水岭**：类**知道自己长什么样**，所以映射可以自动获得。

### 省下多少代码（实测）

```text
手写: 11 行，且每个实体、每个方向都要重写一遍
ORM : 2 行，对所有实体通用
→ 10 个实体 × 4 个方向 ≈ 手写 220 行样板 vs ORM 0 行
→ 这才是 ORM 被广泛采用的真正原因: 消灭【与业务无关的重复代码】
```

### 但 ORM 带来了三个新问题

```text
① N+1 查询: 一次属性访问 = 一条 SQL（Python 版实测 201 条）
② 延迟加载失效: session 关了之后再访问关联 → LazyInitializationException（JS 版实测）
③ 生成的 SQL 不可见: 你写的是对象操作，执行的是你没看过的 SQL
→ 三个问题的根源是同一个: ORM 把【数据访问】伪装成了【内存访问】
   而内存访问是纳秒级的，数据库访问是毫秒级的——伪装掉的正是这六个数量级
```

### Java ORM 生态的光谱

```text
JPA       : 规范（jakarta.persistence 注解，本例模仿的就是它）
Hibernate : JPA 最主流的实现；@Entity/@Table/@Column/@OneToMany
MyBatis   : 「半 ORM」——SQL 你自己写，只帮你做结果映射
jOOQ      : 反过来——用 Java 代码写类型安全的 SQL（完全不隐藏 SQL）
→ 光谱的两端: Hibernate 隐藏 SQL 最多，jOOQ 完全不隐藏
→ 选型的实质是: 你愿意让多少 SQL 细节【不出现在代码里】
```

> **注意**：Hibernate 的实体默认要求无参构造器和非 final 类（因为要生成代理子类）；`@OneToMany` 默认是 `LAZY`，`@ManyToOne` 默认是 `EAGER`——**这个不对称是 N+1 的常见来源**；`fetch join`（`JOIN FETCH`）是 JPQL 里显式预加载的写法。

---

## 7. C++

C++ 版回答了一个语言层面的问题：**为什么 C++ 没有主流 ORM**。

### 答案：第 30 章的那个缺口

```text
Java : for (Field f : User.class.getFields())  ← 运行时【问类要字段】
       @Column 注解也是运行时读出来的 —— 类自己知道自己长什么样
C++  : 类【不知道】自己有哪些字段
       映射必须【你手写】，加一个字段编译器不会提醒你漏了同步
```

**C++ 能做到的是编译期映射**（实测生成的 SQL 与 Java 版一模一样）：

```cpp
static auto meta() {
    return std::make_tuple(
        std::make_tuple("id",        &User::id),
        std::make_tuple("name",      &User::name),
        std::make_tuple("city_name", &User::city));
}
```

```text
select_sql<User>(): SELECT id, name, city_name FROM users
insert_sql(u):      INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')
→ SQL 一样，但【获取映射的方式】完全不同: 这个 meta() 是你手写的
```

### 于是 C++ 的「ORM」分成三条路

```text
① 宏 / 模板元编程（本例）
   sqlpp11、sqlite_orm —— 编译期类型安全，SQL 错误在编译期就报
   代价: 报错信息动辄几百行模板展开，且映射要手写
② 外部代码生成
   ODB —— 用一个【预处理器】扫描头文件，生成映射代码
   代价: 构建流程里多一个工具，等于自己造了一套反射
③ 干脆不用 ORM
   直接写 SQL + 手工映射 —— C++ 项目最常见的选择
→ 三条路都在绕过同一个缺口: 没有反射，映射信息就得【从别处来】
```

### 编译期映射反而有一个优势

```text
Java/Python 的 ORM: 列名写错 → 【运行时】才报错（甚至上线后才发现）
C++ 的模板 ORM:     字段类型对不上 → 【编译期】就报错
→ sqlpp11 甚至能在编译期检查「你 SELECT 的列是否存在于这张表」
→ 这是零开销哲学的又一次体现: 能在编译期做的，绝不留到运行时
→ 但代价是灵活性: 运行时才知道表结构的场景（通用管理后台）它完全做不了
```

### C++26 的静态反射会改变什么

```text
P2996 静态反射已进入 C++26 —— 能在【编译期】遍历类的成员
→ 上面的 meta() 将可以自动生成，宏可以退休
→ 但它仍是【静态】反射: 编译期可见，运行时依然没有类型信息
→ 所以 C++ 会得到更好的编译期 ORM，但不会有 Hibernate 那样的运行时 ORM
```

> **注意**：本例用 C++17 折叠表达式遍历元数据元组；`sqlite_orm` 是 header-only 的现代选择，映射写成一个 `make_storage(...)` 表达式；ODB 的预处理器要解析 C++ 头文件，因此对模板和新标准的支持总是滞后。

---

## 8. C#

C# 版量化了那个著名的 `IQueryable`/`IEnumerable` 陷阱——**并先说明了为什么内存集合测不出来**。

### 实验设计：必须模拟「传输成本」

```text
⚠️ 用内存集合测不出这个差别（那里没有「传输」这一步，IQueryable 只有开销）
→ 所以模拟一个【真实数据源】: 每返回一行都要付传输 + 反序列化的成本
```

```csharp
IEnumerable<User> Materialize(Func<User, bool>? serverFilter)
{
    foreach (var r in _rows)
    {
        if (serverFilter != null && !serverFilter(r)) continue;   // 服务端过滤掉，不传输
        RowsTransferred++;
        Thread.SpinWait(100);                                      // 传输 + 反序列化的成本
        yield return r;
    }
}
```

### 实测结果

```text
.Where(...).ToList()   ← 过滤在【服务端】:    3.1 ms，传输      5 行
.ToList().Where(...)   ← 过滤在【客户端】:  266.3 ms，传输 100000 行
结果一致: True；慢 86x，多传输 20000x 的行
```

```text
→ 一个 .ToList() 放错位置，就把「数据库过滤」变成了「全表拉回内存过滤」
→ 第 46 章实测过真实数据库上的同一笔账: 109 ms + 46 MB 堆 vs 9 μs
```

**这个陷阱之所以危险，是因为两种写法在源码里几乎一模一样**——只是 `.ToList()` 的位置差了一个方法调用。

### 变更跟踪的成本（实测）

```text
实测快照成本: 为 50000 个实体各存一份副本 = 若干毫秒
→ 这就是「查一万行只为了展示，却慢得莫名其妙」的常见原因
→ 只读查询加 .AsNoTracking()（EF）/ 用投影查询代替取实体
```

### 五语言的 ORM 与它们依赖的语言特性

```text
C#     : EF Core —— 靠【表达式树】把 LINQ 翻译成 SQL（本章 C# 版实现的就是它）
Java   : Hibernate/JPA —— 靠【注解 + 运行时反射】（本章 Java 版实现的就是它）
Python : SQLAlchemy —— 靠【描述符 + 元类】拦截属性访问（本章 Python 版实现的就是它）
JS     : Prisma/TypeORM —— 靠【代码生成 + 装饰器】
C++    : ❌ 没有主流 ORM —— 因为它【没有运行时反射】
→ 三种技术路线，对应三种语言特性；哪种特性最强，哪家的 ORM 就最自然
```

> **注意**：EF Core 3.0 起把「无法翻译的表达式」从静默客户端求值改成**直接报错**，这是一次重要的设计修正；`FromSqlRaw` 支持插值字符串并自动参数化（第 47 章提过，是少数拼接安全的场景）；`.Include()` 的链式深度过大时会产生笛卡尔积膨胀，改用 `AsSplitQuery()`。

---

## 9. SQL

本节从数据库一侧看阻抗失配——尤其是**关系模型里根本没有的那个概念：继承**。

### 继承的三种映射策略（实测）

**策略 A：单表继承**（所有子类挤一张表 + 鉴别列）

```sql
CREATE TABLE payment_single (
  id INTEGER PRIMARY KEY, kind TEXT NOT NULL, amount INTEGER NOT NULL,
  card_no TEXT,          -- 只有 CardPayment 用
  bank TEXT              -- 只有 WirePayment 用
);
```

```text
✓ 查询最快（无 JOIN）
✗ 子类专有列必须可空 → 【约束失效】（无法要求 CardPayment 的卡号非空）
```

**策略 B：连接继承**（父表 + 每个子类一张表）

```text
✓ 每列都能 NOT NULL（约束保住了）
✗ 每次查询都要 JOIN
```

**策略 C：每类一表**（完全独立）

```text
✓ 最简单、约束最强
✗ 查「所有支付」要 UNION ALL，且主键要跨表唯一
```

```text
→ 三种策略没有赢家: ORM 让你选，而【选错了很难改】——数据已经落在那个形状里了
```

### 关联与多对多

```text
一对多: 对象里是 author.books（一个 List），数据库里是 book.author_id（一个外键）
       → 对象里「读一个字段」，数据库里是「跑一次查询」——差六个数量级
       → N+1 就诞生在这个落差里

多对多: 对象里 student.courses + course.students，数据库里【多一张中间表】
       → 中间表在对象模型里【不存在】——这是最典型的阻抗失配
       → 一旦中间表需要额外字段（如选课时间），就必须把它提升为一个实体
```

### 集合语义与类型也对不上

```text
集合: List<T> 有顺序、允许重复；Set<T> 无序不重复
     表的行【本身没有顺序】（没有 ORDER BY 时顺序不保证，第 47 章）
     → 想保住顺序就得加一列 position，ORM 再帮你维护它

类型: 枚举 → 存 TEXT 还是 INTEGER？值对象 Money{amount, currency} → 拆两列还是塞 JSON？
     NULL → 对象世界有 Optional<T>，关系世界是三值逻辑（第 47 章）
     → ORM 的「类型转换器」(AttributeConverter / ValueConverter) 就是为这些而生
```

> **注意**：JPA 用 `@Inheritance(strategy = ...)` 选择三种策略；单表继承在 Hibernate 里是默认（`SINGLE_TABLE`）；中间表需要额外字段时，JPA 的做法是把它建模成一个带 `@EmbeddedId` 的独立实体。

---

## 10. 五语言横向对比

### ① ORM 技术路线对照

| 语言 | 靠什么获得映射 | 代表 | 本章实现了什么 |
|------|--------------|------|--------------|
| **Java** | 注解 + **运行时反射** | Hibernate / JPA | 反射 ORM（`@Table`/`@Column` + `Field.set()`） |
| **C#** | **表达式树** + 反射 | EF Core | 表达式树 → SQL 翻译器 |
| **Python** | 描述符 + 元类 | SQLAlchemy / Django ORM | 微型 ORM（映射 + 身份映射 + 脏检查） |
| **JavaScript** | 装饰器 + **代码生成** | Prisma / TypeORM | 延迟加载与会话失效 |
| **C++** | 宏 / 外部代码生成 | sqlpp11 / ODB（小众） | 编译期映射 + 为何没有主流 ORM |

```text
→ 一个规律: 【运行时越能自省的语言，ORM 越强大也越流行】
→ 反过来也成立: C++ 的 ORM 最弱，恰恰因为它把一切都交给了编译期
```

### ② 钥匙实验一：反射生成 SQL（Java 实测）

```text
实体类只有注解，没有一行映射代码
→ 反射读出: 表 users；列 id/name/city_name ← 字段 id/name/city
→ 生成: INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')
→ 手写 11 行 vs ORM 2 行；10 个实体 × 4 个方向 ≈ 省 220 行样板
```

### ③ 钥匙实验二：表达式树 → SQL（C# 实测）

```text
u => u.Score > 90 && u.City == "city-3"  →  WHERE ((Score > 90) AND (City = 'city-3'))
u => u.Name.StartsWith("user-1")         →  WHERE Name LIKE 'user-1%'
翻译不了时: 抛出异常（EF Core 3.0+ 的行为；2.x 会静默降级为客户端求值）
```

### ④ 钥匙实验三：N+1 的诞生现场（Python / JS 实测）

```text
Python: 201 条 SQL / 17.6 ms  vs  一条 JOIN 1 条 / 2.0 ms → 少 201 倍，快 8.9x
JS:     301 条 SQL / 29.2 ms  vs  一条 JOIN 1 条 / 3.7 ms → 少 301 倍，快 8.0x
→ 源码里只是「一个 for 循环 + 一次属性访问」
```

### ⑤ 钥匙实验四：查询在哪一端执行（C# 实测）

```text
.Where(...).ToList()  → 传输     5 行，  3.1 ms
.ToList().Where(...)  → 传输 100000 行，266.3 ms
→ 多传输 20000x，慢 86x —— 差别只在 .ToList() 的位置
```

### ⑥ 共性与根因

**共性**：所有 ORM 都要做同样的三件事——**映射**（类↔表）、**身份映射**（主键↔引用相等）、**变更跟踪**（对象改动↔UPDATE）；所有 ORM 都有 N+1 问题，因为它们都把关联做成了「属性」；所有 ORM 都提供「预加载」开关，因为延迟加载的默认行为在生产中不够用。

**根因**：

- **技术路线由语言特性决定**：Java 有运行时反射就用反射，C# 有表达式树就做查询翻译，Python 有描述符就拦截属性，JS 两者都没有就靠代码生成，C++ 什么都没有所以几乎没有 ORM；
- **N+1 的根源是抽象本身**：ORM 的价值在于「让数据库访问看起来像内存访问」，而这个价值恰恰制造了 N+1——**同一个特性既是卖点也是陷阱**；
- **延迟加载失效的根源是生命周期不匹配**：对象的生命周期由 GC 决定，数据库连接的生命周期由事务决定，两者一旦错位就出问题（第 45 章：事务期间连接不能还给池）；
- **继承没有赢家的策略**，是因为关系模型里根本没有继承这个概念——这是唯一一个**无法被翻译得令人满意**的阻抗失配。

---

## 11. 底层实现对比

| ORM | 映射机制 | 变更跟踪 | 查询翻译 |
|-----|---------|---------|---------|
| **Hibernate** | 注解 + 运行时反射 | 快照法（默认）+ 字节码增强可选 | HQL/JPQL → SQL；Criteria API |
| **EF Core** | 特性 + `DbContext` 配置 | 快照法；`AsNoTracking()` 关闭 | **表达式树 → SQL**（本章实现的核心） |
| **SQLAlchemy** | 描述符 + 元类 | Unit of Work + 属性拦截 | Python 对象构造查询树 |
| **Django ORM** | 元类扫描字段 | 字段级 `has_changed` | QuerySet 惰性求值 |
| **Prisma** | schema 文件 → **代码生成** | 显式 update（不做隐式跟踪） | 生成的客户端直接产 SQL |

**Prisma 的选择值得注意**：

```text
它【不做隐式变更跟踪】—— 你必须显式调用 update({ where, data })
→ 代价: 少了「改对象就自动持久化」的便利
→ 收益: 没有「我改了什么它就写什么」的不确定性，生成的 SQL 完全可预测
→ 这是新一代 ORM 的共同倾向: 【少一点魔法，多一点显式】
```

---

## 12. 性能分析

### 本章实测数字速查

```text
N+1（Python）:  201 条 SQL / 17.6 ms  vs  1 条 / 2.0 ms → 少 201 倍，快 8.9x
N+1（JS）:      301 条 SQL / 29.2 ms  vs  1 条 / 3.7 ms → 少 301 倍，快 8.0x
查询端（C#）:   服务端过滤 5 行 / 3.1 ms vs 客户端过滤 100000 行 / 266.3 ms → 慢 86x
over-fetching:  取 5 列 3.0 ms vs 取 2 列 1.1 ms → 2.7x
样板代码（Java）: 手写 11 行/实体/方向 vs ORM 2 行通用
```

### ORM 的三笔性能账

```text
① 查询条数: N+1 是最贵的一笔（实测 8~9x，第 47 章实测网络数据库上达 51x）
② 传输数据量: 取整个实体 = SELECT *（第 47 章实测它关闭覆盖索引）
③ 内存与 CPU: 变更跟踪的快照、代理对象的创建（只读查询应关掉）
→ 三笔账都有同一个成因: ORM 默认按「对象」的粒度工作，而数据库按「集合」的粒度工作
```

> ⚠️ **ORM 的性能问题几乎全部是「没看生成的 SQL」造成的**。本章每一个实测数字，都是打开 SQL 日志之后才能看见的——`set_trace_callback`（Python）、`LogTo`（EF Core）、`show_sql`（Hibernate）。**不看日志用 ORM，等于闭着眼睛写 SQL。**

---

## 13. 工程实践

| 场景 | ✅ 推荐 | ❌ 避免 | 原因 |
|------|--------|--------|------|
| 关联查询 | 显式 `Include`/`select_related`/`fetch join` | 依赖延迟加载 | 实测 N+1 发出 201/301 条 SQL |
| 列表页展示 | 投影查询（只取需要的列） | 取整个实体 | 实测 over-fetching 2.7x；且等于 `SELECT *` |
| 只读查询 | `AsNoTracking()` / 只读会话 | 默认跟踪 | 省掉每个实体一份快照 |
| 过滤条件 | 保持 `IQueryable` 到最后 | 过早 `.ToList()` | 实测慢 86x，多传输 20000x 行 |
| 返回给视图层 | 转成 DTO | 直接返回实体 | 序列化时触发延迟加载 → 事务外炸掉 |
| 批量插入/更新 | 原生 SQL 或批量 API | 循环 `save()` | 每次 save 一条 SQL，且都要跟踪 |
| 复杂报表查询 | 直接写 SQL | 硬用 ORM 表达 | ORM 表达不了的查询，翻译出来往往更慢 |
| 继承映射 | 先想清楚再选策略 | 随手用默认 | 实测三种策略各有硬伤，改起来很贵 |
| 排查性能 | **打开 SQL 日志** | 猜 | 本章所有数字都靠日志才看得见 |
| 数据库迁移 | ORM 的迁移工具 + 人工审阅 | 自动生成直接上线 | 生成的 DDL 可能锁表（第 50 章） |

### 一句话决策

```text
用 ORM 之前问三件事:
  ① 这个查询 ORM 表达得出来吗？   → 表达不出来就直接写 SQL，不要硬凑
  ② 我需要关联数据吗？           → 需要就【显式】声明预加载，别靠延迟加载
  ③ 这是只读查询吗？             → 是就关掉变更跟踪，或直接用投影
用完之后做一件事: 看一眼它生成的 SQL
```

---

## 14. 最佳实践

- **永远打开 SQL 日志**：本章每一个数字都是日志才能看见的——不看日志用 ORM 等于闭着眼睛写 SQL。
- **关联一律显式预加载**：实测延迟加载让一个 for 循环发出 201 条 SQL；把「要不要加载关联」变成代码里看得见的一行。
- **只读查询用投影而不是取实体**：既避开 `SELECT *`（第 47 章：关闭覆盖索引），又省掉变更跟踪的快照。
- **保持 `IQueryable` 到最后一刻**：实测过早 `.ToList()` 慢 86 倍、多传输两万倍的行。
- **返回给视图层的一律转成 DTO**：实体离开事务边界后，延迟加载必然失效（JS 实测会话关闭即抛异常）。
- **复杂查询直接写 SQL**：ORM 表达不出来的查询硬凑，翻译出来的 SQL 往往比手写更慢也更难懂。
- **继承映射要提前想清楚**：三种策略各有硬伤（约束失效 / JOIN 开销 / UNION 与主键），而数据落地后再改极贵。
- **把 ORM 当作「省样板」的工具而非「不用懂 SQL」的借口**：Java 版实测它省下 220 行样板，但省不掉你对 SQL 的理解。

---

## 15. 常见坑

**坑 1 · 循环里访问关联属性**

```python
for u in users:
    total += sum(o.amount for o in u.orders)   # ⚠️ 实测 201 条 SQL
# ✅ 一次预加载: session.query(User).options(joinedload(User.orders))
```

**坑 2 · 实体离开事务后访问关联**

```javascript
const u = session.users()[0];
session.close();
u.orders;    // ⚠️ 实测抛异常（LazyInitializationException 同款）
// ✅ 在事务内就取好需要的数据，或转成 DTO
```

**坑 3 · 过早物化查询**

```csharp
db.Users.ToList().Where(u => u.Score > 98);   // ⚠️ 实测慢 86x，传输 20000x 的行
db.Users.Where(u => u.Score > 98).ToList();   // ✅
```

**坑 4 · 只读查询忘了关跟踪**

```csharp
var list = db.Users.ToList();                  // ⚠️ 每个实体一份快照
var list = db.Users.AsNoTracking().ToList();   // ✅
```

**坑 5 · 循环调用 save()**

```java
for (User u : tenThousandUsers) session.save(u);   // ⚠️ 一万条 INSERT + 一万份跟踪
// ✅ 批量 API / 原生 SQL；并定期 flush + clear
```

**坑 6 · 直接把实体序列化成 JSON 返回**

```text
⚠️ 序列化器会访问所有属性 → 触发全部延迟加载 → N+1 甚至事务外异常
⚠️ 双向关联还会导致无限递归
✅ 转成 DTO，显式决定返回哪些字段
```

**坑 7 · 用 ORM 硬写复杂报表**

```text
⚠️ 多层嵌套的 GroupBy/Having/窗口函数，ORM 翻译出来的 SQL 常常又长又慢
✅ 这类查询直接写 SQL（第 47 章），ORM 只负责把结果映射成对象
```

---

## 16. 面试题

**基础**

1. 什么是阻抗失配？举出至少三个具体表现。
2. ORM 的三件核心工作是什么？（映射、身份映射、变更跟踪）
3. 什么是 N+1 查询？为什么它在源码里看不出来？

**中级**

4. **ORM 是怎么把一个类映射成一张表的？（分别用 Java 的反射和 C# 的表达式树回答）**
5. 延迟加载为什么会在事务外失效？现代 ORM 用什么方案取代它？
6. **`IQueryable` 和 `IEnumerable` 的区别是什么？一个 `.ToList()` 放错位置会有多大代价？**

**高级**

7. **为什么 C++ 没有主流 ORM？（用运行时反射的缺失来解释）**
8. 继承的三种映射策略各有什么取舍？为什么说「选错了很难改」？
9. 变更跟踪的两种实现（快照法 vs 代理法）各有什么代价？

---

## 17. 练习

**基础**

1. 打开你项目 ORM 的 SQL 日志，数一数一个典型页面发出了多少条 SQL。
2. 找出一处 N+1，用预加载消除它，测量前后的 SQL 条数与耗时。
3. 给一个只读查询加上 no-tracking，测量差别。

**中级**

4. **复现钥匙实验一**：用反射（或你的语言的等价机制）写一个能生成 `INSERT`/`SELECT` 的微型 ORM。
5. 用真实 SQL 计数（trace 回调 / 日志）复现 N+1，确认条数是 1+N。
6. 实现身份映射与脏检查，验证「同一行只有一个对象」和「只更新变了的列」。

**挑战**

7. **复现钥匙实验二**：写一个表达式树（或 AST）到 SQL 的翻译器，支持 `AND`/`OR`/比较/`LIKE`。
8. 实现三种继承映射策略中的两种，比较它们的查询 SQL 与约束能力。
9. 给你的微型 ORM 加上预加载（一条 JOIN 取回主实体与关联），并测量它相对延迟加载的加速比。

---

## 18. 本章总结

**一句话**：ORM 是**对象世界与关系世界之间的翻译器**，它翻译的是五处阻抗失配（身份 / 关联 / 集合 / 继承 / 加载时机），实现手段则由语言特性决定——本章用三个钥匙实验把它拆开了：Java 用**反射**读出 `@Table`/`@Column` 并生成 SQL（实体类上没有一行映射代码，实测省下约 220 行样板），C# 用**表达式树**把 `u => u.Score > 90 && u.City == "city-3"` 翻译成 `WHERE ((Score > 90) AND (City = 'city-3'))`（一百行就是 EF Core 的核心），Python 用真实 SQL 计数抓住了 **N+1 的诞生现场**（一个 for 循环加一次属性访问 → **201 条 SQL**，改成 JOIN 后 1 条，快 8.9 倍；JS 复现为 301 条 vs 1 条）；而 ORM 的每个坑都来自它最大的卖点——**把数据库访问伪装成内存访问**：延迟加载让实体离开事务边界后直接抛异常（JS 实测），`.ToList()` 放错位置让过滤从服务端跑到客户端（C# 实测**多传输两万倍的行、慢 86 倍**）；最后是一个语言层面的规律——**运行时越能自省的语言，ORM 越强大也越流行**，而 C++ 因为没有运行时反射（第 30 章的缺口），至今没有主流 ORM。

**关键要点**

- **阻抗失配的五处**：身份、关联、集合、继承、加载时机——ORM 省下的每一行代码都对应其中一项。
- **三件核心工作**：映射（类↔表）、身份映射（主键↔引用相等）、变更跟踪（对象改动↔UPDATE）。
- **钥匙实验一**（Java）：反射 + 注解生成 SQL，实测省下约 220 行样板。
- **钥匙实验二**（C#）：表达式树 → SQL，一百行就是 EF Core 的核心；翻译不了时应报错而非静默降级。
- **钥匙实验三**（Python/JS）：N+1 从一次属性访问里冒出来——201 / 301 条 SQL，快 8~9 倍可消除。
- **延迟加载的代价**（JS 实测）：离开事务边界即失效，这是 ORM 最典型的抽象泄漏。
- **查询在哪一端执行**（C# 实测）：`.ToList()` 早一步 → 多传输 20000x 行、慢 86x。
- **语言特性决定技术路线**：反射（Java）/ 表达式树（C#）/ 描述符（Python）/ 代码生成（JS）/ 宏（C++）。

**自查清单**

- [ ] 我能说出阻抗失配的五处表现，以及 ORM 分别做了什么。
- [ ] 我能解释 N+1 为什么在源码里看不出来，并用三种手段消除它。
- [ ] 我知道延迟加载什么时候会失效，以及该用什么替代。
- [ ] 我能判断一个查询是在数据库端还是内存端执行。
- [ ] 我会打开 SQL 日志，并看得懂 ORM 生成的 SQL。

**Part 7 完结**：从第 46 章的「为什么需要数据库」（持久化、原子性、并发控制、索引、查询语言这五件事的手写代价），到第 47 章的 SQL（声明式，把算法选择权交给优化器）、第 48 章的事务（ACID 与 MVCC 的三行可见性规则）、第 49 章的索引（扇出决定树高）、第 50 章的锁（等待图找环）、直到本章的 ORM——**Part 7 的六章其实在回答同一个问题的六个层面：如何把数据安全、正确、高效地放在进程之外**。

**下一章预告**：Part 8「工程化」是全书的收口，把前面所有能力落到真实工程。第 52 章从**测试**开始——它回答一个此前所有章节都默认成立却从未论证的问题：**你怎么知道代码是对的**？我们会实测测试金字塔各层的真实成本（单元测试毫秒级、集成测试秒级、端到端测试分钟级）、量化「测试替身」省下了多少时间又掩盖了多少真实缺陷，以及为什么**测试覆盖率是一个极其容易被误用的指标**。

---

## 19. 延伸阅读

- <a href="https://martinfowler.com/bliki/OrmHate.html" target="_blank" rel="noopener">Martin Fowler · OrmHate</a> —— 对「ORM 是反模式」这一说法最平衡的回应。
- <a href="https://en.wikipedia.org/wiki/Object%E2%80%93relational_impedance_mismatch" target="_blank" rel="noopener">Wikipedia · 对象-关系阻抗失配</a> —— 本章第 ⑥ 节那张表的完整版。
- <a href="https://martinfowler.com/eaaCatalog/identityMap.html" target="_blank" rel="noopener">Fowler · Identity Map</a> —— 身份映射模式的原始定义（本章 Python 版实现的就是它）。
- <a href="https://martinfowler.com/eaaCatalog/unitOfWork.html" target="_blank" rel="noopener">Fowler · Unit of Work</a> —— 变更跟踪与批量提交的模式来源。
- <a href="https://docs.microsoft.com/en-us/ef/core/querying/client-eval" target="_blank" rel="noopener">EF Core · 客户端求值</a> —— EF Core 3.0 为何把静默降级改成报错。
- <a href="https://docs.sqlalchemy.org/en/20/orm/queryguide/relationships.html" target="_blank" rel="noopener">SQLAlchemy · 关联加载策略</a> —— 各种预加载方式的官方对比。
- <a href="https://docs.jboss.org/hibernate/orm/current/userguide/html_single/Hibernate_User_Guide.html#fetching" target="_blank" rel="noopener">Hibernate · Fetching</a> —— 延迟加载与 `LazyInitializationException` 的权威说明。
- <a href="https://www.prisma.io/docs/orm/prisma-client/queries/relation-queries" target="_blank" rel="noopener">Prisma · 关联查询</a> —— 「显式而非隐式」的新一代 ORM 设计。
- <a href="https://martinfowler.com/eaaCatalog/" target="_blank" rel="noopener">Patterns of Enterprise Application Architecture</a> —— ORM 用到的绝大多数模式的出处。
