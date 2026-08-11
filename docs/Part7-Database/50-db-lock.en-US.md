# Chapter 50 · Database Locks

[简体中文](./50-db-lock.md) ｜ **English**

---

> Chapter 48 kept mentioning row locks, gap locks, and needing `SELECT ... FOR UPDATE` against write skew, without unpacking any of them. Chapter 49 closed by saying "indexes also determine the scope of locks," another loose end. This chapter settles all of them.
>
> The **key experiment** hand-writes a lock manager — implementing the three things MySQL does every day: **compatibility checking, cycle detection on the wait-for graph, and victim selection**. The compatibility matrix has exactly one rule (reads are compatible with reads; the other three combinations conflict); deadlock detection is a DFS for a cycle in the wait-for graph; victim selection picks whoever holds the fewest locks. The measured output plays it out end to end: two transactions lock in opposite order → the wait-for graph shows `T1 → T2 → T1` → **a cycle is detected** → T1 (fewest locks) is rolled back → the other transaction proceeds immediately.
>
> The Java example stages the same deadlock on real threads and lets the JVM catch it with `findDeadlockedThreads()` — **and it does** (two `WAITING` threads, each holding what the other wants). That sets up an important contrast: Chapter 45's **thread starvation deadlock** and Chapter 48's **write skew** are both **invisible to the same tool**. "Databases handle deadlocks automatically" covers only the kind with a cycle in the wait-for graph.
>
> Lock granularity sets the ceiling on concurrency, and the C# example quantifies it: 4096 rows and 8 threads gave **a table lock (1 lock) 80 ms, striped locks (16) 17 ms, and row locks (4096) 13 ms — row locks 6.25× faster than a table lock**; yet single-threaded all three took 7 ms, because without contention locking costs tens of nanoseconds. But fine granularity is not free — a ten-million-row table with one lock per row would spend hundreds of megabytes on lock objects alone, which is exactly why **lock escalation** exists, and why "a bulk UPDATE suddenly froze the system" is such a common story.
>
> The Python example exposes sqlite's most insidious trap: with the default `BEGIN` (a deferred transaction), reading and then writing **fails when it tries to upgrade to a write lock** — because the snapshot is already stale. The database refuses the upgrade precisely to prevent a lost update; the fix is to start read-then-write transactions with `BEGIN IMMEDIATE`.
>
> Finally the JS example measures what to do after a `BUSY`: against the same 120 ms obstacle, **immediate retry made 17,641 attempts while exponential backoff made 8, and both took essentially the same total time (120 ms vs 127 ms)**. **Two thousand times more futile lock requests bought exactly zero speed** — all they do at scale is amplify one slow transaction into a site-wide avalanche.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- Write the **S/X compatibility matrix** and derive all lock behavior from one rule;
- Recite the full **deadlock detection** algorithm (wait-for graph → find a cycle → pick a victim) and explain why databases dare let deadlocks happen;
- Say which of three kinds of "stuck" tools can detect and which they cannot (this chapter vs Chapter 45 vs Chapter 48);
- Quantify **lock granularity**'s effect on concurrency (measured 6.25×) and explain why lock escalation cuts both ways;
- Handle lock conflicts correctly: **fixed lock ordering** to prevent deadlocks, **exponential backoff** as the safety net (measured: 2205× futile requests).

---

## 2. Why This Concept Exists

### Three loose ends from the last two chapters

```text
Ch. 48: "write skew needs SELECT ... FOR UPDATE" — but what lock does that actually take?
Ch. 48: "MySQL's RR blocks phantoms with gap locks" — what is a gap lock? Why does it cause deadlocks?
Ch. 49: "indexes also determine the scope of locks" — how exactly?
```

### What locks solve: the half MVCC cannot

```text
Ch. 48 measured: MVCC makes【reads】lock-free; readers and writers never block (0.05 ms, zero waiting)
But MVCC does not stop【write-write conflicts】— the C++ example measured it:
two transactions both wrote seat, the later one won, the earlier vanished quietly
→ so locks were not made obsolete by MVCC; they【retreated to the write side】
```

**Nearly everything in this chapter concerns writes**:

| Scenario | Lock needed? | Why |
|----------|-------------|-----|
| Pure reads | ❌ | MVCC gives you a snapshot (Chapter 48) |
| Pure writes (atomic statements) | the database handles it | `UPDATE ... SET v = v + 1` needs nothing from you |
| **Read then write** | ✅ **yes** | the value you read is the basis of the write |
| **Check then act** | ✅ **yes** | write skew (Chapter 48); MVCC cannot block it |

> **In one sentence**: MVCC freed reads from locking, and what remains — **who may write, in what order, and what to do when they wait on each other** — is this entire chapter.

---

## 3. How It Works

### Key experiment one: the compatibility matrix — one rule generates all behavior

```text
┌──────────────┬────────┬────────┐
│ held \ wanted │   S    │   X    │
├──────────────┼────────┼────────┤
│      S       │ ✓ ok   │ ✗ conflict │
│      X       │ ✗ conflict │ ✗ conflict │
└──────────────┴────────┴────────┘
```

```text
S = shared (read)   X = exclusive (write)
One rule: "reads don't conflict with reads; anything touching a write conflicts"
→ of four combinations only S+S coexists; the other three are mutually exclusive
```

**⚠️ But S locks are rarer than you'd think in modern databases**:

```text
MVCC (Chapter 48) means a plain SELECT【takes no lock at all】— it reads a snapshot
S locks appear mainly in SELECT ... FOR SHARE, foreign-key checks, and pre-MVCC engines
→ so the lock conflicts you meet day to day are almost all X-versus-X (write-write)
```

**The C++ example's normal flow**:

