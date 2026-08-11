// 锁：JS 主线程单线程无需锁——但异步与共享内存各自需要另一种「锁」。

const { Worker, isMainThread, workerData, parentPort } = require("worker_threads");

if (!isMainThread) {
  // ---- Worker：用 Atomics 实现一把真正的自旋锁 ----
  const { buffer, times, useLock } = workerData;
  const view = new Int32Array(buffer);
  const LOCK = 0, COUNTER = 1;
  for (let i = 0; i < times; i++) {
    if (useLock) {
      // 自旋获取：把 0 换成 1 才算拿到锁
      while (Atomics.compareExchange(view, LOCK, 0, 1) !== 0) {
        Atomics.wait(view, LOCK, 1, 1); // 等锁释放（带超时，避免忙等烧 CPU）
      }
      view[COUNTER] = view[COUNTER] + 1; // 临界区：非原子也安全
      Atomics.store(view, LOCK, 0);
      Atomics.notify(view, LOCK, 1); // 唤醒一个等待者
    } else {
      view[COUNTER] = view[COUNTER] + 1; // ⚠️ 无保护
    }
  }
  parentPort.postMessage("done");
  return;
}

console.log("== ① JS 主线程为什么不需要锁 ==");
console.log("  单线程事件循环（第 43 章）：一段同步代码执行时不会被打断");
console.log("  → 没有抢占，就没有「读到一半被插队」——数据竞争的前提不成立");

function run(useLock) {
  return new Promise((resolve) => {
    const buffer = new SharedArrayBuffer(8); // [0]=锁，[1]=计数器
    const view = new Int32Array(buffer);
    let done = 0;
    const t0 = process.hrtime.bigint();
    for (let i = 0; i < 2; i++) {
      const w = new Worker(__filename, { workerData: { buffer, times: 50_000, useLock } });
      w.on("message", () => {
        if (++done === 2) {
          resolve({ value: view[1], ms: Number(process.hrtime.bigint() - t0) / 1e6 });
        }
      });
    }
  });
}

(async () => {
  console.log("\n== ② 但共享内存一出现，锁就成了刚需 ==");
  const bad = await run(false);
  console.log(`  无锁: 结果 = ${bad.value}（期望 100000）❌，耗时 ${bad.ms.toFixed(0)} ms`);
  const good = await run(true);
  console.log(`  自旋锁: 结果 = ${good.value}（期望 100000）✅，耗时 ${good.ms.toFixed(0)} ms`);
  console.log("  （这把锁完全由 Atomics.compareExchange + wait/notify 手工搭出来）");

  console.log("\n== ③ 手搓一把锁需要什么 ==");
  console.log("  compareExchange(view, i, 0, 1)  : 原子地「若为 0 则置 1」= 抢锁");
  console.log("  Atomics.wait(view, i, 1)        : 值仍是 1 就睡（不烧 CPU）");
  console.log("  Atomics.store(view, i, 0)       : 释放");
  console.log("  Atomics.notify(view, i, 1)      : 唤醒一个等待者");
  console.log("  ↑ 这正是 mutex 在操作系统里的实现骨架（futex）");

  console.log("\n== ④ 异步世界的另一种「锁」：串行化队列 ==");
  console.log("  async 函数在 await 处会让出控制权 → 两个异步流程可能交错");
  console.log("  典型症状：读取余额 -> await 网络请求 -> 写回余额（丢失更新，第 40 章同款）");
  console.log("  解法不是互斥锁，而是把操作排队串行执行：");

  // 极简的异步互斥：用 Promise 链把操作串起来
  let chain = Promise.resolve();
  const mutex = (fn) => (chain = chain.then(fn, fn));
  let balance = 100;
  const deposit = async (n) => {
    const cur = balance;
    await new Promise((r) => setTimeout(r, 10)); // 模拟 await 期间的让出
    balance = cur + n;
  };
  await Promise.all([mutex(() => deposit(10)), mutex(() => deposit(10))]);
  console.log(`  Promise 链串行化后余额 = ${balance}（期望 120）✅`);
  console.log("  （不串行化的话两个 deposit 都读到 100，结果是 110——第 40 章的丢失更新）");

  console.log("\n== ⑤ JS 的立场 ==");
  console.log("  主线程：不需要锁（单线程）");
  console.log("  异步流程：需要「串行化」而非互斥锁（await 会让出，但不会被抢占）");
  console.log("  worker + SharedArrayBuffer：需要真锁（Atomics 手搓或用库）");
})();
