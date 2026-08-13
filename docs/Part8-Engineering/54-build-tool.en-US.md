# Chapter 54 · Build Tools

[简体中文](./54-build-tool.md) ｜ **English**

---

> Chapter 53 got dependencies under control; how does code become something that runs? The answer sounds like one sentence — "compile it" — but a real build system must get three mutually opposed things right at once: **do less** (incremental), **do it simultaneously** (parallel), and **don't redo what's already done** (caching) — and each of the three has a trap that lets you **silently produce a wrong artifact**.
>
> The **key experiments** begin by hand-writing an incremental build system and measuring timestamp-based checking's two failure modes. **False positive**: `touch` a header whose content never changed, and 3 targets get rebuilt for nothing (`git checkout` manufactures thousands of these daily). **False negative is fatal**: change the header's content while its mtime is dialed back an hour (clock drift, unpacking an old archive, restoring a cache all do this) and the build reports "rebuilt 0" while **the artifact still contains the old version** — you think you fixed it; you didn't. Switch to Bazel-style **content hashing**, and all four scenarios come out right.
>
> The Java example turns that abstract trap into a runnable crash site: change `static final int MAX_RETRIES = 3` to 10 and, following "rebuild whichever file changed," recompile only `Config.java` — running `App.run()` still prints **3**. The constant was **inlined into App.class's bytecode** at compile time; App holds no runtime reference to Config at all. The second crash is blunter: change a method signature, rebuild only that class, and the downstream caller gets **`NoSuchMethodError`**. Both share one root cause: **incremental correctness depends on the dependency graph's completeness**, and file-level mtimes cannot see bytecode-level dependencies.
>
> The C++ example invokes a real `g++ -E` to price another bill: a two-line addition function, after `#include <iostream>`, comes out of the preprocessor at **66,293 lines — an 8,286× expansion**. That is the physical reason "changing one header rebuilds a swath," and C++ compilation's number-one cost.
>
> The JS example hand-writes a mini bundler: dependency graph from an entry → a runnable bundle → **tree-shaking**. The never-imported `matrixMultiply`/`sub`/`formatCsv` are removed wholesale, shrinking the bundle from 972 to 662 bytes (**32% smaller**) with byte-identical output. Its one prerequisite is that ESM's `import/export` is **statically analyzable** — CommonJS's `require(variable)` cannot be shaken.
>
> The C# example measured parallelism's ceiling: the same task graph takes 670 ms serially and 460 ms with 8 workers — and the **critical path** (`core → data → api → bundle`) is exactly 460 ms, so 999 workers still take 460 ms. **The first step in speeding up a build is not adding machines but shortening the critical path.** And caching's prerequisite — **deterministic builds** — proved trivially breakable: write a timestamp into the artifact and two builds of identical input hash differently, invalidating every cache.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- Name a build system's three foundations (**dependency graph + topological order + staleness check**) and hand-write an incremental builder;
- Reproduce timestamp checking's **false positives and false negatives** (measured) and explain why content hashing eliminates both;
- Recognize **incremental compilation's correctness traps** (constant inlining, stale class files — measured producing silently wrong programs);
- Explain why **tree-shaking requires static module structure** (measured 32% reduction) and why CommonJS can't be shaken;
- Use the **critical path** to judge whether parallelism can still help (measured: 999 workers can't beat it) and state why **deterministic builds** are caching's prerequisite.

---

## 2. Why This Concept Exists

### Isn't `make` enough?

```text
The naive build: one command compiles every source file
The problem is【scale】: 100k lines, 5000 files, twenty minutes per one-line change — unbearable
→ hence incremental: rebuild only what's affected
→ but those two words, "what's affected," are the source of this chapter's entire difficulty
```

### A build system's three foundations

```text
① the dependency graph (DAG): who depends on whom — the prerequisite for everything
② topological order:          build dependencies first, then targets
③ the staleness check:        which targets are【out of date】← the hardest of the three
```

### The one-sentence definition

```text
A build system = an execution engine that models "source → artifact" as a DAG
                 and does as little as possible, in parallel, reusing what others already built
```

> **In one sentence**: a build tool's difficulty lies not in "how to compile" but in **"what can be skipped"** — and when that judgment is wrong, the cost is not a failed build (that would be lucky) but a **silently produced artifact that doesn't match the source** (measured once in each of three languages here).

---

## 3. How It Works

### Key experiment one: a hand-written incremental builder and timestamp's two failures

**Graph and topological order** (Python measured):

```text
rules: a.obj ← [a.src, common.hdr]   b.obj ← [b.src, common.hdr]
       c.obj ← [c.src]               app.bin ← [a.obj, b.obj, c.obj]
topological order: ['a.obj', 'b.obj', 'c.obj', 'app.bin']
first build (full): 4 rebuilt
```

**Incremental working as intended**:

```text
nothing changed, rebuild:  0 rebuilt ✓
change c.src:              2 rebuilt → ['c.obj', 'app.bin'] (a.obj/b.obj untouched) ✓
change common.hdr:         3 rebuilt → ['a.obj', 'b.obj', 'app.bin']
→ one header pulls two objects —【the graph's fan-out determines a change's blast radius】
```

**Failure one: the false positive** (content unchanged, rebuilt anyway):

```text
touch common.hdr (not one character changed): 3 rebuilt
→ wasted work — git checkout and branch switches manufacture these constantly
```

**Failure two: the false negative** (content changed, not rebuilt — the dangerous one):

```text
changed common.hdr but dialed its mtime back an hour: 0 rebuilt
the header version inside the artifact:【still the old v2】✗
→ the change【never reached the artifact】— clock drift, unpacked archives,
  and restored build caches all create this
→ a false positive wastes time; a false negative【silently ships a stale artifact】
```

**Bazel's answer: content hashing** (all four scenarios re-run, Python measured):

```text
no change:                    0 rebuilt ✓
touch (false-positive case):  0 rebuilt ✓ unchanged content, no rebuild
changed content + old mtime:  3 rebuilt ✓ the hash changed, so rebuild
→ content hashing eliminates both failures — at the cost of reading and hashing files
```

### Key experiment two: incremental compilation's correctness traps (Java measured)

**Trap one: constant inlining** — Java incremental compilation's most famous pitfall:

```java
public class Config { public static final int MAX_RETRIES = 3; }
public class App { public static String run() { return "retry limit = " + Config.MAX_RETRIES; } }
```

```text
change MAX_RETRIES to 10 and, per the incremental strategy,【recompile only Config.java】:
running App.run() → retry limit = 3   ← still 3!
→ why: static final constants are【inlined into App.class's bytecode】at compile time
  App.class holds the literal 3 and has no runtime reference to Config at all
→ "rebuild whichever file changed" here【silently produces a wrong program】— worse than a failure
after recompiling App.java → retry limit = 10 ✓
```

**Trap two: the stale class file**:

```text
Util.greet gains a parameter; recompile only Util.java:
running Caller.run() → ✗ NoSuchMethodError: 'String Util.greet(String)'
→ Chapter 53's runtime bomb, this time caused by a【stale Caller.class】
```

**Both crashes share a root cause**:

```text
Incremental correctness depends on【the dependency graph's completeness】
and file-level mtimes cannot see "the constant was inlined" or
"the signature is referenced" — these are【bytecode-level】dependencies
→ the golden rule: an incremental result must be【byte-identical to a full rebuild】
→ the first move when chasing weird behavior is always a clean build —
  if it disappears, your incremental system is missing a dependency edge
```

### Key experiment three: header expansion's real cost (C++ invoking g++ -E)

```text
a one-line addition function, after preprocessing:
  no headers at all:                8 lines
  #include <iostream>:          66293 lines (8286× expansion)
  #include <vector/string/map>: 63839 lines (7979× expansion)
→ your two lines of code force the compiler through【tens of thousands】— per .cpp, every time
→ C++ compilation's number-one cost, and the physical reason
  "changing one header rebuilds a swath"
```

**Real incremental measurements** (same experiment, invoking g++):

```text
full build of 3 .cpp files:               77 ms
change c.cpp (no shared header) → 1 file: 28 ms (64% saved)
change common.hpp → 2 files:              50 ms (35% saved)
→ where a change lands in the graph determines how much incremental saves —
  headers are the most expensive place to change
```

### Key experiment four: bundling and tree-shaking (JS hand-written bundler)

**Three steps**:

```text
① parse imports from the entry, recursing into a graph:
   main.js → ./format.js{formatSum}
   format.js → ./math.js{add}
   math.js → (no dependencies)
② rewrite the module graph into a registry + __require runtime, emit one file
   → really executed, printing "结果: 42"
③ tree-shaking: mark what's reachable from the entry, delete the rest wholesale
```

```text
format.js's used exports: {formatSum}   (formatCsv unused)
math.js's used exports:   {add}         (sub, matrixMultiply unused)
size: 972 → 662 bytes (32% smaller), output byte-identical ✓
```

**Why CommonJS can't be shaken and ESM can**:

```text
CommonJS: require(variable), module.exports[key] = ... —
          what's imported/exported is only known【at runtime】
ESM:      import { add } from './math.js' — fixed【at the syntax level】,
          analyzable without executing
→ this is why ESM insists on static structure: not aesthetics, but to enable tree-shaking
→ Chapter 47 déjà vu: declarative (statically analyzable) gives the optimizer something to work with —
  SQL and ESM, the same principle
```

### Key experiment five: parallelism's ceiling is the critical path (C# measured)

```text
graph: core(200) → {utils(80), data(120)} → {api(90), ui(70)} → bundle(50); docs(60) independent

serial execution:       670 ms (the sum of all task durations)
parallel (8 workers):   460 ms (1.46× speedup)
  wave 1: core, docs      wave 2: utils, data
  wave 3: api, ui         wave 4: bundle
critical path: core → data → api → bundle = 460 ms
with 999 workers:       460 ms   ← cannot catch up at all
```

```text
→ with infinite CPUs, a build still cannot beat its critical path
→ the first step in speeding up a build is not adding machines but【shortening the critical path】:
  splitting a "everyone depends on it" node like core beats buying CPUs
```

### Caching's prerequisite: deterministic builds (C# measured)

```text
build with a timestamp, same input twice: 492994B701CC vs 7407931BE6ED → identical: False
with the timestamp removed:               E0C277E9CB07 vs E0C277E9CB07 → identical: True

the three usual determinism-breakers:
  ① timestamps (build time written into the artifact)
  ② absolute paths (/Users/your-name/...)
  ③ parallel/hash iteration order (same input, different symbol order in the output)
```

```text
→ without determinism, cache hit rates collapse (the action cache's key differs every time)
→ and you cannot verify "was this binary really built from this source" —
  Chapter 53's supply-chain endgame
→ Chapter 53's lockfile guarantees "install the same dependencies";
  deterministic builds guarantee "produce the same artifact" —
  the two cornerstones of a verifiable supply chain
```

### How the three relate

```text
incremental: do less        — correctness is hardest (two measured crashes)
parallel:    do at once     — capped by the critical path (experiment five)
caching:     someone did it — requires determinism + content hashing
→ modern build experience needs all three; missing any one hits a wall at some scale
```

---

## 4. JavaScript

The JS example hand-writes the bundler (data in key experiment four); here is the mechanism and ecosystem.

### The bundler's runtime is a userland replica of the module system

```javascript
const __modules = {}, __cache = {};
function __require(id) {
  if (__cache[id]) return __cache[id];
  const exports = {}; __cache[id] = exports;
  __modules[id](exports, __require);
  return exports;
}
```

**This is Chapter 14's module system, hand-implemented for the browser** — browsers had no modules back then, so bundlers built one.

### What modern bundlers compete on

```text
webpack : written in JS, richest plugin ecosystem — minute-scale cold starts on big projects
esbuild : written in Go, parallel with fewer passes — 10–100× faster (this chapter's algorithm, industrialized)
vite    : in development, simply【doesn't bundle】— native browser ESM on demand, second-scale startup
          production still bundles with Rollup (the request-count/compression trade-off returns)
→ "bundling" is an HTTP/1.1-era artifact; tools evolve between【faster】and【not at all】
```

> **Note**: `sideEffects: false` (package.json) tells bundlers "this package has no import-time side effects, shake freely" — get it wrong and genuinely needed initialization disappears; code splitting is tree-shaking's other face (not deleting, but splitting into on-demand chunks); source maps let minified artifacts map back to source — the debugging infrastructure of build outputs.

---

## 5. Python

The Python example hand-writes the incremental builder (data in key experiment one); here is its engineering meaning.

### Three generations of staleness checking

```text
gen 1 (Make):        mtime comparison — fast, with the two measured failures
gen 2 (content hash): sha256(inputs) — correct, at the cost of reading files
gen 3 (remote cache): key = hash(all inputs + command line) → look it up
  → identical inputs compute the same key on【any machine】
  → whatever a colleague or CI already built downloads directly —
    "building" degenerates into "a table lookup"
```

### Why hashing's cost is acceptable

```text
reading and hashing vs recompiling: milliseconds of I/O against seconds of CPU
→ and hashes can be cached by (path, mtime, size) — unchanged mtime means no rehash
→ so it becomes: mtime as a【fast filter】, hashes as the【final verdict】—
  the two combined, not one or the other
```

> **Note**: Make's implicit rules and `.PHONY` are decades of accumulated convention; `ninja` is the minimal "execute, don't configure" builder (CMake/Meson generate its input); Python's own build (PEP 517/518) made the "build backend" a swappable component — setuptools, hatchling, and maturin each trade differently.

---

## 6. Java

The Java example reproduces both incremental traps (data in key experiment two); here is how tools respond.

### How real tools solve it

```text
javac itself:        no incrementality — it recompiles every file you hand it
                     (delegating the hard problem upward)
Gradle incremental:  analyzes【class-level and constant dependencies】—
                     a changed constant recompiles every referencing class
Bazel:               content hashing + sandboxing + header jars (signatures only)
```

**The header-jar idea deserves its own note**:

```text
downstream depends on your【signature】, not your implementation
→ changed implementation needn't rebuild downstream; a changed signature must
→ blast-radius refinement: from "this file changed" to
  "did this file's public interface change?"
```

### The sandbox: turning missing dependencies into build failures

```text
Bazel runs each action in a sandbox where only【declared inputs】are visible
→ omit a header or dependency → it isn't there → the build fails immediately
→ this promotes "quietly used something undeclared" from a runtime error to a build error
→ the same idea as Chapter 53's pnpm killing phantom dependencies:
 【make undeclared things physically unreachable】
```

> **Note**: `mvn clean install` became muscle memory precisely because Maven's incrementality isn't trustworthy enough; Gradle's build cache (`--build-cache`) and configuration cache are different things (task outputs vs build-script evaluation); `--scan` produces a build performance report — the first tool for finding the critical path.

---

## 7. C++

The C++ example invokes real g++ (data in key experiment three); here is the full pipeline and its weapons.

### Compiling vs linking: two very different phases

```text
compiling: each .cpp → .o (independent, parallelizable, cacheable — ccache caches this step)
linking:   all .o + libraries → executable (resolving cross-file symbols, Ch. 53's symbol world)
→ linking【cannot be incremental】: change one .o and the whole executable relinks —
  often the bottleneck on large projects
→ hence parallel linkers like lld and mold
```

### Four weapons against recompilation

```text
① forward declarations: don't #include in headers when you can avoid it
   — one fewer edge is one less swath rebuilt (Pimpl is its extreme; Ch. 53's ABI relies on it too)
② precompiled headers (PCH): preprocess stable giants like <vector> once and reuse (treats the expansion)
③ ccache: cache compilation results keyed by【the hash of preprocessed content】
④ C++20 modules: abolish textual expansion at the root — import parses once into a binary interface
```

**Item ④ is the real cure**: C++ finally gets the "modules" JS and Java have had for ages (Chapters 14/53) — and modularity's direct dividend is **precise dependencies with no repeated expansion**.

> **Note**: `-MMD -MP` makes gcc emit dependency files (Make's automatic dependency discovery); `include-what-you-use` finds headers included-but-unused and used-but-unincluded; distributed compilation (distcc/icecc) pushes parallelism across machines — still bounded by the critical path (C# measured).

---

## 8. C#

The C# example measured parallelism's ceiling and determinism (data in key experiment five).

### The scheduler's core loop

```csharp
while (done.Count < Graph.Length) {
    // find every task whose dependencies are complete
    var ready = Graph.Where(t => !done.Contains(t.Name) && t.Deps.All(done.Contains))
                     .Take(workers).ToList();
    elapsed += ready.Max(t => t.Ms);          // a wave costs as much as its slowest task
    foreach (var t in ready) done.Add(t.Name);
}
```

**This is every build tool's core loop**: repeatedly compute the ready set and feed it to a worker pool (Chapter 45's thread pool, cashing in).

### What the critical path means in practice

```text
measured: 8 workers 460 ms, 999 workers 460 ms, critical path 460 ms — all identical
→ diagnostic order: measure the critical path first, then decide machines vs structure
→ shortening it: split giant nodes, remove unnecessary edges, start slow tasks earlier
→ the answer to "builds are slow" is often not in the build tool but in【module decomposition】
```

### Tooling for determinism

```text
.NET:    <Deterministic>true</Deterministic> (on by default) + <PathMap> to erase absolute paths
gcc:     -ffile-prefix-map, -frandom-seed
general: the SOURCE_DATE_EPOCH convention (tools use a fixed timestamp instead of now)
→ the Reproducible Builds project pushes the whole open-source ecosystem toward this
```

> **Note**: MSBuild's `Inputs`/`Outputs` is mtime-based (with both measured failures); `dotnet build`'s incrementality across project references relies on `.csproj` timestamps, so editing Directory.Build.props often needs `--no-incremental`; deterministic NuGet packages additionally require `ContinuousIntegrationBuild=true`.

---

## 9. SQL

Databases have exactly the same incremental-build problem: **refreshing materialized views**.

### View vs materialized view = full build vs build artifact

```text
plain view:        recomputed on every query — like running make clean every time
materialized view: the result【stored as a table】— it is the build artifact,
                   at the cost of【going stale】
```

### Incremental refresh (measured)

```text
2 new orders arrive (both on 2026-08-01):
  full refresh:        recompute all 28 days
  incremental refresh: recompute only【the affected day】
after incremental refresh, verified against live computation: ✓ consistent
→ "rebuild only what's affected" — the same algorithm as the Python incremental builder
```

### The incremental crash is identical too (measured)

```text
update id=1, an【old】order (belonging to 2026-08-02), while incremental refresh
only looks at "newly inserted rows":
  materialized total for 2026-08-02 = 70855
  live computation                  = 80853
→ the artifact【silently went stale】— isomorphic to Java's constant inlining
  and Python's mtime false negative
→ three examples, one lesson: incremental correctness = dependency-tracking completeness
```

### Triggers as automatic dependency discovery (measured fix)

```sql
CREATE TRIGGER orders_au AFTER UPDATE OF amount ON orders BEGIN
  UPDATE daily_totals SET total = total - OLD.amount + NEW.amount WHERE day = NEW.day;
END;
```

```text
after adding INSERT/UPDATE triggers:
  2026-08-02 materialized=78631 live=78631 ✓
  2026-08-03 materialized=71681 live=71681 ✓
→ triggers = a build system's【automatic dependency discovery】: not relying on humans
  to declare, but on a mechanism that captures every change
→ the cost is the same too: every write gets slower (Ch. 49's measured index write amplification)
```

### The isomorphism table

```text
source files         ↔ base tables (orders)
build artifacts      ↔ materialized views (daily_totals)
dependency graph     ↔ table references in the view definition
incremental build    ↔ incremental refresh (recompute affected partitions only)
automatic discovery  ↔ triggers / CDC change capture
clean build          ↔ full refresh (always correct, always slow)
→ dbt moved this wholesale into data engineering: models as targets, DAG scheduling,
  incremental materialization strategies
```

> **Note**: PostgreSQL's `MATERIALIZED VIEW` supports only full `REFRESH` (concurrent refresh needs a unique index); incremental requires extensions like pg_ivm or hand-written triggers; sqlite has no materialized views, so this example builds one from a table plus triggers; warehouse "incremental models" (dbt's `incremental`) require you to supply the "which rows are new" predicate explicitly.

