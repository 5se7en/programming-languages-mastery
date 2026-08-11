// 事件循环：让单线程扛住万级并发的引擎——两级队列 + 六个阶段。

console.log("== ① 钥匙实验：经典输出顺序题 ==");
console.log("  代码顺序: 同步1 → setTimeout → Promise → queueMicrotask → nextTick → 同步2");
console.log("  实际输出:");

console.log("    1. 同步代码（栈上直接执行）");

setTimeout(() => console.log("    6. setTimeout 0ms（宏任务：timers 阶段）"), 0);
setImmediate(() => console.log("    7. setImmediate（宏任务：check 阶段）"));

Promise.resolve().then(() => console.log("    4. Promise.then（微任务）"));
queueMicrotask(() => console.log("    5. queueMicrotask（微任务，与 Promise 同队列）"));
process.nextTick(() => console.log("    3. process.nextTick（Node 特权队列，优先于微任务）"));

console.log("    2. 同步代码（同步永远先跑完）");

setTimeout(() => {
  console.log("\n== ② 规则总结（由上面的顺序推出）==");
  console.log("  ① 同步代码跑完 → ② nextTick 队列 → ③ 微任务队列 → ④ 宏任务（一个）");
  console.log("  → 每跑完一个宏任务，都要把微任务队列清空才继续下一个宏任务");

  demoMicrotaskPriority();
}, 10);

function demoMicrotaskPriority() {
  console.log("\n== ③ 每个宏任务之后都会清空微任务 ==");
  const order = [];
  setTimeout(() => {
    order.push("宏任务A");
    Promise.resolve().then(() => order.push("  ↳A的微任务"));
  }, 0);
  setTimeout(() => {
    order.push("宏任务B");
    Promise.resolve().then(() => order.push("  ↳B的微任务"));
  }, 0);
  setTimeout(() => {
    console.log("  " + order.join(" → "));
    console.log("  ↑ 不是 A、B、微A、微B，而是 A、微A、B、微B");
    demoStarvation();
  }, 20);
}

function demoStarvation() {
  console.log("\n== ④ 钥匙实验二：微任务饿死宏任务 ==");
  const TOTAL = 200_000;
  let microCount = 0;
  let macroRan = false;
  const t0 = Date.now();

  setTimeout(() => (macroRan = true), 0); // 宏任务：0ms 后就该跑

  function greedyMicrotask() {
    microCount++;
    if (microCount === TOTAL) {
      // 在微任务链的最后一环检查：此刻队列即将清空，但宏任务还没轮到
      console.log(`  排完 ${TOTAL} 个微任务、耗时 ${Date.now() - t0} ms 之后，`);
      console.log(`  那个 0ms 的 setTimeout 执行了吗: ${macroRan} ❌`);
      console.log("  ↑ 微任务队列必须彻底清空才轮到宏任务——期间宏任务一个都跑不了");
      console.log("  （真实事故：递归 Promise 让定时器/IO 回调永不触发，服务假死）");
      return;
    }
    Promise.resolve().then(greedyMicrotask); // 微任务里再排微任务
  }
  greedyMicrotask();

  setTimeout(demoPhases, 50);
}

function demoPhases() {
  console.log("\n== ⑤ libuv 的六个阶段 ==");
  console.log("  timers      : setTimeout / setInterval 到期回调");
  console.log("  pending     : 上一轮延后的系统回调（如 TCP 错误）");
  console.log("  idle/prepare: 内部使用");
  console.log("  poll        : ⭐ 取 I/O 事件，必要时在这里阻塞等待");
  console.log("  check       : setImmediate 回调");
  console.log("  close       : close 事件（socket.on('close')）");
  console.log("  → 每个阶段之间、每个回调之后，都会清空 nextTick + 微任务队列");

  const fs = require("fs");
  fs.readFile(__filename, () => {
    console.log("\n== ⑥ I/O 回调里：setImmediate 一定先于 setTimeout ==");
    setTimeout(() => {
      console.log("    setTimeout（下一轮的 timers 阶段）← 后跑");
      finale();
    }, 0);
    setImmediate(() => console.log("    setImmediate（本轮的 check 阶段，紧跟 poll）← 先跑"));
  });
}

function finale() {
  console.log("\n== ⑦ 事件循环的一句话 ==");
  console.log("  「取一个任务 → 执行到底（不可抢占）→ 清空微任务 → 取下一个」");
  console.log("  正因为「执行到底」，任何长任务都会卡住整个循环（第 42 章实测过）");
  console.log("  正因为「不可抢占」，主线程代码天然无数据竞争（第 40/41 章）");
}