```text
T1 requests an S lock on 行A: ✓ granted
T2 requests an S lock on 行A: ✓ granted   ← concurrent reads
T3 requests an X lock on 行A: ✗ blocked   ← the writer waits for readers
After T1 and T2 commit and release, T3 retries: ✓ granted
```

### Key experiment two: the full deadlock detection algorithm

**Staging a deadlock** (two transactions touching the same rows in opposite order — the classic recipe):

```text
T1 acquire(行A, X): ✓
T2 acquire(行B, X): ✓
T1 acquire(行B, X): ✗ blocked   ← 行B is held by T2
T2 acquire(行A, X): ✗ blocked   ← 行A is held by T1
    lock table: 行A{T1:X} 行B{T2:X}
```

**Step one: build the wait-for graph** (A waits for resource R held by B → edge `A→B`):

```cpp
std::map<int, std::set<int>> wait_for_graph() const {
    std::map<int, std::set<int>> g;
    for (const auto& [txn, res] : waits_for_res_) {      // which resource I'm waiting on
        for (const auto& h : holders_.at(res))            // who holds that resource
            if (h.txn != txn) g[txn].insert(h.txn);       // draw an edge to them
    }
    return g;
}
```

```text
measured output:
  T1 ──waits──> T2
  T2 ──waits──> T1
```

**Step two: find a cycle** (DFS; returning to a node currently being visited means a cycle):

```cpp
if (visiting.count(v)) {                    // back to a node under visit = cycle
    auto pos = std::find(path.begin(), path.end(), v);
    cycle.assign(pos, path.end());
    return true;
}
```

```text
measured: ⚠️ cycle detected: T1 → T2 → T1  = deadlock
```

**Step three: pick a victim** (fewest locks held — cheapest to roll back; the same policy as InnoDB):

```text
measured: → victim: T1 (holds the fewest locks, cheapest rollback)
          → after rolling T1 back and releasing its locks, the other transaction proceeds: ✓
```

### Databases and your process treat deadlocks completely differently

**Chapter 41's four necessary conditions hold verbatim in a database**:

```text
① mutual exclusion —— X locks are exclusive by nature
② hold and wait    —— a transaction holds 行A while waiting for 行B
③ no preemption    —— locks are released only when the holder commits or rolls back
④ circular wait    —— T1→T2→T1
```

**But the strategies diverge completely**:

```text
Inside your process (Ch. 41): no rollback → deadlocks can only be【prevented】;
                              once one happens it hangs forever
Databases:                    they can roll back → they dare let deadlocks【happen】,
                              then detect and roll one side back
→ this is why database locks are fundamentally more powerful than in-process locks:
  they have an undo button
→ the cost is passed to you: you must【catch the error and retry the whole transaction】
```

**The Java example shows the JVM can detect it too** (`findDeadlockedThreads()`):

```text
⚠️ 2 threads deadlocked:
  事务-T1 state=WAITING  waiting on: ReentrantLock@24d46ca6  held by: 事务-T2
  事务-T2 state=WAITING  waiting on: ReentrantLock@36baf30c  held by: 事务-T1
→ the wait-for graph has a cycle → deadlock confirmed. Exactly the C++ algorithm.
```

**But it detects only this one kind of "stuck"** — the chapter's most important contrast:

| Phenomenon | Mechanism | Detectable? |
|-----------|-----------|-------------|
| **This chapter's deadlock** | two threads each hold a lock, each waits on the other → the graph **has a cycle** | ✅ yes |
| Chapter 45's **thread starvation deadlock** | threads wait for tasks, tasks wait for threads → no lock cycle | ❌ no |
| Chapter 48's **write skew** | the transactions never conflict; they merely violate a constraint logically | ❌ even less so |

```text
→ "databases handle deadlocks automatically"【covers only the first】
→ the other two are yours: split pools (Ch. 45) and SERIALIZABLE or a reshaped model (Ch. 48)
```

### Key experiment three: granularity sets the concurrency ceiling

**C# measured** (4096 rows, 20000 read-modify-writes per thread):

| Threads | Table lock (1) | Striped (16) | Row locks (4096) | Row vs table |
|---------|---------------|--------------|-----------------|--------------|
| 1 | 7 ms | 7 ms | 7 ms | 0.98× |
| 2 | 17 ms | 8 ms | 8 ms | 2.23× |
| 4 | 38 ms | 14 ms | 12 ms | 3.23× |
| **8** | **80 ms** | **17 ms** | **13 ms** | **6.25×** |

```text
① single-threaded all three are the same —— without contention, locking costs tens of nanoseconds
② the more threads, the worse coarse locks look —— a table lock【forces serialization】
③ striped locks use 1/256 the memory and already approach row-lock concurrency
→ granularity is the【ceiling】on concurrency: hence databases' relentless push for row locks
```

### But fine granularity is not free: lock escalation

```text
4096 rows → 4096 locks; the references alone cost 32 KB
a ten-million-row table with one lock per row → hundreds of MB in lock objects
```

```text
→ hence【lock escalation】: when one transaction locks too many rows,
  thousands of row locks are automatically replaced by【one table lock】
  it saves memory, and concurrency collapses instantly
→ SQL Server escalates around 5000 rows; InnoDB doesn't escalate,
  using bitmap-compressed lock information instead
→ this is the usual cause of "a bulk UPDATE suddenly froze the whole system"
```

### Locks and indexes: Chapter 49 closes here

```text
Row locks are taken on【index entries】, not on the abstract notion of "a row"
→ without a suitable index, the database can only【scan and lock every row it scans】
```

**SQL measured**:

```text
UPDATE seat SET taken=1 WHERE row_no = 3   (idx_row exists)
  → EXPLAIN: SEARCH ... USING INDEX idx_row
  → only the 10 matching rows are locked

UPDATE seat SET taken=1 WHERE taken = 0    (taken has no index)
  → EXPLAIN: SCAN seat
  → all 100 scanned rows are locked
```

