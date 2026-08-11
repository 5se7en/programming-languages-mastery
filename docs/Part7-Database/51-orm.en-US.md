# Chapter 51 · ORM

[简体中文](./51-orm.md) ｜ **English**

---

> The last five chapters were all about how a database thinks — tables, rows, SQL, transactions, locks. But your code thinks in **objects** — classes, fields, collections, inheritance. Where those two worldviews fail to line up has a name: the **impedance mismatch**. An ORM is the translator across that gap.
>
> This chapter's **key experiment** lands in three places: Java **hand-writes an ORM using reflection** (`@Table`/`@Column` annotations plus `Field.set()`, measured generating `INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')` while the entity class contains **not one line of mapping code**); C# **hand-writes an expression-tree-to-SQL translator** (measured turning `u => u.Score > 90 && u.City == "city-3"` into `WHERE ((Score > 90) AND (City = 'city-3'))` — a hundred lines that are EF Core's core); and Python **catches N+1 at its moment of birth using real SQL counting**.
>
> That moment of birth deserves its own mention: the source code reads `for u in users: sum(o.amount for o in u.orders)` — **one for loop and one property access** — yet it measured **201 SQL statements**; rewritten as a single JOIN it is one, **201× fewer statements and 8.9× faster**. The JS example reproduced the same thing (301 versus 1, 8.0× faster). **What makes N+1 so insidious is that it is invisible in the source** — because an ORM disguises database access as memory access, and those differ by six orders of magnitude.
>
> The abstraction leaks again at **lazy loading**: JS measured `u.orders` returning 20 orders inside the session and throwing once the session closed — this is Hibernate's `LazyInitializationException`. The root cause is that **the object looks like an ordinary object while still being attached to the database**; the moment it leaves the transaction boundary (returned to a view layer, cached, serialized to JSON), its associations are unreachable.
>
> The C# example also quantified the famous `IQueryable`/`IEnumerable` trap. An in-memory collection cannot show it at all (there is no "transfer" step), so the experiment simulates a real data source: **filtering server-side transferred 5 rows in 3.1 ms; calling `.ToList()` first transferred 100,000 rows in 266.3 ms — 20,000× more rows and 86× slower**. One method call's position, two orders of magnitude.
>
> Finally, a language-level finding: **C++ has no mainstream ORM because it has no runtime reflection** (Chapter 30's gap). Java can write `for (Field f : User.class.getFields())` and simply ask the class for its fields; a C++ class **does not know what fields it has** — so C++ ORMs rely on macros, templates, or external code generators. A rule emerges: **the more a language can introspect at runtime, the more powerful and popular its ORMs**.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- List the five concrete faces of the **impedance mismatch** (identity, association, collections, inheritance, load timing) and what an ORM does for each;
- Explain how an **N+1 query emerges from a single property access** (measured 201 statements) and eliminate it three ways;
- Explain why **lazy loading** fails outside a transaction and what modern ORMs use instead;
- Determine whether an ORM query executes **in the database** or **in memory** (measured 86× apart);
- Explain **why different languages' ORMs took different technical routes**, and why C++ has almost none.

---

## 2. Why This Concept Exists

### Two worldviews

```text
Your code thinks in: classes, objects, fields, List<T>, inheritance, references
The database thinks in: tables, rows, columns, foreign keys, JOINs, primary keys
→ these are【not two phrasings of the same thing】; they genuinely fail to line up
```

**Five concrete places they fail** (the Python example prints this table):

| Concept | Object world | Relational world |
|---------|-------------|------------------|
| Identity | reference equality (`is`) | primary key |
| Association | holding a reference directly | foreign key + JOIN |
| Collections | `List<Order>` | multiple rows in another table |
| Inheritance | native | **no such concept** |
| Load timing | all in memory | queried on demand (lazy or eager) |

### What you write without an ORM

**The Java example lays out the manual code**:

```java
PreparedStatement ps = conn.prepareStatement(
    "INSERT INTO users (id, name, city_name) VALUES (?, ?, ?)");
ps.setInt(1, user.id);
ps.setString(2, user.name);
ps.setString(3, user.city);
ps.executeUpdate();
// and the query side needs the【reverse】mapping written again:
User u = new User();
u.id = rs.getInt("id");
u.name = rs.getString("name");
u.city = rs.getString("city_name");
```

```text
by hand: 11 lines, rewritten for【every entity and every direction】
ORM    : orm.save(user) / orm.find(User.class, 1) — 2 lines, generic across entities
→ 10 entities × 4 CRUD directions ≈ 220 lines of boilerplate by hand vs 0 with an ORM
```

> **In one sentence**: an ORM eliminates **repetitive code unrelated to your business** — but it does so by **disguising database access as memory access**, and that disguise is precisely where all its traps come from (this chapter measures each one).

---

## 3. How It Works

### Key experiment one: how reflection generates SQL (Java)

**The entity class has only annotations, not one line of mapping code**:

```java
@Table("users")
public static class User {
    @Id @Column public Integer id;
    @Column public String name;
    @Column("city_name") public String city;      // field name differs from column name
    public String notPersisted = "fields without @Column are not persisted";
}
```

**The ORM reads the mapping via reflection** (measured):

```text
table: users
primary key field: id
column id         ← field id     (Integer)
column name       ← field name   (String)
column city_name  ← field city   (String)
⚠️ notPersisted has no @Column, so it is【not in the mapping】
```

**And generates SQL from it**:

```java
for (Field f : c.getFields()) {
    Column col = f.getAnnotation(Column.class);
    if (col == null) continue;                       // unannotated fields aren't persisted
    cols.put(col.value().isEmpty() ? f.getName() : col.value(), f);
}
```

```text
you write:  orm.save(user)
ORM emits:  INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')

