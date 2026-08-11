# 第 46 章 · 数据库

**简体中文** ｜ [English](./46-database.en-US.md)

---

> Part 5 把内存讲透了，Part 6 把并发讲透了——可是有三件事，它们联手也做不到：**进程死了数据还在**、**两个进程同时改一份数据不会乱**、**十万行里找一行不用扫十万行**。这三件事的工业级答案，就叫数据库。
>
> 本章的**钥匙实验**是用「文件 + 手写代码」和数据库各实现一遍同样的需求，把差距一项一项量出来。结果比预期更狠：**持久化**有三档价格（实测 write 1.86 μs → fsync 26 μs → `F_FULLFSYNC` 4399 μs，每档一个数量级，且 macOS 的 fsync **不防掉电**）；**原子性**上，文件版转账崩在半路留下 `'id=1,balance=40\nid=2,bal'`——钱凭空消失、格式也毁了，而数据库回滚后分毫不差；**并发**上最反直觉的是 JS：50 个协程自增一个文件计数器，**单线程、没有数据竞争，结果是 1**——丢更新的元凶从来不是多线程，而是「读-改-写不原子」；**查询**上，十万行找一行，文件顺序扫描比 sqlite 主键查询慢 **551 倍**。
>
> 过程中还撞出一个五语言级别的发现：**同一个名字叫 fsync 的 API，五个运行时给了你两种不同的承诺**。C/Python/Java 的 fsync 是裸系统调用（26 μs，掉电可能丢）；而 Node 的 `fsyncSync` 实测 4.0 ms、C# 的 `Flush(true)` 实测 4.4 ms——libuv 和 .NET 在 macOS 上把它们**悄悄升级成了 `F_FULLFSYNC`**。这同时解开了第 43 章的悬案：SQLite 的 `synchronous=FULL` 只慢 1.5x，是因为 macOS 上它走的是 `F_BARRIERFSYNC` 档，动真格要 `PRAGMA fullfsync=ON`。
>
> 数据库怎么把这么贵的持久化做到可用？答案是 **WAL（预写日志）**：实测 100 条记录逐条 `F_FULLFSYNC` 要约 440 ms，攒一批只刷一次是 **4.1 ms——快 107 倍**。Java 版更是亲手写了一个 60 行的 TinyDB（追加日志 + 内存索引 + CRC32 校验和），注入半条损坏记录后重启，**恢复协议正确识别并截断了它**——你会亲眼看到：把持久化、原子性、并发控制、索引、查询这五件事的手写代码合起来，就是一个数据库的雏形。

## 1. 学习目标

学完本章，你将能够：

- 说出数据库对文件的**五层承诺**（持久化、原子性、一致性、隔离性、查询引擎），每层都能引用一个实测数据；
- 画出**持久化的三档阶梯**（页缓存 → 磁盘缓存 → 存储介质）并写出每档的实测价格与崩溃语义；
- 解释为什么**单线程也会丢更新**（JS 实测 50 个协程自增结果为 1），并说出事务如何根治它；
- 复述 **WAL 的三步协议**与组提交为什么能把落盘成本降 107 倍（实测）；
- 亲手实现一个最小的崩溃恢复协议（`[长度][数据][校验和]` + 重放截断，Java 版实测可用）。

---

## 2. 为什么会出现这个概念

### 前六个 Part 留下的三个洞

```text
洞一（持久化）: 第 31 章说变量住在内存——进程一死全没了；写文件呢？本章实测: write 成功 ≠ 落盘
洞二（并发）:   第 41 章的锁只管【本进程内的线程】——两个进程改同一份数据，锁不到
洞三（查询）:   第 20 章的哈希表 O(1) 查找只活在内存里——磁盘上的十万行只能从头扫
```

**这三个洞用文件 + 手写代码补起来是什么代价？本章逐项实测了：**

| 需求 | 文件 + 手写代码 | 数据库 | 差距 |
|------|---------------|--------|------|
| 十万行里查 20 次 | 399.8 ms（每次扫全文件） | **0.726 ms**（主键 B 树） | **551x** |
| 100 条记录真落盘 | ~440 ms（逐条 `F_FULLFSYNC`） | **4.1 ms**（WAL 组提交） | **107x** |
| 2 进程 × 200 次自增 | 280（**丢 120 次更新**） | **400** ✓ | 正确 vs 错误 |
| 转账崩在半路 | `'id=1,balance=40\nid=2,bal'`（钱消失+格式毁） | 回滚，分毫不差 | 有救 vs 没救 |
| 换个统计口径 | 重写 15 行解析+分组 | 改一句 SQL | 天壤之别 |

### 一句话定义

```text
数据库 = 替你在磁盘上把这五件事做到工业级的软件：
  D 持久化（写了就不丢）    A 原子性（要么全做要么全不做）
  C 一致性（坏数据进不来）  I 隔离性（并发像串行一样正确）
  + 一个声明式查询引擎（说「要什么」，不管「怎么找」）
```

> **一句话总结**：文件给你的只是「一段能读写的字节」；数据库在这段字节之上，把 Part 5 的内存问题（数据放哪）和 Part 6 的并发问题（谁能动它）在**磁盘**这个介质上重新解决了一遍——而且解决得比你手写好得多（本章每一节都是证据）。

---

## 3. 底层原理

### 钥匙实验一：持久化的三档价格

「写文件」远不是一个动作，而是一段旅程。数据从你的代码到永不丢失，要过四站：

```mermaid
flowchart LR
    A["用户态缓冲<br/>(FILE*/BufferedWriter)"] -->|"flush / write()"| B["内核页缓存<br/>(page cache)"]
    B -->|"fsync()"| C["磁盘写缓存<br/>(drive cache)"]
    C -->|"F_FULLFSYNC"| D["存储介质<br/>(NAND/盘片)"]
```

**C++ 实测每一站的价格**（Python 版数字几乎相同）：

