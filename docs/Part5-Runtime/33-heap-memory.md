# 第 33 章 · 堆内存

**简体中文** ｜ [English](./33-heap-memory.en-US.md)

---

> 栈的规矩是"函数返回帧必亡"（第 32 章），可真实程序里大量数据要**活过创造它的函数**：返回的字符串、缓存的对象、传给别的线程的数据。它们只有一个去处——**堆**：生命周期你说了算的自由市场。
>
> 但自由有账单。栈分配是一条 `sub sp` 指令；堆分配得**找空位**——实测 macOS 的 `malloc/free` 一对约 **15.8 ns**，是栈的几十倍。而且分配器还会"抹零"：实测 `malloc(1)` 给你 16 字节、`malloc(100)` 给你 112 字节——多给的部分叫**内部碎片**。
>
> 本章有一个反转值得先剧透：**托管语言的堆分配反而更快**——Java 实测每个对象 **2.87 ns**（TLAB 指针碰撞，本质就是堆版的 `sp` 移动），更夸张的是不逃逸的对象实测 **0.24 ns**：JIT 的逃逸分析直接把分配整个消除了。便宜的秘密是**费用转移**：GC 会整理堆，让空闲区永远连续，分配才能"指针加一下"——账单挪到了回收端（C# 实测：一千万个临时对象背后是 38 次 GC）。
>
> 至于"忘了还"的后果，本章用工具当场人赃并获：macOS 的 `leaks` 逐块列出 C++ 故意泄漏的三个 1 MB（`3 leaks for 3244032 total leaked bytes`）；Java 的静态 List 积累 50 MB，`System.gc()` 无能为力——而且实测占了 **100 MB**：1 MB 的数组在 G1 里是 humongous 对象，独占整个 2 MB region——**现代 GC 堆同样有内部碎片**，与 `malloc(1)` 给 16 字节遥相呼应。

## 1. 学习目标

本章结束后，你将能够：

- 说清**为什么需要堆**（生命周期超越函数）以及**为什么堆分配贵**（找空位、记账、线程同步）；
- 解释 `malloc` 的内部组织——**size class、空闲链表、arena**——并用实测（粒度取整、同级紧邻）验证；
- 讲出托管堆分配便宜的原因（**指针碰撞 + GC 整理**），并用三组实测（0.24 / 2.87 / 15.8 ns）说明"费用转移"；
- 区分**内部碎片与外部碎片**，并举出三个世界的实例：`malloc(1)`→16 字节、G1 humongous 100 MB、SQLite 删行页数不减；
- 用各语言的标准工具**定位泄漏**：`leaks`、`tracemalloc`、Runtime/heapUsed 观测——并说清 GC 语言里泄漏的真正形态。

---

## 2. 为什么会出现这个概念

### 栈装不下的三种数据

```java
String buildReport() {
    String report = "...";     // 若 report 只活在栈上，return 的一刻它就没了
    return report;             // 调用者拿到的是什么？
}
```

| 需求 | 栈为什么不行 |
|------|-------------|
| **返回值要活过函数** | 帧一弹，数据即亡（第 32 章实测悬垂指针） |
| **大小运行期才知道** | 栈帧尺寸编译期定死（第 32 章实测 `stack=2, locals=3`） |
| **多方共享、寿命说不准** | 栈是每线程私有的 LIFO——共享数据没法归任何一帧所有 |

### 堆的答案：按需租借的自由市场

```text
栈：物业统一分配的宿舍——住多久由函数决定，退房自动打扫
堆：自由市场租房——想租多大租多大、想住多久住多久
    代价①：租的时候要找房（分配慢）
    代价②：退租得自己办（忘了 = 泄漏；退两次 = 崩溃）
    代价③：租来退去，市场会破碎（碎片化）
```

> **一句话**：堆用"分配变贵 + 回收要人负责"换来了栈给不了的**自由生命周期**。本章讲清这笔交易的两侧：分配器如何压低找房成本（size class、指针碰撞），以及不退租（泄漏）与市场破碎（碎片）的真实代价。

---

## 3. 底层原理

### 为什么堆分配天生比栈贵

| 环节 | 栈 | 堆（malloc 路线） |
|------|-----|------------------|
| 找空间 | 无需找——栈顶就是 | 在空闲块中**检索**合适的一块 |
| 记账 | 无账可记 | 记下这块的大小、状态（free 时要用） |
| 线程安全 | 每线程一个栈，天然无争 | 堆是共享的——要加锁或分 arena |
| 回收 | `sp` 加回去，整帧一次清 | 逐块归还，还要考虑与邻居**合并** |

### malloc 的三大组织术

**① size class：按尺寸取整，分箱管理**（实测）：

```text
malloc(1)   实际得到 16 字节
malloc(17)  实际得到 32 字节
malloc(100) 实际得到 112 字节   ← 取整到最近的档位——多给的就是内部碎片
```

**② 同级紧邻，异级分区**（实测）：

```text
两个 malloc(32):   0x132605dc0 / 0x132605de0   相距 32 字节——肩并肩
两个 malloc(4096): 0x132809e00 / 0x13280ae00   相距 4096——另一个街区
```

