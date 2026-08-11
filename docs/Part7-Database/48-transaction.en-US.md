# Chapter 48 · Transactions

[简体中文](./48-transaction.md) ｜ **English**

---

> Chapter 46 measured what a transfer crashing midway costs — a file left holding `'id=1,balance=40\nid=2,bal'`, with money vanished. That was only the **A** in ACID. This chapter opens all four letters and answers a harder question: **when many people read and write the same data at once, how does each of them get to act as if they owned it alone?**
>
> This chapter's **key experiment** reproduces the isolation divide with two real database connections. Without a transaction, connection A reading the same row twice gets **100 → 777** — a **non-repeatable read**. Wrapped in `BEGIN`, even after B changes the value to 999 and **commits**, A's second read still returns **100**, and only sees 999 after A itself commits. Range queries behave the same: two `COUNT`s inside the transaction both return 3, while outside it immediately returns 4 — **phantom reads** blocked too. That is **snapshot isolation**: you see the entire database as of the instant your transaction began.
>
> How is that done? The C++ example **hand-writes an MVCC engine** to answer, and the answer is startlingly short — **the visibility rule is three lines**: versions newer than me are invisible, versions that died before I began are invisible, everything else is visible. Those three lines replace the entire read-lock apparatus, which is why **reads are never blocked by writes** (measured in Python: while B holds an uncommitted write lock, A reads the same row in **0.05 ms with zero waiting**). The cost is measured too: after 100 more writes the version chain reaches **102 entries** — old versions cannot be deleted immediately, which is exactly why PostgreSQL needs `VACUUM` and why long transactions are MVCC's natural enemy.
>
> But snapshot isolation has an anomaly it **cannot** block, and it targets code that looks perfectly correct: **write skew**. The JS example demonstrates it with hospital scheduling — two doctors request leave simultaneously, each reads "2 doctors still on call" and passes its check, and together they leave zero on call. **Neither transaction wrote the same row**, so write-write conflict detection never sees it. sqlite blocks it because it only has SERIALIZABLE (measured: B was outright rejected), but PostgreSQL's and MySQL's default levels do not.
>
> Finally, an **empirical test of a popular claim**. "Optimistic locking wins at low contention, pessimistic wins at high contention" — the C# example ran both across four contention levels, and only the hot-row case was stably reproducible (**pessimistic 1.5–1.9× faster**, with optimistic doing fifty thousand wasted retries); the other three flip direction between runs, which is noise, not a rule. The reason: acquiring an in-memory lock costs tens of nanoseconds, so optimistic locking has almost nothing to save. **Optimistic locking's real value is not speed** — it is that it can span boundaries **where holding a lock is impossible**: a user opens an edit form, thinks for five minutes, then submits; you cannot hold a database lock for five minutes, and a version column is the only way.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- Name **what mechanism implements each letter** of ACID, and identify which one requires your participation;
- Reproduce and distinguish the three read anomalies (**dirty read, non-repeatable read, phantom read**);
- Use the three-line visibility rule to explain why **MVCC** lets reads and writes stop blocking each other, and name its cost (version accumulation);
- Recognize **write skew** — the anomaly snapshot isolation cannot block, which targets code that looks correct;
- Choose between **pessimistic and optimistic** locking on the right basis — not "which is faster" but "can you hold a lock at all."

---

## 2. Why This Concept Exists

### The half-answer Chapter 46 left

```text
Ch. 46 measured: the file-based transfer crashing midway → 'id=1,balance=40\nid=2,bal'
                 the database under the same crash        → rolled back, both balances 100
→ that chapter proved【atomicity】matters, but covered only the A in ACID
```

**The real difficulty is concurrency**:

```text
With one user, you need only A (crashes roll back) and D (commits survive)
With ten thousand simultaneous users you also need I (isolation) —
otherwise everyone sees everyone else's half-finished states
```

### A transaction's definition: four promises about a group of operations

```text
A Atomicity   —— all or nothing (the Java example implements it with a hand-written undo log)
C Consistency —— invariants stay unbroken (Ch. 46 measured CHECK blocking -999)
I Isolation   —— concurrent execution yields a result equal to some serial execution
D Durability  —— once committed, never lost (Ch. 46 measured three tiers: 1.86/26/4399 μs)
```

**Only C is yours**:

```text
A/I/D are the database's job; you only write BEGIN/COMMIT
C is your job: the database can only enforce constraints【you declare】—
it has no idea "balances cannot go negative" unless you write CHECK
→ which is why C is the most misunderstood letter: it isn't a gift, it's your definition
```

> **In one sentence**: a transaction turns "a group of operations" into an indivisible unit, moving the two hardest problems — concurrency and crashes — from **every application handling them separately** to **the database handling them once**. This chapter proves it handles them far better than your hand-rolled version, **but not without holes**.

---

## 3. How It Works

### Key experiment one: three read anomalies and snapshot isolation

**Python reproduced them with two real connections** (sqlite in WAL mode):

**① Non-repeatable read** (no transaction wrapper)

```text
A's first read: 100   after B commits, A's second read: 777
→ two reads in the same logic returning different values
→ consequence: "check the balance, then deduct" — the balance changed in between
  and your check is already stale
```

**② Wrapped in a transaction → snapshot isolation**

```text
first read inside the transaction: 100
after B commits 999, second read inside the transaction: 100   ← still the old value!
after A commits, reading again: 999
→ two reads inside a transaction agree =【snapshot isolation】
```

**A sees the entire database as of the instant its transaction began**; B's commit is completely invisible to it.

**③ Phantom reads blocked as well**

