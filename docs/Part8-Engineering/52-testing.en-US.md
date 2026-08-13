# Chapter 52 · Testing

[简体中文](./52-testing.md) ｜ **English**

---

> The previous fifty-one chapters ran hundreds of "measured examples," each verifying a claim — which is, in fact, testing. This chapter formalizes it, answering the question every previous chapter assumed but never argued: **how do you know the code is correct?**
>
> The **key experiments** first reduce the test pyramid from dogma to a **cost structure**: verifying the same logic, a unit test (pure function) takes **0.7 μs**, an integration test (a real database) **665 μs — 1,022× slower**, and an end-to-end test (spawning a process) **10,157 μs — 15,608× slower**. A 1,000-test suite runs in 1 millisecond if all unit, 10 seconds if all end-to-end. The pyramid's shape is not aesthetics; it is the direct corollary of **each layer costing one to two orders of magnitude more**.
>
> Then, mock's two faces. The same test case — "checkout a zero-yuan order" — **passes** with a mock (57 μs, green) and is **rejected** by the real gateway, which enforces a rule the mock never heard of: "amount must be positive." **A mock tests what you believe the dependency does; an integration test tests what it actually does.** The C# example reproduced the lesson on another medium: a fake repository happily accepts the order ID `2026/08/001`, while the real file system fails — the slash is a path separator.
>
> Coverage misuse is also nailed by measurement: a VIP-discount function hides a bug (30% off written as 20% off), the tests exercise both branches — **100% line coverage**, all green, bug intact. Because the VIP branch's assertion was the weak `assert result > 0`. **Coverage measures "the code was executed," not "the behavior was verified."**
>
> Two hand-written experiments demystify the framework itself: Java builds a micro-JUnit with reflection (discovery, isolation, reporting — about 10 lines each) and measures **the shared-instance run failing t2 while fresh instances pass everything** — JUnit's instance-per-method buys determinism with isolation; C++ has no reflection, so tests are discovered by **static registration** (the `TEST` macro expands to a global object whose constructor runs before `main`) — the same conclusion as Chapter 51's ORM.
>
> The finest part is C++'s **property-based testing** in three acts: hand-picked cases all pass → 100,000 random inputs shatter `(a+b)/2` (**12,433 counterexamples**, integer overflow) → the textbook fix `a+(b-a)/2` **gets caught too** (12,600 counterexamples — `b-a` overflows for large opposite-sign values) → `std::midpoint` passes all 100,000. "Computing the midpoint of two integers" was hard enough to enter the standard library only in 2019 — **property tests catch even the bugs in your fixes**. The JS example measures the most dangerous class of test bug: the **async false pass** — an un-awaited assertion executes after the verdict, its failure swallowed, the test green forever.

## 1. Learning Objectives

By the end of this chapter, you will be able to:

- Explain the test pyramid as a **cost structure** (measured 1× / 1,022× / 15,608×), not recite it as dogma;
- State **mock's boundary**: it should isolate the "slow/expensive/unstable," not define the dependency's real behavior (two measured counterexamples);
- Explain **why coverage lies** (measured: 100% coverage + all green + a live bug) and fix it with strong assertions;
- Hand-write a test framework's three core jobs (discovery, isolation, reporting) and contrast Java's and C++'s discovery mechanisms;
- Use **property-based testing** to verify invariants (measured three-act bug hunt) and recognize the **async false pass** (measured swallowed failure).

---

## 2. Why This Concept Exists

### An assumption never argued

```text
Every "measured" example in the previous 51 chapters did the same thing:
run code → check output → compare with expectations
This book's run-all.sh is itself a test runner: 6 passed, 0 failed
→ testing is not a new concept — it is verification made【automated, repeatable, reported】
```

### The real cost of not testing

```text
In an untested codebase, the cost of "changing one line" is:
  manually clicking through every related feature (minutes to hours, every time)
  or — not clicking, and gambling on the release
→ testing's essence:【prepay verification once instead of paying per change】
→ the return: when refactoring, upgrading, or rearchitecting,
  a safety net tells you what you broke
```

### The one-sentence definition

```text
A test = an executable specification
         with two readers: the machine (pass/fail on every run)
         + the next person to change the code (reading it as behavior documentation)
```

> **In one sentence**: testing answers "how do you know the code is correct," and this chapter's measurements show the question has three traps — **fast tests can't detect real-world failures** (the fidelity boundary of mocks/fakes), **executed is not verified** (coverage and weak assertions), and **green can be fake** (async false passes). Writing tests is easy; writing **honest** tests takes everything in this chapter.

---

## 3. How It Works

### Key experiment one: the pyramid is a cost structure, not dogma

**Python measured all three layers** (verifying the same amount-parsing logic):