同尺寸的块从同一片"格子田"里切，free 后回到本级的**空闲链表**等复用——检索快、碎片可控（第 31 章 SQLite 的 freelist 是同一思想）。

**③ arena 与大块直通**：多线程各用各的 arena 减少锁争抢；超大块（MB 级）绕过所有机制直接 `mmap` 向 OS 要页，free 时直接归还。

**分配的价格**（实测，加逃逸屏障防止编译器消除）：

```text
malloc/free 一千万对 32 字节：157.5 ms → 约 15.8 ns/对
```

### 托管堆的反转：分配比 malloc 快

Java / C# / V8 的新生代分配是**指针碰撞**（bump allocation）：

```text
malloc：在空闲链表里找一块合适的        →  ~16 ns（实测）
托管堆：alloc_ptr += size，完事          →  ~3 ns（Java 实测 2.87、C# 3.2）
```

**凭什么能这么草率？** 因为 GC 会**整理（压缩）堆**——存活对象被搬到一起，空闲区永远是完整的一大块，于是"找空位"永远不用找。这是运行时世界最漂亮的费用转移之一：

```text
malloc 路线：free 就地留洞 → 分配时必须绕洞找位 → 每次分配付检索费
GC 路线：   分配一路狂奔 → 定期停下来整理战场 → 费用集中付在回收端
```

**回收端的账单真实存在**（实测）：C# 分配一千万个临时对象，第 0 代 GC 悄悄跑了 **38 次**；Java 的一千万个临时对象过后堆占用变化 **-0.3 MB**——GC 边分配边收走了它们。

### 更快的分配是不分配（实测）

```text
Java 分配一千万个 Student：
  对象不逃逸（用完即弃）:  0.24 ns/个   ← JIT 逃逸分析：对象拆成寄存器变量，堆根本没参与
  对象逃逸（存进数组）:    2.87 ns/个   ← 真实的 TLAB 指针碰撞
```

第 31 章埋的伏笔（"对象一律在堆，JIT 逃逸分析除外"）在此兑现：**没逃出方法的对象，JIT 连分配都省了**。这也是"托管语言分配便宜"的完整版本——快的不只是分配器，还有"根本不去分配"的编译器。

### 碎片：两种破碎方式

```mermaid
flowchart LR
    subgraph 内部碎片["内部碎片：给多了"]
        A["要 1 字节<br/>给 16 字节<br/>（malloc 实测）"]
        B["1 MB 数组<br/>占 2 MB region<br/>（G1 实测）"]
    end
    subgraph 外部碎片["外部碎片：洞太碎"]
        C["free 留下的洞<br/>加起来够大<br/>却没有一块连续的"]
    end
```

- **内部碎片**是分配粒度的代价——三个世界实测同款：`malloc(1)`→16 字节、G1 的 1 MB 数组独占 2 MB region（下文 Java 节）、SQLite 删一半行页数一页不少（下文 SQL 节）；
- **外部碎片**是"就地 free"的代价——托管堆用压缩整理消灭它，malloc 世界只能靠合并邻居缓解；C# 的 LOH 因为大对象不搬家，是托管世界里外部碎片的最后据点。

---

## 4. JavaScript

V8 的堆：分配便宜、回收自动——但"引用还在"依旧是泄漏。

### 分配的价格（实测）

```javascript
for (let i = 0; i < 10_000_000; i++) {
  const s = { name: "s", score: i };
  sum += s.score;
}
```

```text
总耗时 6.2 ms，平均每个对象约 0.6 ns
```

新生代 bump 分配 + 引擎对短命对象的优化（V8 也做逃逸分析）——量级与 Java 的实测一致：**托管堆的分配不是性能顾虑，回收才是**。

### A/B 实验：同样的分配，留不留引用天差地别（实测）

```text
A. 一千万个临时对象，不留引用: 堆增长 0.0 MB    ← GC 边分配边收走
B. 一百万个对象全部留引用:     堆增长 61.0 MB   ← 引用在，GC 无能为力
```

**这一组 A/B 就是 JS 内存问题的全部纲领**：GC 从不失灵，失灵的是"你以为没用了、引用却还攥着"。

### 泄漏的日常形态

```javascript
const cache = new Map();                 // 全局 Map 只进不出
element.addEventListener("click", fn);   // 移除元素前忘了 removeEventListener
setInterval(poll, 1000);                 // 组件销毁后忘了 clearInterval
```

全是「B 实验」的变体。对"想缓存又不想阻止回收"的场景，用 `WeakMap` / `WeakRef`——键失去其他引用时条目自动消失。

> **注意事项**：Node 里排查泄漏的标准三步——`process.memoryUsage()` 看趋势（第 31 章实测）、`--inspect` 拿堆快照对比两个时间点、找"只增不减"的保留树。浏览器里同一套操作在 DevTools 的 Memory 面板。

---

## 5. Python

CPython 的分配器在你看不见的地方拼命**复用**——池、驻留、free list，全是"少去堆里跑一趟"的手段。

