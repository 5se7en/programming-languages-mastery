// 包管理：npm 的答案——同一个包多版本共存。真实构造 node_modules，实测共存的能力与代价。
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');

const WORK = fs.mkdtempSync(path.join(os.tmpdir(), 'pl-mastery-npm-'));

/** 造一个假包: 目录 + package.json + index.js */
function makePkg(dir, name, version, indexJs) {
  const p = path.join(dir, 'node_modules', name);
  fs.mkdirSync(p, { recursive: true });
  fs.writeFileSync(path.join(p, 'package.json'), JSON.stringify({ name, version }));
  fs.writeFileSync(path.join(p, 'index.js'), indexJs);
  return p;
}

// util-pkg 两个版本: 同一个类 Money，v2 多了个方法
const MONEY_V1 = `
class Money {
  constructor(cents) { this.cents = cents; }
}
module.exports = { VERSION: '1.0.0', Money };
`;
const MONEY_V2 = `
class Money {
  constructor(cents) { this.cents = cents; }
  format() { return (this.cents / 100).toFixed(2); }
}
module.exports = { VERSION: '2.0.0', Money };
`;

// ---------- 搭出真实的 node_modules 树 ----------
// app
// ├── node_modules/util-pkg@2        ← 顶层（被提升的那个）
// ├── node_modules/lib-a
// │   └── node_modules/util-pkg@1    ← lib-a 需要 v1，与顶层冲突 → 嵌套安装
// └── node_modules/lib-b             ← lib-b 需要 v2 → 用顶层的（提升）
makePkg(WORK, 'util-pkg', '2.0.0', MONEY_V2);
const libA = makePkg(WORK, 'lib-a', '1.0.0', `
const util = require('util-pkg');                    // 解析到自己的 node_modules → v1
module.exports = {
  utilVersion: util.VERSION,
  makeMoney: (c) => new util.Money(c),
  MoneyClass: util.Money,
};
`);
makePkg(libA, 'util-pkg', '1.0.0', MONEY_V1);         // lib-a 私有的 v1
makePkg(WORK, 'lib-b', '1.0.0', `
const util = require('util-pkg');                    // 自己没有 → 向上找到顶层 → v2
module.exports = {
  utilVersion: util.VERSION,
  format: (m) => m.format(),
  isMoney: (m) => m instanceof util.Money,
  MoneyClass: util.Money,
};
`);

const appRequire = (name) => require(path.join(WORK, 'node_modules', name));

console.log('== ① 多版本共存：npm 与 Maven/pip 的根本分野（真实 node_modules 实测）==');
console.log('  构造的目录树:');
console.log('    node_modules/util-pkg@2.0.0          ← 顶层（提升）');
console.log('    node_modules/lib-a/node_modules/util-pkg@1.0.0   ← 冲突版本嵌套安装');
console.log('    node_modules/lib-b                   ← 无私有副本，用顶层的');
const a = appRequire('lib-a');
const b = appRequire('lib-b');
console.log(`  lib-a 看到的 util-pkg: ${a.utilVersion}`);
console.log(`  lib-b 看到的 util-pkg: ${b.utilVersion}`);
console.log('  → 同一个进程里【两个版本同时存在】——Python 版 ⑥ 说过 pip 做不到这件事');
console.log('  → 机制: require 从【调用文件所在目录】逐级向上找 node_modules（第 14 章的解析）');
console.log('  → 所以 npm 面对钻石冲突根本不用回溯: 冲突了就各装各的');

