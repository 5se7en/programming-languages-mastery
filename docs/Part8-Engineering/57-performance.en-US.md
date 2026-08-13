# Chapter 57 · Performance Optimization

[简体中文](./57-performance.md) ｜ **English**

---

> Performance is the chapter where your intuition is least reliable. Across the previous 56 chapters, intuition mostly worked — think carefully about how a type system should be designed, how concurrency should synchronize, how transactions should isolate, and you'd largely be right. **Performance doesn't work that way.** It depends on cache hierarchies, compiler rewrites, JIT state, branch predictors, and the generational structure of the GC — none of which appear anywhere in your source code.
>
> This chapter's **key experiment** puts two functions in a Python program: one does string formatting inside a loop (`slow_looking`), the other does a single membership test (`actually_slow`). By intuition, almost everyone would optimize the first. Measured: `slow_looking` **5.0 ms (1%)**, `actually_slow` **426.2 ms (99%)**. Optimizing the suspicious-looking one to perfection buys **1.01x** overall. The real bottleneck needs one `[]` changed to `set()` — **191x faster, two lines**.
>
> That 1% vs 99% is not a contrived example; it's the norm in real projects. **Amdahl's law** gives it a mathematical form: optimizing a portion that takes fraction p of the time has a speedup ceiling of `1/(1-p)`. A hotspot worth 1% of runtime, sped up a thousandfold, still yields only 1.01x overall. So the first step in performance work is never "how do I optimize this" — it's **"what should I optimize"** — and the only reliable way to answer that is a profiler, not reading code.
>
> The C++ examples quantify the variable that lives outside big-O. `vector` and `list` traversal are both O(n) — textbooks call them equivalent. Measured: **0.3 ms vs 3.6 ms, a 12.8x gap.** A cleaner control uses the same array, the same number of elements, and only changes the access order: sequential **0.6 ms**, shuffled **4.0 ms**, a **7.0x gap**. Identical instruction counts, identical total memory touched — the difference is 100% **cache and hardware prefetch**. That's Chapter 16's cache locality showing its true weight on real data structures.
>
> But this chapter's most valuable experiments are **the ones that failed**. The highest-voted question in Stack Overflow history — "why is processing a sorted array faster" (branch prediction) — **does not reproduce** under today's compilers: measured gap **1.0x**. Not because branch prediction stopped mattering, but because the compiler turned the `if` into a **conditional move** (`csel`/`cmov`) — there is no branch left to predict. Force a real branch back in with a memory barrier and the gap returns to **11.0x**. Likewise, the widely repeated JavaScript advice that "objects with different property orders are slower" measures at **3% for 4 shapes** — V8's polymorphic inline caches have handled a handful of shapes efficiently for years. The real cliff is at **30 shapes (2.87x)**.
>
> The Java examples show the most dangerous illusion in microbenchmarking: the same function called twenty million times costs **0.0 ms when the return value is discarded and 4.5 ms when it's accumulated — a factor of 4547**. No optimization made it 4547x faster; the C2 compiler decided nobody used the result and **deleted the entire twenty-million-iteration loop**. And the C# examples correct the crude claim that "heap allocation is expensive": allocating **305 MB** of pure garbage and triggering 38 Gen0 collections costs only about **1.2x**. What actually determines GC cost is the **live set** — a full collection with 0 / 1M / 4M live objects measured **1.0 / 3.8 / 13.6 ms**.
>
> In one sentence: **performance folklore expires; measuring on the spot doesn't.**

## 1. Learning Objectives

After this chapter you will be able to:

- Use **Amdahl's law** to compute an optimization's **ceiling before writing any code**, and explain why "picking the right place" beats "optimizing thoroughly";
- Use each language's profiler to locate hotspots, and explain why **guessing from source code fails so often** (measured: 1% vs 99%);
- Quantify the impact of **cache locality**, and explain how two O(n) loops differ by an order of magnitude;
- Recognize the three microbenchmark traps — **no warmup, dead-code elimination, single run** — and write measurement code that doesn't lie to you;
- Distinguish **allocation volume from live-set size** in GC languages, and explain why allocation volume is the better metric;
- Judge whether a piece of performance folklore has expired, and know **how to verify it on the spot**.

---

## 2. Why This Concept Exists

### 2.1 A Real Scenario

An endpoint got slow. You open the code and find this:

```python
def build_report(orders):
    lines = []
    for o in orders:                          # ten thousand orders
        for item in o.items:                  # ~5 items each
            lines.append(format_line(item))   # triple nesting, looks suspicious
    return "\n".join(lines)
```

Triple nesting, fifty thousand string formats — "the bottleneck must be here." So you spend two days rewriting `format_line` with precompiled templates, and locally that section gets 3x faster. In production **the endpoint's latency barely moves**.

The reason: that code is 4% of total time. The other 96% is somewhere you never looked — `o.items` is an ORM lazy-loaded property that issues a SQL query per access (Chapter 51 measured this: **201 queries vs 1**).

This isn't a made-up story. It's the most common failure mode in performance work, and it has exactly one root cause: **optimizing before measuring.**

### 2.2 Why Intuition Fails Specifically Here

Across the previous 56 chapters, your intuition rested on **source-level semantics** — what this code does, who it calls, how state changes. That intuition is adequate for correctness.

But performance isn't determined by source semantics. It's determined by **physical reality at execution time**:

| What you see | What actually happens | Source of the gap |
|---|---|---|
| `for x : list` and `for x : vector` are both O(n) | One reads memory sequentially; one jumps to random heap addresses | **Cache** (measured 12.8x) |
| `if (x >= 128)` is a branch | The compiler may turn it into a branchless conditional move | **Compiler rewrite** (gap collapses from 11x to 1x) |
| `pure(i)` is called twenty million times | The JIT sees nobody uses the result and deletes the loop | **Dead-code elimination** (a 4547x illusion) |
| `new Point(...)` is a heap allocation | The JIT did scalar replacement — nothing was allocated | **Escape analysis** (11.6x per-op cost difference) |
| `obj.x` is one property read | Could be one machine instruction, or a hash lookup | **Inline cache state** (measured 2.87x) |
| `alloc 305 MB` sounds expensive | If it's all garbage, the GC never touches it | **Generational hypothesis** (measured cost: 1.2x) |

**Not one of those six rows can be seen from the source.** They all have to be measured.

### 2.3 Amdahl's Law: The Ceiling

Gene Amdahl proposed a formula about parallel speedup in 1967, but it holds for any local optimization:

```
                     1
max speedup = ───────────────────
               (1 - p) + p / s
```

where `p` is the fraction of total time spent in the optimized part and `s` is that part's speedup. Let `s → ∞`:

```
max speedup = 1 / (1 - p)
```

The Python example verifies this empirically:

```
Speed up "string building" (1% of time)     2x → total 431 → 429 ms (overall 1.01x)
Speed up "string building" (1% of time)    10x → total 431 → 427 ms (overall 1.01x)
Speed up "string building" (1% of time)  1000x → total 431 → 426 ms (overall 1.01x)
Speed up "membership test" (99% of time)    2x → total 431 → 218 ms (overall 1.98x)
Speed up "membership test" (99% of time)   10x → total 431 →  48 ms (overall 8.94x)
Speed up "membership test" (99% of time) 1000x → total 431 →   5 ms (overall 79.9x)
```