```text
two COUNTs inside the transaction: 3 → 3 (consistent); outside afterward: 4
```

**Distinguishing the three precisely** (many people conflate them):

| Anomaly | What was read | Example |
|---------|--------------|---------|
| **Dirty read** | Another transaction's **uncommitted** data | reading a transfer that may still roll back |
| **Non-repeatable read** | An **existing row** changed | the same row reading 100 then 777 |
| **Phantom read** | A **new row** appeared | the same range counting 3 then 4 |

```text
The key difference: non-repeatable read is【row contents changed】,
phantom read is【the row set changed】
→ that difference sets the implementation difficulty: locking existing rows is easy,
  locking "rows that don't exist yet" is hard (it needs gap locks)
```

### Key experiment two: hand-written MVCC — the visibility rule is three lines

**Why can reads avoid being blocked by writes? The C++ engine answers.**

**The core data structure**: a row is not a value but a **version chain**:

```cpp
struct Version {
    int value;
    uint64_t created_by;    // which transaction created it
    uint64_t deleted_by;    // which transaction deleted it (0 = still live)
};
```

**Writes never modify in place**:

```cpp
void write(const std::string& key, int value, uint64_t txn) {
    for (auto& v : versions)
        if (v.deleted_by == 0) v.deleted_by = txn;   // mark old versions "died in this txn"
    versions.push_back({value, txn, 0});             // append a new version
}
```

**The visibility rule — MVCC's entire secret**:

```cpp
if (v.created_by > snapshot)                       return false;  // a future version
if (v.deleted_by != 0 && v.deleted_by <= snapshot) return false;  // already in the past
return true;                                                      // exactly my snapshot's
```

**The measured version chain evolving**:

```text
T1 writes 100:  [value=100 born T1 died T-]
T2 begins (snapshot=2), reads 100
T3 writes 999:  [value=100 born T1 died T3] [value=999 born T3 died T-]
T2 reads again: 100   ← the new version's created_by=T3 > snapshot T2, blocked by the rule
T4 begins, reads: 999   ← only a new transaction sees it
```

**Those three lines replace the entire read-lock apparatus**:

```text
The old version the reader wants【is still in the chain】; the writer's new version doesn't touch it
→ reads need【no locks at all】: just chain traversal plus a visibility check
→ versus two-phase locking (2PL): readers take shared locks, writers exclusive ones,
  and the two are mutually exclusive → reads and writes block each other
```

**Python's measurement confirms it**:

```text
While B holds an uncommitted write lock, A reads the same row: 100 (0.052 ms, no waiting)
```

### MVCC's cost: version accumulation

**Measured in C++**:

```text
after 100 more writes, the version chain length: 102
```

```text
Old versions cannot be deleted immediately: while any transaction's snapshot【might see】them, they stay
→ PostgreSQL relies on VACUUM to remove dead versions; skipping it causes table bloat
→ MySQL/InnoDB keeps old versions in the undo log, reclaimed by purge threads
→ long transactions are MVCC's natural enemy: one open for hours stalls
  every version reclamation during those hours
```

**Echoing Chapter 36**: "anything still referenced cannot be collected" — **MVCC's old-version reclamation and GC are the same decision problem**.

### How atomicity is implemented: the undo log

**The Java example hand-writes it**, and the mechanism is unexpectedly plain:

```java
void put(String k, int v) {
    if (inTxn) undoLog.push(new Undo(k, data.get(k)));   // record the old value, then change
    data.put(k, v);
}
int rollback() {
    while (!undoLog.isEmpty()) applyUndo(undoLog.pop());  // replay in reverse
}
```

```text
measured: mid-transfer 甲=40, with 1 record in the undo log
          ROLLBACK undid 1 record → 甲=100 乙=100
```

**Reverse order is mandatory** (measured):

```text
the same key changed three times: 甲=3, with three old values [100, 1, 2] in the undo log
reverse replay → 甲=100 ✓
forward replay → 甲=2   ✗ (wrong)
```

**Its relationship to Chapter 46's WAL**:

```text
WAL (redo log): stores【new values】, to redo "committed but not yet in the data file"
undo log:       stores【old values】, to undo "crashed or rolled back before committing"
→ two sides of one coin: one ensures promises are kept, the other ensures non-promises leave no trace
→ MVCC's old versions are really another organization of the undo log
  (in InnoDB they are literally the same data)
```

### Savepoints: partial rollback inside a transaction

```text
Java measured: two changes after sp1 → ROLLBACK TO sp1 undid 2 records
               甲=50 (before sp1, kept) 乙=100 (after sp1, undone) 丙=null (deleted back to "absent")
Python measured: after commit [(1, 1), (2, 100)] — the same behavior
```

**The implementation merely records "how deep the undo log was at the savepoint" and rolls back to that depth** — an ORM's "nested transactions" are usually this (Chapter 51).

---

## 4. JavaScript

The JS example owns this chapter's most important counterexample: **the anomaly snapshot isolation cannot block**.

### Write skew: two transactions each correct, wrong together

**The scenario**: hospital scheduling, with the rule "at least one doctor must be on call at all times." Alice and Bob are both on call, and both click "request leave" **simultaneously**.

**The application code looks perfectly correct**:

```text
BEGIN; if (doctors on call >= 2) { set myself off call } COMMIT;
```

**What happens under snapshot isolation**:

```text
T1 reads "2 on call" → check passes → sets Alice off call
T2 reads "2 on call" → check passes → sets Bob off call   ← it reads a snapshot and cannot see T1
Both commit →【zero on call】, the rule is broken
```

