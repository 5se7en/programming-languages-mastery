// 第 17 章 · 列表 — JavaScript 示例
// 运行：node main.js

// 1. JS 的 Array 天生动态，但不暴露容量
const scores = [92, 75];
scores.push(88);           // 末尾追加：摊还 O(1)
console.log("push 后:", scores, "| length:", scores.length);
console.log("V8 不暴露 capacity，无法预分配（不像 C++ 的 reserve）");

// 2. 各操作的复杂度差异
const N = 30000;
let t = process.hrtime.bigint();
const byPush = [];
for (let i = 0; i < N; i++) byPush.push(i);          // O(1) 摊还
const pushMs = Number(process.hrtime.bigint() - t) / 1e6;

t = process.hrtime.bigint();
const byUnshift = [];
for (let i = 0; i < N; i++) byUnshift.unshift(i);    // O(n) 每次搬移全部
const unshiftMs = Number(process.hrtime.bigint() - t) / 1e6;

console.log(`\n追加 ${N} 个元素:`);
console.log(`  push   (末尾, 摊还O(1)): ${pushMs.toFixed(1)} ms`);
console.log(`  unshift(头部, O(n))    : ${unshiftMs.toFixed(1)} ms`);
console.log(`  → 头部插入慢 ${(unshiftMs / pushMs).toFixed(0)} 倍（整体退化成 O(n²)）`);

// 3. 正确做法：先 push 再 reverse
t = process.hrtime.bigint();
const fixed = [];
for (let i = 0; i < N; i++) fixed.push(i);
fixed.reverse();
const fixMs = Number(process.hrtime.bigint() - t) / 1e6;
console.log(`  ✓ push + reverse 达到同样效果: ${fixMs.toFixed(1)} ms`);

// 4. TypedArray：定长、连续、同类型
const buf = new Int32Array(5);
buf[0] = 92;
console.log("\nTypedArray(定长连续):", buf, "| 字节数:", buf.byteLength);