### 小整数池：-5..256 全程只有一份（实测）

```python
a, b = 256, 256
print(a is b)            # True——池里取的同一个
e = 200 + 56
print(e is a)            # True——算出来的 256 也是它
f_ = int("257")
print(f_ is c)           # False——池外对象各是各的
```

高频小整数预先造好、永不回收——省下海量的分配与引用计数操作（第 31 章实测过每个 `int` 24 字节起）。

### 字符串驻留：相同内容共享一份（实测）

```text
同一文件里两个 "student_name":       True（编译期常量合并）
运行期拼接的两份 'student name!':    False  ← 各是各的堆对象
sys.intern 之后:                     True   ← 手动驻留，全进程共享
```

注意第一行的 True **不是驻留**而是编译器把同一代码块里的相同字面量合并了——真正的运行期共享要靠 `sys.intern`（海量重复字符串做键时的省内存利器）。

### free list：刚死对象的坑位立刻复用（实测）

```python
x = [1, 2, 3]; addr = id(x); del x
y = [4, 5, 6]
print(id(y) == addr)     # True——pymalloc 把坑位留着
```

pymalloc 对 ≤512 字节的小对象维护分级池（与 malloc 的 size class 同构），高频类型（list、dict、frame）还有自己的专属 free list——**分配常常就是从池里捡现成的**。

### `tracemalloc`：内存花在了哪一行（实测）

```text
main.py:35: size=10171 KiB, count=10001, average=1041 B   ← 分配大户按行号点名
main.py:36: size=639 KiB, count=10001, average=65 B
```

> **注意事项**：`tracemalloc` 是定位 Python 内存问题的标准工具（标准库自带，开销可控）；比较两个 `take_snapshot()` 的差值即可找到"只增不减"的源头。别用 `gc.collect()` 掩盖问题——引用还在的对象，收集器也无能为力（与 JS 的 B 实验同理）。

---

## 6. Java

JVM 把"分配快"做到了极致，也把"泄漏"变成了纯粹的引用问题——还附赠了一个现代碎片的实测彩蛋。

### 分配的两级速度（实测）

```text
对象不逃逸:  0.24 ns/个   ← 逃逸分析把分配整个消除了！
对象逃逸:    2.87 ns/个   ← 真实的 TLAB 指针碰撞
```

**TLAB**（Thread-Local Allocation Buffer）：每个线程从堆里预租一段私有缓冲，线程内分配就是无锁的指针碰撞——多线程分配也不抢锁（第 31 章底层表的伏笔）。

### 代价在回收端（实测）

```text
一千万个临时对象前后，堆占用变化: -0.3 MB —— GC 收走了它们
```

### 泄漏的形态：引用还在，GC 无能为力（实测）

```java
static final List<byte[]> LEAK = new ArrayList<>();
for (int i = 0; i < 50; i++) LEAK.add(new byte[1024 * 1024]);   // "缓存"从不清理
```

```text
静态 List 积累 50 MB 后，System.gc() 也收不回: 堆增长 100.0 MB
```

Java 的泄漏三件套：**静态集合只进不出、监听器忘了注销、ThreadLocal 忘了 remove**——共同点都是"从 GC Root 出发仍可达"。

### 彩蛋：100 MB 而不是 50 MB——现代堆的内部碎片（实测）

```text
默认 region（2 MB）:              50 个 1 MB 数组 → 堆增长 100.0 MB
-XX:G1HeapRegionSize=4m 之后:     同样的代码     → 堆增长 50.0 MB
```

**G1 把堆切成等大的 region**（默认这台机器上 2 MB）；超过 region 一半的对象是 humongous 对象，**独占整段 region**——1 MB 的数组恰好过线，占 2 MB，浪费 50%。这与 `malloc(1)` 给 16 字节是同一件事在两个世界的呈现：**分配粒度决定内部碎片**。

> **注意事项**：生产上"批量 1 MB 左右的缓冲区"是 G1 的经典雷区（humongous 分配还会触发额外 GC）；要么调 region 大小，要么池化复用缓冲区。定位 Java 泄漏的标准路径：堆 dump（`jmap` / OOM 自动 dump）→ MAT/JProfiler 看支配树——找"最大的保留树根"。

---

## 7. C++

C++ 的堆是**全手动市场**：`malloc`/`new` 的每一笔账都看得见，也都得自己还。

### 分配器的账本（实测）

```text
malloc(1)   → 16 字节      malloc(17) → 32 字节      malloc(100) → 112 字节
两个 malloc(32) 肩并肩（相距 32 字节）；malloc(4096) 在另一个街区
malloc/free 一对 ≈ 15.8 ns（一千万次实测）
```

### `new` 与 `malloc` 的关系

```cpp
Student* s = new Student("小明", 90);
// new 做了两件事：① operator new（底层就是 malloc 类分配）拿内存
//                ② 在这块内存上调用构造函数（第 23 章）
delete s;
// delete 反过来：① 调用析构函数  ② operator delete 还内存
```

### 三种事故，一个根源：还房的义务在你

