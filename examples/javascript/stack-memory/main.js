// 栈内存：JS 的调用栈通过 Error.stack 可见；尾调用优化 V8 并不做；异步会切断栈。

function level3() {
  const stack = new Error().stack.split("\n").slice(1, 4);
  console.log("Error.stack 看到的调用链（栈顶在前）:");
  stack.forEach((line) => console.log(" " + line.replace(/\(.*\)/, "")));
}
function level2() { level3(); }
function level1() { level2(); }

console.log("== ① 调用栈：Error.stack 实测 ==");
level1();

console.log('\n== ② "尾递归"在 V8 里不奏效 ==');
("use strict");
function countdown(n, acc) {
  if (n === 0) return acc;
  return countdown(n - 1, acc + 1);   // 标准里的尾调用——但 V8 没有实现 TCO
}
try {
  countdown(1_000_000, 0);
} catch (e) {
  console.log(`countdown(1_000_000): ${e.constructor.name} —— 尾调用照样压栈`);
}
console.log("（ES2015 规定了尾调用优化，但主流引擎里只有 JavaScriptCore 实现）");

console.log("\n== ③ 异步切断调用栈：回调运行在全新的栈上 ==");
function caller() {
  setTimeout(function timeoutCallback() {
    const depth = new Error().stack.split("\n").length - 1;
    console.log(`setTimeout 回调里的栈深: ${depth} 帧 —— caller 的帧早已不在`);
    console.log("（回调由事件循环在空栈上重新调起——第 43 章的伏笔）");
  }, 0);
}
caller();