| 停在哪一站 | 系统调用 | 实测（每次） | 进程崩溃 | 内核崩溃 | 突然断电 |
|-----------|---------|------------|:---:|:---:|:---:|
| 页缓存 | 只 `write()` | **1.86 μs** | ✓ 在 | ✗ 丢 | ✗ 丢 |
| 磁盘缓存 | `write` + `fsync` | **26.2 μs** | ✓ 在 | ✓ 在 | ⚠️ 可能丢 |
| 存储介质 | `write` + `F_FULLFSYNC` | **4398.6 μs** | ✓ 在 | ✓ 在 | ✓ 在 |

**三个要点**：

```text
① write() 返回成功只说明数据进了【内核】——它在页缓存里，断电就没了
② macOS 的 fsync 只保证「交给磁盘」，不保证「磁盘写进介质」（POSIX 允许！）
   → 掉电时磁盘写缓存里的数据仍可能丢——所以 SQLite 专门用 F_FULLFSYNC
③ 每往下走一站，价格涨一个数量级: 1.86 → 26 → 4399 μs
```

### 五语言发现：同一个 fsync，两种承诺

**这是本章最意外的实测结果**。五个运行时各自的「fsync」API：

| 运行时 | API | 实测（每次） | 实际做的事 |
|--------|-----|------------|-----------|
| C/C++ | `fsync(fd)` | 26.2 μs | 裸 fsync（磁盘缓存档） |
| Python | `os.fsync(fd)` | 27.8 μs | 裸 fsync |
| Java | `FileDescriptor.sync()` | ~17 μs | 裸 fsync |
| **Node** | `fs.fsyncSync(fd)` | **4.0 ms** | **libuv 升级成 `F_FULLFSYNC`** |
| **C#** | `FileStream.Flush(true)` | **4.4 ms** | **.NET PAL 升级成 `F_FULLFSYNC`** |

```text
libuv 的源码注释写得明白: Apple 的 fsync 不刷磁盘写缓存，
所以它先试 F_FULLFSYNC，失败退 F_BARRIERFSYNC，再失败才退裸 fsync
.NET 的 PAL 层做了同样的选择
→ C/Python/Java 给你【便宜但不防掉电】的 fsync
  Node/.NET 给你【贵 150 倍但真落盘】的 fsync
→ 你的运行时已经替你做了持久性决策——大多数人根本不知道
```

**第 43 章悬案就此了结**：当时实测 SQLite `synchronous=FULL` 只比 `OFF` 慢 1.5x，远小于预期。谜底：macOS 上 SQLite 的 FULL 档默认走 `F_BARRIERFSYNC`（介于两档之间的屏障写）；要动真格的 `F_FULLFSYNC`，得另开 `PRAGMA fullfsync=ON`——那才是本章实测的 4.4 ms 档。

### 钥匙实验二：WAL——把贵的事变便宜

真落盘一次 4.4 ms，那每秒岂不是只能提交 200 多个事务？**数据库的答案是组提交（group commit）**：

```text
C++ 实测:
  100 条记录逐条 F_FULLFSYNC: 约 440 ms
  100 条记录攒一批刷一次:     4.1 ms      ← 快 107x
```

**WAL（Write-Ahead Log，预写日志）的三步协议**：

```text
① 把「我打算改什么」【追加】写进日志文件，fsync 一次
② 向用户返回「提交成功」          ← 注意: 数据文件此刻还没动！
③ 之后慢慢把改动应用到数据文件（checkpoint）

崩溃后重启: 重放日志里已提交的事务，丢弃写了一半的
           （靠每条记录的【校验和】识别半条记录——Java 版亲手实现了）
```

**为什么先写日志反而快**：

```text
追加写日志 = 顺序 I/O（磁盘最喜欢的访问模式）
直接改 B 树 = 随机 I/O（东一块西一块）
→ 先顺序写日志换取立刻返回，再后台慢慢做随机写——两头占尽
→ N 个并发事务的 fsync 还能合并成一次（组提交，实测 107x 的来源）
```

### 钥匙实验三：最小崩溃恢复协议（Java 版 60 行实现）

```text
记录格式: [4 字节长度][数据][8 字节 CRC32 校验和]
恢复算法: 从头逐条读 → 校验和对得上就重建索引 → 对不上说明是半条 → 截断丢弃

实测: 注入半条损坏记录后文件 77 字节
     重启恢复 → 正确识别 → 截断到 68 字节
     完整记录 get(42) = zhang,40 ✓   半条记录 get(44) = null ✓
```

**这 60 行就是 Bitcask 模型**（Riak 数据库的存储引擎）：追加日志做持久化、内存哈希做索引、校验和做崩溃恢复。数据库没有魔法——只有把这些朴素协议做到极致的工程。

### 为什么并发必须交给数据库

**四组实测，同一个结论**：

```text
JS    : 50 个协程自增文件计数器 → 结果 1（单线程也全丢！）
Python: 2 进程 × 200 次自增     → 280，丢 120 次
Java  : 2 线程 × 150 次自增     → 149，丢 151 次
C#    : 2 线程 × 150 次自增     → 174，丢 126 次 + 26 次读到【空文件】
```

```text
元凶只有一个: 「读 → 改 → 写」是三步，任何人都可能插在中间
第 41 章的锁能救线程，但救不了【进程】；能救进程的 Mutex 救不了【另一台机器】
→ 数据库的事务把三步压成一步原子操作，且天生跨进程、跨机器
→ 实测: 同样的并发压力下 sqlite 的 UPDATE n = n + 1 精确得到 400 和 50
```

---

## 4. JavaScript

Node 的实测贡献了本章两个最反直觉的结果。

### 单线程也会丢更新（实测）

```javascript
const incr = async () => {
  const v = Number(await fsp.readFile(p, 'utf8'));   // 读
  await sleep(1);                                    // ← 一次 await = 一次让出（第 43 章）
  await fsp.writeFile(p, String(v + 1));             // 写
};
await Promise.all(Array.from({ length: 50 }, incr));
```