**⚠️ The crux: neither transaction wrote the same row** — so write-write conflict detection never sees it. That is what makes write skew so insidious: every mechanism built on "detecting conflicts on the same row" fails against it.

### sqlite blocks it, at the cost of write concurrency 1 (measured)

```text
result: B was【rejected】by the database — database is locked
final doctors on call: 1  ✓ the rule held
```

```text
sqlite allows only【one write transaction】at a time, which forces serialization —
so write skew cannot arise
cost: write concurrency = 1 (Ch. 46 measured that it gets correctness by queuing, not parallelism)
```

**But under PostgreSQL's and MySQL's defaults, write skew genuinely happens**:

```text
PostgreSQL defaults to READ COMMITTED, optionally REPEATABLE READ (really snapshot isolation) —
neither blocks it
MySQL's REPEATABLE READ doesn't either; you need SELECT ... FOR UPDATE to lock manually
→ this is what makes "isolation levels" such dangerous terminology:
  the names sound sufficient, and often are not
```

### Three defenses

```text
① raise to SERIALIZABLE   → the database detects the conflict and rolls one side back; you write retries
② SELECT ... FOR UPDATE   → lock at read time, serializing the whole read-check-write
③ reshape the data model  → add a "doctors on call" counter row so both transactions write【the same row】
```

**Option ③ is both fastest and safest**: turn the constraint into a **concrete row that concurrency control can see** — writing the same row makes it an ordinary write-write conflict, which existing locks and detection already catch.

### Transactions cannot govern the outside world (measured)

```text
Suppose the transaction does three things: ① update the database ② send email ③ charge a payment
After ROLLBACK the database rolled back, but —
→ the email【is already sent】and the payment【is already charged】
```

**The right approach**: do only database work inside the transaction, record side effects in an outbox table, and let a background worker perform them after commit — the **outbox pattern**, which degrades "distributed consistency" into "a local transaction plus idempotent retries."

> **Note**: `node:sqlite`/better-sqlite3 are synchronous, so no `await` appears inside a transaction (a good thing); the classic accident with async drivers (pg/mysql2) is **awaiting a query on a different connection inside a transaction** — that query isn't in the transaction and won't be undone on rollback; transactions bind to connections (Ch. 45 measured: a connection cannot return to the pool mid-transaction).

---

## 5. Python

The Python example is the main arena for reproducing the read anomalies, using two **real database connections** rather than a simulation.

### The experiment's key requirement: WAL mode

```python
con.execute("PRAGMA journal_mode=WAL")   # only WAL gives true snapshot isolation
con = sqlite3.connect(DB, isolation_level=None)   # None = we issue BEGIN/COMMIT ourselves
```

**Without WAL this experiment is impossible**: under the rollback-journal mode, B's write would block on A's read lock, so you never reach "A still reads the old value after B commits."

### Write-write conflicts: how the database prevents lost updates (measured)

```text
While A holds the write lock, B attempts to write: OperationalError: database is locked
(B waited 235 ms before giving up — this example sets timeout to 0.2 s; the default is 5 s)
```

```text
sqlite serializes writers with a【database-level write lock】: one write transaction at a time
PostgreSQL/MySQL use【row-level locks】: only writers to the same row conflict — far finer (Ch. 50)
```