| 事故 | 代码 | 后果 |
|------|------|------|
| 泄漏 | `new` 后忘 `delete` | 内存只增不减（下文工具实测） |
| 双重释放 | `delete` 两次 | 分配器账本损坏——崩溃或更糟 |
| 使用已释放 | `delete` 后继续用指针 | 该内存可能已租给别人——静默数据损坏 |

### 人赃并获：`leaks` 工具（shell 实测）

```cpp
for (int i = 0; i < 3; ++i) {
    char* buf = (char*)malloc(1024 * 1024);   // 每轮泄漏 1 MB
    memset(buf, i, 1024 * 1024);
}
```

```text
$ leaks --atExit -- ./leaky
Process 93109: 3 leaks for 3244032 total leaked bytes.
  1 (1.03M) ROOT LEAK: <malloc in main 0x120008000> [1081344]
  1 (1.03M) ROOT LEAK: <malloc in main 0x130008000> [1081344]
  1 (1.03M) ROOT LEAK: <malloc in main 0x130110000> [1081344]
```

`leaks`（macOS 自带）在进程退出时扫描：**堆上还活着、却没有任何指针指向的块**，逐个点名。Linux 上同类工具是 Valgrind 与 ASan（`-fsanitize=address`），后者还能当场抓双重释放与 use-after-free。

> **注意事项**：现代 C++ 的答案是**别裸写 `new`/`delete`**——容器（`vector`/`string`）管好自己的内存，其余交给 RAII（第 37 章）与智能指针（第 38 章）。本章看清了"手动"的全部代价，正是为了理解那两章要自动化的是什么。

---

## 8. C#

CLR 的堆是"双城记"：小对象堆（SOH）飞速分配、勤于整理；大对象堆（LOH）是碎片的最后据点。

### LOH：85 KB 是一条国界线（实测）

```csharp
GC.GetGeneration(new byte[84_000]);   // 0 —— 普通小对象堆，从第 0 代出生
GC.GetGeneration(new byte[86_000]);   // 2 —— 直接进 LOH，按第 2 代对待
```

**为什么分家？** 移动式整理要搬对象，大对象搬家太贵——于是 LOH 默认**不压缩**：分配像 malloc 一样走空闲链表，也像 malloc 一样**会碎片化**（.NET 允许手动触发 LOH 压缩，但那是大动作）。

### 分配与它的账单（实测）

```text
分配一千万个对象: 31.6 ms（约 3.2 ns/个）        ← SOH 指针碰撞
再分配一千万个临时对象: 第 0 代 GC 发生了 38 次   ← 账单在这里
```

`GC.CollectionCount(n)` 是免费的观测哨——分配狂欢的背后，回收器在持续买单（每次 gen0 回收都要暂停线程，第 36 章展开）。

### C# 特有的减负手段

```csharp
Span<int> buf = stackalloc int[256];          // 小缓冲区上栈（第 31 章实测）
var pool = ArrayPool<byte>.Shared;            // 大缓冲区租用池
byte[] rented = pool.Rent(100_000);           // 从池里租（LOH 级别的常客）
try { /* 使用 */ } finally { pool.Return(rented); }
```

`ArrayPool` 是对 LOH 碎片的标准解法——**大缓冲区复用而非重复分配**，正对上一节 G1 humongous 的同款建议（池化）。

> **注意事项**：`IDisposable`/`using` 管的是**非托管资源**（文件句柄、连接），不是内存——忘了 `Dispose` 泄漏的是句柄；忘了断引用泄漏的才是内存。两种"泄漏"混为一谈是 C# 面试的经典陷阱。

---

## 9. SQL

数据库的"堆"就是页的集合（第 31 章），本章看它的**碎片与整理**——一个完整的实测闭环。

### 四步实测：碎片如何产生、如何整理

```sql
-- 一万行基线
step 1  page_count = 56, freelist = 0

-- 删除全部奇数行：每一页都还有活数据
DELETE FROM student WHERE id % 2 = 1;
step 2  page_count = 56, freelist = 0     ← 一页都没释放！

-- 再删光剩下的：整页清空
DELETE FROM student;
step 3  page_count = 56, freelist = 54    ← 空页进 freelist（可复用，不还 OS）

-- VACUUM：重建数据库，把洞挤掉
VACUUM;
step 4  page_count = 2, freelist = 0      ← 文件真正缩回去了
```

### 三个阶段对应三个概念

| 阶段 | 现象 | 对应本章概念 |
|------|------|-------------|
| 删一半行，页数不减 | 每页都有活数据，整页无法回收 | **页内碎片**（内部碎片的数据库版） |
| 删光后 freelist=54 | 空页留着等复用，文件不缩 | **空闲链表**（malloc 的 free 同款） |
| VACUUM 后缩到 2 页 | 重建库、紧凑排布 | **压缩整理**（GC 移动式回收同款） |

**一张表跨三个世界**：SQLite 的 freelist ↔ malloc 的空闲链表 ↔ G1 不做的"就地留洞"；`VACUUM` ↔ GC 的压缩整理 ↔ malloc 世界做不到的奢侈品（指针没法改，块搬不了家——这正是第 34 章指针的伏笔）。