```text
50 个并发自增，期望 50，实际 1
```

**没有线程、没有数据竞争，照样全丢**——50 个协程都在任何人写回之前读到了 0。第 40 章说数据竞争需要多线程，这里证明**丢更新不需要**：只要「读-改-写」中间有让出点（`await`），单线程一样丢。事件循环救不了你，事务才能：

```text
同样 50 个并发自增走 UPDATE counter SET n = n + 1: 实际 50 ✓
→ 「读旧值、加一、写回」被压进数据库里的一条原子语句，中间没人能插队
```

### fsyncSync 的真实身份（实测）

```text
只 writeSync 2000 次: 14 ms；write+fsyncSync 200 次: 808 ms
→ 每次「fsync」4.0 ms —— 可裸 fsync 系统调用只要 ~26 μs
→ 差的 150x 是 libuv 干的: macOS 上它把 fsync 悄悄升级成 F_FULLFSYNC
```

**libuv 替你选了「贵但正确」**。这意味着 Node 里每次 `fs.fsyncSync` 都是真落盘——写日志系统时不知道这一点，会把性能问题查错方向。

### node:sqlite：Node 22.5+ 自带数据库（实测）

```javascript
const { DatabaseSync } = require('node:sqlite');
const db = new DatabaseSync('app.db');
```

```text
转账中途崩溃后: [{"id":1,"balance":100},{"id":2,"balance":100}]   ← 回滚，分毫不差
主键点查 1000 次: 9 ms（每次 9 μs，B 树直达）
全表拉到 JS 再建 Map: 109 ms + 46 MB 堆
```

**注意 API 是同步的**——sqlite 的点查快到 9 μs，不值得为它付出线程池往返（第 43 章实测 libuv 池的排队）。这也是 better-sqlite3 的哲学。

**最后一行实测是个重要习惯的量化**：「把十万行拉到 JS 里自己找」花 109 ms 和 46 MB 堆；「让数据库找完只回传一行」9 μs。**把计算送到数据那边，别把数据搬到计算这边**——第 47 章 SQL 的全部意义。

> **注意**：`node:sqlite` 在 22.x 仍标实验性（打印警告，可用 `process.removeAllListeners('warning')` 静音）；服务器数据库（pg/mysql2）走网络所以是异步 API（第 42 章）；嵌入式与服务器数据库的分界就是第 39 章的进程边界——库在你进程里，服务器在别的进程里。

---

## 5. Python

Python 版是钥匙实验的主战场——「文件 + 手写代码 vs 数据库」的正面对决。

### 三档持久化（实测）

```text
只 write() 2000 次:            6.9 ms（   3.5 μs/条）← 数据还在【页缓存】
write+fsync 200 次:            5.6 ms（  27.8 μs/条）← 到了【磁盘缓存】
write+F_FULLFSYNC 50 次:     201.9 ms（4037.8 μs/条）← 真正落到【存储介质】
```

Python 是五语言里唯一能**直接摸到全部三档**的：`os.write` / `os.fsync` / `fcntl.fcntl(fd, fcntl.F_FULLFSYNC)`——标准库把 macOS 的这个 fcntl 常量都封装好了。

### 崩溃对决（实测）

```text
文件版: 改写到一半进程被杀 → 文件内容 'id=1,balance=40\nid=2,bal'
        → 甲扣了 60，乙的记录只剩半行——钱凭空消失，文件格式也毁了
数据库: 事务中途抛异常 → [(1, 100), (2, 100)]
        → 自动回滚，两人余额都是 100
```

`with con:` 是 Python 操作 sqlite 最该养成的习惯——进入即开事务，正常退出提交，异常退出回滚（第 37 章 RAII 的数据库版）。

### 跨进程对决（实测）

```text
文件版: 2 进程 × 200 次自增，期望 400，实际 280（丢了 120 次更新）
sqlite: 2 进程 × 200 次自增，期望 400，实际 400 ✓
```

文件版还撞见了另一个现象：一个进程 `open(path, "w")` 截断文件、还没写入的瞬间，另一进程读到了**空字符串**（示例里捕获重试了它）。`"w"` 模式的「先清空再写」对读者不是原子的——C# 版同样撞见（26 次）。

### 查询与聚合对决（实测）

```text
十万行里查 20 次:
  文件顺序扫描: 399.8 ms（每次 O(n) 读 100000 行）
  sqlite 主键查: 0.726 ms（B 树 O(log n)）→ 快 551x（一次性建库 106 ms）

换个问题——每个 score 的人数最多是多少:
  文件版手写聚合: 173.7 ms + 15 行解析分组代码（换个需求再写 15 行）
  SQL 一句 GROUP BY: 19.0 ms（改一句话）
```

> **注意**：`sqlite3` 是标准库自带（本章所有 Python 实测零依赖）；连接默认非自动提交模式，忘了 `commit()` 数据就没写进去（最常见新手坑）；`timeout` 参数决定「数据库被别人锁着时等多久」，默认 5 秒；多进程写 sqlite 靠它内部的文件锁排队——实测正确但吞吐有限，高并发写要换服务器数据库。

---

## 6. Java

Java 版干了本章最有野心的事：**手写一个数据库**。

### TinyDB：60 行的 Bitcask（实测可用）

```java
static class TinyDB implements AutoCloseable {
    private final RandomAccessFile log;                 // 追加日志 = 持久化
    private final Map<String, Long> index = new HashMap<>();  // key → 日志偏移 = 索引

    void put(String key, String value) throws Exception {
        byte[] payload = (key + "=" + value).getBytes(UTF_8);
        CRC32 crc = new CRC32(); crc.update(payload);
        long pos = log.length();
        log.seek(pos);
        log.writeInt(payload.length);                   // [长度][数据][校验和]
        log.write(payload);
        log.writeLong(crc.getValue());
        index.put(key, pos);                            // 旧值不删，索引不再指向它而已
    }
}
```