**The `timeout` parameter deserves a note**: it decides how long to wait when someone else holds the lock (default 5 s). Set too high in production, it ties up request threads for a long time (Chapter 45's pool starvation); set too low, ordinary lock waits start failing.

### A transaction's full life cycle

```text
BEGIN            → acquire a【snapshot】(which versions you'll read is now fixed)
reads and writes → writes go into the WAL; reads filter versions by the snapshot
COMMIT           → fsync the log (Ch. 46 measured three tiers) → only now visible to others
ROLLBACK/crash   → discard the uncommitted log records; nobody ever saw anything
```

**"Atomic" doesn't mean "cannot fail" — it means "failure leaves no trace."**

> **Note**: `isolation_level=None` is required for manual control; the default makes Python auto-`BEGIN` before certain statements (counterintuitive historical baggage); `BEGIN` is deferred (the snapshot is taken at the first read) while `BEGIN IMMEDIATE` takes the write lock at once — read-then-write transactions want the latter, or you may discover the conflict only at write time, having already read stale data; Python 3.12+'s `autocommit` attribute has clearer semantics.

---

## 6. Java

The Java example hand-writes an undo log, turning "atomicity" from an abstraction into thirty lines of runnable code.

### The undo log implementation (measured, working)

```java
record Undo(String key, Integer oldValue) {}   // oldValue == null means "originally absent"

void put(String k, int v) {
    if (inTxn) undoLog.push(new Undo(k, data.get(k)));   // record old, then change
    data.put(k, v);
}
```

```text
measured: mid-transfer 甲=40, 1 record in the undo log
          ROLLBACK undid 1 record: 甲=100 乙=100
```

### Reverse replay is mandatory (measured)

```text
the same key changed three times: 甲=3, three old values [100, 1, 2] in the log
reverse replay → 甲=100 ✓ (forward replay would give 2, which is wrong)
```

**This is the trap most hand-written transactions fall into**: `undoLog` must be a **stack**, not a queue.

### The savepoint implementation (measured)

```java
void savepoint(String name) { savepoints.put(name, undoLog.size()); }   // record the depth

int rollbackTo(String name) {
    while (undoLog.size() > mark) applyUndo(undoLog.pop());               // roll back to that depth
}
```

```text
measured: two changes after sp1 → ROLLBACK TO sp1 undid 2 records
          甲=50 (kept) 乙=100 (undone) 丙=null (deleted back to "absent")
```

**Note `丙=null`**: it originally didn't exist, so its undo record's `oldValue` is `null` and rolling back performs a `remove` rather than a `put` — **"absent" is also a state that must be restored**.

### The four isolation levels and what each lets through

| Level | Dirty read | Non-repeatable read | Phantom read | Write skew |
|-------|-----------|--------------------|--------------|------------|
| READ UNCOMMITTED | possible | possible | possible | possible |
| READ COMMITTED | no | possible | possible | possible |
| REPEATABLE READ | no | no | standard allows* | possible |
| SERIALIZABLE | no | no | no | no |

```text
* the SQL standard permits phantoms under RR, but MySQL/InnoDB's RR blocks them with gap locks
  while PostgreSQL's RR is really【snapshot isolation】, which also blocks them
→ one name, "REPEATABLE READ," three different actual behaviors
→ write skew is blocked【only】by SERIALIZABLE
```

### The corresponding JDBC API

```java
conn.setAutoCommit(false);                    // ← only this makes it a transaction
conn.setTransactionIsolation(Connection.TRANSACTION_REPEATABLE_READ);
Savepoint sp = conn.setSavepoint("sp1"); conn.rollback(sp);
conn.commit();  /  conn.rollback();           // ← must live in a finally block
```

> **Note**: the most common accident is **forgetting `setAutoCommit(false)`** — every statement becomes its own transaction, leaving the two halves of a transfer unprotected, as fragile as Chapter 46's measured file version; Spring's `@Transactional` is the annotated form of this boilerplate, but **self-invocation doesn't work** (it goes through a proxy, and `this.method()` bypasses it); restore `autoCommit` before returning a connection to the pool, or you poison the next user.

---

## 7. C++

The C++ example hand-writes an MVCC engine — **this chapter's most valuable code**, because it turns "reads don't block writes" from a claim into three verifiable lines.

### The complete visibility rule

```cpp
bool visible(const Version& v, uint64_t snapshot) const {
    if (v.created_by > snapshot) return false;
    if (v.deleted_by != 0 && v.deleted_by <= snapshot) return false;
    return true;
}
```

**A read is simply "walk newest to oldest and return the first version visible to me"**:

```cpp
for (auto rit = versions.rbegin(); rit != versions.rend(); ++rit)
    if (visible(*rit, snapshot)) return rit->value;
```

**No locks anywhere** — the fundamental divide between MVCC and two-phase locking.

### MVCC does not block write-write conflicts (measured)

```text
T105 and T106 both wrote seat, version chain length 2 —【the later writer wins, the earlier vanishes quietly】
→ this toy engine has no conflict detection, so the lost update genuinely happened
```

**Real databases' two solutions**:

```text
pessimistic: take a row lock before writing; the second writer【waits】(MySQL's default, Ch. 50)
optimistic:  at commit, check "is the version I read still current?" — if not,【abort and roll back】
             (PostgreSQL SI)
```

**So MVCC solved only "read-write concurrency"; write-write concurrency still needs locks or conflict detection** — the single most important dividing line for understanding database concurrency control.

### Three echoes of earlier chapters

```text
Ch. 36 GC:      "anything still referenced can't be collected" — MVCC's version reclamation is the same problem
Ch. 46 WAL:     "never modify in place, only append" — MVCC's version chain is the same idea
Ch. 41 lock-free: "no locks on the read path" — MVCC is the database's read-copy-update (RCU)
```

**MVCC is not a database-only invention** but the storage-layer application of "multiple versions + snapshot reads"; the Linux kernel's RCU, Clojure's persistent data structures, and Git's object model are all the same idea in different forms.

> **Note**: real MVCC is far more complex — it must track transactions' **commit state** (a version is visible only if its creating transaction has committed), snapshots must record "which transactions are currently running" (PostgreSQL's `xmin/xmax/xip_list`), and long version chains degrade performance (an overly long InnoDB undo chain slows reads from old snapshots).

---

## 8. C#

The C# example ran **an empirical test of a popular claim**, and the result deserves its own section.

### The measurement discounts the textbook "optimistic vs pessimistic" rule

**The experiment**: 8 threads performing 16000 read-modify-writes total, across four contention levels.

```text
rows  contention      pessimistic  optimistic   optimistic retries   gap
   1  extreme (hot row)   14.0 ms     21.5 ms              49256   pessimistic 1.54×
   8  high                 3.0 ms      3.8 ms               4454   pessimistic 1.25×
  64  medium               2.6 ms      3.5 ms               1988   pessimistic 1.34×
 512  low (spread)         3.0 ms      2.6 ms               1078   optimistic 1.13×
```

**The honest conclusion after repeated runs**:

```text
Only the first row is【stably reproducible】: pessimistic wins 1.5–1.9× on a hot row
(where optimistic did fifty thousand units of wasted work — read, find the version changed, redo)
The other three levels【flip direction】between runs within 1.0–1.6× — that is noise, not a rule
```

**Why**:

```text
Acquiring an in-memory lock costs tens of nanoseconds, so optimistic locking has little to save
That rule presumes【holding a lock is expensive】—and in a database it is expensive because the lock
is held【across a network round trip】or even【across a user's think time】,
not because of the locking instruction itself
```

### Optimistic locking's real value: spanning boundaries where locks are impossible (measured)

**The scenario**: a user opens an edit form → thinks for 5 minutes → clicks save. **You cannot hold a database lock for five minutes.**

```text
Users A and B open the edit page simultaneously, both reading value=100 version=1
A changes it to 110 and saves: success (now value=110 version=2)
B changes it to 120 and saves:【rejected】(B's version 1 ≠ current version 2)
Final value=110 — B's change did not【silently overwrite】A's
```

