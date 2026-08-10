// 堆内存：V8 的分配便宜、回收自动——但"引用还在"依旧是泄漏。

const mb = (bytes) => (bytes / 1024 / 1024).toFixed(1);
const used = () => process.memoryUsage().heapUsed;

console.log("== ① 分配一千万个对象的价格 ==");
function allocate(n) {
  let sum = 0;
  for (let i = 0; i < n; i++) {
    const s = { name: "s", score: i };
    sum += s.score;
  }
  return sum;
}
allocate(2_000_000); // 预热
const t0 = process.hrtime.bigint();
allocate(10_000_000);
const t1 = process.hrtime.bigint();
const ms = Number(t1 - t0) / 1e6;
console.log(`总耗时 ${ms.toFixed(1)} ms，平均每个对象约 ${((ms * 1e6) / 1e7).toFixed(1)} ns`);
console.log("（新生代 bump 分配：指针加一下——朝生夕死的对象由 Scavenger 整批回收）");

console.log("\n== ② A/B 实验：同样的分配，留不留引用天差地别 ==");
const beforeA = used();
for (let r = 0; r < 10; r++) allocate(1_000_000);      // A：一千万个临时对象
const afterA = used();
console.log(`A. 不留引用: 堆增长 ${mb(afterA - beforeA)} MB   <- GC 边分配边收走`);

const retained = [];
const beforeB = used();
for (let r = 0; r < 10; r++) {
  for (let i = 0; i < 100_000; i++) retained.push({ name: "s", score: i });
}
const afterB = used();
console.log(`B. 全部留引用（一百万个）: 堆增长 ${mb(afterB - beforeB)} MB   <- 引用在，GC 无能为力`);

console.log("\n== ③ 泄漏的日常形态 ==");
console.log("全局数组/Map 只进不出、事件监听器忘了解绑、setInterval 忘了 clear——");
console.log("全都是「B 实验」的变体：不是 GC 失灵，是你还攥着引用");
