// 部署：Node 的交付物——运行时是一个 100 MB 的二进制，依赖是几万个小文件。
'use strict';
const fs = require('fs');
const path = require('path');
const os = require('os');

// ⚠️ 必须在最前面读: process.uptime() 是「Node 进程启动到现在」的秒数
const startupMs = process.uptime() * 1000;

console.log('== ① 启动时间：你的代码之前发生了什么（实测）==');
const t0 = process.hrtime.bigint();
let s = 0;
for (let i = 0; i < 5_000_000; i++) s += i % 7;
const workMs = Number(process.hrtime.bigint() - t0) / 1e6;
console.log(`  Node 启动 → 执行到本文件第 1 行: ${startupMs.toFixed(1)} ms`);
console.log(`  一段 500 万次循环的业务代码:     ${workMs.toFixed(1)} ms（结果 ${s}）`);
console.log('  → 这段启动时间用来: 初始化 V8、建立事件循环（第 43 章）、加载内置模块');
console.log('  → 它是【每次进程启动都要付】的固定成本');
console.log('  → 对长驻服务可忽略；对 CLI 工具和 Serverless，它就是用户感知的延迟');

console.log('\n== ② 交付物：Node 运行时有多大（实测）==');
const nodeBin = fs.statSync(process.execPath);
console.log(`  process.execPath = ${process.execPath}`);
console.log(`  node 可执行文件: ${(nodeBin.size / 1048576).toFixed(1)} MB（单个文件，V8 都在里面）`);
console.log(`  内置模块数量: ${require('module').builtinModules.length}`);
console.log('  → 与 Java 版对照: JVM 运行时是【几百个文件、300 MB】，node 是【一个 100 MB 的二进制】');
console.log('  → 单文件的好处: 分发简单、没有「装错版本的运行时」问题');
console.log('  → 单文件的代价: 无法裁剪——你用不到的模块也在里面');

console.log('\n== ③ 依赖：数量比体积更麻烦（实测本项目 node_modules）==');
function walk(dir, limitMs = 4000) {
  let bytes = 0, files = 0, dirs = 0, deepest = 0;
  const t = Date.now();
  const stack = [[dir, 0]];
  while (stack.length) {
    if (Date.now() - t > limitMs) return { bytes, files, dirs, deepest, truncated: true };
    const [d, depth] = stack.pop();
    deepest = Math.max(deepest, depth);
    let ents;
    try { ents = fs.readdirSync(d, { withFileTypes: true }); } catch { continue; }
    for (const e of ents) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) { dirs++; stack.push([p, depth + 1]); }
      else if (e.isFile()) { try { bytes += fs.statSync(p).size; files++; } catch { } }
    }
  }
  return { bytes, files, dirs, deepest, truncated: false };
}

// 找一个真实存在的 node_modules；没有就跳过这段统计
let nm = null;
for (let d = process.cwd(), i = 0; i < 6; i++, d = path.dirname(d)) {
  const c = path.join(d, 'node_modules');
  if (fs.existsSync(c)) { nm = c; break; }
}
if (nm) {
  const r = walk(nm);
  console.log(`  找到 ${nm}`);
  console.log(`    ${r.files.toLocaleString()} 个文件 / ${r.dirs.toLocaleString()} 个目录`
    + ` / ${(r.bytes / 1048576).toFixed(1)} MB / 最深 ${r.deepest} 层`
    + `${r.truncated ? '（统计超时，实际更多）' : ''}`);
  console.log(`    平均每个文件 ${(r.bytes / Math.max(r.files, 1) / 1024).toFixed(1)} KB —— 【全是小文件】`);
} else {
  console.log('  （本仓库的示例不依赖任何第三方包，没有 node_modules 可统计）');
}

