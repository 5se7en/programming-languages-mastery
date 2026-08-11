# Chapter 49 · Indexes

[简体中文](./49-index.md) ｜ **English**

---

> Chapter 46 measured finding one row among a hundred thousand: a file scan was **551× slower** than a sqlite primary-key lookup. Chapter 47 measured an index turning a plan from `SCAN` into `SEARCH` without changing one character of SQL. But what *is* an index, why is it hundreds of times faster, and what does it cost? This chapter takes it apart.
>
> The **key experiment** hand-writes a B+ tree and measures the quantity that decides everything — **how fanout determines tree height**. For the same one million keys: fanout 2 (the binary tree from the textbooks) gives height **20**, visiting 20 nodes per lookup; fanout 128 (a typical database page) gives height **3**, visiting only 3. In memory that hardly matters, but when **each node is one disk page read**, 20 versus 3 is the difference between 2 ms and 0.3 ms. More striking is how slowly it grows: going from 1,000 keys to 10,000,000 (ten thousandfold), the height goes only from **2 to 4**.
>
> So why not hash indexes? The measurement answers: in memory a hash is indeed fastest for point lookups, but on a range query `1000 <= key <= 3000`, the B+ tree took **0.40 ms** and the hash **757.9 ms — 1,892× slower**, because hashing destroys ordering entirely and leaves nothing but a full sweep. The Java example reproduced the same conclusion with `HashMap` versus `TreeMap` (**533×** on range queries). **One B+ tree covers equality, range, ordering, and prefix queries** — which is why it is the default.
>
> But an index is not a free accelerator; it is **a copy of your data that must be kept in sync**. Python measured going from 0 to 4 indexes: inserting 200,000 rows went from 106 ms to **337 ms (3.18×)** and the file from 5.2 MB to **15.9 MB (3.06×)**. JS measured all three write operations slowing down, with `DELETE` hit hardest at **6.75×**.
>
> Two more counterintuitive measurements. First, **an index is not always faster**: at 4 matching rows the index beat a full scan by **180×**, but at 33% of rows the full scan won by **1.6×** — because table lookups are random I/O while scans are sequential (the C# example located this crossover with a 61 MB wide-row table and measured a **3.1×** penalty purely from shuffling access order). Second, **sqlite chose wrong on that low-selectivity query** — it used the index anyway. "The optimizer picks the best plan" has preconditions.
>
> Finally the **leftmost-prefix rule**: a composite index on `(a, b, c)` serves `WHERE a`, `WHERE a AND b`, and `WHERE a AND b AND c`, and also `WHERE a AND c` (but **using only the `a` column**); while `WHERE b` and `WHERE b AND c` **cannot use a single column of it**. An index's column order is the boundary of its capability — **the wrong order means you built it for nothing**.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- Explain **how fanout determines tree height**, and why disk indexes use B+ trees rather than binary trees (measured 20 versus 3 node visits);
- State the B+ tree's decisive advantage over hash indexes (measured **1,892×** on range queries) and its three disk-oriented design decisions;
- Quantify an index's cost: **write amplification** (measured 3.18× on insert, 6.75× on delete) and **space** (3.06×);
- Use **selectivity** to decide whether an index is worth building, and explain why "table lookups are random I/O" makes high-match queries better served by a full scan;
- Apply the **leftmost-prefix rule** to design composite indexes and verify with `EXPLAIN` that they are actually used.

---

## 2. Why This Concept Exists

### The question the last two chapters left

```text
Ch. 46 measured: 20 lookups in 100k rows — file scan 399.8 ms vs sqlite primary key 0.726 ms → 551×
Ch. 47 measured: adding an index turned SCAN into SEARCH without changing the SQL
→ but what is inside the "index" black box?
```

### The root problem: data on disk has no usable order

```text
Chapter 20's O(1) hash lookups and Chapter 21's O(log n) trees all live in【memory】
A table on disk is just rows in insertion order — finding one means reading【from start to end】: O(n)
→ an index is "maintaining an extra【ordered】structure alongside the data, and locating rows with it"
```

**In one definition**:

```text
index = an【extra, ordered, continuously maintained】copy of your data
        that buys query speed with space and write speed
```

**Every modifier in that definition carries a bill**, and this chapter weighs each:

| Modifier | Cost | Measured here |
|----------|------|---------------|
| extra | disk space | 4 indexes → file 3.06× |
| ordered | insertion must maintain order | insert 3.18×, delete 6.75× |
| continuously maintained | every write updates it | UPDATE also 2.97× slower |

> **In one sentence**: an index is not "a switch that makes the database faster" but **a trade that buys query speed with write speed and storage** — and this whole chapter is about weighing both sides of that trade.

---

## 3. How It Works

### Key experiment one: fanout determines tree height

**Why do databases use B+ trees rather than the binary search trees from textbooks?** The hand-written C++ B+ tree answers (one million keys):

| Fanout | Height | Total nodes | Per lookup | Equivalent to |
|--------|--------|------------|-----------|---------------|
| **2** | **20** | 1,000,007 | 20 nodes visited | a binary tree (the textbook one) |
| 8 | 7 | 142,860 | 7 nodes visited | |
| **128** | **3** | 7,876 | **3 nodes visited** | a typical database page (4KB / 32B per key) |
| 512 | 3 | 1,959 | 3 nodes visited | large pages or narrow keys |

```text
In memory, 20 versus 3 node visits is nothing
But when each node is【one disk page read】(~100 μs):
  20 pages = 2 ms    3 pages = 0.3 ms    → 6.7× faster
→ that is the entire reason B+ trees exist: flatten the tree so every I/O returns as much as possible
```

### How slowly the height grows (the B+ tree's most counterintuitive property)

```text
      1000 keys, fanout 128 → height 2
    100000 keys, fanout 128 → height 3
  10000000 keys, fanout 128 → height 4
```

**Ten-thousandfold more data raises the height only from 2 to 4** — query cost barely moves. That is the mathematical basis for "build the index once and it keeps working": `log_128(N)` grows extremely slowly.

### The B+ tree's three design decisions (each serving the disk)

```text
① Data lives【only in the leaves】; internal nodes hold only separator keys
   → internal nodes fit more keys → larger fanout → a flatter tree

② Leaves are【linked into a list】
   → range scans never return to the root; they read forward sequentially

③ Node size = 【the disk page size】(typically 4KB/8KB/16KB)
   → one I/O returns one complete node; no read is wasted
```

**Versus Chapter 21's binary search tree**: it is designed for **memory** — one key per node, height `log₂ n`. The B+ tree is designed for **disk** — hundreds of keys per node, height `log_f n`. **The same problem, with different optimal answers because the storage medium differs.**

### Key experiment two: the hash index's fatal weakness

**Point lookups in memory** (1M rows, 20k queries, measured in C++):

```text
full scan        : 2286.5 ms  O(n)
sorted array bsearch:  0.8 ms  O(log n), 2838× faster
B+ tree          :    1.2 ms  O(log_f n), 3 node visits
hash table       :    0.6 ms  O(1), 3567× faster
```

**Note the B+ tree is not faster than binary search** — in memory both are `O(log n)` and binary search avoids pointer chasing. **The B+ tree's advantage is not in memory**: a sorted array cannot be inserted into efficiently, while a B+ tree can, and it is optimized for disk.

**But range queries invert the ranking entirely**:

```text
querying "1000 <= key <= 3000" 1000 times (1001 rows matched):
  B+ tree: 0.40 ms (descend to the starting leaf, then walk the leaf list — 11 nodes visited)
  hash   : 757.9 ms (1892× slower — hashing destroyed the order, so【sweep everything】)
```

**The Java example reproduced it with `HashMap` / `TreeMap`**:

```text
200k point lookups: HashMap 12.8 ms, TreeMap 70.7 ms (hash 5.5× faster)
2000 range queries: HashMap full sweep 7279.6 ms, TreeMap subMap 13.7 ms (ordered 533× faster)
```

**The conclusion**:

```text
hash index: answers only "equals"
B+ tree   : equals / greater / less / BETWEEN / prefix LIKE / ORDER BY / MIN / MAX — all of them
→ one structure covering nearly every query shape — which is why databases default to B+ trees
```

**Three capabilities ordered structures give away free** (Java measured):

```text
min/max:            firstKey/lastKey       → SQL's MIN()/MAX() is O(log n), not O(n)
predecessor/successor: floorKey/ceilingKey → SQL's >= / <= / BETWEEN
inherently sorted:  leaves are already ordered → SQL's ORDER BY needs no sort (measured 1652×)
```

### Key experiment three: what an index costs

**Python measured** (200k rows, adding indexes one at a time):

| Indexes | Insert time | Ratio | File size | Ratio |
|---------|------------|-------|-----------|-------|
| 0 (primary key only) | 106 ms | 1.00× | 5.2 MB | 1.00× |
| 1 | 152 ms | 1.44× | 7.5 MB | 1.46× |
| 2 | 211 ms | 1.99× | 9.8 MB | 1.89× |
| 3 | 262 ms | 2.47× | 11.8 MB | 2.28× |
| **4** | **337 ms** | **3.18×** | **15.9 MB** | **3.06×** |

**JS measured all three write operations slowing down**:

```text
operation             no index      1 index        3 indexes
INSERT 150000 rows      90 ms       125 ms(1.40×)   212 ms(2.36×)
UPDATE 30000 rows        9 ms        26 ms(2.97×)    26 ms(2.95×)
DELETE 30000 rows        5 ms        19 ms(4.15×)    31 ms(6.75×)
```

```text
→ UPDATE is especially costly: changing an indexed column means【delete at the old position,
  insert at the new one】— two index operations
→ DELETE is worst (6.75×): every index must locate and remove its entry
→ an index accelerates【reads】and slows down【every write】
```

### Key experiment four: an index is not always faster

**Python forced both execution strategies with `INDEXED BY` / `NOT INDEXED`**:

```text
high selectivity a=5   matched     4 rows ( 0.00%): index  0.06 ms   scan 10.13 ms → index 180× faster
low selectivity  c=1   matched 66667 rows (33.33%): index 23.81 ms   scan 14.94 ms → 【scan 1.6× faster】
```

**Why the reversal? Because the access patterns differ**:

```text
via index: locate in the B+ tree → get a pile of row ids →【look each one up】= random access
full scan: read page one through page N                  = sequential access
→ with few matches, a handful of random accesses wins easily
→ with many matches, tens of thousands of random accesses lose to one sequential sweep
```

**C# located that crossover with a 61 MB wide-row table**:

```text
matched    1000 rows (  0.1%): lookups 0.0 ms   scan 1.9 ms → index faster
matched   10000 rows (  1.0%): lookups 0.1 ms   scan 1.9 ms → index faster
matched  100000 rows ( 10.0%): lookups 0.7 ms   scan 2.3 ms → index faster
matched  300000 rows ( 30.0%): lookups 2.0 ms   scan 2.3 ms → index faster
matched  500000 rows ( 50.0%): lookups 3.3 ms   scan 1.8 ms →【scan faster】
matched 1000000 rows (100.0%): lookups 6.4 ms   scan 2.3 ms →【scan faster】
```

**And measured exactly where the gap comes from**:

```text
reading【the same 1,000,000 rows】: sequential 2.0 ms, shuffled 6.3 ms (3.1× slower)
→ identical row counts, only the【access order】differs
→ the entire gap comes from CPU cache lines and hardware prefetching
```

**⚠️ Note that this example's crossover is high (roughly 30–50%)** because in memory random access is only ~3× slower than sequential. **On disk, random I/O is one to two orders of magnitude slower**, so real databases cross over far earlier — the common rule of thumb is that **matching more than 5–20% of rows should mean a full scan**.

### An honest finding: sqlite chose wrong here

```text
Python measured: the full scan is 1.6× faster on the low-selectivity query
but sqlite actually chose: SEARCH t USING INDEX idx_c (c=?)
→ it【used the index anyway】— it chose wrong in this case
→ sqlite's cost model is far simpler than PostgreSQL's
```

**"The optimizer picks the best plan" has preconditions**: it depends on accurate statistics and a sufficiently detailed cost model. An index on a low-selectivity column is often a net liability — build it and the optimizer may even be misled by it.

---

## 4. JavaScript

The JS example owns the full bill for indexes.

### All three write operations slow down (measured)

```text
operation             no index      1 index        3 indexes
INSERT 150000 rows      90 ms       125 ms(1.40×)   212 ms(2.36×)
UPDATE 30000 rows        9 ms        26 ms(2.97×)    26 ms(2.95×)
DELETE 30000 rows        5 ms        19 ms(4.15×)    31 ms(6.75×)
file size             15.4 MB      17.1 MB(1.11×)  21.7 MB(1.40×)
```

**Note what makes UPDATE special**: changing an indexed column requires **two** index operations — delete at the old position and insert at the new one (a B+ tree orders by value, so a changed value means a changed position). So **"don't index frequently updated columns"** is not folklore.

### Selectivity: not all indexes are worth the same (measured)

```text
column a: 30000 distinct values, selectivity 0.20000, one value matches     5 rows ( 0.0%)
column b:     7 distinct values, selectivity 0.00005, one value matches 21429 rows (14.3%)
```

```text
selectivity = distinct values / total rows; the closer to 1, the more an index is worth
→ gender, status, and boolean columns are almost always wasted【on their own】:
  too many matches, and table lookups lose to a scan
→ but they remain useful as【the trailing columns of a composite index】
  (paired with a high-selectivity leading column)
```

### Indexes need maintenance too: fragmentation and statistics (measured)

```text
after deleting half the rows, page count 4699 → after VACUUM 2351 (50% reclaimed)
```

```text
Deletion does not return space to the OS immediately: holes remain in pages
(Chapter 33's memory fragmentation, disk edition)
Statistics also go stale: only after ANALYZE does the optimizer know
"roughly how many rows this value matches"
→ PostgreSQL's autovacuum does both (Chapter 48's MVCC version reclamation also relies on it)
```

### When **not** to build an index

```text
① Small tables: scanning a few hundred rows takes microseconds; the descent is pure overhead
② Low-selectivity columns: see above — gender/boolean/status
③ Write-heavy, read-light tables: logs and event streams, where write amplification
   eats the entire benefit (measured 6.75×)
④ Columns that never appear in WHERE/ORDER BY/JOIN: it will simply never be used
→ the correct trigger for building an index is【one specific slow query】,
  not "this column looks like it might get queried"
```

> **Note**: for bulk imports, **drop the indexes → import → rebuild** is far faster than maintaining them row by row, because `CREATE INDEX` is a **bulk sorted build** (exactly how the C++ example constructs its tree) while row-by-row insertion is random writes; list existing indexes with `SELECT name, sql FROM sqlite_master WHERE type='index'`; always confirm with `EXPLAIN QUERY PLAN` that an index is actually used.

---

## 5. Python

The Python example is the main arena for index behavior — leftmost prefix, the selectivity crossover, covering indexes, and sort avoidance, all measured.

### The leftmost-prefix rule (measured, forcing `idx_abc`)

For a composite index on `(a, b, c)`:

| Condition | Columns used | Plan |
|-----------|-------------|------|
| `WHERE a = 5` | first **1** | `SEARCH ... COVERING INDEX idx_abc (a=?)` |
| `WHERE a = 5 AND b = 7` | first **2** | `SEARCH ... (a=? AND b=?)` |
| `WHERE a = 5 AND b = 7 AND c = 1` | first **3** | `SEARCH ... (a=? AND b=? AND c=?)` |
| `WHERE a = 5 AND c = 1` | first **1** | `SEARCH ... (a=?)` ← c unusable |
| `WHERE b = 7` | **0** | `SCAN t USING COVERING INDEX idx_abc` |
| `WHERE b = 7 AND c = 1` | **0** | `SCAN t USING COVERING INDEX idx_abc` |

**Why? Think of a composite index as a phone book sorted by province–city–street**:

```text
Know the province → you can locate a section
Know province and city → a smaller section
Know only the street → nowhere to start, because streets are ordered only
【within each province-city group】
→ "a AND c" can use only the a portion: c is ordered within each (a,b) group, not across them
→ so a composite index's【column order】is the boundary of its capability;
  the wrong order means you built it for nothing
```

### Covering indexes: no table lookup at all (measured)

```text
SELECT a, b   (all columns in the index): 0.125 ms  SEARCH ... USING COVERING INDEX idx_abc (a<?)
SELECT a, name (name needs a lookup):     0.240 ms  SEARCH ... USING INDEX idx_abc (a<?)
→ 1.9× slower
```

**The word `COVERING` vanishing from the plan means every matched row requires a table lookup.** This is also the root of Chapter 47's "never write `SELECT *`": **it guarantees covering indexes can never apply**.

### An index's second use: replacing a sort (measured)

```text
ORDER BY a   (indexed):    0.006 ms   SCAN t USING COVERING INDEX idx_abc
ORDER BY name (no index):    9.4 ms   SCAN t | USE TEMP B-TREE FOR ORDER BY
→ 1652× faster
```

**A B+ tree's leaves are already sorted**, so `ORDER BY` just walks the leaf list. **Seeing `USE TEMP B-TREE FOR ORDER BY` means the database is sorting on the fly** — a clear signal to add an index.

> **Note**: `INDEXED BY` / `NOT INDEXED` are sqlite hints, excellent for comparison experiments but **not for production code** (they become shackles once the data distribution changes); the optimizer has no statistics until `ANALYZE` runs; sqlite's `EXPLAIN QUERY PLAN` returns the plan tree while `EXPLAIN` returns bytecode (rarely needed).

---

## 6. Java

The Java example brings the "hash versus ordered tree" question back into memory, proving it is the same problem databases face.

### The correspondence

```text
HashMap          ←→ hash index (MySQL MEMORY engine, PostgreSQL HASH indexes)
                     equality only, no ranges or ordering
TreeMap (red-black) ←→ B+ tree index (nearly every database's default)
                     equality, range, and ordering all supported
→ the difference is【fanout】: a red-black tree holds one key per node (height log₂ n),
  a B+ tree holds hundreds (measured height 3 in C++) — because one is for memory, one for disk
```

### The measurements

```text
① 200k point lookups: HashMap 12.8 ms, TreeMap 70.7 ms (hash 5.5× faster)
② 2000 range queries: HashMap full sweep 7279.6 ms, TreeMap subMap 13.7 ms (ordered 533× faster)
④ 200k insertions:    HashMap 26 ms, TreeMap 53 ms (ordered 2.1× slower)
```

**Those three lines summarize the entire question**: hashes win point lookups, ordered structures win ranges, ordered structures lose on insertion. Databases chose the latter because **range queries and sorting are simply too common**, and that 5.5× point-lookup gap is negligible next to disk I/O.

### What ordered structures give away free (measured)

```text
firstKey=0  lastKey=1999998                 → MIN()/MAX() via index is O(log n), not O(n)
floorKey(1001)=1000  ceilingKey(1001)=1002  → >= / <= / BETWEEN
first keys [0, 2, 4, 6, ...]                → ORDER BY needs no sort
```

> **Note**: to get both ordering and O(1) point lookups, maintain both structures — **which is exactly what databases do**: the primary-key B+ tree stores the data, a secondary index B+ tree stores "column value → primary key," and fetching full rows means a second descent; on the JDBC side, `DatabaseMetaData.getIndexInfo()` lists a table's indexes; never hand-cache an entire table in Java "instead of" an index — Chapter 46 measured that cost (46 MB of heap).

---

## 7. C++

The C++ example is this chapter's key experiment: **a hand-written B+ tree**.

### Bulk construction (exactly how a real `CREATE INDEX` works)

```cpp
// ① first slice the sorted keys into leaves
for (size_t i = 0; i < sorted.size(); i += fanout) { /* each leaf holds `fanout` keys */ }
// link the leaves (range scans depend on this)
for (size_t i = 0; i + 1 < level.size(); ++i) nodes_[level[i]].next = level[i + 1];
// ② build internal levels bottom-up until one root remains
while (level.size() > 1) { /* group every `fanout` nodes under a parent */ }
```

**Why bulk construction beats row-by-row insertion**: row-by-row is **random writes plus frequent node splits**; bulk construction is **one sort followed by sequential filling**. Hence dropping indexes before a large import and rebuilding after is standard practice.

### Point lookups: counting node visits (= disk page reads)

```cpp
bool find(int key, int& visits) const {
    int cur = root_;
    while (true) {
        ++visits;                                    // each node visited = one disk page read
        const Node& n = nodes_[cur];
        if (n.leaf) return std::binary_search(n.keys.begin(), n.keys.end(), key);
        size_t idx = std::upper_bound(n.keys.begin(), n.keys.end(), key) - n.keys.begin();
        cur = n.children[idx];
    }
}
```

**`visits` is this experiment's soul**: timing in memory says little, but **counting node visits** maps directly onto a real database's disk I/O count.

### Range scans: walking the leaf list

```cpp
while (!nodes_[cur].leaf) { /* descend to the leaf containing lo */ }
while (cur != -1) {                                  // then walk the leaf list
    for (int k : nodes_[cur].keys) { if (k > hi) return cnt; if (k >= lo) ++cnt; }
    cur = nodes_[cur].next;
}
```

**These few lines are the entire reason B+ trees beat hashes** (measured 1892×): once the start is found, it is all **sequential reading** with no return to the root.

### An index is data too, and occupies space (measured)

```text
a B+ tree over 1,000,000 keys: 7876 nodes (7813 of them leaves)
if each node is a 4KB page → the index occupies 30.8 MB
```

**Note that leaves are 99.2% of the nodes** — only 63 internal nodes exist. This confirms design decision ①: **internal nodes hold only separator keys, so there are very few; all the data is in the leaves.**

> **Note**: a real B+ tree must also handle **node splits on insert** and **merges on delete** (this example bulk-builds and sidesteps both); nodes usually apply **prefix compression** (adjacent keys share prefixes, raising fanout further); InnoDB's pages are 16KB, PostgreSQL's 8KB.

---

## 8. C#

The C# example answers a lower-level question: **why is a table lookup so expensive?**

### The random-versus-sequential crossover (measured on a 61 MB wide-row table)

```text
matched    1000 rows (  0.1%): lookups 0.0 ms   scan 1.9 ms → index faster
matched   10000 rows (  1.0%): lookups 0.1 ms   scan 1.9 ms → index faster
matched  100000 rows ( 10.0%): lookups 0.7 ms   scan 2.3 ms → index faster
matched  300000 rows ( 30.0%): lookups 2.0 ms   scan 2.3 ms → index faster
matched  500000 rows ( 50.0%): lookups 3.3 ms   scan 1.8 ms →【scan faster】
matched 1000000 rows (100.0%): lookups 6.4 ms   scan 2.3 ms →【scan faster】
```

### Where the gap comes from: cache locality (measured)

```text
reading【the same 1,000,000 rows】: sequential 2.0 ms, shuffled 6.3 ms (3.1× slower)
→ identical row counts; only the【access order】differs
→ the entire gap is CPU cache lines and hardware prefetching
```

**The same principle holds on disk, only the scale changes from "cache line" to "4KB page plus seek"** — which is why real databases cross over much earlier than this example (rule of thumb: 5–20%).

### Clustered versus non-clustered indexes

```text
clustered:     the table data【itself】is physically ordered by the index
  → InnoDB's primary key, SQL Server's clustered index; one per table
  → primary-key range queries are sequential I/O (extremely fast)

non-clustered (secondary): the index stores "column value → primary key"; full rows need a lookup
  → so an InnoDB secondary-index query is【two B+ tree descents】:
    first the index for the primary key, then the primary key for the data
```

**This is the real reason "auto-increment primary keys beat random UUIDs"**:

```text
auto-increment: new rows always append to the【last page】→ sequential writes, high page utilization
random UUID:    new rows land on【random pages】→ every insert may trigger a page split plus a random write
→ not folklore, but a consequence of the clustered index's physical layout
```

> **Note**: EF Core declares indexes with `[Index(nameof(Prop))]` or `modelBuilder.HasIndex(...)`, composite indexes with `.HasIndex(x => new { x.A, x.B })` (**mind the column order**), and covering indexes with `.IncludeProperties(...)` (SQL Server's `INCLUDE`, which widens the index without widening its key); EF Core migrations **will not judge whether an index is worth building** — that is your job.

---

## 9. SQL

This section views indexes through `EXPLAIN`: when they are used and when they are abandoned.

### The four condition shapes an index serves (measured)

```text
equality  amount = 500:               SEARCH ... COVERING INDEX idx_amount (amount=?)
range     amount > 900:               SEARCH ... COVERING INDEX idx_amount (amount>?)
interval  amount BETWEEN 100 AND 200: SEARCH ... COVERING INDEX idx_amount (amount>? AND amount<?)
ordering  ORDER BY amount LIMIT 10:   SCAN ... COVERING INDEX idx_amount (no temp sort)
→ equality, range, interval, and ordering all served — because a B+ tree's leaves are an【ordered list】
```

### Three phrasings that cannot use an index (measured)

```text
expression on the column  amount + 0 = 500:  SCAN
leading wildcard          status LIKE '%ped': SCAN
negation                  amount != 500:      SCAN
```

**The first two were measured in Chapter 47** (1283× and 420× slower). **The third is new**: "not equal" means fetching nearly every row, so an index would be slower — **here the optimizer's refusal is correct**.

### Partial indexes: index only the rows you actually query (measured)

```sql
CREATE INDEX idx_active ON orders(user_id) WHERE status != 'cancelled';
```

```text
the index covers only 75000 / 100000 rows
→ smaller and cheaper to maintain
→ standard for soft-delete tables: the pile of deleted=1 rows need not enter the index at all
```

### Expression indexes: a way out for "functions on columns" (measured)

```sql
CREATE INDEX idx_month ON orders(substr(created, 6, 2));
```

```text
EXPLAIN: SEARCH orders USING INDEX idx_month (<expr>=?)
→ index the expression itself, and SCAN becomes SEARCH again
→ PostgreSQL equivalent: CREATE INDEX ON t (lower(email))
```

**This is the official antidote to "never wrap an indexed column"**: if a function expression is a frequent query condition, build an index on that expression.

### Index metadata

```sql
SELECT name FROM sqlite_master WHERE type = 'index' AND tbl_name = 'orders';
```

```text
→ every index slows INSERT (Python measured 4 indexes → 3.18× slower inserts)
→ periodically check which indexes are never used:
  PostgreSQL's pg_stat_user_indexes.idx_scan, MySQL's sys.schema_unused_indexes
```

> **Note**: sqlite's partial indexes require a deterministic `WHERE` clause; an expression index is used only when the query writes **exactly the same expression**; `REINDEX` rebuilds a badly fragmented index; in sqlite the primary key aliases `rowid` and is inherently clustered.

---

## 10. Cross-Language Comparison

### ① Index-related capabilities

| Capability | SQL | C++ | Java | C# | JavaScript / Python |
|-----------|-----|-----|------|-----|--------------------|
| Ordered structure | B+ tree index | `std::map` (red-black) | `TreeMap` | `SortedDictionary` | none built in / `bisect` |
| Hash structure | hash index (rare) | `unordered_map` | `HashMap` | `Dictionary` | `Object`/`Map` / `dict` |
| Range queries | ✅ `BETWEEN` | `lower_bound` | `subMap` | `GetViewBetween` | hand-written / `bisect` |
| Composite keys | ✅ composite index | `std::tuple` as key | records as keys | tuples as keys | concatenated strings |
| Partial indexes | ✅ `WHERE` clause | ❌ | ❌ | ❌ | ❌ |
| Expression indexes | ✅ | ❌ | ❌ | ❌ | ❌ |
| Covering indexes | ✅ | — | — | — | — |
| ORM-side declaration | — | — | JPA `@Index` | EF `[Index]` | Prisma `@@index` |

### ② Key experiment one: fanout determines height (C++ measured)

```text
1M keys: fanout 2 → height 20 (20 nodes visited)   fanout 128 → height 3 (3 nodes visited)
height growth: 1000 keys → 2; 100k keys → 3; 10M keys → 4
→ each node = one disk page read (~100 μs) → 2 ms vs 0.3 ms
```

### ③ Key experiment two: B+ tree versus hash (C++ / Java measured)

```text
point lookups: hash wins (C++ 0.6 vs 1.2 ms; Java HashMap 5.5× faster)
range queries: B+ tree routs it (C++ 1892×; Java TreeMap 533× faster)
→ databases chose the B+ tree: one structure covering equality/range/ordering/prefix
```

### ④ Key experiment three: what indexes cost (Python / JS measured)

```text
Python: 0→4 indexes, insert 106→337 ms (3.18×), file 5.2→15.9 MB (3.06×)
JS:     INSERT 2.36×, UPDATE 2.97×, DELETE 6.75× (all slower)
```

### ⑤ Key experiment four: indexes are not always faster (Python / C# measured)

```text
Python: 0.00% matched → index 180× faster; 33% matched → full scan 1.6× faster
C#:     crossover between 30% and 50% (in memory); disk rule of thumb 5–20%
source of the gap: sequential 2.0 ms vs shuffled 6.3 ms (3.1×, cache lines and prefetching)
⚠️ sqlite【used the index anyway】on the low-selectivity query — it chose wrong
```

### ⑥ Common ground and root causes

**Common ground**: every language offers both hash and ordered structures, facing exactly the same choice databases do; every ordered structure costs more to insert into than a hash (Java measured 2.1×); and **no language's in-memory structure can replace a database index** — they are not durable, support no concurrent transactions, and require reading the whole table into memory first (Chapter 46 measured 46 MB of heap).

**Root causes**:

- **Databases chose B+ trees over binary trees** because the bottleneck is **disk I/O count**, not comparison count, and fanout flattens the tree (measured 20 → 3);
- **Databases chose B+ trees over hashes** because range queries and sorting are too common (measured 1892× / 533×), and one structure covering four query shapes beats two structures each covering half;
- **Only SQL has partial and expression indexes** because only a database holds both the data and the queries, and can tailor an index to a specific query shape;
- **In-memory languages need no notion of "covering index"** because there is no "table lookup" in memory — data and index share one address space;
- **All of an index's costs come from one fact**: it is a **copy** of the data, and copies must stay in sync (measured 2–7× write amplification).

---

## 11. Implementation Comparison

| Database/engine | Index structure | Key details |
|----------------|----------------|-------------|
| **SQLite** | B-tree (both tables and indexes) | 4KB pages; the table itself is a B-tree keyed by rowid |
| **MySQL/InnoDB** | B+ tree | 16KB pages; **primary key is clustered**, secondary indexes store it → two descents |
| **PostgreSQL** | B-tree (default) + Hash/GiST/GIN/BRIN | 8KB pages; the table is a heap, so **every index is secondary** |
| **SQL Server** | B+ tree | clustered index optional; `INCLUDE` columns cover without widening the key |
| **LSM tree** (RocksDB/Cassandra) | sorted string tables + memtable | **write-optimized**: append and compact in the background; reads consult several levels |

**The key difference between PostgreSQL and InnoDB**:

```text
InnoDB:     the data【lives in the primary key B+ tree's leaves】→ primary-key queries reach it in one descent
            secondary index leaves hold the primary key → full rows need【a second descent】
PostgreSQL: the table is an unordered heap and even the primary key is a secondary index
            → any index query must return to the heap
            but it has index-only scans: if all columns are in the index and the visibility map allows,
            the heap is skipped
→ this explains why InnoDB insists that "primary keys should be short"
  (every secondary index stores a copy of it)
```

**LSM trees are the other road** (Chapter 46's append-only log taken to its extreme):

```text
B+ tree: update in place → fast reads, random-I/O writes
LSM tree: append only + background compaction → very fast writes (sequential I/O),
          reads consult several levels (mitigated by Bloom filters)
→ write-heavy chooses LSM, read-heavy chooses B+ tree — once again "trade one side for the other"
```

---

## 12. Performance Analysis

### This chapter's numbers at a glance

```text
fanout and height: fanout 2 → height 20; fanout 128 → height 3 (1M keys)
height growth:     1000 keys 2 levels; 100k 3 levels; 10M 4 levels
range queries:     B+ tree 0.40 ms vs hash 757.9 ms (1892×)
point lookups (memory): hash 0.6 < bsearch 0.8 < B+ tree 1.2 << full scan 2286 ms
write amplification: insert 3.18× (4 indexes), UPDATE 2.97×, DELETE 6.75×
space:             4 indexes → file 3.06×; a 1M-key B+ tree ≈ 30.8 MB
selectivity crossover: 0.00% matched index 180× faster; 33% matched scan 1.6× faster
covering index:    1.9× (in memory; larger on disk)
sort avoidance:    1652× (ORDER BY with vs without an index)
random vs sequential: 3.1× (same million rows, only the access order differs)
```

### Index optimization priorities

```text
① Find a【specific slow query】first — no slow query, no index
② EXPLAIN it: SCAN or SEARCH?
③ If SCAN → check whether the condition can be served (expression on a column? leading %?)
④ If it can → build the index, high-selectivity columns first in composites
⑤ EXPLAIN again to confirm it is actually used
⑥ Periodically drop indexes that have【never been used】(all cost, no benefit)
```

> ⚠️ **An index's benefits are nonlinear while its costs are linear.** The first index may bring a hundredfold speedup; the tenth will probably never be used — yet it still charges every write. **Index count should be driven by slow queries, not by column count.**

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Deciding whether to index | driven by a specific slow query | "this column might get queried" | measured +40% writes per index |
| Composite column order | high-selectivity, equality columns first | writing them arbitrarily | measured: skip the first and none are usable |
| Low-selectivity columns | as trailing columns in a composite | standalone index | too many matches; lookups lose to a scan |
| Querying only some rows (soft delete) | partial index `WHERE deleted = 0` | full-table index | measured covering only 75% of rows |
| Functions on columns in queries | expression index | giving up on the index | `SCAN` becomes `SEARCH` again |
| A few hot columns | covering index | `SELECT *` | measured 1.9×, and `SELECT *` can never use it |
| Slow `ORDER BY` | index the sort column | more RAM / a bigger machine | measured 1652× |
| Bulk imports | drop → import → rebuild | maintaining during insert | bulk build is sorted filling; row-by-row is random writes |
| Primary key choice | auto-increment / ordered ID | random UUID | random keys cause page splits under a clustered index |
| Frequently updated columns | index cautiously | index by reflex | measured UPDATE 2.97× (delete + insert) |
| Index governance | periodically find and drop unused indexes | only ever adding | all cost, no benefit |
| Verification | `EXPLAIN` every time | trusting "it should be used" | measured: sqlite chooses wrong too |

### The rule of thumb

```text
Before building an index, ask three things:
  ① Is there a【specific】slow query?     → no → don't build it
  ② Is this column's selectivity high?    → distinct/total; too low → not on its own
  ③ Is this table write-heavy or read-heavy? → write-heavy means amplification eats the benefit
And afterwards, always: EXPLAIN to confirm it is actually used
```

---

## 14. Best Practices

- **Let slow queries drive indexes, not column counts**: measured, each index costs +40% on inserts and up to +575% on deletes — and most "preventive" indexes are never used.
- **A composite index's column order is its capability boundary**: measured, skipping the first column makes none of it usable; put **high-selectivity, equality-tested** columns first.
- **Never index a low-selectivity column on its own**: measured, at 33% match a full scan is 1.6× faster — such columns belong only as trailing columns.
- **Use partial indexes when you query only part of the table**: standard for soft-delete tables, measured shrinking the index to 75%.
- **Use expression indexes when queries wrap columns in functions**: the official antidote to Chapter 47's "never wrap an indexed column."
- **Drop and rebuild indexes around bulk imports**: `CREATE INDEX` is a bulk sorted build (the C++ example's construction), far faster than row-by-row maintenance.
- **Prefer auto-increment keys over random UUIDs**: under a clustered index, random keys scatter inserts across random pages, causing splits and random writes.
- **Always verify with `EXPLAIN`**: measured, sqlite **chose wrong** on a low-selectivity query — "the optimizer picks the best plan" has preconditions.

---

## 15. Common Pitfalls

**Pitfall 1 · Indexing every column**

```sql
CREATE INDEX i1 ON t(a); CREATE INDEX i2 ON t(b); ...   -- ⚠️ measured 3.18× slower inserts, 3.06× the file
```

**Avoid it**: let slow queries drive it; periodically find never-used indexes via `pg_stat_user_indexes.idx_scan` and drop them.

**Pitfall 2 · Reversed composite column order**

```sql
CREATE INDEX idx ON t(status, user_id);   -- ⚠️ if queries always filter user_id, this is unusable
CREATE INDEX idx ON t(user_id, status);   -- ✅ high-selectivity column first
```

**Pitfall 3 · A standalone index on a boolean/status column**

```sql
CREATE INDEX idx ON orders(is_paid);   -- ⚠️ two values, 50% match; measured a full scan wins
```

**Pitfall 4 · Assuming a built index will be used**

```sql
-- ⚠️ measured: sqlite chose the index on a low-selectivity query (wrongly),
--    and could not use it at all when the column was wrapped in a function
EXPLAIN QUERY PLAN SELECT ...;   -- ✅ verify every time
```

**Pitfall 5 · Random UUIDs as a clustered primary key**

```sql
id UUID PRIMARY KEY DEFAULT gen_random_uuid()   -- ⚠️ under InnoDB every insert lands on a random page
-- ✅ use auto-increment, or ordered UUIDs (UUIDv7 / ULID)
```

**Pitfall 6 · Keeping indexes during a bulk import**

```python
for row in ten_million_rows:      # ⚠️ every row maintains every index's B+ tree
    conn.execute("INSERT ...")
# ✅ DROP INDEX → import → CREATE INDEX (bulk sorted build)
```

**Pitfall 7 · Forgetting indexes fragment too**

```text
⚠️ measured: after deleting half the rows, page count stayed at 4699;
   only VACUUM brought it down to 2351
✅ PostgreSQL relies on autovacuum; REINDEX when necessary
```

---

## 16. Interview Questions

**Basic**

1. Why does an index speed up queries? What does it cost? (Name at least three.)
2. What is selectivity? Why are low-selectivity columns unsuitable for standalone indexes?
3. What is a covering index? Why can `SELECT *` never benefit from one?

**Intermediate**

4. **Why do databases use B+ trees rather than binary search trees? (Answer via fanout and height.)**
5. Where do hash indexes and B+ tree indexes each apply? Why is the latter the default?
6. **What is the leftmost-prefix rule? Can a `(a,b,c)` index serve `WHERE a AND c`, and to what extent?**

**Advanced**

7. **Why does a database avoid the index when many rows match? (Explain via random versus sequential I/O.)**
8. How do clustered and non-clustered indexes differ? Why is an auto-increment key faster than a random UUID?
9. What does each of B+ trees and LSM trees optimize for? Which workloads suit each?

---

## 17. Exercises

**Basic**

1. Run `EXPLAIN` before and after adding an index and confirm `SCAN` becomes `SEARCH`.
2. Measure how insert speed and file size change as you add indexes one at a time.
3. Find your database's lowest-selectivity column and judge whether its index deserves to exist.

**Intermediate**

4. **Reproduce key experiment one**: implement a B+ tree with configurable fanout (bulk construction is enough) and measure the fanout-to-height relationship.
5. Reproduce the leftmost-prefix experiment: build an `(a,b,c)` index and use `EXPLAIN` to determine how many columns each of six conditions can use.
6. Use `INDEXED BY` / `NOT INDEXED` to find the selectivity crossover on your own data.

**Challenge**

7. **Add insertion and node splitting to your B+ tree** (this chapter bulk-builds and sidesteps them), then measure how many splits a million random insertions cause.
8. Build a covering-index comparison: the same query with all columns in the index versus needing a table lookup, and watch `COVERING` appear and disappear in the plan.
9. Optimize one real slow query with a partial index and another with an expression index, quantifying the improvement.

---

## 18. Chapter Summary

**One sentence**: an index is an **extra, ordered, continuously maintained copy of your data** — it buys query speed with write speed and storage, and this chapter weighed both sides of that trade: on the benefit side, the hand-written B+ tree measured the quantity that decides everything — **fanout determines height** (one million keys: fanout 2 gives height **20**, fanout 128 gives height **3**; and ten-thousandfold more data raises the height only from 2 to 4) — because each node is one disk page read, this directly sets how many I/Os a query costs; and the B+ tree beats the hash index on **range queries** (measured **1892×**, reproduced as **533×** by Java's `TreeMap` versus `HashMap`), covering equality, range, ordering, and prefix with one structure; on the cost side, going from 0 to 4 indexes measured inserts **3.18×** slower and the file **3.06×** larger, with `DELETE` worst at **6.75×**; more importantly **an index is not always faster** — at 4 matching rows it won by **180×**, but at 33% the full scan won by **1.6×**, because table lookups are random I/O while scans are sequential (C# measured a **3.1×** penalty from access order alone), and the measurement also caught **sqlite choosing wrong** on that query; finally the **leftmost-prefix rule** — a `(a,b,c)` index serving `WHERE b` can use **not one column** — meaning **column order is the index's capability boundary**.

**Key takeaways**

- **Fanout determines height** (measured): fanout 2 → 20 levels, fanout 128 → 3; each level is one disk I/O.
- **Height grows extremely slowly**: 1000 keys 2 levels, 10M keys 4 — the basis for "build once, keep working."
- **B+ tree versus hash** (measured 1892× / 533×): hashes answer only equality; B+ trees cover equality/range/ordering/prefix.
- **Three disk-oriented decisions**: data only in leaves, leaves linked into a list, node size = page size.
- **Write amplification** (measured): 4 indexes → insert 3.18×, space 3.06×; `UPDATE` 2.97×, `DELETE` 6.75×.
- **The selectivity crossover** (measured): 0.00% matched index 180× faster, 33% matched scan 1.6× faster; disk rule of thumb 5–20%.
- **Leftmost prefix** (measured): `(a,b,c)` serves `a` / `a,b` / `a,b,c` / `a` (only `a` when `b` is skipped); `b` and `b,c` cannot use it at all.
- **Covering indexes avoid lookups** (measured 1.9×) and **indexes avoid sorts** (measured 1652×) — two frequently overlooked benefits.

**Checklist**

- [ ] I can explain the fanout-height relationship and why B+ trees suit disks.
- [ ] I can state the B+ tree's decisive advantage over hashes and its measured magnitude.
- [ ] I can quantify an index's three costs (insert, update/delete, space).
- [ ] I can use selectivity to judge whether an index is worth building.
- [ ] I can apply the leftmost-prefix rule and verify with `EXPLAIN`.

**Next chapter**: Chapter 48 repeatedly mentioned row locks, gap locks, and needing `SELECT ... FOR UPDATE` against write skew, without unpacking any of them. Chapter 50 covers **database locks**: we will measure the shared/exclusive lock compatibility matrix, how row locks escalate into table locks, why **gap locks** block phantoms and why they create unexpected deadlocks, and how databases **detect deadlocks automatically and roll back the cheaper side** (Chapter 41's `findDeadlockedThreads()` finally gets a genuinely useful counterpart) — closing with a practical question: **when a lock wait times out, should you retry or fail?**

---

## 19. Further Reading

- <a href="https://use-the-index-luke.com/" target="_blank" rel="noopener">Use The Index, Luke!</a> — the best free tutorial on indexes and SQL performance; the systematic version of this chapter.
- <a href="https://www.sqlite.org/optoverview.html" target="_blank" rel="noopener">SQLite · Query Optimizer Overview</a> — how the optimizer decides whether to use an index.
- <a href="https://www.sqlite.org/partialindex.html" target="_blank" rel="noopener">SQLite · Partial Indexes</a> — partial indexes, officially.
- <a href="https://www.postgresql.org/docs/current/indexes.html" target="_blank" rel="noopener">PostgreSQL Docs · Indexes</a> — where B-tree/Hash/GiST/GIN/BRIN each apply.
- <a href="https://dev.mysql.com/doc/refman/8.0/en/innodb-index-types.html" target="_blank" rel="noopener">MySQL Docs · InnoDB index types</a> — clustered and secondary indexes, officially described.
- <a href="https://en.wikipedia.org/wiki/B%2B_tree" target="_blank" rel="noopener">Wikipedia · B+ tree</a> — the theory behind this chapter's key experiment.
- <a href="https://en.wikipedia.org/wiki/Log-structured_merge-tree" target="_blank" rel="noopener">Wikipedia · LSM tree</a> — the other road, and the basis of write-optimized storage engines.
- <a href="https://www.postgresql.org/docs/current/indexes-index-only-scans.html" target="_blank" rel="noopener">PostgreSQL Docs · Index-Only Scans</a> — covering indexes in PostgreSQL, with their limitations.
- <a href="https://dataintensive.net/" target="_blank" rel="noopener">Designing Data-Intensive Applications</a> — Chapter 3's B-tree versus LSM-tree comparison is the best continuation.