```text
put(42) 两次后 get(42) = zhang,40   ← 读到新版本
日志里旧版本还在——追加写从不回头改，全是顺序 I/O，这正是 WAL 的写法
```

### 崩溃恢复实测

```text
注入半条记录（只写了 5 字节就"断电"）→ 文件 77 字节
重启 recover(): 逐条校验 CRC32 → 半条对不上 → 截断到 68 字节
get(42) = zhang,40（完整的都在）  get(44) = null（半条被丢弃）
```

**`[长度][数据][校验和]` + 重放截断，就是最小可用的崩溃恢复协议**——真数据库的 WAL 恢复是它的工业级版本。

### 从 TinyDB 到真数据库的距离

```text
已有: 持久化(追加日志) + 崩溃恢复(校验和) + 点查(内存索引)
还缺: 范围查询(要 B 树，第 49 章)   事务(第 48 章)   并发控制(第 50 章)
      SQL(第 47 章)   日志压缩(不然文件无限涨)   网络协议   权限……
→ 每补一项都是几千行起步——「用数据库」就是把这些全部外包
```

### Java 的两档持久化（实测）

```text
只 write 2000 次: 2.6 ms；write+sync 200 次: 3.5 ms（每次落盘贵 14x）
→ FileDescriptor.sync() / FileChannel.force() 都是裸 fsync（~17 μs 档）
→ F_FULLFSYNC 在纯 Java 里【够不到】（要 JNI）——JVM 无法独立承诺掉电安全
```

> **注意**：生产用 JDBC（`java.sql.*` 一套接口、各家驱动）+ HikariCP 连接池（第 45 章实测过它的公式）；`DriverManager.getConnection` 每次真建连接，绝不能出现在请求路径上；本例不引第三方 jar 故用文件演示原理——JDBC 驱动本身就是个 jar 依赖。

---

## 7. C++

C++ 版承包了本章最重要的一组数字：**三档持久化的精确价格**。

### 三档实测

```text
5000 次 write():          9.3 ms（   1.86 μs/次）
300 次 write+fsync:       7.9 ms（   26.2 μs/次）
50 次 write+F_FULLFSYNC: 219.9 ms（ 4398.6 μs/次）
→ 三档对比: 1.86 → 26 → 4399 μs/次（每档一个数量级）
```

```cpp
write(fd, rec, len);              // ① 页缓存: 进程崩溃数据在（内核持有），断电就没
fsync(fd);                        // ② 磁盘缓存: 内核崩溃也在，掉电仍可能丢（macOS 语义）
fcntl(fd, F_FULLFSYNC);           // ③ 介质: 掉电也不丢——SQLite 的 fullfsync 选项走这里
```

**POSIX 允许 fsync 不刷磁盘写缓存**——macOS 就是这么做的（Apple 的 man page 明说）。Linux 上 fsync 通常含刷盘语义，但最终取决于磁盘固件是否说谎。**数据库工程师对存储栈的不信任是有据可依的**：SQLite 官方文档《How To Corrupt An SQLite Database File》整页都在列举这类背叛。

### 组提交实测

```text
100 条记录逐条 F_FULLFSYNC: 约 440 ms（外推自上面的 4.4 ms/次）
100 条记录攒一批刷一次:     4.1 ms
→ 快 107x —— WAL 的本质: 把 N 个事务的落盘合并成一次顺序写 + 一次 fsync
```

**这就是所有数据库都用 WAL 的原因**：不是它高深，而是这 107 倍没有别的地方可省。

### C++ 与数据库的特殊关系

```text
SQLite 本身就是 C 写的（约 15 万行）；MySQL/PostgreSQL/RocksDB 是 C/C++
→ C++ 是【写数据库】的语言；其他语言是【用数据库】的语言
→ 嵌入式一侧: sqlite3.h 直接 #include，零依赖
  客户端一侧: libpq(PostgreSQL)/MySQL Connector——都是 C API 打底，其他语言的驱动多是它们的封装
```

> **注意**：`FILE*`/`iostream` 有用户态缓冲——`fwrite` 成功连页缓存都没到，要先 `fflush` 再 `fsync`（第 39 章 fork 丢输出的坑同源）；`O_DIRECT`（Linux）绕过页缓存，数据库常用它自管缓存；写数据库级代码时 `fsync` 目录本身也不能省（rename 的持久化要求父目录落盘）。

---

## 8. C#

.NET 版贡献了「同名 API 不同承诺」的第二个证据，和一个只有截断式写入才会暴露的现象。

### Flush(true) 的真实身份（实测）

```text
Flush(false) 2000 次: 3.8 ms    ← 只倒空用户态缓冲，数据在页缓存
Flush(true)  200 次: 875.0 ms（4.4 ms/次）
→ 裸 fsync 只要 ~26 μs；.NET 的 PAL 在 macOS 上把 Flush(true) 直接实现成【F_FULLFSYNC】
→ 和 libuv 一样选择了掉电安全——C/Python/Java 给便宜的，Node/.NET 给真落盘的
```

### 读到空文件：截断不是原子的（实测）

```text
2 线程 × 150 次自增，期望 300，实际 174（丢了 126 次）
更糟: 有 26 次读到了【空文件】——WriteAllText 先截断后写，读者撞在中间
```

**这比丢更新更隐蔽**：`File.WriteAllText` = 截断 + 写入两步，读者可能撞见中间态的空文件。生产事故里「配置文件偶尔读到空」十有八九是它。手工解法是**临时文件 + 原子换名**（实测）：

```csharp
File.WriteAllText(tmp, newContent);    // 新内容先完整写进临时文件
File.Replace(tmp, cfg, null);          // rename 是原子的（POSIX 保证）
// → 读者永远只见完整的旧版或新版，绝不见半成品
```

**但它只能保护「单个文件的整体替换」**——跨文件、跨行的原子性只有事务能给。

### lock 的边界（实测）

