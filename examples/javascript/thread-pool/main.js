// 线程池：你不创建线程，但线程池决定你的吞吐——Node 的池藏在 libuv 里。
'use strict';
const crypto = require('crypto');
const os = require('os');
const { execFileSync } = require('child_process');
const { Worker } = require('worker_threads');

const ITER = 100000;

// 跑 n 个 pbkdf2（走 libuv 线程池），记录每个完成的时刻
function pbkdf2Batch(n) {
  const t0 = Date.now();
  const finish = [];
  return Promise.all(
    Array.from({ length: n }, (_, i) =>
      new Promise((res) =>
        crypto.pbkdf2(`pw${i}`, 'salt', ITER, 64, 'sha512', () => {
          finish.push(Date.now() - t0);
          res();
        })
      )
    )
  ).then(() => ({ total: Date.now() - t0, finish: finish.sort((a, b) => a - b) }));
}

// ④ 手写「并发限流器」——JS 世界的线程池等价物
async function withLimit(makeTask, count, limit) {
  let inFlight = 0, peak = 0, idx = 0;
  const t0 = Date.now();
  async function runner() {
    while (idx < count) {
      idx++;
      inFlight++; peak = Math.max(peak, inFlight);
      await makeTask();
      inFlight--;
    }
  }
  await Promise.all(Array.from({ length: limit }, runner));
  return { ms: Date.now() - t0, peak };
}

async function noLimit(makeTask, count) {
  let inFlight = 0, peak = 0;
  const t0 = Date.now();
  await Promise.all(
    Array.from({ length: count }, async () => {
      inFlight++; peak = Math.max(peak, inFlight);
      await makeTask();
      inFlight--;
    })
  );
  return { ms: Date.now() - t0, peak };
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  console.log('== ① Node 的线程池：藏在 libuv 里的 4 条线程 ==');
  console.log(`  本机 CPU 核心数: ${os.cpus().length}`);
  console.log('  UV_THREADPOOL_SIZE 默认值: 4（与核心数无关！）');
  const r = await pbkdf2Batch(8);
  console.log(`  8 个 pbkdf2 任务总耗时: ${r.total} ms`);
  console.log(`  各任务完成时刻(ms): ${r.finish.join(', ')}`);
  console.log('  ↑ 前 4 个几乎同时完成，后 4 个晚一批——池大小 4 直接印在时间轴上');

  console.log('\n== ② 把池调大：UV_THREADPOOL_SIZE=8 ==');
  const script =
    `const c=require('crypto');const t0=Date.now();let n=0;` +
    `for(let i=0;i<8;i++)c.pbkdf2('pw'+i,'salt',${ITER},64,'sha512',()=>{if(++n===8)console.log(Date.now()-t0)});`;
  const out = execFileSync(process.execPath, ['-e', script], {
    env: { ...process.env, UV_THREADPOOL_SIZE: '8' },
  }).toString().trim();
  const bigger = Number(out);
  console.log(`  同样 8 个任务，池大小 8: ${bigger} ms（池大小 4 时 ${r.total} ms）`);
  console.log(`  → 加速 ${(r.total / bigger).toFixed(2)}x —— 一个环境变量就改变了吞吐`);
  console.log('  ⚠️ 这个池被 crypto / zlib / dns.lookup / 大部分 fs 操作共用');

  console.log('\n== ③ worker_threads：真正的 JS 线程，但很贵 ==');
  const costs = [];
  for (let i = 0; i < 4; i++) {
    const t0 = Date.now();
    await new Promise((resolve, reject) => {
      const w = new Worker('require("worker_threads").parentPort.postMessage(1)', { eval: true });
      w.on('message', () => { costs.push(Date.now() - t0); w.terminate().then(resolve); });
      w.on('error', reject);
    });
  }
  const avg = costs.reduce((a, b) => a + b, 0) / costs.length;
  console.log(`  创建 4 个 Worker 各耗时(ms): ${costs.join(', ')}`);
  console.log(`  平均 ${avg.toFixed(1)} ms/个 —— 每个 Worker 是一个独立的 V8 隔离区（新堆、新事件循环）`);
  console.log('  → 比 OS 线程（第 40 章实测 12.2 μs）贵三个数量级 → 必须池化，绝不能每任务一个');

  console.log('\n== ④ 并发限流器：JS 世界的线程池 ==');
  const N = 200;
  const task = () => sleep(10);
  const free = await noLimit(task, N);
  const cap = await withLimit(task, N, 4);
  console.log(`  不限流: 峰值并发 ${free.peak}，耗时 ${free.ms} ms`);
  console.log(`  限流 4: 峰值并发 ${cap.peak}，耗时 ${cap.ms} ms`);
  console.log(`  → 限流慢了 ${(cap.ms / free.ms).toFixed(1)}x，换来的是【资源可控】`);
  console.log('  → 200 个并发数据库连接会打垮数据库；4 个不会——这就是池存在的第二个理由');

  console.log('\n== ⑤ 池大小的两个公式 ==');
  console.log(`  CPU 密集: 线程数 ≈ 核心数 = ${os.cpus().length}（再多只会增加切换开销）`);
  console.log('  I/O 密集: 线程数 ≈ 核心数 × (1 + 等待时间/计算时间)');
  console.log(`    例：等待 90ms 计算 10ms → ${os.cpus().length} × (1 + 9) = ${os.cpus().length * 10} 条`);
  console.log('  Node 的特殊之处: JS 代码永远单线程（第 43 章），池只服务 C++ 层的阻塞操作');
})();
