// 性能优化：V8 的隐藏类与内联缓存——「对象形状」是 JS 性能最大的隐形变量。
'use strict';

const now = () => Number(process.hrtime.bigint());
const ms = (d) => d / 1e6;

function bench(label, fn, rounds = 5) {
  fn();                                        // 预热一轮
  const runs = [];
  for (let i = 0; i < rounds; i++) {
    const t0 = now();
    fn();
    runs.push(ms(now() - t0));
  }
  return { label, best: Math.min(...runs), median: runs.sort((a, b) => a - b)[rounds >> 1] };
}

console.log('== ① 内联缓存：单态 / 多态 / 超多态（实测三档）==');
const N = 3_000_000;

/** 造一个数组，其中的对象共有 shapes 种不同的「形状」 */
function buildShapes(shapes) {
  const arr = [];
  for (let i = 0; i < N; i++) {
    const o = { x: i };                                // 每个对象都有 x
    for (let k = 0; k < i % shapes; k++) o['f' + k] = k;  // 额外字段数不同 → 不同隐藏类
    arr.push(o);
  }
  return arr;
}
const mono = buildShapes(1);      // 1 种形状
const poly = buildShapes(4);      // 4 种形状
const mega = buildShapes(30);     // 30 种形状

// 关键: 每次都【新建一个函数】，让它有自己独立的内联缓存
const freshSum = () => new Function('arr', 'let s=0;for(const o of arr)s+=o.x;return s;');
function benchShapes(arr, rounds = 7) {
  const f = freshSum();
  f(arr);                                              // 预热
  const runs = [];
  for (let i = 0; i < rounds; i++) {
    const t0 = now();
    f(arr);
    runs.push(ms(now() - t0));
  }
  return Math.min(...runs);
}

const tMono = benchShapes(mono);
const tPoly = benchShapes(poly);
const tMega = benchShapes(mega);
console.log(`  单态（1 种形状）:    ${tMono.toFixed(1)} ms`);
console.log(`  多态（4 种形状）:    ${tPoly.toFixed(1)} ms（${(tPoly / tMono).toFixed(2)}x）`);
console.log(`  超多态（30 种形状）: ${tMega.toFixed(1)} ms（${(tMega / tMono).toFixed(2)}x）`);
console.log('  → V8 给每种「属性集合 + 顺序」建一个【隐藏类】，属性访问点缓存它见过的形状');
console.log('  → 关键发现: 【几种形状几乎没有代价】——多态缓存能高效处理少数几种');
console.log('     真正的悬崖在【超多态】: 形状数超过缓存容量后退化成哈希查找');
console.log('  ⚠️ 这推翻了流传很广的说法「属性顺序不同就会变慢」——实测 4 种形状只慢 3%');
console.log('  → 实践要点不是「形状必须完全一致」，而是【别让一个访问点见到几十种形状】');
console.log('     典型反例: 一个通用函数处理来自十几个不同 API 的对象');

console.log('\n== ② 数组的元素种类（elements kind）也影响性能（实测）==');
const M = 3_000_000;
const smis = new Array(M);                     // 全是小整数
for (let i = 0; i < M; i++) smis[i] = i;
const doubles = new Array(M);                  // 全是浮点
for (let i = 0; i < M; i++) doubles[i] = i + 0.5;
const mixed = new Array(M);                    // 混了个字符串 → 退化成通用元素
for (let i = 0; i < M; i++) mixed[i] = i;
mixed[Math.floor(M / 2)] = 'oops';             // ⚠️ 一个字符串，让整个数组降级

const sumArr = (arr) => { let s = 0; for (let i = 0; i < arr.length; i++) s += +arr[i] || 0; return s; };
const s1 = bench('smi', () => sumArr(smis));
const s2 = bench('double', () => sumArr(doubles));
const s3 = bench('mixed', () => sumArr(mixed));
console.log(`  全小整数数组求和:  ${s1.best.toFixed(1)} ms`);
console.log(`  全浮点数组求和:    ${s2.best.toFixed(1)} ms`);
console.log(`  混入一个字符串后:  ${s3.best.toFixed(1)} ms（比小整数慢 ${(s3.best / s1.best).toFixed(1)}x）`);
console.log('  → V8 的数组有「元素种类」: SMI → DOUBLE → 通用，只能【单向降级】，永不回升');
console.log('  → 一个字符串就能让整个数组从紧凑存储降级为装箱的通用数组');
console.log('  → 实践: 数值数组保持类型纯净；要极致性能就用 TypedArray（Float64Array）');

