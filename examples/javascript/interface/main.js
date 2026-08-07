// 第 28 章 · 接口 —— JavaScript 示例
// 运行：node main.js
// JS 没有 interface 关键字 —— 契约靠约定、运行时检查，或 TypeScript

console.log("=== 1. 纯约定：最常见的做法 ===");
{
  // 约定：任何 Storage 都要有 save 方法
  class ReportService {
    constructor(storage) {
      this.storage = storage; // 只要有 save 就行
    }
    generate(content) {
      return this.storage.save(`报表[${content}]`);
    }
  }

  const fileStorage = { save: (d) => `写入文件: ${d}` };
  const s3Storage = { save: (d) => `上传到 S3: ${d}` };
  class MemoryStorage {
    constructor() {
      this.items = [];
    }
    save(d) {
      this.items.push(d);
      return `存进内存: ${d}`;
    }
  }

  console.log("  同一个 ReportService，换不同的 Storage：");
  for (const s of [fileStorage, s3Storage, new MemoryStorage()]) {
    const name = s.constructor === Object ? "普通对象" : s.constructor.name;
    console.log(`    ${name.padEnd(14)} → ${new ReportService(s).generate("月度")}`);
  }
  console.log("  → 契约完全是隐式的：只要有 save 方法就能用");
  console.log("  → 连普通对象字面量都行，不需要是类的实例");

  console.log("\n  用内存实现做单元测试：");
  const mem = new MemoryStorage();
  const svc = new ReportService(mem);
  svc.generate("一月");
  svc.generate("二月");
  console.log(`    生成 2 份报表后，内存里有 ${mem.items.length} 条`);
  console.log("  → 依赖倒置在 JS 里天然成立，因为根本没有类型检查挡着");
}

console.log("\n=== 2. 运行时检查：把隐式契约变显式 ===");
{
  function assertStorage(obj) {
    if (typeof obj?.save !== "function") {
      throw new TypeError("需要实现 save(data) 方法");
    }
    return obj;
  }

  console.log("  assertStorage({ save: fn })  →", typeof assertStorage({ save: () => {} }));
  try {
    assertStorage({ write: () => {} }); // 方法名不对
  } catch (e) {
    console.log("  assertStorage({ write: fn }) →", e.constructor.name + ":", e.message);
  }
  console.log("  ⚠️ 但这只能检查「有没有这个方法」，检查不了参数和返回值");
  console.log("  → 与 Python @runtime_checkable Protocol 的局限完全一样");
}

console.log("\n=== 3. 语言内置的隐式契约（协议）===");
{
  class Range {
    constructor(start, end) {
      this.start = start;
      this.end = end;
    }
    *[Symbol.iterator]() {
      // 实现迭代协议
      for (let i = this.start; i < this.end; i++) yield i;
    }
  }

  const r = new Range(1, 5);
  console.log("  实现 [Symbol.iterator] 后：");
  console.log(`    [...new Range(1,5)]      = [${[...r]}]`);
  console.log(`    for...of 能用            = ${(() => { let s = ""; for (const x of new Range(1, 4)) s += x; return s; })()}`);
  console.log(`    Array.from(new Range())  = [${Array.from(new Range(1, 4))}]`);

  class Money {
    constructor(amount) {
      this.amount = amount;
    }
    toJSON() {
      return { value: this.amount, currency: "CNY" }; // 序列化协议
    }
    toString() {
      return `¥${this.amount}`; // 字符串化协议
    }
  }
  const m = new Money(99);
  console.log("\n  实现 toJSON / toString 后：");
  console.log(`    JSON.stringify(m) = ${JSON.stringify(m)}`);
  console.log(`    \`${"${m}"}\`          = ${`${m}`}`);

  console.log("\n  JS 的内置协议：");
  console.log("    可迭代      [Symbol.iterator]        for...of、展开语法");
  console.log("    序列化      toJSON()                 JSON.stringify");
  console.log("    字符串化    toString()               字符串拼接");
  console.log("    异步迭代    [Symbol.asyncIterator]   for await...of");
  console.log("  → 实现约定的方法就能接入语言特性 —— 这是 JS 版的结构化契约");
}

console.log("\n=== 4. 用 Symbol 定义自己的协议 ===");
{
  const Serializable = Symbol("Serializable");

  class User {
    constructor(name) {
      this.name = name;
    }
    [Serializable]() {
      return `User(${this.name})`;
    }
  }

  class Product {
    constructor(title) {
      this.title = title;
    }
    [Serializable]() {
      return `Product(${this.title})`;
    }
  }

  class Plain {} // 没实现协议

  function serialize(obj) {
    if (typeof obj[Serializable] !== "function") {
      throw new TypeError("未实现 Serializable 协议");
    }
    return obj[Serializable]();
  }

  console.log(`  serialize(new User("Alice"))  = ${serialize(new User("Alice"))}`);
  console.log(`  serialize(new Product("书"))   = ${serialize(new Product("书"))}`);
  try {
    serialize(new Plain());
  } catch (e) {
    console.log(`  serialize(new Plain())        → ${e.message}`);
  }
  console.log("  → 用 Symbol 做键，避免与普通方法名冲突");
  console.log("  → 这是 JS 里定义自定义协议的推荐做法");
}

console.log("\n=== 5. TypeScript 的 interface（结构化，编译期）===");
console.log("  interface Storage {");
console.log("    save(data: string): string;");
console.log("  }");
console.log();
console.log("  class FileStorage {              // ⚠️ 注意：不需要写 implements");
console.log("    save(data: string) { return `写入: ${data}`; }");
console.log("  }");
console.log();
console.log("  const s: Storage = new FileStorage();   // ✓ 结构匹配就通过");
console.log();
console.log("  ⚠️ TypeScript 的接口是纯编译期的：");
console.log("     编译成 JavaScript 后完全消失，运行时不存在任何接口检查");
console.log("     if (obj instanceof Storage)  → 编译错误：interface 不是运行时的值");
console.log("  → 这与 Java 的接口有本质区别（Java 的接口运行时是存在的）");

console.log("\n=== 6. 各语言的接口检查时机对比 ===");
console.log("  语言              契约风格      检查时机        运行时是否存在");
console.log("  Java / C#         名义化        编译期          ✅ 可以 instanceof");
console.log("  C++ 纯虚类        名义化        编译期          ✅ 有 vtable");
console.log("  C++20 Concept     结构化        编译期          ❌ 编译后无痕");
console.log("  Python Protocol   结构化        静态检查         ❌ 运行时不存在");
console.log("  TypeScript        结构化        编译期          ❌ 编译后消失");
console.log("  JavaScript        —            运行时/无        —");
console.log("  → 契约在哪个阶段检查，决定了它有没有运行时代价（第 27 章同理）");

console.log("\n=== 7. 小结 ===");
console.log("  · JS 没有 interface —— 契约靠约定和文档维系，没有任何强制力");
console.log("  · 依赖倒置在 JS 里天然成立：任何有对应方法的对象都能注入");
console.log("  · 语言内置协议（Symbol.iterator / toJSON）是 JS 版的结构化契约");
console.log("  · 自定义协议推荐用 Symbol 做键，避免命名冲突");
console.log("  · ⚠️ 团队协作请上 TypeScript，否则契约只存在于口头约定里");
