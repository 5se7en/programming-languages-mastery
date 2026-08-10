// 智能指针：JS 只有一种引用（相当于 shared_ptr），外加 WeakRef/WeakMap 两件弱引用工具。

console.log("== ① JS 的引用 ≈ shared_ptr（GC 追踪，不计数）==");
let student = { name: "小明" };
const weak = new WeakRef(student);
console.log(`    强引用在: weak.deref()?.name = ${weak.deref()?.name}`);
console.log("    （没有 delete、没有 use_count——引用即所有权，且是共享的）");

console.log("\n== ② 钥匙实验：同样的环，JS 毫无压力 ==");
function makeCycle() {
  const x = { name: "环-甲" };
  const y = { name: "环-乙" };
  x.partner = y;
  y.partner = x; // 成环（C++ 在这里泄漏）
  return [new WeakRef(x), new WeakRef(y)];
}
const [wx, wy] = makeCycle();
console.log(`    成环后两个 WeakRef 已建立（deref 现在可能仍可达）`);
console.log("    （追踪式 GC 从根出发标记——环内互指再紧，根到不了就是垃圾）");
console.log("    （确定性验证见章节 --expose-gc 实测：跨事件循环轮次后双双 undefined）");

console.log("\n== ③ WeakRef ≈ weak_ptr，deref() ≈ lock() ==");
console.log("    weak_ptr::lock() -> shared_ptr（空或非空）");
console.log("    WeakRef.deref()  -> 对象或 undefined   <- 同一个模式：不挽留，但安全询问");

console.log("\n== ④ WeakMap：C++ 没有的东西 ==");
const metadata = new WeakMap();
let user = { id: 1 };
metadata.set(user, { role: "admin" });
console.log(`    metadata.get(user).role = ${metadata.get(user).role}`);
user = null;
console.log("    user = null 后条目自动可回收——「给别人的对象贴标签而不续命」");
console.log("    （C++ 要自己用 map<weak_ptr, T> + 定期清理才能模拟）");

console.log("\n== ⑤ JS 缺席的两样东西 ==");
console.log("    唯一所有权：无法表达「只有我能释放」——赋值即共享（第 35 章）");
console.log("    确定性释放：对象何时死由 GC 决定（第 36 章实测）");
console.log("    非内存资源只能靠 try/finally 手动界定（第 37 章实测：JS 是唯一缺席者）");
