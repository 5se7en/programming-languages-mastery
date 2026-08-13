# Chapter 59 · Deployment

[简体中文](./59-deployment.md) ｜ **English**

---

> This is the book's final chapter. The previous 58 built something — type-correct, concurrency-safe, transactionally consistent, well-tested, fast enough, hardened. But it's still on your machine. **This chapter hands it over.**
>
> The **key experiment** answers a question rarely quantified seriously: **what does "running it" actually require you to carry along?** For the same "run a loop and print the result" program, the six languages' payloads differ like this: C++ produces **74 KB**; Node needs a **106.5 MB** binary; Python needs a **50 MB / 1,637-file** standard library; and the JVM needs **309 MB / 455 files** — of which your own code is **9,759 bytes**. **The ratio is 1 : 33,212. Just 0.003% of the payload is code you wrote.**
>
> That ratio explains a container-era rule of thumb directly: **base image choice affects image size far more than optimizing your code does.** Shrinking your code from 1 MB to 0.5 MB is meaningless; switching the base image from `openjdk` to `eclipse-temurin:17-jre-alpine` halves the total.
>
> The second systematically ignored variable is **startup time**. Measured, "how much is spent before entering main": C++ **1.23 ms** (exec + dynamic linking), Node **7.4 ms**, JVM **28 ms**, .NET **20 ms**. **In a long-running service this cost is negligible — amortized over months of uptime. But for serverless, CLIs, and scale-out moments, it's latency users feel directly.** There's exactly one criterion: **how many times your process starts.** Starting once and running for three months is a completely different answer from starting a hundred times a second.
>
> .NET's four publish modes make the arithmetic clearest (measured on the same code): framework-dependent **1 MB / 5 files / 20.0 ms**, self-contained **77 MB / 187 files / 20.0 ms**, self-contained + trimmed **19 MB / 34 files / 19.9 ms**, Native AOT **15 MB / 4 files / 3.3 ms**. **The first three have essentially identical startup times** — self-contained and trimming solve portability and size, **not startup**. Only AOT drops startup to 3.3 ms (6x faster), because there's no CLR to load and no JIT to run. The price is in the last column: **give up reflection and dynamic loading.** Chapter 30's bill comes due here.
>
> A third finding comes from a deliberately controlled experiment: **identical total bytes, only the file count changes.** One 8 MB file copies in **4.0 ms**; 4000 files of 2 KB copy in **319.0 ms — 79x slower.** The byte count is identical; the slowdown is **entirely per-file metadata overhead**. That's why Node production deployments bundle: **bundling isn't about smaller size, it's about reducing file count by orders of magnitude.**
>
> Finally, the one part that **cannot be rolled back**: the database. Rolling back code just restores an old artifact, but after `DROP COLUMN` the data is gone, an `ALTER` on a large table takes just as long to reverse, and rollback scripts are almost never tested — **the day you need one is the first time you run it.** So the real strategy isn't "write good rollback scripts" but **make changes that don't need rolling back**: the expand-contract pattern splits one column rename into **four releases**, and every state before and after each one serves both old and new code.
>
> In one sentence: **all of deployment is reducing the assumptions your artifact makes about its environment — fewer assumptions, more places it runs, and less "works on my machine."**

## 1. Learning Objectives

After this chapter you will be able to:

- State what each of the six languages **must carry along to run**, and how that determines image size and build speed;
- Quantify what **startup time** means for different deployment shapes, and use "start count" as a selection criterion;
- Explain the three concrete causes of **"works on my machine"** and defend against each;
- Distinguish **mutable from immutable infrastructure**, and explain why configuration drift is so hard to notice;
- Compare **recreate / rolling / blue-green / canary** on rollback speed versus resource cost;
- Design a database change that needs no rollback using the **expand-contract pattern**;
- State what each of observability's three pillars answers, and why missing one makes "was this release successful?" unanswerable.

---

## 2. Why This Concept Exists

### 2.1 Deployment Is Reconciling Assumptions

A program runs on your machine because a large pile of assumptions **all happen to hold**:

```
the right runtime version is installed     the right system libraries exist
environment variables are set              the timezone is what you expect
the database schema is current             config files are where you think
the network can reach dependencies         the filesystem is writable
the CPU architecture matches               the character encoding matches
```

**None of these is written in the code.** All of deployment is turning that implicit list into something explicit, reproducible, and true on someone else's machine.

**"Works on my machine" isn't a joke — it's the name of an unwritten list of assumptions.**

### 2.2 Where the Payload Boundary Lies

Languages define "the artifact" very differently, and that directly determines deployment difficulty:

| Language | Artifact contains | Target machine must supply |
|---|---|---|
| C++ | one executable (74 KB measured) | libc, libstdc++, system libraries |
| Java | jar (KB scale) | **the entire JVM (309 MB measured)** |
| C# | dll (1 MB measured) | .NET runtime (74 MB measured) |
| Python | .py source | **interpreter + stdlib (50 MB measured) + all third-party packages** |
| Node | .js source | **node binary (106.5 MB measured) + node_modules** |
| Go/Rust | one static binary | (almost nothing) |

Note the counterintuitive part of the C++ row: **the smallest artifact, and it isn't self-contained.** Measured dependencies of this chapter's C++ example:

```
/usr/lib/libc++.1.dylib
/usr/lib/libSystem.B.dylib
```

Those libraries **aren't in the artifact** — the target machine must supply them, and a missing or mismatched version means the program fails to start or behaves differently.

**Containers solve exactly this**: an image is the application *plus* the entire userland environment it needs. It turns "must be supplied by the target machine" into part of the artifact.

### 2.3 Three Kinds of "Works on My Machine"

**① Runtime/toolchain mismatch** — the easiest to catch, because it usually **fails at startup**:

```
Compiled with JDK 17, run on JDK 11 → UnsupportedClassVersionError
Binary built for x86 run on ARM      → exec format error
npm install with native modules on macOS, copied into a Linux container → crash
```

**② Implicit environment dependencies** — the most dangerous, because they **don't error, they just produce different results**:

```
different file.encoding → mojibake, and only in some environments
different timezone      → every date boundary shifts (dev machine Asia/Shanghai, container UTC)
different locale        → "I".toLowerCase() is not "i" in the Turkish locale
```

**③ External state** — the hardest, because it **isn't in your artifact at all**:

```
database schema version, config-center values, dependency service versions,
DNS, clocks, certificate expiry
```

Category ③ is exactly what this chapter's SQL section addresses.

### 2.4 Immutable Infrastructure: Turning Ops into Build

**The mutable approach**: ssh in, `apt install`, edit config, apply a patch. The consequence is that each machine's state is determined by **"every command ever run on it."** Months later nobody can say how any two machines differ — that's **configuration drift**.

You've seen its symptoms:

```
"Just restart that box and it's fine"
"Only node 3 has the problem"
"I don't know why, but taking it out of the cluster fixed it"
```

**The immutable approach**: to change anything, rebuild an image and **replace the whole machine**. State is **entirely determined by the build artifact**, independent of history. Rollback is switching back to the previous image, not "undoing the change."