// 直接实测「文件数量」本身的代价: 同样的总字节数，一个大文件 vs 几千个小文件
console.log('  实测「文件数量」的代价——总字节数完全相同，只改文件个数:');
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'deploy-'));
const TOTAL = 8 * 1024 * 1024;                 // 8 MB
const N_SMALL = 4000;                          // 4000 个 2 KB 的文件
const chunk = Buffer.alloc(TOTAL / N_SMALL, 0x61);
try {
  const oneDir = path.join(tmp, 'one'), manyDir = path.join(tmp, 'many');
  fs.mkdirSync(oneDir); fs.mkdirSync(manyDir);
  fs.writeFileSync(path.join(oneDir, 'bundle.js'), Buffer.alloc(TOTAL, 0x61));
  for (let i = 0; i < N_SMALL; i++) fs.writeFileSync(path.join(manyDir, `m${i}.js`), chunk);

  const copyDir = (src, dst) => {
    fs.mkdirSync(dst, { recursive: true });
    for (const f of fs.readdirSync(src)) fs.copyFileSync(path.join(src, f), path.join(dst, f));
  };
  const timeIt = (fn) => { const t = process.hrtime.bigint(); fn(); return Number(process.hrtime.bigint() - t) / 1e6; };
  const msOne = timeIt(() => copyDir(oneDir, path.join(tmp, 'one-copy')));
  const msMany = timeIt(() => copyDir(manyDir, path.join(tmp, 'many-copy')));
  console.log(`    1 个 ${(TOTAL / 1048576).toFixed(0)} MB 的文件      → 复制耗时 ${msOne.toFixed(1)} ms`);
  console.log(`    ${N_SMALL} 个 ${(TOTAL / N_SMALL / 1024).toFixed(0)} KB 的文件 → 复制耗时 ${msMany.toFixed(1)} ms`
    + `（慢 ${(msMany / Math.max(msOne, 0.001)).toFixed(0)}x）`);
  console.log('    → 字节数一模一样，慢的部分【全是每个文件的元数据开销】（open/write/close）');
} finally {
  fs.rmSync(tmp, { recursive: true, force: true });
}
console.log('  → 第 53 章实测过依赖【数量】，这里量的是它对【部署】的代价:');
console.log('     ⓐ 容器构建慢: 复制几十万个小文件比复制一个大文件慢得多');
console.log('     ⓑ 层缓存易失效: package.json 动一个字符，整层依赖全部重装');
console.log('     ⓒ 冷启动慢: 每个 require() 都是一次文件系统查找');
console.log('  → 所以生产部署普遍要【打包】(esbuild/webpack/ncc): 几万个文件 → 一个文件');
console.log('     打包在这里的意义不是「体积更小」，而是【文件数量少了几个数量级】');

console.log('\n== ④ 「在我机器上是好的」：Node 的三种成因 ==');
console.log(`  当前环境: node ${process.version} / ${process.platform}-${process.arch}`
  + ` / ${os.cpus().length} 核`);
console.log('  成因一【原生模块】: 含 C++ 扩展的包按【本机 ABI】编译');
console.log('     在 macOS 上 npm install，把 node_modules 拷进 Linux 容器 → 直接崩');
console.log('     这也是 .dockerignore 必须包含 node_modules 的原因');
console.log('  成因二【lockfile 没提交或没用上】: npm install 会按 semver 范围重新解析，');
console.log('     两次构建可能拿到不同版本 —— 必须用 npm ci（第 53 章）');
console.log('  成因三【隐式全局状态】: 环境变量、时区、locale、可写的临时目录');
console.log(`     例: 当前时区 ${Intl.DateTimeFormat().resolvedOptions().timeZone}，`
  + '容器里默认通常是 UTC —— 所有日期逻辑都会变');

console.log('\n== ⑤ 健康检查：部署系统怎么知道「起来了」 ==');
console.log('  liveness（活着吗）:  挂了就重启。检查项要【尽量少】——');
console.log('     如果 liveness 依赖数据库，数据库抖一下会让全部实例被同时重启');
console.log('  readiness（能收流量吗）: 依赖没就绪就先不发流量，但【不要重启】');
console.log('  startup（还在启动吗）: 给慢启动的应用一个宽限期，避免被 liveness 误杀');
console.log('  → 三者分开是有原因的: 把它们混成一个，会让「依赖抖动」升级成「集体重启」');
console.log('  → 关键设计: 优雅关闭要先【摘流量】再退出——');
console.log('     收到 SIGTERM → readiness 立刻置为 false → 等在途请求处理完 → 退出');
console.log('     少了这一步，每次发布都会丢掉一批请求');

console.log('\n== ⑥ 十二要素里最实用的三条 ==');
console.log('  ① 配置放环境变量，不放代码: 同一个产物能跑在所有环境');
console.log('     → 推论: 【构建一次，到处部署】。为每个环境单独构建等于放弃了可复现性');
console.log('  ② 进程无状态: 状态放数据库/缓存/对象存储，实例可随时被杀掉重建');
console.log('     → 这是水平扩容与滚动发布能成立的前提');
console.log('  ③ 日志写 stdout: 由平台收集，应用不管理日志文件的轮转与归档');
console.log('     → 应用里写死日志路径，等于假设了一种文件系统布局');
console.log('  → 这三条的共同点: 【减少产物对环境的假设】——');
console.log('     假设越少，能跑起来的地方就越多，「在我机器上是好的」出现的概率就越低');