```text
加 lock 后: 300 ✓ —— 但 lock 只管【本进程】（第 41 章）
→ 跨进程要 Mutex，跨机器就没有 API 了——数据库的锁天生跨进程、跨机器
```

### 索引的本质（实测）

```text
20 次查找逐次扫描: 742.1 ms
先建 Dictionary（6 ms）再查: 0.389 ms
→ 索引 = 「一次预处理换 N 次快查」
→ 数据库把这个 Dictionary【持久化在磁盘上】并在每次写入时自动维护——第 49 章 B 树
```

> **注意**：生产用 ADO.NET（`DbConnection`/`DbCommand` 一套接口）+ `Microsoft.Data.Sqlite`/Npgsql；EF Core 是 ORM（第 51 章）；连接字符串里 `Pooling=true` 默认开启——第 45 章的连接池就藏在这。

---

## 9. SQL

前八节都站在「用文件的人」一侧；本节换到数据库一侧，看它到底承诺了什么。

### A：原子性（实测）

```sql
BEGIN;
UPDATE account SET balance = balance - 60 WHERE id = 1;
-- 「崩溃」
ROLLBACK;
```

```text
① 转账中途崩溃后: 甲=100 乙=100（一分都没少）
   完整提交后:     甲=40 乙=160
```

### C：一致性——约束是数据的守门员（实测）

```sql
CREATE TABLE account2 (
  id      INTEGER PRIMARY KEY,
  balance INTEGER NOT NULL CHECK (balance >= 0)   -- 余额不许为负
);
```

```text
② 试图把余额改成 -999: changes=0（CHECK 约束拦下）
   试图插入重复 id=1:   changes=0（主键约束拦下）
→ 文件版要在【每个写它的程序里】重复这些校验；数据库写一遍，所有人共享
```

**约束是「写一次、处处生效」的校验**。文件方案里，Python 脚本、Java 服务、手工 vim 都可能绕过校验写入坏数据；数据库里没有任何路径能绕过 CHECK。

### D：持久性的档位开关（对照本章实测）

```text
③ PRAGMA synchronous = OFF    → 只 write()，进程/内核崩溃都可能丢（1.86 μs 档）
   PRAGMA synchronous = NORMAL → WAL 下的常规选择，掉电可能丢最后事务
   PRAGMA synchronous = FULL   → 每次提交都刷（macOS 默认走 F_BARRIERFSYNC）
   PRAGMA fullfsync = ON       → 动真格的 F_FULLFSYNC（4399 μs 档，C++ 版实测）
→ 第 43 章 FULL 只慢 1.5x 的谜底: macOS 上 FULL ≠ F_FULLFSYNC，还差一档
```

### 声明式查询与执行计划（实测）

```text
④ 同一份十万行数据问三个问题——点查/聚合/Top-1，各改一句话
⑤ EXPLAIN QUERY PLAN SELECT * FROM users WHERE score = 42;
   无索引: SCAN users                                  ← 全表扫
   建 idx_score 后: SEARCH users USING INDEX idx_score  ← 索引查
→ 同一句 SQL，你一行代码没改——优化器替你换了算法
```

**这是声明式的全部红利**：命令式代码把「怎么找」写死在循环里，换算法要改代码；SQL 只声明「要什么」，加一个索引，优化器自动换算法（第 47/49 章展开）。

### 本章总纲（SQL 版输出）

```text
⑥ 文件给你: 一段能读写的字节。数据库在其上加了五层——
   D 持久化: WAL + fsync 档位（C++ 版实测三档价格）
   A 原子性: 事务 + 回滚
   C 一致性: 约束守门
   I 隔离性: 跨进程并发控制（Python 版实测丢更新 vs 不丢）
   查询引擎: 声明式 SQL + 索引 + 优化器
```

> **注意**：sqlite 的约束违规默认让语句报错（脚本会非零退出），演示用 `UPDATE OR IGNORE`/`INSERT OR IGNORE` 静默拒绝并用 `changes()` 观察；服务器数据库的约束违规是抛给客户端的异常，语义相同。

---

## 10. 五语言横向对比

### ① 数据库接入能力

| 能力 | JavaScript | Python | Java | C++ | C# |
|------|-----------|--------|------|-----|-----|
| 标准库自带 sqlite | ✅ `node:sqlite`（22.5+，实验性） | ✅ `sqlite3`（自古就有） | ❌ | ❌（但 sqlite 本身是 C） | ❌ |
| 统一数据库接口 | ❌（各驱动各自 API） | DB-API 2.0（PEP 249） | **JDBC** | ❌ | **ADO.NET** |
| 主流驱动 | pg / mysql2 / better-sqlite3 | psycopg / PyMySQL | 各家 JDBC 驱动 | libpq / MySQL C API | Npgsql / SqlClient |
| ORM 代表（第 51 章） | Prisma / TypeORM | SQLAlchemy / Django ORM | Hibernate / JPA | ❌（无主流） | **EF Core** |
| 「fsync」的真实档位（实测） | **F_FULLFSYNC（4.0 ms）** | 裸 fsync（27.8 μs） | 裸 fsync（~17 μs） | 你自己选（三档全可达） | **F_FULLFSYNC（4.4 ms）** |
| 触到 F_FULLFSYNC | 默认就是 | `fcntl` 标准库 | ❌ 要 JNI | `fcntl` 直接调 | 默认就是 |

### ② 钥匙实验一：持久化三档（C++/Python 实测）

```text
页缓存(write)      1.86 μs/次   进程崩溃✓  内核崩溃✗  断电✗
磁盘缓存(fsync)    26.2 μs/次   进程崩溃✓  内核崩溃✓  断电⚠️
介质(F_FULLFSYNC)  4399 μs/次   全都✓
组提交(WAL):       100 条 4.1 ms vs 逐条 ~440 ms → 快 107x
```

### ③ 钥匙实验二：丢更新全家福