console.log('\n== ② 共存的代价一：instanceof 跨版本失败（实测经典 bug）==');
const money = a.makeMoney(1250);                       // lib-a 用 v1 的 Money 类创建
console.log(`  lib-a 创建 Money(1250)，传给 lib-b 检查:`);
console.log(`  lib-b 的 m instanceof Money: ${b.isMoney(money)}   ← false！`);
console.log(`  两个 Money 类是同一个吗: ${a.MoneyClass === b.MoneyClass}`);
console.log('  → 同名同结构的类，因为来自【两份模块实例】，instanceof 直接失败');
console.log('  → 真实世界的形态: 「Invalid hook call」(两份 React)、GraphQL 双实例报错……');
try {
  b.format(money);
} catch (e) {
  console.log(`  更糟——lib-b 调 v2 才有的方法: ✗ ${e.constructor.name}: ${e.message}`);
}
console.log('  → v1 的实例没有 format 方法——多版本共存把冲突从【装包时】推迟到了【运行时】');

console.log('\n== ③ 共存的代价二：幽灵依赖（实测）==');
const appDeclared = ['lib-a', 'lib-b'];                // app 的 package.json 只声明了这两个
let phantom;
try {
  phantom = appRequire('util-pkg');                    // app 自己【没声明】util-pkg……
  console.log(`  app 没声明 util-pkg，却 require 成功了: v${phantom.VERSION}`);
} catch { console.log('  require 失败（不应该）'); }
console.log(`  app 声明的依赖: ${JSON.stringify(appDeclared)}`);
console.log('  → 因为提升(hoisting)把 util-pkg 放到了顶层——物理上够得着 ≠ 逻辑上声明过');
console.log('  → 幽灵依赖的炸法: 哪天 lib-b 不再依赖 util-pkg，提升消失，app 直接 require 失败');
console.log('  → pnpm 的答案: 符号链接结构让「没声明的包物理上就够不着」——从机制上消灭幽灵');

console.log('\n== ④ 提升(hoisting)算法：手写 npm 的 node_modules 布局器 ==');
function hoist(tree) {
  const top = new Map();                               // 顶层: 包名 → 版本
  const nested = [];                                   // 装不进顶层的 → 嵌套
  const walk = (deps, owner) => {
    for (const [name, { version, deps: sub = {} }] of Object.entries(deps)) {
      if (!top.has(name)) top.set(name, version);      // 第一个到的占坑
      else if (top.get(name) !== version) nested.push(`${owner}/node_modules/${name}@${version}`);
      walk(sub, `${owner}/node_modules/${name}`);
    }
  };
  walk(tree, '');
  return { top, nested };
}
const { top, nested } = hoist({
  'lib-a': { version: '1.0.0', deps: { 'util-pkg': { version: '1.0.0' } } },
  'lib-b': { version: '1.0.0', deps: { 'util-pkg': { version: '2.0.0' } } },
  'lib-c': { version: '1.0.0', deps: { 'util-pkg': { version: '2.0.0' } } },
});
console.log('  输入依赖树: lib-a→util@1, lib-b→util@2, lib-c→util@2');
console.log(`  顶层: ${[...top].map(([n, v]) => `${n}@${v}`).join(', ')}`);
console.log(`  嵌套: ${nested.join(', ') || '无'}`);
console.log('  ⚠️ 注意: util-pkg@1 占了顶层坑（lib-a 先声明）——【声明顺序决定谁被提升】');
console.log('  → 换个顺序，node_modules 布局就不同——这就是 npm 需要 lockfile 的原因之一:');
console.log('     package-lock.json 锁的不只是版本，还有【整棵树的物理布局】');

console.log('\n== ⑤ 五家包管理器的冲突答案 ==');
console.log('  npm/yarn : 多版本共存（嵌套）——不用解冲突，但有 ②③ 的代价');
console.log('  pnpm     : 共存 + 符号链接严格隔离——消灭幽灵依赖');
console.log('  pip/uv   : 全局单版本 + 回溯求解（Python 版实测）');
console.log('  Maven    : 全局单版本 + 最近者胜——不求解也不报错（Java 版实测它的炸法）');
console.log('  cargo    : 语义化共存——major 版本不同可共存，相同则统一（折中方案）');
console.log('  → 同一个 NP 完全问题，五种工程取舍——没有免费的答案');

fs.rmSync(WORK, { recursive: true, force: true });
