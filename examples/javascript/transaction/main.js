// 事务：写偏斜（write skew）——快照隔离挡不住的那个异常。
'use strict';
process.removeAllListeners('warning');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { DatabaseSync } = require('node:sqlite');

const DB = path.join(os.tmpdir(), `pl-mastery-jstx-${process.pid}.db`);
const cleanup = () => ['', '-wal', '-shm'].forEach((s) => fs.rmSync(DB + s, { force: true }));
cleanup();

const db = new DatabaseSync(DB);
db.exec(`
  PRAGMA journal_mode=WAL;
  CREATE TABLE doctor(id INTEGER PRIMARY KEY, name TEXT, on_call INTEGER);
  INSERT INTO doctor VALUES(1,'Alice',1),(2,'Bob',1);
`);

const onCallCount = (conn) =>
  conn.prepare('SELECT COUNT(*) c FROM doctor WHERE on_call = 1').get().c;

console.log('== 场景: 医院排班系统，规则是「任何时刻至少有一名医生在值班」==');
console.log('  Alice 和 Bob 都在值班，两人【同时】点了「我要请假」');

console.log('\n== ① 写偏斜：两个事务各自都「检查通过」，合起来却违反了规则 ==');
console.log('  应用层代码是这样写的（看起来完全正确）:');
console.log('    BEGIN; if (在值班人数 >= 2) { 把自己设为不值班 } COMMIT;');
console.log('  在【快照隔离】下会发生什么:');
console.log('    T1 读到「2 人在值班」→ 判断通过 → 把 Alice 设为不值班');
console.log('    T2 读到「2 人在值班」→ 判断通过 → 把 Bob 设为不值班   ← 它读的是快照，看不到 T1');
console.log('    两个事务都提交 → 【0 人值班】，规则被破坏');
console.log('  ⚠️ 注意: 两个事务【没有写同一行】——所以写-写冲突检测抓不到它');

console.log('\n== ② sqlite 是 SERIALIZABLE，实测它挡住了写偏斜 ==');
const A = new DatabaseSync(DB);
const B = new DatabaseSync(DB);
let outcome;
try {
  A.exec('BEGIN IMMEDIATE');
  const seenByA = onCallCount(A);
  A.prepare('UPDATE doctor SET on_call = 0 WHERE id = 1').run();   // Alice 请假

  try {
    B.exec('BEGIN IMMEDIATE');                                     // Bob 也要请假
    const seenByB = onCallCount(B);
    B.prepare('UPDATE doctor SET on_call = 0 WHERE id = 2').run();
    B.exec('COMMIT');
    outcome = `两个事务都提交了（A 看到 ${seenByA} 人，B 看到 ${seenByB} 人）`;
  } catch (e) {
    outcome = `B 被数据库【拒绝】——${e.message}`;
  }
  A.exec('COMMIT');
} catch (e) {
  outcome = `异常: ${e.message}`;
}
console.log(`  结果: ${outcome}`);
console.log(`  最终在值班人数: ${onCallCount(db)}  ${onCallCount(db) >= 1 ? '✓ 规则保住了' : '✗ 规则被破坏'}`);
console.log('  → sqlite 只允许【一个写事务】同时存在，等于强制串行化——写偏斜无从发生');
console.log('  → 代价: 写并发度 = 1（第 46 章实测过它靠排队而非并行取得正确性）');

console.log('\n== ③ 但在 PostgreSQL 的默认隔离级别下，写偏斜【真的会发生】==');
console.log('  PostgreSQL 默认 READ COMMITTED，可选 REPEATABLE READ（实为快照隔离）');
console.log('  两者都【挡不住】写偏斜——必须显式用 SERIALIZABLE（可串行化快照隔离，SSI）');
console.log('  MySQL 的 REPEATABLE READ 也挡不住，需要 SELECT ... FOR UPDATE 手工加锁');
console.log('  → 这是「隔离级别」这套术语最危险的地方: 名字听起来够用，实际未必');

console.log('\n== ④ 三种防御手段的对比 ==');
console.log('  ① 提升到 SERIALIZABLE  → 数据库检测冲突并让一方回滚，你要写重试逻辑');
console.log('  ② SELECT ... FOR UPDATE → 读的时候就加锁，把「读-判断-写」串行化');
console.log('  ③ 改造数据模型         → 加一行「值班人数」计数器，让两个事务写【同一行】');
console.log('     → 写同一行就变成了写-写冲突，普通的锁/冲突检测就能抓住');
console.log('  → 手段 ③ 最快也最稳: 把「约束」变成一个可以被并发控制看见的【具体的行】');

console.log('\n== ⑤ 事务不是万能的：它管不了外部世界 ==');
db.exec('BEGIN');
db.prepare('UPDATE doctor SET on_call = 1 WHERE id = 1').run();
console.log('  假设事务里做了三件事: ① 改数据库 ② 发邮件 ③ 扣第三方支付');
db.exec('ROLLBACK');
console.log(`  ROLLBACK 后数据库回滚了（值班人数 ${onCallCount(db)}），但是——`);
console.log('  → 邮件【已经发出去了】，支付【已经扣了】——事务对外部副作用完全无能为力');
console.log('  → 正确做法: 事务里只做数据库操作，副作用记进「待办表」，提交后再由后台执行');
console.log('  → 这就是 outbox 模式；它把「分布式一致」退化成「本地事务 + 幂等重试」');

console.log('\n== ⑥ Node 侧的事务写法 ==');
console.log('  node:sqlite / better-sqlite3 是同步 API —— 事务里不会出现 await（这是好事）');
console.log('  ⚠️ 异步驱动（pg/mysql2）的经典事故: 事务里 await 了一个【别的连接】的查询');
console.log('     → 那个查询不在这个事务里，回滚时它不会被撤销');
console.log('  → 事务与连接【绑定】（第 45 章实测过: 事务期间连接不能还给池）');

A.close();
B.close();
db.close();
cleanup();