---

## 10. Cross-Language Comparison

### ① Build tools and mechanisms

| Dimension | JavaScript | Python | Java | C++ | C# |
|-----------|-----------|--------|------|-----|-----|
| Mainstream tools | webpack/vite/esbuild | setuptools/hatch + tox | Maven/Gradle | Make/CMake/Ninja/Bazel | MSBuild |
| Compilation unit | module (file) | module (no compile) | class (.java→.class) | **translation unit (.cpp)** | project (assembly) |
| Incremental granularity | module | — | class (+ constant deps) | file (headers fan out) | **project** (coarse) |
| Signature trap | CommonJS can't shake | — | **constant inlining** (measured) | **header expansion 8286×** (measured) | cross-project timestamps |
| Dead-code elimination | **tree-shaking** (measured 32%) | — | ProGuard/R8 | linker --gc-sections | IL Linker/trimming |
| Caching | per-tool | — | Gradle build cache | ccache/sccache | MSBuild incrementality |

### ② Key experiment data summary

```text
incremental working: change c.src → 2 rebuilt; change common.hdr → 3 rebuilt (fan-out = blast radius)
mtime false positive: touch an unchanged file → 3 rebuilt for nothing
mtime false negative: change content + rewind mtime → 0 rebuilt, artifact【still old】
content hashing:      all four scenarios judged correctly
constant inlining:    recompile only Config → App still prints 3 (should be 10) — silent error
stale class file:     recompile only Util → Caller throws NoSuchMethodError
header expansion:     8 lines → 66293 lines (8286×)
C++ incremental:      full 77 ms; change c.cpp 28 ms (64% saved); change header 50 ms (35% saved)
tree-shaking:         972 → 662 bytes (32% smaller), identical output
parallel ceiling:     serial 670 ms, 8 workers 460 ms, 999 workers 460 ms = critical path 460 ms
determinism:          with a timestamp two builds hash differently; without, identical
SQL incremental crash: materialized 70855 vs live 80853; identical after adding triggers
```

