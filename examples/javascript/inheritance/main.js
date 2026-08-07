// 第 26 章 · 继承 —— JavaScript 示例
// 运行：node main.js
// JS 只有单继承，底层仍是原型链（第 24 章）；用 mixin 弥补多继承

console.log("=== 1. 基本继承与 super ===");

class Animal {
  constructor(name) {
    this.name = name;
  }
  speak() {
    return `${this.name} 发出声音`;
  }
}

class Dog extends Animal {
  constructor(name, breed) {
    super(name); // ⚠️ 必须先调 super 才能用 this
    this.breed = breed;
  }
  speak() {
    return super.speak() + "：汪！";
  }
}

const d = new Dog("旺财", "柴犬");
console.log("  new Dog('旺财','柴犬').speak() =", d.speak());
console.log("  d instanceof Dog    =", d instanceof Dog);
console.log("  d instanceof Animal =", d instanceof Animal);

console.log("\n=== 2. 继承的底层仍是原型链（第 24 章）===");
let cur = d;
let depth = 0;
while (cur !== null) {
  const props = Object.getOwnPropertyNames(cur).filter((p) => p !== "constructor");
  const label = depth === 0 ? "d 实例" : `${cur.constructor?.name}.prototype`;
  const shown = props.length > 5 ? props.slice(0, 5).join(", ") + ", ..." : props.join(", ");
  console.log(`  第 ${depth} 层: ${label.padEnd(20)} [${shown}]`);
  cur = Object.getPrototypeOf(cur);
  depth++;
}
console.log("  → speak() 在 Dog.prototype 上找到，super.speak() 再往上找到 Animal.prototype");

console.log("\n=== 3. ⚠️ 必须先调 super 才能用 this ===");
{
  class Broken extends Animal {
    constructor(name) {
      try {
        this.x = 1; // ✗ 在 super() 之前使用 this
      } catch (e) {
        console.log("  在 super() 之前写 this.x →", e.constructor.name);
        console.log("    (" + e.message + ")");
      }
      super(name);
    }
  }
  new Broken("测试");
  console.log("  → 子类构造函数里，this 在 super() 调用之后才存在");
}

console.log("\n=== 4. JS 没有多继承，用 mixin 弥补 ===");

const Serializable = (Base) =>
  class extends Base {
    toJSON() {
      return { ...this };
    }
  };

const Comparable = (Base) =>
  class extends Base {
    equals(other) {
      return this.id === other.id;
    }
  };

class Entity {
  constructor(id) {
    this.id = id;
  }
}

class User extends Serializable(Comparable(Entity)) {
  constructor(id, name) {
    super(id);
    this.name = name;
  }
}

const u1 = new User(1, "Alice");
const u2 = new User(1, "Bob");
console.log("  class User extends Serializable(Comparable(Entity))");
console.log("    u1.toJSON()      =", u1.toJSON(), "  ← 来自 Serializable");
console.log("    u1.equals(u2)    =", u1.equals(u2), "        ← 来自 Comparable");
console.log("  mixin 后的原型链长度:");
let c2 = Object.getPrototypeOf(u1);
let n = 0;
while (c2 !== null) {
  n++;
  c2 = Object.getPrototypeOf(c2);
}
console.log(`    ${n} 层`);
console.log("  → mixin 的本质是「把继承链拉长」，不是真正的多继承");
console.log("  → 所以不会有菱形问题，但叠加顺序会影响方法覆盖关系");