| Layer | Verification | Measured per test | Relative cost |
|-------|-------------|------------------|---------------|
| **Unit** | pure function in/out | **0.7 μs** | 1× |
| **Integration** | real sqlite: create → write → query → drop | **665 μs** | **1,022×** |
| **End-to-end** | spawn a full process | **10,157 μs** | **15,608×** |

```text
A 1,000-test suite:
  all unit:       1 ms   —— runnable on every file save
  all end-to-end: 10 s   —— barely runnable per commit; at 10,000 tests it's 100 s
→ the pyramid's shape (many unit, some integration, few E2E) is not an aesthetic preference
→ it is the corollary of feedback speed: slower tests run less often;
  tests that run less often let bugs live longer
```

**Each layer's responsibility** (none can replace another):

```text
Unit:        verifies【a piece of logic】— fast enough to run on every save
Integration: verifies【the contract with real dependencies】— where mock-hidden defects surface
End-to-end:  verifies【the whole system wired together】— most expensive; keep only critical paths
```

### Key experiment two: mock's two faces

**The setup**: the real payment gateway enforces "amount must be positive"; the business code `checkout()` has a bug — it never validates zero-amount orders.

**The mocked test** (measured):

```python
gw = Mock()
gw.charge.return_value = "ok"
assert checkout(gw, "0.00") == "ok"     # a zero-yuan order — the mock happily accepts
```

```text
mocked test "checkout zero-yuan order": PASSED ✓ (57 μs)
the real gateway, same case:            FAILED ✗ — gateway refused: amount must be positive
```

**The mock's green light hid a real defect**:

```text
A mock tests【what you believe the dependency does】;
an integration test tests【what it actually does】
→ mock's right use: isolating【slow/expensive/unstable】dependencies
  (the JS mock timer is the perfect example)
→ mock's wrong use: defining the dependency's behavior —
  your definition and reality will eventually diverge
```

**C# reproduced the same lesson on another medium** (measured):

```text
fake repository (Dictionary): order ID "2026/08/001" works fine ✓
the real file system:         the same case fails ✗ — the slash is a path separator!
→ a fake's fidelity boundary: it replicates【interface semantics】,
  not【the rules of the underlying medium】
→ the prescription: fakes power everyday fast feedback,
  plus a few integration tests against the real medium
```

**The double's speed dividend was measured too** (its reason to exist):

```text
2000 tests with a fake:        0.5 ms
2000 tests with real files:  160.1 ms (333× slower)
→ fast and real cannot coexist in one test — hence the layered pyramid
```

### Key experiment three: a bug under 100% coverage

**The setup**: `discount()` hides a bug (VIP should get 30% off, coded as 20%), and the tests **exercise both branches** — but the VIP branch's assertion is weak.

```python
def weak_tests():
    assert discount(100, False) == 95.0   # non-VIP: correct assertion
    result = discount(100, True)          # VIP: exercised, but —
    assert result > 0                     # ← weak assertion: any positive passes
```

**Measured** (coverage counted with the `trace` module):

```text
line coverage 100%: True
test results: all passing ✓
but the bug lives: discount(100, True) = 80.0 (should be 70.0)
```

```text
Coverage measures【the code was executed】, not【the behavior was verified】
→ weak assertions turn coverage into false confidence
→ make coverage a target and the team will produce "runs but verifies nothing" tests —
  Goodhart's law: when a measure becomes a target, it ceases to be a good measure
```

**A strong assertion catches it instantly** (measured via unittest):

```text
✗ test_vip_discount_correct: AssertionError: 80.0 != 70.0
```

### A test framework's three core jobs (hand-written, measured)

**The Java example implements JUnit's skeleton in ~10 lines each**:

```text
① discovery: reflection scanning for @Test (the same trick as Chapters 30/51)
② isolation: a fresh instance per test method
③ reporting: catch AssertionError, tally pass/fail — failures don't halt other tests
```

**Isolation's necessity, nailed by measurement**:

```text
shared instance (t1 runs first): ✓ t1_add_one_item   ✗ t2_cart_should_be_empty — expected 0, got 1
fresh instance per test:         ✓ ✓  2/2 passed
→ with a shared instance, t2's fate depends on【whether t1 ran first】— order coupling
→ JUnit's instance-per-method buys determinism with isolation
```

**One implementation detail is itself a lesson**: `getDeclaredMethods()` returns methods in **unspecified order** — the first version of this demo happened to run t2 first and silently proved nothing; only sorting by name made it reproducible. **Test infrastructure must itself be reproducible.**

### Key experiment four: property-based testing in three acts (C++)

```text
Act 1: hand-picked cases — average(4,6)=5, average(-3,3)=0, binary search at both ends… all pass ✓
Act 2: the property — "average(a,b) must lie within [min(a,b), max(a,b)]"
        100,000 random inputs → 12,433 counterexamples!
        first: average(-1144634710, -1024273349) = 1063029618 (outside the interval)
        root cause: a+b overflows int
Act 3: the textbook fix a+(b-a)/2 — still 12,600 counterexamples!
        counterexample: average(-2145984020, 505633457) — b-a overflows for opposite signs
Finale: std::midpoint — all 100,000 satisfied ✓
        midpoint was hard enough to enter C++20 only in 2019 (P0811)
```

