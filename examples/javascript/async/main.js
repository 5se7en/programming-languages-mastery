// 异步：JS 的母语——事件循环 + Promise + async/await（第 43 章讲循环本身）。

const IO_DELAY = 50;
const TASKS = 20;

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const asyncIo = async (n) => {
  await sleep(IO_DELAY);
  return n;
};

(async () => {
  console.log("== ① 钥匙实验：串行 await vs 并发 Promise.all ==");
  let t0 = Date.now();
  const serial = [];
  for (let i = 0; i < TASKS; i++) serial.push(await asyncIo(i)); // ⚠️ 循环里 await = 串行
  const serialMs = Date.now() - t0;

  t0 = Date.now();
  const parallel = await Promise.all(Array.from({ length: TASKS }, (_, i) => asyncIo(i)));
  const parallelMs = Date.now() - t0;

  console.log(`  循环里 await（串行）: ${serialMs.toString().padStart(5)} ms`);
  console.log(`  Promise.all（并发）:  ${parallelMs.toString().padStart(5)} ms（加速比 ${(serialMs / parallelMs).toFixed(1)}x）`);
  console.log(`  结果一致: ${JSON.stringify(serial) === JSON.stringify(parallel)}`);
  console.log("  ⚠️ 这是 async/await 最常见的性能坑：for 循环里 await 会退化成串行");

  console.log("\n== ② 规模：一条线程扛住上万并发 ==");
  const BIG = 10_000;
  t0 = Date.now();
  await Promise.all(Array.from({ length: BIG }, () => sleep(10)));
  console.log(`  ${BIG} 个并发定时器: ${Date.now() - t0} ms，全程单线程`);
  console.log(`  （${BIG} 个 OS 线程需要约 ${((BIG * 12.2) / 1000).toFixed(0)} ms 创建 + ${BIG} MB 栈）`);

  console.log("\n== ③ Promise 就是「堆上的续体」==");
  const p = asyncIo(1);
  console.log(`  async 函数调用立刻返回: ${p.constructor.name}（不是结果）`);
  console.log(`  函数体在 await 处暂停，剩余部分作为回调挂在 Promise 上`);
  console.log(`  await 的结果: ${await p}`);

  console.log("\n== ④ 阻塞主线程 = 整个服务卡死 ==");
  t0 = Date.now();
  await Promise.all([sleep(100), sleep(100), sleep(100)]);
  const nonBlockMs = Date.now() - t0;
  t0 = Date.now();
  for (let i = 0; i < 3; i++) {
    const end = Date.now() + 100;
    while (Date.now() < end) {} // ⚠️ 忙等：事件循环完全停转
  }
  const blockMs = Date.now() - t0;
  console.log(`  3 个 await sleep(100):  ${nonBlockMs} ms（并发 ✅）`);
  console.log(`  3 段 100ms 忙等:        ${blockMs} ms（串行 ❌ 且期间无法响应任何事件）`);
  console.log("  → CPU 密集活儿交给 worker_threads 或子进程（第 39/40 章）");

  console.log("\n== ⑤ 错误处理：await 让异步错误能被 try/catch ==");
  try {
    await Promise.reject(new Error("异步失败"));
  } catch (e) {
    console.log(`  try/catch 捕获到: ${e.message}   <- 回调时代做不到这件事`);
  }
  const settled = await Promise.allSettled([Promise.resolve(1), Promise.reject(new Error("x"))]);
  console.log(`  allSettled 不会因单个失败而全盘皆输: ${settled.map((s) => s.status).join(", ")}`);

  console.log("\n== ⑥ 并发原语四件套 ==");
  console.log("  Promise.all       : 全部成功才成功（任一失败立即失败）");
  console.log("  Promise.allSettled: 等全部结束，不管成败");
  console.log("  Promise.race      : 第一个结束的说了算（做超时控制）");
  console.log("  Promise.any       : 第一个成功的说了算");
  const winner = await Promise.race([sleep(10).then(() => "快"), sleep(200).then(() => "慢")]);
  console.log(`  race 的赢家: ${winner}`);
})();