```text
Without a version column: B's save would overwrite A's change and A would never know (a lost update)
→ optimistic locking is not "a faster lock" but
 【the only concurrency control that spans stateless request boundaries】—
  you cannot hold a lock between HTTP requests
```

**That is the real dividing line**: not "which is faster" but "**can you hold a lock at all**."

### Optimistic locking in ORMs

```text
EF Core:   [Timestamp] byte[] RowVersion;  → automatically adds WHERE RowVersion = ?
           zero rows affected at commit throws DbUpdateConcurrencyException
Hibernate: @Version int version;           → the same mechanism (Ch. 51 ORM)
→ "zero rows updated" IS the conflict signal: the version didn't match, so WHERE found nothing
```

### Two things retry logic must know

```text
① Retry only【retryable errors】: serialization failures and deadlocks yes;
   constraint violations will fail ten thousand times too
② Always【back off exponentially with a cap】: retrying without backoff pushes the system
   toward an avalanche under contention (Ch. 45)
→ and you retry【the whole transaction】— including the first read, because the snapshot must be retaken
```

> **Note**: forgetting `cmd.Transaction = tx` leaves the command **outside the transaction** (.NET's most common transaction trap); `TransactionScope` is an ambient transaction spanning connections, but it escalates to a distributed transaction (MSDTC) — use with care; `IsolationLevel.Snapshot` requires `ALLOW_SNAPSHOT_ISOLATION` on the database first.

---

## 9. SQL

This section views the four letters from the database's side.

### A: atomicity (measured)

```text
① after transferring 60: 甲=40 乙=160, total=200
   after the crash rollback: 甲=40, total still 200 (not a cent lost)
```

### C: consistency — constraints guard the invariants (measured)

```sql
CREATE TABLE account (
  balance INTEGER NOT NULL CHECK (balance >= 0)   -- declare the invariant
);
```

```text
② attempting to overdraw by 999: rows affected=0 (CHECK blocked it)
→ C is the only letter that requires【you】: the database can enforce only the rules you declare
```

### The four isolation levels (SQL standard)

```text
READ UNCOMMITTED : dirty✗ non-repeatable✗ phantom✗ write-skew✗ (almost nobody uses it)
READ COMMITTED   : dirty✓ non-repeatable✗ phantom✗ write-skew✗ (PostgreSQL/Oracle default)
REPEATABLE READ  : dirty✓ non-repeatable✓ phantom~ write-skew✗ (MySQL default)
SERIALIZABLE     : all✓ (sqlite has only this one)
```

### One name, three meanings: the three REPEATABLE READs

```text
SQL standard : permits phantoms (guarantees only that existing rows don't change)
MySQL/InnoDB : blocks phantoms too via【gap locks】, but not write skew
PostgreSQL   : really【snapshot isolation】, blocks phantoms, still not write skew
→ so "I use RR, therefore I'm safe" is a dangerous sentence — ask "whose RR?"
```

**This is the chapter's most practical fact**: an isolation level's **name is standardized, its behavior is not**. When migrating databases, behavior under the same level name can change entirely, and such bugs are extremely hard to reproduce.

### sqlite's choice

```text
Isolation level: always SERIALIZABLE
Implementation: only【one write transaction】at a time (database-level write lock)
With PRAGMA journal_mode=WAL, readers are no longer blocked by writers
(Python measured 0.05 ms, no waiting)
→ correctness maxed out, write concurrency 1 — a sensible trade for embedded use
```

### Three practical disciplines

```text
① Keep transactions short: long ones stall MVCC's version reclamation
   (C++ measured the chain reaching 102)
② No I/O inside transactions: emails and payments cannot be rolled back (the JS demo)
③ Access rows in a fixed order: inconsistent ordering → deadlock
   (Ch. 41's four conditions; Ch. 50 expands)
```

> **Note**: sqlite's `BEGIN` is deferred — use `BEGIN IMMEDIATE` when you read then write; `SAVEPOINT` can nest, and reusing a name overwrites; `PRAGMA foreign_keys=ON` is **off by default** in sqlite — inactive foreign keys are the most common beginner confusion.

---

## 10. Cross-Language Comparison

### ① Transaction capabilities

| Capability | SQL | Python | Java | C# | JavaScript |
|-----------|-----|--------|------|-----|-----------|
| Begin a transaction | `BEGIN` | `con.execute("BEGIN")` | `setAutoCommit(false)` | `BeginTransaction()` | `db.exec('BEGIN')` |
| Set isolation | `SET TRANSACTION` | driver parameter | `setTransactionIsolation` | `IsolationLevel` enum | driver parameter |
| Savepoints | `SAVEPOINT` | ✅ | `setSavepoint()` | `tx.Save()` | ✅ |
| Declarative transactions | — | decorators (frameworks) | **`@Transactional`** | `TransactionScope` | framework-specific |
| Optimistic locking | hand-written version column | hand-written | **`@Version`** (JPA) | **`[Timestamp]`** (EF) | framework-specific |
| Autocommit default | on | **on** (needs `isolation_level=None`) | **on** (must disable explicitly) | on | on |

### ② Key experiment one: three read anomalies (Python measured)

```text
no transaction: A reads 100 → B commits 777 → A reads 777      【non-repeatable read】
in transaction: A reads 100 → B commits 999 → A reads 100      【snapshot isolation blocked it】
range query:    COUNT 3 → 3 inside; 4 outside                  【phantoms blocked too】
during a write lock: A reads the same row in 0.05 ms, no wait  【reads not blocked by writes】
```

### ③ Key experiment two: MVCC's three-line rule (C++ measured)

```text
version chain: [value=100 born T1 died T3] [value=999 born T3 died T-]
T2 (snapshot=2) reads 100; T4 (snapshot=4) reads 999
three visibility lines → replace the entire read-lock apparatus
cost: after 100 more writes the chain reaches 102 (old versions can't be deleted yet)
```

### ④ Key experiment three: write skew (JS measured)

```text
Two transactions each read "2 on call" → each check passes → together, zero on call
The crux: neither transaction【wrote the same row】→ write-write detection never sees it
sqlite (SERIALIZABLE) blocked it: B rejected with "database is locked"
PostgreSQL's and MySQL's default levels【do not】
```

### ⑤ Key experiment four: optimistic vs pessimistic (C# measured)

```text
Four contention levels: only the hot row is stable (pessimistic 1.5–1.9×, 50k optimistic retries)
The other three flip direction between runs → noise, not a rule
The real divide: whether you can hold a lock (user thinks for 5 minutes → version column only)
```

### ⑥ Common ground and root causes

**Common ground**: every language defaults to **autocommit** (one transaction per statement) — a default that protects beginners and makes "forgot to open a transaction" the most common accident; every language's transactions **bind to a connection** (Ch. 45: a connection can't return to the pool mid-transaction); no language can make a transaction govern **external side effects**.