### ③ Common ground and root causes

**Common ground**: every language's build system is "DAG + topological order + staleness check"; every incremental scheme's correctness rests on graph completeness (three languages each measured one missing edge's cost); every caching scheme demands determinism.

**Root causes**:

- **Incrementality's difficulty is implicit dependencies**: constant inlining (Java), header expansion (C++), updated old rows (SQL) — none visible at the filesystem level; the tool must understand them at the **language** level;
- **C++ compiles slowest by design**: no modules → textual header expansion → every translation unit re-parses tens of thousands of lines (measured 8,286×);
- **Tree-shaking works only in static module systems**: the same principle as Chapter 47's "declarative gives the optimizer something to work with";
- **Parallelism's ceiling is the graph's shape**: the critical path is set by **module decomposition**, so "slow builds" often live in architecture, not tooling;
- **Determinism is the shared foundation of caching and supply chains**: it turns "a build" from one computation into a **verifiable mapping**.

---

## 11. Implementation Comparison

| Tool | Staleness check | Parallel/caching | Distinctive trait |
|------|----------------|------------------|-------------------|
| **Make** | mtime | `-j` parallelism | minimal viable; both failures present (measured) |
| **Ninja** | mtime + command-line hash | highly parallel | executes without configuring; a changed command rebuilds too |
| **Gradle** | input/output **content hashes** | local/remote build cache | incremental tasks + incremental compilation (with constant analysis) |
| **Bazel** | content hashing + **sandboxing** | remote cache + remote execution | declared dependencies; omissions fail the build |
| **esbuild** | mostly full builds (fast enough) | Go parallelism | outruns incremental complexity with raw speed |
| **Vite (dev)** | no bundling | native browser ESM | defers "building" to on-demand browser requests |

**Bazel's sandbox deserves its own note**:

```text
each action runs in a sandbox containing only【declared inputs】
→ omit a dependency → it isn't found → the build fails immediately
  (instead of quietly using whatever happens to sit on the host)
→ this is "hermetic build": results don't depend on what your machine happens to have
→ isomorphic to Chapter 53's pnpm: make undeclared things physically unreachable
```

---

## 12. Performance Analysis

### This chapter's numbers at a glance

```text
incremental gain: 64% saved when changing a file with no shared header;
                  35% when changing the header (real C++ compilation)
header cost:      8 → 66293 lines (8286× expansion)
tree-shaking:     32% smaller (at this scale; 30–70% is common on real projects)
parallelism:      serial 670 → parallel 460 ms (1.46×); critical path 460 ms is the hard floor
determinism:      break it once → the cache key changes every time → hit rate zero
```

### The optimization order for build speed

```text
① measure the critical path — decides whether more machines help at all
   (C# measured 999 workers failing to beat it)
② shorten it — split giant nodes, cut unnecessary edges (an architecture problem)
③ ensure determinism — otherwise caching is void
④ enable caching — local, then remote (team-wide reuse)
⑤ only then swap in a faster tool (esbuild over webpack)
→ doing it backwards (tool first) usually disappoints: the graph's shape,
  and therefore the ceiling, hasn't moved
```

> ⚠️ **Incremental correctness outranks speed.** All three measured crashes (constant inlining, mtime false negative, missed materialized-view update) produced artifacts that **looked fine but were wrong** — diagnosing those costs far more than two extra minutes of clean build. Any incremental optimization must answer: **is it byte-identical to a full rebuild?**

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Staleness checking | content hashes (or mtime filter + hash verdict) | mtime alone | measured two failures; false negatives ship silently |
| Java constants | expose mutable config via methods or non-final fields | `public static final` across modules | measured: inlining makes changes ineffective |
| C++ headers | forward declarations + Pimpl + PCH | casual `#include` in headers | measured 8286× expansion; fan-out sets rebuild scope |
| Frontend artifacts | ESM with correct `sideEffects` | mixing CommonJS | measured: only static structure can be shaken |
| Slow builds | measure the critical path first | throwing on workers/machines | measured: 999 workers can't beat it |
| Caching | achieve determinism before enabling | enabling it blindly | non-deterministic → new key every time → 0% hits |
| Metadata in artifacts | fixed values via `SOURCE_DATE_EPOCH` | writing build time/hostname | measured: destroys determinism |
| Dependency declaration | sandboxed builds (Bazel) or explicit declaration | relying on "the machine happens to have it" | implicit deps explode on a new machine |
| Chasing weird behavior | clean build first | debugging incremental output repeatedly | if it vanishes, an edge is missing |
| Data materialization | triggers/CDC to capture changes | hand-rolled "new rows only" | measured: missed UPDATEs staled the artifact |

### The rule of thumb

```text
When builds are slow, ask three things:
  ① how long is the critical path? → is there room for parallelism (if not, change module decomposition)
  ② is the incremental correct?    → does a clean build agree with it
  ③ can the cache hit?             → is the artifact deterministic (timestamps/paths/ordering)
```

---

## 14. Best Practices

- **Use content hashes for staleness**: measured, mtime yields both false positives (waste) and false negatives (silent errors) — the latter is the real enemy.
- **Watch for language-level implicit dependencies**: Java constant inlining, C++ header expansion, SQL updated rows — three languages, three measured crashes, one root cause.
- **The acceptance criterion for incrementality is "byte-identical to a full build"**: any optimization must answer it; clean build is always the first diagnostic move.
- **Measure the critical path before parallelizing**: measured, 999 workers cannot beat a 460 ms critical path — "slow builds" often live in module decomposition.
- **Achieve determinism before caching**: measured, one timestamp changes every hash and voids the cache; determinism is also supply-chain verifiability's foundation (Chapter 53).
- **Stay on ESM and label `sideEffects` correctly on the frontend**: measured, only static module structure can be tree-shaken (32%).
- **Make "fewer dependency edges" a daily discipline in C++**: forward declarations, Pimpl, IWYU — measured, headers are the most expensive place to change.
- **Make undeclared dependencies fail the build rather than pass silently**: sandboxed builds (Bazel) and pnpm's phantom elimination are the same idea.

---

## 15. Common Pitfalls

**Pitfall 1 · Relying on mtime for incrementality**

```text
⚠️ measured: touch causes wasted rebuilds; changed content + rewound clock skips the rebuild entirely
✅ content hashing, or mtime as a fast filter with hashes as the verdict
```

**Pitfall 2 · `public static final` constants across modules (Java)**

```java
public static final int MAX_RETRIES = 3;   // ⚠️ measured: change the value, un-recompiled callers keep the old one
// ✅ expose via a method: public static int maxRetries() { return 3; }
```

**Pitfall 3 · Casual includes in headers (C++)**

```cpp
#include <iostream>   // ⚠️ measured: one line preprocesses to 66293; also widens the rebuild fan-out
// ✅ forward declare (class Foo;) and #include only in the implementation file
```

**Pitfall 4 · Publishing a library as CommonJS**

```javascript
module.exports = { add, sub, matrixMultiply };   // ⚠️ unshakeable; everything lands in the bundle
// ✅ ship an ESM build plus package.json's exports/sideEffects fields
```

**Pitfall 5 · Throwing machines at slow builds**

```text
⚠️ measured: with a 460 ms critical path, 999 workers are no faster than 8
✅ measure the critical path first; shortening it means splitting giant modules
```

**Pitfall 6 · Writing build time/hostname into artifacts**

```text
⚠️ measured: two builds of identical input hash differently → the cache never hits
✅ SOURCE_DATE_EPOCH / <Deterministic> / -ffile-prefix-map
```

**Pitfall 7 · Incremental materialization that only handles new rows**

```sql
-- ⚠️ measured: an UPDATE to an old row went uncaptured → materialized 70855 vs live 80853
-- ✅ triggers/CDC capturing INSERT, UPDATE, and DELETE
```

---

## 16. Interview Questions

**Basic**

1. What are a build system's three foundations? Why is the staleness check the hardest?
2. What is incremental correctness's acceptance criterion?
3. What is tree-shaking? Why does it need ESM?

**Intermediate**

4. **What are timestamp checking's two failure modes? Give a real scenario for each and say which is more dangerous.**
5. Why does Java's constant inlining break incremental compilation? How do tools solve it?
6. **Why does changing one C++ header rebuild a swath? How do C++20 modules cure it at the root?**

**Advanced**

7. **What is the critical path? What does it imply about whether more machines can speed up a build?**
8. What is a deterministic build? Why is it simultaneously the prerequisite for caching and supply-chain verification?
9. What problem does Bazel's sandbox solve? How is it similar to pnpm eliminating phantom dependencies?

---

## 17. Exercises

**Basic**

1. `touch` an unmodified file and watch whether your project rebuilds a swath for nothing.
2. Measure your project's full build and a typical incremental build; compute the saving.
3. Use `g++ -E | wc -l` to measure your project's largest header after expansion.

**Intermediate**

4. **Reproduce key experiment one**: hand-write an mtime-based incremental builder and construct both failure scenarios.
5. Reproduce the constant-inlining trap: change a `static final` constant, recompile only it, and check the caller's behavior.
6. Convert a CommonJS library to ESM and compare bundle sizes.

**Challenge**

7. **Add content hashing plus an action cache to your builder** and verify identical inputs hit the cache directly.
8. Draw your project's build task graph, measure the critical path, and identify a giant node worth splitting.
9. Make your build deterministic (two builds diff-empty) and list the sources of nondeterminism you eliminated.

---

## 18. Chapter Summary

**One sentence**: build tools model "source → artifact" as a DAG and optimize along three axes at once — **do less (incremental), do it simultaneously (parallel), don't redo what's done (caching)** — and this chapter measured a silent-wrong-artifact trap on each: timestamp-based staleness produces both **false positives** (`touch` an unchanged file → 3 needless rebuilds) and the far deadlier **false negatives** (changed content with a rewound mtime → 0 rebuilds while the artifact keeps the old version), which **content hashing** eliminates together; language-level **implicit dependencies** make the crashes stealthier still — Java's `static final` constant is inlined into the caller's bytecode, so recompiling only the definition leaves the program **still printing the old 3** (worse than a compile error), C++'s headers are **textual expansion** (measured: 8 lines with `#include <iostream>` become 66,293, an 8,286× blow-up — both compilation's number-one cost and the physical reason one header rebuilds a swath), and SQL's incremental materialization missing an `UPDATE` to an old row stales the artifact identically (70,855 vs 80,853) — three languages, one lesson: **incremental correctness equals dependency-tracking completeness**; on parallelism, a hand-written scheduler measured 670 ms serial and 460 ms with 8 workers while the **critical path is exactly 460 ms**, so 999 workers gain nothing — **the first step in speeding up a build is not adding machines but shortening the critical path**; on caching, its prerequisite **determinism** proved trivially breakable (one timestamp makes two builds of identical input hash differently), and it doubles as Chapter 53's other cornerstone of supply-chain verifiability; finally the JS hand-written bundler carried the full chain from dependency graph to **tree-shaking** (32% smaller, identical output), revealing its one prerequisite to be ESM's **static analyzability** — the same principle as Chapter 47's "declarative gives the optimizer something to work with."