you write:  orm.find(User.class, 1)
ORM emits:  SELECT id, name, city_name FROM users WHERE id = 1
object restored: User{id=1, name='张三', city='北京'}
```

**This is reflection's most practical use** (Chapter 30): read a type's structure at runtime, generate SQL from it, and turn rows back into objects.

### Key experiment two: how expression trees become SQL (C#)

**A hundred-line translator** (measured):

```text
what you write in LINQ                       →  the SQL the ORM emits
u => u.Score > 90                            → WHERE (Score > 90)
u => u.City == "city-3"                      → WHERE (City = 'city-3')
u => u.Score > 90 && u.City == "city-3"      → WHERE ((Score > 90) AND (City = 'city-3'))
u => u.Name.StartsWith("user-1")             → WHERE Name LIKE 'user-1%'
```

**The core is one recursive pattern match**:

```csharp
public static string Translate(Expression expr) => expr switch
{
    BinaryExpression b => $"({Translate(b.Left)} {Op(b.NodeType)} {Translate(b.Right)})",
    MemberExpression m when m.Expression is ParameterExpression => m.Member.Name,   // u.Score → Score
    ConstantExpression c => c.Value is string s ? $"'{s}'" : c.Value?.ToString(),
    ...
};
```

**The point is to *read* rather than *execute*** — Chapter 47 established the prerequisite: only `Expression<Func<>>` retains syntactic structure, while a `Func<>` compiles to IL and cannot be introspected.

**What happens when translation fails** (measured):

```text
translating u => MyCustomCheck(u.Name) ... → ✗ threw: cannot translate: Call

Real ORMs react two ways:
  EF Core 3.0+ : it【errors】— forcing you to decide what to do (good design)
  EF Core 2.x  : it【silently degraded】to client evaluation = quietly fetching the whole table
  → the latter caused countless production incidents, so 3.0 made erroring the default
```

### Key experiment three: N+1's moment of birth (Python)

**The source looks perfectly normal**:

```python
for u in s.query(User):                        # 1 SQL statement for all users
    total += sum(o.amount for o in u.orders)   # ← another statement【per user】
```

**And `u.orders` is just a property**:

```python
@property
def orders(self):
    """⚠️ looks like one property access; it is actually【one SQL statement】"""
    return self._session.query(Order, where=f"user_id = {self.id}")
```

**Counting actually-executed SQL with sqlite's trace callback** (measured):

```text
→ 201 SQL statements actually executed, 17.6 ms
first 3 statements:
  SELECT id, name, city FROM users WHERE 1=1
  SELECT id, user_id, amount FROM orders WHERE user_id = 0
  SELECT id, user_id, amount FROM orders WHERE user_id = 1
```

**Rewritten as one JOIN**:

```text
→ 1 SQL statement, 2.0 ms
→ 201× fewer statements, 8.9× faster, identical results: True
```

**The JS example reproduced it**: 301 versus 1, 8.0× faster.

```text
⚠️ What makes N+1 insidious: it is【invisible in the source】
Root cause: the ORM disguises "database access" as "memory access"
            and memory access is nanoseconds while database access is milliseconds —
            the disguise hides exactly those six orders of magnitude
```

### Identity map and change tracking: an ORM's other two core jobs

**Identity map** (measured):

```text
querying the same row twice: a is b = True (no second object created)
after changing a, b.name = 'the changed name'   ← because they【are the same object】
→ without an identity map: one row loads as two objects; change one and the other is stale
→ this addresses the first mismatch: databases have【primary keys】, objects have【reference equality】
```

**Change tracking** (measured, snapshot approach):

```text
dirty objects: 1
  User(id=1).name: 'user-1' → 'the changed name'