**Root causes**:

- **Java/C# offer declarative transactions** (`@Transactional` / `TransactionScope`) — the enterprise-language tradition of extracting cross-cutting concerns from business code; the price is **implicit boundaries** (Spring's self-invocation failure is the classic incident);
- **Python/JS stay explicit** — the scripting tradition: transaction boundaries are visible to the eye, at the cost of boilerplate;
- **JPA and EF Core both build in optimistic locking** (`@Version` / `[Timestamp]`) — because an ORM's typical scenario *is* "editing across HTTP requests," where holding a lock is impossible (the C# measured divide);
- **sqlite has only SERIALIZABLE** — write concurrency is low in embedded settings anyway, so trading "serialized writers" for "zero concurrency bugs" is sensible;
- **Server databases offer four levels** — throughput is a hard requirement, and lowering isolation is the most direct speedup (at the cost of shifting concurrency bugs onto the application).

---

## 11. Implementation Comparison

| Database | Isolation implementation | Key details |
|----------|-------------------------|-------------|
| **SQLite** | database-level write lock + WAL snapshot reads | SERIALIZABLE only; write concurrency 1; readers never block |
| **PostgreSQL** | MVCC (tuples carry `xmin`/`xmax`) | RR = snapshot isolation; SERIALIZABLE = SSI (detects read-write dependency cycles) |
| **MySQL/InnoDB** | MVCC (undo log chains) + gap locks | RR blocks phantoms with gap locks; deadlocks auto-detected, cheaper side rolled back |
| **Oracle** | MVCC (rollback segments) | no dirty-read level; RR via snapshots; the oldest MVCC implementation |
| **SQL Server** | locks by default; Snapshot optional | only becomes MVCC once `READ_COMMITTED_SNAPSHOT` is enabled |

**PostgreSQL's SSI deserves a note** (serializable snapshot isolation):

```text
It uses no locks; instead it detects【dangerous structures】on top of snapshot isolation:
  one transaction read data another is about to write, forming a particular dependency cycle
→ on detection one side is rolled back (raising serialization_failure)
→ so it blocks write skew while reads still take no locks
→ the price: you【must】write retry logic, because transactions get rolled back through no fault of their own
```

---

## 12. Performance Analysis

### The cost gradient of isolation levels

```text
READ COMMITTED  → a fresh snapshot per statement; version reclamation stays fast
REPEATABLE READ → one snapshot per transaction; long transactions stall reclamation
SERIALIZABLE    → extra conflict detection (SSI) or more locks (2PL) + rollbacks and retries
→ higher isolation, lower throughput — a real curve you must trade along
```

### This chapter's numbers at a glance

```text
snapshot isolation:  two reads inside a transaction both 100 (999 already committed, invisible)
reads not blocked:   0.05 ms with zero waiting (while B held an uncommitted write lock)
MVCC accumulation:   100 more writes → chain length 102
write-lock conflict: B gave up after 235 ms (timeout=0.2 s)
pessimistic vs optimistic: hot row pessimistic 1.5–1.9× (50k optimistic retries); the rest is noise
undo log:            reverse replay correct (甲=100), forward replay wrong (甲=2)
```

### The real cost of long transactions

```text
① stalls MVCC version reclamation → table bloat (C++ measured the chain only growing)
② occupies a connection for a long time → pool exhaustion (Ch. 45 measured that avalanche)
③ holds locks longer → more waiting for others, higher deadlock probability
→ "keep transactions short" is not a style suggestion but the prevention of three specific failures
```