> Key insight: **immutable infrastructure turns an operations problem into a build problem** — and build problems can be versioned, reproduced, and tested (Chapter 54).

---

## 3. How It Works Underneath

### 3.1 What Startup Time Is Made Of

```
exec()                          ← kernel loads the executable
dynamic linker resolves symbols ← C++ ends here (measured 1.23 ms)
initialize the language runtime ← JVM/CLR/V8 live here
  ├─ allocate and initialize the heap
  ├─ load core class libraries / builtin modules
  └─ JIT-compile the startup path
run user top-level/static initializers ← Python's imports live here
enter main()
```

Every layer's cost is measured in this chapter:

| Language | Before user code | Dominant cost |
|---|---|---|
| C++ | **1.23 ms** | exec + dynamic linking |
| Node | **7.4 ms** | initialize V8, build the event loop |
| .NET | **20.0 ms** | load CLR, JIT the startup path, init GC heap |
| JVM | **28 ms** | load JVM, class loading, JIT |
| .NET AOT | **3.3 ms** | no CLR, no JIT |

**This cost is fixed**: it doesn't scale with request volume, only with **how often the process starts**.

```
Long-running service: start once, run three months → 28 ms over 90 days = irrelevant
CLI tool:             start on every invocation    → 28 ms felt every single time
Serverless:           cold start                   → 28 ms plus image pull = hundreds of ms
Autoscaling:          batch starts on a traffic spike → slow startup means scaling lags traffic
```

### 3.2 Why File Count Beats File Size

This chapter runs a clean control: **identical total bytes, only the file count changes.**

```javascript
// 8 MB in one file  vs  8 MB in 4000 files
```

```
1 file of 8 MB     → copy took   4.0 ms
4000 files of 2 KB → copy took 319.0 ms (79x slower)
```

Byte counts are identical; the slowdown is **entirely per-file metadata overhead** (`open`/`write`/`close`, directory updates, inode allocation).

This explains three things:

1. **Slow container builds**: `COPY node_modules` copies hundreds of thousands of tiny files;
2. **Slow cold starts**: every `require`/`import` is a filesystem lookup;
3. **What bundling actually buys**: `esbuild`/`webpack`/`ncc` turn tens of thousands of files into a few — **not "smaller," but "orders of magnitude fewer files."**

### 3.3 Image Layers: Split by Change Frequency

A container image is layered; each layer is a read-only filesystem diff. **Change an upper layer and the lower ones aren't re-transferred.**

A typical Java image (bottom to top, increasing change frequency):

```
① base OS         ~50 MB   changes every few months
② JRE            ~200 MB   changes every few months
③ third-party jars ~50 MB   changes every few weeks
④ your code         ~1 MB   CHANGES DOZENS OF TIMES A DAY
```

As one fat jar, changing a line re-transfers **all 300 MB**. Layered, only layer ④'s 1 MB moves.

> General principle: **layer by change frequency — stable at the bottom, volatile on top.** This is Chapter 54's incremental build idea again: **don't redo what didn't change.**

The most common Dockerfile expression:

```dockerfile
COPY package.json package-lock.json ./   # changes rarely
RUN npm ci                               # dependency layer, cached
COPY . .                                 # changes often, goes last
```

Writing it backwards (`COPY . .` before `npm ci`) makes **every code change reinstall every dependency**.

### 3.4 The Fundamental Conflict Between Dynamism and Ahead-of-Time Compilation

The last column of the C# measurements reveals a tradeoff that runs through the whole book:

```
framework-dependent → target machine needs the matching runtime
self-contained      → nothing to install
self-contained+trim → reflection needs explicit configuration
Native AOT          → give up reflection / dynamic loading
```

**Every step toward faster startup gives up some dynamic capability.**

Because trimming and AOT both need to **statically determine which code is reachable**. Reflection (Chapter 30), dynamic class loading, and runtime code generation make that analysis **impossible in principle**:

```csharp
Type.GetType(configValue).GetMethod(anotherConfigValue).Invoke(...)
```

The compiler cannot know what `configValue` will be. So it has two options: conservatively keep everything (trimming does nothing), or require you to **declare explicitly** (a config file listing every reflected type).

> This is Chapter 30's bill coming due at deployment: **the more dynamic your runtime, the harder it is to compile ahead of time.**

The same conflict appears on the Java side as `jlink` trimming and GraalVM native-image reflection configuration — this chapter's Java example also measures module counts (the JDK provides 70; this process resolves 62).

### 3.5 Release Strategies: Rollback Speed Is Bought with Resources

| Strategy | Resources | Downtime | Rollback speed | Fits |
|---|---|---|---|---|
| Recreate | 1x | yes | fast (just redo it) | internal tools |
| Rolling | 1.2x | none | slow (roll back through) | the default |
| Blue-green | **2x** | none | **fastest, just switch traffic** | critical systems |
| Canary | 1.1x | none | fast (pull the 1% back) | risky changes |

**The core tradeoff: rollback speed is bought with resources.** Blue-green is fastest because it permanently maintains a second full environment.

**Canary's distinctive value isn't fast rollback** — it's that **a failure only reaches 1% of users**. It downgrades "how fast can we roll back" into "how large is the blast radius."

⚠️ **But every one of these strategies assumes old and new versions can run simultaneously.**

During a rolling release the cluster runs v1 and v2 together; blue-green overlaps briefly at the switch; canary coexists for a **long** time. Break that assumption — most commonly with a **database schema change** — and all the strategies fail at once.

### 3.6 Expand-Contract: Making Changes That Need No Rollback

Rolling back code just restores an old artifact. Rolling back a database is **not that cheap**:

```
ⓐ after DROP COLUMN the data IS GONE; rollback restores structure, not content
ⓑ an ALTER on a large table may run for tens of minutes, and so does its reversal
ⓒ rollback scripts are almost never tested — the day you need one is the first time you run it
```

So the real strategy isn't "write good rollback scripts" but **make changes that don't need rolling back**: every step stays compatible with both versions of the application.

Renaming `users.name` to `users.full_name` safely takes **four releases**:

```
Release 1 [expand]   ALTER TABLE users ADD COLUMN full_name TEXT;   -- nullable, invisible to old code
Release 2 [dual-write] new code writes both, and backfills history
Release 3 [switch read] new code reads only full_name (still dual-writing, so release 2 is reachable)
Release 4 [contract]  once nothing uses name, DROP COLUMN name
```

**Four releases; every state before and after each one serves both old and new code.** The cost is time (several release cycles); the benefit is that **any step can be paused or reversed**.

The pattern applies to every breaking change: renaming columns, changing types, splitting tables, tightening constraints.

---

## 4. JavaScript

The Node example is about what "tens of thousands of small files" means for deployment.

### 4.1 Startup Time

```javascript
// ⚠️ Must be read first: process.uptime() is seconds since the Node process started
const startupMs = process.uptime() * 1000;
```

```
Node start → reaching line 1 of this file: 7.4 ms
5-million-iteration business loop:        18.8 ms
```