```text
JS     50 协程（单线程！）  → 结果 1（期望 50）    ← 最反直觉
Java   2 线程 × 150        → 149/300，丢 151
C#     2 线程 × 150        → 174/300，丢 126 + 26 次读到空文件
Python 2 进程 × 200        → 280/400，丢 120
—— 换成数据库的原子 UPDATE ——
JS     50 协程             → 50 ✓
Python 2 进程 × 200        → 400 ✓
```

### ④ 钥匙实验三：查询对决（同一份十万行）

```text
点查 20 次:  文件扫描 399.8 ms  vs  sqlite 0.726 ms   → 551x（Python）
点查 1000 次: sqlite 9 μs/次   vs  全表拉回建 Map 109 ms + 46 MB 堆（JS）
聚合:        手写 15 行 173.7 ms  vs  一句 GROUP BY 19.0 ms（Python）
执行计划:    加索引后 SCAN → SEARCH，SQL 一字未改（SQL 版）
```

### ⑤ 共性与根因

**共性**：所有语言的文件 API 都不给原子性（崩溃留半条）、不给隔离性（并发全丢更新）、不给查询（只能全扫）；所有「fsync」都比 write 贵一个数量级以上；**没有任何语言的标准库能替代数据库**——最多给你一个嵌入式 sqlite 的门票。

**根因**：

- **文件 API 是对 POSIX 的直译**——POSIX 只承诺字节流语义，ACID 从来不在合同里；
- **Node/.NET 升级 fsync**——运行时哲学是「宁可慢也不背锅」：用户写 `fsync` 时期待的是「不丢」，那就给真的；C/Python/Java 哲学是「贴近系统调用」，语义忠实但陷阱留给你；
- **Python 标准库带 sqlite** 因为「batteries included」；**Node 22 补上**是承认了同一现实：现代应用没有不碰数据库的；
- **Java/C# 各定义统一接口（JDBC/ADO.NET）**——大厂语言的生态思路：接口进标准，实现留给厂商；
- **C++ 不接入而是【实现】数据库**——SQLite/MySQL/RocksDB 都是 C/C++ 写的，零开销哲学在存储引擎层是刚需。

---

## 11. 底层实现对比

| 存储引擎 | 持久化方案 | 关键细节 |
|---------|-----------|---------|
| **SQLite**（回滚模式） | 改页前先把【旧页】拷进回滚日志 | 崩溃恢复 = 把旧页拷回去；提交要两次 fsync |
| **SQLite**（WAL 模式） | 新数据【追加】写 WAL，读者读旧版本 | 提交只需一次 fsync；读写不互斥（第 50 章） |
| **PostgreSQL** | WAL + 检查点 + 组提交 | `synchronous_commit` 可调档——和本章三档一一对应 |
| **MySQL/InnoDB** | redo log（WAL）+ doublewrite buffer | doublewrite 防【半页写】——比半条记录更底层的撕裂 |
| **Bitcask**（本章 TinyDB） | 追加日志 + 内存哈希索引 + CRC | Java 版 60 行复刻；Riak 生产引擎同模型 |

**一个值得记住的分层**：

```text
所有引擎的持久化都是同一个三段式:
  ① 先把意图顺序写进日志并 fsync（贵，但只贵一次——组提交摊薄）
  ② 立刻向用户承诺「成功」
  ③ 后台把改动慢慢应用到真正的数据结构（B 树/LSM）
→ 差别只在细节: 日志里存旧页还是新值、checkpoint 何时做、半页写怎么防
```

---

## 12. 性能分析

### 数据库比文件「慢」在哪、快在哪

| 操作 | 文件 | 数据库 | 谁快 |
|------|------|--------|------|
| 顺序追加一条（不落盘） | 1.86 μs | ~10 μs（有事务开销） | 文件 |
| 追加一条并保证不丢 | 4399 μs（`F_FULLFSYNC`） | ~44 μs（WAL 组提交摊薄，107x） | **数据库** |
| 十万行点查 | 20 ms/次（全扫） | 9–36 μs/次（B 树） | **数据库 551x** |
| 全量顺序读 | 最快（无解析开销） | 稍慢（页结构解析） | 文件 |
| 并发读-改-写 | **错的**（丢更新） | 对的 | 不可比 |

**结论**：文件赢在「单写者、只追加、全量读」——所以日志文件不进数据库；数据库赢在其余一切。

### 本章实测数字速查

```text
持久化: 1.86 → 26 → 4399 μs（三档，每档一个数量级）
组提交: 107x（100 条 4.1 ms vs ~440 ms）
点查:   551x（0.726 ms vs 399.8 ms）
聚合:   9.1x（19.0 ms vs 173.7 ms）+ 代码量 1 句 vs 15 行
丢更新: 文件全军覆没（1/50、149/300、174/300、280/400），数据库全对
```

> ⚠️ sqlite 的写并发是**排队**的（库级写锁，第 50 章展开）——本章 2 进程各 200 次自增能全对，靠的是排队而非并行。写吞吐要求高时换 PostgreSQL/MySQL 的行级锁 + MVCC；但**正确性与吞吐是两件事**，本章只论证前者。

---

## 13. 工程实践

| 场景 | ✅ 推荐 | ❌ 避免 | 原因 |
|------|--------|--------|------|
| 应用状态存储 | 数据库（哪怕 sqlite） | 自己读写 JSON 文件 | 实测：崩溃留半条、并发丢更新 |
| 配置文件更新 | 临时文件 + 原子 rename | 原地 WriteAllText | 实测 26 次读到空文件 |
| 计数器/余额类更新 | `UPDATE n = n + 1` / 事务 | 读到内存改完写回 | 实测四语言全丢更新 |
| 日志/指标流水 | 追加写文件 | 逐条进数据库 | 单写者顺序追加是文件的主场 |
| 必须不丢的单机数据 | sqlite WAL + `synchronous=NORMAL` 起步 | 自己 fsync | 组提交实测 107x，且档位语义已被验证 |
| 掉电也不能丢（macOS） | `PRAGMA fullfsync=ON` | 以为 fsync 够了 | 实测 fsync 只到磁盘缓存档 |
| 嵌入式/单机/边缘 | SQLite | 起 PostgreSQL | sqlite 是库不是进程，零运维 |
| 多服务共享数据 | 服务器数据库 | 共享 sqlite 文件（NFS 尤甚） | 文件锁跨机器不可靠（官方明示） |
| Java/C# 接入 | JDBC / ADO.NET + 连接池 | 每请求新建连接 | 第 45 章实测建连 10–100 ms |
| Node 单机存储 | node:sqlite / better-sqlite3（同步 API） | 为 sqlite 包异步层 | 点查 9 μs，不值得线程池往返 |