> ⚠️ Raising the isolation level **is not free correctness**. Under SERIALIZABLE transactions get rolled back on conflict, and **you must write retry logic** — otherwise users see not "inconsistent data" but "operation failed," which merely changes the problem's shape.

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Multi-step operations (transfers) | wrap in one transaction | separate statements | Ch. 46 measured half records on crash |
| Read-then-write (stock deduction) | `BEGIN IMMEDIATE` / `FOR UPDATE` | unlocked read then write | measured lost updates |
| Editing across HTTP requests | optimistic locking (version column) | holding a database lock | you cannot hold one for 5 minutes |
| Check-then-act constraints | SERIALIZABLE or reshape the model | relying on RR | measured: write skew passes through RR |
| Email/payment inside a transaction | outbox table, act after commit | doing it inline | external side effects can't be rolled back |
| Long batch jobs | split into small transactions | one big transaction | stalls reclamation + occupies a connection |
| Java transaction boundaries | `@Transactional` at the service entry | in-class self-invocation | the proxy is bypassed |
| Enlisting .NET commands | `cmd.Transaction = tx` | forgetting it | the command isn't in the transaction at all |
| Manual control in Python | `isolation_level=None` | relying on the default | the default auto-`BEGIN`s, counterintuitively |
| Concurrency conflicts | exponential backoff retries | immediate infinite retries | avalanches under contention (Ch. 45) |
| Migrating databases | verify each level's **actual behavior** | assuming names mean the same | the three RRs are three behaviors |

### The rule of thumb

```text
① Does this group of operations need all-or-nothing? → yes → wrap in a transaction
② Is there a read-then-write inside?                → yes → lock or use optimistic locking
③ Is there a check-then-act constraint?             → yes → write-skew risk; SERIALIZABLE or reshape
④ Is there external I/O inside?                     → yes → move it out (outbox)
```

---

## 14. Best Practices