generated SQL: ["UPDATE users SET name = 'the changed name' WHERE id = 1"]
→ only the【columns that actually changed】are updated, not the whole row
→ how: store a snapshot at load time and compare field by field at flush (5 lines here)
```

**Two implementation routes**:

```text
Snapshot (EF Core default / Hibernate): copy at load, compare at save
  ✓ simple and reliable   ✗ one in-memory copy per entity (C# measured the cost for 50,000)
Proxy (change proxies): generate a subclass intercepting setters, mark on write
  ✓ saves memory          ✗ properties must be virtual, and the object is no longer your type
→ turn tracking off for bulk read-only queries: EF's .AsNoTracking()
```

---

## 4. JavaScript

The JS example catches the ORM abstraction **at the moment it leaks**.

### Lazy loading: a SQL statement hiding behind a property (measured)

```javascript
return {
  ...row,
  // ⚠️ looks like an ordinary property; every access issues a SQL statement
  get orders() { return session.run('SELECT amount FROM orders WHERE user_id = ?', row.id); },
};
```

```text
for (const u of users) u.orders...  → 301 SQL statements, 29.2 ms
one JOIN → 1 statement, 3.7 ms
→ 301× fewer statements, 8.0× faster
```

### The moment the abstraction leaks (measured)

```text
accessing u.orders inside the session: ✓ got 20 orders
accessing it after the session closed: ✗ session is closed
→ this is Hibernate's LazyInitializationException / Django's equivalent
```

```text
Root cause: the object【looks like】an ordinary object while still being【attached to the database】
            once it leaves the transaction boundary (returned to a view, cached,
            serialized to JSON), its associations are unreachable
```

**This is the ORM's most typical abstraction leak**: it works hard to convince you this is just an in-memory object, until one moment when it suddenly isn't.

### Hence the dilemma

```text
Lazy loading: saves memory, but explodes outside the transaction and invites N+1
Eager loading: safe, but may fetch data you never use (over-fetching)
→ the modern answer is【explicit declaration】: Prisma's include, EF's .Include(), JPA's fetch join
   turning "should this association load?" into【a visible line of code】rather than an implicit access
```

### The cost of over-fetching (measured)

```text
5 columns: 3.0 ms (6000 rows)
2 columns: 1.1 ms (6000 rows, 2.7× faster)
→ an ORM's default "fetch the whole entity" is always SELECT *
  (Chapter 47 measured that it closes covering indexes)
→ when a list page needs two columns, use a projection (Prisma's select, EF's .Select())
```

> **Note**: JS has neither runtime reflection nor standard annotations, so the mainstream solutions rely on **code generation** (Prisma's schema file, TypeORM's experimental decorators); this is the same idea as C++'s ODB — **what the language withholds, an external tool generates**; Knex is a pure query builder without object mapping, the choice when you want type safety but not an ORM.

---

## 5. Python

The Python example is the N+1 arena and implements all three of an ORM's core jobs.

### Counting real SQL (the experiment's key design)

```python
con.set_trace_callback(lambda s: QUERY_LOG.append(s))   # ← install a "SQL counter"
```

**Without this callback, N+1 could only be reasoned about**; with it, every executed statement is recorded, and **201 is counted rather than calculated**.

### The micro-ORM's three jobs

```python
class Session:
    def query(self, cls, where="1=1"):
        sql = f"SELECT {', '.join(cls.__fields__)} FROM {cls.__table__} WHERE {where}"
        for row in self.con.execute(sql):
            key = (cls, row[0])
            if key in self.identity_map:            # ① identity map: reuse the same object
                out.append(self.identity_map[key]); continue
            obj = cls._from_row(row, self)          # ② mapping: row → object
            self.snapshots[id(obj)] = {...}         # ③ change tracking: store a snapshot
```

**About 40 lines sketch an ORM's skeleton** — real ORMs are far larger because they also handle type conversion, associations, inheritance, migrations, and dialect differences.

### The five faces of the impedance mismatch (the measured summary)

```text
┌────────────┬──────────────────┬──────────────────────┐
│ concept    │ object world     │ relational world     │
├────────────┼──────────────────┼──────────────────────┤
│ identity   │ reference (is)   │ primary key          │
│ association│ a direct reference│ foreign key + JOIN  │
│ collections│ List<Order>      │ rows in another table│
│ inheritance│ native           │【no such concept】   │
│ load timing│ all in memory    │ queried on demand    │
└────────────┴──────────────────┴──────────────────────┘
→ an ORM is this table's translator
→ every line of code it saves corresponds to one row above
→ and every trap it has (N+1, lazy-load failure, inheritance mapping) comes from the same table
```

> **Note**: SQLAlchemy intercepts attribute access with **descriptors and metaclasses** (this example's `@property` is the simplified form); Django ORM's `select_related` (JOIN) and `prefetch_related` (IN batch) map onto the last two rungs of Chapter 47's "N+1 → IN → JOIN" ladder; `session.query()` became `session.execute(select(...))` in SQLAlchemy 2.0.

---

## 6. Java

The Java example hand-writes a reflection ORM — **JPA/Hibernate's skeleton**.

### Reflection: an ORM's most fundamental dependency

```java
static Map<String, Field> columnsOf(Class<?> c) {
    Map<String, Field> cols = new LinkedHashMap<>();
    for (Field f : c.getFields()) {                      // ← ask the class for its fields at runtime
        Column col = f.getAnnotation(Column.class);       // ← read annotations at runtime
        if (col == null) continue;
        cols.put(col.value().isEmpty() ? f.getName() : col.value(), f);
    }
    return cols;
}
```

**These lines are the watershed between Java's ORMs and C++'s**: the class **knows what it looks like**, so mappings can be obtained automatically.

### How much code it saves (measured)

```text
by hand: 11 lines, rewritten per entity and per direction
ORM    : 2 lines, generic across entities
→ 10 entities × 4 directions ≈ 220 lines of boilerplate vs 0
→ this is the real reason ORMs are so widely adopted:
  eliminating【repetitive code unrelated to the business】
```

### But ORMs introduce three new problems

```text
① N+1 queries: one property access = one SQL statement (Python measured 201)
② lazy-load failure: accessing associations after the session closes →
   LazyInitializationException (measured in JS)
③ invisible SQL: you write object operations while executing SQL you never read
→ all three share one root: the ORM disguises【data access】as【memory access】
   and memory access is nanoseconds while database access is milliseconds —
   the disguise hides exactly those six orders of magnitude
```

### The spectrum of Java ORMs

```text
JPA       : the specification (jakarta.persistence annotations — what this example imitates)
Hibernate : JPA's dominant implementation; @Entity/@Table/@Column/@OneToMany
MyBatis   : a "half ORM" — you write the SQL, it maps the results
jOOQ      : the inverse — type-safe SQL written in Java (hiding nothing)
→ the two ends: Hibernate hides the most SQL, jOOQ hides none
→ the real question in choosing is: how much SQL detail are you willing to keep out of the code?
```

> **Note**: Hibernate entities need a no-arg constructor and non-final classes (it generates proxy subclasses); `@OneToMany` defaults to `LAZY` while `@ManyToOne` defaults to `EAGER` — **that asymmetry is a common N+1 source**; `JOIN FETCH` is JPQL's explicit eager loading.

---

## 7. C++

The C++ example answers a language-level question: **why C++ has no mainstream ORM**.

### The answer: Chapter 30's gap

```text
Java : for (Field f : User.class.getFields())  ← ask the class for its fields at runtime
       @Column annotations are read at runtime too — the class knows what it looks like
C++  : the class【does not know】what fields it has
       the mapping must be【hand-written】, and the compiler won't remind you when you forget
```

**What C++ can do is compile-time mapping** (measured SQL identical to Java's):

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
→ the SQL is identical, but【how the mapping is obtained】differs entirely:
  that meta() is hand-written
```

### So C++'s "ORMs" split three ways

```text
① macros / template metaprogramming (this example)
   sqlpp11, sqlite_orm — compile-time type safety; SQL errors caught at compile time
   the price: error messages spanning hundreds of lines of template expansion, and hand-written mappings
② external code generation
   ODB — a【preprocessor】scans your headers and generates mapping code
   the price: one more tool in the build, i.e. you built your own reflection
③ no ORM at all
   raw SQL plus manual mapping — the most common choice in C++ projects
→ all three route around the same gap: without reflection,
  the mapping information has to【come from somewhere else】
```

### Compile-time mapping does have one advantage

```text
Java/Python ORMs: a typo'd column name errors at【runtime】(sometimes after deployment)
C++ template ORMs: a type mismatch errors at【compile time】
→ sqlpp11 can even check at compile time whether the column you SELECT exists in that table
→ another expression of zero-overhead philosophy: whatever can be done at compile time, is
→ the price is flexibility: scenarios where the schema is known only at runtime
  (a generic admin console) are out of reach
```

### Will C++26's static reflection change this?

```text
P2996 static reflection landed in C++26 — it can walk a class's members【at compile time】
→ that meta() will be generated automatically, and the macros can retire
→ but it remains【static】reflection: visible at compile time, still absent at runtime
→ so C++ will get better compile-time ORMs, but never a runtime one like Hibernate
```

> **Note**: this example walks the metadata tuple with a C++17 fold expression; `sqlite_orm` is the modern header-only choice, expressing the mapping as one `make_storage(...)`; ODB's preprocessor must parse C++ headers, so its support for templates and new standards always lags.

---

## 8. C#

The C# example quantifies the famous `IQueryable`/`IEnumerable` trap — **after first explaining why an in-memory collection cannot show it**.

### Experiment design: transfer cost must be simulated

```text
⚠️ an in-memory collection cannot show this difference (there is no "transfer" step,
   so IQueryable is pure overhead)
→ so simulate a【real data source】: every returned row costs transfer + deserialization
```

```csharp
IEnumerable<User> Materialize(Func<User, bool>? serverFilter)
{
    foreach (var r in _rows)
    {
        if (serverFilter != null && !serverFilter(r)) continue;   // filtered server-side, not transferred
        RowsTransferred++;
        Thread.SpinWait(100);                                      // transfer + deserialization cost
        yield return r;
    }
}
```

### The measurement

```text
.Where(...).ToList()   ← filtering【server-side】:    3.1 ms, transferred      5 rows
.ToList().Where(...)   ← filtering【client-side】:  266.3 ms, transferred 100000 rows
identical results: True; 86× slower, 20000× more rows transferred
```

```text
→ one misplaced .ToList() turns "database filtering" into "fetch everything and filter in memory"
→ Chapter 46 measured the same bill on a real database: 109 ms + 46 MB heap vs 9 μs
```

**The trap is dangerous precisely because the two versions look almost identical in source** — only the position of `.ToList()` differs by one method call.

### The cost of change tracking (measured)

```text
measured snapshot cost: one copy for each of 50000 entities
→ this is the usual cause of "querying ten thousand rows just to display them is inexplicably slow"
→ add .AsNoTracking() to read-only queries, or use projections instead of entities
```

### Five languages' ORMs and the features they rely on

```text
C#     : EF Core —— translates LINQ to SQL via【expression trees】(what this chapter's C# implements)
Java   : Hibernate/JPA —— annotations +【runtime reflection】(what this chapter's Java implements)
Python : SQLAlchemy —— intercepts attribute access with【descriptors and metaclasses】
JS     : Prisma/TypeORM ——【code generation】plus decorators
C++    : ❌ no mainstream ORM —— because it has【no runtime reflection】
→ three technical routes for three language features;
  whichever feature is strongest, that ecosystem's ORM feels most natural
```

> **Note**: EF Core 3.0 changed untranslatable expressions from silent client evaluation to **erroring**, an important design correction; `FromSqlRaw` accepts interpolated strings and parameterizes automatically (Chapter 47's rare safe interpolation); deeply chained `.Include()` calls can produce cartesian explosion — use `AsSplitQuery()`.

---

## 9. SQL

This section views the mismatch from the database side — especially the one concept the relational model simply lacks: **inheritance**.

### Three strategies for mapping inheritance (measured)

**Strategy A: single table** (all subclasses in one table plus a discriminator)

```sql
CREATE TABLE payment_single (
  id INTEGER PRIMARY KEY, kind TEXT NOT NULL, amount INTEGER NOT NULL,
  card_no TEXT,          -- only CardPayment uses it
  bank TEXT              -- only WirePayment uses it
);
```

```text
✓ fastest queries (no JOIN)
✗ subclass-specific columns must be nullable →【constraints are lost】
  (you cannot require a CardPayment's card number to be non-null)
```

**Strategy B: joined** (a parent table plus one per subclass)

```text
✓ every column can be NOT NULL (constraints preserved)
✗ every query needs a JOIN
```

**Strategy C: table per class** (fully independent)

```text
✓ simplest, strongest constraints
✗ querying "all payments" needs UNION ALL, and primary keys must be unique across tables
```

```text
→ no strategy wins: the ORM makes you choose, and【choosing wrong is expensive to fix】—
  the data has already landed in that shape
```

### Associations and many-to-many

```text
one-to-many: objects hold author.books (a List); the database holds book.author_id (a foreign key)
             → "reading a field" in objects is "running a query" in the database — six orders apart
             → N+1 is born in that gap

many-to-many: objects hold student.courses + course.students;
              the database needs【an extra join table】
             → the join table【does not exist】in the object model — the classic mismatch
             → the moment it needs an extra field (enrollment date), it must become an entity
```

### Collections and types don't line up either

```text
collections: List<T> is ordered and allows duplicates; Set<T> is unordered and unique
             table rows have【no inherent order】(unordered without ORDER BY, Chapter 47)
             → preserving order requires a position column that the ORM must maintain

types: enums → store as TEXT or INTEGER? value objects Money{amount, currency} →
       two columns or a JSON column?
       NULL → objects have Optional<T>, the relational world has three-valued logic (Chapter 47)
       → an ORM's type converters (AttributeConverter / ValueConverter) exist for exactly this
```

> **Note**: JPA selects the strategy with `@Inheritance(strategy = ...)`; single-table is Hibernate's default (`SINGLE_TABLE`); when a join table needs extra fields, JPA models it as its own entity with an `@EmbeddedId`.

---

## 10. Cross-Language Comparison

### ① ORM technical routes

| Language | How the mapping is obtained | Representative | What this chapter implemented |
|----------|---------------------------|---------------|------------------------------|
| **Java** | annotations + **runtime reflection** | Hibernate / JPA | a reflection ORM (`@Table`/`@Column` + `Field.set()`) |
| **C#** | **expression trees** + reflection | EF Core | an expression-tree-to-SQL translator |
| **Python** | descriptors + metaclasses | SQLAlchemy / Django ORM | a micro-ORM (mapping + identity map + change tracking) |
| **JavaScript** | decorators + **code generation** | Prisma / TypeORM | lazy loading and session failure |
| **C++** | macros / external code generation | sqlpp11 / ODB (niche) | compile-time mapping, and why no mainstream ORM |

```text
→ a rule:【the more a language can introspect at runtime, the more powerful and popular its ORMs】
→ and the converse:  C++'s ORMs are weakest precisely because it defers everything to compile time
```

### ② Key experiment one: reflection generates SQL (Java measured)

```text
the entity has only annotations, not one line of mapping code
→ reflection reads: table users; columns id/name/city_name ← fields id/name/city
→ generates: INSERT INTO users (id, name, city_name) VALUES (1, '张三', '北京')
→ 11 lines by hand vs 2 with an ORM; 10 entities × 4 directions ≈ 220 lines saved
```

### ③ Key experiment two: expression tree → SQL (C# measured)

```text
u => u.Score > 90 && u.City == "city-3"  →  WHERE ((Score > 90) AND (City = 'city-3'))
u => u.Name.StartsWith("user-1")         →  WHERE Name LIKE 'user-1%'
when untranslatable: it throws (EF Core 3.0+ behavior; 2.x silently degraded to client evaluation)
```

### ④ Key experiment three: N+1's moment of birth (Python / JS measured)

```text
Python: 201 statements / 17.6 ms  vs  one JOIN / 2.0 ms → 201× fewer, 8.9× faster
JS:     301 statements / 29.2 ms  vs  one JOIN / 3.7 ms → 301× fewer, 8.0× faster
→ the source is just "one for loop plus one property access"
```

### ⑤ Key experiment four: where the query executes (C# measured)

```text
.Where(...).ToList()  → transferred      5 rows,   3.1 ms
.ToList().Where(...)  → transferred 100000 rows, 266.3 ms
→ 20000× more rows, 86× slower — the only difference is where .ToList() sits
```

### ⑥ Common ground and root causes

**Common ground**: every ORM does the same three jobs — **mapping** (class↔table), **identity map** (primary key↔reference equality), and **change tracking** (object edits↔UPDATE); every ORM has an N+1 problem, because they all model associations as properties; every ORM offers an eager-loading switch, because lazy loading's default is not good enough in production.

**Root causes**:

- **The technical route follows the language's features**: Java has runtime reflection so it uses reflection; C# has expression trees so it translates queries; Python has descriptors so it intercepts attributes; JS has neither so it generates code; C++ has none of them, hence almost no ORM;
- **N+1's root cause is the abstraction itself**: an ORM's value is making database access look like memory access, and that value is exactly what creates N+1 — **the same feature is both the selling point and the trap**;
- **Lazy-load failure comes from mismatched lifetimes**: an object's lifetime is decided by GC, a connection's by the transaction; the moment they diverge, things break (Chapter 45: a connection can't return to the pool mid-transaction);
- **Inheritance has no winning strategy** because the relational model has no such concept — the one mismatch that **cannot be translated satisfactorily**.

---

## 11. Implementation Comparison

| ORM | Mapping mechanism | Change tracking | Query translation |
|-----|------------------|----------------|-------------------|
| **Hibernate** | annotations + runtime reflection | snapshot (default) + optional bytecode enhancement | HQL/JPQL → SQL; Criteria API |
| **EF Core** | attributes + `DbContext` configuration | snapshot; `AsNoTracking()` disables it | **expression trees → SQL** (this chapter's core) |
| **SQLAlchemy** | descriptors + metaclasses | Unit of Work + attribute interception | query trees built from Python objects |
| **Django ORM** | metaclasses scanning fields | field-level `has_changed` | lazily evaluated QuerySets |
| **Prisma** | schema file → **code generation** | explicit update (no implicit tracking) | the generated client emits SQL directly |

**Prisma's choice is worth noting**:

```text
It does【no implicit change tracking】— you must call update({ where, data }) explicitly
→ the cost: losing the convenience of "edit the object and it persists"
→ the benefit: no uncertainty about "what did it decide to write"; the SQL is fully predictable
→ this is the shared direction of the newer ORMs:【less magic, more explicitness】
```

---

## 12. Performance Analysis

### This chapter's numbers at a glance

```text
N+1 (Python):  201 statements / 17.6 ms  vs  1 / 2.0 ms → 201× fewer, 8.9× faster
N+1 (JS):      301 statements / 29.2 ms  vs  1 / 3.7 ms → 301× fewer, 8.0× faster
query side (C#): server-side 5 rows / 3.1 ms vs client-side 100000 rows / 266.3 ms → 86× slower
over-fetching:  5 columns 3.0 ms vs 2 columns 1.1 ms → 2.7×
boilerplate (Java): 11 lines per entity per direction vs 2 generic lines
```

### An ORM's three performance bills

```text
① statement count: N+1 is the biggest (measured 8–9× here; Ch. 47 measured 51× over a network)
② bytes transferred: fetching whole entities = SELECT * (Ch. 47 measured it closing covering indexes)
③ memory and CPU: change-tracking snapshots, proxy creation (turn off for read-only queries)
→ all three share one cause: an ORM works at the granularity of【objects】
  while a database works at the granularity of【sets】
```

> ⚠️ **Nearly every ORM performance problem comes from not looking at the generated SQL.** Every number in this chapter became visible only after enabling SQL logging — `set_trace_callback` (Python), `LogTo` (EF Core), `show_sql` (Hibernate). **Using an ORM without reading the log is writing SQL with your eyes closed.**

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Association queries | explicit `Include`/`select_related`/`fetch join` | relying on lazy loading | measured N+1 issuing 201/301 statements |
| List pages | projections (only the columns you need) | fetching whole entities | measured over-fetching 2.7×, and it equals `SELECT *` |
| Read-only queries | `AsNoTracking()` / read-only sessions | default tracking | saves one snapshot per entity |
| Filter predicates | keep `IQueryable` until the end | premature `.ToList()` | measured 86× slower, 20000× more rows |
| Returning to a view | convert to DTOs | returning entities | serialization triggers lazy loads outside the transaction |
| Bulk insert/update | raw SQL or bulk APIs | `save()` in a loop | one statement per save, all tracked |
| Complex reports | write the SQL directly | forcing it through the ORM | queries an ORM can't express translate poorly |
| Inheritance mapping | decide the strategy deliberately | accepting the default | measured trade-offs; changing later is expensive |
| Diagnosing performance | **enable SQL logging** | guessing | every number here required the log |
| Schema migrations | the ORM's tool plus human review | auto-generated straight to production | generated DDL can lock tables (Chapter 50) |

### The rule of thumb

```text
Before using the ORM, ask three things:
  ① Can the ORM express this query?  → if not, write SQL; don't force it
  ② Do I need the associations?      → if so,【explicitly】declare eager loading
  ③ Is this read-only?               → if so, disable tracking or use a projection
And afterwards, do one thing: look at the SQL it generated
```

---

## 14. Best Practices

- **Always enable SQL logging**: every number in this chapter required it — using an ORM without the log is writing SQL with your eyes closed.
- **Declare eager loading explicitly**: measured, lazy loading turned one for loop into 201 statements; make "should this association load" a visible line of code.
- **Use projections rather than entities for read-only queries**: it avoids `SELECT *` (Chapter 47: closes covering indexes) and skips change-tracking snapshots.
- **Keep `IQueryable` until the last moment**: measured, a premature `.ToList()` was 86× slower and transferred 20,000× the rows.
- **Convert to DTOs before returning to a view**: once an entity leaves the transaction boundary, lazy loading necessarily fails (JS measured the exception on session close).
- **Write complex queries in SQL**: forcing them through an ORM usually yields SQL that is both slower and harder to read.
- **Decide inheritance mapping up front**: each strategy has a real flaw (lost constraints / JOIN cost / UNION and key uniqueness), and changing after the data lands is expensive.
- **Treat an ORM as a boilerplate eliminator, not an excuse to skip SQL**: Java measured it saving 220 lines, but it cannot save you from understanding SQL.

---

## 15. Common Pitfalls

**Pitfall 1 · Accessing associations inside a loop**

```python
for u in users:
    total += sum(o.amount for o in u.orders)   # ⚠️ measured 201 statements
# ✅ eager load once: session.query(User).options(joinedload(User.orders))
```

**Pitfall 2 · Touching associations after the transaction**

```javascript
const u = session.users()[0];
session.close();
u.orders;    // ⚠️ measured: throws (the LazyInitializationException equivalent)
// ✅ fetch what you need inside the transaction, or convert to a DTO
```

**Pitfall 3 · Materializing too early**

```csharp
db.Users.ToList().Where(u => u.Score > 98);   // ⚠️ measured 86× slower, 20000× the rows
db.Users.Where(u => u.Score > 98).ToList();   // ✅
```

**Pitfall 4 · Forgetting to disable tracking on read-only queries**

```csharp
var list = db.Users.ToList();                  // ⚠️ one snapshot per entity
var list = db.Users.AsNoTracking().ToList();   // ✅
```

**Pitfall 5 · Calling save() in a loop**

```java
for (User u : tenThousandUsers) session.save(u);   // ⚠️ ten thousand INSERTs, all tracked
// ✅ bulk APIs / raw SQL; and flush + clear periodically
```

**Pitfall 6 · Serializing entities straight to JSON**

```text
⚠️ the serializer touches every property → triggers every lazy load → N+1 or an out-of-transaction error
⚠️ bidirectional associations also cause infinite recursion
✅ convert to a DTO and decide explicitly which fields to return
```

**Pitfall 7 · Forcing complex reports through an ORM**

```text
⚠️ deeply nested GroupBy/Having/window functions translate into long, slow SQL
✅ write these directly in SQL (Chapter 47); let the ORM map the results
```

---

## 16. Interview Questions

**Basic**

1. What is the impedance mismatch? Name at least three concrete faces of it.
2. What are an ORM's three core jobs? (Mapping, identity map, change tracking.)
3. What is an N+1 query? Why is it invisible in the source?

**Intermediate**

4. **How does an ORM map a class to a table? (Answer via Java's reflection and C#'s expression trees.)**
5. Why does lazy loading fail outside a transaction? What do modern ORMs use instead?
6. **How do `IQueryable` and `IEnumerable` differ? How costly is a misplaced `.ToList()`?**

**Advanced**

7. **Why does C++ have no mainstream ORM? (Explain via the absence of runtime reflection.)**
8. What are the trade-offs of the three inheritance mapping strategies? Why is "choosing wrong" expensive?
9. What does each change-tracking implementation (snapshot vs proxy) cost?

---

## 17. Exercises

**Basic**

1. Enable your ORM's SQL log and count how many statements one typical page issues.
2. Find an N+1, eliminate it with eager loading, and measure statement count and time before and after.
3. Add no-tracking to a read-only query and measure the difference.

**Intermediate**

4. **Reproduce key experiment one**: use reflection (or your language's equivalent) to write a micro-ORM that generates `INSERT`/`SELECT`.
5. Reproduce N+1 with real SQL counting (a trace callback or log) and confirm the count is 1+N.
6. Implement an identity map and change tracking; verify "one object per row" and "only changed columns updated."

**Challenge**

7. **Reproduce key experiment two**: write an expression-tree (or AST) to SQL translator supporting `AND`/`OR`/comparisons/`LIKE`.
8. Implement two of the three inheritance strategies and compare their query SQL and constraint strength.
9. Add eager loading to your micro-ORM (one JOIN fetching the root and its associations) and measure the speedup over lazy loading.

---

## 18. Chapter Summary

**One sentence**: an ORM is **a translator between the object world and the relational world**, translating five impedance mismatches (identity / association / collections / inheritance / load timing), with the means dictated by each language's features — this chapter took it apart with three key experiments: Java used **reflection** to read `@Table`/`@Column` and generate SQL (the entity class holding not one line of mapping code, measured saving about 220 lines of boilerplate), C# used **expression trees** to translate `u => u.Score > 90 && u.City == "city-3"` into `WHERE ((Score > 90) AND (City = 'city-3'))` (a hundred lines that are EF Core's core), and Python used real SQL counting to catch **N+1's moment of birth** (one for loop plus one property access → **201 statements**, versus 1 with a JOIN, 8.9× faster; JS reproduced 301 versus 1); and every trap an ORM has comes from its greatest selling point — **disguising database access as memory access**: lazy loading throws once an entity leaves the transaction boundary (measured in JS), and a misplaced `.ToList()` moves filtering from the server to the client (C# measured **20,000× more rows transferred and 86× slower**); finally a language-level rule — **the more a language can introspect at runtime, the more powerful and popular its ORMs** — which is why C++, lacking runtime reflection (Chapter 30's gap), still has no mainstream ORM.

**Key takeaways**

- **Five faces of the mismatch**: identity, association, collections, inheritance, load timing — every line an ORM saves maps to one of them.
- **Three core jobs**: mapping (class↔table), identity map (primary key↔reference equality), change tracking (edits↔UPDATE).
- **Key experiment one** (Java): reflection plus annotations generating SQL, measured saving ~220 lines of boilerplate.
- **Key experiment two** (C#): expression trees → SQL, a hundred lines that are EF Core's core; untranslatable expressions should error, not silently degrade.
- **Key experiment three** (Python/JS): N+1 emerging from one property access — 201 / 301 statements, 8–9× removable.
- **Lazy loading's cost** (JS measured): it fails the moment the entity leaves the transaction — the ORM's most typical abstraction leak.
- **Where the query executes** (C# measured): `.ToList()` one step early → 20,000× the rows, 86× slower.
- **Language features dictate the route**: reflection (Java) / expression trees (C#) / descriptors (Python) / code generation (JS) / macros (C++).

**Checklist**

- [ ] I can name the five faces of the mismatch and what an ORM does for each.
- [ ] I can explain why N+1 is invisible in the source and eliminate it three ways.
- [ ] I know when lazy loading fails and what to use instead.
- [ ] I can tell whether a query executes in the database or in memory.
- [ ] I enable SQL logging and can read the SQL my ORM generates.

**Part 7 complete**: from Chapter 46's "why we need a database" (the hand-written cost of durability, atomicity, concurrency control, indexing, and a query language), through Chapter 47's SQL (declarative, handing algorithm choice to the optimizer), Chapter 48's transactions (ACID and MVCC's three-line visibility rule), Chapter 49's indexes (fanout determines height), Chapter 50's locks (cycles in the wait-for graph), to this chapter's ORM — **Part 7's six chapters answer six layers of one question: how to keep data safely, correctly, and efficiently outside your process**.

**Next chapter**: Part 8, "Engineering," is the book's closing arc, applying everything so far to real projects. Chapter 52 begins with **testing** — which answers a question every previous chapter assumed but never argued: **how do you know the code is correct?** We will measure the real cost of each level of the test pyramid (unit tests in milliseconds, integration tests in seconds, end-to-end tests in minutes), quantify how much time test doubles save and how many real defects they hide, and explain why **test coverage is an exceptionally easy metric to misuse**.

---

## 19. Further Reading

- <a href="https://martinfowler.com/bliki/OrmHate.html" target="_blank" rel="noopener">Martin Fowler · OrmHate</a> — the most balanced response to "ORMs are an anti-pattern."
- <a href="https://en.wikipedia.org/wiki/Object%E2%80%93relational_impedance_mismatch" target="_blank" rel="noopener">Wikipedia · Object–relational impedance mismatch</a> — the full version of this chapter's table.
- <a href="https://martinfowler.com/eaaCatalog/identityMap.html" target="_blank" rel="noopener">Fowler · Identity Map</a> — the pattern's original definition (implemented in the Python example).
- <a href="https://martinfowler.com/eaaCatalog/unitOfWork.html" target="_blank" rel="noopener">Fowler · Unit of Work</a> — the source of change tracking and batched commits.
- <a href="https://docs.microsoft.com/en-us/ef/core/querying/client-eval" target="_blank" rel="noopener">EF Core · Client evaluation</a> — why EF Core 3.0 turned silent degradation into an error.
- <a href="https://docs.sqlalchemy.org/en/20/orm/queryguide/relationships.html" target="_blank" rel="noopener">SQLAlchemy · Relationship loading techniques</a> — the official comparison of eager-loading strategies.
- <a href="https://docs.jboss.org/hibernate/orm/current/userguide/html_single/Hibernate_User_Guide.html#fetching" target="_blank" rel="noopener">Hibernate · Fetching</a> — the authoritative account of lazy loading and `LazyInitializationException`.
- <a href="https://www.prisma.io/docs/orm/prisma-client/queries/relation-queries" target="_blank" rel="noopener">Prisma · Relation queries</a> — the newer "explicit over implicit" ORM design.
- <a href="https://martinfowler.com/eaaCatalog/" target="_blank" rel="noopener">Patterns of Enterprise Application Architecture</a> — the source of most patterns an ORM uses.
