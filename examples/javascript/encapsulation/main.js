// 第 25 章 · 封装 —— JavaScript 示例
// 运行：node main.js
// JS 的 # 私有字段是这几门语言里最强的私有性（ES2022）

console.log("=== 1. 不封装的后果：校验形同虚设 ===");
{
  class BadAccount {
    balance = 100; // ⚠️ 公开字段
    deposit(n) {
      if (n <= 0) throw new Error("金额必须为正");
      this.balance += n;
    }
  }

  const acc = new BadAccount();
  try {
    acc.deposit(-50);
  } catch (e) {
    console.log("  acc.deposit(-50)  →", e.message, " ← 正门的校验生效");
  }
  acc.balance = -999; // 直接绕过
  console.log("  acc.balance = -999 → 余额变成", acc.balance, " ← 从墙上的洞进来了");
  console.log("  → 校验只在「正门」生效，公开字段等于在墙上开了个洞");
}

console.log("\n=== 2. 三代私有方案 ===");

console.log("\n  ① 下划线约定：纯靠自觉");
{
  class Account {
    _balance = 100;
  }
  console.log("    new Account()._balance =", new Account()._balance, " ← 照样能访问");
}

console.log("\n  ② 闭包：真私有，但每个实例一份方法");
{
  function createAccount() {
    let balance = 100; // 闭包变量，外部拿不到（第 13 章）
    return {
      getBalance: () => balance,
      deposit(n) {
        if (n <= 0) throw new Error("金额必须为正");
        balance += n;
      },
    };
  }
  const a1 = createAccount();
  const a2 = createAccount();
  a1.deposit(50);
  console.log("    a1.getBalance() =", a1.getBalance(), "  a2.getBalance() =", a2.getBalance());
  console.log("    a1.deposit === a2.deposit ?", a1.deposit === a2.deposit,
    " ← 每个实例一份函数（内存代价）");
}

console.log("\n  ③ # 私有字段（ES2022）：语法层面的真私有");

class Account {
  #balance = 100; // 真私有
  _internal = "约定私有"; // 只是约定

  getBalance() {
    return this.#balance;
  }
  deposit(n) {
    if (n <= 0) throw new Error("金额必须为正");
    this.#balance += n;
  }
  #validate(n) {
    return n > 0; // 私有方法
  }
  static #instances = 0; // 静态私有字段
  static isAccount(obj) {
    return #balance in obj; // ES2022：用 in 检测「是不是自己人」
  }
}

const a = new Account();
console.log("    a.getBalance() =", a.getBalance());
console.log("    a.deposit === new Account().deposit ?",
  a.deposit === new Account().deposit, " ← 方法在原型上，只存一份");

console.log("\n=== 3. ⚠️ 实测：所有反射手段都看不到 #balance ===");
console.log("  Object.keys(a)                =", Object.keys(a));
console.log("  Object.getOwnPropertyNames(a) =", Object.getOwnPropertyNames(a));
console.log("  Reflect.ownKeys(a)            =", Reflect.ownKeys(a));
console.log("  JSON.stringify(a)             =", JSON.stringify(a));
try {
  eval("a.#balance");
} catch (e) {
  console.log("  在类外写 a.#balance           →", e.constructor.name, "（根本编译不过）");
}
console.log("  → 这是这几门语言里唯一「语法层面禁止」的私有");
console.log("  → 对比：Java/C# 反射能破，C++ 内存能破，Python 改个名就能拿到");

console.log("\n  为什么 JS 反而最严格？");
console.log("    因为 # 是 2022 年才加入的新特性，设计者有机会从零做对；");
console.log("    而 Java 的反射早已是生态基石（DI/序列化/ORM），无法收回。");

console.log("\n=== 4. ⚠️ 坑：# 字段不会被 JSON.stringify 序列化 ===");
{
  class WithPrivate {
    #secret = 42;
    public_field = "看得见";
  }
  console.log("  JSON.stringify(new WithPrivate()) =", JSON.stringify(new WithPrivate()));
  console.log("  → #secret 的数据丢了！这是从下划线迁移到 # 时最容易踩的坑");

  class WithToJSON {
    #secret = 42;
    public_field = "看得见";
    toJSON() {
      return { public_field: this.public_field, secret: this.#secret };
    }
  }
  console.log("  加了 toJSON() 后                  =", JSON.stringify(new WithToJSON()));
}

console.log("\n=== 5. #balance in obj：检测是不是自己人 ===");
console.log("  Account.isAccount(a)     =", Account.isAccount(a));
console.log("  Account.isAccount({})    =", Account.isAccount({}), " ← 不抛错，返回 false");

console.log("\n=== 6. getter / setter 与计算属性 ===");
{
  class Temperature {
    #celsius = 0;
    get celsius() {
      return this.#celsius;
    }
    set celsius(v) {
      if (v < -273.15) throw new RangeError("低于绝对零度");
      this.#celsius = v;
    }
    get fahrenheit() {
      return (this.#celsius * 9) / 5 + 32; // 派生数据，不占存储
    }
  }

  const t = new Temperature();
  t.celsius = 25; // 看起来像赋值，实际调用了 setter
  console.log("  t.celsius = 25 后:");
  console.log("    t.celsius    =", t.celsius);
  console.log("    t.fahrenheit =", t.fahrenheit, " ← 计算属性，永远不会不一致");
  try {
    t.celsius = -300;
  } catch (e) {
    console.log("    t.celsius = -300 →", e.constructor.name + ":", e.message);
  }
}

console.log("\n=== 7. ⚠️ 封装泄漏：字段私有，但引用漏出去了 ===");
{
  class BadRoster {
    #items = ["Alice", "Bob"];
    getItems() {
      return this.#items; // ✗ 返回了内部数组本身
    }
  }
  class GoodRoster {
    #items = ["Alice", "Bob"];
    getItems() {
      return [...this.#items]; // ✓ 返回拷贝
    }
    get size() {
      return this.#items.length;
    }
  }

  const bad = new BadRoster();
  bad.getItems().push("入侵者");
  console.log("  BadRoster:  外部 push 后内部变成", bad.getItems());

  const good = new GoodRoster();
  good.getItems().push("入侵者");
  console.log("  GoodRoster: 外部 push 后内部仍是", good.getItems());
  console.log("  → 这是最隐蔽的封装泄漏：字段是私有的，但可变引用漏出去了");
}

console.log("\n=== 8. 暴露操作，而不是暴露状态 ===");
console.log("  ❌ setBalance(n)  —— 只是把字段赋值包了一层，没有任何价值");
console.log("  ✅ deposit(n) / withdraw(n) —— 表达业务意图，且能保护不变式");
console.log("  → 设计时先问「调用方需要做什么」，而不是「这个对象有什么数据」");