**A part worth 1%, sped up a thousandfold, still yields 1.01x.** This is why "profile first" isn't polite advice — it's a **mathematical gate**: without knowing p, you cannot know whether the work is worth doing.

### 2.4 How This Chapter Relates to Earlier Ones

Performance is where the whole book's methodology converges. Every experiment here builds on a mechanism from an earlier chapter:

| Prerequisite | Mechanism it provides | How this chapter uses it |
|---|---|---|
| Ch. 3 Compilation | Compiler optimization, JIT | Dead-code elimination, conditional-move rewrite |
| Ch. 16 Arrays | Cache locality | vector vs list, row-major vs column-major |
| Ch. 20 Hash tables | O(1) lookup | The 191x from list → set |
| Ch. 33 Memory allocation | Pointer bumping, TLAB | Separating allocation cost from survival cost |
| Ch. 36 Garbage collection | Generational hypothesis | GC pause grows with live set |
| Ch. 41 Memory model | Reordering, barriers | Using a barrier to force a real branch |
| Ch. 49 Indexes | B+ trees, selectivity | SQL execution-plan diagnosis |
| Ch. 51 ORM | N+1, lazy loading | First step of database-side diagnosis |
| Ch. 52 Testing | Warmup, reproducibility | The three microbenchmark traps |

In other words: **this chapter introduces no new mechanism. It teaches you how to find which mechanism to reach for in a real system.**

---

## 3. How It Works Underneath

### 3.1 Latency Magnitudes: A Table Worth Memorizing

Performance intuition rests on **knowing which operations differ by orders of magnitude**. Here is a widely circulated set (absolute values vary by hardware, but the **ratios** are remarkably stable):

```
L1 cache hit                   ~1 ns        ┐
L2 cache hit                   ~4 ns        │ 100x
Main memory (cache miss)       ~100 ns      ┘

Branch misprediction           ~10-20 ns
System call                    ~100 ns - 1 μs
SSD random read                ~100 μs      ┐
                                            │ 100x
HDD seek                       ~10 ms       ┘
Same-datacenter round trip     ~0.5 ms
Cross-continent round trip     ~150 ms
```

What matters isn't the absolute numbers but the **steps**: a cache miss costs ~100x a hit, disk ~1000x memory, cross-continent ~300x same-datacenter.

This table directly explains this chapter's results. The C++ vector vs list case: identical instruction counts, but every list node is a potential cache miss. **12.8x = 100x per-miss cost × the fraction that actually miss.**

### 3.2 Cache Locality: Why "Same O(n)" Differs by 10x

CPUs don't read memory byte by byte; they read **cache lines** (typically 64 bytes). Which means:

```
read vec[0]  → hardware pulls vec[0..15] into L1 as one line (16 ints)
read vec[1]  → already in L1, essentially free
...
read vec[15] → already in L1
read vec[16] → new line, but the prefetcher predicted it and it's already in flight
```

Versus a linked list:

```
read node1->val → one cache miss (~100 ns)
node1->next     → points to some random heap address
read node2->val → another miss (the prefetcher cannot predict random addresses)
```

**Big-O counts operations, not what each operation costs.** This is the largest blind spot left by algorithms courses.

The C++ example isolates the variable with a cleaner experiment — **same array, same elements, only the access order changes**:

```cpp
for (int i : idxSeq)  a1 += vec[i];   // sequential: 0,1,2,3,...
for (int i : idxRand) a2 += vec[i];   // shuffled: same indices, reshuffled
```

Identical instruction counts, identical memory touched, identical results. Measured **0.6 ms vs 4.0 ms (7.0x)** — the gap is 100% cache and prefetch.

### 3.3 The Compiler: What You Wrote Isn't What Runs

Modern compilers rewrite your code substantially. Three rewrites bear directly on measurement:

**① Dead-code elimination (DCE)** — if a computation's result is unused, the whole computation can be deleted.

**② Conditional move** — `if (c) x = a;` can compile to "compute both, select by condition" branchless instructions (x86 `cmov`, ARM `csel`). This removes the misprediction penalty — and also **removes the branch-prediction experiment**.

**③ Scalar replacement** — if an object doesn't escape the current method, the JIT can put its fields directly in registers and **never allocate at all**.

The shared consequence: **a microbenchmark may not be measuring what you think it is.** The C++ and Java examples each fell into this trap, and both are preserved in the code — because falling in is the lesson.

### 3.4 JIT: One Piece of Code Has Several Speeds

On the JVM, CLR, and V8, code speed depends on **how many times it has already run**:

```
runs 1..N:      interpreted bytecode              (slow)
counter trips:  C1 / baseline compiler → machine code  (medium)
stays hot:      C2 / optimizing compiler, aggressive   (fast)
assumption breaks: deoptimize, fall back to interpreter
```

The Java example runs the same `compute(2_000_000)` eight times:

```
1.7  1.2  1.0  0.9  0.9  0.9  0.9  0.9  ms
run 1: 1.7 ms → run 8: 0.9 ms (2.0x faster)
```

**Timing without warmup measures the interpreter** — which has nothing to do with production. Chapter 52 measured the consequence: skipping warmup dropped a parallel speedup from 5.78x to 1.92x.

CPython has no JIT, and the Python example measures a warmup effect **too small to detect** (±2%, inside the noise band). **Whether to warm up is itself language-dependent** — the easiest thing in this chapter to get wrong by copying habits across languages.

### 3.5 GC: Allocation Is Cheap, Survival Is Expensive

Chapter 33 covered pointer bumping: allocating an object is often just `ptr += size`, a few nanoseconds. Chapter 36 covered the generational hypothesis: **most objects die young**.

Put those together and you get a counterintuitive conclusion: **discarded garbage is nearly free.** A generational GC's mark-copy pass walks only the **live** objects and moves them — **dead objects are never touched at all**.

The C# example measures this causality directly:

```
live objects        0, one full Gen2 collection:    1.0 ms
live objects  1000000, one full Gen2 collection:    3.8 ms
live objects  4000000, one full Gen2 collection:   13.6 ms
```

Time grows monotonically with the **live set**, essentially independent of how much garbage was produced. So the accurate principle isn't "don't allocate":

> **Don't let short-lived objects survive Gen0.** What deserves suspicion are caches and static collections — they pin objects in the old generation, and every full collection walks them again.

### 3.6 Shape Assumptions in Dynamic Languages: Inline Caches

V8, the CLR, and the JVM all accelerate dynamic dispatch the same way: **assume next time looks like last time**.

```
obj.x first execution → look up x's offset, cache "hidden class H → offset 8" at this site
obj.x again           → if the object is still H, read offset 8 (one instruction)
                        if not, add another entry (polymorphic)
                        too many shapes (>4~8) → give up caching, fall back to hash lookup (megamorphic)
```

The JavaScript example measures all three tiers:

```
monomorphic (1 shape):   18.0 ms
polymorphic (4 shapes):  18.5 ms (1.03x)
megamorphic (30 shapes): 51.5 ms (2.87x)
```

**Key finding: a handful of shapes costs essentially nothing.** This directly refutes the widespread "different property order makes it slow" advice — polymorphic inline caches handle a few shapes efficiently. The cliff is megamorphism: a generic function processing objects from a dozen different APIs. That's the real problem.

---

## 4. JavaScript

The JS example focuses on V8's **object shapes** — the largest hidden variable in JS performance.

### 4.1 The Three Inline-Cache Tiers

