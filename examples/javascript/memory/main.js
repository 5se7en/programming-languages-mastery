// 内存：JS 拿不到地址，但 V8 的堆可以被观测，栈溢出可以被捕获。

const mb = (bytes) => (bytes / 1024 / 1024).toFixed(1);

console.log("== ① V8 的内存账单：process.memoryUsage() ==");
const m1 = process.memoryUsage();
console.log(`rss（进程总占用）:   ${mb(m1.rss)} MB`);
console.log(`heapTotal（V8 堆）:  ${mb(m1.heapTotal)} MB`);
console.log(`heapUsed（已使用）:  ${mb(m1.heapUsed)} MB`);
console.log(`external（堆外）:    ${mb(m1.external)} MB`);

console.log("\n== ② 分配一百万个对象，看堆增长 ==");
const arr = [];
for (let i = 0; i < 1_000_000; i++) arr.push({ id: i });
const m2 = process.memoryUsage();
console.log(`heapUsed 增长: ${mb(m2.heapUsed - m1.heapUsed)} MB   <- 对象全在 V8 堆上`);

console.log("\n== ③ 栈溢出：一个可以 catch 的 RangeError ==");
let depth = 0;
function recurse() {
  depth++;
  recurse();
}
try {
  recurse();
} catch (e) {
  console.log(`${e.constructor.name}: ${e.message}，深度 = ${depth}`);
}

console.log("\n== ④ 闭包：让\"局部变量\"活过函数返回 ==");
function makeCounter() {
  let count = 0; // 本该随函数返回消失——但被闭包捕获，逃到了堆上
  return () => ++count;
}
const counter = makeCounter();
counter();
console.log(`makeCounter 早已返回，count 却还活着: counter() = ${counter()}`);
console.log("（被闭包捕获的变量不在栈上——这是第 13 章闭包的内存真相）");
