# Chapter 58 · Security

[简体中文](./58-security.md) ｜ **English**

---

> Chapter 57 taught you to make programs fast. This chapter's point is that **a fast wrong program just fails faster**. And security bugs are a special class of error — they **don't crash your program, don't turn tests red, and leave nothing unusual in monitoring**. They sit quietly until someone comes looking.
>
> This chapter's **key experiment** differs by a single line. The same login query returns 1 row for the input `alice`, and **3 rows — the entire users table** — for `alice' OR '1'='1`. Switched to a parameterized query, the same malicious input returns **0 rows**, and the SQL string executed both times is **byte-for-byte identical**. That contrast reveals what parameterization actually means: it **isn't escaping quotes for you** — it puts the statement and the data on **two separate channels**. The statement is parsed into a plan first; the parameter is filled in afterward. It never gets a chance to become syntax.
>
> Which raises: "can't I just escape it myself?" This chapter measures a real bypass. The defenders ran every input through `escape_quotes()` — but another query in the same project used a value that **wasn't wrapped in quotes** (a numeric column), so the input `1 OR 1=1` was **completely unchanged** by escaping and dragged out the whole table. **Escaping presupposes that the value is quoted, and that premise is written down nowhere.**
>
> XSS is worse, because "escaping" here isn't one action but **six different ones**. This chapter runs a single `escapeHtml` against three contexts: in HTML text it **✅ works**; in an unquoted attribute it **❌ fails** (the payload `a onmouseover=alert(1)` contains nothing that needs escaping); in an event-handler attribute it **❌ fails** most insidiously — the browser **decodes `&#39;` back to `'`** when parsing the attribute value, before handing it to the JS engine, so escaping actively *created* the hole. **One function, three contexts, three outcomes.**
>
> Then comes the book's only "slower is better" design. SHA-256 runs at **3.66 million/sec** on one core; PBKDF2 at 600,000 rounds manages **6.4/sec** — a **568,411x** difference in guessing rate (Java measured 2,334,582x). Password hashing is **deliberately slow**, because once an attacker has your database, their only cost is the price of each guess. A ten-million-entry common-password dictionary finishes in seconds with SHA-256 and takes weeks with PBKDF2.
>
> Timing attacks then invert Chapter 57's instincts entirely. Comparing a token byte by byte, **differing at byte 1 takes 18.5 ms; differing at byte 32 takes 237.4 ms — 12.9x**. That gap is itself an information channel, cutting a 32-byte token's brute-force space from 256³² to **8192 attempts**. Constant-time comparison flattens it to **0.99x**, at the price of being slower in absolute terms (453 ms vs 18 ms): **it always does the full work and refuses to return early.**
>
> Finally, memory safety. The C++ example measures an integer overflow: requesting 1.07 billion 4-byte elements, the 32-bit multiply wraps to **4 bytes** — the program believes it has 4 GB. And the argument that "C++ skips bounds checks for performance" largely no longer holds: `at()` versus `[]` costs **2.21x at -O0, 1.05x at -O1, and 1.02x at -O2**. **As optimizers got stronger, the price of safety collapsed.**
>
> In one sentence: **every injection vulnerability has the same root cause — untrusted data handed to something that parses it — and every defense has the same shape: don't build strings, use structured interfaces.**

## 1. Learning Objectives

After this chapter you will be able to:

- Explain the **real root cause** of SQL injection, and why parameterized queries are not "automatic escaping";
- Explain why **manual escaping almost inevitably fails**, and reproduce a real bypass;
- Distinguish the **multiple contexts** of XSS and choose the correct encoding for each;
- Explain why password hashing is **deliberately slow**, and quantify the gap against general-purpose hashes;
- Recognize the cause of **timing attacks** and know when constant-time comparison is mandatory;
- Articulate which class of vulnerabilities **memory safety** eliminates, which it doesn't, and what bounds checking actually costs today;
- Evaluate a system with **defense in depth**: what remains after the first line fails.

---

## 2. Why This Concept Exists

### 2.1 Three Ways Security Bugs Differ from Ordinary Bugs

| | Ordinary bug | Security bug |
|---|---|---|
| **Who triggers it** | A user, by accident | **Someone actively hunting** |
| **Input distribution** | Normal business data | **Deliberately crafted edge cases** |
| **What failure looks like** | Error, wrong result | **Everything looks fine** (that *is* what success looks like) |

The third row is the fundamental one. A functional bug announces itself: blank page, HTTP 500, wrong totals. A SQL injection hole passes **100% of functional tests**, because under normal input it works perfectly.

The C# examples make this bluntest: ECB-mode encryption **raises no error**, decrypts **perfectly**, and round-trip tests are **all green** — it merely leaks the plaintext's structure verbatim. **Cryptographic mistakes don't crash your program; they just make it quietly insecure.**

### 2.2 One Root Cause, Many Names

The vulnerabilities in this chapter look varied, but the whole injection family has **one root cause**:

```
SQL injection   : data spliced into the input of a SQL parser
XSS             : data spliced into the input of an HTML parser
Command injection: data spliced into the input of a shell parser
Path traversal  : data spliced into the input of a path parser
Deserialization : data handed to a restorer that CALLS METHODS
Template injection: data spliced into the input of a template engine
```

**Untrusted data handed to something that parses it.**

So the defense has one shape too: **don't build strings, use structured interfaces.**

```
SQL      → parameterized queries (statement and data on separate channels)
HTML     → textContent / setAttribute (DOM API, no HTML parser involved)
Shell    → execve(["cmd", arg1, arg2]) (array form, no shell involved)
Paths    → normalize then verify the prefix, or use a lookup table with no user input
Deserialization → plain data formats like JSON
```

This isomorphism is not a coincidence. It's the most valuable idea to take from this chapter: **when you find yourself building a string that will be parsed by something, you are creating an injection vulnerability.**

### 2.3 Why "Filter the Input" Is the Wrong Mental Model

Many people's first instinct is "strip dangerous characters at the entrance." Three fatal problems:

**① The dangerous character set depends on the output location, and the entrance doesn't know where output goes.** The same username is dangerous with `'` in SQL, `<` in HTML, `;` in a shell, `(` in LDAP. Entrance filtering either misses one or destroys legitimate content (a user named O'Brien can never register).

**② Data gets used many times.** A username stored in the database may later appear in an HTML page, a JSON API, a log file, a CSV export, and an email body — five contexts, five rule sets.

**③ Filtering is a blacklist, and blacklists are never complete.**

The correct model is: **validate on input** (is this legitimate business data?) and **encode on output** (per the target context). Two different things at two different times with two different rule sets.

### 2.4 Defense in Depth: Assume the First Line Fails

Security design's premise isn't "nothing will go wrong" — it's **"how bad is it when something does."**

The SQL examples are entirely about this: assume the application's parameterization failed. How much can the attacker do? Entirely determined by **the privileges of that database connection**. Connected as a superuser? Read every table, DROP things, possibly execute commands. Connected as a least-privilege account? Only the granted columns of the granted tables — `pw_hash` isn't even in the view.

The same thinking runs through the chapter:

| First line | Second line, after it fails |
|---|---|
| Parameterized queries | DB least privilege, views, RLS |
| Output encoding | CSP (inline scripts don't run), HttpOnly (cookies can't be stolen) |
| Password hashing | Salting (rainbow tables die), slow hashing (brute force crawls) |
| Memory-safe coding | ASan/UBSan, stack protector, ASLR |
| Authorization checks | Audit logs (afterward, you can answer "what happened") |

---

## 3. How It Works Underneath

### 3.1 The Injection Mechanism: One Parse, Two Sources

Understanding injection means seeing the world as the parser sees it.

```
What you think:  SELECT ... WHERE name = [a string value]
                                          ↑ user data

What the parser sees: SELECT ... WHERE name = 'alice' OR '1'='1'
                      └────── one string of SQL to parse ──────┘
```