**That is the real mechanism behind "an UPDATE without an index locks the whole table"** — Chapter 49's indexes determine not only query speed but **the scope of locks and the ceiling on concurrency**.

---

## 4. JavaScript

The JS example answers the most practical question: **what to do after a `SQLITE_BUSY`**.

### busy_timeout: how long before giving up (measured)

```text
busy_timeout=  0ms: failed, actually waited   0 ms
busy_timeout= 50ms: failed, actually waited  62 ms
busy_timeout=200ms: failed, actually waited 234 ms
```

```text
→ with busy_timeout=0 it fails immediately; with a value it【spins internally】until timeout
→ ⚠️ that wait happens inside sqlite, so your thread is【blocked】
  (Ch. 45: it occupies a pool slot; in Node it is worse — it blocks the whole event loop)
```

### Immediate retry versus exponential backoff (measured — this section's core)

Against the same obstacle, a lock held for 120 ms:

```text
immediate retry (no backoff): 17641 attempts, 120 ms total
exponential backoff:              8 attempts, 127 ms total (127 ms of backoff)
→ 2205× the attempts, essentially identical total time
```

**This comparison reveals something counterintuitive**:

```text
Those 17641 attempts【bought no speed at all】— the bottleneck is when the lock is released,
not how often you ask
Their only effect is to pound the database with two thousand times more futile lock requests
→ with one client it merely wastes CPU
→ at scale, N clients doing this amplifies one slow transaction into a site-wide avalanche (Ch. 45)
```

### A single-writer model cannot deadlock, but it can starve

```text
sqlite allows only one write transaction → no circular wait →【deadlock is impossible】
(the C++ example's cycle cannot form)
But another problem appears: among queued writers,【there is no fairness guarantee】
→ a stream of short transactions can keep cutting in, making one long transaction
  time out on BUSY repeatedly (writer starvation)
→ fix: queue at the application level (a single write channel) rather than
  letting N threads fight for one write lock
```

### When explicit locks are still needed (beyond MVCC)

```text
MVCC frees【reads】from locking, but these three still need explicit locks:
  ① read then write: the value read drives the write → BEGIN IMMEDIATE / SELECT FOR UPDATE
  ② check then act: constraints like "keep at least one admin" → write skew (Ch. 48)
  ③ generating unique values: "take a number and increment" → use a sequence or an atomic UPDATE
→ the test is simple: does what you read【determine what you're about to write】? Yes → lock.
```

> **Note**: `PRAGMA busy_timeout` is **per connection**, not global; better-sqlite3's `db.transaction(fn)` wraps `BEGIN/COMMIT` but **does not retry**; both `node:sqlite` and better-sqlite3 are synchronous, so a lock wait **blocks the entire event loop** (Chapter 43 measured that consequence) — write-heavy Node services should funnel writes into a single queue.

---

## 5. Python

The Python example exposes sqlite's most insidious trap.

### sqlite's lock model: one writer, many readers (measured)

```text
While A holds the write lock, B attempts to open a write transaction:
  ✗ rejected (waited 236 ms) → database is locked
But B reading the same row: 100 (0.178 ms, zero waiting) ← WAL lets reads proceed
```

```text
sqlite's granularity is【the whole database】: there is one write lock, and writers queue strictly
Versus PostgreSQL/MySQL's【row locks】: only writers to the same row conflict
(the C++ example implements row locks)
→ a sensible embedded trade: give up write concurrency for simplicity and zero configuration
```

### The insidious trap: a deferred transaction failing to upgrade (measured)

```python
A.execute("BEGIN")                       # DEFERRED: takes【no lock】at this point
v = A.execute("SELECT balance ...").fetchone()[0]     # reads 100
# B slips in, changes it to 999, and commits
A.execute(f"UPDATE ... SET balance = {v - 10} ...")   # ✗ fails
```

```text
A: BEGIN (deferred) → reads balance = 100   ← A holds only a【read lock】
B: slips in, sets it to 999, commits
A: now tries to write back the value it read minus 10: ✗ failed — database is locked
```

```text
The underlying reason: A's snapshot has expired — the 100 it saw is long stale
(sqlite's C API returns the extended code SQLITE_BUSY_SNAPSHOT;
 Python surfaces only "database is locked")
→ the database refuses the upgrade precisely to【prevent a lost update】(Ch. 46 measured that outcome)
```

**The fix: start read-then-write transactions with `BEGIN IMMEDIATE`** (measured):

```text
A: BEGIN IMMEDIATE → reads 999; B now tries to open a write transaction: ✗ kept out
A's write: ✓ succeeded, final balance = 989
→ cost: B is blocked throughout (lower concurrency)
→ benefit: A's read-modify-write cannot be invalidated midway
→ this is【pessimistic locking】: claim the spot before working
  (Chapter 48's C# example measured its boundary against optimistic locking)
```

### The busy_timeout trade-off (measured)

```text
timeout=0.05s: gave up after  63 ms
timeout=0.2s : gave up after 230 ms
timeout=0.5s : gave up after 570 ms
```

```text
Too small: ordinary lock waits start failing
Too large: request threads are tied up for a long time (Chapter 45's pool starvation)
→ in production: set it to the longest response time you can tolerate, and pair it with retries
```

> **Note**: `isolation_level=None` is required for manual transaction control; `BEGIN` is deferred (the snapshot is taken at the first read) while `BEGIN IMMEDIATE` takes the write lock at once — **that word is the entire source of this section's trap**; retry only retryable errors (`database is locked` / `busy`); a constraint violation will fail ten thousand times too.

---

## 6. Java

The Java example stages the deadlock on real threads and contrasts three kinds of "stuck."