> **工程提醒**：生产库的 `VACUUM`（PostgreSQL 的 `VACUUM FULL`、MySQL 的 `OPTIMIZE TABLE`）会锁表/重写文件，要挑窗口执行；SQLite 可用 `auto_vacuum` 增量归还。判断要不要做：`freelist_count` 占比高、或删除大量数据后文件迟迟不缩。

---

## 10. 五语言横向对比

### ① 堆分配机制对比

| 特性 | JavaScript | Python | Java | C++ | C# |
|------|-----------|--------|------|-----|-----|
| 分配方式 | 新生代 bump | pymalloc 池 + free list | **TLAB 指针碰撞** | **malloc 空闲链表** | SOH 指针碰撞 / LOH 空闲链表 |
| 实测速度 | ~0.6 ns/个 | —（对象复用居多，实测坑位复用） | **2.87 ns**（逃逸时）/ 0.24 ns（被消除） | **15.8 ns**/对 | 3.2 ns/个 |
| 回收责任 | GC | 引用计数 + GC | GC | **你** | GC |
| 碎片治理 | 移动式整理 | 池化缓解 | 压缩整理（humongous 例外，实测） | 合并空闲块（无法搬家） | SOH 整理 / LOH 不整理 |
| 泄漏形态 | 引用未断（实测 B 组 61 MB） | 引用未断 + C 扩展泄漏 | 引用未断（实测 100 MB） | **真泄漏**（实测 leaks 抓获） | 引用未断 + 句柄未 Dispose |
| 定位工具 | 堆快照 / `memoryUsage` | **`tracemalloc`**（实测） | 堆 dump + MAT | **`leaks` / ASan**（实测） | dotnet-gcdump |

### ② 钥匙实测：三级分配速度与费用转移

```text
栈:            ~0 ns      一条 sub sp（第 32 章）——回收也免费（帧弹出）
托管堆:        ~3 ns      指针碰撞（Java 2.87 / C# 3.2 实测）——费用转移给 GC（38 次/千万对象实测）
malloc 堆:     ~16 ns     空闲链表检索（实测）——费用当场付清，free 也由你负责
消除的分配:    0.24 ns    逃逸分析（实测）——最快的分配是不分配
```

**这张表是本章的中枢**：分配速度的差异不是"谁的代码写得好"，而是**费用在哪个环节结算**——malloc 每笔现结；托管堆刷卡记账，GC 定期还款；逃逸分析直接免单。

### ③ 两条设计分歧

**分歧一：free 之后的空间归谁管**

```text
就地留洞（malloc / SQLite freelist / LOH）：  free 快，但洞会碎——分配端付检索费
搬家整理（GC 压缩 / VACUUM）：               空闲永远连续——分配端指针碰撞，回收端付搬家费
```

搬家的前提是**能改指针**——GC 语言的运行时知道所有引用在哪，能全部改写；C++ 的裸指针散落各处没人登记，块永远搬不了家（第 34 章的核心伏笔）。

**分歧二：大对象要不要特殊对待**

```text
特殊化（C# LOH 实测 85 KB 线 / G1 humongous 实测 region 一半 / malloc 大块 mmap 直通）
——三个世界不约而同：大对象搬家太贵、池太粗，都单独开户
——代价也一致：大对象区是碎片重灾区（LOH 不压缩、humongous 50% 浪费实测）
```

### ④ 共同点与差异根源

**共同点**：所有分配器都按尺寸分级（size class / 池 / region）；释放的空间都倾向复用而非归还 OS（实测：pymalloc 坑位复用、SQLite freelist、malloc 同理）；大对象在哪都是特殊公民。

**差异根源**：

- **C++ 不许运行时碰用户的指针**——于是没有搬家、没有整理，就地留洞是唯一选择，分配端付全款；
- **托管语言掌握全部引用**——敢搬对象，才敢让空闲区永远连续、分配永远指针碰撞；
- **Python 的引用计数让对象"死得早"**——池与 free list 的复用率极高，分配常常退化为"捡回刚扔的"；
- **数据库把同一套问题搬到磁盘尺度**——页是它的 size class，freelist 是它的空闲链表，VACUUM 是它的压缩 GC。

---

## 11. 底层实现对比

| 运行时 | 分配器 | 关键细节 |
|--------|--------|---------|
| **V8**（JavaScript） | 新生代 semi-space bump 分配 + 老年代 free list | 新生代满了 Scavenger 整批搬存活者（朝生夕死假设）；大对象直进 large object space |
| **CPython** | pymalloc：≤512 B 走 arena→pool→block 三级池 | 高频类型专属 free list（实测坑位复用）；小整数池与驻留（实测）在分配之前就拦截了请求 |
| **JVM**（Java） | TLAB 指针碰撞（实测 2.87 ns）+ G1 region 化堆 | 逃逸分析可消除分配（实测 0.24 ns）；humongous 对象独占 region（实测 1 MB→2 MB） |
| **C++**（原生） | libmalloc：size class 分箱（实测 16/32/112）+ 空闲链表 + 大块 mmap | 实测 15.8 ns/对；free 只进空闲链表不还 OS（第 31 章原则）；无法移动已分配块 |
| **CLR**（C#） | SOH 三代 + 指针碰撞（实测 3.2 ns）；LOH 空闲链表 | LOH 阈值 85 KB（实测 84/86 KB 分野）；LOH 默认不压缩——托管世界的 malloc 飞地 |

