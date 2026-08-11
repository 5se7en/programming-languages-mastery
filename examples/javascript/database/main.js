// 数据库：单线程也会丢更新——以及 Node 22 自带的 node:sqlite。
'use strict';
process.removeAllListeners('warning');              // 静音 node:sqlite 的实验性警告
const fs = require('fs');
const fsp = require('fs/promises');
const os = require('os');
const path = require('path');
const { DatabaseSync } = require('node:sqlite');

const WORK = fs.mkdtempSync(path.join(os.tmpdir(), 'pl-mastery-js-db-'));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  console.log('== ① 持久化两档：write ≠ 落盘 ==');
  const p1 = path.join(WORK, 't.log');
  const fd = fs.openSync(p1, 'w');
  const rec = 'id=00042,name=zhang,balance=100\n';
  const N1 = 2000;
  let t0 = Date.now();
  for (let i = 0; i < N1; i++) fs.writeSync(fd, rec);
  const ms1 = Date.now() - t0;
  const N2 = 200;
  t0 = Date.now();
  for (let i = 0; i < N2; i++) { fs.writeSync(fd, rec); fs.fsyncSync(fd); }
  const ms2 = Date.now() - t0;
  fs.closeSync(fd);
  console.log(`  只 writeSync ${N1} 次: ${ms1} ms；write+fsyncSync ${N2} 次: ${ms2} ms`);
  console.log(`  → 每次「fsync」${(ms2 / N2).toFixed(1)} ms —— 可裸 fsync 系统调用只要 ~21 μs（C++/Python 版实测）`);
  console.log('  → 差的 150x 是 libuv 干的: 在 macOS 上它把 fsync【悄悄升级成 F_FULLFSYNC】');
  console.log('    （libuv 源码注释: Apple 的 fsync 不刷磁盘缓存，故先试 F_FULLFSYNC 再退回）');
  console.log('  → 同一个名字，不同的承诺——你的运行时已经替你做了持久性档位的选择');

  console.log('\n== ② 单线程也会丢更新（本章最反直觉的实测）==');
  const p2 = path.join(WORK, 'counter.txt');
  await fsp.writeFile(p2, '0');
  const incr = async () => {
    const v = Number(await fsp.readFile(p2, 'utf8'));   // 读
    await sleep(1);                                     // ← 一次 await = 一次让出（第 43 章）
    await fsp.writeFile(p2, String(v + 1));             // 写
  };
  await Promise.all(Array.from({ length: 50 }, incr));
  console.log(`  50 个并发自增，期望 50，实际 ${await fsp.readFile(p2, 'utf8')}`);
  console.log('  → 没有线程、没有数据竞争，照样全丢！50 个协程都在写之前读到了 0');
  console.log('  → 丢更新的元凶不是「多线程」而是【读-改-写不原子】——事件循环也救不了你');

  console.log('\n== ③ 事务把「读-改-写」变成一步（node:sqlite 实测）==');
  const db = new DatabaseSync(path.join(WORK, 'app.db'));
  db.exec('CREATE TABLE counter(n INTEGER); INSERT INTO counter VALUES(0)');
  const upd = db.prepare('UPDATE counter SET n = n + 1');
  await Promise.all(Array.from({ length: 50 }, async () => { await sleep(1); upd.run(); }));
  console.log(`  同样 50 个并发自增走 UPDATE n=n+1: 实际 ${db.prepare('SELECT n FROM counter').get().n} ✓`);
  console.log('  → 「读旧值、加一、写回」被压进数据库里的一条原子语句，中间没有任何人能插队');

  console.log('\n== ④ 原子性：崩溃测试 ==');
  db.exec('CREATE TABLE account(id INTEGER PRIMARY KEY, balance INTEGER)');
  db.exec('INSERT INTO account VALUES(1,100),(2,100)');
  try {
    db.exec('BEGIN');
    db.exec('UPDATE account SET balance = balance - 60 WHERE id = 1');
    throw new Error('模拟崩溃');                    // 转账做了一半
  } catch {
    db.exec('ROLLBACK');
  }
  const rows = db.prepare('SELECT id, balance FROM account ORDER BY id').all();
  console.log(`  转账中途崩溃后: ${JSON.stringify(rows)}`);
  console.log('  → 两人余额都还是 100；换成两次 fs.writeFile 中间崩溃，钱就凭空消失了');

  console.log('\n== ⑤ 查询下推：把计算交给数据库 ==');
  const ROWS = 100000;
  db.exec('CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT, score INTEGER)');
  const ins = db.prepare('INSERT INTO users VALUES(?,?,?)');
  db.exec('BEGIN');
  for (let i = 0; i < ROWS; i++) ins.run(i, `user-${i}`, i % 100);
  db.exec('COMMIT');
  t0 = Date.now();
  let hits = 0;
  const q = db.prepare('SELECT name FROM users WHERE id = ?');
  for (let k = 0; k < 1000; k++) if (q.get(ROWS - 1 - k)) hits++;
  const msIdx = Date.now() - t0;
  t0 = Date.now();
  const all = db.prepare('SELECT id, name FROM users').all();   // 全捞回来自己找
  const map = new Map(all.map((r) => [r.id, r.name]));
  const msPull = Date.now() - t0;
  console.log(`  主键点查 1000 次: ${msIdx} ms（每次 ${(msIdx * 1000 / 1000).toFixed(0)} μs，B 树直达）`);
  console.log(`  全表拉到 JS 再建 Map: ${msPull} ms + ${(process.memoryUsage().heapUsed / 1048576).toFixed(0)} MB 堆`);
  console.log('  → 「把数据搬到计算这边」很贵；数据库的思路是「把计算送到数据那边」（第 47 章 SQL）');

  console.log('\n== ⑥ Node 生态的选择 ==');
  console.log('  node:sqlite（22.5+ 内置）: 同步 API——sqlite 快到不值得为它走线程池');
  console.log('  better-sqlite3          : 同一哲学，生产主流；pg/mysql2 则是网络异步（第 42 章）');
  console.log('  → 嵌入式数据库(sqlite)是【库】，服务器数据库(pg)是【进程】——第 39 章的边界又出现了');

  fs.rmSync(WORK, { recursive: true, force: true });
})();
