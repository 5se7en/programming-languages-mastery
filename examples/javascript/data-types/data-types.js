// 第 09 章 · 数据类型 — JavaScript 示例
// 运行：node data-types.js

// 1. 只有一个 number 类型（双精度浮点），没有整数类型
let score = 92;
console.log("score 是整数值吗:", Number.isInteger(score), "| typeof:", typeof score);

// 2. 浮点误差（IEEE 754，所有语言共有）
console.log("0.1 + 0.2 =", 0.1 + 0.2);
console.log("等于 0.3 吗:", 0.1 + 0.2 === 0.3);
console.log("用容差比较:", Math.abs((0.1 + 0.2) - 0.3) < 1e-9);

// 3. 安全整数上限：大 ID 会丢精度
console.log("MAX_SAFE_INTEGER =", Number.MAX_SAFE_INTEGER);
console.log("超出后:", 9007199254740993, "← 末位被改了");
console.log("用 BigInt 才精确:", 9007199254740993n + 2n);

// 4. 字符串长度数的是 UTF-16 码元
const wave = "👋";
console.log("'👋'.length =", wave.length, "| 按字符数 =", [...wave].length);

// 5. 两种空值
let a;
console.log("undefined:", a, "| null:", null);
