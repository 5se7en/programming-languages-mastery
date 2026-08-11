// SQL：N+1 查询——ORM 时代最贵的一个反模式，用 node:sqlite 把代价量出来。
'use strict';
process.removeAllListeners('warning');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { DatabaseSync } = require('node:sqlite');

const dbp = path.join(os.tmpdir(), `pl-mastery-jssql-${process.pid}.db`);
const db = new DatabaseSync(dbp);
db.exec(`
  CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT);
  CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER, amount INTEGER);
`);
const iu = db.prepare('INSERT INTO users VALUES(?,?)');
const io = db.prepare('INSERT INTO orders VALUES(?,?,?)');
const NU = 1000, NO = 20000;
db.exec('BEGIN');
for (let i = 0; i < NU; i++) iu.run(i, `user-${i}`);
for (let i = 0; i < NO; i++) io.run(i, i % NU, i % 500);
db.exec('COMMIT');

const now = () => Number(process.hrtime.bigint());
const ms = (a) => (a / 1e6).toFixed(1);

console.log('== 任务: 列出每个用户及其订单总额（1000 用户，20000 订单）==');

// ① N+1：先查所有用户，再【为每个用户】查一次订单
let t0 = now();
const users = db.prepare('SELECT id, name FROM users').all();
let queries = 1;
const perUserStmt = db.prepare('SELECT SUM(amount) s FROM orders WHERE user_id = ?');
let checksum1 = 0;
for (const u of users) {
  const row = perUserStmt.get(u.id);                 // ← 每个用户一次查询
  queries++;
  checksum1 += row.s || 0;
}
const msN1 = now() - t0;
console.log('\n== ① N+1 查询（每个用户查一次订单）==');
console.log(`  发出 ${queries} 条 SQL（1 条查用户 + ${NU} 条查订单），耗时 ${ms(msN1)} ms`);
console.log('  → 代码读起来天经地义: for 每个用户 { 查它的订单 }——但它藏了 1000 次往返');

// ② 一次 JOIN + GROUP BY 解决
t0 = now();
const joined = db.prepare(`
  SELECT u.id, u.name, SUM(o.amount) s
  FROM users u LEFT JOIN orders o ON o.user_id = u.id
  GROUP BY u.id
`).all();
const msJoin = now() - t0;
let checksum2 = 0;
for (const r of joined) checksum2 += r.s || 0;
console.log('\n== ② 一条 JOIN + GROUP BY ==');
console.log(`  发出 1 条 SQL，耗时 ${ms(msJoin)} ms（结果一致: ${checksum1 === checksum2}）`);
console.log(`  → 快 ${(msN1 / msJoin).toFixed(0)}x —— 把「循环里查询」翻译成「集合运算」`);
console.log('  → sqlite 是进程内库，1000 次往返已经这么贵；网络数据库每次多 0.5ms RTT，N+1 是致命的');

// ③ IN 批量：N+1 与 JOIN 之间的折中
t0 = now();
const ids = users.map((u) => u.id);
const placeholders = ids.map(() => '?').join(',');
const batch = db.prepare(
  `SELECT user_id, SUM(amount) s FROM orders WHERE user_id IN (${placeholders}) GROUP BY user_id`
).all(...ids);
const msIn = now() - t0;
console.log('\n== ③ IN 批量查询（一次取回所有需要的）==');
console.log(`  1 条带 ${ids.length} 个占位符的 SQL，耗时 ${ms(msIn)} ms`);
console.log('  → ORM 的 DataLoader / eager-loading 本质就是把 N+1 改写成这个 IN');

// ④ 占位符不是万能：IN 列表本身有上限
console.log('\n== ④ 占位符的边界 ==');
console.log('  sqlite 默认参数上限 999（SQLITE_MAX_VARIABLE_NUMBER）——IN 列表太长会撞墙');
console.log('  → 真正海量关联应回到 JOIN（让数据库在内部做，无参数个数限制）');
console.log('  → 「N+1 → IN → JOIN」是关联查询优化的三级台阶');

// ⑤ 参数化顺带防注入
console.log('\n== ⑤ 同一个 prepare 顺带解决了注入 ==');
const evil = "'; DROP TABLE users; --";
const safe = db.prepare('SELECT COUNT(*) c FROM users WHERE name = ?').get(evil);
console.log(`  恶意输入 ${JSON.stringify(evil)} 走参数化: 匹配 ${safe.c} 行，users 表安然无恙`);
console.log(`  验证表还在: ${db.prepare('SELECT COUNT(*) c FROM users').get().c} 个用户`);
console.log('  → prepare 的占位符同时买到了三样: 防注入 + 语句复用 + 类型正确（第 47 章主线）');

db.close();
fs.rmSync(dbp, { force: true });
