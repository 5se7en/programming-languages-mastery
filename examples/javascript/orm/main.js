// ORM：延迟加载为什么会在事务外炸掉——以及 ORM 抽象漏出来的那一刻。
'use strict';
process.removeAllListeners('warning');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { DatabaseSync } = require('node:sqlite');

const DB = path.join(os.tmpdir(), `pl-mastery-jsorm-${process.pid}.db`);
fs.rmSync(DB, { force: true });

const db = new DatabaseSync(DB);
db.exec(`
  CREATE TABLE users(id INTEGER PRIMARY KEY, name TEXT);
  CREATE TABLE orders(id INTEGER PRIMARY KEY, user_id INTEGER, amount INTEGER);
`);
const iu = db.prepare('INSERT INTO users VALUES(?,?)');
const io = db.prepare('INSERT INTO orders VALUES(?,?,?)');
db.exec('BEGIN');
for (let i = 0; i < 300; i++) iu.run(i, `user-${i}`);
for (let i = 0; i < 6000; i++) io.run(i, i % 300, i % 500);
db.exec('COMMIT');

const now = () => Number(process.hrtime.bigint());
const ms = (d) => d / 1e6;

// ---------- 一个会「延迟加载」的微型 ORM ----------
let queryCount = 0;
class Session {
  constructor(conn) { this.conn = conn; this.open = true; }
  close() { this.open = false; }
  run(sql, ...args) {
    if (!this.open) throw new Error('会话已关闭（LazyInitializationException 的 JS 版）');
    queryCount++;
    return this.conn.prepare(sql).all(...args);
  }
  users() {
    return this.run('SELECT id, name FROM users').map((row) => {
      const session = this;
      return {
        ...row,
        // ⚠️ 看起来是个普通属性，实际每次访问都发一条 SQL
        get orders() { return session.run('SELECT amount FROM orders WHERE user_id = ?', row.id); },
      };
    });
  }
}

console.log('== ① 延迟加载：属性访问背后藏着一条 SQL ==');
let s = new Session(db);
queryCount = 0;
let t0 = now();
let total = 0;
for (const u of s.users()) total += u.orders.reduce((a, o) => a + o.amount, 0);
const lazyMs = ms(now() - t0), lazyQ = queryCount;
console.log(`  for (const u of users) u.orders...  → 发出 ${lazyQ} 条 SQL，耗时 ${lazyMs.toFixed(1)} ms`);
console.log('  → 源码里只有一次属性访问，实际是 1 + N 条查询（Python 版实测 201 条同款）');

console.log('\n== ② 预加载：一条 JOIN 解决 ==');
queryCount = 0;
t0 = now();
const rows = db.prepare(`
  SELECT u.id, u.name, o.amount FROM users u LEFT JOIN orders o ON o.user_id = u.id
`).all();
const eagerTotal = rows.reduce((a, r) => a + (r.amount || 0), 0);
const eagerMs = ms(now() - t0);
console.log(`  一条 JOIN → 1 条 SQL，耗时 ${eagerMs.toFixed(1)} ms（结果一致: ${total === eagerTotal}）`);
console.log(`  → SQL 条数少 ${lazyQ} 倍，耗时快 ${(lazyMs / eagerMs).toFixed(1)}x`);

console.log('\n== ③ 抽象漏出来的那一刻：会话关闭后再访问关联 ==');
s = new Session(db);
const oneUser = s.users()[0];
console.log(`  在会话内访问 u.orders: ✓ 拿到 ${oneUser.orders.length} 条订单`);
s.close();                                    // 模拟请求结束、事务提交、连接归还
try {
  oneUser.orders;
  console.log('  会话关闭后再访问: 竟然成功了（不应该）');
} catch (e) {
  console.log(`  会话关闭后再访问: ✗ ${e.message}`);
}
console.log('  → 这就是 Hibernate 的 LazyInitializationException / Django 的 DoesNotExist');
console.log('  → 根因: 对象【看起来】是个普通对象，实际上它还【连着数据库】');
console.log('     一旦离开事务边界（返回给视图层、放进缓存、序列化成 JSON），关联就取不到了');

console.log('\n== ④ 于是产生了两难 ==');
console.log('  用延迟加载: 省内存，但离开事务就炸，且极易 N+1（① 实测）');
console.log('  用预加载  : 安全，但可能把用不到的数据也捞回来（over-fetching）');
console.log('  → 现代方案是【显式声明】: Prisma 的 include、EF 的 .Include()、JPA 的 fetch join');
console.log('     把「要不要加载关联」变成代码里【看得见的一行】，而不是隐式的属性访问');

console.log('\n== ⑤ 实测 over-fetching 的代价 ==');
t0 = now();
const wide = db.prepare('SELECT u.id, u.name, o.id, o.user_id, o.amount FROM users u JOIN orders o ON o.user_id = u.id').all();
const wideMs = ms(now() - t0);
t0 = now();
const narrow = db.prepare('SELECT u.name, o.amount FROM users u JOIN orders o ON o.user_id = u.id').all();
const narrowMs = ms(now() - t0);
console.log(`  取 5 列: ${wideMs.toFixed(1)} ms（${wide.length} 行）`);
console.log(`  取 2 列: ${narrowMs.toFixed(1)} ms（${narrow.length} 行，快 ${(wideMs / narrowMs).toFixed(1)}x）`);
console.log('  → ORM 默认「取整个实体」= 永远是 SELECT *（第 47 章实测它关闭了覆盖索引）');
console.log('  → 列表页只需两列时，用投影查询（Prisma 的 select、EF 的 .Select()）而不是取实体');

console.log('\n== ⑥ JS 的 ORM 生态与它的特殊之处 ==');
console.log('  Prisma  : schema 文件 + 【代码生成】→ 类型安全，但要跑生成步骤');
console.log('  TypeORM : 装饰器（@Entity/@Column）→ 最像 Hibernate，但依赖实验性装饰器');
console.log('  Drizzle : 用 TS 类型定义 schema，查询构建器风格（接近 jOOQ）');
console.log('  Knex    : 纯查询构建器 —— 不做对象映射，只帮你拼 SQL');
console.log('  → JS 没有运行时反射也没有注解，所以主流方案都靠【代码生成】补上这一课');
console.log('  → 这与 C++ 的 ODB 是同一个思路: 语言不给的，就用外部工具生成');

db.close();
fs.rmSync(DB, { force: true });