- **Keep transactions short**: three measured consequences — stalled version reclamation (chain reaching 102), occupied connections (Chapter 45's pool exhaustion), and longer lock holds.
- **Know both the name and the actual behavior of your isolation level**: this chapter's most practical fact — "I use RR so I'm safe" must first answer "whose RR?"
- **Watch for write skew in check-then-act constraints**: measured, it targets code that looks correct; the safest fix is to **turn the constraint into a concrete row** concurrency control can see.
- **Always use optimistic locking for cross-request edits**: not because it's fast (measured: no order-of-magnitude advantage) but because **you cannot hold a lock between HTTP requests**.
- **Never do external I/O inside a transaction**: a sent email cannot be recalled; use the outbox pattern to turn side effects into post-commit local work.
- **Retry logic needs error classification plus exponential backoff**: only serialization failures and deadlocks deserve retries, and you retry **the whole transaction** (the snapshot must be retaken).
- **Explicitly disable autocommit**: Java's `setAutoCommit(false)`, Python's `isolation_level=None` — skip it and the transaction you think you have doesn't exist.
- **Don't forget `cmd.Transaction = tx`** (.NET) and Spring's self-invocation trap (Java): these are each ecosystem's most frequent cause of "the transaction didn't take effect."

---

## 15. Common Pitfalls

**Pitfall 1 · Forgetting to disable autocommit**

```java
conn.createStatement().execute("UPDATE ...");   // ⚠️ each statement its own transaction; the two halves are unprotected
// correct: conn.setAutoCommit(false); ... conn.commit();
```

**Pitfall 2 · Read-then-write without a lock**

```sql
SELECT stock FROM item WHERE id = 1;    -- ⚠️ reads 1
-- another transaction also reads 1 and sells it
UPDATE item SET stock = 0 WHERE id = 1; -- oversold
-- correct: SELECT ... FOR UPDATE, or UPDATE ... WHERE stock > 0
```

**Pitfall 3 · Assuming REPEATABLE READ blocks write skew**

```text
⚠️ measured: two transactions each pass their check and together violate the constraint — RR doesn't block it
correct: SERIALIZABLE, or SELECT ... FOR UPDATE, or reshape the data model
```

**Pitfall 4 · External I/O inside a transaction**

```javascript
await db.exec('BEGIN');
await sendEmail(user);        // ⚠️ cannot be rolled back
await chargePayment(user);    // ⚠️ even less so
```

**Pitfall 5 · Spring `@Transactional` self-invocation**

```java
public void outer() { this.inner(); }        // ⚠️ bypasses the proxy; @Transactional has no effect
@Transactional public void inner() { ... }
```

**Pitfall 6 · Long transactions**

```python
con.execute("BEGIN")
for row in huge_list:          # ⚠️ hours of work → all version reclamation stalls + a connection tied up
    process(row)
con.execute("COMMIT")
```

**Pitfall 7 · Retrying without backoff**

```python
while True:
    try: do_txn(); break
    except SerializationFailure: pass    # ⚠️ instant retry → pushes the database toward an avalanche
```

---

## 16. Interview Questions

**Basic**

1. What mechanism implements each letter of ACID? Which one requires your participation?
2. How do dirty reads, non-repeatable reads, and phantom reads differ? Give an example of each.
3. What is a savepoint? How does it relate to nested transactions?

**Intermediate**

4. **What is MVCC's visibility rule? Why does it let reads avoid being blocked by writes? (Answer with the three lines.)**
5. What problem does each of the undo log and the redo log (WAL) solve? Why must rollback replay in reverse?
6. **How does MySQL's REPEATABLE READ differ from PostgreSQL's?**

**Advanced**

7. **What is write skew? Why can't snapshot isolation block it? Give three defenses and compare them.**
8. What is MVCC's cost? Why are long transactions its natural enemy?
9. When should you choose optimistic over pessimistic locking? (Hint: the real divide isn't performance.)

---

## 17. Exercises

**Basic**

1. Reproduce a non-repeatable read with two connections, then wrap it in `BEGIN` and verify snapshot isolation.
2. Use `SAVEPOINT` for a partial rollback and verify that changes before the savepoint survive.
3. Deliberately run a transfer without a transaction, throw midway, and inspect the broken data.

**Intermediate**

4. **Reproduce key experiment two**: implement MVCC's three-line visibility rule in your language of choice and verify reads don't block writes.
5. Hand-write a transaction with an undo log; verify reverse replay is correct and forward replay is wrong.
6. Reproduce write skew (hospital scheduling, or "always keep at least one admin") and fix it three different ways.

**Challenge**

7. **Add version reclamation to your MVCC engine**: determine which versions no active snapshot can ever see again and delete them safely (this is VACUUM's core).
8. Implement a transaction-retry decorator with exponential backoff that retries only retryable error types.
9. Build a "cross-HTTP-request edit form" with optimistic locking and verify the later submitter is properly rejected rather than silently overwriting.

---

## 18. Chapter Summary

**One sentence**: a transaction turns a group of operations into an indivisible unit and makes four promises — atomicity (the Java example's hand-written undo log, measured correct only when replayed in reverse), consistency (constraints at the gate, the one letter requiring you), isolation (this chapter's protagonist), and durability (Chapter 46's three measured fsync tiers); **isolation** is the hardest and richest — Python measured the snapshot-isolation divide with two real connections (without a transaction, two reads give 100 → 777, a **non-repeatable read**; wrapped in one, a read still returns 100 even after another connection commits 999), and the hand-written C++ MVCC engine reveals that its implementation is **just three visibility lines** (hence reads are never blocked by writes — measured at **0.05 ms with zero waiting** — at the cost of version chains reaching **102 entries**, which is exactly where VACUUM and "long transactions are the enemy" come from); yet snapshot isolation has a hole that targets code that looks correct — **write skew** (the JS hospital demo: two transactions each pass their check, together violate the constraint, and **evade every conflict detector because neither wrote the same row**); finally the C# example **discounted the popular claim that optimistic locking wins at low contention** (across four contention levels only the hot-row case was stable, the rest was noise), showing the real divide is not performance but **whether you can hold a lock at all** — when a user thinks for five minutes before submitting, a version column is the only option.

**Key takeaways**

- **What implements ACID**: A = undo log, C = constraints (your part), I = MVCC + locks, D = WAL + fsync.
- **Three read anomalies**: dirty (uncommitted), non-repeatable (row contents changed), phantom (row set changed) — the last distinction sets implementation difficulty.
- **Snapshot isolation** (measured): two reads inside a transaction always agree; you see the database as of your start instant.
- **MVCC's three-line rule**: newer than me invisible, dead before me invisible, everything else visible — replacing the entire read-lock apparatus.
- **MVCC's cost** (measured 102 versions): old versions can't be deleted → VACUUM, table bloat, long transactions as the enemy.
- **Write skew**: snapshot isolation can't block it because the transactions write different rows; only SERIALIZABLE or a reshaped model solves it.
- **One name, three meanings**: MySQL's RR, PostgreSQL's RR, and the standard's RR are three behaviors.
- **Optimistic locking's real value** (measured): not speed, but **spanning boundaries where locks are impossible**.

**Checklist**

- [ ] I can name what implements each ACID letter and which one is mine.
- [ ] I can distinguish the three read anomalies and explain why phantoms are harder to block.
- [ ] I can explain MVCC with the three-line visibility rule.
- [ ] I can recognize write skew and name three defenses.
- [ ] I know the real criterion for optimistic locking is "can I hold a lock?"

**Next chapter**: "full table scan" versus "index lookup" has recurred throughout this chapter, and Chapter 47 measured an index turning `SCAN` into `SEARCH` — but what *is* an index, why is it hundreds of times faster, and what does it cost? Chapter 49 covers **indexes**: we will hand-write a B+ tree and measure it against hash indexes and full scans, quantify how much slower each added index makes writes, explain why **a composite index's column order decides whether it can be used at all** (the leftmost-prefix rule), and why a database sometimes **deliberately refuses** the index you carefully built.

---

## 19. Further Reading

- <a href="https://www.sqlite.org/lang_transaction.html" target="_blank" rel="noopener">SQLite · Transaction syntax and semantics</a> — `BEGIN`'s DEFERRED/IMMEDIATE/EXCLUSIVE modes.
- <a href="https://www.postgresql.org/docs/current/transaction-iso.html" target="_blank" rel="noopener">PostgreSQL Docs · Transaction Isolation</a> — each level's **actual behavior**, with the official write-skew example.
- <a href="https://dev.mysql.com/doc/refman/8.0/en/innodb-transaction-isolation-levels.html" target="_blank" rel="noopener">MySQL Docs · InnoDB isolation levels</a> — read alongside the above to see "one name, different meanings" directly.
- <a href="https://en.wikipedia.org/wiki/Multiversion_concurrency_control" target="_blank" rel="noopener">Wikipedia · MVCC</a> — the theory behind multiversion concurrency control.
- <a href="https://en.wikipedia.org/wiki/Snapshot_isolation" target="_blank" rel="noopener">Wikipedia · Snapshot isolation</a> — formal definitions of snapshot isolation and write skew.
- <a href="https://www.postgresql.org/docs/current/routine-vacuuming.html" target="_blank" rel="noopener">PostgreSQL Docs · Routine Vacuuming</a> — how this chapter's measured version accumulation is cleaned up in production.
- <a href="https://en.wikipedia.org/wiki/Isolation_(database_systems)" target="_blank" rel="noopener">Wikipedia · Isolation</a> — the level-versus-anomaly table.
- <a href="https://jepsen.io/consistency" target="_blank" rel="noopener">Jepsen · Consistency Models</a> — the full map of consistency and isolation models, with measured findings per database.
- <a href="https://dataintensive.net/" target="_blank" rel="noopener">Designing Data-Intensive Applications</a> — Chapter 7 "Transactions" is the best continuation, and its write-skew section is especially good.