### The JVM's deadlock detection (measured)

```java
long[] ids = mx.findDeadlockedThreads();
for (ThreadInfo info : mx.getThreadInfo(ids, true, true)) { ... }
```

```text
⚠️ 2 threads deadlocked:
  事务-T1 state=WAITING  waiting on: ReentrantLock@24d46ca6  held by: 事务-T2
  事务-T2 state=WAITING  waiting on: ReentrantLock@36baf30c  held by: 事务-T1
→ the wait-for graph has a cycle → deadlock. Exactly the algorithm the C++ example hand-writes.
```

### The three kinds of "stuck" (this chapter's most important table)

```text
This chapter's deadlock: two threads each hold a lock, each waits on the other
                         → the graph【has a cycle】→ detectable ✓
Chapter 45's starvation:  threads wait for【tasks】, tasks wait for【threads】
                         → no lock cycle → undetectable ✗
Chapter 48's write skew:  the transactions never conflict; they violate a constraint logically
                         → even less detectable ✗
→ "databases handle deadlocks automatically" covers only the first; the rest are yours
```

### Breaking circular wait: fixed lock ordering (measured, no deadlocks)

```java
rowC.lock();                      // ← every thread goes【C then D】
try { rowD.lock(); try { ... } finally { rowD.unlock(); } }
finally { rowC.unlock(); }
```

```text
two threads, 20000 double-row lock acquisitions each, fixed order: 7 ms, 40000 completed
new deadlocks during that: ✓ none (consistent ordering makes a cycle impossible)
```

**The correct way to write a transfer**:

```text
✓ always lock【by account id, ascending】
✗ "the payer first, then the payee" — that order depends on the data,
  so the opposite combination is guaranteed to occur
  (A→B locks 1 then 2; B→A locks 2 then 1; when they meet, deadlock)
```

### Timing out: the other escape route (measured)

```text
tryLock(200ms) result: ✓ gave up on timeout (no permanent hang)
→ corresponds to the database's innodb_lock_wait_timeout (MySQL's default is 50 seconds)
→ on timeout you receive a【retryable】error rather than hanging forever
```

> **Note**: `SELECT ... FOR UPDATE` / `FOR SHARE` **must be used inside a transaction**, or the lock vanishes when the statement ends; `Statement.setQueryTimeout(n)` is a statement timeout, different from a lock timeout; catch deadlocks via `SQLException.getSQLState() == "40001"`; this example marks the deadlocked threads as daemons, or the JVM could never exit — **itself evidence that in-process deadlocks are unrecoverable**.

---

## 7. C++

The C++ example is this chapter's key experiment: **a complete lock manager**.

### Three components

```cpp
/// ① the compatibility matrix: only read-read is compatible
static bool compatible(Mode held, Mode want) {
    return held == Mode::S && want == Mode::S;
}

/// ② acquire: on conflict, record an edge in the wait-for graph
bool acquire(int txn, const std::string& res, Mode want) {
    for (const auto& h : holders_[res])
        if (!compatible(h.mode, want)) {
            waits_for_res_[txn] = res;              // record which resource I wait on
            return false;
        }
    holders_[res].push_back({txn, want});
    return true;
}

/// ③ pick a victim: fewest locks held (cheapest rollback)
int pick_victim(const std::vector<int>& cycle) const;
```

**These three pieces are the teaching edition of InnoDB's lock module** — a real implementation also handles lock escalation, intention locks, gap locks, and wait-queue fairness, but **the core structure is identical**.

### Why databases detect rather than prevent

```text
Preventing deadlock means breaking one of the four conditions:
  ① mutual exclusion → impossible; X locks exist to be exclusive
  ② hold and wait    → would require acquiring all locks up front, but which rows a
                       transaction locks is often known only at runtime
  ③ no preemption    → allowing lock stealing means allowing isolation violations
  ④ circular wait    → requires a global ordering, which a database cannot force on applications
→ the first three are unworkable and the fourth needs【your】cooperation
→ so databases choose: let deadlock happen, then detect and roll back
  (they can roll back — that is their confidence)
```

### Breaking circular wait (measured)

```text
the same two transactions, but both accessing in the fixed order【行A → 行B】:
  T1 acquire(行A): ✓
  T2 acquire(行A): ✗ blocked   ← T2 waits at step one, holding nothing yet
  T1 acquire(行B): ✓
  deadlock check: ✓ no cycle — deadlock impossible
```

**The key is that T2 begins waiting while holding no locks at all** — which breaks "hold and wait" and "circular wait" simultaneously.

> **Note**: a real lock manager also has **intention locks** (IS/IX) — table-level markers saying "I hold locks on some rows in here," so a transaction wanting a table lock needn't check every row; gap locks and next-key locks are InnoDB's way of blocking phantoms; wait queues are usually FIFO to avoid starvation (this toy has no queue).

---

## 8. C#

The C# example quantifies **lock granularity**, the design choice that most affects concurrency.

### Throughput across three granularities (measured)

| Threads | Table lock (1) | Striped (16) | Row locks (4096) | Row vs table |
|---------|---------------|--------------|-----------------|--------------|
| 1 | 7 ms | 7 ms | 7 ms | 0.98× |
| 2 | 17 ms | 8 ms | 8 ms | 2.23× |
| 4 | 38 ms | 14 ms | 12 ms | 3.23× |
| **8** | **80 ms** | **17 ms** | **13 ms** | **6.25×** |

**Note the single-threaded row**: all three are identical. **A lock's cost is not in locking but in waiting** — without contention, acquire and release take tens of nanoseconds.

### Striped locks: the engineering compromise