console.log("\n=== 5. ⚠️ 里氏替换原则：正方形 is-a 长方形？ ===");
{
  class Rectangle {
    constructor(w, h) {
      this._w = w;
      this._h = h;
    }
    get width() {
      return this._w;
    }
    set width(v) {
      this._w = v;
    }
    get height() {
      return this._h;
    }
    set height(v) {
      this._h = v;
    }
    get area() {
      return this._w * this._h;
    }
  }

  class Square extends Rectangle {
    constructor(side) {
      super(side, side);
    }
    set width(v) {
      this._w = this._h = v; // 改宽必须同时改高
    }
    set height(v) {
      this._w = this._h = v;
    }
    get width() {
      return this._w;
    }
    get height() {
      return this._h;
    }
  }

  function stretch(rect) {
    rect.width = 4;
    rect.height = 5;
    return rect.area; // 任何长方形都该返回 20
  }

  console.log("  stretch: 把宽设成 4、高设成 5，期望面积 = 20");
  console.log("    Rectangle(2, 3) → 面积 =", stretch(new Rectangle(2, 3)), " ✓");
  console.log("    Square(2)       → 面积 =", stretch(new Square(2)), " ✗ 期望 20！");
  console.log("  → Square 破坏了 Rectangle「宽高独立可变」的行为约定");
  console.log("  → 数学上的 is-a 不等于代码里的 is-a");
}

console.log("\n=== 6. 组合优于继承 ===");
{
  // ✗ 继承：CountingSet 被迫「是一个」Set
  class CountingSetBad extends Set {
    constructor() {
      super();
      this.addCount = 0;
    }
    add(v) {
      this.addCount++;
      return super.add(v);
    }
  }

  // ✓ 组合：只依赖 Set 的公开接口
  class CountingSetGood {
    #inner = new Set();
    #addCount = 0;
    add(v) {
      this.#addCount++;
      this.#inner.add(v);
      return this;
    }
    addAll(items) {
      for (const v of items) {
        this.#addCount++;
        this.#inner.add(v);
      }
      return this;
    }
    get addCount() {
      return this.#addCount;
    }
    get size() {
      return this.#inner.size;
    }
  }

  const bad = new CountingSetBad();
  ["x", "y", "z"].forEach((v) => bad.add(v));
  const good = new CountingSetGood().addAll(["x", "y", "z"]);
  console.log("  继承版本: add 3 次 → addCount =", bad.addCount);
  console.log("  组合版本: addAll 3 个 → addCount =", good.addCount);
  console.log();
  console.log("  ⚠️ 继承版本还有个隐患：new Set(...) 的构造过程也会调用 add");
  const bad2 = new CountingSetBad();
  console.log("    构造时 addCount =", bad2.addCount, "（这次没问题，但取决于引擎实现）");
  console.log("  → 组合版本不依赖任何实现细节，且不会被当成 Set 到处传递");
}

console.log("\n=== 7. 继承内置类型的注意事项 ===");
{
  class MyArray extends Array {
    last() {
      return this[this.length - 1];
    }
  }
  const arr = MyArray.from([1, 2, 3]);
  console.log("  class MyArray extends Array");
  console.log("    MyArray.from([1,2,3]).last() =", arr.last());
  console.log("    arr instanceof MyArray =", arr instanceof MyArray);

  class MyError extends Error {
    constructor(msg) {
      super(msg);
      this.name = "MyError"; // ⚠️ 必须手动设置，否则 name 是 "Error"
    }
  }
  const e = new MyError("出错了");
  console.log("  class MyError extends Error");
  console.log(`    e.name = "${e.name}"  ← 必须在构造函数里手动设置`);
  console.log("  ⚠️ 继承内置类型在旧环境或经过某些转译后行为会不正常");
  console.log("     转译器难以完全模拟原生构造过程");
}

console.log("\n=== 8. 小结 ===");
console.log("  · JS 只有单继承，底层是原型链（第 24 章）");
console.log("  · 子类构造函数里 this 必须在 super() 之后才能用");
console.log("  · mixin 用「函数返回类」叠加能力，本质是拉长原型链，不是多继承");
console.log("  · 里氏替换：实测 Square 让 stretch 算出 25 而非 20");
console.log("  · JS 没有 final/sealed，无法阻止继承；用组合表达「不是 is-a」");