### 一句话决策

```text
数据是「流水」还是「状态」？
  流水（日志/指标，只追加、单写者）→ 文件
  状态（会被更新、被并发访问、被查询）→ 数据库
分不清 → 用数据库；文件方案的每个坑本章都实测过了
```

---

## 14. 最佳实践

- **状态数据一律进数据库**：本章四语言实测证明，手写文件方案在崩溃与并发面前没有胜算；sqlite 零运维，没理由不用。
- **更新永远走原子语句或事务**：`UPDATE n = n + 1` 而不是「读-改-写」——JS 实测单线程都能全丢。
- **知道你的 fsync 是哪一档**：C/Python/Java 是磁盘缓存档（掉电可能丢），Node/.NET 是介质档（贵 150 倍）——写存储相关代码前先确认。
- **配置类文件用「临时文件 + rename」**：唯一的原子写文件手段；直接覆写会让读者撞见空文件（实测 26 次）。
- **别把数据拉到应用侧再算**：实测「拉全表建 Map」比「让数据库查」贵四个数量级内存和三个数量级时间——把计算送到数据那边。
- **给 sqlite 开 WAL**：`PRAGMA journal_mode=WAL` 一句话，读写不再互斥、提交只需一次 fsync。
- **约束写进 schema 而不是应用代码**：CHECK/UNIQUE/NOT NULL 是「写一次、所有写入路径共享」的校验（实测拦下 -999 与重复主键）。
- **尊重「文件的主场」**：日志、指标、导出文件这类只追加流水，不要硬塞进数据库。

---

## 15. 常见坑

**坑 1 · 以为 write 成功 = 数据安全**

```python
f.write(data)          # ⚠️ 数据在用户态缓冲
f.flush()              # ⚠️ 才到页缓存——断电就没
```

**避免**：不丢的数据交给数据库；必须手写时 `fsync`（并知道它在 macOS 不防掉电）。

**坑 2 · 读-改-写一个共享文件**

```javascript
const v = Number(await fsp.readFile(p));   // ⚠️ 单线程也丢——实测 50 个协程结果为 1
await fsp.writeFile(p, String(v + 1));
```

**避免**：改成数据库的原子 `UPDATE`；文件方案需要 `flock` 且跨平台语义堪忧。

**坑 3 · 原地覆写配置文件**

```csharp
File.WriteAllText(cfg, json);   // ⚠️ 截断→写入两步，读者可能读到空文件（实测 26 次）
```

**避免**：写临时文件再 `File.Replace`/`rename`。

**坑 4 · Python sqlite 忘了 commit**

```python
con.execute("INSERT ...")       # ⚠️ 默认非自动提交——进程退出数据就没了
```

**避免**：用 `with con:` 块（退出即提交/回滚），或显式 `con.commit()`。

**坑 5 · 把 sqlite 文件放在网络盘上共享**

```text
NFS/SMB 的文件锁实现不可靠 → sqlite 官方文档明确警告会损坏数据库
```

**避免**：多机共享就上服务器数据库——这正是第 39 章进程边界的延伸。

**坑 6 · 用 Executors 思维配连接**（回收第 45 章）

```java
DriverManager.getConnection(...)   // ⚠️ 每请求建连 10–100 ms，还耗数据库进程
```

**避免**：连接池（HikariCP 等），大小按第 45 章公式 `核心数×2 + 磁盘数`。

**坑 7 · 在数据库外做它擅长的事**

```text
SELECT * 全拉回来自己 filter/sort/count   # ⚠️ 实测 109 ms + 46 MB vs 9 μs
```

**避免**：WHERE/GROUP BY/ORDER BY 下推给数据库——第 47 章的主题。

---

## 16. 面试题

**基础**

1. write() 返回成功后，数据可能在哪四个位置？每个位置分别怕什么级别的崩溃？
2. 数据库对文件多承诺了哪五层？各举一个本章的实测证据。
3. 为什么「读-改-写」在单线程 JS 里也会丢更新？事务是怎么根治它的？

**中级**

4. **WAL 的三步协议是什么？为什么「先写日志」反而更快？（组提交实测 107x 的原因）**
5. macOS 的 fsync 和 F_FULLFSYNC 有什么区别？五个运行时的「fsync」API 各落在哪一档？
6. **设计一个最小的崩溃恢复协议：怎么用校验和识别「写了一半」的记录？（Java 版 60 行的做法）**

**高级**

7. **SQLite 的回滚日志模式和 WAL 模式在崩溃恢复上的策略有何不同？各要几次 fsync？**
8. 为什么 InnoDB 需要 doublewrite buffer？「半页写」和本章的「半条记录」是什么关系？
9. 什么数据适合留在文件里？从「单写者、只追加、访问模式」三个维度论证。

---

## 17. 练习

**基础**

1. 在你的机器上复现三档持久化实测（write/fsync/F_FULLFSYNC 或 Linux 的 fdatasync），画出价格阶梯。
2. 复现 JS 的单线程丢更新实验，把 50 改成 5，观察结果是否仍然是 1（并解释为什么）。
3. 写一段「崩在半路」的文件转账，观察损坏的文件内容；再用 sqlite 事务重做一遍。

**中级**

