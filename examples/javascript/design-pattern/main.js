// 设计模式：观察者模式是 JS 全部异步的思想源头——从手写 EventEmitter 到 Promise 的血缘。
'use strict';
const { EventEmitter } = require('events');

console.log('== ① 手写观察者：30 行就是 EventEmitter ==');
class MiniEmitter {
  constructor() { this.handlers = new Map(); }        // 事件名 → 回调列表
  on(event, fn) {
    if (!this.handlers.has(event)) this.handlers.set(event, []);
    this.handlers.get(event).push(fn);
    return () => this.off(event, fn);                  // 返回退订函数（闭包）
  }
  off(event, fn) {
    const list = this.handlers.get(event) || [];
    const i = list.indexOf(fn);
    if (i >= 0) list.splice(i, 1);
  }
  emit(event, ...args) {
    for (const fn of [...(this.handlers.get(event) || [])]) fn(...args);
  }
}

const bus = new MiniEmitter();
const log = [];
const unsub = bus.on('order', (id) => log.push(`库存服务处理 ${id}`));
bus.on('order', (id) => log.push(`邮件服务处理 ${id}`));
bus.emit('order', 'A1');
unsub();
bus.emit('order', 'A2');
console.log(`  两个订阅者 → emit(A1) → 退订一个 → emit(A2)`);
console.log(`  ${JSON.stringify(log)}`);
console.log('  → 「观察者模式」= 一个 Map<事件名, 回调数组> + 遍历调用');
console.log('  → 它解耦的是【谁产生事件】和【谁关心事件】——发布者不需要知道订阅者是谁');

console.log('\n== ② 与 Node 内建的 EventEmitter 行为一致（实测）==');
const real = new EventEmitter();
const log2 = [];
real.on('order', (id) => log2.push(`库存服务处理 ${id}`));
real.on('order', (id) => log2.push(`邮件服务处理 ${id}`));
real.emit('order', 'A1');
console.log(`  内建 EventEmitter: ${JSON.stringify(log2)}`);
console.log(`  与手写版一致: ${JSON.stringify(log2) === JSON.stringify(log.slice(0, 2))}`);
console.log('  → Node 的 Stream、HTTP Server、Process 全部建立在它之上');
console.log('  → 第 43 章的事件循环是【运行时层】的观察者，EventEmitter 是【用户态】的同一模式');

console.log('\n== ③ 观察者的三个演化形态（同一个思想的三代）==');
// 一代: 回调（观察者的最朴素形态）
function readCallback(cb) { setTimeout(() => cb(null, '一代:回调'), 0); }
// 二代: Promise（把「一次性的观察者」标准化）
const readPromise = () => new Promise((res) => setTimeout(() => res('二代:Promise'), 0));
// 三代: async 迭代器（把「多次事件的观察者」标准化）
async function* readStream() {
  for (const chunk of ['三代:', '流式', '数据']) {
    await new Promise((r) => setTimeout(r, 0));
    yield chunk;
  }
}

(async () => {
  await new Promise((resolve) => readCallback((_, v) => { console.log(`  ${v}`); resolve(); }));
  console.log(`  ${await readPromise()}`);
  let acc = '';
  for await (const chunk of readStream()) acc += chunk;
  console.log(`  ${acc}`);
  console.log('  → Promise = 只会触发一次的观察者（resolve 就是 emit，then 就是 on）');
  console.log('  → 异步迭代器 = 会触发多次且【可背压】的观察者（第 44 章的协程）');
  console.log('  → 三代都是同一个模式，语言把它一层层内建、标准化');

  console.log('\n== ④ 哪些 GoF 模式在 JS 里也消失了 ==');
  const rows = [
    ['策略 Strategy', 'arr.sort(fn) —— 传函数'],
    ['命令 Command', '函数 / bind（第 55 章的部分应用）'],
    ['观察者 Observer', 'EventEmitter / Promise / 事件循环（① ③ 实测）'],
    ['迭代器 Iterator', 'for...of + Symbol.iterator（语言内建）'],
    ['装饰器 Decorator', '高阶函数（下方 ⑤）'],
    ['单例 Singleton', 'ES 模块只求值一次（下方 ⑥）'],
    ['代理 Proxy', 'Proxy 是【内建对象】——语言把模式做成了 API'],
  ];
  for (const [gof, js] of rows) console.log(`  ${gof.padEnd(20)} → ${js}`);
  console.log('  → 「代理模式」尤其有意思: 它从一个需要手写的模式，变成了 new Proxy(...)');

  console.log('\n== ⑤ 装饰器 = 高阶函数（实测）==');
  const withRetry = (fn, times) => async (...args) => {
    let lastErr;
    for (let i = 0; i < times; i++) {
      try { return await fn(...args); } catch (e) { lastErr = e; }
    }
    throw lastErr;
  };
  let attempts = 0;
  const flaky = async () => { if (++attempts < 3) throw new Error('临时失败'); return '成功'; };
  console.log(`  withRetry(flaky, 5)() → ${await withRetry(flaky, 5)()}（尝试了 ${attempts} 次）`);
  console.log('  → 一个高阶函数就实现了 GoF 需要四个类的装饰器模式');
  console.log('  → 而且可以任意组合: withLog(withRetry(withTimeout(fn)))');

  console.log('\n== ⑥ 单例：ES 模块天然是单例（实测）==');
  const m1 = require('events');
  const m2 = require('events');
  console.log(`  两次 require('events') 是同一个对象吗: ${m1 === m2}`);
  console.log('  → 模块缓存（第 14 章）让「模块级的对象」天然是单例');
  console.log('  → 所以 JS 的单例写法是: export const db = new Database() —— 没有 getInstance()');
  console.log('  ⚠️ 代价与第 55 章一致: 这个单例【测试时换不掉】（import 绑定是静态的）');

  console.log('\n== ⑦ JS 里真正还需要的模式 ==');
  console.log('  模块模式/IIFE  —— 曾经用来造私有作用域，现在被 ESM + # 私有字段取代');
  console.log('  中间件/管道    —— Express/Koa 的核心（本质是责任链模式，仍然活跃）');
  console.log('  状态机         —— 复杂 UI 状态（XState），领域复杂度带来的，语言消不掉');
  console.log('  不可变更新     —— React 的核心约定，不是 GoF 模式但同样是「被广泛复用的解法」');
  console.log('  → 前两个说明: 模式会随语言演化【退场】，也会随框架生态【新生】');
})();
