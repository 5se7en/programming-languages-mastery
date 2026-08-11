// 线程：JS 主世界是单线程；worker_threads 给了真线程，但默认不共享对象。

const { Worker, isMainThread, workerData, parentPort } = require("worker_threads");
const os = require("os");

if (!isMainThread) {
  // ---- Worker 线程：对 SharedArrayBuffer 做自增 ----
  const { buffer, times, useAtomics } = workerData;
  const view = new Int32Array(buffer);
  for (let i = 0; i < times; i++) {
    if (useAtomics) Atomics.add(view, 0, 1); // ✅ 原子
    else view[0] = view[0] + 1; // ⚠️ 读-改-写三步
  }
  parentPort.postMessage("done");
  return;
}

// ---- 主线程 ----
console.log("== ① JS 的默认世界：单线程 ==");
console.log(`  CPU 核数 = ${os.cpus().length}，但主线程只有一条（第 43 章事件循环）`);
console.log("  worker_threads 提供真线程——但默认「什么都不共享」（消息传递）");

const N = 200_000;

function runWorkers(useAtomics) {
  return new Promise((resolve) => {
    const buffer = new SharedArrayBuffer(4); // 唯一能共享的东西
    const view = new Int32Array(buffer);
    view[0] = 0;
    let done = 0;
    for (let i = 0; i < 2; i++) {
      const w = new Worker(__filename, { workerData: { buffer, times: N, useAtomics } });
      w.on("message", () => {
        if (++done === 2) resolve(view[0]);
      });
    }
  });
}

(async () => {
  console.log("\n== ② 钥匙实验：数据竞争（SharedArrayBuffer 上）==");
  for (let run = 1; run <= 3; run++) {
    const got = await runWorkers(false);
    console.log(`  第 ${run} 次运行: 期望 ${2 * N}，实际 ${got}   （丢了 ${2 * N - got} 次）`);
  }
  console.log("  ↑ 一旦真的共享内存，JS 也逃不掉数据竞争");

  console.log("\n== ③ Atomics 修复 ==");
  for (let run = 1; run <= 2; run++) {
    const got = await runWorkers(true);
    console.log(`  第 ${run} 次运行: 期望 ${2 * N}，实际 ${got}   ✅`);
  }

  console.log("\n== ④ 为什么 JS 敢说自己「没有数据竞争」==");
  console.log("  因为默认情况下 worker 之间传对象是结构化克隆（拷贝，第 39 章）");
  console.log("  只有显式使用 SharedArrayBuffer 才真共享内存——竞争也随之而来");
  console.log("  这是「默认安全，按需危险」的设计（对比 Java/C++ 的默认共享）");

  console.log("\n== ⑤ 主线程的铁律：不要阻塞 ==");
  console.log("  主线程跑重计算 = 整个服务卡死（事件循环停转，第 43 章）");
  console.log("  CPU 密集活儿交给 worker_threads 或子进程（第 39 章 cluster）");
})();