**Key takeaways**

- **Three foundations**: dependency graph + topological order + staleness check; all the difficulty is in the third.
- **mtime's two failures** (measured): false positives waste time; **false negatives silently ship stale artifacts**.
- **Content hashing** (measured): eliminates both, and lets "building" degenerate into "a lookup" (remote caching).
- **Three implicit-dependency crashes** (measured): Java constant inlining, C++ header expansion 8286×, SQL missed UPDATE.
- **The golden rule**: incremental results must be byte-identical to a full rebuild; clean build is the first diagnostic.
- **The critical path is parallelism's hard floor** (measured 999 workers = 8 workers = 460 ms).
- **Determinism is caching's prerequisite** (measured timestamps breaking hashes) and supply-chain verification's basis.
- **Tree-shaking requires static module structure** (measured 32%) — exactly why ESM insists on static syntax.

**Checklist**

- [ ] I can hand-write an incremental builder and name mtime's two failure modes.
- [ ] I know how Java constant inlining and C++ header expansion each break incrementality.
- [ ] I use the critical path to judge whether more machines will help.
- [ ] I know why deterministic builds underpin both caching and supply chains.
- [ ] I can explain why tree-shaking needs ESM.

**Next chapter**: Chapter 52 kept repeating that "hard to test is almost always a design smell," and its C# example made `OrderService` depend on interfaces precisely so test doubles could be swapped in. Chapter 55 covers **dependency injection**: why hand your dependencies over; the trade-offs among constructor, property, and method injection; what a DI container actually does (hand-write one and you'll find it's Chapter 30's reflection plus Chapter 51's object-graph construction); and why Spring's `@Autowired` fails on self-invocation, and why the "service locator" is considered an anti-pattern — it differs from DI by exactly one direction.