```text
Hand-picked cases test【the inputs you thought of】;
property tests test【the ones you didn't】
→ the same overflow broke binary search on huge arrays —
  it lurked in the JDK for nine years (JDK-5045582)
→ property tests catch even【the bugs in your fixes】—
  their essential advantage over hand-picked cases
```

### Key experiment five: the async false pass (JS)

```javascript
test('bad: forgot await', () => {
  fetchBalance(1).then((balance) => {
    assert.strictEqual(balance, 100);   // will fail — but the test already "passed"
  });
  // the test function returns synchronously → verdict: PASS ✓
});
```

**Measured**:

```text
test 1 "bad": the framework reports【PASS】— but the assertion hasn't run yet
did the leaked assertion eventually run? leaked=1, and it actually failed:
"Expected values to be strictly equal:"
→ that failure【appeared in no test report】— a real bug buried under a green light
```

```text
Mechanism: the test function returns synchronously; the Promise's assertion
runs after the verdict (Chapter 43's event loop, cashing in)
→ these are the most dangerous tests: they are【green forever】—
  you could delete the code and nobody would notice
→ node:test diagnoses "asynchronous activity after the test ended" —
  frameworks try to catch this, but cannot cure it
→ the cure: async tests must await every asynchronous assertion
```

---

## 4. JavaScript

The JS example owns testing's most dangerous trap and its most elegant tool.

### The async false pass (measured — key experiment five)

**Why JS is especially prone**: three generations of async models coexist (callbacks, Promises, async — Chapter 42), and any "forgot to wait" produces a false pass. **The single criterion: when the assertion runs, is the framework still watching the test?**

### Mock timers: time becomes a controlled input (measured)

```javascript
mock.timers.enable({ apis: ['setTimeout'] });
const save = debounce((v) => calls.push(v), 1000);
save('a'); save('b'); save('c');            // three rapid calls
mock.timers.tick(1000);                     // ← advance the virtual clock 1 second
assert.deepStrictEqual(calls, ['c']);       // only the last call took effect
```

```text
The "wait 1000 ms" debounce test actually took: 0.06 ms (essentially zero real time)
→ mock.timers hijacks setTimeout: time becomes a【controlled input】, not a wait
→ this is mock's【right】use: isolating "slow," not isolating "real"
```

### Flakiness's two big sources: time and randomness (measured)

```javascript
const pickWinner = (users, rand) => users[Math.floor(rand() * users.length)];
pickWinner(['A', 'B', 'C'], () => 0.99)   // → 'C' (randomness injected as a constant)
```

```text
bad:  Math.random() / new Date() inline in business code — untestable
good: inject the random source and the clock as【parameters】—
      nondeterminism becomes a deterministic input
```

> **Note**: node 18+ ships `node:test` + `node:assert` — zero-dependency testing; Vitest/Jest add watch mode, snapshots, parallelism over the same skeleton; beware **snapshot testing's** "update all snapshots" button — it makes the weak-assertion problem stealthier (the coverage lesson, snapshot edition).

---

## 5. Python

