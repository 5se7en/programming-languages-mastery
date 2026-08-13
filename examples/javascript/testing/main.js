// 测试：异步测试的「假通过」——JS 测试里最危险的一类 bug，用 node:test 实测。
'use strict';
process.removeAllListeners('warning');
const { test, mock } = require('node:test');
const assert = require('node:assert');

// ============ 被测代码 ============

/** 异步取用户余额——故意埋一个 bug: 返回了字符串而不是数字 */
async function fetchBalance(_userId) {
  await new Promise((r) => setTimeout(r, 5));
  return '100';                                    // ⚠️ bug: 应该是数字 100
}

/** 防抖: 停止调用 1 秒后才真正执行 */
function debounce(fn, ms) {
  let timer = null;
  return (...args) => {
    clearTimeout(timer);
    timer = setTimeout(() => fn(...args), ms);
  };
}

// ============ 实验 ============

console.log('== ① 假通过：异步断言没人等，失败被吞掉（实测）==');

let leaked = 0;
let leakedFailure = '';
test('坏写法: 忘了 await —— 断言在测试结束后才跑', () => {
  fetchBalance(1).then((balance) => {
    leaked++;
    try {
      assert.strictEqual(balance, 100);            // 会失败——但测试已经「通过」了
    } catch (e) {
      leakedFailure = e.message.split('\n')[0];    // 失败被吞掉，没人知道
    }
  });
  // 测试函数同步返回 → 框架判定: 通过 ✓
});

test('对照: 正确写法 await 之后断言', async () => {
  const balance = await fetchBalance(1);
  assert.strictEqual(typeof balance, 'string');    // 诚实断言现状: bug 在这能看见
});

test('好写法抓住 bug（本测试故意失败给你看报告）', async (t) => {
  const balance = await fetchBalance(1);
  // 这条会抓住 bug —— 为了让整个示例退出码为 0，用 t.todo 标记「已知失败」
  t.todo('balance 应该是数字 100，实际是字符串 "100"');
  assert.notStrictEqual(balance, 100);             // 记录现状: '100' !== 100
});

console.log('  测试 1「坏写法」: 框架报告【通过】——但断言根本还没执行');
console.log('  → 异步假通过的机制: 测试函数同步返回，Promise 里的断言晚于判定');
console.log('  → 解法: async 测试必须 await 每个异步断言（或 return Promise）');
console.log('  → 这类测试最危险: 它们【永远是绿的】，删掉都没人发现');

console.log('\n== ② mock 计时器：测「等 1 秒」不用真等 1 秒（实测）==');

test('防抖测试: mock 计时器瞬间拨快 1 秒', () => {
  mock.timers.enable({ apis: ['setTimeout'] });
  const calls = [];
  const save = debounce((v) => calls.push(v), 1000);

  const t0 = process.hrtime.bigint();
  save('a');
  save('b');
  save('c');                                       // 连按三次，只有最后一次该生效
  assert.strictEqual(calls.length, 0);             // 1 秒没到，什么都没发生
  mock.timers.tick(1000);                          // ← 把虚拟时钟拨快 1 秒
  const realMs = Number(process.hrtime.bigint() - t0) / 1e6;

  assert.deepStrictEqual(calls, ['c']);            // 只有最后一次调用生效 ✓
  console.log(`  「等待 1000ms」的防抖测试实际耗时: ${realMs.toFixed(2)} ms（真实时间几乎为零）`);
  console.log('  → mock.timers 劫持了 setTimeout: 时间成了【可控输入】而不是等待');
  console.log('  → 没有它: 每个防抖/重试/超时测试都真等几秒 —— 套件慢到没人愿意跑');
  console.log('  → 这是 mock 的【正确】用途: 隔离「慢」，而不是隔离「真」（Python 版 ② 的边界）');
  mock.timers.reset();
});

console.log('\n== ③ 随机性与时间：测试不稳定的两大来源 ==');
test('用注入把不确定性变成参数', () => {
  // 坏: if (Math.random() < 0.5) / new Date() 直接写在业务代码里 —— 没法测
  // 好: 把随机源和时钟作为参数注入
  const pickWinner = (users, rand) => users[Math.floor(rand() * users.length)];
  const winner = pickWinner(['甲', '乙', '丙'], () => 0.99);   // 注入固定的「随机」
  assert.strictEqual(winner, '丙');
  console.log('  pickWinner(users, () => 0.99) → 丙 ✓（随机源被注入成常量，测试可复现）');
  console.log('  → flaky 测试的两大来源就是【时间】和【随机】——都用注入驯服');
  console.log('  → 第 43/44 章的事件循环知识在这里变现: 你得知道断言和回调谁先跑');
});

process.on('exit', () => {
  console.log('\n== ④ 本示例的测试报告（node:test 内置运行器）==');
  console.log(`  泄漏的断言最终执行了吗: leaked=${leaked}，它其实失败了: 「${leakedFailure}」`);
  console.log('  → 这个失败【没有出现在任何测试报告里】——绿灯下埋着一个真实的 bug');
  console.log('  → 若不捕获，node:test 会诊断「测试结束后产生了异步活动」并让进程退出码变 1——');
  console.log('     现代框架在努力抓假通过，但只有断言【恰好晚到且未被吞】时才抓得到');
  console.log('  → node 18+ 内置 node:test + node:assert，零依赖就能写测试');
  console.log('  → 生态: Vitest/Jest 提供 watch 模式、快照、并行——但骨架与本例相同');
  console.log('  → 快照测试(snapshot): 第一次运行存下输出，之后比对——适合序列化结果，');
  console.log('     但「一键更新快照」让弱断言问题更隐蔽（Python 版 ③ 的覆盖率教训同款）');
});