**一个值得记住的分野**：

```text
能搬对象的堆（V8 / JVM / CLR-SOH）：   分配 = 指针碰撞，碎片被整理消灭
不能搬的堆（malloc / LOH / SQLite 页）：分配 = 检索空闲链表，碎片只能缓解
搬不搬得动，取决于运行时是否掌握全部指针——第 34 章从这里开始
```

---

## 12. 性能分析

### 分配成本全景（本章实测汇总）

| 方式 | 实测成本 | 备注 |
|------|---------|------|
| 逃逸分析消除 | 0.24 ns/个 | 最快的分配是不分配 |
| 托管堆指针碰撞 | 2.87–3.2 ns/个 | Java TLAB / C# SOH |
| malloc/free | 15.8 ns/对 | 空闲链表检索 + 记账 |
| 隐藏账单 | 38 次 gen0 GC/千万对象 | 托管堆的费用在回收端 |

### 但真正贵的往往不是分配本身

```text
① GC 压力：分配率决定 GC 频率——"每请求 new 一堆临时对象"的服务，CPU 的一成在跑 GC
② 缓存局部性：堆对象分散（第 31 章实测 class[] 7 倍内存）——遍历时 cache miss 连环
③ 碎片税：LOH/humongous 的浪费（实测 50%）不体现在任何 profiler 的"耗时"里，只体现在账单上
```

### 减负的通用手段（按性价比排序）

```text
1. 复用：对象池 / ArrayPool / 缓冲区复用——大对象尤其值得（LOH、humongous 双雷区实测）
2. 上栈：小而短命的数据用 struct / stackalloc / 值语义（第 31 章 7 倍密度）
3. 批量：一次 new int[100万] 远快于一百万次 new Integer（第 29/31 章两度实测）
4. 靠编译器：写"不逃逸"的代码（局部用完即弃），逃逸分析自会免单（实测 0.24 ns）
```

> ⚠️ 惯例提醒：分配优化只对热路径有意义。每秒几十次的业务代码里，一次 malloc 的 16 ns 连噪音都算不上——先用本章的观测工具（`GC.CollectionCount`、`tracemalloc`、堆快照）确认分配真的是瓶颈。

---

## 13. 工程实践

| 场景 | ✅ 推荐 | ❌ 不推荐 | 原因 |
|------|--------|----------|------|
| C++ 日常内存管理 | 容器 + RAII + 智能指针 | 裸 `new`/`delete` | 三种事故（泄漏/双释/悬垂）全防 |
| 长生命周期缓存 | 带上限 + 淘汰策略（LRU） | 无限增长的 Map/List | 实测：静态集合就是泄漏（100 MB） |
| 大缓冲区（≥85 KB / ~1 MB） | 池化复用（ArrayPool 等） | 每次新分配 | LOH 碎片 + humongous 浪费（双实测） |
| Python 海量重复字符串键 | `sys.intern` | 放任重复 | 驻留实测：全进程共享一份 |
| 事件/回调注册 | 成对写注销逻辑 | 只注册不注销 | 监听器是三大泄漏源之一 |
| 排查"内存只增不减" | 快照对比（tracemalloc/堆 dump） | 盲调 `gc()`/加内存 | 引用在，GC 无能为力（双语言实测） |
| C++ 上线前 | ASan/leaks 跑一遍测试 | 靠肉眼 review | 实测：leaks 逐块点名，零漏网 |
| 高分配率服务 | 观测 GC 频率并降分配 | 只调 GC 参数 | 38 次/千万对象——源头在分配端 |

### 判断口诀

```text
活不过函数        → 栈（编译器还可能帮你免单）
活得久、量不大    → 堆，正常 new——别过早优化
大块 / 高频 / 热路径 → 池化、复用、批量——分配器的三个雷区都在这
```

---

## 14. 最佳实践

- **需要活过函数才上堆**——第 31 章的口诀在分配端同样成立；小而短命的数据留给栈和逃逸分析。
- **缓存必须有边界**：上限、TTL、LRU 三选一——无界缓存与泄漏只差一个时间维度（实测 100 MB 收不回）。
- **大缓冲区一律池化**：85 KB（LOH）与 region 一半（humongous）两条线附近的分配最伤（双实测）。
- **成对思维**：`new`/`delete`、注册/注销、`Rent`/`Return`、`start`/`stop`——泄漏的本质是"只做了前一半"。
- **工具先行**：`leaks`/ASan（C++）、`tracemalloc`（Python）、堆快照（JS/Java/C#）——本章每种工具都实测过，别靠猜。
- **看懂费用转移**：托管语言"分配便宜"的另一面是 GC 账单（实测 38 次）——降低分配率是唯一同时省两头的办法。
- **别把 `Dispose`/`close` 当内存管理**：它们管句柄不管堆——两种泄漏分开排查。
- **数据库定期看 freelist**：`freelist_count` 高企时安排 `VACUUM`/`OPTIMIZE`（实测 56 页缩回 2 页）。