The Python example hosts three key experiments (pyramid cost, mock's faces, lying coverage) — data in Section 3. Here are its engineering specifics.

### Reproducibility of the experiments themselves

```text
Pyramid: units averaged over 2000 runs, integration over 20, E2E over 5
→ the pricier the test, the smaller the sample — measurement follows the same cost structure
Coverage: real executed lines counted with the stdlib trace module,
→ the line number computed dynamically (discount.__code__.co_firstlineno) —
  the first version hard-coded it and got burned
```

### unittest's minimal usable form (measured, with a real failure report)

```python
class TestParseAmount(unittest.TestCase):
    def test_vip_discount_correct(self):
        self.assertEqual(discount(100, True), 70.0)   # strong assertion catches the bug
```

```text
ran 3 tests: 1 failure
  ✗ test_vip_discount_correct: AssertionError: 80.0 != 70.0
```

### The AAA structure

```text
Arrange → Act → Assert
def test_vip_gets_30_percent_off():        # the name states the expected behavior
    price = 100                            # Arrange
    result = discount(price, is_vip=True)  # Act
    assert result == 70.0                  # Assert
→ one behavior per test: a failure tells you【which rule】broke,
  without debugging the test itself
```

> **Note**: pytest is the community standard (bare `assert` + fixtures + parametrization); `unittest.mock.Mock` accepts any call — `spec=RealGateway` makes it reject nonexistent methods, mitigating mock-reality drift; use `coverage.py` for coverage, remembering this chapter's measurement: **it cannot verify assertion strength**.

---

## 6. Java

The Java example hand-writes a micro-JUnit — reducing "the framework" to three ten-line passages.

### The three core jobs (measured, working)

```java
// ① discovery
for (Method m : methods)
    if (!m.isAnnotationPresent(Test.class)) continue;
// ② isolation
Object instance = freshInstance ? testClass.getDeclaredConstructor().newInstance() : shared;
// ③ reporting
catch (InvocationTargetException e) {
    results.add(new TestResult(m.getName(), false, e.getCause().getMessage()));
}
```

```text
CartTest: ✓ empty_cart_totals_zero  ✓ add_two_items  ✗ deliberately_failing — expected 60, got 50
          2/3 passed
→ the failure did not halt the other tests — every test succeeds or fails independently
```

### The isolation experiment (measured)

```text
shared instance: 1/2 passed (t2 failed — it saw the cart t1 left behind)
fresh instances: 2/2 passed
```

**But isolation is neither free nor omnipotent**:

```text
not free:  new instance + @Before per test — share expensive setup via @BeforeAll
not omnipotent: static fields, singletons, files, databases —
                state【outside the instance】is not isolated
→ the pollution-hunting routine: run the failing test alone —
  passes alone, fails together = something polluted shared state
```

### Test naming: a failure should read like a requirement

```text
bad:  test1 / testCart / testTotal   ← you must read code to know what broke
good: empty_cart_totals_zero / vip_gets_30_percent_off
      ← the failure list is itself a list of broken requirements
```

> **Note**: JUnit 5 uses `@Test`/`@BeforeEach`/`@AfterEach` — this example is its skeleton; AssertJ's fluent assertions give better failure messages; Testcontainers runs real databases in Docker for the integration layer — its raison d'être is exactly this chapter's measured "mocks hide real defects."

---

## 7. C++

The C++ example answers two questions: how tests are discovered without reflection, and why property testing is powerful.

### Test discovery: the static registrar (a 30-line framework)

```cpp
struct Registrar {                            // global objects construct before main
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({name, fn}); }
};

#define TEST(name)                                                  \
    static void test_##name();                                      \
    static Registrar reg_##name(#name, test_##name);  /* auto-register */ \
    static void test_##name()
```

```text
Java scans @Test via reflection; a C++ class doesn't know its own methods
→ the TEST macro expands to a global Registrar whose constructor,
  running before main, pushes the test into the registry
→ Catch2's TEST_CASE and GoogleTest's TEST work exactly this way
→ the same conclusion as Chapter 51's ORM: no reflection → macros or codegen
```

### Property testing in three acts (measured — key experiment four)

```text
hand-picked 4/4 pass → (a+b)/2 shattered by 12,433 counterexamples
→ a+(b-a)/2 shattered by 12,600 → std::midpoint passes 100,000/100,000
```

### What good invariants look like

```text
sorting:   output ordered + same length + same multiset (all three needed)
codecs:    decode(encode(x)) == x (round-trip — serialization/compression/escaping)
inverses:  apply(undo(op)) == original (Chapter 48's undo log correctness is exactly this)
idempotence: f(f(x)) == f(x) (dedup, normalization, retry safety — measured in SQL)
→ invariants are【cheaper】than cases: one property outweighs hundreds of assertions
```

> **Note**: the signed overflow here is **undefined behavior** — RapidCheck is the industrial property-testing tool (it automatically **shrinks** counterexamples); `-fsanitize=address,undefined` is another kind of "test": UBSan catches `(a+b)/2`'s overflow at runtime. **C++'s specialty: some bugs (UB) are invisible to test frameworks and need compiler instrumentation.**

---

## 8. C#

The C# example splits the muddled word "mock" into three distinct faces.

### What each double verifies (measured)

| Double | What it is | The question it answers | In this example |
|--------|-----------|------------------------|-----------------|
| **stub** | fixed return values | "given the dependency returns X, is my logic right?" | `StubRepo.Find` always returns 9999 |
| **mock** | records interactions | "did I call the dependency **correctly**?" | `MockNotifier.Sent` list |
| **fake** | a working lightweight implementation | "does what I store **come back intact**?" | `FakeRepo` (Dictionary) |

```text
measured: large order sends notification ✓ (mock verified 1 message containing 200.00)
          small order sends none        ✓ (mock verified 0)
          saved order reads back        ✓ (fake verified the round trip)
→ the terms get lumped together as "mock," but the wrong type
  leaves your test【verifying something other than what you intended】
```

### The bug a fake cannot catch (measured — key experiment two)

```text
fake (Dictionary): order ID "2026/08/001" works ✓
real file system:  the same case fails ✗ — the slash is a path separator
→ a fake's fidelity boundary: interface semantics yes, medium rules no
```

### Testability comes from design

```text
OrderService depends on【interfaces】IOrderRepo/INotifier, not concrete classes
→ that is why doubles【can be swapped in】— dependency injection (Chapter 55's protagonist)
→ the anti-pattern: new FileRepo() inside a method, or static calls →
  nothing can be swapped → untestable
→ "hard to test" is almost always a design smell: hard-wired dependencies,
  side effects mixed into logic, global state
```

> **Note**: the .NET stack is xUnit (framework) + Moq/NSubstitute (mocking) + coverlet (coverage); xUnit creates a fresh test-class instance per test — the same isolation strategy Java measured; `[Theory]` + `[InlineData]` parametrizes one test over many data rows.

---

## 9. SQL

The database world has three kinds of "assertions" of its own.

### Constraints = always-on assertions (measured)

```text
inserting a -50-cent order: changes=0 (blocked by CHECK)
inserting status 'flying':  changes=0 (blocked by the enum CHECK)
→ a unit test runs once; a constraint runs【on every write】— the data layer's last line of defense
→ application-level validation can be bypassed (new code, manual SQL, another service);
  constraints cannot
```

### Assertion queries: select the violations, zero rows = pass (measured)

```sql
-- write the business rule as a "find violations" query
SELECT COUNT(*) FROM orders WHERE status NOT IN ('paid','shipped','done','cancelled');
```

```text
rule A "no orphan statuses": 0 violations → PASS ✓
→ this is how dbt tests work; these run against【production data】
→ unit tests test code; data tests test data — different subjects under test
```

### Rollback isolation: the standard posture for database tests (measured)

```text
orders before the test: 2
BEGIN → create fixtures + run the operation under test → 3 rows inside, order 1 = done
ROLLBACK → 2 rows, order 1 = paid (untouched to the cent)
→ one transaction per test, ROLLBACK at the end — no cleanup code, no cross-test pollution
→ Django/Rails/Spring database tests do exactly this by default
→ the boundary: it breaks when the code under test COMMITs or opens its own connection
```

### Idempotence: the data side's most test-worthy property (measured)

```text
UPSERT set-to-value executed twice: qty = 8 (idempotent ✓ — retry-safe)
contrast: the accumulate form qty = qty - 2 gives 6 on repeat (retry-unsafe)
→ message redelivery and API retries are everywhere (Ch. 48's retry logic) —
  idempotence is a must-test property
```

> **Note**: the fixture dilemma — hand-built data is controlled and stable but misses real data's oddities, while anonymized production copies are realistic but slow and irreproducible; the usual split is hand-built for unit/integration and assertion queries on production; **migration testing** (rehearsing new schemas on production-data copies) is the most-skipped and most painful kind.

---

## 10. Cross-Language Comparison

### ① Testing facilities

| Capability | JavaScript | Python | Java | C++ | C# |
|-----------|-----------|--------|------|-----|-----|
| Built-in test facility | ✅ `node:test` (18+) | ✅ `unittest` | ❌ (JUnit) | ❌ (only `assert`) | ❌ (xUnit) |
| Discovery mechanism | file conventions | naming conventions | **reflection on annotations** | **static-registration macros** | reflection on attributes |
| De-facto framework | Vitest / Jest | pytest | JUnit 5 | Catch2 / GoogleTest | xUnit |
| Mocking library | built-in `mock` | built-in `unittest.mock` | Mockito | hand-rolled/FakeIt | Moq / NSubstitute |
| Mock timers | ✅ `mock.timers` (measured) | `freezegun` | — | — | — |
| Property testing | fast-check | Hypothesis | jqwik | RapidCheck | FsCheck |
| Signature trap | **async false pass** (measured) | fixture scoping | static state pierces isolation | **UB invisible to tests** (needs sanitizers) | same as Java |

### ② Key experiment data summary

```text
pyramid cost (Python):  unit 0.7 μs → integration 665 μs (1022×) → E2E 10157 μs (15608×)
mock's faces (Python):  mocked zero-yuan order passes ✓ (57 μs); the real gateway rejects ✗
fake boundary (C#):     slashed order ID passes on fake, fails on the real file system; speed 333×
lying coverage (Python): 100% lines + all green + live bug (80.0 ≠ 70.0)
isolation (Java):        shared instance 1/2, fresh instances 2/2
property tests (C++):    picked 4/4 pass → original bug 12,433 CEs → fixed bug 12,600 CEs → midpoint clean
false pass (JS):         leaked=1 assertion failed into the void — no report saw it
rollback isolation (SQL): 3 rows inside the transaction → 2 rows after ROLLBACK, untouched
```

### ③ The discovery divide (isomorphic to Chapter 51)

```text
Java/C#: runtime reflection over annotations/attributes — classes know their own shape
Python:  naming conventions + introspection — the dynamic language's natural path
JS:      file conventions + function registration — the test() call itself registers
C++:     static-registration macros — global constructors substitute for reflection
→ Chapter 51's ORM mapping and this chapter's test discovery:
  two projections of the same language-feature difference
```

### ④ Common ground and root causes

**Common ground**: every framework does the same three jobs (discovery, isolation, reporting — hand-written in ~10 lines each in Java); every ecosystem needs both fast doubles and real integrations (measured: neither substitutes for the other); coverage tools in every language fail to measure assertion strength.

**Root causes**:

- **The pyramid's shape is physics**: process startup, disk I/O, and network round trips (measured in Chapters 39/46) directly price each layer;
- **Mock risk is structural**: the double and the real implementation are **two pieces of code** with no mechanism forcing them to agree — drift is only a matter of time;
- **Coverage lies because all metrics do**: it measures execution, not verification, and Goodhart's law guarantees a targeted metric degrades;
- **The async false pass weighs heaviest on JS** because its test-function boundary and async-completion boundary are naturally separate (Chapter 43's event loop);
- **The discovery divide** mirrors Chapter 51's ORM divide exactly — runtime introspection shapes the framework.

---

## 11. Implementation Comparison

| Framework | Discovery | Isolation | Reporting |
|-----------|----------|-----------|-----------|
| **JUnit 5** | reflection on `@Test` | fresh instance per method | TestEngine event stream |
| **pytest** | `test_*` naming + introspection | fixture scope management | **assertion rewriting** (AST) |
| **node:test** | `test()` call registers | subtest tree | TAP protocol output (as measured) |
| **Catch2** | `TEST_CASE` static registration | section re-entry | expression decomposition (`REQUIRE(a==b)` prints both values) |
| **xUnit** | reflection on `[Fact]`/`[Theory]` | fresh class instance per test | parallel by default |

**pytest's assertion rewriting deserves a note**:

```text
A bare `assert a == b` normally throws a detail-free AssertionError
pytest rewrites the test file's AST at import time so the assert reports both sides
→ that is why pytest failures read "assert 80.0 == 70.0" instead of a bare error
→ Chapter 30's "modify code at runtime," most successfully applied to test tooling
```

---

## 12. Performance Analysis

### This chapter's numbers at a glance

```text
pyramid:      unit 0.7 μs / integration 665 μs (1022×) / E2E 10157 μs (15608×)
extrapolated: 1000 tests — all-unit 1 ms, all-E2E 10 s
double dividend: fake vs real files 333× (C#); mock timers turn 1 s into 0.06 ms (JS)
property tests: 100,000 inputs in a few hundred ms — far cheaper than hundreds of cases
coverage:     100% line coverage coexisting with a live bug (weak assertion)
```

### Suite performance is team feedback speed

```text
10-second suite:  runs on every save → bugs live under a minute
10-minute suite:  runs per commit → bugs live hours; context is cold by fix time
1-hour suite:     runs daily → bugs live days; archaeology required
→ "slow tests" is not a comfort issue — it is a【defect lifetime】issue
→ optimization order: push tests down the pyramid → replace slow dependencies
  with doubles → parallelize → run only affected tests
```

> ⚠️ **Flaky tests are more toxic than slow ones.** One test that fails randomly 1% of the time drops a 100-test suite's all-green probability to 37% — the team learns to "just rerun it," and then **real failures get rerun away too**. This chapter gave injection cures for the two big sources (time, randomness) and a hunting routine for the third (cross-test pollution).

---

## 13. Engineering Practice

| Scenario | ✅ Recommended | ❌ Avoid | Why |
|----------|--------------|---------|-----|
| Layering | many unit + some integration + few E2E | everything E2E | measured 1–2 orders of magnitude per layer |
| What to mock | slow/expensive/unstable dependencies | mocking everything | measured: mock hid the gateway rule |
| Beyond doubles | keep a few real-medium integration tests | fakes only | measured: only the real FS caught the slash |
| Assertions | exact expected values | `> 0`/`not null` weak forms | measured: weak assertions defeat 100% coverage |
| Coverage | as a **detector** (find untested areas) | as a KPI | Goodhart + measurement |
| Async tests | await every async assertion | fire-and-forget | measured false pass, swallowed failure |
| Time/randomness | inject clocks and random sources | direct `Date.now`/`random` | the two big flakiness sources |
| Algorithmic invariants | property tests | hand-picked cases only | measured: caught two layers of bugs picked cases missed |
| Database tests | transaction-rollback isolation | hand-written cleanup | measured: untouched after ROLLBACK |
| Data quality | assertion queries on production | testing only code | data goes bad too |
| Test naming | behavior-describing sentences | test1/test2 | the failure list = broken requirements |
| Order-coupling hunt | run the failing test alone | blind reruns | passes alone, fails together = pollution |

### The rule of thumb

```text
Before writing a test, ask three things:
  ① Does it verify behavior or mere execution?   → assertions must pin exact values
  ② Does the dependency's realness matter?       → if yes, don't mock; go integration
  ③ Can time/randomness/order destabilize it?    → if yes, inject and isolate
Maintaining the suite, watch one thing: feedback speed —
sink slow tests down the pyramid; fix or delete flaky ones immediately
```

---

## 14. Best Practices

- **Layer by cost**: measured unit : integration : E2E = 1 : 1,022 : 15,608 — write many cheap ones, few expensive ones.
- **Mock the "slow/expensive/unstable," never substitute for "real"**: measured, a mock and a fake each hid a real defect; every double-covered dependency needs at least one real-path integration test.
- **Assert exact values**: measured, `assert result > 0` rendered 100% coverage meaningless; write `assert result == 70.0`.
- **Coverage as detector, not KPI**: it shows where you **haven't** tested, never how **well** you tested.
- **Async assertions must be awaited**: measured, an un-awaited failure was swallowed and the test stayed green — no fire-and-forget in async tests.
- **Inject time and randomness, always**: measured, mock timers turned a 1-second wait into 0.06 ms; injected randomness made results reproducible.
- **Use property tests wherever invariants exist**: measured, they caught even the fix's bug — sorting, codecs, idempotence, and inverses are ready-made properties.
- **Isolate tests, and know isolation's limits**: measured, a shared instance caused order coupling; state outside the instance (static/files/databases) needs its own handling (databases: transaction rollback, measured clean).

---

## 15. Common Pitfalls

**Pitfall 1 · Nobody awaits the async assertion (JS's most common)**

```javascript
test('...', () => {
  fetchBalance(1).then(b => assert.equal(b, 100));   // ⚠️ measured: swallowed, forever green
});
// ✅ test('...', async () => { assert.equal(await fetchBalance(1), 100); });
```

**Pitfall 2 · Weak assertions propping up coverage**

```python
result = discount(100, True)
assert result > 0            # ⚠️ measured: 100% coverage, bug intact
assert result == 70.0        # ✅
```

**Pitfall 3 · The mock defines the dependency's behavior**

```python
gw.charge.return_value = "ok"    # ⚠️ the real gateway rejects zero — the mock doesn't know
# ✅ alongside mocks, keep one integration test against the real gateway
```

**Pitfall 4 · Shared state between tests**

```java
static List<Order> orders = ...;   // ⚠️ measured: t2's fate depends on whether t1 ran first
// ✅ fresh instance per test (framework default) + reset static state in @BeforeEach
```

**Pitfall 5 · Sleeping to wait for async completion**

```javascript
await sleep(500); assert(...)   // ⚠️ flaky on slow machines, wasteful on fast ones
// ✅ mock timers (measured 0.06 ms) or wait on an explicit completion signal
```

**Pitfall 6 · Database tests that don't clean up**

```sql
-- ⚠️ fixtures linger → the next test's COUNTs are all wrong
-- ✅ BEGIN → test → ROLLBACK (measured: untouched)
```

**Pitfall 7 · Testing only the happy path**

```text
⚠️ hand-picked cases all passing ≠ no bugs — measured: (a+b)/2 hid 12,433 counterexamples
✅ boundary values (0/negative/empty/overflow) + property-test bombardment
```

---

## 16. Interview Questions

**Basic**

1. Why is the test pyramid pyramid-shaped? Argue with cost data.
2. How do stubs, mocks, and fakes differ? What does each verify?
3. What is the AAA structure? Why one behavior per test?

**Intermediate**

4. **What are mock's right and wrong uses? Give an example of a mock hiding a real defect.**
5. What does 100% coverage guarantee, and what doesn't it? (Answer with the weak-assertion example.)
6. **How does JS's async false pass happen? How do you prevent it?**

**Advanced**

7. **What is property testing's essential advantage over hand-picked cases? Give an example where the fix itself had a bug that got caught.**
8. What are a test framework's three core jobs? Why do Java's and C++'s discovery mechanisms differ?
9. What are flakiness's three main sources, and the cure for each?

---

## 17. Exercises

**Basic**

1. Measure one unit test and one integration test in your project; compute the ratio.
2. Find a test that calls without asserting (or asserts weakly) and give it exact assertions.
3. Rewrite a "tests three things at once" test into AAA form.

**Intermediate**

4. **Reproduce key experiment two**: add one real integration test behind a mocked dependency and see whether it finds new defects.
5. Reproduce the coverage experiment: write a 100%-covered module with a bug, then catch it with strong assertions.
6. Hand-write a 30-line test runner (reflection or static registration) with discovery, isolation, and reporting.

**Challenge**

7. **Reproduce the three-act property test**: bombard `(a+b)/2` with random inputs, verify the textbook fix also has counterexamples, then verify the standard-library version.
8. Find every "assertion inside .then without return/await" test in your JS project (write a lint rule) and count the false passes.
9. Write a mock-timer test for a time-dependent feature (debounce/retry/timeout) and compare its runtime with the real-wait version.

---

## 18. Chapter Summary

**One sentence**: a test is an **executable specification**, and this chapter's five key experiments quantify all three traps in "how do you know the code is correct" — **fast tests can't detect real-world failures** (the measured 1×/1,022×/15,608× cost structure forces layering; a mocked zero-yuan order stayed green while the real gateway rejected it, and a fake accepted the slashed order ID that the real file system refused — doubles have fidelity boundaries), **executed is not verified** (measured: 100% line coverage, all tests green, and the VIP-discount bug intact — weak assertions turn coverage into false confidence, and making it a KPI triggers Goodhart's law), and **green can be fake** (JS measured the un-awaited assertion executing after the verdict, its failure swallowed — such tests stay green forever); the framework itself was demystified by hand — Java implemented discovery/isolation/reporting in ~30 lines of reflection (and measured shared instances causing order coupling: 1/2 vs 2/2), while C++, lacking reflection, uses static-registration macros (isomorphic to Chapter 51's ORM); and **property-based testing's** three acts (picked cases pass → `(a+b)/2` shattered by 12,433 counterexamples → the textbook fix shattered by 12,600 more → `std::midpoint` clean) prove it catches even the bugs in your fixes — computing a midpoint was hard enough to enter the standard library only in 2019.

**Key takeaways**

- **Pyramid = cost structure** (measured): 0.7 μs / 665 μs / 10,157 μs — layering is a corollary of feedback speed, not dogma.
- **Mock's two faces** (measured): right use is isolating slow/expensive/unstable (mock timers: 1 s → 0.06 ms); wrong use is defining dependency behavior (two counterexamples).
- **Coverage lies** (measured): 100% coverage coexists with bugs; it measures execution, not verification.
- **The framework's three jobs** (hand-written): discovery (reflection/static registration), isolation (fresh instance, measured 1/2 vs 2/2), reporting (failures don't halt).
- **Property testing** (measured, three acts): invariants are cheaper than cases and catch even fixed-in bugs.
- **The async false pass** (measured): assertion after verdict → swallowed failure → forever green; await everything.
- **The database's three assertions** (measured): constraints (run on every write), assertion queries (zero rows = pass), rollback isolation (untouched).
- **Flakiness's three sources**: time, randomness (cured by injection), cross-test pollution (cured by isolation and the hunting routine).

**Checklist**

- [ ] I can explain the pyramid's layers with measured cost data.
- [ ] I know mock's boundary, and every double has a real-path integration test behind it.
- [ ] My assertions pin exact values; I don't take comfort in coverage numbers.
- [ ] My async tests contain no fire-and-forget assertions.
- [ ] I can write property tests for code with invariants.

**Next chapter**: tests guarantee your code's correctness — but your code depends on dozens or hundreds of packages written by others, and who manages their versions? Chapter 53 covers **package management**: why "dependency hell" exists, semantic versioning's promise and its lies, what a lockfile actually locks, how five ecosystems (pip/npm/Maven/NuGet/vcpkg) resolve version conflicts differently (npm lets multiple versions of one package coexist while Maven keeps exactly one — a difference with far larger consequences than it sounds), and why supply-chain attacks turned "installing a package" into a security decision.

---

## 19. Further Reading

- <a href="https://martinfowler.com/articles/practical-test-pyramid.html" target="_blank" rel="noopener">Martin Fowler · The Practical Test Pyramid</a> — the full treatment of the pyramid; the systematic version of experiment ①.
- <a href="https://martinfowler.com/articles/mocksArentStubs.html" target="_blank" rel="noopener">Fowler · Mocks Aren't Stubs</a> — the original taxonomy of test doubles (the three faces the C# example implements).
- <a href="https://abseil.io/resources/swe-book/html/ch11.html" target="_blank" rel="noopener">Software Engineering at Google · Testing Overview</a> — testing at scale, with the small/medium/large layering.
- <a href="https://hypothesis.readthedocs.io/" target="_blank" rel="noopener">Hypothesis documentation</a> — Python's de-facto property-testing tool, with counterexample shrinking.
- <a href="https://nodejs.org/api/test.html" target="_blank" rel="noopener">Node.js Docs · node:test</a> — the built-in runner used in this chapter's JS measurements (including mock.timers).
- <a href="https://docs.pytest.org/en/stable/how-to/assert.html" target="_blank" rel="noopener">pytest · assertion rewriting</a> — why bare asserts get good failure messages.
- <a href="https://junit.org/junit5/docs/current/user-guide/" target="_blank" rel="noopener">JUnit 5 User Guide</a> — the complete form of the Java example's hand-written skeleton.
- <a href="https://en.wikipedia.org/wiki/Goodhart%27s_law" target="_blank" rel="noopener">Wikipedia · Goodhart's Law</a> — "when a measure becomes a target" — the coverage lesson's theoretical name.
- <a href="https://research.google/pubs/pub45880/" target="_blank" rel="noopener">Google · Flaky Tests study</a> — scale data and remediation experience for flaky tests.