The parser **has no way** to distinguish what you wrote from what the user supplied — it sees one character stream. "Injection" is user data **crossing the boundary between data and code**.

Parameterized queries eliminate the boundary problem mechanically:

```
① Client sends:  SELECT id FROM users WHERE name = ?
   Database:     parse → build an execution plan (? is a PARAMETER SLOT)
② Client sends:  parameter value = "alice' OR '1'='1"
   Database:     put the value INTO the slot — parsing finished long ago
```

Step two involves no parsing at all. That's why parameterization "needs no escaping": **there is no string that needs correct escaping, because no string was ever built.**

Bonus: the plan can be reused (Chapter 49), so **parameterization is typically faster too**.

### 3.2 Why Escaping Always Loses

Escaping's correctness depends on a chain of **unwritten premises**:

1. The value is wrapped in quotes (this chapter's measured bypass hits exactly here);
2. The escaping rules match this database's dialect;
3. The character set matches the database (historically MySQL's GBK encoding broke `addslashes()` entirely — `0x5c` got swallowed as the second byte of a multibyte character, and the backslash vanished);
4. Escaping happens exactly once (one too many or too few is wrong);
5. **Every single site** in the project remembers to call it.

Point 5 is especially fatal: escaping is a defense that must be **correct everywhere**, while an attacker needs to find **one** omission. Parameterization is a defense that is **correct by default**.

> This is a recurring principle in security engineering: **prefer mechanisms that are safe by default over mechanisms that are safe when used correctly.**

### 3.3 XSS: Six Contexts, Six Rules

A browser isn't one parser — it's a **chain** of them: HTML parser → attribute-value decoding → JS parser / CSS parser / URL parser. Whichever layer the data lands in dictates the encoding.

```
<div>       user data </div>          ← HTML text: entity-encode &<>"'
<div class="user data">   </div>      ← attribute: entity-encode + MUST BE QUOTED
<div class=user data>     </div>      ← unquoted attribute: CANNOT be safely encoded
<a href="user data">                  ← URL: entity-encode + PROTOCOL ALLOWLIST
<a onclick="f('user data')">          ← event handler: JS-encode; entities get double-decoded
<script> var x = "user data"; </script> ← raw text: JSON-encode; entities are NOT decoded
<style> .a { color: user data } </style> ← CSS: strict allowlist
```

**Event-handler attributes** are the easiest to explain wrong and the easiest to write wrong. When the browser parses `onclick="..."`, it **first decodes character references in the attribute value**, then hands the result to the JS engine. Measured here:

```
Escaped HTML:      <a onclick="greet('&#39;);alert(1);//')">
What the JS engine sees: greet('');alert(1);//')
```

`&#39;` decodes back to `'`, the string closes, and `alert(1)` becomes its own statement. **HTML escaping here isn't merely useless — it created the hole.** This is the double-decoding vulnerability.

> A commonly mis-taught contrast: the inside of a `<script>` element is **raw text**, and character references are **not** decoded. So HTML-escaping inside `<script>` isn't "a hole" — it's **corrupted data**: the username literally renders as `&#39;`. The correct tool is `JSON.stringify`.

### 3.4 Password Hashing: The One Place Slower Is Better

General-purpose hashes (SHA-256, MD5) are designed to be **fast** — they must chew through gigabytes per second. That goal **directly conflicts** with password storage.

Once an attacker has the database, cracking looks like:

```
for candidate in common_password_dictionary:
    if hash(candidate + salt) == stored_hash:
        cracked
```

**The only cost is that one `hash()` call.** So password hash functions are deliberately slow:

| Function | How it's slow |
|---|---|
| PBKDF2 | Iterates HMAC hundreds of thousands of times |
| bcrypt | Iteration + requires 4 KB of memory |
| scrypt | Iteration + **configurable large memory** |
| Argon2id | Iteration + large memory + side-channel resistance |

The "eats memory" part is key: a GPU has thousands of cores but **limited memory bandwidth**. A hash that needs 64 MB per instance erases the GPU's parallelism advantage — which is why scrypt/Argon2 beat PBKDF2 (PBKDF2 only burns CPU and accelerates well on GPUs).

**Note the cost is symmetric**: your login endpoint pays that 150 ms too. Iteration count is therefore a **security-vs-usability** tradeoff, and OWASP raises its recommendation as hardware improves.

### 3.5 Salting: Killing Precomputation

Without a salt, the same password produces the same hash everywhere, so an attacker can **precompute** an enormous table (a rainbow table) and just look values up.

With a per-user salt, the same password produces completely different hashes → **precomputation dies as a category**, and the attacker must run the dictionary separately per user.

Measured here, three users sharing one weak password:

```
No salt: three identical hashes (instantly reveals "these three share a password",
         and one rainbow-table hit cracks all three)
Salted:  three completely different results
```

The salt **need not be secret** (it's stored alongside the hash); it only needs to be **unique and random**.

### 3.6 Timing Attacks: Elapsed Time Is Also Output

```java
for (int i = 0; i < a.length; i++)
    if (a[i] != b[i]) return false;      // ⚠️ returns the moment it finds a difference
```

This loop's runtime is **proportional to how many prefix bytes were guessed correctly**. So an attacker can:

```
Guess byte 1 = 0x00..0xff (256 tries); the slowest one is correct
Guess byte 2 = 0x00..0xff (256 tries); ...
```

A 32-byte token drops from **256³²** to **256 × 32 = 8192** attempts.

The C# example measures the gap: **byte 1 differing takes 18.5 ms; byte 32 differing takes 237.4 ms — 12.9x** (Java measured 5.9x).

Constant-time comparison **never returns early and accumulates differences with bitwise ops**:

```java
int diff = a.length ^ b.length;
for (int i = 0; i < a.length; i++) diff |= a[i] ^ b[i];   // always scans everything
return diff == 0;
```

Measured, the gap flattens to **0.99x**. The price is a higher absolute cost (453 ms vs 18 ms) — **it always does the full work**.

> This is the most direct conflict with Chapter 57: **there, return as early as possible; here, returning early is forbidden.**

### 3.7 Memory Safety: A Whole Class of Bugs, Present and Absent

Memory-safety bugs in C/C++ share one structure: **length information is separated from the data**.

```c
char* p;             // an address, no length
strcpy(dst, src);    // strcpy doesn't know dst's size; it just looks for '\0'
```

And the bug is often **not on the line that crashes**. The integer overflow measured here:

```
Requested 1073741825 elements × 4 bytes = expected 4294967300 bytes
32-bit multiply actually produced: 4 bytes  ← wrapped around
```

So 4 bytes were allocated while the program believes it has 4 GB — **every subsequent write is out of bounds**. The real bug is that multiplication, possibly dozens of lines before the crash.

Microsoft and Chromium independently report: **~70% of high-severity CVEs are memory-safety issues.** That 70% **does not exist as a category** in Java/C#/Python/JS/Rust.

But the other half must be said plainly: **the remaining 30% — injection, authentication, authorization, crypto misuse — exists identically in every language.** The first six sections of this chapter are all about that 30%.

**Changing languages eliminates one class of bugs. It does not eliminate security work.**

---

## 4. JavaScript

The JS example targets XSS — plus one injection category unique to JS.

### 4.1 One escapeHtml, Three Outcomes

```javascript
function escapeHtml(s) {
  return String(s)
    .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}
```

**Context A: HTML text** — ✅ works

```
Unescaped: <div><img src=x onerror=alert(1)></div>
Escaped:   <div>&lt;img src=x onerror=alert(1)&gt;</div>
```

**Context B: unquoted attribute** — ❌ fails

```
Unescaped: <div class=a onmouseover=alert(1)>
Escaped:   <div class=a onmouseover=alert(1)>     ← identical
```

The payload contains **nothing that needs escaping**. With no quotes around the attribute, a single space introduces a new one.

**Context C: event-handler attribute** — ❌ fails (most insidiously)

```
Escaped HTML:            <a onclick="greet('&#39;);alert(1);//')">
What the JS engine sees: greet('');alert(1);//')
```

The browser **decodes character references first**, `&#39;` becomes `'`, and the string closes.

### 4.2 The javascript: Pseudo-Protocol — Perfectly Escaped, Still Owned

```javascript
const url = 'javascript:alert(document.cookie)';
escapeHtml(url)   // → unchanged; nothing in it needs escaping
// <a href="javascript:alert(document.cookie)">click</a>
```

The right answer is a **protocol allowlist**:

```
https://ok.example/a     → https://ok.example/a
javascript:alert(1)      → #
DaTa:text/html,x         → #        (case-variation bypass blocked)
//evil.example           → //evil.example   ⚠️ passes!
```

That last row matters: `//evil.example` **passes the protocol allowlist** — it inherits the page's `https:`. The protocol is fine; the host belongs to the attacker. That's an open redirect, and it needs **an additional host allowlist**.

> Lesson: a check passing ≠ safe. **Ask what the check actually checks.**

### 4.3 The Real Fix: Structured APIs

```javascript
el.innerHTML = "<div>" + userInput + "</div>";   // ❌ goes through the HTML parser
el.textContent = userInput;                      // ✅ data stays data
el.setAttribute("class", userInput);             // ✅ attribute values aren't parsed
```

**Exactly isomorphic to parameterized queries**: put the data on a channel that won't parse it.

React and Vue escape `{expr}` by default — that's where their "safe by default" reputation comes from — and `dangerouslySetInnerHTML` / `v-html` spell out the cost in their names.

### 4.4 Prototype Pollution: An Injection Class Unique to JS

```javascript
function unsafeMerge(target, src) {
  for (const k in src) {
    if (typeof src[k] === 'object' && src[k] !== null) {
      target[k] = target[k] || {};
      unsafeMerge(target[k], src[k]);       // ⚠️ never filters __proto__
    } else target[k] = src[k];
  }
  return target;
}
const evil = JSON.parse('{"__proto__":{"isAdmin":true}}');
unsafeMerge({}, evil);
```

```
Before merge, a BRAND NEW empty object's isAdmin: undefined
After merge,  a BRAND NEW empty object's isAdmin: true
```

The attacker never touched that object, yet it now has `isAdmin === true`. Chapter 26 covered prototype chains: **modifying `Object.prototype` affects every object** — every `if (user.isAdmin)` check in the codebase falls.

Defense: filter `__proto__`/`constructor`/`prototype`, or use `Object.create(null)` / `Map`.

### 4.5 Defense in Depth: What the Browser Gives You

```
CSP:              script-src 'self' → even if injected, inline scripts don't run
HttpOnly cookie:  → document.cookie can't read it; XSS can't steal the session
SameSite=Lax:     → cross-site requests carry no cookies, blocking most CSRF
SRI:              <script integrity="sha384-..."> → a compromised CDN won't load
```

All of these rest on one assumption: **the first line will fail, and losses must still be bounded.**

---

## 5. Python

The Python example carries the chapter's spine: SQL injection and password storage.

### 5.1 The Key Experiment

```python
def login_vulnerable(con, name):
    """⚠️ String concatenation — whatever is in `name` becomes part of the SQL syntax"""
    sql = f"SELECT id, name, role FROM users WHERE name = '{name}'"
    return sql, con.execute(sql).fetchall()
```

```
Normal input: name = 'alice'
  SQL actually executed: SELECT ... WHERE name = 'alice'
  1 row returned: ['alice']

Malicious input: name = "alice' OR '1'='1"
  SQL actually executed: SELECT ... WHERE name = 'alice' OR '1'='1'
  3 rows returned: ['alice', 'bob', 'root']       ← the whole table
```

Parameterized:

```python
sql = "SELECT id, name, role FROM users WHERE name = ?"
con.execute(sql, (name,))
```

```
name = 'alice'                → 1 row: ['alice']
name = "alice' OR '1'='1"     → 0 rows: []
SQL executed both times is identical: SELECT id, name, role FROM users WHERE name = ?
```

**Note the last line**: the SQL string never changed; the malicious input was treated as a (nonexistent) username.

### 5.2 A Real Escaping Bypass

```python
def escape_quotes(s):
    """Some people believe "replacing ' with '' makes it safe" """
    return s.replace("'", "''")
```

The defenders applied this to every input. But another query in the project used a value **not wrapped in quotes**:

```
Input: '1 OR 1=1'  (unchanged by escaping: '1 OR 1=1')
SQL:   SELECT id, name FROM users WHERE id = 1 OR 1=1
→ 3 rows: ['alice', 'bob', 'root'] —— ESCAPING FULLY BYPASSED
```

**Escaping presupposes that the value is quoted, and numeric columns usually aren't.** Once the premise fails, the whole defense fails — and that premise is written down nowhere.

### 5.3 Where Parameterization Can't Reach: Identifiers

`?` substitutes **values** only — not table names, column names, or `ORDER BY` direction:

```
ORDER BY ? → syntactically accepted, but it sorts by the CONSTANT 'name' — i.e. not at all
```

Dynamic identifiers have exactly one safe form — an **allowlist mapping**:

```python
allowed = {"name", "role"}
col = want if want in allowed else "id"
```

```
requested sort column 'name'                     → actually used 'name'
requested sort column 'secret; DROP TABLE users' → actually used 'id'
```

### 5.4 Password Hashing: Slower Is Better

```
SHA-256, 200000 direct hashes:  55 ms → ~3.66 M/sec (single core)
PBKDF2-SHA256   10000 rounds:  2.7 ms → ~ 375.2 /sec
PBKDF2-SHA256  100000 rounds: 26.2 ms → ~  38.1 /sec
PBKDF2-SHA256  600000 rounds: 155.5 ms → ~   6.4 /sec
→ guessing rate differs by 568,411x
```

A ten-million-entry common-password dictionary: **seconds with SHA-256, weeks with PBKDF2.**

### 5.5 Salting

```
No salt (three users, same weak password):
  alice  → f52fbd32b2b3b86ff88ef6c490628285...
  bob    → f52fbd32b2b3b86ff88ef6c490628285...
  carol  → f52fbd32b2b3b86ff88ef6c490628285...

Per-user random salt:
  alice  → d0d8eeedb6eca936bc357fff14c726ca...
  bob    → bb58bff0b9c5c487e49c78629d171c1c...
  carol  → 7afefe537e84aad404039da16a82d80d...
```

### 5.6 An Honest Failure: The Timing Gap Didn't Show Up

The Python example tried to measure the timing difference between `==` and `hmac.compare_digest` and **could not**: the per-call difference is nanoseconds, entirely drowned by interpreter overhead.

That is stated plainly in the example:

> **Undetectable ≠ nonexistent** — an attacker can issue a million requests and average the noise away. This is the inverse of Chapter 57's "the gap must exceed the spread": **the attacker has a way to shrink the spread.**

(Java and C#, on compiled runtimes, successfully measured 5.9x and 12.9x.)

---

## 6. Java

The Java example tests three APIs that look usable and aren't.

### 6.1 java.util.Random Is Predictable

```java
long serverSeed = 1_700_000_000_123L;         // the attacker guessed the timestamp seed
Random serverRng = new Random(serverSeed);    // server issues reset tokens
Random attackerRng = new Random(serverSeed);  // attacker replays
```

```
5 reset tokens issued by the server: [700288, 997043, 31130, 561012, 185333]
Attacker's replay with the same seed: [700288, 997043, 31130, 561012, 185333]
Identical: true
```

`java.util.Random` is a **linear congruential generator**: only 48 bits of state, and observing **two consecutive outputs** solves for the internal state and predicts everything after. It was designed for simulation, shuffling, and games — **not for security**.

> The test is simple: **any security-relevant random number uses `SecureRandom`** — tokens, salts, IVs, session IDs, password reset codes, CSRF tokens. No exceptions.

### 6.2 Password Hashing (JVM Numbers)

```
SHA-256, 1000000 direct hashes: 182 ms → ~5.49 M/sec (single core)
PBKDF2-HmacSHA256  10000 rounds:   7.9 ms → ~126.5 /sec
PBKDF2-HmacSHA256 100000 rounds:  72.4 ms → ~ 13.8 /sec
PBKDF2-HmacSHA256 600000 rounds: 425.6 ms → ~  2.3 /sec
→ guessing rate differs by 2,334,582x
```

### 6.3 Constant-Time Comparison (the Gap Is Measurable)

```
byte-by-byte, differing at byte 1:   3.3 ms
byte-by-byte, differing at byte 32: 19.5 ms (5.9x slower)

MessageDigest.isEqual, early diff: 43.1 ms
MessageDigest.isEqual, late diff:  45.4 ms (1.05x)
```

Runtime **grows with the correctly-guessed prefix length** — that gap is an information channel.

### 6.4 Deserialization: Another Path from Data to Code

`ObjectInputStream.readObject()` **calls methods on the classes being deserialized**. An attacker constructs a "gadget chain" so the JVM executes arbitrary commands while restoring the object graph.

**Exactly isomorphic to SQL injection**: untrusted data handed to something that executes it.

Defense, in order: ① don't deserialize untrusted data ② use plain data formats like JSON ③ if you must, apply an `ObjectInputFilter` allowlist (Java 9+).

### 6.5 What's Left in a Memory-Safe Language

```
✅ Out-of-bounds access throws ArrayIndexOutOfBoundsException; no stomping other memory
✅ new int[n] throws on a negative n
⚠️ But integers still overflow: Integer.MAX_VALUE + 1 = -2147483648
```

Classic vulnerability shape: `if (offset + len > size)` overflows to a negative on the left and the check is bypassed. Use `Math.addExact()` to throw on overflow, or rewrite as `offset > size - len`.

Also: the default `ObjectInputStream` is unsafe, and the default XML parser allows external entities (XXE).

**Memory-safe languages eliminate one whole class of bugs; the logic bugs are all still there.**

---

## 7. C++

The C++ example covers that 70% — and what eliminating it actually costs.

### 7.1 Integer Overflow: The Allocation Silently Shrinks

```cpp
static size_t alloc_size_buggy(uint32_t count, uint32_t elem) { return count * elem; }
```

```
Requested 1073741825 elements × 4 bytes = expected 4294967300 bytes
32-bit multiply actually produced: 4 bytes  ← wrapped around
```

4 bytes were malloc'd while the program believes it has 4 GB — **every subsequent write is out of bounds**.

**The bug isn't on the writing line; it's in a multiplication dozens of lines earlier.** This is one of the most common shapes in the CVE database.

The checked version throws on overflow, and the allocation never happens.

### 7.2 Signed Lengths: An Out-of-Bounds That Passes the Check

```cpp
if (user_len > BUFSZ) reject();
memcpy(dst, src, user_len);
```

```
Attacker supplies user_len = -1, BUFSZ = 64
Upper-bound check (-1 > 64): PASSES  ← of course a negative is below the bound
But memcpy's third parameter is size_t; -1 converts to: 18446744073709551615 bytes
```

A check that only tests the **upper bound** is fully bypassed by a negative number. The root cause is **one value with two meanings**: signed when checked, unsigned when used.

### 7.3 The Cost of Bounds Checking: That Argument Has Expired

```
operator[] (unchecked) 20000000 times: 20.20 ms
at()       (checked)   20000000 times: 44.60 ms (2.21x slower)     ← -O0
```

But change the optimization level:

| Optimization | `at()` relative to `[]` |
|---|---|
| `-O0` | **2.21x** |
| `-O1` | **1.05x** |
| `-O2` | **1.02x** |

**The cost of bounds checking collapses as compilers get stronger.** The optimizer can prove `i` is in range, or hoist the check out of the loop; whatever remains is predicted correctly ~100% of the time (Chapter 57).

> So "C++ skips checks for performance" largely **no longer holds** — that design was a 1980s tradeoff, when optimizers were far weaker. It also explains why Rust checks by default and still claims "zero-cost abstraction": **the math works out now.**

And the real difference between `at()` and `[]` is **what happens on failure**:

```
at(N+10) → throws std::out_of_range (out-of-bounds becomes a HANDLEABLE ERROR)
v[N+10]  → undefined behavior: may crash, may return garbage, may LOOK COMPLETELY FINE
```

**That last case is the dangerous one: tests all green, vulnerability already in production.**

> The first version of this experiment measured `at()` as **faster** — because the first loop absorbed the page-fault cost of first touching 80 MB. Warmup plus best-of-N produced the numbers above. **That's another one of Chapter 57's three traps, stepped on right here.**

### 7.4 C Strings and std::span

```
Destination buffer 16 bytes, source data 76 bytes
strcpy(dst, src)                → OVERFLOW: strcpy only looks for '\0'
snprintf(dst, sizeof(dst), ...) → truncates to 15 bytes, returns 76 (what it WOULD have written)
```

**Return value 76 > buffer 16**: the return value is the only way to detect truncation; ignoring it turns into silent data loss.

Root cause: `char*` splits "pointer" from "length", and **only the pointer gets passed**.

```cpp
auto sum_raw = [](const int* p, size_t n) { ... };    // ⚠️ caller passes the wrong n and it's over
auto sum_span = [](std::span<const int> sp) { ... };  // ✅ the length cannot be wrong
```

**The core principle of safe API design: make wrong usage fail to compile, rather than documenting it.**

---

## 8. C#

The C# example covers cryptography — a domain where "it runs" and "it's secure" are separated by several choices that raise no error.

### 8.1 ECB Mode: Encrypted, and Hiding Nothing

Three records, the first and third identical:

```
Plain  blk1: alice:role=user      blk2: bob:role=user      blk3: alice:role=user

ECB    blk1: 77f4a8dddffd1b30...  blk2: 48a0cfc60182...  blk3: 77f4a8dddffd1b30...
       → ciphertext block 1 == block 3: True  ← PLAINTEXT STRUCTURE FULLY LEAKED

CBC    blk1: f6ed351b2538c3f2...  blk2: 1c3b0c8d4e1f8...  blk3: f4db96f6f2cca4eb...
       → ciphertext block 1 == block 3: False
```

ECB encrypts each block independently → **identical plaintext blocks produce identical ciphertext blocks**. The famous "ECB penguin" image comes from exactly this: after encryption you can still see the outline.

**Both encryptions raised no error and decrypt correctly** — security doesn't show up in functional tests.

### 8.2 Reusing an IV Breaks CBC the Same Way

```
Plaintext A = "transfer 100 to bob", Plaintext B = "transfer 100 to eve"
Encrypting the SAME plaintext A twice → identical ciphertext: True
Encrypting A vs B → block 1 identical: True   ← leaks "first 16 bytes match"
                    block 2 identical: False  ← the difference starts here
```

From this, an attacker can **binary-search the position of the difference** and identify replayed requests.

An IV exists to **make identical plaintexts produce different ciphertexts**; fixing it cancels that purpose. IVs must be randomly generated per message (they may be transmitted publicly, but never repeat).

### 8.3 Encryption Without Authentication

CBC provides no integrity: an attacker flips a ciphertext byte, decryption still "succeeds", the corresponding plaintext block becomes garbage — and **the corresponding bits of the previous block flip exactly**.

```
AES-GCM decrypting tampered ciphertext → throws CryptographicException ✅
```

AEAD (`AES-GCM`, `ChaCha20-Poly1305`) builds authentication in: flip one bit and decryption simply fails.

> **Never encrypt without authenticating.**

### 8.4 Constant-Time Comparison (Largest Measured Gap)

```
byte-by-byte, differing at byte 1:   18.5 ms
byte-by-byte, differing at byte 32: 237.4 ms (12.9x slower)

FixedTimeEquals, differing at byte 1:  452.8 ms
FixedTimeEquals, differing at byte 32: 450.0 ms (0.99x)
```

Note `FixedTimeEquals` has a **higher absolute cost** (453 ms vs 18 ms): it always does the full work.

> This is precisely where this chapter and Chapter 57 diverge: **there, return early; here, returning early is forbidden.**

### 8.5 Random Numbers: The Naming Is the Trap

```
Random(12345) produced: [66746, 70159, 774765, 511139]
Same-seed replay:       [66746, 70159, 774765, 511139]  → identical: True
RandomNumberGenerator:  8420b04c4c99a0d7ec187fcea9596ca3 (unpredictable)
```

> `Random` sounds random enough, and it's precisely the one you can't use.

### 8.6 The "Don't Roll Your Own" Checklist

```
❌ Designing your own cipher       → use AES-GCM / ChaCha20-Poly1305
❌ Hand-assembling CBC+HMAC        → use AEAD
❌ SHA-256 for passwords           → use Argon2id / bcrypt / PBKDF2
❌ == to compare keys              → use FixedTimeEquals
❌ Random for tokens               → use RandomNumberGenerator
❌ Fixed or reused IV/nonce        → generate randomly every time
```

**What they share: every one of these passes functional tests.**

---

## 9. SQL

The SQL example is about defense in depth: assuming the application layer already fell, what's left at the database layer.

### 9.1 Least Privilege Determines the Blast Radius

```
Connected as superuser: read every table, DROP, read files, possibly execute commands
Connected as least-privilege: only the granted columns of the granted tables
```

```sql
GRANT SELECT, INSERT, UPDATE ON app.orders TO app_user;
-- never grant DROP / CREATE / FILE / SUPERUSER; migrations use a separate account
```

### 9.2 Views: Put Sensitive Columns Out of Reach

```sql
CREATE VIEW users_public AS
  SELECT id, tenant_id, name, role FROM users WHERE deleted_at IS NULL;
```

```
Columns of users_public: id, tenant_id, name, role
```

`pw_hash` and `email` **are not in the view** — even if this query is injected, the attacker cannot reach them.

### 9.3 Broken Access Control (IDOR): Parameterization Cannot Help

The most important item in this section. A tenant-100 user requests `GET /orders/3` (which belongs to tenant 200):

```
ⓐ query by id only (the vulnerable form) → 1 row returned, cross-tenant leak
ⓑ also filter by tenant_id               → 0 rows returned
```

**The input is the legitimate number 3, with no special characters at all.** Parameterization, input validation, and a WAF all fail to help.

> **Authorization must be checked at every data access, not once at the route entrance.**

PostgreSQL's **row-level security (RLS)** pushes this constraint into the database so a forgetful application still can't leak:

```sql
ALTER TABLE orders ENABLE ROW LEVEL SECURITY;
CREATE POLICY t ON orders USING (tenant_id = current_setting('app.tenant')::int);
```

### 9.4 Audit Logs

```sql
CREATE TRIGGER audit_role_change AFTER UPDATE OF role ON users
BEGIN
  INSERT INTO audit_log(at, actor, action, target)
  VALUES (..., 'role: '||OLD.role||' → '||NEW.role, NEW.name);
END;
```

```
role: user → admin (target bob, at 2026-08-13T10:00:00)
```

An audit log's value isn't prevention — it's **being able to answer "what happened" afterward**. Crucially, the application account must be able to **INSERT only, never UPDATE/DELETE**, or it can be scrubbed.

### 9.5 The Security Implications of Soft Deletes

```
carol still exists in users:            1 row
carol is invisible in users_public:     0 rows
```

Any query that **forgets `deleted_at IS NULL`** leaks deleted data. And GDPR's right to erasure requires real deletion — soft deletes turn compliance into an ongoing debt.

### 9.6 The Database-Side Checklist

```
ⓐ least-privilege application account; separate migration account
ⓑ sensitive columns behind views / column-level grants
ⓒ multi-tenancy backstopped by RLS
ⓓ connection-string secrets in a key management service
ⓔ TLS in transit + encryption at rest; back-ups encrypted too
ⓕ audit logs are append-only
```

**Note that none of the six is "prevent injection"** — that's the application layer's job. **The database layer's job is to assume the application will fail and contain the damage.**

---

## 10. Cross-Language Comparison

| Dimension | JavaScript | Python | Java | C++ | C# | SQL |
|---|---|---|---|---|---|---|
| **Focus here** | XSS / prototype pollution | SQL injection / passwords | RNG / deserialization | Memory safety | Crypto misuse | Defense in depth |
| **Memory safe** | ✅ | ✅ | ✅ | ❌ | ✅ (outside `unsafe`) | ✅ |
| **Safe-by-default injection defense** | framework auto-escaping | DB-API parameterization | PreparedStatement | — | parameterization | — |
| **CSPRNG** | `crypto.randomBytes` | `secrets` | `SecureRandom` | no CSPRNG in `<random>` | `RandomNumberGenerator` | — |
| **Constant-time compare** | `crypto.timingSafeEqual` | `hmac.compare_digest` | `MessageDigest.isEqual` | write it yourself | `FixedTimeEquals` | — |
| **Password hashing (stdlib)** | `crypto.scrypt` | `hashlib.pbkdf2_hmac` | `SecretKeyFactory` | none | `Rfc2898DeriveBytes` | — |
| **Language-specific trap** | prototype pollution | `pickle` deserialization | `ObjectInputStream` | the entire UB category | `Random`'s misleading name | IDOR |
| **Largest gap measured** | — | password hash **568,411x** | password hash **2.33M x** | bounds check **2.21x→1.02x** | timing **12.9x** | — |

### 10.1 How Much "Safe by Default" Varies

The same requirement, six default paths of very different safety:

```python
# Python: the stdlib DB-API IS parameterized; f-string SQL is the MORE ANNOYING path
con.execute("... WHERE name = ?", (name,))
```

```javascript
// React: {expr} escapes by default; bypassing requires writing dangerouslySetInnerHTML
<div>{userInput}</div>
```

```java
// Java: PreparedStatement is standard, but Statement + concatenation is equally one line
stmt.executeQuery("... WHERE name = '" + name + "'");   // just as easy to write
```

**Defaults determine what most code looks like.** An API where the safe usage is more annoying will end up written the unsafe way somewhere.

### 10.2 What Memory Safety Eliminates and What It Doesn't

| Vulnerability class | C/C++ | Java/C#/Python/JS |
|---|---|---|
| Buffer overflow / UAF / double free | ⚠️ present | ✅ **eliminated as a category** |
| Integer overflow | ⚠️ present (UB) | ⚠️ **present** (but never becomes memory corruption) |
| SQL injection / XSS / command injection | ⚠️ present | ⚠️ **equally present** |
| Deserialization | ⚠️ present | ⚠️ **equally present** (Java especially) |
| Crypto misuse | ⚠️ present | ⚠️ **equally present** |
| Broken access control / auth | ⚠️ present | ⚠️ **equally present** |

**~70% of high-severity CVEs vanish in row one. The other six rows lose nothing.**

---

## 11. Implementation Comparison

| Mechanism | JavaScript | Python | Java | C++ | C# |
|---|---|---|---|---|---|
| **Out-of-bounds access** | returns `undefined` | raises `IndexError` | throws | **undefined behavior** | throws |
| **Integer overflow** | becomes `double` (precision loss) | **arbitrary precision** | silent wraparound | **UB when signed** | silent wraparound (`checked` optional) |
| **CSPRNG source** | `getrandom`/`BCryptGenRandom` | same | same | platform-dependent | same |
| **Strings** | immutable, carry length | immutable, carry length | immutable, carry length | `char*` **carries no length** | immutable, carry length |
| **Default deserializer** | `JSON.parse` (pure data) ✅ | `pickle` (executes) ⚠️ | `readObject` (executes) ⚠️ | no standard | `BinaryFormatter` (obsoleted) ⚠️ |

### 11.1 Three Ways to Handle Integer Overflow

```
Python:  promotes to arbitrary precision — overflow DOESN'T EXIST, at a performance cost
Java/C#: silently wraps to negative — well-defined, but checks are easily bypassed
C++:     signed overflow is UNDEFINED BEHAVIOR — the compiler may assume it never happens,
         so a hand-written check like if (a + b < a) CAN BE OPTIMIZED AWAY
```

That last one deserves attention: hand-written overflow checks in C++ **may be deleted by the compiler**, because the standard permits assuming UB doesn't occur. This is Chapter 57's "what you wrote isn't what runs" reappearing in the security domain.

### 11.2 Why Deserialization Defaults Differ So Much

`JSON.parse` can only produce **plain data** (objects, arrays, strings, numbers, booleans, null) — it has no ability to construct arbitrary types, let alone call methods.

`pickle` / `ObjectInputStream` / `BinaryFormatter` are designed to **restore arbitrary object graphs**, which necessarily requires constructing arbitrary types and invoking constructors and callbacks. **More capability, more attack surface.**

> A general security design principle: **choose the format with just enough capability.**

---

## 12. Performance Analysis

Security and performance interact in two directions here, both worth quantifying.

### 12.1 What Security Costs (Smaller Is Better)

| Measure | Measured cost |
|---|---|
| Parameterized queries | **Negative cost** (plan reuse; usually faster) |
| Array bounds checking (C++ `at()`) | -O0 **2.21x** / -O1 **1.05x** / -O2 **1.02x** |
| Output encoding (HTML escaping) | Negligible (versus one network round trip) |
| TLS handshake | ~1 extra RTT once (≈0 with session resumption) |
| ASan | ~**2x** (hence CI/test only, never production) |

**The bounds-checking row is this chapter's most important performance result**: it shows the "safety is too expensive" argument is largely expired.

### 12.2 Deliberate Slowness (Bigger Is Better)

| Measure | Measured |
|---|---|
| SHA-256 (Python) | 3.66 M/sec |
| SHA-256 (Java) | 5.49 M/sec |
| PBKDF2 600k rounds (Python) | **6.4/sec** (**568,411x** gap) |
| PBKDF2 600k rounds (Java) | **2.3/sec** (**2,334,582x** gap) |
| Constant-time compare (C#) | 453 ms vs 18 ms (**24x slower**, but timing gap 12.9x → 0.99x) |

### 12.3 Quantifying the Timing Leak

| Language | Early diff vs late diff | Constant-time version |
|---|---|---|
| Java | 3.3 ms vs 19.5 ms (**5.9x**) | 1.05x |
| C# | 18.5 ms vs 237.4 ms (**12.9x**) | 0.99x |
| Python | **not measurable** (drowned by interpreter overhead) | — |

The Python row is an honest failure and a lesson: **undetectable ≠ nonexistent**. An attacker can issue a million requests and average out the noise — **the inverse of Chapter 57's "the gap must exceed the spread": the attacker can shrink the spread.**

### 12.4 Three Things These Numbers Say

**① The cost of security measures is systematically overestimated.** Parameterized queries are faster, bounds checking costs 2% at -O2, and output encoding is negligible next to one network round trip.

**② The only expensive one is password hashing, and that's the point.** 150 ms of login latency buys a 568,411x increase in cracking cost.

**③ Exactly one place genuinely conflicts: constant-time comparison.** It must forgo every early-return optimization. It's the one place in this book where you must deliberately write slower code.

---

## 13. Engineering Practice

### 13.1 Threat Modeling: Four Questions Before Writing Code

```
① What are we protecting?     (user data? money? availability? reputation?)
② Who is the attacker?        (anonymous outsider? logged-in user? insider? supply chain?)
③ What can they reach?        (attack surface: APIs, uploads, dependencies, ops channels)
④ What happens if it fails?   (leak scope? lateral movement? tampering?)
```

Question ② is the most-skipped. **Most broken-access-control (IDOR) bugs have "a logged-in ordinary user" as the attacker** — if your threat model contains only "external hacker," this class never gets considered.

### 13.2 Wire Security Checks into CI

| Layer | Tools | What it checks |
|---|---|---|
| SAST | CodeQL, Semgrep, Bandit | Dangerous patterns in code (string-built SQL) |
| SCA | `npm audit`, Dependabot, Trivy | Known CVEs in dependencies |
| Secret scanning | gitleaks, truffleHog | Credentials committed to the repo |
| Dynamic | ZAP, Burp | The running application |
| Runtime | ASan/UBSan (C++) | Memory errors |

Key design points (same logic as Chapter 52's tests and Chapter 57's benchmarks):

- **It must be able to fail**: a check that never blocks a merge is no check;
- **It must have low false positives**: a noisy tool is universally ignored within a week;
- **It must give a fix path**, not just "found a vulnerability."

### 13.3 Secret Management

```
❌ Hardcoded in source        → git history is never truly cleaned
❌ Plaintext in env vars      → process lists, core dumps, and logs can leak them
❌ .env committed to the repo → the #1 finding of secret scanners
✅ A key management service (Vault/KMS/Secrets Manager)
✅ Rotate regularly, AND REHEARSE THE ROTATION PROCEDURE
```

That last point gets skipped: a rotation procedure never rehearsed will not succeed the day you actually need it urgently (because a key leaked).

### 13.4 Dependencies and the Supply Chain

Chapter 53 measured it: one `create-react-app` pulls in 1000+ transitive dependencies. **You audited your 200 lines and are running hundreds of thousands of someone else's.**

`event-stream` (2018) is the landmark case: a package with 2 million weekly downloads was handed to a new maintainer who planted crypto-stealing code.

The defense is a **dependency-management problem, not a code-review problem**:

```
Lockfiles pinning versions + integrity checks
Minimal dependencies (ask "is it worth it" for each)
SCA in CI
Vendoring or an internal mirror for critical dependencies
```

### 13.5 Incident Response: Assume It Already Happened

```
① Detect:  logs, alerts, anomalous traffic (no logs, no detection)
② Contain: revoke keys, isolate systems, disable affected features
③ Forensics: can the audit log answer "who accessed what, when"? (SQL §9.4)
④ Fix:     patch the vulnerability, not just the traces
⑤ Review:  write it down, change the process, add automated checks
```

Step ③ determines whether the first four are even possible. **Logs and audit trails must be in place before the incident** — the same logic as Chapter 52's "write the test before the bug appears."

---

## 14. Best Practices

- **Always use parameterized queries**; never concatenate SQL. Use allowlist mappings for dynamic identifiers.
- **Encode on output, per context**; don't "filter dangerous characters" on input.
- **Prefer structured APIs** (`textContent`, `execve` arrays, DOM APIs) over string building.
- **Hash passwords with Argon2id/bcrypt/scrypt**, a unique random salt per user, never bare SHA/MD5.
- **Use a CSPRNG for anything security-relevant** (`SecureRandom` / `secrets` / `RandomNumberGenerator`).
- **Compare keys, tokens, and signatures with constant-time functions.**
- **Encrypt with AEAD** (AES-GCM / ChaCha20-Poly1305); IVs/nonces random and never repeated.
- **Check authorization at every data access**, not once at the route entrance.
- **Least-privilege database accounts**, sensitive columns behind views, RLS as a multi-tenancy backstop.
- **Defense in depth**: CSP, HttpOnly, SameSite, SRI — assume the first line fails.
- **In C/C++, turn on all warnings as errors**, run ASan/UBSan in CI, use `std::span`/containers over raw pointers.
- **Pin, scan, and minimize dependencies.**
- **Security checks in CI must be able to block a merge.**
- **Secrets go in a key management service, and rotation must be rehearsed.**

---

## 15. Common Pitfalls

### Pitfall 1: Assuming Escaping Is Enough

```python
name = user_input.replace("'", "''")
sql = f"SELECT ... WHERE id = {name}"      # ⚠️ numeric column, no quotes
```

**Why it's wrong**: measured here, `1 OR 1=1` is unchanged by escaping and the whole table is dumped. Escaping presupposes a quoted value, and that premise is written down nowhere.

**How to avoid it**: parameterize. It never produces a string that needs correct escaping.

### Pitfall 2: One Escaping Function for Every Context

```javascript
el.setAttribute('onclick', `f('${escapeHtml(name)}')`);
```

**Why it's wrong**: event-handler attributes **decode character references first**, so `&#39;` becomes `'` and escaping created the hole (measured here).

**How to avoid it**: choose the encoding by context; better still, **don't build such strings at all** — use `addEventListener` with a function.

### Pitfall 3: SHA-256 for Passwords

**Why it's wrong**: measured here, SHA-256 runs 3.66 M/sec versus PBKDF2's 6.4/sec — the attacker's cost differs by **568,411x**.

**How to avoid it**: Argon2id / bcrypt / scrypt. The stored format should **carry its algorithm and parameters** so iteration counts can be upgraded smoothly later.

### Pitfall 4: Generating Tokens with `Random`

**Why it's wrong**: measured here, replaying the same seed produces an **identical** sequence. `java.util.Random` has 48 bits of state; two consecutive outputs solve for it.

**How to avoid it**: `SecureRandom` / `secrets` / `RandomNumberGenerator`. **The test: if the random number relates to security at all, use a CSPRNG.**

### Pitfall 5: Comparing Tokens with `==`

**Why it's wrong**: measured here, 18.5 ms for a byte-1 difference versus 237.4 ms for a byte-32 difference (12.9x). That gap cuts a 32-byte token's brute-force space from 256³² to 8192.

**How to avoid it**: use a constant-time comparison function.

### Pitfall 6: Checking Authorization Only at the Entrance

```python
@require_login
def get_order(order_id):
    return db.query("SELECT * FROM orders WHERE id = ?", order_id)  # ⚠️ parameterized, but no ownership check
```

**Why it's wrong**: measured in the SQL example — the input is the legitimate number 3, no injection at all, and parameterization is irrelevant. This is **broken access control**, long ranked #1 in the OWASP Top 10.

**How to avoid it**: carry the ownership condition on every data access; backstop with RLS in the database.

### Pitfall 7: Encrypting Without Authenticating

**Why it's wrong**: CBC provides no integrity, and an attacker can flip plaintext bits precisely.

**How to avoid it**: use AEAD (AES-GCM / ChaCha20-Poly1305). Measured here: flip one bit and AES-GCM decryption throws.

### Pitfall 8: Assuming a Memory-Safe Language Is "Safe"

**Why it's wrong**: memory safety eliminates ~70% of high-severity CVEs, but injection, broken access control, crypto misuse, and deserialization are **all still there**. The first six sections of this chapter are entirely about that remaining 30%.

**How to avoid it**: treat "switch languages" as eliminating one class of bugs, not as the end of security work.

### Pitfall 9: Treating "Not Measurable" as "Not There"

**Why it's wrong**: the Python example could not measure the timing gap (drowned by interpreter overhead), but that doesn't mean the vulnerability is absent — an attacker can issue a million requests and average the noise.

**How to avoid it**: base security judgments on the **mechanism** (does this code return early?), not on one local measurement.

---

## 16. Interview Questions

**Q1: Why do parameterized queries prevent SQL injection, and how does that differ from "automatic escaping"?**

A: Parameterization **isn't escaping**. It puts the statement and the data on separate channels: the statement is sent and parsed into an execution plan first (with `?` as a parameter slot), and the value is filled into the slot afterward — **no parsing occurs in that second step**. So there is no string that needs correct escaping. Measured here: for the same malicious input `alice' OR '1'='1`, the concatenated version returns 3 rows (the whole table) and the parameterized version returns 0, while the SQL string executed is identical both times. A bonus is plan reuse, which usually makes it faster too.

**Q2: Why does "manual escaping" almost inevitably fail?**

A: Its correctness depends on unwritten premises: the value is quoted, the dialect matches, the charset matches, escaping happens exactly once, and every site remembers to call it. This chapter measured the first premise failing — the defenders escaped every input, but a numeric-column query had no quotes, so `1 OR 1=1` passed through unchanged and dumped the table. More fundamentally, **the attacker needs one omission while the defender must be right everywhere**. That's why safe-by-default mechanisms win.

**Q3: Why can't one universal escaping function handle XSS?**

A: Because a browser isn't one parser but a chain (HTML → attribute decoding → JS/CSS/URL). This chapter runs one `escapeHtml` against three contexts: HTML text ✅ works; unquoted attribute ❌ fails (nothing in the payload needs escaping); event-handler attribute ❌ fails — the browser **decodes character references first**, so `&#39;` becomes `'` and closes the string. Separately, the inside of `<script>` is raw text where character references are **not** decoded, so HTML-escaping there is "corrupting data," not "a hole." The real fix is to stop building strings: use `textContent`/`setAttribute`.

**Q4: Why is password hashing deliberately slow, and how slow is right?**

A: Once the attacker has the database, their only cost is each `hash()` call. Measured here: SHA-256 at 3.66 M/sec versus PBKDF2 at 600k rounds doing 6.4/sec — **568,411x**. A ten-million-entry dictionary takes seconds versus weeks. How slow is a **security-vs-usability** tradeoff, since your login endpoint pays it too — which is why OWASP raises its recommendation as hardware improves. Better still are Argon2id/scrypt, which are also **deliberately memory-hungry**, erasing the GPU parallelism advantage (PBKDF2 only burns CPU and accelerates well on GPUs).

**Q5: What is a timing attack, and why does constant-time comparison prevent it?**

A: Short-circuit comparison (returning at the first difference) takes time **proportional to the correctly-guessed prefix**, letting an attacker brute-force byte by byte: a 32-byte token drops from 256³² to 256×32 = 8192 attempts. The C# example measured 18.5 ms for a byte-1 difference versus 237.4 ms for byte-32 (**12.9x**). Constant-time comparison **never returns early and accumulates differences bitwise**, flattening the gap to 0.99x at the cost of a higher absolute time (453 ms vs 18 ms) — it always does the full work. **It's the one place in this book where you must write slower code.**

**Q6: What does a memory-safe language solve, and what doesn't it solve?**

A: It solves buffer overflows, use-after-free, and double-free as a category — Microsoft and Chromium each report ~**70%** of high-severity CVEs. It doesn't solve the other 30%: SQL injection, XSS, broken access control, crypto misuse, deserialization — **all equally present in every language**, and the subject of this chapter's first six sections. Integer overflow also **still exists** in Java/C# (it just can't become memory corruption); the classic shape is `if (offset + len > size)` overflowing to a negative on the left and bypassing the check.

**Q7: Does "C++ skips bounds checks for performance" still hold?**

A: Largely not. Measured here, `at()` versus `[]` costs **2.21x at -O0, 1.05x at -O1, and only 1.02x at -O2**. The optimizer can prove the index is in range or hoist the check out of the loop, and whatever remains is predicted ~100% correctly. That design was a 1980s tradeoff when optimizers were far weaker — which also explains why Rust checks by default and still claims zero-cost abstraction. The real difference is **failure behavior**: `at()` throws a handleable exception; `[]` is undefined behavior — it may crash, return garbage, or **look completely fine** (the dangerous case: tests green, vulnerability shipped).

**Q8: Why can't parameterized queries prevent broken access control (IDOR)?**

A: Because it **isn't injection**. Measured in the SQL example: a tenant-100 user requests `GET /orders/3` (owned by tenant 200); the input is the legitimate number 3 with no special characters. Querying by `id` alone returns 1 row (a cross-tenant leak); adding `tenant_id` returns 0. Parameterization, input validation, and WAFs are all irrelevant — **authorization must be checked at every data access**, not once at the route entrance. PostgreSQL's row-level security pushes the constraint into the database so a forgetful application still can't leak. This class has long topped the OWASP Top 10 precisely because it requires no "attack technique" at all.

---

## 17. Exercises

### Exercise 1: Reproduce and Fix an Injection (Basic)

In a language you know, write a minimal login query with string concatenation and craft an input that returns every user; then switch to parameterization and confirm the same input returns 0 rows.

**Requirement**: print **the actual SQL string executed both times** — the parameterized one should be completely unchanged. That step is the key to understanding "two channels."

### Exercise 2: Write a Correct Encoder for Three Contexts (Intermediate)

Write one encoder each for HTML text, quoted attributes, and JS strings, then:

1. Test all three against the same payload `'"><script>alert(1)</script>`;
2. Apply each function in the **wrong** context and explain what happens;
3. Explain why you cannot write a safe implementation for the "unquoted attribute" column.

### Exercise 3: Measure Your Password-Hashing Parameters (Practical)

On your production hardware:

1. Measure the per-call time of Argon2id/bcrypt/PBKDF2 at your current parameters;
2. Compute the QPS your login endpoint can sustain;
3. Work backwards: at that rate, how long would an attacker need for a ten-million-entry dictionary?
4. Compare with OWASP's current recommendation and decide whether to raise your parameters.

**The value is in step 3** — it turns "security" into a number people can discuss.

### Exercise 4: Find a Broken Access Control (Practical)

In a project you're working on:

1. List every endpoint that accepts a resource ID;
2. For each, ask: what happens with another tenant's or user's ID?
3. Actually test three of them;
4. For anything you find, evaluate whether RLS in the database is a viable backstop.

**This exercise usually surfaces more real problems than the previous three combined.**

### Exercise 5: Add a Security Pipeline (Practical)

Pick a repository and wire in SAST + SCA + secret scanning:

1. Run it once; record findings **and false positives**;
2. Tune rules until the false-positive rate is acceptable;
3. Set it to **block merges**;
4. Deliberately commit a string-built SQL query and confirm it's caught;
5. Deliberately commit an unrelated change and confirm no false alarm.

Step 5 is the most-skipped — **a noisy security check gets `# noqa`'d by everyone within a week.**

---

## 18. Chapter Summary

**Core conclusion**: security bugs are a class of error that **never turns tests red**. Every injection vulnerability shares one root cause — **untrusted data handed to something that parses it** — and every defense shares one shape: **don't build strings, use structured interfaces.**

**Key measurements**:

| Finding | Data |
|---|---|
| SQL injection | concatenated returns **3 rows (whole table)**, parameterized returns **0** |
| Escaping bypass | numeric column, unquoted; `1 OR 1=1` **unchanged** by escaping |
| XSS, three contexts | HTML text ✅ / unquoted attribute ❌ / event handler ❌ (double decoding) |
| Password hashing (Python) | SHA-256 3.66 M/s vs PBKDF2 6.4/s (**568,411x**) |
| Password hashing (Java) | SHA-256 5.49 M/s vs PBKDF2 2.3/s (**2,334,582x**) |
| Timing leak (C#) | 18.5 ms vs 237.4 ms (**12.9x**); constant-time 0.99x |
| Timing leak (Java) | 3.3 ms vs 19.5 ms (**5.9x**); constant-time 1.05x |
| Integer overflow | 1.07 billion × 4 bytes → actually allocated **4 bytes** |
| Signed length | `-1 > 64` check **passes**; memcpy receives 18446744073709551615 |
| Bounds-check cost | -O0 **2.21x** → -O1 **1.05x** → -O2 **1.02x** |
| ECB leak | identical plaintext blocks → **identical ciphertext blocks** |
| Fixed IV | identical plaintext → **identical ciphertext**; prefix differences binary-searchable |
| `Random` predictable | same-seed replay is **identical** |
| Broken access control | legitimate number 3, no injection, **parameterization irrelevant** |

**Three principles to take away**:

```
① The shape of injection: untrusted data handed to something that parses it
   → The shape of the defense: don't build strings, use structured interfaces

② Safe by default > safe when used correctly
   → The attacker needs one omission; the defender must be right everywhere

③ Defense in depth: assume the first line fails
   → The question isn't "will something go wrong" but "how bad is it when it does"
```

**Three conflicts with Chapter 57**:

| | Ch. 57 (Performance) | Ch. 58 (Security) |
|---|---|---|
| Hashing | faster is better | **password hashing: slower is better** (568,411x) |
| Comparison | return early | **refuse to return early** (constant time) |
| Checks | remove what you can | **keep bounds checks** (and the cost is now 1.02x) |

**Self-check**:

- [ ] I can explain why parameterized queries are not "automatic escaping."
- [ ] I know at least three reasons manual escaping fails.
- [ ] I can pick the correct encoding for HTML text, attributes, and JS strings.
- [ ] I know why password hashing is deliberately slow and can quantify the gap.
- [ ] I know when constant-time comparison is mandatory and why it's slower.
- [ ] I can state which class of bugs memory safety eliminates and which it doesn't.
- [ ] I know why parameterization can't stop IDOR and how RLS backstops it.
- [ ] I evaluate systems by asking "what's left after the first line fails."
- [ ] I know security bugs pass 100% of functional tests, so they need dedicated checks.

**Next chapter**: the code is written, tested, optimized, and hardened — and it's still on your machine. Chapter 59 covers **deployment**: we'll measure what each of the six languages must carry along to go from source to "runs on someone else's machine" (which directly determines container image size and startup time), quantify the difference between immutable infrastructure and configuration drift, reproduce the three concrete causes behind "it works on my machine," and lay out what blue-green, canary, and rolling deployments actually cost in **rollback speed** and resources. It's the book's final chapter — the one that puts everything the previous 58 built into users' hands.

---

## 19. Further Reading

- <a href="https://owasp.org/www-project-top-ten/" target="_blank" rel="noopener">OWASP Top 10</a> — the most widely used list of web security risks; most of this chapter's vulnerabilities appear on it.
- <a href="https://cheatsheetseries.owasp.org/cheatsheets/Cross_Site_Scripting_Prevention_Cheat_Sheet.html" target="_blank" rel="noopener">OWASP · XSS Prevention Cheat Sheet</a> — encoding rules organized by context; the authoritative source for §3.3's six contexts.
- <a href="https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html" target="_blank" rel="noopener">OWASP · Password Storage Cheat Sheet</a> — current recommended parameters for Argon2id/bcrypt/PBKDF2, updated as hardware improves.
- <a href="https://cwe.mitre.org/top25/" target="_blank" rel="noopener">CWE Top 25</a> — ranking of the most dangerous software weaknesses, with real CVEs per class.
- <a href="https://www.chromium.org/Home/chromium-security/memory-safety/" target="_blank" rel="noopener">Chromium · Memory Safety</a> — the original source of the "~70% of severe bugs are memory safety" statistic.
- <a href="https://www.rfc-editor.org/rfc/rfc9106.html" target="_blank" rel="noopener">RFC 9106 · Argon2</a> — the Argon2 specification, including the rationale for memory hardness.
- <a href="https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/CSP" target="_blank" rel="noopener">MDN · Content Security Policy</a> — the full directive reference; the tooling behind §4.5.
- <a href="https://www.postgresql.org/docs/current/ddl-rowsecurity.html" target="_blank" rel="noopener">PostgreSQL · Row Security Policies</a> — official RLS documentation; the backstop in §9.3.
- <a href="https://en.wikipedia.org/wiki/Timing_attack" target="_blank" rel="noopener">Wikipedia · Timing Attack</a> — the mechanism and historical cases behind §3.6.