---

## 15. 常见坑

**坑 1 · C++ 忘了 free / delete**（实测人赃并获）

```text
$ leaks --atExit -- ./leaky
Process 93109: 3 leaks for 3244032 total leaked bytes.
```

**如何避免**：现代 C++ 不裸写 `new`；CI 里用 ASan 跑测试——工具抓泄漏是确定性的，肉眼不是。

**坑 2 · 以为 GC 语言不会泄漏**（双实测反驳）

```text
Java 静态 List：100 MB，System.gc() 无能为力
JS  保留引用：  61 MB，GC 无能为力
```

**如何避免**：GC 只回收"不可达"的对象——从 Root 可达的"僵尸缓存"是 GC 语言泄漏的唯一形态，也是全部形态。

**坑 3 · 大对象随手分配**（双实测代价）

```text
C# 86 KB 数组直进 LOH（不压缩，会碎片化）
Java 1 MB 数组独占 2 MB region（50% 浪费）
```

**如何避免**：接近这两条线的缓冲区用池（ArrayPool / 自建池）；或者一次分配、长期复用。

**坑 4 · 在循环里反复 malloc/new 小对象**

```text
malloc/free 15.8 ns/对（实测）——一千万次就是 157 ms 纯开销，还有 GC/碎片的隐性账
```

**如何避免**：循环外分配、循环内复用；容器 `reserve` 预留容量（第 17 章扩容的教训）。

**坑 5 · Python 把 `is` 当 `==` 用**

```python
a = 256; b = 256; a is b   # True——小整数池的巧合
c = 257; d = int("257"); c is d   # False！（实测）
```

**如何避免**：池和驻留是**优化**不是语义——值相等用 `==`，`is` 只用于 `None` 等单例判断。本章实测正是"为什么 `is` 时灵时不灵"的答案。

**坑 6 · `DELETE` 了数据，磁盘没变大**

```text
实测：删一半行 page_count 纹丝不动（页内碎片）；删光也只是进 freelist（不还 OS）
```

**如何避免**：理解三层——行删除留页内洞、整页进 freelist、`VACUUM` 才缩文件；容量规划按"高水位"算。

**坑 7 · 双重释放与使用已释放**（C++）

```cpp
delete p;
delete p;      // 分配器账本损坏——可能立刻崩，也可能埋雷到十分钟后
use(*p);       // 这块内存可能已租给别人——静默数据损坏
```

**如何避免**：`delete` 后置空只是止痛药；根治是所有权唯一化——`unique_ptr` 让"谁负责 delete"由类型系统保证（第 38 章）。

---

## 16. 面试题

**基础**

1. 为什么需要堆？哪三类数据栈装不下？
2. `new` 和 `malloc` 的关系是什么？`delete` 和 `free` 呢？
3. 内部碎片和外部碎片有什么区别？各举一个本章的实测例子。

**中级**

4. **malloc 为什么比栈分配慢？它内部的 size class 和空闲链表各解决什么问题？**
5. 托管语言的堆分配为什么反而比 malloc 快？"费用转移"转移到了哪里？
6. **GC 语言里的"内存泄漏"是什么形态？为什么 `System.gc()` 救不了？**

**高级**

7. **为什么 GC 堆能做压缩整理，而 malloc 的堆不能搬动任何块？这与指针有什么关系？**
8. C# 的 LOH 和 G1 的 humongous region 是同一个问题的两种答案——问题是什么？两者又各付了什么代价？
9. 逃逸分析如何做到 0.24 ns 的"分配"？什么样的代码能享受这个待遇？

---

## 17. 练习

**基础**

1. 用 `malloc_size`（或 Linux 的 `malloc_usable_size`）测你平台的分配粒度表：1 到 1024 字节，找出所有档位。
2. 在 Java/C#/JS 里复现 A/B 泄漏实验：同样的分配，留引用 vs 不留引用，观测堆占用。
3. 用 `tracemalloc` 找出一段 Python 代码里分配最多的三行。

**提高**

4. **复现三级速度实测**：malloc/free、托管堆分配、逃逸分析消除——注意加逃逸屏障/让对象逃逸，防止编译器骗你。
5. 复现 G1 humongous 实测：50 个 1 MB 数组的堆占用，对比默认与 `-XX:G1HeapRegionSize=4m`。
6. 写一个故意泄漏的 C++ 程序，分别用 `leaks`（macOS）或 ASan（`-fsanitize=address`）抓出每一块。

**挑战**

7. 实现一个玩具版固定大小分配器：一次 `malloc` 一大块，内部用空闲链表切分/回收 32 字节小块，对比它与直接 malloc 的速度。
8. 在 C# 里用 `ArrayPool` 改写一段高频分配 100 KB 缓冲区的代码，用 `GC.CollectionCount` 对比改写前后的 GC 次数。
9. 对 SQLite 复现四步碎片实测，然后打开 `auto_vacuum=INCREMENTAL` 再跑一遍，解释 freelist 行为的变化。

