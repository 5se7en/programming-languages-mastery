# Chapter 56 · Design Patterns

[简体中文](./56-design-pattern.md) ｜ **English**

---

> The 1994 book *Design Patterns* offered 23 "proven solutions to recurring problems," revered by countless teams ever since — and misused by countless teams into "every class needs an interface." This chapter doesn't restate those 23 patterns; it answers a more fundamental question: **why the same pattern carries wildly different weight in different languages**.
>
> The **key experiment** implements one requirement — "parameterize a behavior" — in six languages. GoF's strategy pattern needs **an interface + N implementation classes + a context class** (about 25 lines); Python needs `sorted(items, key=len)` (**zero extra structure**); Java 8 onward needs `Comparator.comparingInt(String::length)` (one line). All three produce identical results. **Half of GoF's patterns are, in essence, simulations of first-class functions in languages that lack them.**
>
> But "passing a function" **costs differently** per language. The C++ example measured three strategy implementations: virtual functions at **1.33 ns**, `std::function` at **0.76 ns**, templates at **0.36 ns** — **templates are 3.7× faster than virtual calls**. The C# example measured the same comparison: interface 9.76 ns versus delegate 9.98 ns, **a 1.02× gap, essentially equivalent**. Only together do these numbers form the full conclusion: **in C++ choosing a pattern is a performance decision; in JIT languages it is purely a readability decision.**
>
> The Java example unearthed the most famous crash in pattern history. Lazy-singleton thread-unsafety was reproduced (16 threads calling `getInstance()` simultaneously ran the constructor **16 times** and got **16 distinct instances**); and its standard fix, **double-checked locking, was wrong before Java 5** — `instance = new Singleton()` is not atomic, and the JVM may reorder "assign the reference" before "run the constructor," so another thread can obtain a **half-constructed object**. `volatile`'s real job here is not just visibility but **forbidding that reordering**. This is the classic case of "the pattern was right but the language's memory model made it wrong for a decade" — while after C++11, one line `static Config c;` is thread-safe (Meyers singleton, measured constructing once across 16 threads).
>
> The bigger picture: **patterns have three fates**. Absorbed into the language (iterator → `yield return`, observer → `event`, strategy → lambda, visitor → pattern matching, singleton → magic static), still necessary (adapter, facade, repository — they organize **architectural boundaries**, not language gaps), and language-specific (RAII, Pimpl, CRTP — **none of which appear in GoF**, because C++'s own problems produced them).
>
> Finally the SQL example is a reminder of the other half: **every domain grows its own pattern language**. Soft deletes, optimistic locking (measured: "0 rows affected" is the conflict signal), event sourcing (measured: replay yields a balance of 400), closure tables — none appear in GoF either, because they solve data and persistence problems.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- Explain **why half of GoF's patterns "vanished" in modern languages** (measured across three strategy implementations);
- Use measurements to show that **pattern choice is a performance decision in C++ and a readability decision in JIT languages**;
- Reproduce the **singleton thread-safety trap** and explain why double-checked locking was once wrong (Chapter 41's memory model);
- Distinguish patterns' **three fates** (absorbed / still necessary / language-specific) and use that to judge what's worth learning;
- Recognize that **every domain has its own pattern language** (databases, frontends, concurrency each have theirs).

---

## 2. Why This Concept Exists

### What a pattern is

```text
A pattern = a【recurring problem】+【a proven solution】+【a name for communicating it】
```

**The third element is routinely undervalued**: a pattern's daily value is often **communication efficiency** — "use a closure table here" replacing ten minutes at a whiteboard.

### But GoF reflects 1994's languages

```text
When Design Patterns was written, the mainstream languages were C++ and Smalltalk:
  · functions weren't first class → passing behavior meant wrapping it in an object
  · no lambdas → every strategy needed a named class
  · no generics (C++ templates barely started) → type reuse meant inheritance
→ so "simulate a function with a single-method object" became a shared skeleton for many patterns
```

### The one-sentence definition

```text
A pattern's necessity = whether the problem it solves【still exists in your language】
→ patterns solving【language gaps】disappear as languages evolve
→ patterns solving【domain complexity】persist forever
```

> **In one sentence**: the question to ask while reading GoF is not "how do I use this pattern" but **"is this still a problem in my language?"** — this chapter answers it with measurements in six languages and draws a clear line: **patterns simulating missing language features disappear; patterns organizing system boundaries remain.**

---

## 3. How It Works

### Key experiment one: one requirement, six languages' weight

**The requirement**: parameterize a sort comparison.

**The GoF version** (Python measured; equally applicable to Java 7 / C++98):

```python
class SortStrategy:                       # the abstract strategy
    def compare(self, a, b): raise NotImplementedError
class ByLength(SortStrategy): ...         # three concrete strategies
class Sorter:                             # the context
    def __init__(self, strategy): self.strategy = strategy
    def sort(self, items): ...
```

**The built-in version**:

```text
GoF (interface + 3 classes + context, ~25 lines): ['fig', 'kiwi', 'apple', 'banana']
Python sort_with(words, key=len):                 ['fig', 'kiwi', 'apple', 'banana']
identical: True
swapping strategies in Python: even an inline lambda — sort_with(words, lambda w: w[-1])
```

```text
→ the strategy pattern's essence is【parameterizing behavior】— and a function IS parameterized behavior
→ in languages without first-class functions you must simulate one with a single-method object
→ hence: half of GoF's patterns are【simulations of first-class functions】
```

**Java 8 degraded it too** (Java measured):

```text
GoF version (interface + implementation):            [fig, kiwi, apple, banana]
Java 8 Comparator.comparingInt(String::length):      [fig, kiwi, apple, banana]
→ but note Java's functions are【still objects】: a lambda compiles to a functional-interface instance
→ so Java "simulates first-class functions with syntax sugar" — still different from Python's native ones
```

### Key experiment two: but "passing a function" costs differently

**C++'s three implementations** (measured over 50M calls):

| Implementation | Time | Per call | Runtime-swappable |
|---------------|------|----------|-------------------|
| virtual function (GoF original) | 66.4 ms | **1.33 ns** | ✓ |
| `std::function` | 38.1 ms | **0.76 ns** | ✓ |
| template (compile time) | 18.2 ms | **0.36 ns** | ✗ |

```text
→ templates are 3.7× faster than virtual calls and 2.1× faster than std::function
→ the gap is【inlining】: templates know the callee at compile time; the other two jump indirectly
```

**C#'s equivalent comparison** (measured over 50M calls):

```text
interface calls: 487.9 ms (9.76 ns each)
delegate calls:  499.2 ms (9.98 ns each)
→ a 1.02× gap — essentially equivalent
```

**Only together do they form the conclusion**:

```text
C++ (no JIT, fixed at compile time): a 3.7× gap → choosing a pattern is a【performance decision】
C#/Java (with JIT):                  1.02×      → choosing a pattern is a【readability decision】
Python/JS (interpreted):             functions were first class all along → there is no "choice"
→ this is the root of a pattern's differing weight across languages
```

### Key experiment three: the singleton — right pattern, wrong language, for a decade

**The lazy singleton's thread-unsafety** (Java measured, consistent across three runs):

```java
static UnsafeLazySingleton getInstance() {
    if (instance == null) instance = new UnsafeLazySingleton();  // ⚠️ check-then-create isn't atomic
    return instance;
}
```

```text
16 threads calling getInstance(): the constructor ran 16 times, yielding 16 distinct instances
→ Chapters 46/48's read-modify-write in a new shape: the same race condition
```

**Double-checked locking: the correct version needs `volatile`** (Java measured):

```java
private static volatile DclSingleton instance;      // ← volatile is mandatory
static DclSingleton getInstance() {
    if (instance == null) {                          // first check: unlocked fast path
        synchronized (DclSingleton.class) {
            if (instance == null) instance = new DclSingleton();   // second check
        }
    }
    return instance;
}
```

```text
16 threads: the constructor ran 1 time, 1 distinct instance ✓
```

**But this was wrong before Java 5, and wrong in an extremely subtle way**:

```text
instance = new DclSingleton() is not atomic; it is three steps:
  ① allocate memory  ② run the constructor  ③ assign the reference to instance
The JVM/CPU may【reorder】② and ③ (Chapter 41's memory model), so:
  thread A reaches ③ before ② (instance is non-null but the object isn't initialized)
  thread B's first check sees instance != null → returns a【half-built object】
→ volatile's role is not merely "visibility"; it also【forbids that reordering】
→ only after Java 5 (JSR-133) revised the memory model did volatile make DCL correct
```

**And language evolution absorbed the complexity**:

```text
Java's best form — the static holder class (measured: 16 threads, 1 construction):
  the inner class loads on first access → the JVM guarantees【class initialization】is thread-safe (JLS 12.4.2)
  → lazy by nature + thread-safe by nature + zero locking

C++11's Meyers singleton (measured: 16 threads, 1 construction):
  static Config c;   // that's the whole thing
  → since C++11 the standard【requires】static local initialization to be thread-safe (magic static)
  → before C++11 this too required DCL, and hit the same reordering trap
```

**The best footnote in this chapter**: **a pattern's complexity gets absorbed into the language as it evolves.**

### The singleton is itself a suspect pattern

```text
It does two things at once: ① guarantee a single instance  ② provide a【global access point】
② is the problem: global access = hidden dependency (Chapter 55's service-locator critique)
  · untestable (Chapter 52 measured what hard-wired dependencies cost)
  · nothing in a signature reveals who uses it
→ the modern approach: let the container manage a【singleton lifetime】and inject via constructors (Ch. 55)
→ "I need exactly one instance" is legitimate; "I want global static access" is not
```

### Key experiment four: patterns' three fates

**Fate one: absorbed into the language** (measured across languages):

| GoF pattern | Python | JavaScript | C# | C++ |
|------------|--------|-----------|-----|-----|
| Strategy | pass a function | pass a function | `Func<T,R>` | lambda/template |
| Command | `partial` | closure/`bind` | `Action` | lambda |
| Observer | list of functions | `EventEmitter`/Promise | **`event` keyword** | signal/slot libraries |
| Iterator | generators (Ch. 44) | `for...of` | **`yield return`** | range-for |
| Decorator | **`@` syntax** | higher-order functions | extension methods | — |
| Singleton | modules are singletons | ES modules are singletons | — | **magic static** |
| Visitor | — | — | **pattern matching** | `std::visit` |
| Proxy | `__getattr__` | **built-in `Proxy`** | `DispatchProxy` | — |

**Fate two: still necessary** (language-independent):

```text
adapter / facade / repository / state machine
→ they organize【architectural boundaries】and【domain complexity】, not language gaps
→ Chapter 51 measured the repository's duties; Chapter 55 measured adapter-style interface isolation
```

**Fate three: language-specific** (the C++ list, measured):

```text
RAII (Ch. 37)   —— C++'s own "pattern," envied by others, now a language mechanism
Pimpl (Ch. 53)  —— isolating ABI and compile dependencies, a C++-specific fix for a C++-specific problem
CRTP            —— static polymorphism via templates (avoiding virtual-call overhead)
type erasure    —— how std::function/std::any are implemented
→ note that【none of these appear in GoF】: C++'s own problems produced them
```

---

## 4. JavaScript

The JS example reveals the observer pattern's bloodline through all of JS async.

### A hand-written observer: 30 lines is EventEmitter (measured)

```javascript
class MiniEmitter {
  constructor() { this.handlers = new Map(); }        // event name → callback list
  on(event, fn) { /* ... */ return () => this.off(event, fn); }   // returns an unsubscribe closure
  emit(event, ...args) { for (const fn of [...this.handlers.get(event) || []]) fn(...args); }
}
```

```text
two subscribers → emit(A1) → unsubscribe one → emit(A2)
["库存服务处理 A1","邮件服务处理 A1","邮件服务处理 A2"]
identical behavior to Node's built-in EventEmitter ✓
→ the observer pattern = a Map<event, callbacks> plus iteration
→ it decouples【who produces events】from【who cares about them】
```

### The observer's three generations (measured)

```text
gen 1 callbacks:     readCallback(cb)             —— the observer's plainest form
gen 2 Promise:       an observer that fires【once】—— resolve is emit, then is on
gen 3 async iterator: fires repeatedly with【backpressure】—— Chapter 44's coroutines
→ all three are the same pattern; the language built it in and standardized it, layer by layer
→ Chapter 43's event loop is the observer at the【runtime layer】; EventEmitter is the same pattern in userland
```

### The "proxy pattern" became a built-in object

```text
GoF's proxy requires hand-writing a same-interface wrapper class
JS: new Proxy(target, handler) —— the language turned the pattern into an API
→ absorption's most extreme form: the name survives, but you no longer write the code
```

### Decorator = higher-order function (measured)

```javascript
const withRetry = (fn, times) => async (...args) => { /* retry logic */ };
withRetry(flaky, 5)() → 成功 (after 3 attempts)
```

```text
→ one higher-order function replaces GoF's four classes
→ and it composes freely: withLog(withRetry(withTimeout(fn)))
```

> **Note**: the patterns still alive in JS are **middleware/pipelines** (Express/Koa's core, essentially chain of responsibility) and **state machines** (XState) — the former born of framework ecosystems, the latter of domain complexity; `export const db = new Database()` is JS's singleton idiom, with Chapter 55's cost: **it can't be swapped in tests**.

---

## 5. Python

The Python example measured the most thorough case of "patterns absorbed by the language."

### Nine GoF patterns' Python counterparts (measured table)

```text
Strategy        → pass a function
Command         → a function / functools.partial (Chapter 55's partial application)
Template Method → pass a function, or use default parameters
Factory Method  → classes are callable — pass the class itself
Abstract Factory→ a function returning constructors
Singleton       → modules are singletons (import evaluates once)
Iterator        → built into the language (Chapter 44's generators)
Decorator       → the @ syntax
Observer        → a list of functions
```

```text
→ not "Python needs no design" but【these designs are already in the language】
```

### Decorator: a GoF pattern became syntax (measured)

```text
@log_calls wrapping, called twice: record = [('甲',), ('乙',)]
@memoize called 5 times (2 distinct arguments): 2 cached, 2 actual computations
→ GoF's decorator needs 4 classes; Python needs one higher-order function and one @
```

### Singleton: modules are singletons (measured)

```text
getting this module object twice: identical? True
→ import evaluates module code once (Chapter 14's sys.modules cache) → module-level variables are singletons
→ Python's idiom is `_instance = Thing()` in a module, not getInstance()
```

> **Note**: Python still needs **repositories/adapters** (architectural boundaries) and **state machines** (domain complexity); the builder pattern is usually replaced by keyword arguments plus `dataclass`; the criterion is always **"does this pattern solve a language gap or domain complexity?"**

---

## 6. Java

The Java example owns the singleton measurements (data in key experiment three); here is their historical weight.

### The four singleton forms

| Form | Thread-safe | Lazy | Lock cost | Notes |
|------|------------|------|-----------|-------|
| Eager | ✓ (JVM class init) | ✗ | none | simple; may build an unused instance |
| Lazy, unsynchronized | **✗** (measured 16 instances) | ✓ | none | the broken form |
| DCL + `volatile` | ✓ (measured 1) | ✓ | first call only | **wrong before Java 5** |
| **Static holder class** | ✓ (measured 1) | ✓ | **none** | Java's best practice |

**Even better is the enum singleton** (*Effective Java*'s recommendation): it also resists reflection and serialization attacks.

### DCL's lesson exceeds the singleton

```text
"one assignment statement isn't atomic" affects far more than singletons:
  · Chapter 41's volatile ≠ atomic (measured: volatile int still loses updates)
  · Chapter 41's memory model and reordering
  · safe publication in lock-free data structures
→ DCL is the most famous demonstration of all these concepts
```

> **Note**: Java 8's lambdas degrade strategy/command/template method into functional interfaces, though lambdas still compile to objects; `Optional` is the null-object pattern made linguistic; `record` + `sealed interface` + `switch` pattern matching gave Java C#'s visitor alternative.

---

## 7. C++

The C++ example's core: **here, choosing a pattern is a performance decision** (data in key experiment two).

### Where each implementation applies

```text
virtual function: the strategy is chosen at【runtime by config/user】and calls aren't on a hot path
std::function:    you need to hold arbitrary callables (especially state-capturing lambdas)
template:         the strategy is fixed【at compile time】and sits on a hot path
→ std::sort's comparator is a template parameter while qsort's is a function pointer —
  one reason std::sort is faster
```

### C++'s four own patterns (none in GoF)

```text
RAII (Ch. 37)   —— already a language mechanism; others imitate it with try-with-resources/using
Pimpl (Ch. 53)  —— isolating ABI and compile dependencies (Ch. 54 measured header fan-out's cost)
CRTP            —— static polymorphism via templates, the generalization of key experiment two's template version
type erasure    —— how std::function/std::any are implemented
→ born of C++'s unique constraints: manual memory, ABI, the compilation model, zero-overhead philosophy
```

> **Note**: the Meyers singleton (`static Config c;`) is standard since C++11, measured constructing once across 16 threads; before C++11 this too required DCL and hit the same reordering trap; `std::visit` + `std::variant` is C++'s pattern-matching visitor.

---

## 8. C#

The C# example measured "an evolution history of absorbing design patterns into a language."

### The patterns C# ate (measured)

```text
Strategy   → Func<T,R> / delegates (measured equivalent to interfaces at 1.02×)
Command    → Action / delegates + closures
Observer   → the event keyword (measured += subscribe, -= unsubscribe)
Iterator   → yield return (measured Fibonacci(10))
Visitor    → switch pattern matching (measured identical to the visitor version)
Decorator  → extension methods / higher-order functions
Adapter    → extension methods (adding methods to someone else's type)
Null Object→ nullable reference types + ??
Prototype  → record's with expression
```

### `event`: the observer, built in (measured)

```text
two subscribers → Place(A1) → unsubscribe one → Place(A2)
库存服务处理 A1 | 邮件服务处理 A1 | 邮件服务处理 A2
→ event is even safer than hand-rolling: an event field can only be Invoked inside its declaring class
⚠️ but a classic leak: a subscriber that never unsubscribes keeps the publisher holding its reference (Ch. 36's GC)
```

### Visitor versus pattern matching: the expression problem

```text
GoF visitor: 1 IVisitor interface + one Visit per type + Accept implemented per type
pattern matching: one switch expression (measured identical results)
→ the visitor solves "add operations without changing the classes" — the【expression problem】
→ functional languages solve it natively with algebraic data types plus pattern matching
   (record + sealed + switch is its C# form)
```

> **Note**: `IObservable<T>`/`IObserver<T>` are .NET's standardized observer interfaces (Rx's foundation); extension methods all but eliminate the adapter pattern; source generators are eating more patterns still (such as Chapter 55's compile-time DI).

---

## 9. SQL

The database world has **its own pattern language** — none of it in GoF.

### Soft deletes (measured)

```text
3 physical rows, 2 visible through the view
→ benefits: recoverable, auditable, foreign keys stay intact
→ cost: every query must remember `deleted_at IS NULL` (miss once and it's a data leak)
→ hence the companion practice of【views as interfaces】(Ch. 55): the app queries only active_users
→ indexes must cooperate too: Chapter 49's partial index WHERE deleted_at IS NULL is smaller and faster
```

### Optimistic locking (measured, Chapter 48 made into a pattern)

```sql
UPDATE docs SET content='...', version=version+1 WHERE id=1 AND version=1;
```

```text
user A commits (holding version=1): 1 row affected ✓
user B commits (also holding version=1): 0 rows affected → 【rejected】
current content: 甲的修改, version=2
→ "0 rows affected" IS the conflict signal (Chapter 48's C# example measured its ORM form)
→ this is a【pattern】, not a language feature: same in any database, any language
```

### Event sourcing and snapshots (measured)

```text
event stream: 开户0 → 存入500 → 取出200 → 存入100
replayed balance: 400
snapshot: balance 400, covering up to seq=4
after a new event: current balance = snapshot + incremental replay = 450
→ the same idea as Chapter 46's WAL and Chapter 54's migrations: state = a fold over events
→ the snapshot pattern is isomorphic to Chapter 54's incremental builds:
  snapshot = build artifact, upto_seq = how far it's applied
```

### Two models for tree structures

```text
adjacency list (parent_id): simple writes, descendants need a recursive CTE
  recursive query for 电子's descendants: 电子 → 手机 → 安卓机
closure table: additionally store【every ancestor-descendant pair】—
               queries become a single JOIN, at the cost of write maintenance
→ the classic read/write trade-off (Chapter 49's index bill): optimizing reads costs writes
```

> **Note**: none of these appear in GoF, because they solve **data and persistence** problems — **every domain grows its own pattern language**: GoF covers object orientation, Fowler's PoEAA covers enterprise applications, these cover data.

---

## 10. Cross-Language Comparison

### ① One pattern's shape across six languages

| GoF pattern | C++ | Java | C# | Python | JavaScript |
|------------|-----|------|-----|--------|-----------|
| **Strategy** | virtual/`std::function`/template | lambda (Java 8+) | `Func<T,R>` | pass a function | pass a function |
| **Observer** | signal/slot libraries (Qt/Boost) | listener interfaces | **`event` keyword** | list of functions | `EventEmitter`/Promise |
| **Iterator** | range-for + iterators | `Iterable` | **`yield return`** | generators | `for...of` |
| **Singleton** | **magic static** | static holder/enum | — | modules | ES modules |
| **Decorator** | — | annotations + proxies | extension methods | **`@` syntax** | higher-order functions |
| **Visitor** | `std::visit` | `sealed` + `switch` | **pattern matching** | — | — |
| **Proxy** | — | dynamic proxies (Ch. 55) | `DispatchProxy` | `__getattr__` | **built-in `Proxy`** |

### ② Key experiment data summary

```text
strategy pattern: GoF ~25 lines vs Python one function vs Java 8 one line — identical results
C++ three forms:  virtual 1.33 ns / std::function 0.76 ns / template 0.36 ns (3.7× faster)
C# two forms:     interface 9.76 ns / delegate 9.98 ns (1.02×, essentially equal)
lazy singleton:   16 threads → 16 constructions, 16 distinct instances (consistent across 3 runs)
DCL + volatile:   16 threads → 1 construction ✓ (but wrong pre-Java 5 due to reordering)
static holder:    16 threads → 1 construction ✓ (zero locking)
Meyers singleton: 16 threads → 1 construction ✓ (C++11 magic static)
decorator:        @memoize, 5 calls / 2 distinct args → 2 cached, 2 computations
observer:         30 hand-written lines behave identically to Node's EventEmitter
SQL optimistic:   A affects 1 row ✓, B affects 0 rows (the conflict signal)
SQL event sourcing: replayed balance 400; snapshot + incremental = 450
```

### ③ Common ground and root causes

**Common ground**: every language is absorbing patterns into itself (iterator, observer, and strategy go first); every language retains the patterns that organize architectural boundaries (adapter, facade, repository); every domain grows its own pattern language.

**Root causes**:

- **First-class functions are the great divide**: strategy, command, template method, and decorator all "simulate first-class functions" — give a language them and those patterns vanish;
- **The performance model sets the choice's weight**: C++ has no JIT and fixes things at compile time (measured 3.7×) → choosing a pattern is choosing performance; JIT languages converge (measured 1.02×) → choosing a pattern is choosing readability;
- **A memory model can make a correct pattern wrong**: DCL is the most famous case, and only JSR-133's revision of `volatile` made it valid;
- **Language evolution absorbs a pattern's complexity**: Java's static holder and C++11's magic static both erase the singleton's synchronization details;
- **Patterns are domain-bound**: GoF covers object orientation, PoEAA covers enterprise applications, and databases/frontends/concurrency each have their own — **there is no universal catalog**.

---

## 11. Implementation Comparison

| Language mechanism | Pattern absorbed | How it works |
|-------------------|-----------------|--------------|
| **`yield return`** (C#) | iterator | the compiler generates a state machine (Ch. 44 measured `<Counter>d__0`) |
| **`event`** (C#) | observer | a multicast delegate's invocation list plus access restrictions |
| **`@` decorators** (Python) | decorator | syntax sugar for `f = deco(f)` |
| **magic static** (C++11) | singleton | the compiler inserts a one-time guard variable and synchronization |
| **static holder** (Java) | singleton | the JVM guarantees thread-safe class initialization (JLS 12.4.2) |
| **`Proxy`** (JS) | proxy | engine-level property-access interception |
| **module caching** (Python/JS) | singleton | `sys.modules` / the ESM module graph evaluates once (Ch. 14) |

**A pattern in the pattern**:

```text
Every absorbed pattern ends up as【compiler-generated code】or【a runtime built-in】
→ you no longer write it, but it still runs — the pattern didn't vanish, its implementer changed
→ Chapter 44's coroutine state machines, Chapter 51's ORM object graphs,
  and Chapter 55's DI assembly code are all the same phenomenon
```

---

## 12. Performance Analysis

### This chapter's numbers at a glance

```text
C++ three strategies: virtual 1.33 ns / std::function 0.76 ns / template 0.36 ns (3.7×)
C# two strategies:    interface 9.76 ns / delegate 9.98 ns (1.02×)
singleton safety:     lazy 16 instances vs DCL/static holder/magic static 1 each
```

### Where a pattern's performance cost comes from

```text
indirect jumps: virtual calls, std::function, delegates — the compiler can't inline
allocations:    the GoF strategy allocates a strategy object (Chapter 33's allocation cost)
extra layers:   three stacked decorators = three calls plus three closure objects
→ these deserve care in C++; in JIT languages most are optimized away
→ but the cost【every language shares】isn't performance — it's cognitive load (below)
```

> ⚠️ **A pattern's biggest cost is its abstraction layers.** Each indirection is one more jump while reading; the GoF strategy's three classes correspond to `key=len` in Python — **the two extra classes carry no domain information**. Chapter 55 said "a single-implementation interface is pure indirection"; the criterion repeats here: **will you actually use the flexibility this pattern buys?**

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Parameterizing behavior | pass a function/lambda | writing strategy classes | measured identical results, 20+ fewer lines |
| C++ hot-path strategy | templates | virtual functions | measured 3.7× |
| C++ runtime switching | virtual/`std::function` | forcing templates | templates can't switch at runtime |
| Choosing in JIT languages | choose for readability | guessing at performance | measured 1.02×; performance isn't a factor |
| Needing a single instance | container-managed lifetime + injection | hand-written `getInstance()` | Ch. 55 measured; global access hides dependencies |
| Hand-written singleton (Java) | static holder/enum | DCL | measured equally correct and far simpler |
| Hand-written singleton (C++11+) | `static T& instance()` | DCL | magic static is already thread-safe |
| "Add operations without changing classes" | pattern matching (if available) | the visitor pattern | measured identical results, half the code |
| Event notification | the language built-in (`event`/EventEmitter) | hand-rolled observers | built-ins are safer (restricted Invoke) |
| Recoverable/auditable data | soft delete + views | physical deletes | measured: views prevent forgotten filters |
| Cross-request editing | optimistic locking (version column) | pessimistic locks | Ch. 48 measured the boundary |
| Before learning a new pattern | ask "is this still a problem in my language?" | copying GoF | half of them are already absorbed |

### The rule of thumb

```text
On seeing a pattern, ask three things:
  ① does it solve a【language gap】or【domain complexity】? → the former may already be absorbed
  ② does my language have a built-in counterpart?         → use it (shorter and safer)
  ③ will I actually use the flexibility it buys?          → if not, it is pure indirection
```

---

## 14. Best Practices

- **First ask "is this still a problem in my language?"**: measured, nine GoF patterns are built into Python; copying them only adds noise.
- **Prefer built-in mechanisms**: `event`, `yield return`, `@` decorators, magic static — shorter and safer (e.g. `event` restricts who can Invoke).
- **In C++, treat pattern choice as a performance decision**: measured templates 3.7× faster; use templates on hot paths and virtuals only when runtime swapping is needed.
- **In JIT languages, choose for readability**: measured 1.02× between interfaces and delegates — performance shouldn't enter the decision.
- **Use language mechanisms rather than hand-written synchronization for singletons**: measured, the static holder (Java) and magic static (C++11) are thread-safe and lock-free.
- **Separate "a single instance" from "global access"**: the former is legitimate; the latter hides dependencies — delegate to a container plus constructor injection (Chapter 55).
- **Recognize that every domain has its own pattern language**: soft deletes, optimistic locking, and event sourcing appear nowhere in GoF yet are equally patterns.
- **Treat patterns as vocabulary, not an implementation checklist**: their daily value is letting "use a closure table" replace ten whiteboard minutes.

---

## 15. Common Pitfalls

**Pitfall 1 · Using a pattern for its own sake**

```java
interface IUserService { }   // ⚠️ one implementation, and there will never be a second
class UserService implements IUserService { }
// ✅ use the class; extract an interface when replacement is actually needed (Ch. 55's criterion)
```

**Pitfall 2 · Hand-writing a lazy singleton**

```java
if (instance == null) instance = new Singleton();   // ⚠️ measured: 16 threads, 16 instances
// ✅ static holder / enum / container-managed
```

**Pitfall 3 · DCL without `volatile`**

```java
private static Singleton instance;    // ⚠️ without volatile: may return a half-built object
private static volatile Singleton instance;   // ✅
```

**Pitfall 4 · Observers that never unsubscribe (memory leak)**

```csharp
svc.OrderPlaced += handler;   // ⚠️ without -=, the publisher holds the subscriber forever (Ch. 36's GC)
// ✅ objects with lifecycles must unsubscribe on disposal
```

**Pitfall 5 · Stacking too many decorators**

```javascript
withLog(withRetry(withTimeout(withCache(fn))))   // ⚠️ four stack layers to read while debugging
// ✅ keep it to 2–3; one responsibility per layer
```

**Pitfall 6 · Forgetting the soft-delete filter**

```sql
SELECT * FROM users;                          -- ⚠️ returns deleted rows too
SELECT * FROM users WHERE deleted_at IS NULL; -- ✅ or query the view (the measured companion practice)
```

**Pitfall 7 · Using GoF names loosely**

```text
⚠️ "we use the factory pattern here" — simple factory, factory method, or abstract factory?
✅ a pattern's value is precise communication; the wrong name adds confusion
```

---

## 16. Interview Questions

**Basic**

1. What are a pattern's three elements? Why is the "name" one of them?
2. Name three GoF patterns that have "vanished" in your primary language and what replaced them.
3. What is wrong with the singleton pattern? What is the modern approach?

**Intermediate**

4. **Why is it said that half of GoF's patterns "simulate first-class functions"? Give two examples.**
5. What are C++'s three strategy implementations and their trade-offs?
6. **How does the observer pattern relate to Promises and the event loop?**

**Advanced**

7. **Why was double-checked locking wrong before Java 5? What does `volatile` do there?**
8. What problem does the visitor pattern solve, and why can pattern matching replace it? (Hint: the expression problem.)
9. Why are RAII and Pimpl absent from GoF? What does that tell you?

---

## 17. Exercises

**Basic**

1. Find a single-implementation interface in your project and judge whether it earns its keep.
2. Rewrite one strategy-pattern class hierarchy as a passed function; compare line count and readability.
3. List which GoF patterns your primary language has built in.

**Intermediate**

4. **Reproduce key experiment three**: write a lazy singleton and use threads to prove it creates multiple instances.
5. Implement a 30-line EventEmitter in your language and verify it matches the built-in behavior.
6. Rewrite a visitor implementation as pattern matching (if supported) and compare code size.

**Challenge**

7. **Reproduce key experiment two**: implement all three C++ strategy forms and measure the gap; then repeat the comparison in a JIT language.
8. Implement a minimal event-sourcing system (event table + replay + snapshot) and verify snapshot + incremental = full replay.
9. Pick a framework you use, identify the patterns inside it, and judge which are "patches for language gaps."

---

## 18. Chapter Summary

**One sentence**: design patterns are "a recurring problem + a proven solution + a name for communicating it," but GoF reflects the limitations of 1994's C++ and Smalltalk — and this chapter's six-language measurements show that **half its patterns are essentially simulations of first-class functions**: for the single requirement of "parameterize a behavior," the GoF version needs an interface plus three implementations plus a context (~25 lines), Python needs `sorted(items, key=len)` (zero extra structure), and Java 8 onward needs one lambda, all producing identical results; yet "passing a function" **costs differently by language** — C++'s three forms (virtual 1.33 ns / `std::function` 0.76 ns / template 0.36 ns, **templates 3.7× faster**) make pattern choice a **performance decision** there, while C#'s interface-versus-delegate measurement (9.76 vs 9.98 ns, **1.02×**) makes it purely a **readability decision** in JIT languages; the Java example also unearthed pattern history's most famous crash — the lazy singleton measured creating **16 instances** across 16 threads, and its standard fix, **double-checked locking, was wrong before Java 5** (the three steps of `instance = new Singleton()` can be reordered so another thread receives a half-built object, and `volatile`'s real job is **forbidding that reordering**) — yet language evolution absorbed that complexity, with Java's static holder and C++11's magic static both measured constructing once across 16 threads with zero locking; from this emerge patterns' **three fates**: absorbed (iterator → `yield return`, observer → `event`, visitor → pattern matching, proxy → built-in `Proxy`), still necessary (adapter/facade/repository — they organize **architectural boundaries**), and language-specific (RAII/Pimpl/CRTP — **none in GoF**); finally the SQL example reminds us **every domain grows its own pattern language** (soft deletes, optimistic locking measured as "0 rows affected," event sourcing measured replaying to 400), and a pattern's greatest daily value is often that third element — **letting "use a closure table" replace ten minutes at a whiteboard**.

**Key takeaways**

- **Half of GoF simulates first-class functions** (measured): strategy, command, template method, decorator — give a language lambdas and they vanish.
- **A pattern's weight is set by the language** (measured): C++'s 3.7× makes it performance, C#'s 1.02× makes it readability, Python removes the choice entirely.
- **The singleton's threading trap** (measured): lazy yields 16 instances; DCL needs `volatile` and was wrong pre-Java 5 due to reordering.
- **Language evolution absorbs pattern complexity** (measured): static holders and magic statics achieve thread safety with zero locking.
- **The singleton's real problem is global access**: it hides dependencies — the modern answer is container-managed lifetimes plus constructor injection (Chapter 55).
- **Patterns' three fates**: absorbed / still necessary (architectural boundaries) / language-specific (RAII, Pimpl).
- **Every domain has its own pattern language** (SQL measured): soft deletes, optimistic locking, event sourcing, closure tables.
- **A pattern's biggest cost is cognitive load**: an abstraction layer carrying no domain information is pure indirection.

**Checklist**

- [ ] I can name at least three GoF patterns my language has built in.
- [ ] I know the criteria for choosing an implementation differ between C++ and JIT languages.
- [ ] I can reproduce the lazy singleton's race and explain why DCL was once wrong.
- [ ] I judge a pattern by "language gap versus domain complexity."
- [ ] I treat patterns as vocabulary, not a checklist to fill.

**Next chapter**: patterns organize code better — but does it run fast enough? Chapter 57 covers **performance optimization**, a subject that fights intuition from beginning to end. We will measure how often "optimizing by feel" fails, quantify Chapter 16's cache locality on real data structures (identical algorithmic complexity, arrays an order of magnitude faster than linked lists), reproduce Chapter 33's allocation costs on a hot path, and above all deliver the central lesson: **measure first, optimize second** — every measurement method accumulated in the preceding chapters converges here into a complete performance-diagnosis workflow.

---

## 19. Further Reading

- <a href="https://en.wikipedia.org/wiki/Design_Patterns" target="_blank" rel="noopener">Wikipedia · Design Patterns (GoF)</a> — the full catalog of 23 patterns and their history.
- <a href="https://norvig.com/design-patterns/" target="_blank" rel="noopener">Peter Norvig · Design Patterns in Dynamic Languages</a> — "16 of 23 patterns are invisible or simpler in dynamic languages," this chapter's original source.
- <a href="https://www.cs.umd.edu/~pugh/java/memoryModel/DoubleCheckedLocking.html" target="_blank" rel="noopener">The "Double-Checked Locking is Broken" Declaration</a> — why DCL was wrong before Java 5, co-signed by the memory model's authors.
- <a href="https://docs.oracle.com/javase/specs/jls/se17/html/jls-12.html#jls-12.4.2" target="_blank" rel="noopener">JLS 12.4.2 · Class initialization</a> — the specification behind the static-holder singleton's thread safety.
- <a href="https://en.cppreference.com/w/cpp/language/storage_duration#Static_local_variables" target="_blank" rel="noopener">cppreference · Static local variables</a> — C++11's magic-static thread-safety guarantee.
- <a href="https://martinfowler.com/eaaCatalog/" target="_blank" rel="noopener">Patterns of Enterprise Application Architecture</a> — the enterprise domain's pattern language (source of Chapter 51's ORM patterns).
- <a href="https://martinfowler.com/eaaDev/EventSourcing.html" target="_blank" rel="noopener">Martin Fowler · Event Sourcing</a> — the pattern measured in the SQL example.
- <a href="https://en.wikipedia.org/wiki/Expression_problem" target="_blank" rel="noopener">Wikipedia · The expression problem</a> — the half-problem each of the visitor pattern and pattern matching solves.
- <a href="https://wiki.c2.com/?DesignPatternsConsideredHarmful" target="_blank" rel="noopener">C2 Wiki · Design Patterns Considered Harmful</a> — the classic compilation of critiques of pattern misuse.