---

## 19. Further Reading

- <a href="https://gittup.org/tup/build_system_rules_and_algorithms.pdf" target="_blank" rel="noopener">Build System Rules and Algorithms</a> — the classic paper on build algorithms (tup's author); this chapter's theoretical source.
- <a href="https://bazel.build/basics/hermeticity" target="_blank" rel="noopener">Bazel · Hermeticity</a> — hermetic builds and sandboxing, officially.
- <a href="https://ninja-build.org/manual.html" target="_blank" rel="noopener">The Ninja manual</a> — the minimal builder's design trade-offs (execute, don't configure).
- <a href="https://reproducible-builds.org/docs/" target="_blank" rel="noopener">Reproducible Builds · documentation</a> — the complete determinism checklist (including SOURCE_DATE_EPOCH).
- <a href="https://webpack.js.org/guides/tree-shaking/" target="_blank" rel="noopener">webpack · Tree Shaking</a> — the official guide to `sideEffects` and shaking.
- <a href="https://esbuild.github.io/faq/#why-is-esbuild-fast" target="_blank" rel="noopener">esbuild · why it's fast</a> — parallelism, fewer passes, and avoiding intermediate representations.
- <a href="https://docs.gradle.org/current/userguide/incremental_build.html" target="_blank" rel="noopener">Gradle · incremental builds</a> — input/output hashing and UP-TO-DATE checks.
- <a href="https://en.cppreference.com/w/cpp/language/modules" target="_blank" rel="noopener">cppreference · C++20 Modules</a> — the root cure for header expansion.
- <a href="https://docs.getdbt.com/docs/build/incremental-models" target="_blank" rel="noopener">dbt · incremental models</a> — incremental building in data engineering (the industrial form of the SQL experiment).