---

## 18. 本章总结

**一句话总结**：堆用"分配变贵、回收要人负责"换来自由生命周期——malloc 路线按 size class 分箱、空闲链表检索（实测 15.8 ns/对，`malloc(1)` 给 16 字节），托管路线靠 GC 压缩让分配退化为指针碰撞（实测 2.87 ns，逃逸分析下 0.24 ns），**费用从分配端转移到回收端**（实测 38 次 GC/千万对象）；而"忘了还"与"还不干净"各有实测铁证——`leaks` 逐块点名 3 MB、静态 List 的 100 MB 连 `System.gc()` 都无能为力（且因 humongous 多付 50%）、SQLite 删行不缩页直到 `VACUUM`——泄漏与碎片，正是自由市场的两种税。

**核心知识点**

- **堆的存在理由**：活过函数、运行期定大小、跨方共享——栈的三个做不到。
- **malloc 三件套**（实测）：size class 取整（1→16、100→112）、同级紧邻异级分区、空闲链表复用——15.8 ns/对。
- **托管堆的反转**（实测）：指针碰撞 2.87 ns、逃逸分析 0.24 ns——快的前提是 GC 会整理、空闲永远连续。
- **费用转移**（实测）：一千万临时对象 = 38 次 gen0 GC；堆占用 -0.3 MB——账单在回收端。
- **泄漏的两种世界**（实测）：C++ 真泄漏（leaks 抓获 3 块）；GC 语言"引用还在"（Java 100 MB / JS 61 MB）。
- **碎片的三个世界**（实测）：`malloc(1)`→16 字节、G1 humongous 1 MB→2 MB、SQLite 删行页数不动——内部碎片同源；LOH 不压缩是外部碎片据点。
- **搬得动与搬不动**：GC 掌握全部引用所以能压缩；malloc 世界指针散落无人登记——第 34 章的入口。

**检查清单**

- [ ] 我能说清堆分配比栈贵在哪三个环节。
- [ ] 我能解释托管堆"分配便宜"的机制与代价。
- [ ] 我能区分两种碎片，并各举一个实测例子。
- [ ] 我知道 GC 语言泄漏的唯一形态，以及各语言的定位工具。
- [ ] 我知道大对象在三个运行时里的特殊待遇与雷区。

**下一章预告**：本章反复出现一个隐身角色——**指针**。malloc 返回的是它、悬垂与双重释放伤的是它、GC 能压缩堆靠的是"掌握所有指针"、而 malloc 的堆搬不了家也是因为它散落无踪。第 34 章正面直视这个 C/C++ 的核心概念：地址运算的全部能力（`*`、`&`、指针算术、函数指针）、它与数组的暧昧关系、空指针/野指针/悬垂指针三大事故的解剖——以及为什么其余四门语言集体决定**把指针藏起来**，藏起来之后又各自留了什么后门。

---

## 19. 延伸阅读

- <a href="https://en.wikipedia.org/wiki/C_dynamic_memory_allocation" target="_blank" rel="noopener">Wikipedia：C dynamic memory allocation</a> — malloc/free 的标准描述与实现综述。
- <a href="https://en.wikipedia.org/wiki/Fragmentation_(computing)" target="_blank" rel="noopener">Wikipedia：Fragmentation</a> — 内部/外部碎片的概念定义。
- <a href="https://sourceware.org/glibc/wiki/MallocInternals" target="_blank" rel="noopener">glibc Wiki · Malloc Internals</a> — glibc 分配器（arena/bin/chunk）内部结构的权威文档。
- <a href="https://docs.oracle.com/en/java/javase/17/gctuning/" target="_blank" rel="noopener">Oracle · HotSpot GC Tuning Guide</a> — TLAB、G1 region 与 humongous 对象的官方说明。
- <a href="https://learn.microsoft.com/en-us/dotnet/standard/garbage-collection/large-object-heap" target="_blank" rel="noopener">Microsoft Learn · The large object heap</a> — LOH 阈值、不压缩策略与调优的官方文档。
- <a href="https://learn.microsoft.com/en-us/dotnet/api/system.buffers.arraypool-1" target="_blank" rel="noopener">Microsoft Learn · ArrayPool&lt;T&gt;</a> — 大缓冲区池化的标准 API。
- <a href="https://docs.python.org/3/library/tracemalloc.html" target="_blank" rel="noopener">Python 文档 · tracemalloc</a> — 按行定位分配来源的标准库工具。
- <a href="https://v8.dev/blog/trash-talk" target="_blank" rel="noopener">V8 Blog · Trash talk</a> — V8 堆结构与回收器（Scavenger/Orinoco）的官方介绍。
- <a href="https://www.sqlite.org/lang_vacuum.html" target="_blank" rel="noopener">SQLite 文档 · VACUUM</a> — 数据库重建与空间回收的官方说明。