```text
16 locks covering 4096 rows via hash(rowId) % 16
→ 1/256 the memory of row locks, with concurrency already close (17 ms vs 13 ms)
→ the cost:【false conflicts】— two rows landing on the same stripe wait on each other
→ Java's ConcurrentHashMap (JDK7) and sharded counters use exactly this idea
```

### Reader-writer locks: an honest counterintuitive result (measured)

```text
8 threads × 20000 operations (90% reads / 10% writes):
  plain mutex:        3 ms
  reader-writer lock: 5 ms (1.42× slower)
```

**The reader-writer lock is slower**:

```text
In theory reads are compatible, so a read-heavy workload should favor it
But a reader-writer lock's own【bookkeeping】is not cheap
(maintaining reader counts, handling writer priority, and so on)
With a very short critical section, that overhead exceeds the concurrency gain
→ databases sidestep this entirely: MVCC makes reads【take no lock at all】(Ch. 48),
  which is more thorough than any smarter lock
```

**This also explains a historical arc**: early databases used S locks for read concurrency, while modern ones have moved almost entirely to MVCC — **because "no lock" always beats "a smarter lock."**

### How four systems handle deadlocks

| System | What happens | What you must do |
|--------|-------------|------------------|
| Inside your process | hangs forever (Chapter 41) | guarantee lock ordering yourself |
| InnoDB | auto-detect + roll one side back | catch the error and **retry the whole transaction** |
| PostgreSQL | auto-detect + roll one side back | the same (`deadlock_timeout`) |
| SQLite | cannot deadlock (single writer) | handle `SQLITE_BUSY` and retry |

> **Note**: EF Core can only lock via raw SQL (`FromSqlRaw("SELECT ... FOR UPDATE")`); catch deadlocks via `SqlException.Number == 1205` (SQL Server) or `PostgresException.SqlState == "40P01"`; retries must cover **the whole transaction** (including the first read, since the snapshot must be retaken).

---

## 9. SQL

### Explicit locking syntax (cross-database)

```sql
SELECT ... FOR UPDATE              -- take an X lock (PostgreSQL / MySQL / Oracle)
SELECT ... FOR SHARE               -- take an S lock (MySQL 8; older: LOCK IN SHARE MODE)
SELECT ... FOR UPDATE NOWAIT       -- error immediately instead of waiting
SELECT ... FOR UPDATE SKIP LOCKED  -- skip locked rows (the standard task-queue pattern)
-- sqlite: none of these; use BEGIN IMMEDIATE instead (measured in Python)
```

### `SKIP LOCKED`: the right way to build a task queue

```text
With N workers competing for tasks, a plain FOR UPDATE means:
  workers 2..N all【queue behind worker 1】→ concurrency degrades to 1
With FOR UPDATE SKIP LOCKED:
  each worker skips rows others have locked and takes its own → real concurrency
```

```sql
SELECT id FROM job WHERE state='pending'
ORDER BY id LIMIT 10 FOR UPDATE SKIP LOCKED;
```

**This is the only correct way to use a database as a task queue** — without it, multiple consumers queue rather than consume in parallel.

### Gap locks: the price of blocking phantoms (Chapter 48's setup)

```text
Ordinary row locks can lock only【existing rows】; they cannot stop someone【inserting】(a phantom)
Gap locks lock the【gaps between rows】: lock (10, 20) and nobody can insert 15

Cost one: the locked range is far wider than the rows you actually read
Cost two:【they easily create unexpected deadlocks】— two transactions locking overlapping ranges
→ PostgreSQL uses no gap locks, blocking phantoms with MVCC snapshots
  (the root of Chapter 48's measured difference between the two RRs)
```

### Why read-then-write needs a lock

```sql
-- ⚠️ dangerous
SELECT balance FROM account WHERE id=1;      -- reads 100
-- another transaction may already have changed it
UPDATE account SET balance = 90 WHERE id=1;  -- writes based on a【stale value】

-- ✓ correct option one: let the database compute (atomic, no read needed)
UPDATE account SET balance = balance - 10 WHERE id = 1;
-- ✓ correct option two: lock at read time
SELECT balance FROM account WHERE id=1 FOR UPDATE;
```

### Choosing among three strategies

```text
pessimistic (FOR UPDATE): frequent conflicts, short transactions, must succeed
                          → stock deduction, seat reservation
optimistic (version column): rare conflicts, or【cross-request】edits
                          → form saving (Chapter 48's measured boundary)
lock-free (atomic statements): if it fits in one UPDATE, don't read
                          → SET balance = balance - 10
→ priority: lock-free > optimistic > pessimistic.
  Avoid locking when you can — the first principle of concurrent design.
```

> **Note**: `FOR UPDATE` behaves differently across databases on a `LEFT JOIN`'s right table (PostgreSQL needs `OF table_name`); `NOWAIT` and `SKIP LOCKED` are SQL:2016 and supported by MySQL 8.0+ and PostgreSQL 9.5+; deadlock error codes: MySQL 1213, PostgreSQL 40P01, SQLSTATE 40001.

---

## 10. Cross-Language Comparison

### ① Lock-related capabilities

| Capability | SQL | Java | C# | Python | JavaScript |
|-----------|-----|------|-----|--------|-----------|
| Explicit row locks | `FOR UPDATE` | via SQL | via raw SQL | `BEGIN IMMEDIATE` (sqlite) | same |
| Deadlock detection | database does it | **`findDeadlockedThreads()`** | none built in | none built in | none built in |
| Lock timeouts | `innodb_lock_wait_timeout` | `tryLock(t)` | `Monitor.TryEnter(t)` | `timeout=` | `PRAGMA busy_timeout` |
| Reader-writer locks | S/X locks | `ReentrantReadWriteLock` | `ReaderWriterLockSlim` | none built in | none |
| Skipping locked rows | **`SKIP LOCKED`** | — | — | — | — |
| Deadlock error codes | 40001 / 1213 / 40P01 | `SQLState` | `SqlException.Number` | `OperationalError` | exception message |

