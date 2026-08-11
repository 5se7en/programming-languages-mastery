// 协程：JS 的 function* 生成器——async/await 的地基。

// ① 生成器：可暂停恢复的函数
function* counter(name, n) {
  let total = 0;
  for (let i = 0; i < n; i++) {
    total += i;
    yield `${name}: 第 ${i} 步，累计 ${total}`;
  }
  return total;
}

// ③ 手工协程调度器：轮转执行
function scheduler(tasks) {
  const queue = [...tasks];
  const trace = [];
  while (queue.length) {
    const task = queue.shift();
    const { value, done } = task.next(); // 恢复它，跑到下一个 yield
    if (!done) {
      trace.push(value);
      queue.push(task); // 排回队尾（与第 43 章事件循环同构）
    }
  }
  return trace;
}

console.log("== ① 生成器就是协程：暂停与恢复 ==");
const gen = counter("A", 3);
console.log(`  调用生成器函数返回: ${gen[Symbol.toStringTag]} 对象（函数体一行都没执行）`);
console.log(`  第一次 next(): ${gen.next().value}`);
console.log(`  第二次 next(): ${gen.next().value}   ← 从上次 yield 的下一行继续，total 还在`);
console.log("  它的状态（局部变量 i、total）保存在引擎为它分配的堆对象里（第 32 章）");

console.log("\n== ② 双向通信：yield 既能出也能进 ==");
function* echo() {
  while (true) {
    const received = yield "等待输入……"; // yield 的返回值 = 外部 send 进来的东西
    if (received === undefined) return;
    yield `收到了: ${received}`;
  }
}
const e = echo();
console.log(`  第一次 next():        ${e.next().value}`);
console.log(`  next("你好") 送入值:  ${e.next("你好").value}`);
console.log("  ↑ 协程与调用者是【双向】的：yield 出去一个值，next() 送进来一个值");

console.log("\n== ③ 钥匙实验：二十行搭一个协程调度器 ==");
scheduler([counter("协程甲", 3), counter("协程乙", 2)]).forEach((l) => console.log(`    ${l}`));
console.log("  ↑ 两个协程交替推进——单线程上实现了「并发」，且完全没有锁");

console.log("\n== ④ async/await 就是「生成器 + 自动驱动」==");
function* asyncLike() {
  const a = yield Promise.resolve(1);
  const b = yield Promise.resolve(a + 1);
  return a + b;
}
// 手工写一个驱动器：把 yield 出来的 Promise 等好了再 send 回去
function drive(genFn) {
  const g = genFn();
  return new Promise((resolve) => {
    (function step(input) {
      const { value, done } = g.next(input);
      if (done) return resolve(value);
      Promise.resolve(value).then(step); // ← await 的本质就是这一行
    })();
  });
}
drive(asyncLike).then((r) => {
  console.log(`  手工驱动生成器模拟 await，结果 = ${r}（1 + 2）`);
  console.log("  → async/await 只是把这个驱动器做进了语言（co 库当年就这么干的）");
  scaleTest();
});

async function scaleTest() {
  console.log("\n== ⑤ 规模实验：协程的内存代价 ==");
  const N = 100_000;
  const before = process.memoryUsage().heapUsed;
  const gens = Array.from({ length: N }, (_, i) => counter(`g${i}`, 1000));
  gens.forEach((g) => g.next()); // 每个都启动一下，让状态真的分配出来
  const after = process.memoryUsage().heapUsed;
  const kb = (after - before) / 1024 / N;
  console.log(`  ${N} 个暂停中的生成器: 占用 ${((after - before) / 1024 / 1024).toFixed(1)} MB`);
  console.log(`  平均每个约 ${kb.toFixed(2)} KB —— 而一条 OS 线程要 1024 KB（第 31/39 章）`);
  console.log(`  → 相差约 ${Math.round(1024 / kb)} 倍，这就是能开十万个协程的原因`);

  console.log("\n== ⑥ JS 的协程家族 ==");
  console.log("  function*    : 生成器（本节主角）—— 无栈协程");
  console.log("  async/await  : 生成器 + Promise 驱动（第 42 章）");
  console.log("  async function* : 异步生成器，配合 for await...of 做流式处理");
  console.log("  ❌ 没有有栈协程：JS 无法在任意深度让出（必须显式 yield/await）");

  console.log("\n== ⑦ 异步生成器演示 ==");
  async function* ticker(n) {
    for (let i = 1; i <= n; i++) {
      await new Promise((r) => setTimeout(r, 5));
      yield i;
    }
  }
  const got = [];
  for await (const v of ticker(3)) got.push(v);
  console.log(`  for await...of 收到: [${got}]（每个都经过了一次 await + yield）`);
  console.log("  → 这是「流式处理」的标准写法：数据边产生边消费，不必全部载入内存");
}