```javascript
/** Build an array whose objects have `shapes` distinct shapes */
function buildShapes(shapes) {
  const arr = [];
  for (let i = 0; i < N; i++) {
    const o = { x: i };                                   // every object has x
    for (let k = 0; k < i % shapes; k++) o['f' + k] = k;  // varying extra fields → distinct hidden classes
    arr.push(o);
  }
  return arr;
}

// Critical: build a FRESH function each time so it gets its own inline cache
const freshSum = () => new Function('arr', 'let s=0;for(const o of arr)s+=o.x;return s;');
```

That `new Function` is not decoration. **The first version of this experiment measured backwards**: all three arrays shared one summing function, whose inline cache had seen all three cases, so all three tiers came out equally slow. **The measurement code became part of the system under test** — the subtlest trap in microbenchmarking.

```
monomorphic (1 shape):   18.0 ms
polymorphic (4 shapes):  18.5 ms (1.03x)
megamorphic (30 shapes): 51.5 ms (2.87x)
```

### 4.2 Array Elements Kind

V8 arrays have an **elements kind**: SMI (small integer) → DOUBLE → generic. Transitions are **one-way; it never upgrades back**.

```javascript
const mixed = new Array(M);
for (let i = 0; i < M; i++) mixed[i] = i;
mixed[Math.floor(M / 2)] = 'oops';             // ⚠️ one string degrades the whole array
```

```
sum of all-SMI array:      2.1 ms
sum of all-double array:   1.8 ms
after mixing in a string:  3.7 ms (1.7x slower than SMI)
```

**One string is enough to demote an entire array from packed storage to boxed generic storage.** For maximum performance use a `TypedArray` (`Float64Array`) — its type simply doesn't permit the demotion.

### 4.3 The Cost of `delete`

```javascript
const o = { x: i, tmp: 0, y: i };
delete o.tmp;                                  // ⚠️ breaks the hidden-class chain → dictionary mode
```

```
sum of 1000000 objects keeping tmp:  5.9 ms
after delete o.tmp:                 10.7 ms (1.8x slower)
using o.tmp = null instead:          7.7 ms (1.3x slower)
```

`delete` makes V8 convert the object to **dictionary mode** (properties in a hash table). To clear a field, assign `null` — measurably faster than `delete`, though still not as good as never having the field.

### 4.4 But Don't Rush to Optimize These

```
Frontend: network round trips > main-thread blocking > reflow/repaint >> JS execution details
Node:     I/O waits > serialization (JSON) > database queries >> JS details
```

The three experiments above show multi-fold gaps, but in a real application they act on a few percent of total time. **Amdahl's law is always the first gate.**

---

## 5. Python

The Python example carries the chapter's methodological spine: **guess first, then measure, then see whether you guessed right**.

### 5.1 The Key Experiment

```python
def slow_looking(n):
    """Looks most suspicious: string building in a loop"""
    parts = []
    for i in range(n):
        parts.append(str(i))
    return "".join(parts)


def actually_slow(n):
    """Looks innocent: one membership test — but `seen` is a list, so it's an O(len) scan"""
    seen = []                                    # ⚠️ a list used as a set
    for i in range(n):
        key = i % 3000                           # 3000 distinct values → list grows to 3000
        if key not in seen:                      # ⚠️ this line is O(len(seen))
            seen.append(key)
    return len(seen)
```

```
slow_looking:       5.0 ms (1% of total)
actually_slow:    426.2 ms (99% of total)
```

`key not in seen` is **one line** that reads like a lookup and is actually a linear scan — and the scan gets slower as `seen` grows. An O(n²) hides inside an `in`.

### 5.2 The Profiler Points Straight at It

```
   ncalls  tottime  percall  cumtime  percall filename:lineno(function)
        1    0.427    0.427    0.427    0.427 main.py:18(actually_slow)
        1    0.006    0.006    0.008    0.008 main.py:10(slow_looking)
        1    0.000    0.000    0.435    0.435 main.py:36(workload)
```

Sorted by `tottime`, **the first row is the answer**. This step costs one minute; guessing wrong costs days of useless work.

> The distinction between `tottime` (time in the function itself) and `cumtime` (including callees) matters: use `tottime` to find hotspots, `cumtime` to find the most expensive call chain.

### 5.3 The Fix Is Usually Small

```python
def fast_version(n):
    seen = set()          # this one change
    for i in range(n):
        seen.add(i % 3000)
    return len(seen)
```

```
membership test with a list:  413.1 ms
with a set:                     2.2 ms (191x faster)
```

**Diff size: one `[]` becomes `set()`.** Payoff: an O(n) membership test becomes O(1) (Chapter 20, hash tables).

Performance work is rarely fine-grained polishing. Most of the time it's **finding the one data structure that was chosen wrong**.

### 5.4 Three Microbenchmark Traps

```
Trap 1 — measuring an empty loop:
  timed(lambda: None) is 0.028 μs per call — that's the measurement overhead itself
  → if the code under test is faster than this, you're measuring noise

Trap 2 — forgetting warmup:
  first run 9.6 ms vs warmed average 9.8 ms (-2%, inside the noise band)
  → Python has no JIT; the warmup effect is TOO SMALL TO DETECT

Trap 3 — running it once:
  same code, 7 runs: min 9.4 / median 9.5 / max 9.7 ms (spread 1.03x)
  → an optimization is only real if the gap EXCEEDS this spread
```

The third is the most-skipped: **if your optimization yields 5% and your measurement varies by 10%, you have proved nothing.**

---

## 6. Java

The Java example is dedicated to why JVM microbenchmarks are so hard to write.

### 6.1 Dead-Code Elimination: A 4547x Illusion

```java
/** A pure function — trivially inlined, therefore trivially eliminated */
static int pure(int x) { return x * 31 + 7; }

/** Put the loop in its OWN method so C2 compiles it normally (in main it only gets OSR compilation) */
static void discardLoop(int n) { for (int i = 0; i < n; i++) pure(i); }
static long accumulateLoop(int n) {
    long s = 0;
    for (int i = 0; i < n; i++) s += pure(i);
    return s;
}
```

```
calling pure() 20000000 times, DISCARDING the result:     0.0 ms
calling pure() 20000000 times, ACCUMULATING the result:   4.5 ms
gap 4547x (sum = 6199999830000000, proving it really computed)
```

The discarding version drops to zero: C2 decided nobody uses the result and **deleted the entire twenty-million-iteration loop**.

This is the most common illusion in microbenchmarking: you think you measured "100x faster after optimization," when in fact **the optimized code never ran**.

> That comment about putting the loop in its own method was earned the hard way: with the loop written inline in `main`, the two versions measured 324 ms vs 183 ms — a 1.8x gap that contradicted an isolated test entirely. The root cause is that a loop in `main` can only get **OSR (on-stack replacement) compilation**, which optimizes differently from standard C2 compilation. **Which method your benchmark lives in changes the result.**

### 6.2 Escape Analysis

```java
for (int i = 0; i < M; i++) {
    int[] pair = {i, i + 1};        // local, doesn't escape → scalar replacement, no allocation
    sumA += pair[0] + pair[1];
}
```

```
non-escaping objects, 20000000 iterations:  11.1 ms (0.55 ns each)
escaping objects,      1000000 iterations:   6.4 ms (6.42 ns each)
per-operation cost differs 11.6x
```

Non-escaping objects get **scalar replaced**: fields go straight into registers, no heap allocation. This is part of why "Java object allocation is expensive" is outdated.