That startup time initializes V8, builds the event loop (Chapter 43), and loads builtin modules.

### 4.2 The Runtime: One 106.5 MB Binary

```
process.execPath = .../bin/node
node executable: 106.5 MB (a single file; V8 is inside it)
builtin modules: 68
```

Contrast with Java: **the JVM runtime is hundreds of files totaling 309 MB; node is one 106.5 MB binary.**

- Single-file upside: simple distribution, no "wrong runtime version installed" problem;
- Single-file cost: **it can't be trimmed** — modules you never use ship anyway.

### 4.3 The Measured Cost of File Count

```
1 file of 8 MB     → copy took   4.0 ms
4000 files of 2 KB → copy took 319.0 ms (79x slower)
```

> What bundling buys in deployment **isn't "smaller," it's "orders of magnitude fewer files."**

### 4.4 Node's Three Kinds of "Works on My Machine"

```
Cause 1 [native modules]: packages with C++ extensions compile against the LOCAL ABI
   npm install on macOS, copy node_modules into a Linux container → crash
   this is also why .dockerignore must include node_modules
Cause 2 [lockfile unused]: npm install re-resolves within semver ranges
   you must use npm ci (Chapter 53)
Cause 3 [implicit global state]: env vars, timezone, locale, writable temp dirs
   measured: local timezone Asia/Shanghai; containers usually default to UTC — every date shifts
```

### 4.5 Health Probes: Why the Three Must Be Separate

```
liveness (alive?):    failure means RESTART. Keep the checks minimal —
   if liveness touches the database, one database hiccup restarts every instance at once
readiness (take traffic?): failure removes traffic but MUST NOT restart
startup (still booting?):  a grace period for slow starters, so liveness doesn't kill them
```

**Merging them turns a dependency hiccup into a fleet-wide restart.**

Graceful shutdown matters just as much:

```
SIGTERM → set readiness false immediately → drain in-flight requests → exit
```

Without that step, **every release drops a batch of requests**.

---

## 5. Python

The Python example is about dependencies being resolved **at deploy time**.

### 5.1 The Payload

```
stdlib directory: .../lib/python3.9
  50 MB / 1,637 files
interpreter executable: 0.1 MB
```

Python's runtime is **a small executable plus a large pile of .py source**. Consequence: the standard library alone means copying 1,637 files into a container; with third-party packages it's tens of thousands — the same problem measured in the Node example.

### 5.2 Import Cost: Paid Again on Every Start

```
import json                        2.3 ms
import sqlite3                     2.4 ms
import email                       0.4 ms
import unittest                    6.1 ms
import xml.etree.ElementTree       4.0 ms
five stdlib modules total:        15.4 ms
```

Python's `import` is a **runtime activity**: locate the file → compile to bytecode → execute the module's top level. `.pyc` caching skips compilation, but **lookup and execution still happen on every start**.

So the first cut in Python cold-start optimization is usually **removing imports** — especially heavy top-level ones.

### 5.3 Dependency Locking: The Minimum for Reproducible Builds

| Approach | Result | Verdict |
|---|---|---|
| `flask>=2.0` | 2.3.3 today, 2.4.0 next month | ❌ not reproducible |
| `flask==2.3.3` | always 2.3.3 | ⚠️ but **its** dependencies aren't pinned |
| `pip freeze` full lock | the whole tree is pinned | ✅ reproducible |
| plus `--require-hashes` | contents are verified too | ✅ reproducible + tamper-resistant |

Row two is the common misconception: **pinning direct dependencies does not pin transitive ones.** Row four defends against the supply-chain attacks from Chapter 58: **the version can be right and the contents still swapped.**

### 5.4 Configuration: Three Approaches and Their Failure Modes

```
ⓐ hardcoded            → one codebase per environment; "it's correct in staging" is inevitable
ⓑ config baked into the image → one codebase, but ONE IMAGE PER ENVIRONMENT
                          = what you tested isn't what you shipped
ⓒ env vars / config service → build once, deploy everywhere; the tested artifact IS the shipped one
```

**ⓑ is insidious**: it looks like configuration is separated from code, but as long as the artifact contains environment information, what you validated in staging is **not** what goes to production.

> The test: **can one build artifact run unmodified in every environment?**

---

## 6. Java

The Java example takes "how much of the payload isn't your code" to its extreme.

### 6.1 Startup Time

```java
long beforeMain = ManagementFactory.getRuntimeMXBean().getUptime();
```

```
JVM start → entering main(): 28.0 ms
5-million-iteration business loop: 4.5 ms
```

**28 ms before any business logic runs** — while the actual business code takes 4.5 ms.

### 6.2 1 : 33,212

```
runtime total: 324,118,808 bytes (309 MB, 455 files)
your code (this example's compiled output): 9,759 bytes
→ ratio 1 : 33,212 — only 0.0030% of the payload is code you wrote
```

This is the measured basis for "base image choice matters more than optimizing your code."

### 6.3 Modularity: Far More Shipped Than Used

```
modules provided by the JDK: 70
modules resolved by this process: 62
actually used here: java.base, java.management (plus defaults resolved at startup)
```

`jlink` can trim the runtime to the modules actually used. But note: **trimming is decided at build time, and discovering a missing module at runtime is too late.** And reflective loading (Chapter 30) makes "which modules are used" **statically undecidable** — the shared difficulty of AOT, trimming, and native images.

### 6.4 Environment Dependencies

```
file.encoding    = UTF-8
java.version     = 17.0.18
os.arch          = aarch64
user.timezone / user.language / java.vendor ...
```

Three causes:

- **Runtime version**: compiled with 17, run on 11 → `UnsupportedClassVersionError`. The good news is it **fails at startup**;
- **Implicit environment**: `file.encoding` / timezone / locale — **no error, just quietly different results**;
- **External state**: database schema, config, paths, clocks, DNS — **not in your artifact**.

> Defense: **declare everything explicitly** (JDK version, encoding, timezone); never rely on defaults.

### 6.5 Four Delivery Shapes

```
ⓐ jar + system JRE:  smallest artifact (a few MB), but depends on the right JRE being installed
ⓑ fat jar / container: carries everything, reproducible; image is hundreds of MB
ⓒ jlink custom runtime: trimmed to tens of MB, slightly faster startup
ⓓ GraalVM native image: millisecond startup, much lower memory,
   at the cost of slow builds, explicit reflection config, and losing JIT peak performance (Ch. 57)
```

> The criterion is **start count**: starting once for three months and starting a hundred times a second have completely different answers.

---

## 7. C++

The C++ example's theme: **smallest artifact, and not self-contained.**

### 7.1 74 KB vs 309 MB

```
this program: 74,312 bytes (0.07 MB)
```

Contrast: JVM runtime ~309 MB, node binary ~106.5 MB, Python stdlib ~50 MB. **C++'s artifact is two to three orders of magnitude smaller — because it doesn't carry a runtime.**

### 7.2 But It Depends on the Target Machine

```
/usr/lib/libc++.1.dylib (compatibility version 1.0.0, current version 1900.180.0)
/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1351.0.0)
```

