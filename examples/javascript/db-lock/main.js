// 数据库锁：SQLITE_BUSY 的正确处理方式——退避重试，以及为什么「立刻重试」会更糟。
'use strict';
process.removeAllListeners('warning');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { DatabaseSync } = require('node:sqlite');

const DB = path.join(os.tmpdir(), `pl-mastery-jslock-${process.pid}.db`);
const cleanup = () => ['', '-wal', '-shm'].forEach((s) => fs.rmSync(DB + s, { force: true }));
cleanup();

const main = new DatabaseSync(DB);
main.exec(`
  PRAGMA journal_mode=WAL;
  CREATE TABLE t(id INTEGER PRIMARY KEY, v INTEGER);
  INSERT INTO t VALUES(1, 0);
`);

const open = (timeoutMs) => {
  const db = new DatabaseSync(DB);
  db.exec(`PRAGMA busy_timeout = ${timeoutMs}`);
  return db;
};
const now = () => Number(process.hrtime.bigint());
const msOf = (d) => d / 1e6;

console.log('== ① busy_timeout：拿不到锁时等多久（实测）==');
const holder = open(0);
holder.exec('BEGIN IMMEDIATE');                    // 占住写锁不放
for (const timeout of [0, 50, 200]) {
  const other = open(timeout);
  const t0 = now();
  let outcome;
  try {
    other.exec('BEGIN IMMEDIATE');
    outcome = '拿到了锁';
    other.exec('ROLLBACK');
  } catch (e) {
    outcome = `失败（${e.message}）`;
  }
  console.log(`  busy_timeout=${String(timeout).padStart(3)}ms: ${outcome}，实际等了 ${msOf(now() - t0).toFixed(0)} ms`);
  other.close();
}
console.log('  → busy_timeout=0 时立刻失败；设了值就会在内部【自旋等待】到超时');
console.log('  → 这个等待发生在 sqlite 内部，你的线程是被【阻塞】的（第 45 章：会占住池里的位置）');

console.log('\n== ② 立刻重试 vs 指数退避（实测对比）==');
function attempt(db) {
  try {
    db.exec('BEGIN IMMEDIATE');
    db.prepare('UPDATE t SET v = v + 1').run();
    db.exec('COMMIT');
    return true;
  } catch {
    try { db.exec('ROLLBACK'); } catch { /* 事务根本没开起来 */ }
    return false;
  }
}

// 每一轮都【重新占住】写锁 120ms，保证两种策略面对同样的障碍
function race(useBackoff) {
  try { holder.exec('BEGIN IMMEDIATE'); } catch { /* 已持有 */ }
  const releaseAt = now() + 120e6;
  const tick = () => {
    if (now() >= releaseAt) { try { holder.exec('ROLLBACK'); } catch { /* 已释放 */ } }
  };
  const db = open(0);
  let tries = 0, backoffMs = 0, delay = 1;
  const t0 = now();
  for (;;) {
    tries++;
    tick();
    if (attempt(db)) break;
    if (useBackoff) {
      const until = now() + delay * 1e6;
      while (now() < until) tick();                 // 退避期间什么都不做
      backoffMs += delay;
      delay *= 2;
    }
    if (msOf(now() - t0) > 3000) break;             // 保险丝
  }
  db.close();
  return { tries, ms: msOf(now() - t0), backoffMs };
}

const noBackoff = race(false);
const withBackoff = race(true);
console.log(`  立刻重试（无退避）: 尝试 ${String(noBackoff.tries).padStart(5)} 次，`
  + `总耗时 ${noBackoff.ms.toFixed(0)} ms`);
console.log(`  指数退避:          尝试 ${String(withBackoff.tries).padStart(5)} 次，`
  + `总耗时 ${withBackoff.ms.toFixed(0)} ms（累计退避 ${withBackoff.backoffMs} ms）`);
console.log(`  → 两者【耗时相近】（都要等锁释放），但尝试次数相差 `
  + `${(noBackoff.tries / withBackoff.tries).toFixed(0)} 倍`);
console.log('  → 每次失败的尝试都是一次真实的锁申请: 无退避 = 用无效请求持续冲击数据库');
console.log('  → 高并发下这会把「一个慢事务」放大成「全站雪崩」（第 45 章的重试雪崩）');

console.log('\n== ③ 单写者模型下不会死锁，但会「饿死」==');
console.log('  sqlite 同时只有一个写事务 → 没有循环等待 → 【不可能死锁】（C++ 版的环不存在）');
console.log('  但会出现另一种问题: 写者排队时，【谁先拿到锁没有公平性保证】');
console.log('  → 高频短事务可能一直插队，让某个长事务反复 BUSY 超时（写者饥饿）');
console.log('  → 解法: 应用层排队（单一写入通道），而不是让 N 个线程抢同一把写锁');

console.log('\n== ④ WAL 模式下读者不阻塞写者（实测）==');
const reader = open(0);
const writer = open(0);
writer.exec('BEGIN IMMEDIATE');
writer.prepare('UPDATE t SET v = 12345').run();     // 未提交
let t0 = now();
const seen = reader.prepare('SELECT v FROM t WHERE id = 1').get().v;
console.log(`  写者持锁未提交时，读者读到: ${seen}（耗时 ${msOf(now() - t0).toFixed(3)} ms，零等待）`);
writer.exec('ROLLBACK');
console.log('  → 读到的是【旧版本】——MVCC（第 48 章 C++ 版手写过它的可见性规则）');
console.log('  → 传统的两阶段锁会让这个读【阻塞到写者提交】；MVCC 把读写解耦了');

console.log('\n== ⑤ 什么时候需要显式加锁（MVCC 之外的场景）==');
console.log('  MVCC 让【读】不需要锁，但下面三种情况仍要显式锁:');
console.log('    ① 读后写: 读到的值要作为写入的依据 → BEGIN IMMEDIATE / SELECT FOR UPDATE');
console.log('    ② 检查后行动: 「至少保留一个管理员」这类约束 → 防写偏斜（第 48 章）');
console.log('    ③ 唯一性生成: 「取号 + 加一」→ 用数据库序列或原子 UPDATE 代替');
console.log('  → 判据很简单: 你读到的东西会不会【影响你接下来要写什么】？会 → 需要锁');

console.log('\n== ⑥ Node 侧的实用提示 ==');
console.log('  PRAGMA busy_timeout = 5000   ← 每个连接都要单独设，不是全局的');
console.log('  better-sqlite3 的 db.transaction(fn) 会自动包 BEGIN/COMMIT，但【不会自动重试】');
console.log('  ⚠️ node:sqlite / better-sqlite3 是同步 API: 锁等待会【阻塞整个事件循环】');
console.log('     → 第 43 章实测过这个后果: 一个阻塞操作让所有并发请求一起停摆');
console.log('  → 写密集的 Node 服务应该把写操作收敛到单一队列，而不是每个请求各开事务');

holder.close(); reader.close(); writer.close(); main.close();
cleanup();
