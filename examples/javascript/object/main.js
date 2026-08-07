// 第 24 章 · 对象 —— JavaScript 示例
// 运行：node main.js

console.log("=== 1. 对象的形状：隐藏类的基础 ===");
{
  // ✅ 形状一致 —— 引擎可以给它们生成同一个隐藏类
  const a = { x: 1, y: 2 };
  const b = { x: 3, y: 4 };
  console.log("  a =", a, " 属性顺序:", Object.keys(a));
  console.log("  b =", b, " 属性顺序:", Object.keys(b));
  console.log("  → 形状一致，可以共享隐藏类，属性访问接近固定偏移");

  // ❌ 顺序不同 —— 产生不同的隐藏类
  const c = { y: 4, x: 3 };
  console.log("\n  c =", c, " 属性顺序:", Object.keys(c));
  console.log("  → 属性相同但顺序不同，隐藏类不同，无法共享优化");

  // ❌ 后加属性 —— 触发隐藏类变更
  const d = {};
  d.x = 1;
  d.y = 2;
  console.log("\n  d 分两步添加属性 →", Object.keys(d));
  console.log("  → 每次添加都可能触发隐藏类变更（transition）");
  console.log("  → 实践建议：在构造函数里一次性初始化所有属性，并保持顺序一致");
}

console.log("\n=== 2. ⚠️ 原型链查找：属性找不到就往上找 ===");

class Animal {
  constructor(name) {
    this.name = name;
  }
  speak() {
    return "...";
  }
}

class Dog extends Animal {
  bark() {
    return "汪";
  }
}

const dog = new Dog("旺财");

console.log("  dog 自己有 bark 吗？   ", Object.hasOwn(dog, "bark"));
console.log("  Dog.prototype 有吗？   ", Object.hasOwn(Dog.prototype, "bark"));
console.log("  dog 自己拥有什么？     ", Object.getOwnPropertyNames(dog), " ← 只有数据");

console.log("\n  完整的原型链：");
let cur = dog;
let depth = 0;
while (cur !== null) {
  const props = Object.getOwnPropertyNames(cur).filter((p) => p !== "constructor");
  const label = depth === 0 ? "dog 实例" : cur.constructor?.name + ".prototype";
  const shown = props.length > 6 ? props.slice(0, 6).join(", ") + ", ..." : props.join(", ");
  console.log(`    第 ${depth} 层: ${label.padEnd(20)} [${shown}]`);
  cur = Object.getPrototypeOf(cur);
  depth++;
}
console.log("  → dog.speak() 要往上找 2 层才找到，这就是原型链查找的代价");
console.log("  → 越深的查找越慢，所以引擎用「内联缓存」记住上次找到的位置");

console.log("\n=== 3. 方法只存一份，实例只存数据 ===");
{
  const d1 = new Dog("A");
  const d2 = new Dog("B");
  console.log("  d1 拥有:", Object.getOwnPropertyNames(d1));
  console.log("  d2 拥有:", Object.getOwnPropertyNames(d2));
  console.log("  d1.bark === d2.bark ?", d1.bark === d2.bark, " ← 同一个函数对象");
  console.log("  → 创建一百万个对象，不会产生一百万份方法代码（第 23 章）");
}

console.log("\n=== 4. delete 的代价：可能让对象退化到字典模式 ===");
{
  const obj = { x: 1, y: 2, z: 3 };
  console.log("  原始对象:", Object.keys(obj));

  const clone = { ...obj };
  delete clone.y; // ⚠️ 改变了对象形状
  console.log("  delete clone.y 后:", Object.keys(clone));
  console.log("    'y' in clone →", "y" in clone);

  const clone2 = { ...obj };
  clone2.y = undefined; // ✓ 形状不变
  console.log("  clone2.y = undefined 后:", Object.keys(clone2));
  console.log("    'y' in clone2 →", "y" in clone2, " ← 属性还在，只是值为 undefined");
  console.log("  → 热点代码里设为 undefined 通常比 delete 快");
  console.log("  → 除非你确实需要 in 运算符返回 false");
}

console.log("\n=== 5. 属性访问的三种实现（跨语言对比）===");
console.log("  ① 固定偏移   C++/Java/C#  →  [对象地址 + 4] 读 4 字节    最快");
console.log("  ② 哈希查找   Python __dict__ / JS 字典模式              中等");
console.log("  ③ 原型链上溯 JavaScript                                 取决于深度");
console.log("  → JS 引擎用隐藏类努力把 ③ 优化成接近 ①");

console.log("\n=== 6. 对象与 Map 的选择 ===");
{
  // 属性名固定、数量少 → 用对象（引擎能优化成隐藏类）
  const fixed = { x: 1, y: 2 };

  // 键会频繁增删、键不是字符串 → 用 Map
  const dynamic = new Map();
  dynamic.set("a", 1);
  dynamic.set({ obj: "key" }, 2); // 对象也能当键
  dynamic.delete("a"); // 频繁增删是 Map 的强项

  console.log("  固定形状用对象:", fixed);
  console.log("  频繁增删用 Map: size =", dynamic.size, "，且键可以是任意类型");
  console.log("  → 对象适合「记录」，Map 适合「字典」");
}

console.log("\n=== 7. 小结 ===");
console.log("  · JS 对象在引擎里有两种模式：隐藏类（快）和字典（慢）");
console.log("  · 保持形状一致 = 让引擎能共享隐藏类 = 更快");
console.log("  · 属性查找会沿原型链上溯，越深越慢");
console.log("  · 这是「动态语义 + 引擎努力优化成固定布局」的妥协产物");
