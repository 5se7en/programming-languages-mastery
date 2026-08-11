// 索引：写放大与索引体积——「加个索引又不要钱」这句话的真实账单。
'use strict';
process.removeAllListeners('warning');
const fs = require('fs');
const os = require('os');
const path = require('path');
const { DatabaseSync } = require('node:sqlite');

const WORK = fs.mkdtempSync(path.join(os.tmpdir(), 'pl-mastery-jsidx-'));
const ROWS = 150000;
const now = () => Number(process.hrtime.bigint());
const ms = (d) => d / 1e6;

function makeDb(name, indexes) {
  const p = path.join(WORK, name);
  const db = new DatabaseSync(p);
  db.exec('CREATE TABLE t(id INTEGER PRIMARY KEY, a INTEGER, b INTEGER, name TEXT, pad TEXT)');
  for (const [i, cols] of indexes.entries()) db.exec(`CREATE INDEX i${i} ON t(${cols})`);
  return { db, p };
}

function fill(db) {
  const ins = db.prepare('INSERT INTO t VALUES(?,?,?,?,?)');
  const pad = 'x'.repeat(80);
  const t0 = now();
  db.exec('BEGIN');
  for (let i = 0; i < ROWS; i++) ins.run(i, i % 30000, i % 7, `name-${i}`, pad);
  db.exec('COMMIT');
  return ms(now() - t0);
}

console.log('== ① 写放大：索引让 INSERT / UPDATE / DELETE 都变慢（实测）==');
const results = [];
for (const [label, idxs] of [
  ['无索引', []],
  ['1 个索引', ['a']],
  ['3 个索引', ['a', 'b', 'name']],
]) {
  const { db, p } = makeDb(`w-${idxs.length}.db`, idxs);
  const msIns = fill(db);

  let t0 = now();
  db.exec('UPDATE t SET a = a + 1 WHERE id < 30000');       // 改的是被索引的列
  const msUpd = ms(now() - t0);

  t0 = now();
  db.exec('DELETE FROM t WHERE id >= 120000');
  const msDel = ms(now() - t0);

  db.close();
  const size = fs.statSync(p).size / 1048576;
  results.push({ label, msIns, msUpd, msDel, size });
}
const base = results[0];
console.log('  操作            无索引     1 个索引     3 个索引');
for (const [key, name] of [['msIns', `INSERT ${ROWS} 行`], ['msUpd', 'UPDATE 30000 行'],
                           ['msDel', 'DELETE 30000 行'], ['size', '文件大小(MB)   ']]) {
  const cells = results.map((r) => {
    const v = r[key];
    const ratio = v / base[key];
    return key === 'size' ? `${v.toFixed(1)}(${ratio.toFixed(2)}x)` : `${v.toFixed(0)}ms(${ratio.toFixed(2)}x)`;
  });
  console.log(`  ${name}  ${cells.map((c) => c.padStart(13)).join('')}`);
}
console.log('  → 三种写操作【全都】变慢: 索引是数据的副本，改数据就要同步改所有副本');
console.log('  → 注意 UPDATE 尤其贵: 改一个被索引的列 = 索引里【先删旧位置再插新位置】');

console.log('\n== ② 索引选择性：同样是索引，价值天差地别 ==');
const { db } = makeDb('sel.db', []);
fill(db);
db.exec('CREATE INDEX idx_a ON t(a)');    // 30000 个不同值
db.exec('CREATE INDEX idx_b ON t(b)');    // 7 个不同值
db.exec('ANALYZE');
for (const col of ['a', 'b']) {
  const distinct = db.prepare(`SELECT COUNT(DISTINCT ${col}) c FROM t`).get().c;
  const total = db.prepare('SELECT COUNT(*) c FROM t').get().c;
  const hit = db.prepare(`SELECT COUNT(*) c FROM t WHERE ${col} = 3`).get().c;
  console.log(`  列 ${col}: ${distinct} 个不同值，选择性 ${(distinct / total).toFixed(5)}，`
    + `查一个值命中 ${hit} 行（${((100 * hit) / total).toFixed(1)}%）`);
}
console.log('  → 选择性 = 不同值个数 / 总行数。越接近 1 越值得建索引');
console.log('  → 性别、状态、布尔这类列单独建索引几乎总是浪费——命中太多，回表比扫表还慢');
console.log('  → 但它们放进【复合索引的后面几列】仍然有用（配合前面高选择性的列）');

console.log('\n== ③ 索引也需要维护：碎片与统计信息 ==');
const before = db.prepare("SELECT COUNT(*) c FROM sqlite_master WHERE type='index'").get().c;
db.exec('DELETE FROM t WHERE id % 2 = 0');            // 删一半
const pageBefore = db.prepare('PRAGMA page_count').get()['page_count'];
db.exec('VACUUM');
const pageAfter = db.prepare('PRAGMA page_count').get()['page_count'];
console.log(`  删掉一半行后页数 ${pageBefore} → VACUUM 后 ${pageAfter}（回收 ${((1 - pageAfter / pageBefore) * 100).toFixed(0)}%）`);
console.log(`  索引数量: ${before} 个`);
console.log('  → 删除不会立刻还给操作系统: 页里留下空洞（第 33 章的内存碎片，磁盘版）');
console.log('  → 统计信息也会过期: ANALYZE 之后优化器才知道「这个值大概命中多少行」');
console.log('  → PostgreSQL 的 autovacuum 同时做这两件事（第 48 章 MVCC 版本回收也靠它）');

console.log('\n== ④ 什么时候【不】该建索引 ==');
console.log('  ① 小表: 几百行全扫只要几微秒，索引的下钻反而是净开销');
console.log('  ② 低选择性列: 见 ②——性别/布尔/状态');
console.log('  ③ 写多读少的表: 日志/流水表，写放大直接吃掉全部收益（① 实测）');
console.log('  ④ 从来没出现在 WHERE/ORDER BY/JOIN 里的列: 建了也不会被用');
console.log('  → 建索引的正确触发条件是【一条具体的慢查询】，不是「这列看起来会被查」');

console.log('\n== ⑤ Node 侧的实用提示 ==');
console.log('  查看表上的索引:  SELECT name, sql FROM sqlite_master WHERE type = \'index\'');
console.log('  查看是否被用上:  EXPLAIN QUERY PLAN <你的查询>');
console.log('  批量导入时: 【先删索引 → 导入 → 再建索引】比边插边维护快得多');
console.log('    因为 CREATE INDEX 是【批量排序构建】（C++ 版就是这么建树的），而逐行插入是随机写');

db.close();
fs.rmSync(WORK, { recursive: true, force: true });
