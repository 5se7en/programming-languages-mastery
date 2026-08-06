// 第 20 章 · 哈希 — JavaScript 示例
// 运行：node main.js

// 1. Object vs Map：键的类型限制不同
const obj = {};
obj[1] = "数字键";
console.log("Object 的键会被转成字符串:", Object.keys(obj), "← 数字 1 变成了 '1'");

const map = new Map();
map.set(1, "数字键").set("1", "字符串键").set({id: 1}, "对象键");
console.log("Map 保持键的类型: size =", map.size, "| get(1) =", map.get(1));

// 2. Set：O(1) 判重
const seen = new Set([1, 2, 2, 3]);
console.log("\nSet 自动去重:", [...seen], "| has(2):", seen.has(2));

// 3. 哈希 vs 线性查找的性能差距
const N = 200000;
const data = Array.from({length: N}, (_, i) => `student${i}`);
const arr = data, st = new Set(data);
const targets = Array.from({length: 200}, () => data[(Math.random() * N) | 0]);

let t = process.hrtime.bigint();
for (const x of targets) arr.includes(x);          // O(n)
const arrMs = Number(process.hrtime.bigint() - t) / 1e6;
t = process.hrtime.bigint();
for (const x of targets) st.has(x);                 // O(1)
const setMs = Number(process.hrtime.bigint() - t) / 1e6;
console.log(`\n在 ${N} 个元素中查找 200 次:`);
console.log(`  Array.includes (O(n)): ${arrMs.toFixed(2)} ms`);
console.log(`  Set.has        (O(1)): ${setMs.toFixed(3)} ms`);
console.log(`  → 哈希快约 ${(arrMs / setMs).toFixed(0)} 倍`);

// 4. ⚠️ 对象键按引用比较
const m2 = new Map();
m2.set({a: 1}, "值");
console.log("\n⚠️ 用另一个 {a:1} 取值:", m2.get({a: 1}), "← 对象键按引用比较，取不到");

// 5. 词频统计（哈希最经典的应用）
const words = "the quick brown fox jumps over the lazy dog the fox".split(" ");
const counts = new Map();
for (const w of words) counts.set(w, (counts.get(w) ?? 0) + 1);
const top = [...counts].sort((a, b) => b[1] - a[1]).slice(0, 3);
console.log("\n词频 Top3:", top.map(([w, c]) => `${w}:${c}`).join(" "));

// 6. Map 保持插入顺序
const ordered = new Map([["zebra", 1], ["apple", 2], ["mango", 3]]);
console.log("Map 保持插入顺序:", [...ordered.keys()]);