These are **not in the artifact**. Static linking can pull them in (larger artifact; updating a library means recompiling everything), but static glibc has known problems (NSS/dlopen), so **musl is the common answer**.

### 7.3 What the Compiler Bakes In

```
target architecture = aarch64
operating system    = macOS
C++ standard        = 202002
standard library    = libc++ 190102
compiler            = clang 17.0
pointer width       = 64 bits
optimization        = enabled
assertions (NDEBUG) = still on
```

**Every one of those lines is decided at build time and cannot change at runtime.**

"Works on my machine" in C++ most often means this table doesn't match: different architecture (x86 → ARM), different libc (glibc → musl), different standard-library ABI (the famous `_GLIBCXX_USE_CXX11_ABI` split).

> So reproducible C++ builds must **pin the toolchain itself**, not just dependency versions.

### 7.4 Startup Overhead: 1.23 ms

```
starting this program and exiting immediately, 30 times: 2.04 ms avg
same count of the shell builtin `true` (baseline):        1.81 ms avg
→ the 1.23 ms difference IS exec + dynamic linking + exit
```

(The difference varies between runs, measuring 0.2–1.3 ms.)

Compare the rest of the chapter: JVM 28 ms, Node 7.4 ms, .NET 20 ms. **This is why CLI tools and serverless favor C++/Go/Rust**, and what GraalVM native images and .NET AOT are chasing.

### 7.5 Why Containers Solved C++ Deployment

A container image is **the application plus the entire userland environment it needs** (libc, libstdc++, certificates, tzdata). It turns §7.2's "must be supplied by the target machine" into part of the artifact.

Multi-stage builds are the standard approach:

```dockerfile
FROM gcc AS build
...
FROM debian-slim          # COPY only the binary
```

The extreme is `FROM scratch` (static linking, an image of a few MB), at the cost of **no shell, no certificates, no timezone data — nothing to inspect when things break.**

> Another familiar tradeoff: **minimalism and diagnosability are opposed.**

---

## 8. C#

The C# example does the clearest arithmetic — four publish modes measured on **the same code**.

### 8.1 Four Publish Modes (Measured)

| Mode | Artifact | Files | Entering Main() | Requires |
|---|---|---|---|---|
| Framework-dependent (default) | 1 MB | 5 | 20.0 ms | matching .NET installed |
| Self-contained | **77 MB** | 187 | 20.0 ms | nothing installed |
| Self-contained + trimmed | 19 MB | 34 | 19.9 ms | reflection needs config |
| **Native AOT** | 15 MB | **4** | **3.3 ms** | give up reflection/dynamic loading |

```bash
dotnet publish -c Release -r osx-arm64 \
  [--self-contained] [-p:PublishTrimmed=true] [-p:PublishAot=true]
```

**Three readings**:

**① The first three modes have essentially identical startup times** (all 20 ms) — self-contained and trimming solve **portability and size**, **not startup**. This is the most misunderstood point.

**② Only AOT drops startup to 3.3 ms (6x faster)**, because there's no CLR to load and no JIT to run.