### ② Key experiment one: the deadlock detection algorithm (C++ measured)

```text
wait-for graph: T1 ──waits──> T2 ──waits──> T1
detection: DFS finds the cycle T1 → T2 → T1 = deadlock
victim: T1 (fewest locks) → rolled back → the other side proceeds ✓
```

### ③ Key experiment two: only one of three "stucks" is detectable (Java measured)

```text
this chapter's deadlock (cycle in the graph) → findDeadlockedThreads() ✓ caught 2 WAITING threads
Chapter 45's thread starvation deadlock      → ✗ undetectable (no lock cycle)
Chapter 48's write skew                      → ✗ undetectable (no conflict at all)
```

### ④ Key experiment three: granularity sets concurrency (C# measured)

```text
8 threads: table 80 ms / striped 17 ms / row 13 ms → row locks 6.25× faster
1 thread:  all three 7 ms → a lock's cost is not in locking but in【waiting】
striped:   1/256 the memory, near row-lock concurrency — the engineering optimum
```

### ⑤ Key experiment four: the value of backoff (JS measured)

```text
against the same 120 ms obstacle:
  immediate retry: 17641 attempts, 120 ms
  exponential backoff:  8 attempts, 127 ms
→ 2205× the futile requests for 0 speedup
```

### ⑥ Common ground and root causes

**Common ground**: every language offers mutexes and timed acquisition; **only Java has built-in deadlock detection**; and no language's in-process locks can **roll back** — so their deadlocks can only be prevented, never recovered from.

**Root causes**:

- **Databases dare let deadlocks happen because they can roll back** — transactions are their confidence, while in-process locks have no undo button;
- **Java has built-in detection** because the JVM owns all lock information (both `synchronized` and `java.util.concurrent` locks register with it), while other languages' locks are library-level and invisible to the runtime;
- **Only SQL has `SKIP LOCKED`** because only a database manages both the data and who is locking it, and can skip during a scan;
- **Modern databases moved to MVCC rather than smarter reader-writer locks** — the C# measurement shows why: a reader-writer lock's bookkeeping makes it slower on short critical sections. **"No lock" always beats "a smarter lock."**
- **sqlite's single-writer choice** is a sensible embedded trade: give up write concurrency for "deadlock is impossible" and zero configuration.

---

## 11. Implementation Comparison

| Database | Granularity | Deadlock handling | Key details |
|----------|------------|------------------|-------------|
| **SQLite** | database-wide write lock | cannot deadlock | single writer; WAL lets readers proceed |
| **MySQL/InnoDB** | row + gap + intention locks | wait-for graph detection; rolls back the side that **changed fewer rows** | locks on index entries; without an index, all scanned rows |
| **PostgreSQL** | row + table-level intention | detects only after `deadlock_timeout` (default 1s) | no gap locks; blocks phantoms with MVCC |
| **SQL Server** | row/page/table, **escalates** | detects + rolls back the cheaper side | escalation around 5000 rows |
| **Oracle** | row locks | detects immediately | reads never lock (one of the earliest MVCC implementations) |

**PostgreSQL's deferred detection deserves a mention**:

```text
It does not run cycle detection on every lock wait (too expensive)
Instead it waits deadlock_timeout (default 1 second), then builds the graph and checks
→ because the vast majority of lock waits are【normal brief waits】not worth a full graph scan
→ the cost: a real deadlock takes an extra second to clear
```

---

## 12. Performance Analysis

### This chapter's numbers at a glance

```text
granularity (8 threads): table 80 ms / striped 17 ms / row 13 ms → 6.25×
granularity (1 thread):  all three 7 ms → without contention, locking is negligible
striped memory:          1/256 of row locks, near row-lock concurrency
reader-writer lock:      3 ms (mutex) vs 5 ms (RW lock) → RW is【slower】by 1.42× on short sections
backoff:                 17641 vs 8 attempts, same total time (120 vs 127 ms)
busy_timeout:            settings 0/50/200 ms → actual waits 0/62/234 ms
WAL reads don't block:   with an uncommitted write lock held, a reader takes 0.037 ms
fixed ordering:          2 threads × 20000 double-row acquisitions, 7 ms, zero deadlocks
```

### Three levels of lock contention

```text
① no contention     → locking costs tens of nanoseconds, negligible
                      (measured: all three granularities equal at 1 thread)
② light contention  → brief waits, throughput dips slightly (measured at 2–4 threads)
③ heavy contention  →【serialization】; throughput【degrades】as threads increase
                      (measured: table lock, 8 threads, 80 ms)
→ the first step in optimizing locks is always: confirm you are actually at level ③
```

> ⚠️ **The most common mistake in lock optimization is optimizing at level ①.** Without contention, swapping a mutex for a reader-writer lock, a lock-free structure, or finer granularity only **adds** overhead — the C# measurement of a reader-writer lock being 1.42× slower is a live example. **Measure contention first, then decide whether to touch the locks.**

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Counters / balances | atomic `UPDATE ... SET v = v + 1` | read into memory, modify, write back | lock-free is fastest and loses nothing |
| Read then write (stock) | `SELECT ... FOR UPDATE` | unlocked read then write | a change in between makes it wrong |
| Cross-request edits | optimistic locking (version column) | holding a database lock | you cannot hold one for 5 minutes (Ch. 48) |
| Multi-row transactions | **fixed order (e.g. ascending id)** | business order (payer first) | measured: consistent order makes deadlock impossible |
| Task queues | `FOR UPDATE SKIP LOCKED` | plain `FOR UPDATE` | the latter serializes your consumers |
| Bulk updates | commit in batches (hundreds of rows) | one update over a million rows | triggers lock escalation; concurrency collapses |
| `UPDATE`/`DELETE` predicates | **ensure an index exists** | unindexed conditions | measured: all scanned rows get locked |
| Lock wait timeout | the longest response time you tolerate | the default (possibly 50 s) | too long exhausts the connection pool (Ch. 45) |
| Retrying | **exponential backoff + a cap** | immediate infinite retries | measured 2205× futile requests, zero gain |
| Retry scope | the **whole transaction** | just the failed statement | the snapshot must be retaken (Ch. 48) |
| sqlite read-then-write | `BEGIN IMMEDIATE` | the default `BEGIN` | measured: the write-lock upgrade fails |
| Diagnosing lock problems | the database's lock views | guessing | `pg_locks` / `sys.innodb_lock_waits` |

