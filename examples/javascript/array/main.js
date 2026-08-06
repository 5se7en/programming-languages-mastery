// 第 16 章 · 数组 — JavaScript 示例
// 运行：node main.js

const scores = [92, 75, 88];

// 1. JS 的 Array 是对象，不是独立类型
console.log("typeof:", typeof scores, "| Array.isArray:", Array.isArray(scores));
console.log("长度:", scores.length);

// 2. 可以异构、可以稀疏 —— 这是 JS 数组「不是真数组」的证据
const mixed = [1, "两", true, null];
console.log("异构数组:", mixed);
const sparse = [1, , 3];
sparse[100] = 9;
console.log("稀疏数组长度:", sparse.length, "← 赋值 a[100] 让长度变成 101");

// 3. ⚠️ 越界不报错，静默返回 undefined（五种语言里最危险的行为之一）
console.log("a[10] →", scores[10], " ← 不报错！");
console.log("a[-1] →", scores[-1], "← 不支持负索引（与 Python 不同）");

// 4. 高阶函数
console.log("map:", scores.map(s => Math.round(s * 1.1)));
console.log("filter:", scores.filter(s => s >= 80));
console.log("reduce 求和:", scores.reduce((a, b) => a + b, 0));

// 5. 缓存局部性：行优先 vs 列优先（同为 O(n²)）
const N = 1000;
const m = Array.from({ length: N }, () => new Int32Array(N).fill(1));
let t = process.hrtime.bigint();
let s1 = 0;
for (let i = 0; i < N; i++) for (let j = 0; j < N; j++) s1 += m[i][j];
const rowMs = Number(process.hrtime.bigint() - t) / 1e6;
t = process.hrtime.bigint();
let s2 = 0;
for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) s2 += m[i][j];
const colMs = Number(process.hrtime.bigint() - t) / 1e6;
console.log(`\n缓存局部性: 行优先 ${rowMs.toFixed(1)}ms vs 列优先 ${colMs.toFixed(1)}ms`
  + ` → 列优先慢 ${(colMs / rowMs).toFixed(1)} 倍（校验和一致: ${s1 === s2}）`);
