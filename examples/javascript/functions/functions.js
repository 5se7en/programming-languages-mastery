// 第 12 章 · 函数 — JavaScript 示例
// 运行：node functions.js

// 1. 三种定义方式
function average(scores) {
  if (scores.length === 0) return 0;
  return scores.reduce((a, b) => a + b, 0) / scores.length;
}
const avgArrow = (scores) => scores.length ? scores.reduce((a,b)=>a+b,0)/scores.length : 0;
console.log("平均分:", average([92, 75, 50]), "| 箭头函数:", avgArrow([92, 75, 50]));

// 2. 默认参数 + 剩余参数
function greet(name = "同学", ...rest) { return `${name} ${JSON.stringify(rest)}`; }
console.log("默认参数:", greet(), "| 剩余参数:", greet("Alice", 1, 2));

// 3. 值传递：传对象时复制的是「引用的副本」
function modify(obj)   { obj.score = 60; }        // 改内容 → 外部可见
function reassign(obj) { obj = { score: 0 }; }    // 重新赋值 → 外部不变
const s = { score: 92 };
modify(s);   console.log("改内容后:  ", s.score, " ← 外部可见");
reassign(s); console.log("重新赋值后:", s.score, "← 外部没变");

// 4. 闭包：函数记住了外层变量
function makeCounter() {
  let count = 0;
  return { inc: () => ++count, get: () => count };
}
const c = makeCounter();
c.inc(); c.inc();
console.log("闭包计数器:", c.get(), "← count 被记住了");

// 5. 递归与栈深度
function depth(n = 0) { try { return depth(n + 1); } catch { return n; } }
console.log("递归深度上限约:", depth(), "层");