### The rule of thumb

```text
① Can it be one atomic UPDATE?     → yes → no lock needed (optimal)
② Must you read before writing?    → yes → FOR UPDATE or BEGIN IMMEDIATE
③ Touching multiple rows?          → yes → lock in a fixed order (ascending id)
④ Does the predicate column have an index? → no → build one, or you lock the whole table
⑤ What on conflict?                → catch + exponential backoff + retry the whole transaction
```

---

## 14. Best Practices

- **Avoid locking when you can**: the priority is always "lock-free atomic statement > optimistic > pessimistic" — measured, a reader-writer lock is 1.42× *slower* on short critical sections.
- **Lock multiple rows in a fixed order**: the only practical deadlock prevention (measured: consistent ordering makes a cycle impossible); transfers lock by ascending `id`, not "payer first."
- **Ensure `UPDATE`/`DELETE` predicate columns are indexed**: measured, without one every scanned row is locked — Chapter 49's indexes govern **both query speed and lock scope**.
- **Retries must back off exponentially**: measured, 17641 attempts without backoff took the same time as 8 with it — **two thousand times the futile requests for zero gain**, plus avalanche risk at scale.
- **Retry the whole transaction, not one statement**: the snapshot must be retaken, or you redo the work on stale data.
- **Use `BEGIN IMMEDIATE` for sqlite read-then-write**: the default deferred transaction fails on the write-lock upgrade (measured), and that failure is hard to reproduce in testing.
- **Commit bulk updates in batches**: locking too many rows at once triggers escalation (SQL Server around 5000 rows) and concurrency collapses.
- **Measure contention before optimizing locks**: without contention locking costs tens of nanoseconds (measured: all three granularities equal at one thread), so any "lock optimization" is pure overhead.

---

## 15. Common Pitfalls

**Pitfall 1 · Locking in business order causes deadlocks**

```sql
-- ⚠️ A→B locks 1 then 2; B→A locks 2 then 1; when they meet, deadlock
UPDATE account SET balance = balance - 10 WHERE id = :from;
UPDATE account SET balance = balance + 10 WHERE id = :to;
-- ✅ lock min(from,to) first, then max(from,to)
```

**Pitfall 2 · An unindexed UPDATE locks the whole table**

```sql
UPDATE seat SET taken = 1 WHERE taken = 0;   -- ⚠️ no index on taken → every scanned row is locked
-- ✅ index the predicate column first (Chapter 49)
```

**Pitfall 3 · Retrying without backoff**

```python
while True:
    try: do_txn(); break
    except OperationalError: pass    # ⚠️ measured 17641 futile attempts, zero gain
```

**Pitfall 4 · sqlite read-then-write with the default `BEGIN`**

```python
con.execute("BEGIN")                 # ⚠️ deferred; only a read lock
v = con.execute("SELECT ...").fetchone()[0]
con.execute(f"UPDATE ... {v-10} ...")  # ⚠️ write-lock upgrade fails: database is locked
# ✅ con.execute("BEGIN IMMEDIATE")
```

**Pitfall 5 · A task queue with plain `FOR UPDATE`**

```sql
SELECT id FROM job WHERE state='pending' LIMIT 1 FOR UPDATE;  -- ⚠️ N consumers queue up
-- ✅ ... FOR UPDATE SKIP LOCKED
```

**Pitfall 6 · Retrying only the failed statement**

```python
except Deadlock:
    cur.execute(failed_sql)      # ⚠️ the transaction already rolled back; this redoes it on a stale snapshot
# ✅ retry the whole transaction from BEGIN
```

**Pitfall 7 · A lock wait timeout set too high**

```text
innodb_lock_wait_timeout = 50   # ⚠️ request threads tied up for 50 s → pool exhaustion (Ch. 45)
```

---

## 16. Interview Questions

**Basic**

1. What is the S/X compatibility matrix? Summarize it in one sentence.
2. What are deadlock's four necessary conditions? Which one do databases break?
3. How does `SELECT ... FOR UPDATE` differ from a plain `SELECT`? Why must it be inside a transaction?

**Intermediate**

4. **Describe the full deadlock detection algorithm, from wait-for graph to victim selection.**
5. How does granularity affect concurrency? What is lock escalation and why does it cut both ways?
6. **Why do databases dare let deadlocks happen while in-process locks do not? (Hint: rollback.)**

**Advanced**

7. **Why does "an UPDATE without an index lock the whole table"? (Explain via "locks are taken on index entries.")**
8. What problem do gap locks solve? What are their two costs? Why does PostgreSQL avoid them?
9. Why did modern databases move to MVCC rather than smarter reader-writer locks?

---

## 17. Exercises

**Basic**

1. Reproduce a lock wait with two connections and observe the timeout error.
2. Run a bulk `UPDATE` before and after indexing the predicate column, observing the change in lock scope.
3. Write a read-then-write routine and run a concurrency test with and without `FOR UPDATE`.

**Intermediate**

