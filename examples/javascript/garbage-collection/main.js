// 垃圾回收：V8 的世界里你只有三个观察窗——heapUsed、WeakRef、FinalizationRegistry。

const mb = (b) => (b / 1024 / 1024).toFixed(1);

console.log("== ① GC 在自动工作：heapUsed 的呼吸 ==");
const samples = [];
for (let round = 0; round < 5; round++) {
  for (let i = 0; i < 500_000; i++) {
    const tmp = { name: "t", payload: new Array(8).fill(round) };
    if (tmp.payload.length === 0) console.log();
  }
  samples.push(process.memoryUsage().heapUsed);
}
console.log(`五轮各 50 万临时对象后的 heapUsed: ${samples.map(mb).join(" / ")} MB`);
console.log("（没有持续上涨——Scavenger 在每轮之间悄悄收走了尸体）");

console.log("\n== ② WeakRef：不挽留的引用 ==");
let student = { name: "小明", payload: new Array(1000).fill(0) };
const weak = new WeakRef(student);
console.log(`对象活着: weak.deref()?.name = ${weak.deref()?.name}`);
console.log("（strong 引用断掉后何时回收由 V8 决定——确定性演示见章节 --expose-gc 实测）");

console.log("\n== ③ FinalizationRegistry：死亡通知簿 ==");
const registry = new FinalizationRegistry((who) => {
  console.log(`    [讣告] ${who} 已被回收`);
});
registry.register(student, "小明的对象");
student = null; // 斩断强引用——通知何时送达同样由 V8 决定
console.log("已注册死亡通知并斩断强引用（通知是否在进程退出前送达，规范不保证）");

console.log("\n== ④ WeakMap：给别人的对象贴标签，而不延长它的命 ==");
const metadata = new WeakMap();
let user = { id: 1 };
metadata.set(user, { lastSeen: "2026-08-10" });
console.log(`metadata.get(user).lastSeen = ${metadata.get(user).lastSeen}`);
user = null; // 键的最后强引用断掉——条目自动有资格被回收
console.log("user = null 之后：WeakMap 里的条目不再阻止回收——缓存不成为泄漏（第 33 章的药方）");