4. **给 Java 版 TinyDB 加一个 `delete(key)`**（提示：追加一条墓碑记录，恢复时识别它）。
5. 测量你的语言里「fsync」API 的真实耗时，判断它是裸 fsync 档还是 F_FULLFSYNC 档。
6. 用「临时文件 + rename」实现一个绝不会被读到半成品的配置写入器，并用两个进程压测验证。

**挑战**

7. **给 TinyDB 加日志压缩**：把日志重写成只含每个 key 最新值的新文件，原子切换（rename），崩在切换途中也不丢数据。
8. 用 `EXPLAIN QUERY PLAN` 观察五种查询（点查/范围/聚合/排序/连接）在有无索引时的计划变化。
9. 复现组提交：N 个并发「事务」各自要求落盘，实现一个把它们的 fsync 合并成一次的提交线程，测量吞吐提升。

---

## 18. 本章总结

**一句话**：文件给你的只是一段能读写的字节，而应用需要的是**状态**——会崩溃的进程、并发的写者、多变的查询都在威胁它，本章用四语言实测证明手写文件方案全线失守（崩溃留下 `'id=1,balance=40\nid=2,bal'`、单线程 50 个协程自增只得 1、两进程自增丢 120 次更新、十万行点查慢 551 倍），而数据库在同样的攻击下全部给出正确答案；它的手段并不神秘——**持久化**是三档阶梯上的精确选择（实测 1.86 → 26 → 4399 μs，且五个运行时的「fsync」落在不同档：Node/.NET 悄悄升级成 `F_FULLFSYNC`，第 43 章 FULL 只慢 1.5x 的悬案就此了结）；**原子性与恢复**靠 WAL 的三步协议加校验和（Java 版 60 行 TinyDB 实测：注入半条记录，恢复时正确截断）；**贵的落盘**靠组提交摊薄（实测 107x）；**查询**靠持久化在磁盘上的索引与声明式优化器（`SCAN → SEARCH`，SQL 一字未改）——把这五件事的手写代码合起来就是数据库的雏形，而 SQLite 用约 15 万行 C 把它们做到了工业级。

**关键要点**

- **三个洞**：内存不持久、进程锁不跨界、磁盘查找只能扫——文件补不上，数据库为此而生。
- **持久化三档**（实测）：页缓存 1.86 μs / 磁盘缓存 26 μs / 介质 4399 μs；macOS 的 fsync 不防掉电。
- **五语言 fsync 分裂**（实测）：C/Python/Java 裸 fsync；Node 4.0 ms、C# 4.4 ms = 运行时升级成 `F_FULLFSYNC`。
- **单线程也丢更新**（实测）：50 个 JS 协程自增 → 1；元凶是读-改-写不原子，不是多线程。
- **WAL 三步**：先顺序写日志并 fsync → 立刻承诺成功 → 后台 checkpoint；组提交实测 107x。
- **最小恢复协议**（实测）：`[长度][数据][校验和]` + 重放截断——60 行复刻 Bitcask。
- **查询对决**（实测）：551x（点查）、9.1x + 15 行代码 → 1 句（聚合）、`SCAN → SEARCH`（加索引不改 SQL）。
- **文件的主场**：单写者、只追加、全量读的流水（日志/指标）——别硬塞进数据库。

**自查清单**

- [ ] 我能画出数据从代码到介质的四站，并写出每站的实测价格与崩溃语义。
- [ ] 我能解释为什么单线程 JS 也丢更新，并写出根治它的 SQL。
- [ ] 我能复述 WAL 三步协议和组提交 107x 的原理。
- [ ] 我知道自己语言的「fsync」落在哪一档。
- [ ] 我能实现「长度+数据+校验和」的崩溃恢复协议。

**下一章预告**：数据库有了，怎么跟它说话？第 47 章讲 **SQL**——它是六语言里唯一的**声明式**语言：你说「要什么」，优化器决定「怎么找」。我们会实测同一个查询的多种写法在优化器手里殊途同归、JOIN 的三种物理实现（嵌套循环/哈希/归并）的真实差距、以及为什么 `SELECT *` 和 `WHERE` 里套函数会让优化器缴械——把「SQL 只是拼字符串」的错觉一次拆干净。

---

## 19. 延伸阅读

- <a href="https://www.sqlite.org/atomiccommit.html" target="_blank" rel="noopener">SQLite · Atomic Commit In SQLite</a> —— 数据库文献里最好的原子提交长文，本章 WAL 协议的权威版本。
- <a href="https://www.sqlite.org/wal.html" target="_blank" rel="noopener">SQLite · Write-Ahead Logging</a> —— WAL 模式的官方说明。
- <a href="https://www.sqlite.org/howtocorrupt.html" target="_blank" rel="noopener">SQLite · How To Corrupt An SQLite Database File</a> —— 存储栈会怎样背叛你的完整清单（含 fsync 说谎的磁盘）。
- <a href="https://www.sqlite.org/whentouse.html" target="_blank" rel="noopener">SQLite · Appropriate Uses For SQLite</a> —— 嵌入式与服务器数据库的边界，官方论述。
- <a href="https://en.wikipedia.org/wiki/ACID" target="_blank" rel="noopener">Wikipedia · ACID</a> —— 四个字母的出处与形式化定义。
- <a href="https://www.postgresql.org/docs/current/wal-intro.html" target="_blank" rel="noopener">PostgreSQL Docs · WAL Introduction</a> —— 服务器数据库的 WAL 与 `synchronous_commit` 档位。
- <a href="https://en.wikipedia.org/wiki/Bitcask" target="_blank" rel="noopener">Wikipedia · Bitcask</a> —— 本章 TinyDB 的原型：追加日志 + 内存哈希索引。
- <a href="https://nodejs.org/api/sqlite.html" target="_blank" rel="noopener">Node.js Docs · node:sqlite</a> —— Node 22.5+ 内置 sqlite 的官方文档。
- <a href="https://dataintensive.net/" target="_blank" rel="noopener">Designing Data-Intensive Applications</a> —— 存储引擎一章（第 3 章）是本章的最佳延伸。