4. **Reproduce key experiment one**: implement a wait-for graph plus cycle detection and have the program find the deadlock and pick a victim.
5. Reproduce the granularity experiment: protect the same data with 1 versus N locks and measure how concurrency scales with threads.
6. Build a transaction retrier with exponential backoff and count the difference in attempts with and without it.

**Challenge**

7. **Add a wait queue and fairness to your lock manager** (this chapter's toy has no queue and starves latecomers).
8. Build a multi-consumer task queue with `FOR UPDATE SKIP LOCKED` and verify the consumers truly run in parallel.
9. Construct a gap-lock deadlock: two transactions inserting different values yet waiting on each other, and explain the shape of the wait-for graph.

---

## 18. Chapter Summary

**One sentence**: MVCC freed reads from locking (Chapter 48), and the remaining battlefield — **write-write conflicts** — is this whole chapter; the hand-written lock manager takes apart the three things databases do daily: the **compatibility matrix** has one rule (reads compatible with reads, everything else conflicts), **deadlock detection** is a DFS for a cycle in the wait-for graph (measured `T1 → T2 → T1`), and **victim selection** picks whoever holds the fewest locks; the Java example had the JVM catch the same deadlock with `findDeadlockedThreads()`, yielding this chapter's most important contrast — **Chapter 45's thread starvation and Chapter 48's write skew are both invisible to that same tool**, so "databases handle deadlocks automatically" covers only the cycle-in-the-graph kind; the C# example quantified that **granularity is the ceiling on concurrency** (row locks **6.25×** faster than a table lock at 8 threads, while all three tie single-threaded — **a lock's cost is not in locking but in waiting**) and explained why escalation cuts both ways; the Python example exposed sqlite's deferred transactions **inevitably failing to upgrade** to a write lock; and the JS example delivered the most practical lesson — against the same obstacle, **17,641 immediate retries and 8 backed-off retries took the same time**, so two thousand times the futile requests bought nothing and only risk amplifying one slow transaction into an avalanche.

**Key takeaways**

- **One rule for the matrix**: reads don't conflict with reads; anything touching a write conflicts — though S locks are rare under MVCC.
- **Deadlock detection in three steps** (measured): build the wait-for graph → DFS for a cycle → roll back whoever holds the fewest locks.
- **Databases dare let deadlocks happen** because they can roll back; in-process locks have no undo and must prevent.
- **Only one of three "stucks" is detectable**: cycle in the graph ✓; thread starvation ✗; write skew ✗.
- **Granularity sets the concurrency ceiling** (measured 6.25×); fine granularity costs memory, hence escalation.
- **A lock's cost is in waiting, not locking** (measured: all three granularities tie at 7 ms single-threaded).
- **Locks are taken on index entries** (measured): an unindexed predicate locks every scanned row.
- **The value of backoff** (measured): 17641 versus 8 attempts, same time — no backoff is pure waste and genuinely dangerous.

**Checklist**

- [ ] I can write the S/X compatibility matrix and summarize it in one sentence.
- [ ] I can recite the three-step deadlock detection algorithm.
- [ ] I know which kinds of "stuck" tools can detect and which they cannot.
- [ ] I can explain granularity's effect on concurrency and escalation's two faces.
- [ ] I use fixed lock ordering to prevent deadlocks and exponential backoff as a net.

**Next chapter**: Part 7's final chapter returns to the application side. The last five chapters were all about how a database thinks — tables, rows, SQL, transactions, locks; but your code thinks in **objects** — classes, fields, collections, inheritance. Chapter 51 covers **ORM**: the gap between these two worldviews is called the impedance mismatch, and we will measure how much boilerplate an ORM saves you as well as what it quietly does on your behalf — **how an N+1 query emerges from a single property access** (Chapter 47 measured its 51× cost), why lazy loading explodes outside a transaction, and why "the SQL your ORM generated" deserves a look in the log every single time.

---

## 19. Further Reading

- <a href="https://dev.mysql.com/doc/refman/8.0/en/innodb-locking.html" target="_blank" rel="noopener">MySQL Docs · InnoDB Locking</a> — the authoritative account of row, gap, intention, and next-key locks.
- <a href="https://dev.mysql.com/doc/refman/8.0/en/innodb-deadlocks.html" target="_blank" rel="noopener">MySQL Docs · InnoDB Deadlocks</a> — deadlock detection and how to read `SHOW ENGINE INNODB STATUS`.
- <a href="https://www.postgresql.org/docs/current/explicit-locking.html" target="_blank" rel="noopener">PostgreSQL Docs · Explicit Locking</a> — every lock mode and their compatibility matrix.
- <a href="https://www.sqlite.org/lockingv3.html" target="_blank" rel="noopener">SQLite · File Locking And Concurrency</a> — the complete description of the single-writer model.
- <a href="https://en.wikipedia.org/wiki/Two-phase_locking" target="_blank" rel="noopener">Wikipedia · Two-phase locking</a> — 2PL, the dominant approach before MVCC.
- <a href="https://en.wikipedia.org/wiki/Deadlock_(computer_science)" target="_blank" rel="noopener">Wikipedia · Deadlock</a> — the four conditions and the range of handling strategies.
- <a href="https://en.wikipedia.org/wiki/Lock_(computer_science)" target="_blank" rel="noopener">Wikipedia · Lock</a> — lock taxonomy and granularity levels.
- <a href="https://www.2ndquadrant.com/en/blog/postgresql-anti-patterns-read-modify-write-cycles/" target="_blank" rel="noopener">PostgreSQL Anti-Patterns · Read-Modify-Write Cycles</a> — the practical version of this chapter's "read-then-write needs a lock."
- <a href="https://dataintensive.net/" target="_blank" rel="noopener">Designing Data-Intensive Applications</a> — Chapter 7's discussion of two-phase locking and serializability.