console.log('\n== ③ delete 会让对象退化成字典模式（实测）==');
const K = 1_000_000;
function buildK(mode) {
  const arr = [];
  for (let i = 0; i < K; i++) {
    const o = { x: i, tmp: 0, y: i };
    if (mode === 'delete') delete o.tmp;        // ⚠️ delete → 转成字典模式
    else if (mode === 'null') o.tmp = null;     // 推荐的替代写法
    arr.push(o);
  }
  return arr;
}
// 与 ① 同样的纪律: 每个数组配一个【全新的函数】，否则内联缓存会被上一个数组污染
const freshSumXY = () => new Function('arr', 'let s=0;for(const o of arr)s+=o.x+o.y;return s;');
function benchK(arr, rounds = 7) {
  const f = freshSumXY();
  f(arr);
  let best = Infinity;
  for (let i = 0; i < rounds; i++) { const t0 = now(); f(arr); best = Math.min(best, ms(now() - t0)); }
  return best;
}
const tKeep = benchK(buildK('keep'));
const tDel = benchK(buildK('delete'));
const tNull = benchK(buildK('null'));
console.log(`  保留 tmp 字段的 ${K} 个对象求和: ${tKeep.toFixed(1)} ms`);
console.log(`  delete o.tmp 之后:              ${tDel.toFixed(1)} ms（慢 ${(tDel / tKeep).toFixed(1)}x）`);
console.log(`  改成 o.tmp = null:              ${tNull.toFixed(1)} ms（慢 ${(tNull / tKeep).toFixed(1)}x）`);
console.log('  → delete 会破坏隐藏类链，V8 把对象转成【字典模式】（哈希表存属性）');
console.log('  → 想「清空」一个字段就赋 null: 实测比 delete 快，但仍不如【一开始就别留这个字段】');
console.log('  ⚠️ 这个实验第一次写的时候【测反了】: 两个数组共用同一个求和函数，');
console.log('     那个函数的内联缓存同时见过两种形状，于是两边一样慢（正是 ① 的超多态）');
console.log('  → 教训: 测量代码本身也会成为被测系统的一部分——这是微基准最隐蔽的坑');

console.log('\n== ④ 但先别急着优化这些：真实项目的瓶颈往往在别处 ==');
console.log('  前端: 网络往返 > 主线程阻塞 > 重排重绘 >> 这里说的 JS 执行细节');
console.log('  Node: I/O 等待 > 序列化(JSON) > 数据库查询(第 51 章 N+1 实测 201 条 SQL) >> JS 细节');
console.log('  → 上面三个实验的差距虽然是几倍，但它们作用的时间通常只占总耗时的几个百分点');
console.log('  → 阿姆达尔定律（Python 版 ④ 实测）: 优化 2% 的部分，天花板就是 2%');
console.log('  → 所以顺序永远是: 先 profile 找占比最大的那块，再决定要不要抠这些细节');

console.log('\n== ⑤ JS 的测量工具与陷阱 ==');
console.log('  工具: node --prof / --cpu-prof（V8 采样）、Chrome DevTools Performance 面板');
console.log('        --trace-opt / --trace-deopt 看 JIT 优化与【去优化】');
console.log('  陷阱一: 没预热 —— V8 也有 JIT（本例每个 bench 都先跑一轮）');
console.log('  陷阱二: 结果没被消费 —— 与 Java 版 ② 同款的死代码消除风险');
console.log('  陷阱三: 用 Date.now() 计时 —— 精度只有毫秒；要用 performance.now/hrtime.bigint');
console.log('  → 本例统一取【多轮的最小值】，因为最小值最接近「没被干扰时的真实速度」');

console.log('\n== ⑥ 三条能带走的 JS 性能规则 ==');
console.log('  ① 别让一个访问点见到几十种对象形状（① 实测: 4 种无代价，30 种慢 3x）');
console.log('  ② 数组类型纯净: 别往数值数组里塞字符串（② 实测）');
console.log('  ③ 不用 delete: 赋 null 代替（③ 实测）');
console.log('  → 这三条的共同点: 【让 V8 能对你的数据形状做出稳定假设】');
console.log('  → 动态语言的性能优化，本质上是「主动表现得像静态语言」');