But it cuts the other way: **if the objects in your microbenchmark don't escape, you aren't measuring real allocation cost.**

### 6.3 String Concatenation: One of the Few You Can Judge Without Measuring

```
s += "x" in a loop, 20000 times:      14.7 ms
StringBuilder.append, 20000 times:     2.3 ms (6x faster)
```

`String` is immutable (Chapter 9) → each `+=` copies the whole string → O(n²).

Note the boundary: **`a + b + c` within one statement is compiled to a `StringBuilder`, but accumulation across loop iterations is not.**

---

## 7. C++

The C++ example quantifies Chapter 16's cache locality — and shows how a compiler can destroy a classic experiment.

### 7.1 Four Cache Experiments

```
vector traversal, 4000000 elements:     0.3 ms
list traversal,   4000000 elements:     3.6 ms (12.8x slower)

sequential access, 4000000 elements:    0.6 ms
shuffled access, THE SAME elements:     4.0 ms (7.0x slower)

row-major traversal, 2000x2000:         0.3 ms
column-major traversal, 2000x2000:      1.1 ms (4.1x slower)

AoS (array of 64-byte structs), summing x only:  1.1 ms
SoA (separate x array),         summing x:       0.5 ms (2.2x faster)
```

Four experiments, four forms, one cause: **whether the data is contiguous in memory**.

AoS vs SoA (Array of Structs vs Struct of Arrays) deserves a note:

```cpp
struct Particle { float x, y, z, vx, vy, vz; char pad[40]; };  // 64 bytes
std::vector<Particle> aos;      // summing x still pulls all 64 bytes into cache
std::vector<float>    soaX;     // pulls only x; one cache line holds 16 of them
```

Game engines and numerical code use SoA heavily for exactly this reason (**data-oriented design**).

### 7.2 The Branch-Prediction Experiment That Failed

"Why is processing a sorted array faster than an unsorted array" is one of the highest-voted questions in Stack Overflow history. This chapter set out to reproduce it and **could not**:

```
Version A (compiler free to optimize):
  sorted 0.3 ms   unsorted 0.3 ms   → gap only 1.0x
```

Diagnosis: recompiling with `-O0` brought the gap back to 2.3x; then `objdump | grep csel` found a `csel` instruction. **The compiler turned the `if` into a conditional move** — compute both paths, select by condition — so there is no branch and nothing to predict.

The fix was a two-version design, using a memory barrier to force a real branch:

```cpp
// Version A: let the compiler do as it pleases
for (int x : sorted_) if (x >= 128) c1 += x;

// Version B: a memory barrier FORCES a real branch (the barrier may only run when the condition holds,
// so the compiler cannot use a conditional move)
for (int x : sorted_) if (x >= 128) { __asm__ volatile("" ::: "memory"); c3 += x; }
```

```
Version A (compiler free):        sorted 0.3 ms   unsorted  0.3 ms   → gap only 1.0x
Version B (barrier forces branch): sorted 0.9 ms   unsorted 10.2 ms   → gap 11.0x
```

**Branch prediction still matters (11x), but the classic experiment can no longer detect it.** That's this chapter's core claim: performance folklore expires.

### 7.3 Build Configuration: A Variable That Can Invert Conclusions

This repository's `run-all.sh` compiles without any `-O` flag (i.e. `-O0`) so every chapter stays reproducible. For the same source:

| | `-O0` (harness default) | `-O2` |
|---|---|---|
| vector traversal, 4M | 11.4 ms | **0.3 ms** |
| vector vs list | 1.7x | **12.8x** |
| branch prediction, version A | 2.4x (real branch) | **1.0x (rewritten to csel)** |

**The same benchmark yields opposite conclusions at two optimization levels.** So the example prints its own build configuration:

```cpp
#ifdef __OPTIMIZE__
    printf("[Build] Optimization enabled (-O2 or higher) — the correct config for measurement\n");
#else
    printf("⚠️ [Build] No optimization (-O0) — every number below deviates from real performance\n");
#endif
```

**Any performance number must state its build configuration**, or it isn't reproducible and therefore isn't evidence.

---

## 8. C#

The C# example targets the most common cost in GC languages — **allocation on the hot path** — and corrects a widely repeated but crude claim.

### 8.1 How Expensive Is "Expensive" Heap Allocation?

```csharp
record Point(double X, double Y);                 // reference type: every new goes on the heap
readonly record struct PointS(double X, double Y); // value type: stack, no GC
```

```
record class (heap-allocated every time): 163.2 ms, allocated 305 MB, 38 Gen0 collections
record struct (stack, zero allocation):    77.7 ms, allocated   0 MB,  0 Gen0 collections
→ only 2.1x faster — FAR less than intuition predicts
```

305 MB allocated, 38 Gen0 collections triggered, and the cost is about 2x (repeated runs range 1.2x–2.1x).

So what does determine GC cost? Measure it directly:

```
live objects        0, one full Gen2 collection:    1.0 ms
live objects  1000000, one full Gen2 collection:    3.8 ms
live objects  4000000, one full Gen2 collection:   13.6 ms
```

**Collection time grows with the live set, essentially independent of how much garbage died.**

### 8.2 Allocation Volume Is the Stable Metric

While writing this chapter, repeated runs of the same code produced times ranging over 1.2x–2.1x, while the **allocation numbers were identical every single time** (305 MB / 859.1 MB / 427 MB / 114 MB).

That's the C# example's core methodological advice:

> **In a GC language, look at allocation volume before elapsed time.** Allocation volume is deterministic; elapsed time swings with GC timing.

`GC.GetTotalAllocatedBytes()` is the API used here; BenchmarkDotNet reports it by default.

### 8.3 Three Typical Hidden Allocations

```
① String concatenation
  s += "x" in a loop, 30000 times:  76.290 ms, allocated  859.1 MB
  StringBuilder.Append, 30000 times: 0.040 ms, allocated   61.0 KB
  → 14417x difference in allocation

② Substring vs Span
  Substring(50,100), 2000000 times:  25.7 ms, allocated  427 MB, 54 Gen0
  AsSpan(50,100),    2000000 times:   4.7 ms, allocated    0 KB,  0 Gen0
  → 6x faster, essentially zero allocation

③ Boxing
  boxing to object, 5000000 times:  18.9 ms, allocated  114 MB
  generic List<int>, 5000000 times: 10.1 ms, allocated    8 KB
  → 1.9x faster
```