**③ Trimming cuts 77 MB to 19 MB (75% off)**, and file count from 187 to 4 — the latter may matter more for container build speed than the former (see §3.2's measured 79x).

### 8.2 Runtime Share

```
application output: 146 KB / 5 files
shared framework:   74 MB / 184 files
→ ratio 1 : 517 — the same order of magnitude as Java's result
```

### 8.3 On-Demand Loading: A Missing DLL Can Hide for a Long Time

```
assemblies loaded by this process: 11
```

.NET assemblies load **on demand** (the same mechanism as Chapter 30's reflection). Consequence: a missing DLL may **only crash when that code path runs** — a branch reached only during month-end settlement can hide the problem for a month.

> Defense: run a smoke test after deployment that covers every path that **loads new assemblies**.

### 8.4 Configuration: A Common Pseudo-Solution

```
precedence: command line > env vars > config files > defaults
```

⚠️ **"Substitute config files per environment at build time" is a pseudo-solution** — it looks like separated configuration but produces N **mutually different artifacts**, so what you tested in staging ≠ what you ship.

### 8.5 The Three Pillars of Observability

```
Logs:    discrete events. Answers "what happened at that moment"
Metrics: aggregated numbers. Answers "is it healthy now"; cheap, retained long-term
Traces:  one request's full path. Answers "which hop is slow"
```

> The relationship: **metrics tell you something broke, traces tell you where, logs tell you why.**

Deployment and observability are two sides of one thing: **without observability you cannot tell whether a release succeeded** — and without that, **canary releases lose all their meaning**.

---

## 9. SQL

The database is the one part that **cannot be rolled back**.

### 9.1 The Minimal Migration Mechanism

```sql
CREATE TABLE schema_migrations(version TEXT PRIMARY KEY, applied_at TEXT, checksum TEXT);
```

```
applied 001_create_users @ 2026-08-01T10:00:00
applied 002_add_email    @ 2026-08-05T14:30:00
```

Migration state is recorded **by the database itself**, not by "who remembers running it." At deploy time the tool diffs "migrations in the repo" against "migrations in the table" and runs only the difference.

The `checksum` detects **an already-applied migration file being edited** — which means the dev machine and production schema have quietly diverged.

> Same idea as Chapter 53's lockfiles: **turn "current state" into a verifiable fact.**

### 9.2 Expand-Contract: One Rename, Four Releases

```sql
-- ❌ dangerous
ALTER TABLE users RENAME COLUMN name TO full_name;
-- during a rolling release old code runs alongside new; old code can't find `name` → everything errors
```

```sql
-- ✅ safe: four releases
-- Release 1 [expand]
ALTER TABLE users ADD COLUMN full_name TEXT;        -- nullable, invisible to old code
-- Release 2 [dual-write]
UPDATE users SET full_name = name WHERE full_name IS NULL;   -- new code dual-writes + backfills
-- Release 3 [switch read]: new code reads full_name only (still dual-writing, so release 2 is reachable)
-- Release 4 [contract]
ALTER TABLE users DROP COLUMN name;                 -- only once nothing uses it
```

### 9.3 Safety Levels of Changes

```
✅ safe (old code unaffected):  add nullable column, add table, add index (concurrently), relax constraints
⚠️ dangerous (old code breaks): drop column, rename, change type, tighten constraints,
                                 add NOT NULL without a default
```

> The test is simple: **can the old version of the code still work after this change?**

Adding indexes needs care too: `CREATE INDEX` without `CONCURRENTLY` locks the table (Chapters 49/50).

### 9.4 Schema Migration and Data Migration Are Different Things

```
schema migration: fast, atomic, can run synchronously in the release pipeline
data migration:   may touch hundreds of millions of rows; MUST be batched, interruptible, resumable
```

The anti-pattern is one full-table `UPDATE` — it locks the table for tens of minutes, i.e. **planned downtime**.

The right approach is a background job advancing by primary key in batches, recording progress and resuming after failure:

```sql
UPDATE orders SET ... WHERE id BETWEEN ? AND ?   -- a few thousand rows per batch, record position
```

### 9.5 Release Order and the Rollback Window

```
ⓐ run EXPAND migrations first (add columns/tables) — production is still old code, unaffected
ⓑ then roll out the new code — old and new run together, both work
ⓒ observe for a while (canary/metrics) and confirm
ⓓ only then run CONTRACT migrations (drop columns), IN THE NEXT RELEASE CYCLE
```

**At least one full release cycle separates ⓐ from ⓓ, and that span is your rollback window** — until it closes, you can return to the old code at any time.

⚠️ The most common incident: **merging ⓐ and ⓓ into one release, which erases the rollback window entirely.**

### 9.6 Backups

> **A backup whose restore has never been rehearsed is not a backup.**

Three questions to answer regularly:

```
RPO (how much data can we lose)  → determines backup frequency and whether WAL archiving is needed (Ch. 46)
RTO (how long can recovery take) → determines full vs incremental, and whether a hot standby is needed
Has recovery been rehearsed?      → determines whether the first two numbers are real
```

Same as Chapter 58's key rotation: **a procedure never rehearsed will not succeed when you need it.**

---

## 10. Cross-Language Comparison

| Dimension | JavaScript | Python | Java | C++ | C# | SQL |
|---|---|---|---|---|---|---|
| **Artifact** | .js source | .py source | jar | executable | dll | migration scripts |
| **Runtime you must carry** | node binary **106.5 MB** | interpreter + stdlib **50 MB** | JVM **309 MB** | **none** (uses system libs) | .NET **74 MB** | — |
| **Runtime file count** | 1 | 1,637 | 455 | — | 184 | — |
| **Before user code** | **7.4 ms** | see import cost | **28 ms** | **1.23 ms** | **20 ms** (AOT **3.3 ms**) | — |
| **When deps resolve** | install time (lockfile) | **deploy time** (unless pinned) | build time | **compile/link time** | build time + on demand | — |
| **Runtime trimming** | ❌ | partial (delete stdlib) | `jlink` | naturally minimal | `PublishTrimmed` | — |
| **Ahead-of-time** | ❌ | ❌ | GraalVM native image | natively AOT | Native AOT | — |
| **Signature pitfall** | native module ABI | dependency drift | runtime version / encoding | libc / ABI mismatch | missing DLL surfaces late | breaking schema change |

### 10.1 Three Runtime Shapes

The measurements group the six languages into three shapes:

**① No runtime (C++/Go/Rust)** — 74 KB artifact, 1.23 ms startup. The cost is **pinning the entire toolchain**, and it isn't self-contained by default.

**② Single-file runtime (Node)** — one 106.5 MB binary. Simple distribution, no wrong-version problem; the cost is that **it cannot be trimmed**.

**③ Multi-file runtime (JVM / .NET / Python)** — hundreds of files, tens to hundreds of MB. Upside: **it can be trimmed** (`jlink`, `PublishTrimmed`); downside: the build and cold-start cost of file count (a measured 79x on copying).

### 10.2 Containers Flattened the Three

Before containers, the three shapes had wildly different deployment difficulty: C++ fought libc versions, Java required a JRE on the target, Python required an interpreter plus packages.

Containers made "the runtime environment" part of the artifact, so all three became the same task: **build an image, replace the machine.**

**But containers didn't remove the difference — they relocated it**:

```
The old question: does the target machine have the right runtime?
The new question: how big is the image, how long is the build, how slow is the cold start?
```

And the answers to those three are exactly the numbers measured in this chapter.

---

## 11. Implementation Comparison

| Mechanism | JavaScript | Python | Java | C++ | C# |
|---|---|---|---|---|---|
| **Distribution form** | source (bundleable) | source + `.pyc` | bytecode (class/jar) | machine code | IL (AOT-able) |
| **Dependency resolution** | at `npm ci` | at `pip install` | at build (Maven/Gradle) | at link time | at build |
| **Missing dependency surfaces** | at `require` | at `import` | `NoClassDefFoundError` | **at startup** | **only when loaded on demand** |
| **Version mismatch** | syntax/API error | syntax/API error | `UnsupportedClassVersionError` | ABI mismatch (**may not error**) | `FileLoadException` |
| **Trimming** | bundler tree-shaking | manual | `jlink` | linker `--gc-sections` | `PublishTrimmed` |

### 11.1 When a Missing Dependency Surfaces Determines How Hard It Is to Debug

That row deserves its own look:

```
C++:         fails at startup            → easiest to catch (the deploy script errors immediately)
Node/Python: at first require/import     → usually during startup, still easy
Java:        when the class is first used → can be late
C#:          when the assembly loads on demand → CAN BE VERY LATE
```

The C# example points at the worst case: **a branch reached only during month-end settlement can hide the problem for a month.**

> Which is why post-deploy **smoke tests** must cover scenarios that load new code paths, not just "the homepage renders."

### 11.2 Static Linking and Containers Solve the Same Problem

```
Static linking:  bundle dependencies into the EXECUTABLE
Container image: bundle dependencies into the FILESYSTEM IMAGE
```

Both aim to **eliminate assumptions about the target machine**; only the granularity differs. Containers win by also covering config files, certificates, and tzdata — things that aren't libraries but are just as required.

`FROM scratch` plus static linking is both at once: an image of a few MB, at the cost of being **completely undiagnosable**.

---

## 12. Performance Analysis

### 12.1 Payload Size (Measured)

| Language | Runtime you must carry | Files | Your code | Ratio |
|---|---|---|---|---|
| C++ | **0** (uses system libs) | — | 74 KB | — |
| Python | 50 MB (stdlib) | 1,637 | source | — |
| C# | 74 MB (shared framework) | 184 | 146 KB | **1 : 517** |
| Node | 106.5 MB (node binary) | 1 | source | — |
| Java | **309 MB** (JVM) | 455 | 9,759 B | **1 : 33,212** |

### 12.2 Startup Time (Measured)

| Shape | Before user code |
|---|---|
| C++ (exec + dynamic linking) | **1.23 ms** |
| .NET Native AOT | **3.3 ms** |
| Node | **7.4 ms** |
| .NET (framework-dependent / self-contained / trimmed) | **20.0 ms** |
| JVM | **28 ms** |

**Note that the three non-AOT .NET modes are identical (20.0 / 20.0 / 19.9 ms)** — the chapter's most counterintuitive finding: **optimizing size and portability does not incidentally optimize startup.**

### 12.3 .NET's Four Publish Modes (Same Code)

| Mode | Artifact | Files | Startup |
|---|---|---|---|
| Framework-dependent | 1 MB | 5 | 20.0 ms |
| Self-contained | 77 MB | 187 | 20.0 ms |
| Self-contained + trimmed | 19 MB (**75% off**) | 34 | 19.9 ms |
| Native AOT | 15 MB | 4 | **3.3 ms (6x)** |

### 12.4 The Cost of File Count (Measured)

| Shape | Total bytes | Files | Copy time |
|---|---|---|---|
| One large file | 8 MB | 1 | **4.0 ms** |
| Many small files | 8 MB | 4000 | **319.0 ms (79x)** |

**Byte counts are identical.** The slowdown is entirely per-file metadata overhead.

### 12.5 Three Things These Numbers Say

**① Your code takes up almost no space in the payload.** 1 : 33,212 means optimizing business-code size is pointless while changing the base image halves the total.

**② "Smaller" and "faster to start" are different things.** .NET trimming saved 75% of size and **not one millisecond** of startup. Only changing the compilation model (AOT) moves startup.

**③ File count is a systematically ignored dimension.** A 79x difference appears in no "image size" metric, yet it directly determines container build and cold-start speed.

---

## 13. Engineering Practice

### 13.1 Build Once, Deploy Everywhere

The most practical of the twelve factors, and the easiest to break in practice.

```
✅ correct: one artifact + N sets of configuration
❌ broken:  a separate build per environment ("prod jar" / "staging image")
```

**Breaking it makes testing meaningless**: the artifact you validated in staging is **not** the one going to production.

Self-check: **can one build artifact run unmodified in every environment?** If not, find what baked environment information into it.

### 13.2 A Workable Dockerfile Skeleton

```dockerfile
# Build stage: full toolchain
FROM node:22 AS build
WORKDIR /app
COPY package.json package-lock.json ./     # ← changes rarely, its own layer
RUN npm ci                                  # ← dependency layer, cached
COPY . .
RUN npm run build                           # ← bundle into few files (the 79x bill)

# Runtime stage: artifact only
FROM node:22-slim
WORKDIR /app
COPY --from=build /app/dist ./dist
USER node                                   # ← don't run as root (Chapter 58)
CMD ["node", "dist/main.js"]
```

Four points:

1. **Separate dependency and code layers** (§3.3);
2. **Multi-stage build**: no compiler in the runtime stage;
3. **Bundle**: tens of thousands of files become a few;
4. **Non-root user**: directly related to container-escape blast radius (Chapter 58's least privilege).

### 13.3 Release Checklist

```
Before:
  □ the artifact came from the build pipeline, not a local build
  □ expand migrations are done, and old code still works
  □ the rollback plan is "switch back to the previous artifact," not "run a rollback script"
  □ alert thresholds for key metrics are in place

During:
  □ small traffic first (canary), observe long enough before ramping
  □ observe BUSINESS metrics (error rate, latency, conversion), not just "the process is alive"

After:
  □ smoke tests cover paths that load new code (§11.1)
  □ contract migrations deferred to the next cycle (the rollback window)
```

### 13.4 Rollback Must Be Easier Than Release

A reliable standard: **rolling back must take fewer steps, less time, and less thought than releasing.**

If rollback requires "stop the service, run a script, then hand-edit config," then at the moment you actually need it — 2 a.m., alerts firing, everyone tense — **it will not succeed.**

Same law as Chapter 58's key rotation and §9.6's backup restore: **a procedure never rehearsed will not succeed when needed.**

### 13.5 Deployment and Observability Are One Thing

```
Metrics tell you SOMETHING BROKE
Traces tell you WHERE
Logs tell you WHY
```

A canary release's entire value rests on being able to judge whether that 1% of traffic is healthy. **Without observability, a canary just prolongs the outage.**

The minimum viable set:

```
error rate, P95/P99 latency, throughput   ← tagged by version, or you can't compare old vs new
resource usage (CPU/memory/connections)
business metrics (order success, payment success) ← most important, most often missing
```

---

## 14. Best Practices

- **Build once, deploy everywhere**: the artifact contains no environment-specific values.
- **Configuration via env vars or a config service**, never baked into the artifact.
- **Immutable infrastructure**: rebuild and replace, don't ssh in and edit.
- **Layer by change frequency**: separate dependency and code layers; use multi-stage builds.
- **Bundle into few files**: the measured cost of file count is 79x and appears in no size metric.
- **Choose the deployment shape by start count**: long-running services don't care about startup; serverless/CLI care about nothing else.
- **Pin dependencies** (lockfile + hashes) and **pin the toolchain** (essential in C++).
- **Keep the three probes separate**: keep liveness minimal so dependency hiccups don't cause fleet restarts.
- **Shut down gracefully**: SIGTERM → drain traffic → finish in-flight requests → exit.
- **Use expand-contract for database changes**, with expand and contract in different release cycles.
- **Batch data migrations**, interruptible and resumable; never one full-table UPDATE.
- **Run smoke tests after deployment**, covering paths that load new code.
- **Rollback must be simpler than release**, and **rehearsed**.
- **Observability before canaries**: if you can't judge success, a canary is pointless.

---

## 15. Common Pitfalls

### Pitfall 1: A Separate Build per Environment

```
production uses build:prod, staging uses build:test
```

**Why it's wrong**: the artifact you validated in staging isn't the one going to production — testing loses its meaning.

**How to avoid it**: one artifact + N configurations. Self-check: can one artifact run unmodified everywhere?

### Pitfall 2: COPY All Code Before Installing Dependencies

```dockerfile
COPY . .
RUN npm ci        # ⚠️ any code change invalidates this layer's cache
```

**Why it's wrong**: changing one line of business code reinstalls every dependency; builds go from seconds to minutes.

**How to avoid it**: `COPY package*.json` → `npm ci` → `COPY . .`.

### Pitfall 3: Copying node_modules into the Image

**Why it's wrong**: packages with C++ extensions compile against the **local ABI**. `npm install` on macOS then copy into a Linux container → crash.

**How to avoid it**: add `node_modules` to `.dockerignore` and run `npm ci` inside the image.

### Pitfall 4: Expecting Self-Contained Publishing to Speed Up Startup

**Why it's wrong**: measured here — framework-dependent 20.0 ms, self-contained 20.0 ms, trimmed 19.9 ms. **Not one millisecond saved.** Self-contained solves portability; trimming solves size.

**How to avoid it**: faster startup has exactly one path — **change the compilation model** (AOT / native image), at the cost of reflection and dynamic loading.

### Pitfall 5: A Liveness Probe That Checks the Database

```yaml
livenessProbe:
  httpGet: { path: /health/full }    # ⚠️ this checks the database
```

**Why it's wrong**: a 30-second database hiccup marks **every instance dead and restarts them all at once** → cascade.

**How to avoid it**: liveness checks only "can the process respond"; dependency checks go in readiness (which removes traffic without restarting).

### Pitfall 6: No Graceful Shutdown

**Why it's wrong**: exiting immediately on SIGTERM fails every in-flight request — **every release drops a batch.**

**How to avoid it**: SIGTERM → set readiness false → wait for the load balancer to drain → finish in-flight requests → exit.

### Pitfall 7: Merging Expand and Contract into One Release

```sql
ALTER TABLE users RENAME COLUMN name TO full_name;   -- ⚠️ all at once
```

**Why it's wrong**: during a rolling release old code runs alongside new, and old code can't find `name` → **everything errors**. Worse, **you can't roll back** — the old code no longer works either.

**How to avoid it**: four-step expand-contract, with expand and contract in different release cycles.

### Pitfall 8: One UPDATE for a Data Migration

**Why it's wrong**: a full-table UPDATE over hundreds of millions of rows locks the table for tens of minutes — **unplanned downtime** — and can't resume after a failure.

**How to avoid it**: batch by primary key (a few thousand rows each), record progress, make it interruptible and resumable.

### Pitfall 9: Backups Whose Restore Was Never Rehearsed

**Why it's wrong**: a successful backup ≠ a successful restore. Corrupt files, a missing table, a missing step in the procedure — **you only find out by actually restoring once.**

**How to avoid it**: rehearse recovery regularly, and use the results to calibrate RPO/RTO.

### Pitfall 10: Optimizing the Size of Your Own Code

**Why it's wrong**: measured here, **0.003% of the payload is code you wrote.** Going from 1 MB to 0.5 MB doesn't change a 309 MB total.

**How to avoid it**: look at the base image and runtime first — switch to `-slim`/`-alpine`, use `jlink`/`PublishTrimmed`. That's where the hundreds of megabytes are.

---

## 16. Interview Questions

**Q1: Why are container images layered, and how should you split them?**

A: Layers are read-only filesystem diffs; **changing an upper layer doesn't re-transfer the lower ones**. Split **by change frequency**: stable at the bottom (base OS, runtime), volatile on top (business code). In a typical Java image the ~50 MB OS and ~200 MB JRE change every few months while the ~1 MB of business code changes dozens of times a day — as one fat jar, a one-line change re-transfers all 300 MB; layered, only 1 MB moves. Concretely in a Dockerfile: `COPY package.json` → `npm ci` → `COPY . .`. Same idea as Chapter 54's incremental builds: **don't redo what didn't change.**

**Q2: What causes "works on my machine," and how do you defend against each?**

A: Three kinds. ① **Runtime/toolchain mismatch** (JDK version, CPU architecture, native module ABI) — easiest to catch since it usually fails at startup; defend by declaring versions explicitly and building in a production-like environment. ② **Implicit environment dependencies** (encoding, timezone, locale) — the most dangerous, because they **don't error, they just quietly change results**; defend by setting them explicitly rather than relying on defaults. ③ **External state** (database schema, config, DNS, clocks) — the hardest, because it **isn't in your artifact**; defend with versioned migrations, externalized config, and contract tests.

**Q3: Why does "file count" deserve more attention than "image size"?**

A: This chapter runs a control with identical total bytes and only the file count varying: one 8 MB file copies in 4.0 ms, 4000 files of 2 KB copy in 319.0 ms — **79x slower**. The entire difference is per-file metadata overhead. It directly affects container build speed (`COPY node_modules` copies hundreds of thousands of tiny files) and cold-start speed (every `require` is a filesystem lookup). And this dimension **appears in no "image size" metric** — both are 8 MB, yet one is 79x slower. It's also the real reason production deployments bundle: **not smaller, but orders of magnitude fewer files.**

**Q4: What do self-contained publishing, trimming, and AOT each solve?**

A: Measured on the same C# code: framework-dependent 1 MB/20.0 ms, self-contained 77 MB/20.0 ms, trimmed 19 MB/19.9 ms, Native AOT 15 MB/**3.3 ms**. The reading: **self-contained solves portability** (no runtime needed on the target), **trimming solves size** (75% off), and **only AOT solves startup** (6x faster) — the first three have essentially identical startup. The cost is dynamic capability: trimming needs explicit reflection configuration; AOT gives up reflection and dynamic loading entirely. This is Chapter 30's bill coming due at deployment — **the more dynamic your runtime, the harder it is to compile ahead of time.**

**Q5: What are the tradeoffs among blue-green, canary, and rolling releases?**

A: The core tradeoff is **rollback speed bought with resources**. Blue-green needs 2x resources because it permanently maintains a second full environment, and rollback is just a traffic switch — fastest. Rolling needs 1.2x but rolling back means rolling through again — slowest. Canary needs 1.1x, and **its distinctive value isn't fast rollback but that failures reach only 1% of users** — it downgrades "how fast can we roll back" into "how large is the blast radius." Note that **all strategies assume old and new versions can run simultaneously**; a database schema change breaks that assumption and disables all of them at once. Canary's value also rests entirely on observability: if you can't tell whether that 1% is healthy, a canary just prolongs the outage.

**Q6: Why can't database changes be rolled back like code, and what's the right approach?**

A: Three reasons: ① after `DROP COLUMN` the data **is gone** — rollback restores structure, not content; ② an `ALTER` on a large table may run for tens of minutes and take just as long to reverse, leaving the system in an intermediate state; ③ **rollback scripts are almost never tested** — the day you need one is the first time you run it. So the right strategy isn't "write good rollback scripts" but **make changes that need no rollback**: use expand-contract, splitting a column rename into four releases (expand → dual-write/backfill → switch read → contract), where **every state before and after each step serves both old and new code**. The key is putting expand and contract in different release cycles — the span between them is your **rollback window**.

**Q7: Why must liveness and readiness probes be separate?**

A: Because their failure actions differ completely: liveness failure **restarts** the container; readiness failure only **removes traffic**. Putting a dependency check (say, a database query) in liveness means a 30-second database hiccup marks **every instance dead and restarts them all simultaneously** — escalating a dependency blip into a fleet-wide restart, which usually finishes off the database as every instance reconnects at once. The right split: liveness checks only "can the process respond," dependency checks go in readiness, and slow-starting applications get a separate startup probe so liveness doesn't kill them.

**Q8: What does immutable infrastructure solve?**

A: **Configuration drift.** The mutable approach (ssh in, apt install, edit config, patch) makes each machine's state a function of "every command ever run on it," and months later nobody can explain how two machines differ — symptoms are "just restart that box" and "only node 3 has the problem." The immutable approach rebuilds an image and replaces the machine, so state is **entirely determined by the build artifact, independent of history**, and rollback is switching back to the previous image. More fundamentally: **it turns an operations problem into a build problem** — and build problems can be versioned, reproduced, and tested (Chapter 54).

---

## 17. Exercises

### Exercise 1: Measure Your Payload (Basic)

For a project you're working on:

1. Measure the runtime's size and file count (JRE / node / interpreter / .NET);
2. Measure your own code's size;
3. Compute the ratio;
4. Decide: where should image slimming start?

**If the ratio exceeds 1:100, any optimization of your code's size is wasted time.**

### Exercise 2: Break Down Your Startup Time (Intermediate)

1. Measure "process start → entering main/line 1";
2. Measure "entering main → able to serve traffic (readiness passes)";
3. Find the three most expensive items in step 2 (usually: loading config, building connection pools, warming caches);
4. Estimate: if instance count must double during a traffic spike, how long until the new capacity is effective?

### Exercise 3: Reproduce the File-Count Cost (Basic)

Write a script that creates two directories with identical total bytes (one large file vs N small ones) and measure:

1. Copy time;
2. `tar` time;
3. Container image build time (if Docker is available).

**Requirement**: report N = 100 / 1000 / 10000 and plot the trend.

### Exercise 4: Design a Schema Change That Needs No Rollback (Practical)

Pick a breaking change in your project (rename a column, change a type, split a table) and write out all four expand-contract steps:

1. The SQL for each step;
2. After each step, does the **old code** still work?
3. After each step, does the **new code** still work?
4. Where does the rollback window start and end?

**Questions 2 and 3 must be "yes" at every step — one "no" means the plan is wrong.**

### Exercise 5: Rehearse a Rollback (Practical)

In a staging environment:

1. Deploy a version; record the time;
2. Deploy the next version; record the time;
3. **Roll back to the previous version; record the time and the number of steps**;
4. Compare: is rollback faster and simpler than release?
5. If not, redesign the deployment process.

**The value is entirely in question 4.** A rollback more complicated than a release will not succeed when you actually need it.

---

## 18. Chapter Summary

**Core conclusion**: all of deployment is **reducing the assumptions your artifact makes about its environment**. Fewer assumptions, more places it runs, less "works on my machine."

**Key measurements**:

| Finding | Data |
|---|---|
| How much of the payload is your code | JVM **1 : 33,212** (0.003%); .NET **1 : 517** |
| Runtime you must carry | JVM **309 MB**/455 files, node **106.5 MB**/1 file, Python **50 MB**/1,637 files, C++ **0** |
| Time before user code | C++ **1.23 ms** / AOT **3.3 ms** / Node **7.4 ms** / .NET **20 ms** / JVM **28 ms** |
| .NET's four publish modes | 1 MB·20.0ms / 77 MB·20.0ms / 19 MB·19.9ms / **15 MB·3.3ms** |
| The cost of file count | same 8 MB: 1 file **4.0 ms** vs 4000 files **319.0 ms (79x)** |
| Python import cost | five stdlib modules **15.4 ms**, paid on every start |
| C++'s dynamic dependencies | 74 KB artifact, but needs `libc++`, `libSystem` — **not self-contained** |

**Three most counterintuitive conclusions**:

```
① Your code takes up almost no space in the payload (0.003%)
   → optimizing its size is pointless; changing the base image halves the total

② "Smaller" and "faster to start" are two different things
   → .NET trimming saved 75% of size and zero milliseconds of startup
     only changing the compilation model (AOT) moves startup

③ File count is a systematically ignored dimension
   → a 79x difference appears in no "image size" metric
```

**Four principles to take away**:

```
① Build once, deploy everywhere    no environment-specific values in the artifact
② Immutable infrastructure          turn operations problems into build problems
③ Pick the shape by start count     long-running services don't care; serverless cares only
④ Make changes that need no rollback expand-contract, preserve the rollback window
```

**Self-check**:

- [ ] I know how much of my payload is runtime and how much is my code.
- [ ] I've measured how long "process start → able to serve traffic" takes.
- [ ] I can name the three causes of "works on my machine" and how to defend against each.
- [ ] My image is layered by change frequency, with dependency and code layers separate.
- [ ] I know what self-contained/trimming/AOT each solve and don't solve.
- [ ] My liveness probe doesn't depend on external services.
- [ ] My application handles SIGTERM and shuts down gracefully.
- [ ] My database changes are expand-contract with a preserved rollback window.
- [ ] My rollback is simpler than my release, and it's been rehearsed.
- [ ] I have metrics that can tell me whether a release succeeded.

---

## 🎓 Closing

That's 59 chapters.

Looking back at the route: from **Chapter 1**'s "why machine code evolved into high-level languages," through **Chapter 7**'s type systems, **Chapter 20**'s hash tables, **Chapter 27**'s vtables, **Chapter 36**'s garbage collection, **Chapter 41**'s memory models, **Chapter 49**'s B+ trees, **Chapter 57**'s cache locality, **Chapter 58**'s injection defenses, and finally this chapter's **delivery**.

If this book has one real through-line, it's the thing it kept doing: **turning "how does this work" into a question you can answer on the spot.**

Every claim came with runnable code — and **a substantial number of those experiments did not produce the expected result**:

- Node and .NET silently upgraded `fsync` to `F_FULLFSYNC` (Chapter 46);
- sqlite's optimizer **chose wrong** on a low-selectivity index (Chapter 49);
- the textbook rule "optimistic locking wins at low contention" **failed to reproduce** (Chapter 48);
- the classic fix for binary search **is itself buggy** (Chapter 21);
- the compiler **optimized away** Stack Overflow's highest-voted branch-prediction demo (Chapter 57);
- V8's polymorphic inline caches made the widespread "property order affects performance" advice **obsolete** (Chapter 57);
- "C++ skips bounds checks for performance" is worth **1.02x** at -O2 (Chapter 58).

Those failed experiments became the book's most valuable parts. Together they say one thing: **software folklore expires; the method for checking it doesn't.**

So if you take one thing from these 59 chapters, let it be this habit:

> **When you're unsure about something, write a piece of code and ask it.**

Six languages, 59 concepts, hundreds of measured numbers — all of them will expire. "When unsure, measure" won't.

**Go write better code.** 🚀

---

## 19. Further Reading

- <a href="https://12factor.net/" target="_blank" rel="noopener">The Twelve-Factor App</a> — the twelve principles for cloud-native applications; the source of §13.1's "build once, deploy everywhere."
- <a href="https://docs.docker.com/build/cache/" target="_blank" rel="noopener">Docker · Build Cache</a> — layering and cache-invalidation rules; the official basis for §3.3.
- <a href="https://martinfowler.com/bliki/BlueGreenDeployment.html" target="_blank" rel="noopener">Martin Fowler · Blue Green Deployment</a> — the classic description and its boundaries.
- <a href="https://martinfowler.com/bliki/ParallelChange.html" target="_blank" rel="noopener">Martin Fowler · Parallel Change (expand-contract)</a> — the original naming and full procedure of the pattern.
- <a href="https://kubernetes.io/docs/tasks/configure-pod-container/configure-liveness-readiness-startup-probes/" target="_blank" rel="noopener">Kubernetes · Liveness, Readiness and Startup Probes</a> — the semantic differences among the three probes; the official basis for §4.5.
- <a href="https://sre.google/sre-book/table-of-contents/" target="_blank" rel="noopener">Google · Site Reliability Engineering</a> — the systematic treatment of SRE, including release engineering and incident response.
- <a href="https://learn.microsoft.com/en-us/dotnet/core/deploying/native-aot/" target="_blank" rel="noopener">.NET · Native AOT Deployment</a> — the list of dynamic capabilities AOT gives up; the official background for §8.1.
- <a href="https://docs.oracle.com/en/java/javase/17/docs/specs/man/jlink.html" target="_blank" rel="noopener">Oracle · jlink</a> — the official tool for custom JREs; the trimming approach in §6.3.
- <a href="https://opentelemetry.io/docs/what-is-opentelemetry/" target="_blank" rel="noopener">OpenTelemetry</a> — the unified standard for logs, metrics, and traces; the implementation path for §8.5.