`Span<T>` is **a view into existing memory** (Chapter 34's pointer, kept in line by the type system) — modern parsers and serializers are built on it precisely to avoid copying just to look.

The boxing case echoes Chapter 29: **C# generics are reified** (specialized at runtime), while Java erases types and must box — so Java's `List<Integer>` cannot avoid this cost. It's a direct performance consequence of two generics designs.

### 8.4 Build Configuration (Again)

The example detects and prints:

```
⚠️ This run is in DEBUG configuration (dotnet run's default) —
   Debug disables most JIT optimizations; every number above differs in Release
```

BenchmarkDotNet **refuses to run in Debug** for exactly this reason.

---

## 9. SQL

Database performance diagnosis has its own workflow, but its first step is identical to every other language: **look at the facts, don't guess.**

### 9.1 EXPLAIN Is the Database's Profiler

```sql
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id = 42;
```

```
`--SCAN orders                                    ← full scan of 200,000 rows
```

After creating an index:

```
`--SEARCH orders USING INDEX idx_user (user_id=?)  ← index used
```

**That one line is the diagnosis.** No application code to read, nothing to guess.

### 9.2 Three Ways a Query Disables Its Own Index

```sql
-- ⓐ function/expression on the column
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE user_id + 0 = 42;   → SCAN

-- ⓑ LIKE with a leading wildcard
EXPLAIN QUERY PLAN SELECT * FROM orders WHERE status LIKE '%ped'; → SCAN

-- ⓒ low-selectivity column
status='paid' matches 50000 / 200000 rows — Chapter 49 measured that a full scan wins here
```

**After adding an index, always EXPLAIN again to confirm it's actually used.** "I added an index and it's still slow" is one of these three nine times out of ten.

### 9.3 Covering Indexes and Aggregate Pushdown

```
`--SEARCH orders USING COVERING INDEX idx_user_amount (user_id=?)
```

`COVERING INDEX` in the plan means every needed column is in the index — no table lookup. And `SELECT *` guarantees this optimization **can never apply** (Chapter 47).

Aggregate pushdown is the same idea:

```
ⓐ pull every row back and sum in the application → 200000 rows transferred
ⓑ push SUM(amount) into the database             → 1 row transferred
```

Chapter 51 measured that bill: 5 rows vs 100000 rows, **86x slower**.

### 9.4 Deep Pagination

```sql
SELECT * FROM orders ORDER BY id LIMIT 10 OFFSET 100000;      -- scans and discards 100,000 rows first
SELECT * FROM orders WHERE id > 100000 ORDER BY id LIMIT 10;  -- keyset pagination, seeks directly
```

`OFFSET` gets slower the deeper you page; keyset pagination is independent of page number.

### 9.5 Diagnostic Order

```
ⓐ Query count: is it N+1? (Chapter 51 measured 201 queries vs 1)
ⓑ Execution plan: SCAN or SEARCH?
ⓒ Data volume: how many unused columns is SELECT * pulling?
ⓓ Locks and transactions: waiting on a lock? transaction too long? (Ch. 48/50)
ⓔ Only then: add indexes, change schema, add caching
```

**Note that ⓐ–ⓒ require no database changes at all** — the overwhelming majority of "the database is slow" is actually a query-writing problem.

In production, find slow queries with `pg_stat_statements` (PostgreSQL) or `performance_schema` (MySQL). What makes them useful is that they sort by **total time**, not per-execution time: **a 1 ms query running ten thousand times a second is worth more attention than a 3-second query that runs once a day.** That's Amdahl's law in database form.

---

## 10. Cross-Language Comparison

| Dimension | JavaScript | Python | Java | C++ | C# | SQL |
|---|---|---|---|---|---|---|
| **Dominant variable** | Object shape | Data-structure choice | JIT state + GC | Cache locality | Allocation vs survival | Execution plan |
| **Needs warmup** | Yes (V8 JIT) | **No** (measured ±2%) | Yes (measured 2.0x) | No (AOT) | Yes (CLR JIT) | Yes (buffer pool) |
| **Primary profiler** | `--cpu-prof` / DevTools | `cProfile` | async-profiler / JFR | `perf` / Instruments | dotnet-trace | `EXPLAIN` |
| **Benchmark framework** | none standard | `timeit` | **JMH** | Google Benchmark | **BenchmarkDotNet** | — |
| **Biggest trap** | IC polluted by the harness | overhead exceeds subject | **dead-code elimination** | **compiler rewrites the experiment** | Debug/Release config | index not actually used |
| **Most stable metric** | min of N runs | median of N runs | JMH forked mean | min of N runs | **allocation volume** | logical reads |
| **Largest gap measured here** | 2.87x (megamorphic) | **191x** (list→set) | **4547x** (DCE illusion) | **12.8x** (vector/list) | 14417x (allocation) | 551x (Ch. 49 index) |

### 10.1 Three Rules That Actually Cross Languages

Six languages were measured, and only three rules are genuinely **language-independent**:

1. **Profile before optimizing** (Amdahl's law holds everywhere);
2. **String accumulation in a loop is O(n²)** (Java measured 6x; C# measured 14417x in allocation);
3. **The wrong data structure costs far more than any micro-optimization** (Python measured 191x).

Everything else — warmup, escape analysis, inline caches, conditional moves — **is language-specific**.

### 10.2 "How to Measure" Is Itself Language-Specific

This is the chapter's most-overlooked lesson. The same microbenchmark methodology must:

- On **C++**, defend against the **compiler** optimizing it away (`volatile`, memory barriers, `DoNotOptimize`);
- On **JVM/CLR**, defend against the **JIT** (warmup, Blackhole, escape);
- On **Python**, defend against almost nothing (no JIT), but against **measurement overhead** drowning the subject;
- On **JS**, defend against **the harness itself** polluting inline caches;
- On **SQL**, defend against **caching** (the second run hits the buffer pool and is meaninglessly fast).

Transplanting one language's measurement habits to another is the most common of all the traps listed here.

---

## 11. Implementation Comparison

| Mechanism | JavaScript (V8) | Python (CPython) | Java (HotSpot) | C++ | C# (CoreCLR) |
|---|---|---|---|---|---|
| **Object layout** | Hidden class + property array | `dict` / `__slots__` | Header + fields | Layout follows declaration | Header + fields |
| **Field access** | Inline cache (3 tiers) | Hash lookup | Fixed offset | **Compile-time offset** | Fixed offset |
| **Dispatch** | Inline cache + inlining | Dict lookup | vtable + inlining | vtable / static / template | vtable + inlining |
| **Allocation** | Semi-space nursery | Refcount + arenas | TLAB pointer bump | `malloc` / stack / pools | Pointer bump |
| **Collection cost** | ∝ live set | Refcount immediate + cycle detector | ∝ live set | **No GC** | ∝ live set (measured 1.0/3.8/13.6 ms) |
| **Optimizing compiler** | TurboFan | none (3.11+ has a specializing interpreter) | C1 + C2 | GCC/Clang/MSVC | RyuJIT |
| **Deoptimization** | Yes | No | Yes | No | Yes (limited) |

### 11.1 Why C++ Needs No Warmup and Has No Deoptimization

C++ fixes everything at compile time: field offsets, function addresses (except virtuals), loop unrolling, vectorization. The price is that **no runtime information is available** — the compiler doesn't know which branch is actually taken more often, or that a virtual call has exactly one implementation in practice.

JIT languages invert this: **they use runtime information for more aggressive optimization** (e.g. inlining a virtual call with a single observed implementation), at the cost of building on **assumptions** that, when violated, require deoptimization.

That tradeoff directly determines the difference in measurement methodology. C++ numbers are right the first time; JIT-language numbers only mean something after **enough rounds**.

### 11.2 Profile-Guided Optimization: The Two Sides Converging

Modern C++ compilers support **PGO**: run an instrumented build against a real workload, collect branch frequencies, then recompile using that data. This is essentially **moving the JIT's runtime information into compile time**.

Conversely, the JVM's AOT (GraalVM Native Image) and .NET's ReadyToRun **move compilation ahead of runtime**.

The two sides are converging — but **the thing they can't converge away is measurement itself**.

---

## 12. Performance Analysis

This section collects every measurement in this chapter. All numbers were taken on Apple Silicon / macOS, with C++ at `-O2` and C# in Debug (the harness default). **Ratios matter more than absolute values.**

### 12.1 Cache and Memory (C++, `-O2`)

| Experiment | Faster | Slower | Ratio |
|---|---|---|---|
| vector vs list traversal, 4M | 0.3 ms | 3.6 ms | **12.8x** |
| sequential vs shuffled, same array | 0.6 ms | 4.0 ms | **7.0x** |
| row-major vs column-major, 2000×2000 | 0.3 ms | 1.1 ms | **4.1x** |
| SoA vs AoS (64-byte struct) | 0.5 ms | 1.1 ms | **2.2x** |
| branch prediction (compiler free) | 0.3 ms | 0.3 ms | **1.0x** ⚠️ |
| branch prediction (barrier forced) | 0.9 ms | 10.2 ms | **11.0x** |

### 12.2 JIT and Runtime (Java)

| Experiment | Value |
|---|---|
| JIT warmup (run 1 vs run 8) | 1.7 ms → 0.9 ms (**2.0x**) |
| Dead-code elimination (discard vs accumulate) | 0.0 ms vs 4.5 ms (**4547x** illusion) |
| Escape analysis (non-escaping vs escaping, per op) | 0.55 ns vs 6.42 ns (**11.6x**) |
| String `+=` vs StringBuilder (20000 iterations) | 14.7 ms vs 2.3 ms (**6x**) |

### 12.3 Dynamic Languages (JavaScript / Python)

| Experiment | Value |
|---|---|
| Inline cache: mono / poly(4) / mega(30) | 18.0 / 18.5 / 51.5 ms (**1.03x / 2.87x**) |
| Elements kind: SMI vs one string mixed in | 2.1 ms vs 3.7 ms (**1.7x**) |
| `delete` vs keep vs assign null | 5.9 / 10.7 / 7.7 ms (**1.8x / 1.3x**) |
| Python hotspot share (guessed vs measured) | 1% vs **99%** |
| Python list → set membership | 413.1 ms → 2.2 ms (**191x**) |
| Python measurement overhead | 0.028 μs/call |
| Python warmup effect | ±2% (**undetectable**) |
| Python run-to-run spread | 1.03x |

### 12.4 Allocation and GC (C#, Debug)

| Experiment | Time | Allocation |
|---|---|---|
| record class vs struct (5M) | 163.2 vs 77.7 ms (1.2–2.1x, varies) | 305 MB vs **0 MB** |
| Full Gen2 collection (live 0 / 1M / 4M) | **1.0 / 3.8 / 13.6 ms** | — |
| String `+=` vs StringBuilder (30000) | 76.3 vs 0.040 ms | 859.1 MB vs 61.0 KB (**14417x**) |
| Substring vs AsSpan (2M) | 25.7 vs 4.7 ms (**6x**) | 427 MB vs **0 KB** |
| Boxing vs generics (5M) | 18.9 vs 10.1 ms (**1.9x**) | 114 MB vs 8 KB |

**Note the last column**: the time column varies between runs; the allocation column is **identical every time**. That's the empirical basis for "look at allocation first."

### 12.5 Three Things These Numbers Say

**① The biggest gaps come from data structures, not micro-optimizations.** 191x (list→set) and 12.8x (vector/list) are both "pick a different data structure," while every micro-optimization here combined is under 3x.

**② The biggest numbers are usually fake.** 4547x is a dead-code-elimination illusion; 14417x is allocation volume, not time. **When you see a spectacular number, suspect the methodology first.**

**③ Some numbers deserve more trust than others.** Allocation bytes, cache-miss counts, SQL logical reads — these are **counts**, and they're deterministic. Elapsed time is a **measurement**, subject to every kind of interference. When a count can make the point, don't use time.

---

## 13. Engineering Practice

### 13.1 A Performance Budget: Write the Target Down

Without a target there is no "done." Performance work must start from a **falsifiable number**:

```
Homepage P95 < 200 ms
Search endpoint P99 < 500 ms
Build time < 90 s
App cold start < 1.5 s
```

Note **P95/P99, not the mean**: averages are diluted by fast requests, while users experience the slow ones. An endpoint averaging 50 ms with a P99 of 3 s means **one user in a hundred waited three seconds**.

### 13.2 The USE Method: Start at the System Level

Brendan Gregg's USE method gives a systematic checking order. For each resource (CPU, memory, disk, network), check three things:

- **U**tilization: what fraction of time is it busy?
- **S**aturation: how much work is queued?
- **E**rrors: any failures or retries?

Its value is that **it eliminates whole categories first**. If CPU utilization is 5%, every CPU-side micro-optimization is irrelevant — the bottleneck is I/O or lock contention.

### 13.3 Flame Graphs: Making Profiler Output Readable

A sampling profiler emits hundreds of thousands of stacks. A flame graph aggregates them into one picture:

- **The x-axis is sample share** (not a timeline) — wider means more CPU;
- **The y-axis is stack depth**;
- **Look for plateaus** — a wide, flat top is your hotspot.

Combined with `perf` / async-profiler / `--cpu-prof`, it's about the fastest way to locate CPU hotspots.

### 13.4 Put Performance Tests in CI

Chapter 52 established that **a fix without a regression test gets reverted**. The same is true of performance.

```yaml
# a workable minimal form
- run: ./bench.sh > current.json
- run: python compare.py baseline.json current.json --threshold 10%
```

Key design points:

- **Compare relative, not absolute values** — CI machine performance changes daily;
- **The threshold must exceed the noise** (Python here measured 1.03x spread, so at least 10%);
- **On failure, print a reproducible command**, not just "it got slower."

### 13.5 Continuous Profiling in Production

A profile taken on a dev machine can differ enormously from production (data volume, concurrency, cache hit rates). The modern approach is **low-overhead sampling running continuously in production**:

| Language | Tool |
|---|---|
| Java | async-profiler, JFR (< 2% overhead) |
| .NET | dotnet-trace, dotnet-counters |
| Go | built-in `net/http/pprof` |
| Node | `--cpu-prof`, Clinic.js |
| Python | py-spy (**no code changes; attaches to a running process**) |
| Databases | `pg_stat_statements`, `performance_schema` |

py-spy is worth calling out: it reads process memory from outside, requiring **no code modification and no restart** — which makes on-the-spot diagnosis of "production suddenly got slow" actually possible.

---

## 14. Best Practices

- **Set a target, measure the current state, and only then optimize.** Optimization without a target has no end.
- **Use a profiler to find hotspots; don't guess from source** (measured cost of guessing: optimizing 1% of runtime).
- **Compute the Amdahl ceiling before starting**: parts under ~5% usually aren't worth it.
- **Check data structures and algorithms first**: this chapter's two largest gaps (191x, 12.8x) both came from there.
- **Change one variable at a time**, or you won't know what worked and can't revert cleanly.
- **Report the min or median of several runs**, never a single casual run.
- **Confirm the gap exceeds the noise**: with a measured spread of 1.03x, a "5% improvement" means nothing.
- **In GC languages, look at allocation volume first**: it's deterministic; time isn't.
- **State the build configuration** (`-O2`/Release) — otherwise the number isn't reproducible and isn't evidence.
- **Use mature benchmark frameworks** (JMH, BenchmarkDotNet, Google Benchmark); they handle warmup and DCE for you.
- **Put benchmarks in CI** with a threshold above the noise.
- **Distrust spectacular numbers**: 4547x was an illusion; 14417x was allocation, not time.

---

## 15. Common Pitfalls

### Pitfall 1: Optimizing a Small Share

```python
# two days spent taking slow_looking from 5.0 ms to 0.5 ms
# total: 431 ms → 427 ms
```

**Why it's wrong**: Amdahl's ceiling is `1/(1-0.01) = 1.01x`.

**How to avoid it**: compute p first. Below 5%, don't touch it unless the fix is free.

### Pitfall 2: The Benchmark Gets Eliminated

```java
for (int i = 0; i < 20_000_000; i++) pure(i);   // result unused
// measured 0.0 ms — C2 deleted the whole loop
```

**Why it's wrong**: the compiler/JIT decides the result is unused and removes the code.

**How to avoid it**: accumulate into an escaping variable, or use JMH's `Blackhole` / BenchmarkDotNet's `Consumer` / Google Benchmark's `DoNotOptimize`.

### Pitfall 3: Timing a JIT Language Without Warmup

```java
long t0 = System.nanoTime();
compute(N);                    // first call: interpreted
```

**Why it's wrong**: measured 1.7 ms on run 1 vs 0.9 ms on run 8 (2.0x) — you measured the interpreter.

**How to avoid it**: run enough iterations for C2/TurboFan to take over before timing. But note **this rule does not apply to Python** (measured warmup effect: ±2%).

### Pitfall 4: The Harness Pollutes the System Under Test

```javascript
const sumX = (arr) => { let s = 0; for (const o of arr) s += o.x; return s; };
sumX(monoArray);      // this function's inline cache has now seen shape A
sumX(megaArray);      // ...and 30 more shapes → both measurements become megamorphic
```

**Why it's wrong**: JS inline caches attach to **code sites**, so a shared function shares its cache. Sections ① and ③ of this chapter's JS example **each fell into this trap**.

**How to avoid it**: give each subject a **fresh function** (`new Function(...)`).

### Pitfall 5: Concluding From a Single Run

```
one run: 100 ms before, 92 ms after → "8% faster!"
```

**Why it's wrong**: if measurement varies by ±10%, that 8% proves nothing.

**How to avoid it**: run 5–7 rounds, report min or median, and **report the spread**.

### Pitfall 6: Omitting the Build Configuration

**Why it's wrong**: the same C++ source measured 1.7x for vector vs list at `-O0` and 12.8x at `-O2`; the branch-prediction experiment reaches **opposite conclusions** at the two levels.

**How to avoid it**: always attach compile/run configuration to performance data. BenchmarkDotNet outright **refuses to run in Debug** for this reason.

### Pitfall 7: Trusting Expired Folklore

```
"Sorted arrays process faster"      → measured 1.0x (compiler used a conditional move)
"Keep property order consistent"    → 4 shapes cost only 3%
"Java object allocation is costly"  → non-escaping objects aren't allocated at all
"Heap allocation is expensive"      → 305 MB of garbage cost only 1.2x
```

**Why it's wrong**: each was true when stated, but compilers, JITs, and GCs kept improving.

**How to avoid it**: **measure on the spot**. Every "⚠️" in this chapter marks a claim that was once correct and no longer holds.

### Pitfall 8: Profiling Production Problems on a Dev Machine

**Why it's wrong**: data volume, concurrency, cache hit rates, and network latency all differ. A 100-row table locally is 200 million rows in production — the hotspot isn't in the same place.

**How to avoid it**: use continuous production profiling (py-spy, async-profiler, `pg_stat_statements`), or at minimum reproduce with production-scale data.

---

## 16. Interview Questions

**Q1: What is Amdahl's law and how does it guide everyday optimization?**

A: Max speedup = `1 / ((1-p) + p/s)`, where p is the optimized part's share of total time and s is its speedup. As s→∞ the ceiling is `1/(1-p)`. The guidance is: **compute p before you start.** This chapter measured that a part worth 1% yields only 1.01x even at 1000x speedup, while a part worth 99% yields 8.94x at just 10x. Picking the right place beats optimizing thoroughly by an order of magnitude.

**Q2: Why do vector and list traversal, both O(n), differ by more than 10x in practice?**

A: Big-O counts **operations**, not what each operation costs. Vector elements are contiguous — one 64-byte cache line holds 16 ints, and the hardware prefetcher predicts the next line. Every list node sits at a random heap address, so each step risks a cache miss (~100 ns, 100x an L1 hit). Measured here: 12.8x. A cleaner control — the same array with only the access order changed — measured 7.0x sequential vs shuffled.

**Q3: What are the three most common mistakes in JVM microbenchmarks?**

A: ① **No warmup** — you measure the interpreter (2.0x gap measured); ② **dead-code elimination** — with an unused result, C2 deletes the entire loop (a "4547x faster" illusion); ③ **objects that don't escape** — the JIT scalar-replaces them, so you never measure real allocation cost (11.6x per-op difference). A subtler fourth: a loop written directly in `main` only gets OSR compilation, which optimizes differently from standard C2 — **which method the benchmark lives in changes the result**. JMH avoids the first three.

**Q4: Does "processing a sorted array is faster than an unsorted one" still hold?**

A: **Not in that classic example.** Measured here: 1.0x, because the compiler turned the `if` into a conditional move (`csel`/`cmov`) — compute both paths, select by condition, no branch to predict. But branch prediction itself still matters: forcing a real branch back in with a memory barrier restores an 11.0x gap. The lesson: **performance folklore expires; verify by reading the assembly (`objdump | grep csel`) or comparing optimization levels.**

**Q5: Is "reduce memory allocation" accurate advice in a GC language?**

A: Not accurate enough. This chapter measured 305 MB of pure garbage and 38 Gen0 collections costing only ~1.2x. A generational GC walks only **live** objects — dead ones are never touched. What determines cost is the **live set**: a full Gen2 collection with 0 / 1M / 4M live objects measured 1.0 / 3.8 / 13.6 ms. So the accurate phrasing is "**don't let short-lived objects survive Gen0**," and what deserves suspicion is caches and static collections that pin objects in the old generation.

**Q6: Why is allocation volume a better metric than elapsed time?**

A: Because it's **deterministic**. While writing this chapter, repeated runs of identical code produced times spanning 1.2x–2.1x, while allocation figures were identical every time (305 MB / 859.1 MB / 427 MB / 114 MB). Time is affected by GC timing, CPU frequency, OS scheduling, and other processes; allocation depends only on the code. Other "count-type" metrics behave the same way: cache misses, branch mispredictions, SQL logical reads. **When a count can make the point, don't use time.**

**Q7: An endpoint got slow. What's your diagnostic order?**

A: ① Establish **target and current state** (what are P95/P99, what's the goal); ② use the **USE method** to eliminate categories (low CPU utilization → the bottleneck is I/O or locks); ③ on the database side, check **query count** first (N+1? measured 201 vs 1), then the **execution plan** (SCAN or SEARCH); ④ on the application side, use a profiler and look for plateaus in the **flame graph**; ⑤ compute the **Amdahl ceiling** to decide whether it's worth it; ⑥ change one variable and **measure again**; ⑦ add the benchmark to CI. Note the first four steps are all measurement.

**Q8: Is the JS advice "keep object property order consistent" still valid?**

A: **Largely obsolete.** Measured here: four distinct shapes cost 3% — V8's polymorphic inline caches handle a handful of shapes efficiently. The cliff is **megamorphism**: 30 shapes cost 2.87x, at which point V8 abandons the cache and falls back to hash lookup. So the accurate advice isn't "shapes must be identical" but "**don't let one access site see dozens of shapes**" — the classic offender being a generic function processing objects from a dozen different APIs.

---

## 17. Exercises

### Exercise 1: Verify Amdahl's Law (Basic)

Write a program with two functions: A takes ~10% of total time, B takes ~90%. Speed up each by 10x in turn, measure the change in total time, and compare against `1/((1-p)+p/s)`.

**Requirement**: error under 5%. If it isn't, find out why (hint: check whether your "10x speedup" really achieved 10x).

### Exercise 2: Reproduce Dead-Code Elimination (Intermediate)

In a JIT language you know, write a microbenchmark that produces a false "100x+ faster" result, then:

1. Prove the code was deleted, using a profiler or `-XX:+PrintCompilation` / `--trace-opt`;
2. Add the correct defense (Blackhole / Consumer) and give the real number;
3. Write down the ratio between the two — that's the size of the lie you nearly published.

### Exercise 3: Find the Real Hotspot in Your Project (Practical)

On a project you're actually working on:

1. **Write down your guess** for the top three hotspots (no looking at code);
2. Profile and list the real top three;
3. Compare: how many did you get right?
4. For the real #1, compute the Amdahl ceiling and decide whether it's worth optimizing.

**The value of this exercise isn't the optimization — it's the number from step 3.**

### Exercise 4: Re-verify a Classic Conclusion (Intermediate)

Pick a piece of performance folklore you've heard ("`i++` is slower than `++i`," "exceptions are slower than error codes," "virtual calls are expensive") and design an experiment to test whether it still holds **in your current language and compiler version**.

**Requirement**: if you can't measure a difference, you must find out why (compiler rewrite? inlined? insufficient timer resolution?) rather than simply declaring it false. This chapter's C++ branch-prediction experiment is a full worked example of that process.

### Exercise 5: Add a Performance Regression Test (Practical)

Pick a critical path, write a benchmark, and wire it into CI:

1. Run 7 rounds; record the median and the spread;
2. Set the threshold to twice the spread;
3. Deliberately commit a change that's 30% slower; confirm CI catches it;
4. Deliberately commit an unrelated change; confirm CI **does not** false-alarm.

Step 4 is the most-skipped and the most important — a performance test that cries wolf will be ignored by everyone within a week.

---

## 18. Chapter Summary

**Core conclusion**: performance optimization is a discipline of **measurement**, not of guessing. Six languages' experiments prove the same thing repeatedly: **intuition and folklore about performance both expire; measuring on the spot doesn't.**

**Key measurements**:

| Finding | Data |
|---|---|
| Guessing the hotspot fails | the guess was **1%**, the real one **99%** |
| The fix is usually small | `[]` → `set()`, **191x** |
| The weight of cache locality | same O(n), vector vs list **12.8x** |
| The clean cache experiment | same array, only access order **7.0x** |
| The dead-code-elimination illusion | discard vs accumulate **4547x** |
| JIT warmup | run 1 vs run 8 **2.0x** |
| Escape analysis | non-escaping objects aren't allocated; per-op cost **11.6x** |
| Inline caches | 4 shapes **1.03x**, 30 shapes **2.87x** |
| Allocation vs survival | 305 MB of garbage cost **1.2x**; GC pause tracks live set **1.0→13.6 ms** |
| The classic branch experiment | compiler free **1.0x**, barrier forced **11.0x** |

**Four pieces of expired folklore** (each refuted by measurement here):

1. ~~Sorted arrays process faster~~ → the compiler eliminated the branch with a conditional move (1.0x);
2. ~~Inconsistent property order is slow~~ → 4 shapes cost 3%; the problem is megamorphism (2.87x);
3. ~~Java object allocation is expensive~~ → non-escaping objects are scalar-replaced and never allocated;
4. ~~Heap allocation is expensive~~ → cost is determined by the live set, not the allocation volume.

**The six-step workflow**:

```
① Set a target    "Homepage P95 < 200 ms" — without one there is no "done"
② Measure         Profile to find hotspots; don't guess
③ Compute the cap Amdahl's law — decide whether it's worth it before starting
④ Change one thing One variable at a time
⑤ Measure again   Confirm it's really faster, and that the gap exceeds the noise
⑥ Lock it in      Add the benchmark to CI (Chapter 52's approach)
```

**Three of the six steps are measurement** — that's the chapter's only real discipline.

**Self-check**:

- [ ] I can compute an optimization's ceiling with Amdahl's law before starting.
- [ ] I profile to find hotspots instead of guessing from source.
- [ ] I know why two O(n) loops can differ by an order of magnitude.
- [ ] I can name the three microbenchmark traps and their defenses.
- [ ] I know that "warm up first" is language-dependent and can't be transplanted.
- [ ] In GC languages I look at allocation volume before time, and I know why.
- [ ] I state build configuration and methodology when reporting performance numbers.
- [ ] My first reaction to a spectacular ratio is to suspect the methodology.
- [ ] I know how to verify on the spot whether a piece of performance folklore has expired.

**Next chapter**: optimization makes programs faster, but **a fast wrong program just fails faster**. Chapter 58 covers **security** — we'll measure how SQL injection behaves before and after parameterized queries, reproduce why XSS's three contexts (HTML/attribute/JS) need three different escapes, quantify how password hashing is **deliberately made slow** (this chapter's "faster is better" inverts completely), and explain why the six languages' security defaults differ so much — and how those differences become real vulnerabilities.

---

## 19. Further Reading

- <a href="https://en.wikipedia.org/wiki/Amdahl%27s_law" target="_blank" rel="noopener">Wikipedia · Amdahl's Law</a> — full derivation and historical context for the formula in §2.3.
- <a href="https://www.brendangregg.com/usemethod.html" target="_blank" rel="noopener">Brendan Gregg · The USE Method</a> — the systematic diagnosis order behind §13.2.
- <a href="https://github.com/brendangregg/FlameGraph" target="_blank" rel="noopener">FlameGraph</a> — the original implementation and usage guide.
- <a href="https://www.akkadia.org/drepper/cpumemory.pdf" target="_blank" rel="noopener">Ulrich Drepper · What Every Programmer Should Know About Memory</a> — the authoritative treatment of cache hierarchies and locality; the theoretical basis for this chapter's C++ experiments.
- <a href="https://github.com/openjdk/jmh" target="_blank" rel="noopener">JMH · Java Microbenchmark Harness</a> — the de facto standard for JVM microbenchmarks; its samples directory is a textbook on how to get benchmarks wrong.
- <a href="https://github.com/dotnet/BenchmarkDotNet" target="_blank" rel="noopener">BenchmarkDotNet</a> — .NET's benchmark framework; reports allocation and GC counts by default and refuses to run in Debug.
- <a href="https://v8.dev/blog/fast-properties" target="_blank" rel="noopener">V8 · Fast Properties</a> — the official explanation of hidden classes, inline caches, and dictionary mode behind this chapter's JS experiments.
- <a href="https://en.algorithmica.org/hpc/pipelining/branchless/" target="_blank" rel="noopener">Algorithmica · Branchless Programming</a> — how compilers eliminate branches with `cmov`/`csel`, explaining why this chapter's C++ experiment couldn't reproduce the classic result.
- <a href="https://www.postgresql.org/docs/current/pgstatstatements.html" target="_blank" rel="noopener">PostgreSQL · pg_stat_statements</a> — sorting slow queries by total time; the tooling behind §9.5.
